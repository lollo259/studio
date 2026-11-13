# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""What the SketchUp tools draw in the 3D view.

- the model's edges (black lines, like SketchUp; hidden seams and softened
  edges are not drawn), the selection (blue), guides (dashed);
- the active tool's overlay: rubber band, inference dots and lines in
  SketchUp's colours, and the tooltip next to the cursor.
"""

import bpy
import numpy as np
from mathutils import Vector

try:
    import gpu
    import blf
    from gpu_extras.batch import batch_for_shader
except Exception:  # pragma: no cover - no GPU module (e.g. some background builds)
    gpu = None
    blf = None
    batch_for_shader = None

EDGE_COLOR = (0.08, 0.08, 0.09, 1.0)
SEL_COLOR = (0.10, 0.35, 1.0, 1.0)
SEL_FACE = (0.20, 0.40, 1.0, 0.28)
HOVER_FACE = (0.20, 0.40, 1.0, 0.18)
GUIDE_COLOR = (0.25, 0.25, 0.28, 1.0)
CONTEXT_COLOR = (0.35, 0.35, 0.38, 1.0)


class Overlay:
    """Filled by the active tool, drawn every redraw."""

    def __init__(self):
        self.owner = None
        self.clear()

    def clear(self):
        self.lines = []      # (a, b, colour, width, dashed)
        self.points = []     # (p, colour, radius_px)
        self.faces = []      # (list of triangles [(a,b,c)], colour)
        self.squares = []    # (p, colour, half_size_px) - grips
        self.tooltip = None  # (text, colour)
        self.mouse = None    # (x, y) region pixels
        self.rect = None     # selection rectangle ((x0,y0),(x1,y1), dashed)


OVERLAY = Overlay()
_handles = []
_cache = {"version": 0, "data": {}}


def bump():
    """The model changed: rebuild the cached edge lists."""
    _cache["version"] += 1
    _cache["data"].clear()


# ------------------------------------------------------------------ gpu helpers

def _shader(name):
    try:
        return gpu.shader.from_builtin(name)
    except Exception:
        return None


def draw_lines(coords, color, width=1.0):
    if not coords or gpu is None:
        return
    coords = [tuple(c) if len(c) == 3 else (c[0], c[1], 0.0) for c in coords]
    sh = _shader("POLYLINE_UNIFORM_COLOR")
    if sh is not None:
        try:
            vp = gpu.state.viewport_get()
            sh.bind()
            sh.uniform_float("viewportSize", (float(vp[2]), float(vp[3])))
            sh.uniform_float("lineWidth", float(width))
            sh.uniform_float("color", color)
            batch_for_shader(sh, "LINES", {"pos": coords}).draw(sh)
            return
        except Exception:
            pass
    sh = _shader("UNIFORM_COLOR")
    if sh is None:
        return
    gpu.state.line_width_set(width)
    sh.bind()
    sh.uniform_float("color", color)
    batch_for_shader(sh, "LINES", {"pos": coords}).draw(sh)
    gpu.state.line_width_set(1.0)


def draw_tris(coords, color):
    if not coords or gpu is None:
        return
    sh = _shader("UNIFORM_COLOR")
    if sh is None:
        return
    coords = [tuple(c) if len(c) == 3 else (c[0], c[1], 0.0) for c in coords]
    sh.bind()
    sh.uniform_float("color", color)
    batch_for_shader(sh, "TRIS", {"pos": coords}).draw(sh)


def _disc(x, y, r, n=14):
    import math
    pts = []
    for i in range(n):
        a0 = 2 * math.pi * i / n
        a1 = 2 * math.pi * (i + 1) / n
        pts += [(x, y, 0.0), (x + r * math.cos(a0), y + r * math.sin(a0), 0.0),
                (x + r * math.cos(a1), y + r * math.sin(a1), 0.0)]
    return pts


def _square(x, y, r):
    return [(x - r, y - r, 0), (x + r, y - r, 0), (x + r, y + r, 0),
            (x - r, y - r, 0), (x + r, y + r, 0), (x - r, y + r, 0)]


def _dashes(a, b, dash=6.0, gap=4.0):
    d = (b - a)
    L = d.length
    if L < 1e-6:
        return []
    u = d / L
    out = []
    s = 0.0
    while s < L:
        e = min(L, s + dash)
        out += [a + u * s, a + u * e]
        s = e + gap
    return out


# ------------------------------------------------------------------ projection with clipping

def _clip_segment(M, a, b, W, H):
    """3D segment -> 2D pixel segment, clipped to the front of the camera (None if behind)."""
    ca = M @ Vector((a[0], a[1], a[2], 1.0))
    cb = M @ Vector((b[0], b[1], b[2], 1.0))
    eps = 1e-5
    if ca.w < eps and cb.w < eps:
        return None
    if ca.w < eps:
        t = (eps - ca.w) / (cb.w - ca.w)
        ca = ca + (cb - ca) * t
    elif cb.w < eps:
        t = (eps - cb.w) / (ca.w - cb.w)
        cb = cb + (ca - cb) * t

    def px(c):
        return Vector(((c.x / c.w + 1) * 0.5 * W, (c.y / c.w + 1) * 0.5 * H))
    return px(ca), px(cb)


def _project(M, p, W, H):
    c = M @ Vector((p[0], p[1], p[2], 1.0))
    if c.w <= 1e-6:
        return None
    return Vector(((c.x / c.w + 1) * 0.5 * W, (c.y / c.w + 1) * 0.5 * H))


# ------------------------------------------------------------------ model edges (POST_VIEW)

def _model_objects(context):
    out = []
    for obj in context.visible_objects:
        if obj.type == "MESH" and obj.get("sketch_kind") in ("model", "group"):
            out.append(obj)
    return out


def _mesh_lines(obj):
    """World-space hard edges (E,2,3), selected edges, selected face triangles of a Sketch object."""
    me = obj.data
    key = (obj.name, me.name, _cache["version"], tuple(round(x, 6) for r in obj.matrix_world for x in r))
    hit = _cache["data"].get(obj.name)
    if hit is not None and hit[0] == key:
        return hit[1]
    nv, ne = len(me.vertices), len(me.edges)
    co = np.empty(nv * 3, dtype=np.float32)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3).astype(np.float64)
    mw = np.array([list(r) for r in obj.matrix_world])
    co = co @ mw[:3, :3].T + mw[:3, 3]
    ev = np.empty(ne * 2, dtype=np.int32)
    me.edges.foreach_get("vertices", ev)
    ev = ev.reshape(-1, 2)
    soft = np.zeros(ne, dtype=np.int32)
    at = me.attributes.get("sketch_soft")
    if at is not None and at.domain == "EDGE" and len(at.data) == ne:
        tmp = np.empty(ne, dtype=np.int32)
        try:
            at.data.foreach_get("value", tmp)
            soft = tmp
        except Exception:
            pass
    esel = np.zeros(ne, dtype=bool)
    try:
        me.edges.foreach_get("select", esel)
    except Exception:
        pass
    hard = co[ev[soft == 0]] if ne else np.zeros((0, 2, 3))
    sel = co[ev[esel & (soft == 0)]] if ne else np.zeros((0, 2, 3))
    tris = np.zeros((0, 3, 3))
    np_ = len(me.polygons)
    if np_:
        psel = np.zeros(np_, dtype=bool)
        try:
            me.polygons.foreach_get("select", psel)
        except Exception:
            pass
        if psel.any():
            me.calc_loop_triangles()
            nt = len(me.loop_triangles)
            tv = np.empty(nt * 3, dtype=np.int32)
            tp = np.empty(nt, dtype=np.int32)
            me.loop_triangles.foreach_get("vertices", tv)
            me.loop_triangles.foreach_get("polygon_index", tp)
            tv = tv.reshape(-1, 3)
            tris = co[tv[psel[tp]]]
    data = (hard, sel, tris)
    _cache["data"][obj.name] = (key, data)
    return data


def _toward_eye(arr, rv3d):
    """Pull lines a hair towards the viewer so they sit on top of their faces."""
    if len(arr) == 0:
        return arr
    vi = rv3d.view_matrix.inverted()
    if rv3d.is_perspective:
        eye = np.array(tuple(vi.translation))
        return arr + (eye - arr) * 0.002
    back = np.array(tuple(vi.col[2].xyz))
    return arr + back * (0.002 * rv3d.view_distance)


def draw_model():
    if gpu is None:
        return
    context = bpy.context
    rv3d = context.region_data
    if rv3d is None:
        return
    try:
        gpu.state.blend_set("ALPHA")
        gpu.state.depth_test_set("LESS_EQUAL")
        gpu.state.depth_mask_set(False)
        sel_objs = []
        for obj in _model_objects(context):
            hard, sel, tris = _mesh_lines(obj)
            if len(hard):
                pts = _toward_eye(hard.reshape(-1, 3), rv3d)
                draw_lines([tuple(p) for p in pts], EDGE_COLOR, 1.0)
            if len(tris):
                pts = _toward_eye(tris.reshape(-1, 3), rv3d)
                draw_tris([tuple(p) for p in pts], SEL_FACE)
            if len(sel):
                pts = _toward_eye(sel.reshape(-1, 3), rv3d)
                draw_lines([tuple(p) for p in pts], SEL_COLOR, 2.5)
            if obj.get("sketch_kind") == "group" and obj.select_get():
                sel_objs.append(obj)
        gpu.state.depth_test_set("NONE")
        for obj in sel_objs:
            draw_lines(_bbox_lines(obj), SEL_COLOR, 1.5)
        ctx = context.scene.get("sketch_context")
        cobj = bpy.data.objects.get(ctx) if ctx else None
        if cobj is not None:
            pts = _bbox_lines(cobj)
            dashed = []
            for i in range(0, len(pts), 2):
                dashed += _dashes(Vector(pts[i]), Vector(pts[i + 1]), 0.05 * max(cobj.dimensions), 0.03 * max(cobj.dimensions))
            draw_lines([tuple(p) for p in dashed], CONTEXT_COLOR, 1.0)
        # tool faces (hovered face of Push/Pull, Paint...)
        for tris_, col in OVERLAY.faces:
            flat = [tuple(p) for t in tris_ for p in t]
            draw_tris(flat, col)
    except Exception as exc:  # never break the viewport
        print("Sketch draw:", exc)
    finally:
        gpu.state.depth_test_set("NONE")
        gpu.state.depth_mask_set(True)
        gpu.state.blend_set("NONE")


def _bbox_lines(obj):
    mw = obj.matrix_world
    c = [mw @ Vector(b) for b in obj.bound_box]
    idx = [(0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4), (0, 4), (1, 5), (2, 6), (3, 7)]
    out = []
    for i, j in idx:
        out += [tuple(c[i]), tuple(c[j])]
    return out


# ------------------------------------------------------------------ overlay (POST_PIXEL)

def draw_overlay():
    if gpu is None:
        return
    context = bpy.context
    region = context.region
    rv3d = context.region_data
    if rv3d is None or region is None:
        return
    M = rv3d.perspective_matrix
    W, H = region.width, region.height
    scale = context.preferences.system.ui_scale
    try:
        gpu.state.blend_set("ALPHA")
        gpu.state.depth_test_set("NONE")
        # guides
        guides = list(context.scene.get("sketch_guides_lines") or [])
        segs = []
        for i in range(0, len(guides) - 5, 6):
            p = Vector(guides[i:i + 3])
            d = Vector(guides[i + 3:i + 6]).normalized()
            s = _clip_segment(M, p - d * 1000.0, p + d * 1000.0, W, H)
            if s:
                segs += _dashes(s[0], s[1], 7 * scale, 5 * scale)
        draw_lines(segs, GUIDE_COLOR, 1.0)
        gp = list(context.scene.get("sketch_guides_points") or [])
        for i in range(0, len(gp) - 2, 3):
            s = _project(M, gp[i:i + 3], W, H)
            if s is not None:
                draw_lines([(s.x - 4, s.y - 4), (s.x + 4, s.y + 4), (s.x - 4, s.y + 4), (s.x + 4, s.y - 4)],
                           GUIDE_COLOR, 1.5)
        ov = OVERLAY
        if ov.owner is None:
            return
        # lines
        for a, b, col, width, dashed in ov.lines:
            s = _clip_segment(M, a, b, W, H)
            if s is None:
                continue
            if dashed:
                draw_lines(_dashes(s[0], s[1], 4 * scale, 4 * scale), col, width * scale)
            else:
                draw_lines([s[0], s[1]], col, width * scale)
        # grips
        for p, col, r in ov.squares:
            s = _project(M, p, W, H)
            if s is not None:
                draw_tris(_square(s.x, s.y, (r + 1.5) * scale), (0, 0, 0, 1))
                draw_tris(_square(s.x, s.y, r * scale), col)
        # inference dots, with a dark outline like SketchUp's
        for p, col, r in ov.points:
            s = _project(M, p, W, H)
            if s is not None:
                draw_tris(_disc(s.x, s.y, (r + 1.5) * scale), (0.05, 0.05, 0.05, 1))
                draw_tris(_disc(s.x, s.y, r * scale), col)
        if ov.rect is not None:
            (x0, y0), (x1, y1), dashed = ov.rect
            corners = [Vector((x0, y0)), Vector((x1, y0)), Vector((x1, y1)), Vector((x0, y1))]
            seg = []
            for i in range(4):
                a, b = corners[i], corners[(i + 1) % 4]
                seg += _dashes(a, b, 5, 4) if dashed else [a, b]
            draw_lines(seg, (0.1, 0.1, 0.1, 1), 1.0)
        # tooltip
        if ov.tooltip and ov.mouse is not None and blf is not None:
            text, col = ov.tooltip
            if text:
                size = int(12 * scale)
                try:
                    blf.size(0, size)
                except TypeError:
                    blf.size(0, size, 72)
                w, h = blf.dimensions(0, text)
                x = ov.mouse[0] + 16 * scale
                y = ov.mouse[1] - 26 * scale
                pad = 4 * scale
                draw_tris([(x - pad, y - pad, 0), (x + w + pad, y - pad, 0), (x + w + pad, y + h + pad, 0),
                           (x - pad, y - pad, 0), (x + w + pad, y + h + pad, 0), (x - pad, y + h + pad, 0)],
                          (1.0, 1.0, 0.88, 0.95))
                draw_lines([(x - pad, y - pad), (x + w + pad, y - pad), (x + w + pad, y - pad),
                            (x + w + pad, y + h + pad), (x + w + pad, y + h + pad), (x - pad, y + h + pad),
                            (x - pad, y + h + pad), (x - pad, y - pad)], (0.3, 0.3, 0.3, 1), 1.0)
                blf.color(0, col[0] * 0.8, col[1] * 0.8, col[2] * 0.8, 1.0)
                blf.position(0, x, y, 0)
                blf.draw(0, text)
    except Exception as exc:
        print("Sketch overlay:", exc)
    finally:
        gpu.state.blend_set("NONE")


# ------------------------------------------------------------------ handlers

def _on_depsgraph(scene, depsgraph):
    for u in depsgraph.updates:
        if u.is_updated_geometry or u.is_updated_transform:
            bump()
            return


def register():
    if _handles:
        return
    try:
        _handles.append(bpy.types.SpaceView3D.draw_handler_add(draw_model, (), "WINDOW", "POST_VIEW"))
        _handles.append(bpy.types.SpaceView3D.draw_handler_add(draw_overlay, (), "WINDOW", "POST_PIXEL"))
    except Exception as exc:
        print("Sketch: no draw handlers:", exc)
    if _on_depsgraph not in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.append(_on_depsgraph)


def unregister():
    for h in _handles:
        try:
            bpy.types.SpaceView3D.draw_handler_remove(h, "WINDOW")
        except Exception:
            pass
    _handles.clear()
    if _on_depsgraph in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(_on_depsgraph)


def redraw_all(context=None):
    context = context or bpy.context
    try:
        for win in context.window_manager.windows:
            for area in win.screen.areas:
                if area.type == "VIEW_3D":
                    area.tag_redraw()
    except Exception:
        pass
