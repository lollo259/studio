# Ben - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fabric: turn a drawn shape into cloth, plastic or cardboard.

Make fabric -> it floats (no gravity) so you can place it.
Drop        -> it falls and drapes over whatever is below (real simulation).
Keep shape  -> freezes the result as normal geometry; you can move it and drop it again.
"""

import bmesh
import bpy
from bpy.props import EnumProperty, FloatProperty, IntProperty
from mathutils import Vector

from . import geometry as g

# name: (mass kg/m2, tension, bending, colour, roughness, thickness m)
FABRICS = {
    "COTTON": (0.3, 15, 0.5, (0.85, 0.83, 0.78), 0.9, 0.002),
    "SILK": (0.15, 5, 0.05, (0.75, 0.75, 0.80), 0.35, 0.001),
    "DENIM": (1.0, 40, 10, (0.10, 0.16, 0.30), 0.85, 0.003),
    "LEATHER": (0.4, 80, 150, (0.20, 0.12, 0.07), 0.55, 0.003),
    "PLASTIC": (0.15, 40, 20, (0.90, 0.93, 0.97), 0.05, 0.001),
    "CARDBOARD": (0.5, 200, 3000, (0.62, 0.48, 0.32), 0.85, 0.004),
}
FABRIC_ITEMS = [
    ("COTTON", "Cotton", "Soft cloth: sheets, tablecloths, curtains"),
    ("SILK", "Silk", "Very light, flowing"),
    ("DENIM", "Denim / canvas", "Heavy cloth"),
    ("LEATHER", "Leather", "Thick, holds its shape"),
    ("PLASTIC", "Plastic sheet", "Thin, glossy, crinkly"),
    ("CARDBOARD", "Cardboard", "Stiff: bends a little, mostly stays flat"),
]


def is_fabric(obj):
    return obj is not None and obj.type == "MESH" and (
        obj.modifiers.get("Ben fabric") is not None or obj.get("ben_rigid", False))


def make_grid(obj, cell):
    """Give the shape enough points to fold: quads become a regular grid."""
    me = obj.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    if len(bm.faces) == 1 and len(bm.faces[0].verts) == 4:
        a, b, c, d = [v.co.copy() for v in bm.faces[0].verts]
        nu = max(2, min(80, round((b - a).length / cell)))
        nv = max(2, min(80, round((d - a).length / cell)))
        bm.clear()
        grid = [[bm.verts.new(a.lerp(b, i / nu).lerp(d.lerp(c, i / nu), j / nv)) for i in range(nu + 1)]
                for j in range(nv + 1)]
        for j in range(nv):
            for i in range(nu):
                bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
    else:
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        longest = max((e.calc_length() for e in bm.edges), default=cell)
        cuts = max(1, min(40, int(longest / cell)))
        bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=cuts, use_grid_fill=True)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True


def surfaces(context, exclude):
    """Everything solid in the scene that fabric should land on."""
    return [o for o in context.scene.objects
            if o.type == "MESH" and o.visible_get() and o not in exclude and not is_fabric(o)
            and not any(m.type == "FLUID" for m in o.modifiers)]


def fabric_material(kind):
    _m, _t, _b, rgb, rough, _th = FABRICS[kind]
    mat = g.material(f"Ben {kind.lower()}", rgb, rough)
    if kind == "PLASTIC" and mat.node_tree:
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            for key in ("Transmission Weight", "Transmission"):
                if key in bsdf.inputs:
                    bsdf.inputs[key].default_value = 0.7
                    break
    return mat


class BEN_OT_make_fabric(bpy.types.Operator):
    """Turn the selected flat shape into fabric. It floats until you press Drop"""
    bl_idname = "ben.make_fabric"
    bl_label = "Make fabric"
    bl_options = {"REGISTER", "UNDO"}

    kind: EnumProperty(name="Material", items=FABRIC_ITEMS, default="COTTON")
    detail: FloatProperty(name="Detail", default=0.04, min=0.005, max=0.5, unit="LENGTH",
                          description="Size of the folds the fabric can make (smaller = finer, slower)")

    @classmethod
    def poll(cls, context):
        o = context.active_object
        return o is not None and o.type == "MESH" and not is_fabric(o)

    def execute(self, context):
        obj = context.active_object
        if context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        mass, tension, bending, _rgb, _r, thick = FABRICS[self.kind]
        if self.kind == "CARDBOARD":
            # cardboard keeps its shape: a rigid sheet that falls, tips and lands
            sol = obj.modifiers.new("Ben fabric thickness", "SOLIDIFY")
            sol.thickness = thick
            obj["ben_rigid"] = True
            obj["ben_fabric"] = self.kind
            obj.data.materials.clear()
            obj.data.materials.append(fabric_material(self.kind))
            self.report({"INFO"}, "Cardboard is floating. Move it (M), then press Drop")
            return {"FINISHED"}
        dims = obj.dimensions
        cell = max(self.detail, max(dims) / 80)
        make_grid(obj, cell)
        m = obj.modifiers.new("Ben fabric", "CLOTH")
        s = m.settings
        s.quality = 8
        s.mass = mass
        s.air_damping = 1.0
        for attr in ("tension_stiffness", "compression_stiffness", "shear_stiffness"):
            setattr(s, attr, tension)
        s.bending_stiffness = bending
        s.effector_weights.gravity = 0.0          # floats while you place it
        cs = m.collision_settings
        cs.use_collision = True
        cs.distance_min = max(0.002, thick)
        cs.use_self_collision = True
        cs.self_distance_min = max(0.002, thick)
        cs.collision_quality = 4
        sol = obj.modifiers.new("Ben fabric thickness", "SOLIDIFY")
        sol.thickness = thick
        sol.offset = 0.0
        obj.data.materials.clear()
        obj.data.materials.append(fabric_material(self.kind))
        obj["ben_fabric"] = self.kind
        context.scene.frame_set(context.scene.frame_start)
        self.report({"INFO"}, "Fabric is floating. Move it (M), then press Drop")
        return {"FINISHED"}


class BEN_OT_drop(bpy.types.Operator):
    """Let the fabric fall and drape over what is below it"""
    bl_idname = "ben.drop"
    bl_label = "Drop"
    bl_options = {"REGISTER", "UNDO"}

    seconds: FloatProperty(name="Fall time (s)", default=2.5, min=0.2, max=20.0)

    @classmethod
    def poll(cls, context):
        return any(is_fabric(o) for o in context.selected_objects) or is_fabric(context.active_object)

    def execute(self, context):
        scene = context.scene
        cloths = [o for o in context.selected_objects if is_fabric(o)] or [context.active_object]
        for o in surfaces(context, cloths):
            if not any(m.type == "COLLISION" for m in o.modifiers):
                o.modifiers.new("Ben surface", "COLLISION")
                o.collision.thickness_outer = 0.005
                o.collision.cloth_friction = 8.0
        frames = max(2, int(self.seconds * scene.render.fps))
        start = scene.frame_start
        rigid = [o for o in cloths if o.get("ben_rigid")]
        if rigid:
            if scene.rigidbody_world is None:
                with context.temp_override(scene=scene):
                    bpy.ops.rigidbody.world_add()
            rbw = scene.rigidbody_world
            rbw.point_cache.frame_start = start
            rbw.point_cache.frame_end = start + frames
            for o in surfaces(context, cloths):
                if o.rigid_body is None:
                    with context.temp_override(object=o, active_object=o, selected_objects=[o]):
                        bpy.ops.rigidbody.object_add(type="PASSIVE")
                    o.rigid_body.collision_shape = "MESH"
            for o in rigid:
                if o.rigid_body is None:
                    with context.temp_override(object=o, active_object=o, selected_objects=[o]):
                        bpy.ops.rigidbody.object_add(type="ACTIVE")
                o.rigid_body.collision_shape = "BOX"
                o.rigid_body.mass = 0.2
                o.rigid_body.friction = 0.8
            rbw.point_cache.frame_end = start + frames
        for o in [c for c in cloths if not c.get("ben_rigid")]:
            m = o.modifiers["Ben fabric"]
            m.settings.effector_weights.gravity = 1.0
            m.point_cache.frame_start = start
            m.point_cache.frame_end = start + frames
        scene.frame_end = max(scene.frame_end, start + frames)
        for f in range(start, start + frames + 1):
            scene.frame_set(f)
        self.report({"INFO"}, "Done. Press Keep shape to freeze it")
        return {"FINISHED"}


class BEN_OT_keep_shape(bpy.types.Operator):
    """Freeze the fabric as it is now. You can then move it and make it fabric again to re-drop"""
    bl_idname = "ben.keep_shape"
    bl_label = "Keep shape"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return is_fabric(context.active_object)

    def execute(self, context):
        obj = context.active_object
        kind = obj.get("ben_fabric", "COTTON")
        if obj.get("ben_rigid"):
            mw = obj.matrix_world.copy()
            if obj.rigid_body is not None:
                with context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
                    bpy.ops.rigidbody.object_remove()
            obj.matrix_world = mw
            obj["ben_rigid"] = False
            context.scene.frame_set(context.scene.frame_start)
            obj.matrix_world = mw
            return {"FINISHED"}
        depsgraph = context.evaluated_depsgraph_get()
        ev = obj.evaluated_get(depsgraph)
        # keep the draped surface (before thickness), then re-add thickness
        sol = obj.modifiers.get("Ben fabric thickness")
        if sol:
            sol.show_viewport = False
            depsgraph.update()
            ev = obj.evaluated_get(context.evaluated_depsgraph_get())
        new_me = bpy.data.meshes.new_from_object(ev)
        old = obj.data
        obj.modifiers.clear()
        obj.data = new_me
        if old.users == 0:
            bpy.data.meshes.remove(old)
        s = obj.modifiers.new("Ben fabric thickness", "SOLIDIFY")
        s.thickness = FABRICS.get(kind, FABRICS["COTTON"])[5]
        s.offset = 0.0
        if not obj.data.materials:
            obj.data.materials.append(fabric_material(kind))
        context.scene.frame_set(context.scene.frame_start)
        return {"FINISHED"}


class BEN_OT_redrop(bpy.types.Operator):
    """Make a frozen piece of fabric soft again so it can be dropped somewhere else"""
    bl_idname = "ben.soften"
    bl_label = "Make soft again"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        o = context.active_object
        return o is not None and "ben_fabric" in o and not is_fabric(o)

    def execute(self, context):
        obj = context.active_object
        kind = obj["ben_fabric"]
        if kind == "CARDBOARD":
            obj["ben_rigid"] = True
            return {"FINISHED"}
        for m in list(obj.modifiers):
            obj.modifiers.remove(m)
        mass, tension, bending, _rgb, _r, thick = FABRICS[kind]
        m = obj.modifiers.new("Ben fabric", "CLOTH")
        s = m.settings
        s.quality = 8
        s.mass = mass
        s.air_damping = 1.0
        for attr in ("tension_stiffness", "compression_stiffness", "shear_stiffness"):
            setattr(s, attr, tension)
        s.bending_stiffness = bending
        s.effector_weights.gravity = 0.0
        cs = m.collision_settings
        cs.use_self_collision = True
        cs.distance_min = cs.self_distance_min = max(0.002, thick)
        sol = obj.modifiers.new("Ben fabric thickness", "SOLIDIFY")
        sol.thickness = thick
        sol.offset = 0.0
        context.scene.frame_set(context.scene.frame_start)
        return {"FINISHED"}


# ------------------------------------------------------------------ nails (pins)

PIN_GROUP = "Ben nails"


def pin_group(obj):
    vg = obj.vertex_groups.get(PIN_GROUP) or obj.vertex_groups.new(name=PIN_GROUP)
    cloth = obj.modifiers.get("Ben fabric")
    if cloth is not None:
        cloth.settings.vertex_group_mass = PIN_GROUP
        cloth.settings.pin_stiffness = 1.0
    return vg


def nail_near(obj, world_point, radius):
    vg = pin_group(obj)
    inv = obj.matrix_world.inverted()
    p = inv @ world_point
    idx = [v.index for v in obj.data.vertices if (v.co - p).length <= radius]
    if not idx:  # at least the nearest point
        idx = [min(obj.data.vertices, key=lambda v: (v.co - p).length).index]
    vg.add(idx, 1.0, "REPLACE")
    return len(idx)


class BEN_OT_nail(bpy.types.Operator):
    """Nail (pin) the fabric: click the points that must stay fixed. Enter or right-click when done"""
    bl_idname = "ben.nail"
    bl_label = "Nail"
    bl_options = {"REGISTER", "UNDO"}

    radius: FloatProperty(name="Nail size", default=0.02, min=0.001, unit="LENGTH")

    @classmethod
    def poll(cls, context):
        return is_fabric(context.active_object) and not context.active_object.get("ben_rigid")

    def invoke(self, context, event):
        from . import tools
        self.tools = tools
        self.count = 0
        context.window_manager.modal_handler_add(self)
        context.window.cursor_modal_set("CROSSHAIR")
        context.area.header_text_set("Nail: click on the fabric to pin it · Enter / right-click when done")
        return {"RUNNING_MODAL"}

    def modal(self, context, event):
        if event.type in {"MIDDLEMOUSE", "WHEELUPMOUSE", "WHEELDOWNMOUSE"}:
            return {"PASS_THROUGH"}
        if event.type == "LEFTMOUSE" and event.value == "PRESS":
            origin, direction = self.tools.mouse_ray(context, event)
            loc, _n, obj, _i = self.tools.ray_hit(context, origin, direction)
            if obj is not None and is_fabric(obj):
                self.count += 1
                nail_near(obj, loc, self.radius)
                context.area.header_text_set(f"Nail: {self.count} nail(s) · Enter / right-click when done")
            return {"RUNNING_MODAL"}
        if event.type in {"RET", "NUMPAD_ENTER", "RIGHTMOUSE", "ESC"} and event.value == "PRESS":
            context.window.cursor_modal_restore()
            context.area.header_text_set(None)
            return {"FINISHED"}
        return {"RUNNING_MODAL"}


class BEN_OT_nail_quick(bpy.types.Operator):
    """Nail the corners or one edge of the fabric"""
    bl_idname = "ben.nail_quick"
    bl_label = "Quick nails"
    bl_options = {"REGISTER", "UNDO"}

    where: EnumProperty(name="Where", items=[
        ("CORNERS", "4 corners", ""), ("EDGE_BACK", "Back edge", "Like a curtain on a rail"),
        ("EDGES", "All edges", "Like a drum skin or a hammock frame"), ("CLEAR", "Remove all nails", "")],
        default="CORNERS")

    @classmethod
    def poll(cls, context):
        return is_fabric(context.active_object) and not context.active_object.get("ben_rigid")

    def execute(self, context):
        obj = context.active_object
        vg = pin_group(obj)
        verts = obj.data.vertices
        vg.remove([v.index for v in verts])
        if self.where == "CLEAR":
            return {"FINISHED"}
        xs = [v.co.x for v in verts]
        ys = [v.co.y for v in verts]
        x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
        eps = max(x1 - x0, y1 - y0) * 0.01
        pick = []
        for v in verts:
            on_x = abs(v.co.x - x0) < eps or abs(v.co.x - x1) < eps
            on_y = abs(v.co.y - y0) < eps or abs(v.co.y - y1) < eps
            if self.where == "CORNERS" and on_x and on_y:
                pick.append(v.index)
            elif self.where == "EDGE_BACK" and abs(v.co.y - y1) < eps:
                pick.append(v.index)
            elif self.where == "EDGES" and (on_x or on_y):
                pick.append(v.index)
        vg.add(pick, 1.0, "REPLACE")
        self.report({"INFO"}, f"{len(pick)} points nailed")
        return {"FINISHED"}


# ------------------------------------------------------------------ metal web

METALS = {
    "STEEL": ((0.75, 0.75, 0.77), 0.25),
    "BRASS": ((0.85, 0.65, 0.30), 0.2),
    "COPPER": ((0.90, 0.50, 0.35), 0.25),
    "IRON": ((0.06, 0.06, 0.06), 0.45),
    "GOLD": ((1.0, 0.77, 0.34), 0.15),
}


class BEN_OT_metal_web(bpy.types.Operator):
    """Turn a (frozen) fabric into a metal mesh that keeps exactly the same folds"""
    bl_idname = "ben.metal_web"
    bl_label = "Metal web"
    bl_options = {"REGISTER", "UNDO"}

    metal: EnumProperty(name="Metal", items=[(k, k.title() if k != "IRON" else "Black iron", "") for k in METALS],
                        default="STEEL")
    wire: FloatProperty(name="Wire thickness", default=0.003, min=0.0002, max=0.1, unit="LENGTH")
    density: IntProperty(name="Mesh", default=1, min=1, max=4,
                         description="1 = one wire per fabric thread line; higher = finer mesh")

    @classmethod
    def poll(cls, context):
        o = context.active_object
        return o is not None and o.type == "MESH"

    def execute(self, context):
        obj = context.active_object
        if is_fabric(obj) and not obj.get("ben_rigid"):
            bpy.ops.ben.keep_shape()
        for m in list(obj.modifiers):
            if m.type in {"SOLIDIFY", "WIREFRAME", "SUBSURF"} and m.name.startswith("Ben"):
                obj.modifiers.remove(m)
        if self.density > 1:
            sub = obj.modifiers.new("Ben web density", "SUBSURF")
            sub.subdivision_type = "SIMPLE"
            sub.levels = sub.render_levels = self.density - 1
        w = obj.modifiers.new("Ben metal web", "WIREFRAME")
        w.thickness = self.wire
        w.use_even_offset = True
        w.use_relative_offset = False
        w.use_replace = True
        rgb, rough = METALS[self.metal]
        mat = g.material(f"Ben metal {self.metal.lower()}", rgb, rough)
        bsdf = mat.node_tree.nodes.get("Principled BSDF") if mat.node_tree else None
        if bsdf and "Metallic" in bsdf.inputs:
            bsdf.inputs["Metallic"].default_value = 1.0
        obj.data.materials.clear()
        obj.data.materials.append(mat)
        for p in obj.data.polygons:
            p.use_smooth = False
        obj["ben_web"] = self.metal
        return {"FINISHED"}


classes = (BEN_OT_make_fabric, BEN_OT_drop, BEN_OT_keep_shape, BEN_OT_redrop,
           BEN_OT_nail, BEN_OT_nail_quick, BEN_OT_metal_web)
