# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""SketchUp's tools, working the way SketchUp's do.

Everything drawn outside groups is one connected model (one mesh, "Model",
per layer): edges and faces stick together, a closed loop makes a face, a
line across a face splits it, Push/Pull grows or cuts the solid. Groups (G)
are separate objects; double-click one with Select to edit inside it.

Every drawing tool uses the inference engine (sk_infer): Endpoint, Midpoint,
On Edge, On Face, Intersection, Center, the red/green/blue axes, parallel and
perpendicular to edges, From Point; arrow keys lock an axis, Shift locks the
current inference. Type values at any time (Measurements box, shown in the
header) and press Enter.

The geometry itself is done by plain functions in sk_geom; the operators
below only read the mouse and call them.
"""

import math
import time
import traceback
from contextlib import contextmanager

import bmesh
import bpy
import numpy as np
from bpy.props import IntProperty
from mathutils import Matrix, Vector

from . import geometry as g
from . import sk_draw as sd
from . import sk_geom as sg
from . import sk_infer as si

Z = Vector((0, 0, 1))
WHITE = (1, 1, 1, 1)
BAND = (0.1, 0.1, 0.1, 1.0)
LAST = {"pushpull": None, "offset": None}
_ACTIVE = {"token": None}


# ------------------------------------------------------------------ the model and groups

def _layer(context):
    from . import layers
    return layers.active_layer(context)


def model_object(context, create=True):
    """The loose geometry of the active layer: one mesh object."""
    layer = _layer(context)
    for obj in layer.objects:
        if obj.type == "MESH" and obj.get("sketch_kind") == "model":
            return obj
    if not create:
        return None
    me = bpy.data.meshes.new("Model")
    me.materials.append(g.mat("wall"))
    obj = bpy.data.objects.new("Model", me)
    obj["sketch_kind"] = "model"
    layer.objects.link(obj)
    return obj


def context_object(context):
    """The group being edited (double-clicked), if any."""
    name = context.scene.get("sketch_context")
    if not name:
        return None
    obj = bpy.data.objects.get(name)
    if obj is None or obj.type != "MESH":
        try:
            del context.scene["sketch_context"]
        except Exception:
            pass
        return None
    return obj


def target_object(context, create=True):
    return context_object(context) or model_object(context, create)


def editable_objects(context):
    """Objects whose geometry the tools edit directly: the open group, else every layer's model."""
    c = context_object(context)
    if c is not None:
        return [c]
    out = []
    for obj in context.scene.objects:
        if obj.type == "MESH" and obj.get("sketch_kind") == "model":
            out.append(obj)
    return out


def is_editable(context, obj):
    return obj is not None and obj in editable_objects(context)


def top_group(context, obj):
    """The outermost group containing obj that is not open (what a click on it selects)."""
    ctx = context_object(context)
    cur = obj
    while cur is not None and cur.parent is not None and cur.parent is not ctx:
        cur = cur.parent
    return cur


@contextmanager
def edit_mesh(obj):
    """bmesh of obj, written back (and the views refreshed) when the block ends without error."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    sg.soft_layer(bm)
    sg.ensure_lookup(bm)
    ok = False
    try:
        yield bm
        ok = True
    finally:
        if ok:
            bm.normal_update()
            bm.to_mesh(obj.data)
            obj.data.update()
        bm.free()
        sd.bump()


def to_local(obj, p):
    return obj.matrix_world.inverted() @ Vector(p)


def dir_to_local(obj, d):
    return obj.matrix_world.inverted().to_3x3() @ Vector(d)


def push_undo(name):
    try:
        bpy.ed.undo_push(message=name)
    except Exception:
        pass


# ------------------------------------------------------------------ picking helpers (also used by fabric.py)

def view_region(context):
    """The 3D viewport's main region, also when a tool was started from a sidebar button."""
    region = context.region
    if region is None or region.type != "WINDOW":
        region = next(r for r in context.area.regions if r.type == "WINDOW")
    return region, context.area.spaces.active.region_3d


def mouse_co(context, event):
    region, _ = view_region(context)
    return Vector((event.mouse_x - region.x, event.mouse_y - region.y))


def mouse_ray(context, event):
    from bpy_extras import view3d_utils
    region, rv3d = view_region(context)
    co = mouse_co(context, event)
    origin = view3d_utils.region_2d_to_origin_3d(region, rv3d, co)
    direction = view3d_utils.region_2d_to_vector_3d(region, rv3d, co)
    return origin, direction


def ray_hit(context, origin, direction):
    depsgraph = context.evaluated_depsgraph_get()
    ok, loc, normal, index, obj, _m = context.scene.ray_cast(depsgraph, origin, direction)
    if ok:
        return loc, normal.normalized(), obj, index
    return None, None, None, None


# ------------------------------------------------------------------ actions (what the tools do; also used by the tests)

def action_line(context, points, closed=False, obj=None):
    """Draw edges through world points into the model. Returns (edges, new faces)."""
    obj = obj or target_object(context)
    with edit_mesh(obj) as bm:
        res = sg.add_edges(bm, [to_local(obj, p) for p in points], closed=closed)
        out = (len(res.edges), len(res.faces))
    return out


def action_push_pull(context, obj, face_index, distance, keep=False):
    with edit_mesh(obj) as bm:
        f = bm.faces[face_index]
        f.normal_update()
        # the distance is along the face's world normal; convert to local length
        nw = (obj.matrix_world.to_3x3() @ f.normal)
        scale = nw.length if nw.length > 1e-12 else 1.0
        sg.push_pull(bm, f, distance / scale, keep=keep)


def action_erase(context, obj, edge_indices, soften=False):
    with edit_mesh(obj) as bm:
        sg.erase_edges(bm, [bm.edges[i] for i in edge_indices if i < len(bm.edges)], soften=soften)


def paint_material(rgb):
    hexname = "#%02X%02X%02X" % tuple(int(max(0, min(1, c)) * 255) for c in rgb)
    return g.material(f"Sketch paint {hexname}", tuple(rgb), 0.6)


def material_slot(obj, mat):
    me = obj.data
    if mat.name not in [m.name for m in me.materials if m]:
        me.materials.append(mat)
    return [m.name if m else "" for m in me.materials].index(mat.name)


def action_paint(context, obj, face_index, mat, mode="face"):
    slot = material_slot(obj, mat)
    with edit_mesh(obj) as bm:
        if face_index is None or mode == "object":
            faces = list(bm.faces)
        else:
            f = bm.faces[face_index]
            if mode == "same":       # Shift: every face with that material
                faces = [x for x in bm.faces if x.material_index == f.material_index]
            elif mode == "connected":  # Ctrl: connected faces with that material
                _v, _e, conn = sg.connected(bm, [f])
                faces = [x for x in conn if x.material_index == f.material_index]
            else:
                faces = sg.region_of(bm, f)
        for x in faces:
            x.material_index = slot


# ------------------------------------------------------------------ selection (SketchUp's, kept in the meshes' select flags)

class Selection:
    def __init__(self):
        self.items = {}    # object name -> (faces, edges, verts) index lists
        self.groups = []   # object names

    @property
    def empty(self):
        return not self.groups and not any(f or e or v for f, e, v in self.items.values())


def read_selection(context):
    sel = Selection()
    for obj in editable_objects(context):
        me = obj.data
        fs = np.zeros(len(me.polygons), dtype=bool)
        es = np.zeros(len(me.edges), dtype=bool)
        vs = np.zeros(len(me.vertices), dtype=bool)
        if len(fs):
            me.polygons.foreach_get("select", fs)
        if len(es):
            me.edges.foreach_get("select", es)
        if len(vs):
            me.vertices.foreach_get("select", vs)
        f, e, v = np.nonzero(fs)[0].tolist(), np.nonzero(es)[0].tolist(), np.nonzero(vs)[0].tolist()
        if f or e or v:
            sel.items[obj.name] = (f, e, v)
    ctx = context_object(context)
    for obj in context.selected_objects:
        if obj.get("sketch_kind") == "group" and obj is not ctx:
            sel.groups.append(obj.name)
    return sel


def clear_selection(context):
    for obj in editable_objects(context):
        me = obj.data
        for coll in (me.polygons, me.edges, me.vertices):
            n = len(coll)
            if n:
                coll.foreach_set("select", np.zeros(n, dtype=bool))
        me.update()
    for obj in list(context.selected_objects):
        obj.select_set(False)
    sd.bump()


def select_elements(context, obj, faces=(), edges=(), mode="add"):
    """mode: add, remove, toggle."""
    with edit_mesh(obj) as bm:
        for i in faces:
            f = bm.faces[i]
            f.select = (not f.select) if mode == "toggle" else (mode == "add")
        for i in edges:
            e = bm.edges[i]
            e.select = (not e.select) if mode == "toggle" else (mode == "add")


def select_group(context, obj, mode="add"):
    if mode == "toggle":
        obj.select_set(not obj.select_get())
    else:
        obj.select_set(mode == "add")
    if obj.select_get():
        context.view_layer.objects.active = obj
    sd.bump()


def selection_world_edges(context, sel, limit=4000):
    """World-space segments that show the selection while it is moved (ghost)."""
    segs = []
    for name, (f, e, _v) in sel.items.items():
        obj = bpy.data.objects.get(name)
        if obj is None:
            continue
        me = obj.data
        mw = obj.matrix_world
        eids = set(e)
        if f:
            ek = {tuple(sorted(k)) for i in f for k in me.polygons[i].edge_keys}
            for ed in me.edges:
                if tuple(sorted(ed.vertices)) in ek:
                    eids.add(ed.index)
        for i in list(eids)[:limit]:
            a, b = me.edges[i].vertices
            segs.append((mw @ me.vertices[a].co, mw @ me.vertices[b].co))
    for name in sel.groups:
        obj = bpy.data.objects.get(name)
        if obj is not None:
            c = sd._bbox_lines(obj)
            segs += [(Vector(c[i]), Vector(c[i + 1])) for i in range(0, len(c), 2)]
    return segs


def selection_bbox(context, sel):
    pts = [p for s in selection_world_edges(context, sel) for p in s]
    for name, (_f, _e, v) in sel.items.items():
        obj = bpy.data.objects.get(name)
        if obj is not None:
            pts += [obj.matrix_world @ obj.data.vertices[i].co for i in v]
    if not pts:
        return None
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return lo, hi


def transform_selection(context, sel, matrix, copy=False):
    """Apply a world matrix to the selection (loose geometry and groups). copy: Ctrl."""
    for name, (f, e, v) in sel.items.items():
        obj = bpy.data.objects.get(name)
        if obj is None:
            continue
        mw = obj.matrix_world
        local = mw.inverted() @ matrix @ mw
        with edit_mesh(obj) as bm:
            faces = [bm.faces[i] for i in f]
            edges = [bm.edges[i] for i in e]
            verts = [bm.verts[i] for i in v]
            if copy:
                nv, ne, nf = sg.duplicate(bm, verts, edges, faces)
                for x in faces + edges + verts:
                    x.select = False
                for x in nv + ne + nf:
                    x.select = True
                moved = nv
            else:
                moved = list(sg.selection_verts(verts, edges, faces))
            for vv in moved:
                vv.co = local @ vv.co
            if not copy:
                sg.weld(bm)
    group_names = []
    for name in sel.groups:
        obj = bpy.data.objects.get(name)
        if obj is None:
            continue
        if copy:
            dup = obj.copy()
            if obj.data is not None:
                dup.data = obj.data.copy()
            for c in obj.users_collection:
                c.objects.link(dup)
            dup.matrix_world = matrix @ obj.matrix_world
            obj.select_set(False)
            dup.select_set(True)
        else:
            obj.matrix_world = matrix @ obj.matrix_world
    sd.bump()
    return read_selection(context)


def copy_selection_array(context, sel, matrices):
    """More copies of the original selection (Move/Rotate arrays: 5x, /5)."""
    for m in matrices:
        transform_selection(context, sel, m, copy=True)


# ------------------------------------------------------------------ guides (Tape Measure)

def add_guide_line(context, p, d):
    lst = list(context.scene.get("sketch_guides_lines") or [])
    lst += [float(x) for x in tuple(p) + tuple(Vector(d).normalized())]
    context.scene["sketch_guides_lines"] = lst


def add_guide_point(context, p):
    lst = list(context.scene.get("sketch_guides_points") or [])
    lst += [float(x) for x in tuple(p)]
    context.scene["sketch_guides_points"] = lst


def guide_lines(context):
    lst = list(context.scene.get("sketch_guides_lines") or [])
    return [(Vector(lst[i:i + 3]), Vector(lst[i + 3:i + 6])) for i in range(0, len(lst) - 5, 6)]


def guide_points(context):
    lst = list(context.scene.get("sketch_guides_points") or [])
    return [Vector(lst[i:i + 3]) for i in range(0, len(lst) - 2, 3)]


def remove_guide_line(context, index):
    lst = list(context.scene.get("sketch_guides_lines") or [])
    del lst[index * 6:index * 6 + 6]
    context.scene["sketch_guides_lines"] = lst


# ------------------------------------------------------------------ snapshot for the inference engine

def build_snapshot(context, limit=300000):
    snap = si.Snapshot()
    total = 0
    editable = set(o.name for o in editable_objects(context))
    for obj in context.visible_objects:
        if obj.type != "MESH":
            continue
        me = obj.data
        nv = len(me.vertices)
        if nv == 0 or total + nv > limit:
            continue
        total += nv
        mw = np.array([list(r) for r in obj.matrix_world])
        co = np.empty(nv * 3, dtype=np.float32)
        me.vertices.foreach_get("co", co)
        co = co.reshape(-1, 3).astype(np.float64) @ mw[:3, :3].T + mw[:3, 3]
        ne = len(me.edges)
        ev = np.empty(ne * 2, dtype=np.int32)
        me.edges.foreach_get("vertices", ev)
        ev = ev.reshape(-1, 2)
        soft = np.zeros(ne, dtype=np.int32)
        at = me.attributes.get(sg.SOFT)
        if at is not None and at.domain == "EDGE" and len(at.data) == ne:
            try:
                at.data.foreach_get("value", soft)
            except Exception:
                pass
        hard = np.nonzero(soft == 0)[0]
        me.calc_loop_triangles()
        nt = len(me.loop_triangles)
        tv = np.empty(nt * 3, dtype=np.int32)
        tp = np.empty(nt, dtype=np.int32)
        if nt:
            me.loop_triangles.foreach_get("vertices", tv)
            me.loop_triangles.foreach_get("polygon_index", tp)
        npoly = len(me.polygons)
        pn = np.empty(npoly * 3, dtype=np.float32)
        if npoly:
            me.polygons.foreach_get("normal", pn)
        pn = pn.reshape(-1, 3).astype(np.float64)
        nm = np.array([list(r) for r in obj.matrix_world.to_3x3().inverted_safe().transposed()])
        pn = pn @ nm.T
        pn /= np.maximum(np.linalg.norm(pn, axis=1), 1e-12)[:, None]
        centers = []
        if npoly:
            lt = np.empty(npoly, dtype=np.int32)
            me.polygons.foreach_get("loop_total", lt)
            for i in np.nonzero(lt >= 8)[0][:500]:
                c = si.circle_center([Vector(co[vi]) for vi in me.polygons[int(i)].vertices])
                if c is not None:
                    centers.append(c)
        in_group = obj.name not in editable
        snap.add(co, ev[hard], tv.reshape(-1, 3), pn[tp] if nt else np.zeros((0, 3)), tp,
                 edge_ids=hard, obj=obj.name, in_group=in_group, centers=centers)
    snap.guide_points = guide_points(context)
    snap.guide_lines = guide_lines(context)
    return snap.finalize()


# ------------------------------------------------------------------ the tool base: one modal operator per tool

NAV_EVENTS = {"MIDDLEMOUSE", "WHEELUPMOUSE", "WHEELDOWNMOUSE", "WHEELINMOUSE", "WHEELOUTMOUSE",
              "TRACKPADPAN", "TRACKPADZOOM", "MOUSEROTATE", "MOUSESMARTZOOM",
              "NDOF_MOTION", "NDOF_BUTTON_FIT"}
ARROWS = {"RIGHT_ARROW": "axis_x", "LEFT_ARROW": "axis_y", "UP_ARROW": "axis_z", "DOWN_ARROW": "parallel"}
TYPABLE = set("0123456789.,;-+[]<>/*'\"")
TOOL_KEYS = {
    "SPACE": "select_tool", "L": "line", "R": "draw_rectangle", "C": "draw_circle", "A": "draw_arc",
    "P": "push_pull", "F": "offset", "M": "move", "Q": "rotate", "S": "scale", "T": "tape",
    "B": "paint", "E": "erase",
}


class SketchTool:
    bl_options = {"REGISTER", "UNDO"}
    cursor = "CROSSHAIR"
    vcb_label = "Length"

    @classmethod
    def poll(cls, context):
        return context.screen is not None and any(a.type == "VIEW_3D" for a in context.screen.areas)

    # --- life cycle
    def invoke(self, context, event):
        area = context.area if context.area is not None and context.area.type == "VIEW_3D" else None
        if area is None:
            area = max((a for a in context.screen.areas if a.type == "VIEW_3D"), key=lambda a: a.width * a.height)
        self._area = area
        self._region = next(r for r in area.regions if r.type == "WINDOW")
        self._rv3d = area.spaces.active.region_3d
        if context.mode != "OBJECT":
            try:
                bpy.ops.object.mode_set(mode="OBJECT")
            except Exception:
                pass
        self.token = object()
        _ACTIVE["token"] = self.token
        self.state = si.InferState()
        self.typed = ""
        self.inf = None
        self.measure = None
        self.mouse = None
        self.ctrl = False
        self.shift_lock = False
        self._last_double = 0.0
        self.refresh(context)
        sd.OVERLAY.clear()
        sd.OVERLAY.owner = self.token
        self.reset(context)
        context.window_manager.modal_handler_add(self)
        try:
            context.window.cursor_modal_set(self.cursor)
        except Exception:
            pass
        m = self._mouse(event)
        if m is not None:
            self.mouse = m
            try:
                self.on_move(context, event)
            except Exception:
                traceback.print_exc()
        self.status(context)
        return {"RUNNING_MODAL"}

    def refresh(self, context):
        self.snap = build_snapshot(context)

    def end(self, context):
        try:
            context.window.cursor_modal_restore()
        except Exception:
            pass
        try:
            self._area.header_text_set(None)
        except Exception:
            pass
        try:
            context.workspace.status_text_set(None)
        except Exception:
            pass
        if sd.OVERLAY.owner is self.token:
            sd.OVERLAY.clear()
            sd.OVERLAY.owner = None
        if _ACTIVE.get("token") is self.token:
            _ACTIVE["token"] = None
        sd.redraw_all(context)
        return {"FINISHED"}

    # --- helpers
    def _mouse(self, event):
        r = self._region
        x, y = event.mouse_x - r.x, event.mouse_y - r.y
        if 0 <= x < r.width and 0 <= y < r.height:
            return Vector((x, y))
        return None

    def view(self):
        r = self._region
        return si.View(self._rv3d.perspective_matrix, r.width, r.height)

    def ray(self):
        return self.view().ray(self.mouse)

    def infer(self, last=None, plane=None, extra=(), allow_lines=True):
        if self.mouse is None:
            return None
        inf = si.infer(self.view(), self.mouse, self.snap, last=last, plane=plane, state=self.state,
                       extra_points=extra, allow_lines=allow_lines)
        self.state.observe(inf)
        self.inf = inf
        return inf

    def show(self, inf, band_from=None):
        """Inference feedback: dot, coloured line, dotted From Point line, tooltip."""
        ov = sd.OVERLAY
        ov.mouse = tuple(self.mouse) if self.mouse is not None else None
        for tp in self.state.tracked:
            ov.points.append((tp, (0.55, 0.55, 0.55, 1), 2.5))
        if inf is None or inf.point is None:
            ov.tooltip = None
            return
        if inf.line is not None:
            s, e, k = inf.line
            ov.lines.append((s, e, si.COLORS.get(k, BAND), 2.0, False))
        elif band_from is not None:
            ov.lines.append((band_from, inf.point, BAND, 1.5, False))
        if inf.from_pt is not None:
            k = inf.line[2] if inf.line is not None else "from_point"
            ov.lines.append((inf.from_pt, inf.point, si.COLORS.get(k, BAND), 1.5, True))
        if inf.kind not in ("plane", "none") and inf.kind not in si.LINE_KINDS:
            ov.points.append((inf.point, inf.color, 4.5))
        ov.tooltip = (inf.label, inf.color) if inf.label else None

    def face_hit(self):
        """(point, object index, face index) of the face under the mouse, or None."""
        if self.mouse is None:
            return None
        origin, ray = self.ray()
        t, tri = si.ray_tris(origin, ray, self.snap.T)
        if tri is None:
            return None
        oi, fi = self.snap.Tsrc[tri]
        return origin + ray * t, int(oi), int(fi)

    def hover_region_tris(self, _inf=None):
        """Triangles of the SketchUp face under the mouse (world), if it is editable:
        (triangles, object, face index, hit point) or None."""
        hit = self.face_hit()
        if hit is None:
            return None
        point, oi, fi = hit
        obj = bpy.data.objects.get(self.snap.objects[oi])
        if obj is None or not is_editable(bpy.context, obj):
            return None
        mask = (self.snap.Tsrc[:, 0] == oi)
        # the Blender faces making that SketchUp face
        with_region = self._region_faces(obj, fi)
        mask &= np.isin(self.snap.Tsrc[:, 1], list(with_region))
        return [tuple(Vector(p) for p in t) for t in self.snap.T[mask]], obj, fi, point

    def _region_faces(self, obj, fi):
        key = (obj.name, fi, sd._cache["version"])
        cache = getattr(self, "_rcache", None)
        if cache and cache[0] == key:
            return cache[1]
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        sg.soft_layer(bm)
        bm.faces.ensure_lookup_table()
        res = {f.index for f in sg.region_of(bm, bm.faces[fi])} if fi < len(bm.faces) else set()
        bm.free()
        self._rcache = (key, res)
        return res

    def status(self, context):
        hint = self.hint()
        val = self.typed if self.typed else self.measure_text()
        lock = ""
        if self.state.lock is not None:
            lock = f"   [locked: {si.LABELS.get(self.state.lock_kind, '') or self.state.lock_kind}]"
        extra = self.flags()
        text = f"{self.bl_label}: {hint}{extra}{lock}        {self.vcb_label}: {val}"
        try:
            self._area.header_text_set(text)
        except Exception:
            pass
        try:
            context.workspace.status_text_set(
                f"{self.bl_label} · {hint}   |   {self.vcb_label}  {val}   |   arrows lock an axis · Shift locks · Esc cancels")
        except Exception:
            pass

    def measure_text(self):
        if self.measure is None:
            return ""
        if isinstance(self.measure, str):
            return self.measure
        return si.fmt_len(self.measure)

    def flags(self):
        return ""

    def hint(self):
        return ""

    def last_point(self):
        return None

    def plane(self):
        return None

    # --- events
    def modal(self, context, event):
        if _ACTIVE.get("token") is not self.token:
            self.end(context)
            return {"FINISHED", "PASS_THROUGH"}
        try:
            res = self._modal(context, event)
        except Exception:
            traceback.print_exc()
            self.end(context)
            return {"CANCELLED"}
        return res

    def _modal(self, context, event):
        et, ev = event.type, event.value
        if et in NAV_EVENTS:
            return {"PASS_THROUGH"}
        if et in {"MOUSEMOVE", "INBETWEEN_MOUSEMOVE"}:
            m = self._mouse(event)
            if m is None:
                return {"PASS_THROUGH"}
            self.mouse = m
            sd.OVERLAY.clear()
            self.on_move(context, event)
            self.status(context)
            self._area.tag_redraw()
            return {"RUNNING_MODAL"}
        if et == "LEFTMOUSE":
            m = self._mouse(event)
            if m is None:
                return {"PASS_THROUGH"}
            self.mouse = m
            if ev == "PRESS":
                self.on_press(context, event)
            elif ev == "DOUBLE_CLICK":
                self._last_double = time.monotonic()
                self.on_double(context, event)
            elif ev == "RELEASE":
                self.on_release(context, event)
            self._after(context)
            return {"RUNNING_MODAL"}
        if et in {"LEFT_SHIFT", "RIGHT_SHIFT"}:
            if ev == "PRESS" and self.state.lock is None:
                self.state.lock_current(self.inf, self.last_point())
                self.shift_lock = self.state.lock is not None
            elif ev == "RELEASE" and self.shift_lock:
                self.state.unlock()
                self.shift_lock = False
            self._refresh_move(context, event)
            return {"RUNNING_MODAL"}
        if et in {"LEFT_CTRL", "RIGHT_CTRL", "OSKEY"} and ev == "PRESS":
            self.ctrl = not self.ctrl
            self.on_ctrl(context)
            self._refresh_move(context, event)
            return {"RUNNING_MODAL"}
        if et in {"LEFT_ALT", "RIGHT_ALT"} and ev == "PRESS":
            self.on_alt(context)
            self._refresh_move(context, event)
            return {"RUNNING_MODAL"}
        if ev != "PRESS":
            return {"PASS_THROUGH"}
        if et in ARROWS and not event.ctrl:
            if not self.on_arrow(context, ARROWS[et]):
                self.state.lock_axis(ARROWS[et], self.last_point(), (self.plane() or (None, Z))[1])
                self.shift_lock = False
            self._refresh_move(context, event)
            return {"RUNNING_MODAL"}
        if et == "ESC":
            if self.typed:
                self.typed = ""
            else:
                self.state.unlock()
                self.cancel_step(context)
            self._refresh_move(context, event)
            return {"RUNNING_MODAL"}
        if et in {"RET", "NUMPAD_ENTER"}:
            text, self.typed = self.typed, ""
            if text.strip():
                self.on_vcb(context, text.strip())
            self._after(context)
            return {"RUNNING_MODAL"}
        if et == "BACK_SPACE":
            if self.typed:
                self.typed = self.typed[:-1]
                self.status(context)
                return {"RUNNING_MODAL"}
            return {"PASS_THROUGH"}
        if event.ctrl and et in {"Z", "Y"}:
            # undo/redo while a tool runs: stop it, let Blender undo
            self.end(context)
            return {"FINISHED", "PASS_THROUGH"}
        if et == "DEL" and not self.typed:
            erase_selection(context)
            self.refresh(context)
            self._after(context)
            return {"RUNNING_MODAL"}
        ch = event.unicode or ""
        if not event.ctrl and not event.alt and ch and (ch in TYPABLE or (self.typed and (ch.isalpha() or ch == " "))):
            if et == "SPACE" and not self.typed:
                pass
            else:
                self.typed += ch
                self.status(context)
                return {"RUNNING_MODAL"}
        if not event.ctrl and not event.alt and not event.shift and et in TOOL_KEYS and not self.typed:
            name = TOOL_KEYS[et]
            self.end(context)
            getattr(bpy.ops.sketch, name)("INVOKE_DEFAULT")
            return {"FINISHED"}
        return {"PASS_THROUGH"}

    def _refresh_move(self, context, event):
        if self.mouse is not None:
            sd.OVERLAY.clear()
            self.on_move(context, event)
        self.status(context)
        self._area.tag_redraw()

    def _after(self, context):
        sd.OVERLAY.clear()
        if self.mouse is not None:
            self.on_move(context, None)
        self.status(context)
        sd.redraw_all(context)

    # --- to override
    def reset(self, context):
        pass

    def on_move(self, context, event):
        pass

    def on_press(self, context, event):
        pass

    def on_double(self, context, event):
        pass

    def on_release(self, context, event):
        pass

    def on_vcb(self, context, text):
        pass

    def on_ctrl(self, context):
        pass

    def on_alt(self, context):
        pass

    def on_arrow(self, context, kind):
        return False

    def cancel_step(self, context):
        self.reset(context)

    def committed(self, context, name=None):
        self.refresh(context)
        push_undo(name or self.bl_label)


def erase_selection(context):
    """Delete key: erase what is selected."""
    sel = read_selection(context)
    for name, (f, e, _v) in sel.items.items():
        obj = bpy.data.objects.get(name)
        if obj is None:
            continue
        with edit_mesh(obj) as bm:
            faces = [bm.faces[i] for i in f]
            edges = [bm.edges[i] for i in e]
            if edges:
                sg.erase_edges(bm, edges)
            faces = [x for x in faces if x.is_valid]
            if faces:
                sg.erase_faces(bm, faces)
    for name in sel.groups:
        obj = bpy.data.objects.get(name)
        if obj is not None:
            bpy.data.objects.remove(obj, do_unlink=True)
    push_undo("Erase")


def plane_from_inference(inf, forced=None):
    if forced is not None:
        return forced
    if inf is not None and inf.kind == "on_face" and inf.normal is not None:
        return inf.normal.normalized()
    return Z.copy()


def project_to_plane(p, origin, normal):
    n = Vector(normal).normalized()
    return p - n * (p - origin).dot(n)


AXIS_NORMALS = {"axis_x": Vector((1, 0, 0)), "axis_y": Vector((0, 1, 0)), "axis_z": Z}


# ------------------------------------------------------------------ Line (L)

class SKETCH_OT_line(SketchTool, bpy.types.Operator):
    """Line (L): click, click, click... A closed loop makes a face; a line across a face splits it"""
    bl_idname = "sketch.line"
    bl_label = "Line"

    def reset(self, context):
        self.pts = []
        self.draw_plane = None
        self.measure = None

    def hint(self):
        return "Select start point." if not self.pts else "Select end point (or type a length, [x,y,z], <x,y,z>)."

    def last_point(self):
        return self.pts[-1] if self.pts else None

    def plane(self):
        if self.draw_plane is not None:
            return self.draw_plane
        if self.pts:
            return (self.pts[-1], Z)
        return None

    def on_move(self, context, event):
        last = self.last_point()
        inf = self.infer(last=last, plane=self.plane(), extra=self.pts[:1])
        self.show(inf, band_from=last)
        if last is not None and inf is not None and inf.point is not None:
            if inf.line is None:
                sd.OVERLAY.lines.append((last, inf.point, BAND, 1.5, False))
            self.measure = (inf.point - last).length

    def on_press(self, context, event):
        if self.inf is None or self.inf.point is None:
            return
        if not self.pts and self.inf.kind == "on_face" and self.inf.normal is not None:
            self.draw_plane = (self.inf.point.copy(), self.inf.normal.copy())
        self.add_point(context, self.inf.point.copy())

    def add_point(self, context, p):
        if not self.pts:
            self.pts = [p]
            return
        last = self.pts[-1]
        if (p - last).length < sg.WELD:
            return
        _ne, nf = action_line(context, [last, p])
        self.committed(context, "Line")
        if nf or (p - self.pts[0]).length < sg.WELD:
            self.reset(context)          # a face was made: the chain ends, as in SketchUp
        else:
            self.pts.append(p)
        self.state.unlock()

    def on_vcb(self, context, text):
        v = si.parse_vcb(text)
        if v is None:
            return
        kind, val = v
        if kind == "absolute":
            self.add_point(context, val)
        elif kind == "relative":
            base = self.last_point() or Vector()
            self.add_point(context, base + val)
        elif kind == "values" and self.pts and val and val[0] is not None:
            last = self.pts[-1]
            if self.inf is not None and self.inf.point is not None and (self.inf.point - last).length > 1e-9:
                d = (self.inf.point - last).normalized()
            elif self.state.lock is not None and self.state.lock[0] == "dir":
                d = self.state.lock[1]
            else:
                return
            self.add_point(context, last + d * val[0])


# ------------------------------------------------------------------ Rectangle (R)

class SKETCH_OT_draw_rectangle(SketchTool, bpy.types.Operator):
    """Rectangle (R): two opposite corners (or type width,depth). On a face it splits the face"""
    bl_idname = "sketch.draw_rectangle"
    bl_label = "Rectangle"
    vcb_label = "Dimensions"

    def reset(self, context):
        self.a = None
        self.normal = None
        self.forced = getattr(self, "forced", None)
        self.cur = None
        self.measure = None

    def hint(self):
        return "Select first corner." if self.a is None else "Select opposite corner (or type width,depth)."

    def on_arrow(self, context, kind):
        if kind in AXIS_NORMALS:
            self.forced = None if self.forced is not None and self.forced == AXIS_NORMALS[kind] else AXIS_NORMALS[kind].copy()
            if self.a is not None:
                self.normal = self.forced or Z.copy()
            return True
        return False

    def plane(self):
        return (self.a, self.normal) if self.a is not None else None

    def on_move(self, context, event):
        if self.a is None:
            inf = self.infer()
            self.show(inf)
            return
        inf = self.infer(plane=self.plane(), allow_lines=False)
        self.show(inf)
        if inf is None or inf.point is None:
            return
        p = project_to_plane(inf.point, self.a, self.normal)
        self.cur = p
        c = sg.rectangle_points(self.a, p, self.normal)
        for i in range(4):
            sd.OVERLAY.lines.append((c[i], c[(i + 1) % 4], BAND, 1.5, False))
        u, v = sg.plane_axes(self.normal)
        d = p - self.a
        self.measure = f"{si.fmt_len(abs(d.dot(u)))}, {si.fmt_len(abs(d.dot(v)))}"

    def on_press(self, context, event):
        if self.inf is None or self.inf.point is None:
            return
        if self.a is None:
            self.a = self.inf.point.copy()
            self.normal = plane_from_inference(self.inf, self.forced)
        elif self.cur is not None:
            self.make(context, self.a, self.cur)

    def make(self, context, a, b):
        c = sg.rectangle_points(a, b, self.normal)
        if (c[1] - c[0]).length < sg.WELD or (c[3] - c[0]).length < sg.WELD:
            return
        action_line(context, c, closed=True)
        self.committed(context, "Rectangle")
        self.reset(context)

    def on_vcb(self, context, text):
        v = si.parse_vcb(text)
        if v is None or v[0] != "values" or self.a is None:
            return
        vals = [x for x in v[1] if x is not None]
        if len(vals) < 2:
            return
        u, w = sg.plane_axes(self.normal)
        d = (self.cur - self.a) if self.cur is not None else u + w
        su = math.copysign(1, d.dot(u) or 1)
        sw = math.copysign(1, d.dot(w) or 1)
        self.make(context, self.a, self.a + u * vals[0] * su + w * vals[1] * sw)


# ------------------------------------------------------------------ Circle (C)

class SKETCH_OT_draw_circle(SketchTool, bpy.types.Operator):
    """Circle (C): click the centre, then the radius (type it, or '24s' for 24 sides)"""
    bl_idname = "sketch.draw_circle"
    bl_label = "Circle"
    vcb_label = "Radius"

    segments: IntProperty(name="Sides", default=24, min=3, max=999)

    def reset(self, context):
        self.c = None
        self.normal = None
        self.forced = getattr(self, "forced", None)
        self.cur = None
        self.measure = None

    def hint(self):
        return "Select center point." if self.c is None else "Select edge point (or type the radius)."

    def flags(self):
        return f"   [{self.segments} sides]"

    def on_arrow(self, context, kind):
        if kind in AXIS_NORMALS:
            self.forced = AXIS_NORMALS[kind].copy()
            if self.c is not None:
                self.normal = self.forced
            return True
        return False

    def plane(self):
        return (self.c, self.normal) if self.c is not None else None

    def last_point(self):
        return self.c

    def on_move(self, context, event):
        if self.c is None:
            inf = self.infer()
            self.show(inf)
            if inf is not None and inf.point is not None:
                n = plane_from_inference(inf, self.forced)
                ring = sg.circle_points(inf.point, 0.0 + self._preview_r(), n, 24)
                for i in range(len(ring)):
                    sd.OVERLAY.lines.append((ring[i], ring[(i + 1) % len(ring)], (0.4, 0.4, 0.4, 1), 1.0, False))
            return
        inf = self.infer(last=self.c, plane=self.plane())
        self.show(inf, band_from=self.c)
        if inf is None or inf.point is None:
            return
        p = project_to_plane(inf.point, self.c, self.normal)
        self.cur = p
        r = (p - self.c).length
        self.measure = r
        ring = sg.circle_points(self.c, r, self.normal, self.segments, start=p - self.c)
        for i in range(len(ring)):
            sd.OVERLAY.lines.append((ring[i], ring[(i + 1) % len(ring)], BAND, 1.5, False))

    def _preview_r(self):
        # a small protractor-size ring under the cursor
        rv = self._rv3d
        return 0.04 * (rv.view_distance if rv is not None else 10)

    def on_press(self, context, event):
        if self.inf is None or self.inf.point is None:
            return
        if self.c is None:
            self.c = self.inf.point.copy()
            self.normal = plane_from_inference(self.inf, self.forced)
        elif self.cur is not None:
            self.make(context, (self.cur - self.c).length, self.cur - self.c)

    def make(self, context, r, start=None):
        if r < sg.WELD:
            return
        pts = sg.circle_points(self.c, r, self.normal, self.segments, start=start)
        action_line(context, pts, closed=True)
        self.committed(context, "Circle")
        self.reset(context)

    def on_vcb(self, context, text):
        v = si.parse_vcb(text)
        if v is None:
            return
        if v[0] == "segments":
            self.segments = max(3, min(999, v[1]))
        elif v[0] == "values" and self.c is not None and v[1] and v[1][0]:
            self.make(context, v[1][0], (self.cur - self.c) if self.cur is not None else None)


# ------------------------------------------------------------------ Arc (A): SketchUp's 2-point arc

class SKETCH_OT_draw_arc(SketchTool, bpy.types.Operator):
    """Arc (A): click the start, the end, then the bulge (type it; '12s' for 12 segments)"""
    bl_idname = "sketch.draw_arc"
    bl_label = "Arc"

    segments: IntProperty(name="Segments", default=12, min=2, max=999)

    def reset(self, context):
        self.a = None
        self.b = None
        self.normal = None
        self.bulge = 0.0
        self.measure = None

    @property
    def vcb_label(self):
        return "Length" if self.b is None else "Bulge"

    def hint(self):
        if self.a is None:
            return "Select start point."
        if self.b is None:
            return "Select end point (or type the length)."
        return "Select bulge (or type it)."

    def flags(self):
        return f"   [{self.segments} segments]"

    def last_point(self):
        return self.b if self.b is not None else self.a

    def plane(self):
        return (self.a, self.normal) if self.a is not None else None

    def on_move(self, context, event):
        if self.a is None:
            self.show(self.infer())
            return
        if self.b is None:
            inf = self.infer(last=self.a, plane=self.plane())
            self.show(inf, band_from=self.a)
            if inf is not None and inf.point is not None:
                self.measure = (inf.point - self.a).length
            return
        inf = self.infer(plane=self.plane(), allow_lines=False)
        self.show(inf)
        if inf is None or inf.point is None:
            return
        p = project_to_plane(inf.point, self.a, self.normal)
        self.bulge = sg.arc_bulge_from_point(self.a, self.b, p, self.normal)
        # SketchUp offers the half circle when the bulge is close to it
        half = (self.b - self.a).length / 2
        if abs(abs(self.bulge) - half) < half * 0.04:
            self.bulge = math.copysign(half, self.bulge)
            sd.OVERLAY.tooltip = ("Half Circle", si.MAGENTA)
        self.measure = abs(self.bulge)
        pts = sg.arc_points(self.a, self.b, self.bulge, self.normal, self.segments)
        for x, y in zip(pts, pts[1:]):
            sd.OVERLAY.lines.append((x, y, BAND, 1.5, False))

    def on_press(self, context, event):
        if self.inf is None or self.inf.point is None:
            return
        if self.a is None:
            self.a = self.inf.point.copy()
            self.normal = plane_from_inference(self.inf)
        elif self.b is None:
            b = project_to_plane(self.inf.point, self.a, self.normal) if self.inf.kind == "plane" else self.inf.point.copy()
            if (b - self.a).length > sg.WELD:
                self.b = b
                if abs((self.b - self.a).normalized().dot(self.normal)) > 0.999:
                    self.normal = Vector((1, 0, 0))
                elif abs((self.b - self.a).normalized().dot(self.normal)) > 1e-6:
                    # the arc lies in the vertical plane through a and b
                    n = (self.b - self.a).cross(Z)
                    self.normal = n.normalized() if n.length > 1e-9 else Vector((1, 0, 0))
        else:
            self.make(context, self.bulge)

    def make(self, context, bulge):
        pts = sg.arc_points(self.a, self.b, bulge, self.normal, self.segments)
        action_line(context, pts)
        self.committed(context, "Arc")
        self.reset(context)

    def on_vcb(self, context, text):
        v = si.parse_vcb(text)
        if v is None:
            return
        if v[0] == "segments":
            self.segments = max(2, min(999, v[1]))
        elif v[0] == "values" and v[1] and v[1][0] is not None:
            if self.a is not None and self.b is None and self.inf is not None and self.inf.point is not None:
                d = self.inf.point - self.a
                if d.length > 1e-9:
                    self.b = self.a + d.normalized() * v[1][0]
            elif self.b is not None:
                self.make(context, math.copysign(v[1][0], self.bulge or 1.0))


# ------------------------------------------------------------------ Push/Pull (P)

class SKETCH_OT_push_pull(SketchTool, bpy.types.Operator):
    """Push/Pull (P): click a face, move, click (or type the distance). Ctrl: new starting face. Double-click: repeat"""
    bl_idname = "sketch.push_pull"
    bl_label = "Push/Pull"
    vcb_label = "Distance"
    cursor = "SCROLL_Y"

    def reset(self, context):
        self.face = None   # (object, face index, start point, world normal, outline segments)
        self.dist = 0.0
        self.measure = None

    def hint(self):
        return "Pick a face." if self.face is None else "Pick distance (or type it)."

    def flags(self):
        return "   [+ new starting face]" if self.ctrl else ""

    def on_move(self, context, event):
        if self.face is None:
            inf = self.infer(allow_lines=False)
            hov = self.hover_region_tris(inf)
            if hov is not None:
                sd.OVERLAY.faces.append((hov[0], sd.HOVER_FACE))
            sd.OVERLAY.mouse = tuple(self.mouse)
            return
        obj, fi, p0, n, outline = self.face
        origin, ray = self.ray()
        inf = self.infer(allow_lines=False)
        if inf is not None and inf.point is not None and inf.kind in si.POINT_KINDS + ("on_edge",):
            d = (inf.point - p0).dot(n)
            self.show(inf)
        else:
            q = si.closest_on_line_to_ray(p0, n, origin, ray)
            d = (q - p0).dot(n) if q is not None else 0.0
            sd.OVERLAY.tooltip = None
        self.dist = d
        self.measure = d
        off = n * d
        for a, b in outline:
            sd.OVERLAY.lines.append((a + off, b + off, BAND, 1.5, False))
            sd.OVERLAY.lines.append((a, a + off, BAND, 1.0, False))
        sd.OVERLAY.lines.append((p0, p0 + off, (0.3, 0.3, 0.3, 1), 1.0, True))
        sd.OVERLAY.mouse = tuple(self.mouse)

    def pick(self, context):
        hov = self.hover_region_tris()
        if hov is None:
            return False
        _tris, obj, fi, point = hov
        with edit_mesh(obj) as bm:
            f = bm.faces[fi]
            f.normal_update()
            mw = obj.matrix_world
            n = (mw.to_3x3().inverted_safe().transposed() @ f.normal).normalized()
            region = sg.region_of(bm, f)
            outline = [(mw @ e.verts[0].co, mw @ e.verts[1].co) for e in sg.region_boundary_edges(bm, region)]
        self.face = (obj, fi, point.copy(), n, outline)
        return True

    def on_press(self, context, event):
        if self.face is None:
            self.pick(context)
        else:
            self.commit(context, self.dist)

    def on_double(self, context, event):
        if LAST["pushpull"] is None:
            return
        self.reset(context)
        if self.pick(context):
            self.commit(context, LAST["pushpull"])

    def commit(self, context, dist):
        if self.face is None or abs(dist) < sg.WELD:
            return
        obj, fi, _p0, _n, _o = self.face
        action_push_pull(context, obj, fi, dist, keep=self.ctrl)
        LAST["pushpull"] = dist
        self.committed(context, "Push/Pull")
        self.reset(context)

    def on_vcb(self, context, text):
        v = si.parse_vcb(text)
        if v is None or v[0] != "values" or not v[1] or v[1][0] is None:
            return
        if self.face is not None:
            self.commit(context, math.copysign(v[1][0], self.dist or 1.0) if v[1][0] >= 0 else v[1][0])


# ------------------------------------------------------------------ Offset (F)

class SKETCH_OT_offset(SketchTool, bpy.types.Operator):
    """Offset (F): click a face, move inwards or outwards, click (or type the distance)"""
    bl_idname = "sketch.offset"
    bl_label = "Offset"
    vcb_label = "Distance"

    def reset(self, context):
        self.face = None    # (object, outer loop world points, plane point, normal)
        self.dist = 0.0
        self.measure = None

    def hint(self):
        return "Pick a face." if self.face is None else "Pick offset distance (or type it)."

    def pick(self, context):
        hov = self.hover_region_tris()
        if hov is None:
            return False
        _t, obj, fi, point = hov
        with edit_mesh(obj) as bm:
            pts, pl = sg.outer_loop_points(bm, bm.faces[fi])
            mw = obj.matrix_world
            wp = [mw @ p for p in pts]
        if len(wp) < 3:
            return False
        normal = sg.Plane(wp[0], (mw.to_3x3().inverted_safe().transposed() @ pl.no)).no
        self.face = (obj, wp, point.copy(), normal)
        return True

    def _loop(self, dist):
        obj, wp, p0, normal = self.face
        pl = sg.Plane(wp[0], normal)
        pts = wp if sg.poly_area([pl.to2(p) for p in wp]) > 0 else wp[::-1]
        return sg.offset_points3(pts, pl, dist), pl, pts

    def on_move(self, context, event):
        if self.face is None:
            inf = self.infer(allow_lines=False)
            hov = self.hover_region_tris(inf)
            if hov is not None:
                sd.OVERLAY.faces.append((hov[0], sd.HOVER_FACE))
            return
        obj, wp, p0, normal = self.face
        origin, ray = self.ray()
        from mathutils import geometry as mgeo
        q = mgeo.intersect_line_plane(origin, origin + ray, p0, normal)
        if q is None:
            return
        _l, pl, pts = self._loop(0.0)
        poly = [pl.to2(p) for p in pts]
        q2 = pl.to2(q)
        d = min(sg.point_seg_dist2(q2, poly[i], poly[(i + 1) % len(poly)]) for i in range(len(poly))) ** 0.5
        self.dist = d if sg.point_in_poly(q2, poly) else -d
        self.measure = abs(self.dist)
        loop, _pl, _p = self._loop(self.dist)
        for i in range(len(loop)):
            sd.OVERLAY.lines.append((loop[i], loop[(i + 1) % len(loop)], BAND, 1.5, False))
        sd.OVERLAY.mouse = tuple(self.mouse)

    def on_press(self, context, event):
        if self.face is None:
            self.pick(context)
        else:
            self.commit(context, self.dist)

    def on_double(self, context, event):
        if LAST["offset"] is None:
            return
        self.reset(context)
        if self.pick(context):
            self.commit(context, LAST["offset"])

    def commit(self, context, dist):
        if self.face is None or abs(dist) < sg.WELD:
            return
        loop, _pl, _p = self._loop(dist)
        action_line(context, loop, closed=True, obj=self.face[0])
        LAST["offset"] = dist
        self.committed(context, "Offset")
        self.reset(context)

    def on_vcb(self, context, text):
        v = si.parse_vcb(text)
        if v and v[0] == "values" and v[1] and v[1][0] is not None and self.face is not None:
            self.commit(context, math.copysign(v[1][0], self.dist or 1.0))


# ------------------------------------------------------------------ picking entities (Select, Move, Eraser, Paint...)

def pick_entity(tool, context):
    """What is under the mouse, SketchUp style: ('edge'|'face', obj, index) for editable
    geometry, ('group', obj, None) for a closed group, or None."""
    if tool.mouse is None:
        return None
    view = tool.view()
    origin, ray = view.ray(tool.mouse)
    snap = tool.snap
    hit_t, tri = si.ray_tris(origin, ray, snap.T)
    edges = si._edge_candidates(view, np.asarray(tool.mouse, dtype=np.float64), snap, origin, ray, hit_t, si.EDGE_PX)
    cand = []
    if edges:
        _d, ei, _q = edges[0]
        oi, idx = snap.Esrc[ei]
        cand.append(("edge", snap.objects[oi], int(idx)))
    if tri is not None:
        oi, idx = snap.Tsrc[tri]
        cand.append(("face", snap.objects[oi], int(idx)))
    for kind, name, idx in cand:
        obj = bpy.data.objects.get(name)
        if obj is None:
            continue
        if is_editable(context, obj):
            return (kind, obj, idx)
        grp = top_group(context, obj)
        if grp is not None and grp.get("sketch_kind") == "group":
            return ("group", grp, None)
        return ("other", obj, None)
    return None


def select_entity(context, ent, mode="set", extent="single"):
    """Click (single), double-click (face+edges / edge+faces), triple-click (all connected)."""
    if mode == "set":
        clear_selection(context)
        mode = "add"
    if ent is None:
        return
    kind, obj, idx = ent
    if kind in ("group", "other"):
        if kind == "group":
            select_group(context, obj, mode)
        return
    with edit_mesh(obj) as bm:
        if kind == "edge":
            seed = bm.edges[idx]
        else:
            seed = bm.faces[idx]
        if extent == "connected":
            vs, es, fs = sg.connected(bm, [seed])
            items = list(es) + list(fs)
        elif extent == "bounded":
            if kind == "face":
                region = sg.region_of(bm, seed)
                items = list(region) + sg.region_boundary_edges(bm, region)
            else:
                items = [seed]
                for f in seed.link_faces:
                    items += list(sg.region_of(bm, f))
        else:
            items = list(sg.region_of(bm, seed)) if kind == "face" else [seed]
        lay = sg.soft_layer(bm)
        for x in items:
            if isinstance(x, bmesh.types.BMEdge) and x[lay] == sg.SEAM:
                continue
            x.select = (not x.select) if mode == "toggle" else (mode == "add")


def ensure_selection(tool, context):
    """Move/Rotate/Scale with nothing selected act on what is under the mouse (as SketchUp)."""
    sel = read_selection(context)
    if not sel.empty:
        return sel
    ent = pick_entity(tool, context)
    if ent is None or ent[0] == "other":
        return sel
    if ent[0] == "face":
        select_entity(context, ent, "set", "bounded")
    else:
        select_entity(context, ent, "set")
    tool.refresh(context)
    return read_selection(context)


# ------------------------------------------------------------------ Select (Space)

class SKETCH_OT_select_tool(SketchTool, bpy.types.Operator):
    """Select (Space): click; double-click a face for it and its edges; triple-click for all connected.
    Drag left to right for a window, right to left for a crossing selection. Shift toggles, Ctrl adds,
    Shift+Ctrl removes. Double-click a group to edit it; Esc or click outside to close it"""
    bl_idname = "sketch.select_tool"
    bl_label = "Select"
    cursor = "DEFAULT"
    vcb_label = ""

    def reset(self, context):
        self.press = None
        self.drag = False
        self.mods = None

    def hint(self):
        c = context_object(bpy.context)
        where = f"   [editing {c.name}]" if c is not None else ""
        return "Click or drag to select. Double-click a group to edit it." + where

    def on_move(self, context, event):
        sd.OVERLAY.mouse = None
        if self.press is not None and self.mouse is not None:
            if (self.mouse - self.press).length > 4:
                self.drag = True
            if self.drag:
                sd.OVERLAY.rect = (tuple(self.press), tuple(self.mouse), self.mouse.x < self.press.x)

    def _mode(self, event):
        if event is None:
            return "set"
        if event.shift and event.ctrl:
            return "remove"
        if event.shift:
            return "toggle"
        if event.ctrl:
            return "add"
        return "set"

    def on_press(self, context, event):
        if time.monotonic() - self._last_double < 0.45:
            # triple click: everything connected
            ent = pick_entity(self, context)
            if ent is not None and ent[0] in ("edge", "face"):
                select_entity(context, ent, self._mode(event), "connected")
            self._last_double = 0.0
            return
        self.press = self.mouse.copy()
        self.drag = False
        self.mods = self._mode(event)

    def on_release(self, context, event):
        if self.press is None:
            return
        if self.drag:
            self.box_select(context, self.press, self.mouse, self.mods)
        else:
            ent = pick_entity(self, context)
            ctx = context_object(context)
            if ctx is not None and (ent is None or (ent[0] in ("group", "other") and ent[1] is not ctx)):
                # clicking outside the open group closes it
                close_context(context)
                self.refresh(context)
                ent = pick_entity(self, context)
            if ent is None:
                if self.mods == "set":
                    clear_selection(context)
            else:
                select_entity(context, ent, self.mods)
        self.press = None
        self.drag = False
        self.refresh(context)

    def on_double(self, context, event):
        ent = pick_entity(self, context)
        if ent is None:
            return
        if ent[0] == "group":
            open_context(context, ent[1])
            self.refresh(context)
            return
        select_entity(context, ent, self._mode(event) if event and (event.shift or event.ctrl) else "set", "bounded")
        self.refresh(context)

    def cancel_step(self, context):
        if context_object(context) is not None:
            close_context(context)
            self.refresh(context)
        else:
            clear_selection(context)
        self.reset(context)

    def box_select(self, context, p0, p1, mode):
        window = p1.x >= p0.x   # left to right: window; right to left: crossing
        x0, x1 = sorted((p0.x, p1.x))
        y0, y1 = sorted((p0.y, p1.y))
        view = self.view()
        if mode == "set":
            clear_selection(context)
            mode = "add"
        for obj in editable_objects(context):
            me = obj.data
            nv = len(me.vertices)
            if not nv:
                continue
            co = np.empty(nv * 3, dtype=np.float32)
            me.vertices.foreach_get("co", co)
            mw = np.array([list(r) for r in obj.matrix_world])
            co = co.reshape(-1, 3).astype(np.float64) @ mw[:3, :3].T + mw[:3, 3]
            scr, ok = view.project_many(co)
            inside = ok & (scr[:, 0] >= x0) & (scr[:, 0] <= x1) & (scr[:, 1] >= y0) & (scr[:, 1] <= y1)
            with edit_mesh(obj) as bm:
                lay = sg.soft_layer(bm)
                for e in bm.edges:
                    if e[lay] == sg.SEAM:
                        continue
                    a, b = e.verts[0].index, e.verts[1].index
                    hit = (inside[a] and inside[b]) if window else (
                        inside[a] or inside[b] or _seg_rect(scr[a], scr[b], x0, y0, x1, y1))
                    if hit:
                        e.select = (mode == "add") if mode != "toggle" else not e.select
                for f in bm.faces:
                    idx = [v.index for v in f.verts]
                    if window:
                        hit = all(inside[i] for i in idx)
                    else:
                        hit = any(inside[i] for i in idx) or any(
                            _seg_rect(scr[idx[k]], scr[idx[(k + 1) % len(idx)]], x0, y0, x1, y1) for k in range(len(idx)))
                    if hit:
                        f.select = (mode == "add") if mode != "toggle" else not f.select
        ctx = context_object(context)
        for obj in context.visible_objects:
            if obj.get("sketch_kind") != "group" or obj is ctx or top_group(context, obj) is not obj:
                continue
            pts = [obj.matrix_world @ Vector(b) for b in obj.bound_box]
            scr = [view.project(p) for p in pts]
            ins = [s is not None and x0 <= s.x <= x1 and y0 <= s.y <= y1 for s in scr]
            if (all(ins) if window else any(ins)):
                select_group(context, obj, mode if mode != "set" else "add")
        sd.bump()


def _seg_rect(a, b, x0, y0, x1, y1):
    """Does the 2D segment a-b cross the rectangle's border?"""
    from mathutils import geometry as mgeo
    A, B = Vector((a[0], a[1])), Vector((b[0], b[1]))
    corners = [Vector((x0, y0)), Vector((x1, y0)), Vector((x1, y1)), Vector((x0, y1))]
    for i in range(4):
        if mgeo.intersect_line_line_2d(A, B, corners[i], corners[(i + 1) % 4]) is not None:
            return True
    return False


def open_context(context, obj):
    clear_selection(context)
    context.scene["sketch_context"] = obj.name
    sd.bump()


def close_context(context):
    clear_selection(context)
    if "sketch_context" in context.scene:
        del context.scene["sketch_context"]
    sd.bump()


# ------------------------------------------------------------------ Eraser (E)

class SKETCH_OT_erase(SketchTool, bpy.types.Operator):
    """Eraser (E): click or drag over edges to erase them (and the faces they bound). Ctrl: soften. Groups and guides too"""
    bl_idname = "sketch.erase"
    bl_label = "Eraser"
    vcb_label = ""

    def reset(self, context):
        self.down = False
        self.hits = {}        # object name -> set of edge indices
        self.groups = set()
        self.guides = set()
        self.segs = []

    def hint(self):
        return "Select objects to erase. Hold Ctrl (or Shift) to soften edges instead."

    def _collect(self, context):
        ent = pick_entity(self, context)
        if ent is not None:
            kind, obj, idx = ent
            if kind == "edge":
                s = self.hits.setdefault(obj.name, set())
                if idx not in s:
                    s.add(idx)
                    me = obj.data
                    a, b = me.edges[idx].vertices
                    self.segs.append((obj.matrix_world @ me.vertices[a].co, obj.matrix_world @ me.vertices[b].co))
                return
            if kind == "group":
                self.groups.add(obj.name)
                return
        # guides
        view = self.view()
        mv = Vector(self.mouse)
        for i, (p, d) in enumerate(guide_lines(context)):
            q = si.closest_on_line_to_ray(p, d, *view.ray(self.mouse))
            s = view.project(q) if q is not None else None
            if s is not None and (s - mv).length < si.EDGE_PX:
                self.guides.add(i)

    def on_move(self, context, event):
        sd.OVERLAY.mouse = None
        if self.down:
            self._collect(context)
        else:
            ent = pick_entity(self, context)
            if ent is not None and ent[0] == "edge":
                me = ent[1].data
                a, b = me.edges[ent[2]].vertices
                mw = ent[1].matrix_world
                sd.OVERLAY.lines.append((mw @ me.vertices[a].co, mw @ me.vertices[b].co, sd.SEL_COLOR, 3.0, False))
        for a, b in self.segs:
            sd.OVERLAY.lines.append((a, b, sd.SEL_COLOR, 3.0, False))

    def on_press(self, context, event):
        self.down = True
        self.soften = bool(event and (event.ctrl or event.shift))
        self._collect(context)

    def on_release(self, context, event):
        if not self.down:
            return
        for name, idx in self.hits.items():
            obj = bpy.data.objects.get(name)
            if obj is not None:
                action_erase(context, obj, sorted(idx), soften=getattr(self, "soften", False))
        for name in self.groups:
            obj = bpy.data.objects.get(name)
            if obj is not None:
                bpy.data.objects.remove(obj, do_unlink=True)
        for i in sorted(self.guides, reverse=True):
            remove_guide_line(context, i)
        changed = self.hits or self.groups or self.guides
        self.reset(context)
        if changed:
            self.committed(context, "Erase")


# ------------------------------------------------------------------ Paint Bucket (B)

class SKETCH_OT_paint(SketchTool, bpy.types.Operator):
    """Paint Bucket (B): click a face. Ctrl: connected faces of that material; Shift: all faces of that material; Alt: sample"""
    bl_idname = "sketch.paint"
    bl_label = "Paint Bucket"
    cursor = "PAINT_BRUSH"
    vcb_label = ""

    def hint(self):
        return "Select a face to paint. Alt: sample a material."

    def on_move(self, context, event):
        inf = self.infer(allow_lines=False)
        hov = self.hover_region_tris(inf)
        if hov is not None:
            sd.OVERLAY.faces.append((hov[0], (*context.scene.sketch_paint_color, 0.35)))
        sd.OVERLAY.mouse = None

    def on_press(self, context, event):
        ent = pick_entity(self, context)
        if ent is None:
            return
        kind, obj, idx = ent
        if event is not None and event.alt:
            self.sample(context, obj, idx if kind == "face" else None)
            return
        mat = paint_material(context.scene.sketch_paint_color)
        if kind in ("group", "other"):
            action_paint(context, obj, None, mat, "object")
        elif kind == "face":
            mode = "connected" if event and event.ctrl else ("same" if event and event.shift else "face")
            action_paint(context, obj, idx, mat, mode)
        self.committed(context, "Paint")

    def sample(self, context, obj, idx):
        me = obj.data
        mi = me.polygons[idx].material_index if idx is not None and idx < len(me.polygons) else 0
        if mi < len(me.materials) and me.materials[mi] is not None:
            c = me.materials[mi].diffuse_color
            context.scene.sketch_paint_color = (c[0], c[1], c[2])


# ------------------------------------------------------------------ Move (M)

class SKETCH_OT_move(SketchTool, bpy.types.Operator):
    """Move (M): click a base point, click the destination. Ctrl: copy; then type 5x or /5 for arrays"""
    bl_idname = "sketch.move"
    bl_label = "Move"
    vcb_label = "Distance"
    cursor = "SCROLL_XY"

    def reset(self, context):
        self.base = None
        self.sel = None
        self.ghost = []
        self.measure = None
        if not hasattr(self, "last_copy"):
            self.last_copy = None

    def hint(self):
        return "Pick the point to move from." if self.base is None else "Pick the destination (or type a distance)."

    def flags(self):
        return "   [+ copy]" if self.ctrl else ""

    def last_point(self):
        return self.base

    def on_move(self, context, event):
        inf = self.infer(last=self.base, plane=(self.base, Z) if self.base is not None else None)
        self.show(inf, band_from=self.base)
        if self.base is not None and inf is not None and inf.point is not None:
            v = inf.point - self.base
            self.measure = v.length
            for a, b in self.ghost:
                sd.OVERLAY.lines.append((a + v, b + v, sd.SEL_COLOR, 1.0, False))

    def on_press(self, context, event):
        if self.inf is None or self.inf.point is None:
            return
        if self.base is None:
            self.sel = ensure_selection(self, context)
            if self.sel.empty:
                return
            self.base = self.inf.point.copy()
            self.ghost = selection_world_edges(context, self.sel, 1500)
            # the selection does not infer from itself
            self.refresh(context)
        else:
            self.commit(context, self.inf.point - self.base)

    def commit(self, context, vec):
        if self.sel is None or vec.length < 1e-9:
            return
        m = Matrix.Translation(vec)
        self.orig = self.sel
        self.sel = transform_selection(context, self.sel, m, copy=self.ctrl)
        self.last_copy = (self.orig, vec) if self.ctrl else None
        self.committed(context, "Move")
        self.base = None
        self.ghost = []

    def on_vcb(self, context, text):
        v = si.parse_vcb(text)
        if v is None:
            return
        kind, val = v
        if kind in ("array", "divide") and self.last_copy is not None:
            orig, vec = self.last_copy
            if kind == "array" and val > 1:
                copy_selection_array(context, orig, [Matrix.Translation(vec * k) for k in range(2, val + 1)])
            elif kind == "divide" and val > 1:
                copy_selection_array(context, orig, [Matrix.Translation(vec * (k / val)) for k in range(1, val)])
            self.last_copy = None
            self.committed(context, "Array")
            return
        if self.base is None:
            return
        if kind == "values" and val and val[0] is not None and self.inf is not None and self.inf.point is not None:
            d = self.inf.point - self.base
            if d.length > 1e-9:
                self.commit(context, d.normalized() * val[0])
        elif kind == "relative":
            self.commit(context, val)
        elif kind == "absolute":
            self.commit(context, val - self.base)


# ------------------------------------------------------------------ Rotate (Q): the protractor

class SKETCH_OT_rotate(SketchTool, bpy.types.Operator):
    """Rotate (Q): click the centre (protractor), a point to rotate from, then the angle (or type degrees). Ctrl: copy"""
    bl_idname = "sketch.rotate"
    bl_label = "Rotate"
    vcb_label = "Angle"

    def reset(self, context):
        self.c = None
        self.normal = None
        self.forced = getattr(self, "forced", None)
        self.ref = None
        self.sel = None
        self.ghost = []
        self.angle = 0.0
        self.measure = None
        if not hasattr(self, "last_copy"):
            self.last_copy = None

    def hint(self):
        if self.c is None:
            return "Pick the centre of rotation (arrows: red/green/blue protractor)."
        if self.ref is None:
            return "Pick the start of the angle."
        return "Pick the angle (or type degrees)."

    def flags(self):
        return "   [+ copy]" if self.ctrl else ""

    def last_point(self):
        return self.c

    def on_arrow(self, context, kind):
        if kind in AXIS_NORMALS and self.ref is None:
            self.forced = None if self.forced == AXIS_NORMALS[kind] else AXIS_NORMALS[kind].copy()
            if self.c is not None:
                self.normal = self.forced or Z.copy()
            return True
        return False

    def _protractor(self, c, n, r, col=(0.1, 0.25, 1.0, 1)):
        ring = sg.circle_points(c, r, n, 48)
        for i in range(len(ring)):
            a, b = ring[i], ring[(i + 1) % len(ring)]
            sd.OVERLAY.lines.append((a, b, col, 1.5, False))
        u, v = sg.plane_axes(n)
        for k in range(24):
            ang = 2 * math.pi * k / 24
            d = u * math.cos(ang) + v * math.sin(ang)
            sd.OVERLAY.lines.append((c + d * r * (0.85 if k % 6 else 0.7), c + d * r, col, 1.0, False))

    def _size(self):
        return 0.06 * (self._rv3d.view_distance if self._rv3d is not None else 10)

    def on_move(self, context, event):
        if self.c is None:
            inf = self.infer()
            self.show(inf)
            if inf is not None and inf.point is not None:
                n = plane_from_inference(inf, self.forced)
                col = si.COLORS.get(si.axis_kind_of(n) or "on_face", si.BLUE)
                self._protractor(inf.point, n, self._size(), col)
            return
        col = si.COLORS.get(si.axis_kind_of(self.normal) or "on_face", si.BLUE)
        self._protractor(self.c, self.normal, self._size(), col)
        inf = self.infer(last=self.c, plane=(self.c, self.normal))
        self.show(inf, band_from=self.c)
        if inf is None or inf.point is None:
            return
        p = project_to_plane(inf.point, self.c, self.normal)
        if self.ref is None:
            return
        a = (self.ref - self.c)
        b = (p - self.c)
        if a.length < 1e-9 or b.length < 1e-9:
            return
        ang = math.atan2(self.normal.dot(a.cross(b)), a.dot(b))
        snap = math.radians(15)
        if abs(ang - round(ang / snap) * snap) < math.radians(2):
            ang = round(ang / snap) * snap
        self.angle = ang
        self.measure = f"{math.degrees(ang):.1f}°"
        m = self._matrix(ang)
        for x, y in self.ghost:
            sd.OVERLAY.lines.append((m @ x, m @ y, sd.SEL_COLOR, 1.0, False))
        sd.OVERLAY.lines.append((self.c, self.ref, BAND, 1.0, True))

    def _matrix(self, ang):
        return (Matrix.Translation(self.c) @ Matrix.Rotation(ang, 4, self.normal) @ Matrix.Translation(-self.c))

    def on_press(self, context, event):
        if self.inf is None or self.inf.point is None:
            return
        if self.c is None:
            self.sel = ensure_selection(self, context)
            if self.sel.empty:
                return
            self.c = self.inf.point.copy()
            self.normal = plane_from_inference(self.inf, self.forced)
            self.ghost = selection_world_edges(context, self.sel, 1500)
            self.refresh(context)
        elif self.ref is None:
            p = project_to_plane(self.inf.point, self.c, self.normal)
            if (p - self.c).length > sg.WELD:
                self.ref = p
        else:
            self.commit(context, self.angle)

    def commit(self, context, ang):
        if self.sel is None or abs(ang) < 1e-9:
            return
        m = self._matrix(ang)
        orig = self.sel
        self.sel = transform_selection(context, self.sel, m, copy=self.ctrl)
        self.last_copy = (orig, ang, self.c.copy(), self.normal.copy()) if self.ctrl else None
        self.committed(context, "Rotate")
        keep = self.last_copy
        self.reset(context)
        self.last_copy = keep

    def on_vcb(self, context, text):
        v = si.parse_vcb(text)
        if v is None:
            return
        kind, val = v
        if kind in ("array", "divide") and self.last_copy is not None:
            orig, ang, c, n = self.last_copy

            def mat(a):
                return Matrix.Translation(c) @ Matrix.Rotation(a, 4, n) @ Matrix.Translation(-c)
            if kind == "array" and val > 1:
                copy_selection_array(context, orig, [mat(ang * k) for k in range(2, val + 1)])
            elif kind == "divide" and val > 1:
                copy_selection_array(context, orig, [mat(ang * k / val) for k in range(1, val)])
            self.last_copy = None
            self.committed(context, "Array")
            return
        if kind == "values" and val and val[0] is not None and self.ref is not None:
            ang = math.radians(val[0])
            if self.angle < 0:
                ang = -ang
            self.commit(context, ang)


# ------------------------------------------------------------------ Scale (S): grips on the selection's box

class SKETCH_OT_scale(SketchTool, bpy.types.Operator):
    """Scale (S): drag a grip of the selection's box (corners: uniform). Ctrl: about the centre. Type a factor"""
    bl_idname = "sketch.scale"
    bl_label = "Scale"
    vcb_label = "Scale"

    def reset(self, context):
        self.sel = read_selection(context) if not hasattr(self, "sel") or self.sel is None else self.sel
        self.box = selection_bbox(context, self.sel) if not self.sel.empty else None
        self.grip = None
        self.factors = (1.0, 1.0, 1.0)
        self.measure = None

    def hint(self):
        if self.box is None:
            return "Select something first (or click it)."
        return "Pick a grip." if self.grip is None else "Pick the scale (or type a factor)."

    def flags(self):
        return "   [about centre]" if self.ctrl else ""

    def grips(self):
        lo, hi = self.box
        out = []
        for i in (0, 0.5, 1):
            for j in (0, 0.5, 1):
                for k in (0, 0.5, 1):
                    if (i, j, k) == (0.5, 0.5, 0.5):
                        continue
                    out.append(((i, j, k), Vector((lo.x + (hi.x - lo.x) * i, lo.y + (hi.y - lo.y) * j,
                                                    lo.z + (hi.z - lo.z) * k))))
        return out

    def _near_grip(self):
        view = self.view()
        best = None
        for key, p in self.grips():
            s = view.project(p)
            if s is None:
                continue
            d = (s - self.mouse).length
            if d < 12 and (best is None or d < best[0]):
                best = (d, key, p)
        return best

    def on_move(self, context, event):
        sd.OVERLAY.mouse = None
        if self.box is None:
            return
        hover = self._near_grip() if self.grip is None else None
        for key, p in self.grips():
            col = (0.85, 0.1, 0.1, 1) if hover is not None and hover[1] == key else (0.1, 0.75, 0.1, 1)
            sd.OVERLAY.squares.append((p, col, 3.5))
        lo, hi = self.box
        c = [Vector((x, y, z)) for x in (lo.x, hi.x) for y in (lo.y, hi.y) for z in (lo.z, hi.z)]
        for i in range(8):
            for j in range(i + 1, 8):
                if sum(1 for a, b in zip(c[i], c[j]) if abs(a - b) > 1e-9) == 1:
                    sd.OVERLAY.lines.append((c[i], c[j], (0.6, 0.6, 0.65, 1), 1.0, True))
        if self.grip is None:
            return
        key, gp, anchor = self.grip
        origin, ray = self.ray()
        d = gp - anchor
        if d.length < 1e-9:
            return
        q = si.closest_on_line_to_ray(anchor, d, origin, ray)
        if q is None:
            return
        t = (q - anchor).dot(d) / d.length_squared
        self.factors = tuple(t if abs(key[i] - 0.5) > 0.1 else 1.0 for i in range(3))
        self.measure = ", ".join(f"{f:.2f}" for f in self.factors if True)
        m = self._matrix(self.factors)
        for a, b in selection_world_edges(context, self.sel, 800):
            sd.OVERLAY.lines.append((m @ a, m @ b, sd.SEL_COLOR, 1.0, False))

    def _matrix(self, f):
        anchor = self.grip[2]
        return Matrix.Translation(anchor) @ Matrix.Diagonal((f[0], f[1], f[2], 1.0)) @ Matrix.Translation(-anchor)

    def on_press(self, context, event):
        if self.box is None:
            self.sel = ensure_selection(self, context)
            self.box = selection_bbox(context, self.sel) if not self.sel.empty else None
            return
        if self.grip is None:
            h = self._near_grip()
            if h is None:
                return
            _d, key, p = h
            lo, hi = self.box
            if self.ctrl:
                anchor = (lo + hi) / 2
            else:
                anchor = Vector((lo.x + (hi.x - lo.x) * (1 - key[0]), lo.y + (hi.y - lo.y) * (1 - key[1]),
                                 lo.z + (hi.z - lo.z) * (1 - key[2])))
            self.grip = (key, p, anchor)
        else:
            self.commit(context, self.factors)

    def commit(self, context, f):
        if self.grip is None or any(abs(x) < 1e-6 for x in f):
            return
        self.sel = transform_selection(context, self.sel, self._matrix(f))
        self.committed(context, "Scale")
        self.box = selection_bbox(context, self.sel)
        self.grip = None

    def on_vcb(self, context, text):
        v = si.parse_vcb(text)
        if v is None or v[0] != "values" or self.grip is None:
            return
        vals = [x for x in v[1] if x is not None]
        if not vals:
            return
        key = self.grip[0]
        active = [i for i in range(3) if abs(key[i] - 0.5) > 0.1]
        if len(vals) == 1:
            f = tuple(vals[0] if i in active else 1.0 for i in range(3))
        else:
            it = iter(vals)
            f = tuple((next(it, 1.0) if i in active else 1.0) for i in range(3))
        self.commit(context, f)


# ------------------------------------------------------------------ Tape Measure (T)

class SKETCH_OT_tape(SketchTool, bpy.types.Operator):
    """Tape Measure (T): measure between two points; from an edge it makes a parallel guide line,
    from a point a guide point. Ctrl: measure only"""
    bl_idname = "sketch.tape"
    bl_label = "Tape Measure"

    def reset(self, context):
        self.a = None
        self.a_inf = None
        self.measure = None

    def hint(self):
        return "Select start point." if self.a is None else "Select end point (or type a length)."

    def flags(self):
        return "   [measure only]" if self.ctrl else ""

    def last_point(self):
        return self.a

    def _edge_dir(self):
        if self.a_inf is not None and self.a_inf.kind in ("on_edge", "on_guide") and self.a_inf.edge is not None:
            e0, e1 = self.a_inf.edge
            if (e1 - e0).length > 1e-9:
                return (e1 - e0).normalized()
        return None

    def on_move(self, context, event):
        inf = self.infer(last=self.a, plane=(self.a, Z) if self.a is not None else None)
        self.show(inf, band_from=None)
        if self.a is None or inf is None or inf.point is None:
            return
        p = inf.point
        d = self._edge_dir()
        if d is not None:
            # parallel guide: the distance is the perpendicular offset from the edge
            off = (p - self.a) - d * (p - self.a).dot(d)
            self.measure = off.length
            sd.OVERLAY.lines.append((p - d * 1000, p + d * 1000, sd.GUIDE_COLOR, 1.0, True))
            sd.OVERLAY.lines.append((self.a, self.a + off, BAND, 1.0, True))
        else:
            self.measure = (p - self.a).length
            sd.OVERLAY.lines.append((self.a, p, BAND, 1.0, True))

    def on_press(self, context, event):
        if self.inf is None or self.inf.point is None:
            return
        if self.a is None:
            self.a = self.inf.point.copy()
            self.a_inf = self.inf
            return
        self.make(context, self.inf.point.copy())

    def make(self, context, p):
        if not self.ctrl:
            d = self._edge_dir()
            if d is not None:
                add_guide_line(context, p, d)
            else:
                add_guide_point(context, p)
            self.committed(context, "Guide")
        self.reset(context)

    def on_vcb(self, context, text):
        v = si.parse_vcb(text)
        if v is None or v[0] != "values" or self.a is None or not v[1] or v[1][0] is None:
            return
        if self.inf is None or self.inf.point is None:
            return
        p = self.inf.point
        d = self._edge_dir()
        if d is not None:
            off = (p - self.a) - d * (p - self.a).dot(d)
            if off.length > 1e-9:
                self.make(context, self.a + off.normalized() * v[1][0])
        elif (p - self.a).length > 1e-9:
            self.make(context, self.a + (p - self.a).normalized() * v[1][0])


# ------------------------------------------------------------------ Make Group (G)

def make_group(context, sel=None):
    """Wrap the selection in a group: its loose geometry leaves the model for its own object;
    selected groups go inside it. Returns the new group object (or None)."""
    sel = sel or read_selection(context)
    if sel.empty:
        return None
    layer = _layer(context)
    ctx = context_object(context)
    bm_new = bmesh.new()
    sg.soft_layer(bm_new)
    mats = []
    for name, (f, e, _v) in sel.items.items():
        obj = bpy.data.objects.get(name)
        if obj is None:
            continue
        with edit_mesh(obj) as bm:
            part = sg.extract(bm, [bm.faces[i] for i in f], [bm.edges[i] for i in e])
            mw = obj.matrix_world
            lay_p = sg.soft_layer(part)
            lay_n = sg.soft_layer(bm_new)
            vmap = {}
            base = len(mats)
            mats += [m for m in obj.data.materials]
            for v in part.verts:
                vmap[v] = bm_new.verts.new(mw @ v.co)
            for face in part.faces:
                nf = bm_new.faces.new([vmap[v] for v in face.verts])
                nf.material_index = base + face.material_index
            for ed in part.edges:
                a, b = vmap[ed.verts[0]], vmap[ed.verts[1]]
                ne = bm_new.edges.get((a, b)) or bm_new.edges.new((a, b))
                ne[lay_n] = ed[lay_p]
            part.free()
    if bm_new.verts:
        lo = Vector((min(v.co.x for v in bm_new.verts), min(v.co.y for v in bm_new.verts),
                     min(v.co.z for v in bm_new.verts)))
    else:
        lo = Vector()
    for v in bm_new.verts:
        v.co -= lo
    me = bpy.data.meshes.new("Group")
    bm_new.to_mesh(me)
    bm_new.free()
    for m in mats or [g.mat("wall")]:
        me.materials.append(m)
    grp = bpy.data.objects.new("Group", me)
    grp["sketch_kind"] = "group"
    grp.location = lo
    (ctx.users_collection[0] if ctx is not None else layer).objects.link(grp)
    if ctx is not None:
        grp.parent = ctx
        grp.matrix_parent_inverse = ctx.matrix_world.inverted()
    context.view_layer.update()
    for name in sel.groups:
        child = bpy.data.objects.get(name)
        if child is None:
            continue
        mw = child.matrix_world.copy()
        child.parent = grp
        child.matrix_world = mw
    clear_selection(context)
    grp.select_set(True)
    context.view_layer.objects.active = grp
    sd.bump()
    return grp


class SKETCH_OT_make_group(bpy.types.Operator):
    """Make Group (G): wrap the selection in a group (double-click it to edit inside)"""
    bl_idname = "sketch.make_group"
    bl_label = "Make Group"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        if context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        grp = make_group(context)
        if grp is None:
            # Blender objects selected the Blender way: join them as before
            meshes = [o for o in context.selected_objects if o.type == "MESH"]
            if not meshes:
                self.report({"INFO"}, "Select something first")
                return {"CANCELLED"}
            if context.view_layer.objects.active not in meshes:
                context.view_layer.objects.active = meshes[0]
            if len(meshes) > 1:
                bpy.ops.object.join()
            context.view_layer.objects.active["sketch_kind"] = "group"
            context.view_layer.objects.active.name = "Group"
        return {"FINISHED"}


class SKETCH_OT_close_group(bpy.types.Operator):
    """Close the group being edited"""
    bl_idname = "sketch.close_group"
    bl_label = "Close Group"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        close_context(context)
        return {"FINISHED"}


class SKETCH_OT_delete_selection(bpy.types.Operator):
    """Delete the selection (SketchUp's Delete key)"""
    bl_idname = "sketch.delete_selection"
    bl_label = "Delete"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        if context.mode != "OBJECT":
            return {"PASS_THROUGH"}
        sel = read_selection(context)
        if sel.empty:
            if context.selected_objects:
                return bpy.ops.object.delete()
            return {"CANCELLED"}
        erase_selection(context)
        return {"FINISHED"}


# ------------------------------------------------------------------ shortcuts

# (operator, key, shift, ctrl, properties)
SHORTCUTS = [
    ("sketch.select_tool", "SPACE", False, False, {}),
    ("sketch.line", "L", False, False, {}),
    ("sketch.draw_rectangle", "R", False, False, {}),
    ("sketch.draw_circle", "C", False, False, {}),
    ("sketch.draw_arc", "A", False, False, {}),
    ("sketch.push_pull", "P", False, False, {}),
    ("sketch.offset", "F", False, False, {}),
    ("sketch.move", "M", False, False, {}),
    ("sketch.rotate", "Q", False, False, {}),
    ("sketch.scale", "S", False, False, {}),
    ("sketch.tape", "T", False, False, {}),
    ("sketch.paint", "B", False, False, {}),
    ("sketch.erase", "E", False, False, {}),
    ("sketch.make_group", "G", False, False, {}),
    ("sketch.delete_selection", "DEL", False, False, {}),
    ("view3d.rotate", "O", False, False, {}),
    ("view3d.move", "H", False, False, {}),
    ("view3d.zoom", "Z", False, False, {}),
    ("view3d.view_all", "Z", True, False, {}),
    ("view3d.walk", "W", True, False, {}),
]
# SketchUp keys win in every 3D View context
KEYMAPS = ("3D View", "Object Mode", "Mesh", "3D View Generic")
_keymap_items = []


def register_keymaps():
    """Shortcuts, and the drawing of edges, guides and the tools' feedback."""
    sd.register()
    kc = bpy.context.window_manager.keyconfigs.addon
    if kc is None:
        return
    for km_name in KEYMAPS:
        km = kc.keymaps.new(name=km_name, space_type="VIEW_3D" if km_name != "Object Mode" and km_name != "Mesh" else "EMPTY")
        for op, key, shift, ctrl, props in SHORTCUTS:
            kmi = km.keymap_items.new(op, key, "PRESS", shift=shift, ctrl=ctrl)
            for k, v in props.items():
                setattr(kmi.properties, k, v)
            _keymap_items.append((km, kmi))


def unregister_keymaps():
    for km, kmi in _keymap_items:
        try:
            km.keymap_items.remove(kmi)
        except Exception:
            pass
    _keymap_items.clear()
    sd.unregister()


classes = (
    SKETCH_OT_line, SKETCH_OT_draw_rectangle, SKETCH_OT_draw_circle, SKETCH_OT_draw_arc,
    SKETCH_OT_push_pull, SKETCH_OT_offset, SKETCH_OT_erase, SKETCH_OT_paint, SKETCH_OT_make_group,
    SKETCH_OT_select_tool, SKETCH_OT_tape, SKETCH_OT_move, SKETCH_OT_rotate, SKETCH_OT_scale,
    SKETCH_OT_close_group, SKETCH_OT_delete_selection,
)
