# Ben - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""SketchUp-style tools and shortcuts.

Drawing tools (Line, Rectangle, Circle, Arc) are modal: click to place points,
snap to existing corners, lock an axis with the arrow keys and type exact
dimensions at any time (e.g. `4,3` Enter for a 4 x 3 m rectangle).
"""

import math
import bmesh
import bpy
from bpy.props import FloatVectorProperty, IntProperty
from bpy_extras import view3d_utils
from mathutils import Vector, geometry as mgeo

from . import geometry as g

SNAP_PX = 12
Z = Vector((0, 0, 1))
AXES = {"RIGHT_ARROW": Vector((1, 0, 0)), "LEFT_ARROW": Vector((0, 1, 0)), "UP_ARROW": Z.copy()}
AXIS_NAMES = {"RIGHT_ARROW": "red (X)", "LEFT_ARROW": "green (Y)", "UP_ARROW": "blue (Z)"}
AXIS_COLORS = {"RIGHT_ARROW": (1.0, 0.25, 0.25, 1), "LEFT_ARROW": (0.3, 0.9, 0.3, 1), "UP_ARROW": (0.3, 0.5, 1.0, 1)}
BLUE = (0.30, 0.55, 1.0, 1.0)


# ------------------------------------------------------------------ picking

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


def ray_plane(origin, direction, point, normal):
    return mgeo.intersect_line_plane(origin, origin + direction * 1e4, point, normal)


def snap_vertex(context, event, extra=()):
    """Nearest visible mesh corner within SNAP_PX pixels of the mouse (SketchUp 'endpoint')."""
    region, rv3d = view_region(context)
    mouse = mouse_co(context, event)
    best, best_d = None, SNAP_PX
    cands = list(extra)
    objs = []
    root = bpy.data.collections.get(g.COLLECTION_NAME)
    if root:
        objs += [o for o in root.all_objects if o.type == "MESH" and o.visible_get()
                 and len(o.data.vertices) < 2000]
    origin, direction = mouse_ray(context, event)
    _l, _n, hit_obj, _i = ray_hit(context, origin, direction)
    if hit_obj is not None and hit_obj.type == "MESH" and hit_obj not in objs:
        objs.append(hit_obj)
    for o in objs[:200]:
        mw = o.matrix_world
        cands += [mw @ v.co for v in o.data.vertices[:5000]]
    for co in cands:
        p = view3d_utils.location_3d_to_region_2d(region, rv3d, co)
        if p is None:
            continue
        d = (p - mouse).length
        if d < best_d:
            best, best_d = co.copy(), d
    return best


def plane_axes(normal):
    n = Vector(normal).normalized()
    if abs(n.dot(Z)) > 0.999:
        u = Vector((1, 0, 0))
    else:
        u = Z.cross(n).normalized()
    v = n.cross(u).normalized()
    return u, v


def parse_numbers(text):
    out = []
    for part in text.replace(";", ",").split(","):
        part = part.strip()
        if not part:
            continue
        try:
            out.append(float(part))
        except ValueError:
            pass
    return out


def model_collection(context):
    from . import layers
    return layers.active_layer(context)


# ------------------------------------------------------------------ preview drawing

class Preview:
    def __init__(self):
        self.lines = []        # [(a, b, colour)]
        self.points = []       # [(co, colour)]
        self.handle = None

    def start(self):
        if self.handle is None:
            self.handle = bpy.types.SpaceView3D.draw_handler_add(self.draw, (), "WINDOW", "POST_VIEW")

    def stop(self):
        if self.handle is not None:
            bpy.types.SpaceView3D.draw_handler_remove(self.handle, "WINDOW")
            self.handle = None

    def draw(self):
        try:
            import gpu
            from gpu_extras.batch import batch_for_shader
        except Exception:
            return
        gpu.state.depth_test_set("NONE")
        try:
            sh = gpu.shader.from_builtin("UNIFORM_COLOR")
        except Exception:
            return
        gpu.state.line_width_set(2.0)
        for a, b, col in self.lines:
            batch = batch_for_shader(sh, "LINES", {"pos": [tuple(a), tuple(b)]})
            sh.uniform_float("color", col)
            batch.draw(sh)
        gpu.state.point_size_set(9.0)
        for co, col in self.points:
            batch = batch_for_shader(sh, "POINTS", {"pos": [tuple(co)]})
            sh.uniform_float("color", col)
            batch.draw(sh)
        gpu.state.line_width_set(1.0)
        gpu.state.point_size_set(1.0)


# ------------------------------------------------------------------ base modal

class DrawToolBase:
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return context.area is not None and context.area.type == "VIEW_3D"

    def invoke(self, context, event):
        if context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        self.points = []
        self.plane_point = None
        self.plane_normal = Z.copy()
        self.typed = ""
        self.lock = None
        self.cur = None
        self.snapped = False
        self.preview = Preview()
        self.preview.start()
        context.window_manager.modal_handler_add(self)
        context.window.cursor_modal_set("CROSSHAIR")
        self.update_header(context)
        return {"RUNNING_MODAL"}

    def finish(self, context, cancelled=False):
        self.preview.stop()
        context.window.cursor_modal_restore()
        context.area.header_text_set(None)
        context.area.tag_redraw()
        return {"CANCELLED"} if cancelled else {"FINISHED"}

    # --- point under the mouse, honouring snapping, plane and axis lock
    def pick(self, context, event):
        origin, direction = mouse_ray(context, event)
        snap = snap_vertex(context, event, extra=self.points)
        self.snapped = snap is not None
        if self.points and self.lock:
            axis = AXES[self.lock]
            last = self.points[-1]
            # closest point on the axis line to the mouse ray
            res = mgeo.intersect_line_line(last, last + axis, origin, origin + direction)
            if res:
                return res[0]
        if snap is not None:
            return snap
        if self.plane_point is None:
            loc, normal, _o, _i = ray_hit(context, origin, direction)
            if loc is not None:
                return loc
            p = ray_plane(origin, direction, Vector((0, 0, 0)), Z)
            return p
        p = ray_plane(origin, direction, self.plane_point, self.plane_normal)
        return p

    def set_plane_from_mouse(self, context, event, point):
        origin, direction = mouse_ray(context, event)
        loc, normal, _o, _i = ray_hit(context, origin, direction)
        if loc is not None and (loc - point).length < 0.05:
            self.plane_normal = normal
        else:
            self.plane_normal = Z.copy()
        self.plane_point = point.copy()

    def update_header(self, context):
        lock = f"  ·  locked to {AXIS_NAMES[self.lock]}" if self.lock else ""
        typed = f"  ·  typed: {self.typed}" if self.typed else ""
        snap = "  ·  endpoint" if self.snapped else ""
        context.area.header_text_set(f"{self.bl_label}: {self.hint()}{snap}{lock}{typed}"
                                     "   [arrows: lock axis · type a size + Enter · Esc: stop]")

    def hint(self):
        return ""

    def handle_common(self, context, event):
        """Returns a modal result or None if the event was not consumed."""
        if event.type in {"MIDDLEMOUSE", "WHEELUPMOUSE", "WHEELDOWNMOUSE", "TRACKPADPAN", "TRACKPADZOOM"}:
            return {"PASS_THROUGH"}
        if event.type in AXES and event.value == "PRESS":
            self.lock = None if self.lock == event.type else event.type
            self.update_header(context)
            return {"RUNNING_MODAL"}
        if event.value == "PRESS" and event.ascii and event.ascii in "0123456789.,;-":
            self.typed += event.ascii
            self.update_header(context)
            return {"RUNNING_MODAL"}
        if event.type == "BACK_SPACE" and event.value == "PRESS":
            self.typed = self.typed[:-1]
            self.update_header(context)
            return {"RUNNING_MODAL"}
        return None


def new_mesh_object(context, name, verts, faces, edges=()):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], list(edges), [list(f) for f in faces])
    me.update()
    obj = bpy.data.objects.new(name, me)
    model_collection(context).objects.link(obj)
    me.materials.append(g.mat("wall"))
    for o in context.selected_objects:
        o.select_set(False)
    obj.select_set(True)
    context.view_layer.objects.active = obj
    return obj


def lift(points, normal, amount=0.0005):
    """Nudge new faces off the surface they were drawn on, so they don't flicker."""
    n = Vector(normal)
    return [p + n * amount for p in points]


# ------------------------------------------------------------------ Line (L)

class BEN_OT_line(DrawToolBase, bpy.types.Operator):
    """Line (L): click to draw edges. Close the loop on the first point to make a face"""
    bl_idname = "ben.line"
    bl_label = "Line"

    def hint(self):
        return "click the start point" if not self.points else "click the next point (or type a length)"

    def modal(self, context, event):
        context.area.tag_redraw()
        r = self.handle_common(context, event)
        if r is not None:
            return r
        if event.type == "MOUSEMOVE":
            self.cur = self.pick(context, event)
            self.refresh_preview()
            self.update_header(context)
        elif event.type in {"RET", "NUMPAD_ENTER"} and event.value == "PRESS":
            nums = parse_numbers(self.typed)
            self.typed = ""
            if nums and self.points and self.cur is not None:
                d = self.cur - self.points[-1]
                if d.length > 1e-9:
                    self.add_point(context, self.points[-1] + d.normalized() * nums[0])
                    return {"RUNNING_MODAL"}
            return self.commit(context, closed=False)
        elif event.type == "LEFTMOUSE" and event.value == "PRESS":
            p = self.pick(context, event)
            if p is None:
                return {"RUNNING_MODAL"}
            if not self.points:
                self.set_plane_from_mouse(context, event, p)
            if len(self.points) >= 3 and (p - self.points[0]).length < 1e-4:
                return self.commit(context, closed=True)
            self.add_point(context, p)
        elif event.type in {"RIGHTMOUSE", "ESC"} and event.value == "PRESS":
            return self.commit(context, closed=False)
        return {"RUNNING_MODAL"}

    def add_point(self, context, p):
        self.points.append(p.copy())
        self.lock = None
        self.refresh_preview()
        self.update_header(context)

    def refresh_preview(self):
        pts = self.points + ([self.cur] if self.cur is not None else [])
        col = AXIS_COLORS.get(self.lock, BLUE)
        self.preview.lines = [(pts[i], pts[i + 1], col) for i in range(len(pts) - 1)]
        self.preview.points = [(p, BLUE) for p in self.points[:1]]
        if self.cur is not None:
            self.preview.points.append((self.cur, (1, 1, 1, 1) if self.snapped else BLUE))

    def commit(self, context, closed):
        if len(self.points) < 2:
            return self.finish(context, cancelled=True)
        if closed:
            n = mgeo.normal(self.points) if len(self.points) >= 3 else Z
            verts = lift(self.points, n)
            new_mesh_object(context, "Face", verts, [range(len(verts))])
        else:
            edges = [(i, i + 1) for i in range(len(self.points) - 1)]
            new_mesh_object(context, "Lines", self.points, [], edges)
        return self.finish(context)


# ------------------------------------------------------------------ Rectangle (R)

class BEN_OT_draw_rectangle(DrawToolBase, bpy.types.Operator):
    """Rectangle (R): click two opposite corners, or type width,depth"""
    bl_idname = "ben.draw_rectangle"
    bl_label = "Rectangle"

    def hint(self):
        return "click the first corner" if not self.points else "click the opposite corner (or type width,depth)"

    def corners(self, a, b):
        u, v = plane_axes(self.plane_normal)
        d = b - a
        du, dv = d.dot(u), d.dot(v)
        return [a, a + u * du, a + u * du + v * dv, a + v * dv], du, dv

    def modal(self, context, event):
        context.area.tag_redraw()
        r = self.handle_common(context, event)
        if r is not None:
            return r
        if event.type == "MOUSEMOVE":
            self.cur = self.pick(context, event)
            if self.points and self.cur is not None:
                c, du, dv = self.corners(self.points[0], self.cur)
                self.preview.lines = [(c[i], c[(i + 1) % 4], BLUE) for i in range(4)]
                context.area.header_text_set(f"Rectangle: {abs(du):.3f} × {abs(dv):.3f} m"
                                             + (f"   typed: {self.typed}" if self.typed else ""))
            self.preview.points = [(self.cur, (1, 1, 1, 1) if self.snapped else BLUE)] if self.cur else []
        elif event.type in {"RET", "NUMPAD_ENTER"} and event.value == "PRESS" and self.points:
            nums = parse_numbers(self.typed)
            if len(nums) >= 2:
                u, v = plane_axes(self.plane_normal)
                sx = math.copysign(1, (self.cur - self.points[0]).dot(u)) if self.cur else 1
                sy = math.copysign(1, (self.cur - self.points[0]).dot(v)) if self.cur else 1
                b = self.points[0] + u * nums[0] * sx + v * nums[1] * sy
                return self.make(context, self.points[0], b)
        elif event.type == "LEFTMOUSE" and event.value == "PRESS":
            p = self.pick(context, event)
            if p is None:
                return {"RUNNING_MODAL"}
            if not self.points:
                self.set_plane_from_mouse(context, event, p)
                self.points.append(p)
                self.update_header(context)
            else:
                return self.make(context, self.points[0], p)
        elif event.type in {"RIGHTMOUSE", "ESC"} and event.value == "PRESS":
            return self.finish(context, cancelled=True)
        return {"RUNNING_MODAL"}

    def make(self, context, a, b):
        c, du, dv = self.corners(a, b)
        if abs(du) < 1e-5 or abs(dv) < 1e-5:
            return {"RUNNING_MODAL"}
        new_mesh_object(context, "Rectangle", lift(c, self.plane_normal), [range(4)])
        return self.finish(context)


# ------------------------------------------------------------------ Circle (C) and Arc (A)

class BEN_OT_draw_circle(DrawToolBase, bpy.types.Operator):
    """Circle (C): click the centre, then the radius (or type it)"""
    bl_idname = "ben.draw_circle"
    bl_label = "Circle"

    segments: IntProperty(name="Sides", default=24, min=3, max=256)

    def hint(self):
        return "click the centre" if not self.points else "click the radius (or type it)"

    def ring(self, centre, r):
        u, v = plane_axes(self.plane_normal)
        n = self.segments
        return [centre + (u * math.cos(2 * math.pi * i / n) + v * math.sin(2 * math.pi * i / n)) * r
                for i in range(n)]

    def modal(self, context, event):
        context.area.tag_redraw()
        r = self.handle_common(context, event)
        if r is not None:
            return r
        if event.type == "MOUSEMOVE":
            self.cur = self.pick(context, event)
            if self.points and self.cur is not None:
                rad = (self.cur - self.points[0]).length
                ring = self.ring(self.points[0], rad)
                self.preview.lines = [(ring[i], ring[(i + 1) % len(ring)], BLUE) for i in range(len(ring))]
                context.area.header_text_set(f"Circle: radius {rad:.3f} m"
                                             + (f"   typed: {self.typed}" if self.typed else ""))
        elif event.type in {"RET", "NUMPAD_ENTER"} and event.value == "PRESS" and self.points:
            nums = parse_numbers(self.typed)
            if nums:
                return self.make(context, nums[0])
        elif event.type == "LEFTMOUSE" and event.value == "PRESS":
            p = self.pick(context, event)
            if p is None:
                return {"RUNNING_MODAL"}
            if not self.points:
                self.set_plane_from_mouse(context, event, p)
                self.points.append(p)
            else:
                return self.make(context, (p - self.points[0]).length)
        elif event.type in {"RIGHTMOUSE", "ESC"} and event.value == "PRESS":
            return self.finish(context, cancelled=True)
        return {"RUNNING_MODAL"}

    def make(self, context, rad):
        if rad < 1e-5:
            return {"RUNNING_MODAL"}
        ring = lift(self.ring(self.points[0], rad), self.plane_normal)
        new_mesh_object(context, "Circle", ring, [range(len(ring))])
        return self.finish(context)


class BEN_OT_draw_arc(DrawToolBase, bpy.types.Operator):
    """Arc (A): click the start, the end, then the bulge"""
    bl_idname = "ben.draw_arc"
    bl_label = "Arc"

    segments: IntProperty(name="Segments", default=16, min=2, max=256)

    def hint(self):
        return ["click the start", "click the end", "click the bulge (or type it)"][min(len(self.points), 2)]

    def arc_points(self, a, b, c):
        """Arc from a to b through c (all on the drawing plane)."""
        n = Vector(self.plane_normal).normalized()
        ab, ac = b - a, c - a
        cross = ab.cross(ac)
        if cross.length < 1e-9:
            return [a, b]
        # circumcentre of triangle a, b, c
        centre = a + (cross.cross(ab) * ac.length_squared + ac.cross(cross) * ab.length_squared) / (2 * cross.length_squared)
        R = (a - centre).length
        u = (a - centre).normalized()
        w = n.cross(u).normalized()

        def ang(p):
            d = p - centre
            t = math.atan2(d.dot(w), d.dot(u))
            return t % (2 * math.pi)
        tb, tc = ang(b), ang(c)
        span = tb if tc < tb else tb - 2 * math.pi
        return [centre + (u * math.cos(span * i / self.segments) + w * math.sin(span * i / self.segments)) * R
                for i in range(self.segments + 1)]

    def modal(self, context, event):
        context.area.tag_redraw()
        r = self.handle_common(context, event)
        if r is not None:
            return r
        if event.type == "MOUSEMOVE":
            self.cur = self.pick(context, event)
            if self.cur is None:
                return {"RUNNING_MODAL"}
            if len(self.points) == 1:
                self.preview.lines = [(self.points[0], self.cur, BLUE)]
            elif len(self.points) == 2:
                pts = self.arc_points(self.points[0], self.points[1], self.cur)
                self.preview.lines = [(pts[i], pts[i + 1], BLUE) for i in range(len(pts) - 1)]
        elif event.type == "LEFTMOUSE" and event.value == "PRESS":
            p = self.pick(context, event)
            if p is None:
                return {"RUNNING_MODAL"}
            if not self.points:
                self.set_plane_from_mouse(context, event, p)
            if len(self.points) < 2:
                self.points.append(p)
                self.update_header(context)
            else:
                pts = self.arc_points(self.points[0], self.points[1], p)
                new_mesh_object(context, "Arc", pts, [], [(i, i + 1) for i in range(len(pts) - 1)])
                return self.finish(context)
        elif event.type in {"RIGHTMOUSE", "ESC"} and event.value == "PRESS":
            return self.finish(context, cancelled=True)
        return {"RUNNING_MODAL"}


# ------------------------------------------------------------------ face tools (P, F)

def select_face_under_mouse(context, event):
    """Enter edit mode on the object under the mouse with only the clicked face selected."""
    origin, direction = mouse_ray(context, event)
    loc, normal, obj, index = ray_hit(context, origin, direction)
    if obj is None or obj.type != "MESH":
        if context.mode == "EDIT_MESH":
            return True  # use the current selection
        return False
    if context.mode == "EDIT_MESH" and context.edit_object == obj:
        region, _ = view_region(context)
        co = mouse_co(context, event)
        bpy.ops.mesh.select_all(action="DESELECT")
        with context.temp_override(region=region):
            bpy.ops.view3d.select(location=(int(co.x), int(co.y)))
        return True
    if context.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    for o in context.selected_objects:
        o.select_set(False)
    obj.select_set(True)
    context.view_layer.objects.active = obj
    me = obj.data
    for p in me.polygons:
        p.select = False
    for e in me.edges:
        e.select = False
    for v in me.vertices:
        v.select = False
    if 0 <= index < len(me.polygons):
        poly = me.polygons[index]
        poly.select = True
        for vi in poly.vertices:
            me.vertices[vi].select = True
    bpy.ops.object.mode_set(mode="EDIT")
    context.tool_settings.mesh_select_mode = (False, False, True)
    return True


class BEN_OT_push_pull(bpy.types.Operator):
    """Push/Pull (P): click a face and move the mouse to extrude it. Type a distance + Enter for an exact size"""
    bl_idname = "ben.push_pull"
    bl_label = "Push/Pull"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return context.area is not None and context.area.type == "VIEW_3D"

    def invoke(self, context, event):
        if not select_face_under_mouse(context, event):
            self.report({"INFO"}, "Point at a face, then press P")
            return {"CANCELLED"}
        region, _ = view_region(context)
        with context.temp_override(region=region):
            return bpy.ops.view3d.edit_mesh_extrude_move_normal("INVOKE_DEFAULT")


class BEN_OT_offset(bpy.types.Operator):
    """Offset (F): click a face to draw a smaller copy inside its edges"""
    bl_idname = "ben.offset"
    bl_label = "Offset"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return context.area is not None and context.area.type == "VIEW_3D"

    def invoke(self, context, event):
        if not select_face_under_mouse(context, event):
            self.report({"INFO"}, "Point at a face, then press F")
            return {"CANCELLED"}
        region, _ = view_region(context)
        with context.temp_override(region=region):
            return bpy.ops.mesh.inset("INVOKE_DEFAULT")


# ------------------------------------------------------------------ Eraser (E), Paint (B), Group (G)

class BEN_OT_erase(bpy.types.Operator):
    """Eraser (E): delete what is under the mouse (in edit mode: the selection)"""
    bl_idname = "ben.erase"
    bl_label = "Eraser"
    bl_options = {"REGISTER", "UNDO"}

    def invoke(self, context, event):
        if context.mode == "EDIT_MESH":
            sel = context.tool_settings.mesh_select_mode
            kind = "VERT" if sel[0] else ("EDGE" if sel[1] else "FACE")
            return bpy.ops.mesh.delete(type=kind)
        origin, direction = mouse_ray(context, event)
        _l, _n, obj, _i = ray_hit(context, origin, direction)
        if obj is None:
            if context.selected_objects:
                return bpy.ops.object.delete()
            return {"CANCELLED"}
        bpy.data.objects.remove(obj, do_unlink=True)
        return {"FINISHED"}


def paint_material(rgb):
    hexname = "#%02X%02X%02X" % tuple(int(max(0, min(1, c)) * 255) for c in rgb)
    return g.material(f"Ben paint {hexname}", tuple(rgb), 0.6)


class BEN_OT_paint(bpy.types.Operator):
    """Paint Bucket (B): click a face (or object) to paint it with the current colour"""
    bl_idname = "ben.paint"
    bl_label = "Paint Bucket"
    bl_options = {"REGISTER", "UNDO"}

    def invoke(self, context, event):
        origin, direction = mouse_ray(context, event)
        _l, _n, obj, index = ray_hit(context, origin, direction)
        if obj is None or obj.type != "MESH":
            return {"CANCELLED"}
        mat = paint_material(context.scene.ben_paint_color)
        me = obj.data
        if mat.name not in me.materials:
            me.materials.append(mat)
        slot = list(me.materials).index(mat)
        if event.shift or len(me.polygons) <= 1:
            for p in me.polygons:
                p.material_index = slot
        elif 0 <= index < len(me.polygons):
            me.polygons[index].material_index = slot
        me.update()
        return {"FINISHED"}


class BEN_OT_make_group(bpy.types.Operator):
    """Make Group (G): join the selected shapes into one object"""
    bl_idname = "ben.make_group"
    bl_label = "Make Group"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return len([o for o in context.selected_objects if o.type == "MESH"]) >= 1

    def execute(self, context):
        if context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        meshes = [o for o in context.selected_objects if o.type == "MESH"]
        if context.view_layer.objects.active not in meshes:
            context.view_layer.objects.active = meshes[0]
        if len(meshes) > 1:
            bpy.ops.object.join()
        context.view_layer.objects.active.name = "Group"
        return {"FINISHED"}


class BEN_OT_select_tool(bpy.types.Operator):
    """Select (Space)"""
    bl_idname = "ben.select_tool"
    bl_label = "Select"

    def execute(self, context):
        if context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        bpy.ops.wm.tool_set_by_id(name="builtin.select_box")
        return {"FINISHED"}


class BEN_OT_tape(bpy.types.Operator):
    """Tape Measure (T)"""
    bl_idname = "ben.tape"
    bl_label = "Tape Measure"

    def execute(self, context):
        bpy.ops.wm.tool_set_by_id(name="builtin.measure")
        return {"FINISHED"}


# ------------------------------------------------------------------ shortcuts

# (operator, key, shift, ctrl, properties)
SHORTCUTS = [
    ("ben.select_tool", "SPACE", False, False, {}),
    ("ben.line", "L", False, False, {}),
    ("ben.draw_rectangle", "R", False, False, {}),
    ("ben.draw_circle", "C", False, False, {}),
    ("ben.draw_arc", "A", False, False, {}),
    ("ben.push_pull", "P", False, False, {}),
    ("ben.offset", "F", False, False, {}),
    ("transform.translate", "M", False, False, {}),
    ("transform.rotate", "Q", False, False, {}),
    ("transform.resize", "S", False, False, {}),
    ("ben.tape", "T", False, False, {}),
    ("ben.paint", "B", False, False, {}),
    ("ben.erase", "E", False, False, {}),
    ("ben.make_group", "G", False, False, {}),
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


classes = (
    BEN_OT_line, BEN_OT_draw_rectangle, BEN_OT_draw_circle, BEN_OT_draw_arc,
    BEN_OT_push_pull, BEN_OT_offset, BEN_OT_erase, BEN_OT_paint, BEN_OT_make_group,
    BEN_OT_select_tool, BEN_OT_tape,
)
