# Ben - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""SketchUp-style modelling helpers: rectangles, floor plans, walls, rooms."""

import bmesh
import bpy
from bpy.props import FloatProperty
from mathutils import Vector

from . import geometry as g


def _layer(context):
    from . import layers
    return layers.active_layer(context)


def cursor(context):
    return Vector(context.scene.cursor.location)


def _to_edit(context, obj, select_mode):
    for o in context.selected_objects:
        o.select_set(False)
    obj.select_set(True)
    context.view_layer.objects.active = obj
    if context.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.mode_set(mode="EDIT")
    context.tool_settings.mesh_select_mode = select_mode


class BEN_OT_rectangle(bpy.types.Operator):
    """Draw a flat rectangle on the ground at the 3D cursor, ready for Push/Pull"""
    bl_idname = "ben.rectangle"
    bl_label = "Rectangle"
    bl_options = {"REGISTER", "UNDO"}

    width: FloatProperty(name="Width (X)", default=4.0, min=0.01, unit="LENGTH")
    depth: FloatProperty(name="Depth (Y)", default=3.0, min=0.01, unit="LENGTH")

    def execute(self, context):
        col = _layer(context)
        bm = bmesh.new()
        w, d = self.width / 2, self.depth / 2
        verts = [bm.verts.new(v) for v in ((-w, -d, 0), (w, -d, 0), (w, d, 0), (-w, d, 0))]
        bm.faces.new(verts)
        obj = g.mesh_object("Shape", bm, col, "wall", location=cursor(context))
        _to_edit(context, obj, (False, False, True))
        bpy.ops.mesh.select_all(action="SELECT")
        return {"FINISHED"}


class BEN_OT_start_plan(bpy.types.Operator):
    """Start a floor plan: one point at the 3D cursor. Ctrl+right-click to draw each next corner"""
    bl_idname = "ben.start_plan"
    bl_label = "Draw floor plan"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        col = _layer(context)
        bm = bmesh.new()
        bm.verts.new((0, 0, 0))
        obj = g.mesh_object("Plan", bm, col, location=cursor(context))
        _to_edit(context, obj, (True, False, False))
        bpy.ops.mesh.select_all(action="SELECT")
        context.tool_settings.use_snap = True
        self.report({"INFO"}, "Ctrl+right-click to add corners. Then: Walls from plan")
        return {"FINISHED"}


class BEN_OT_walls_from_plan(bpy.types.Operator):
    """Raise walls from the edges of the active object (a drawn plan, a rectangle, any outline)"""
    bl_idname = "ben.walls_from_plan"
    bl_label = "Walls from plan"
    bl_options = {"REGISTER", "UNDO"}

    height: FloatProperty(name="Height", default=3.0, min=0.05, unit="LENGTH")
    thickness: FloatProperty(name="Thickness", default=0.3, min=0.01, unit="LENGTH")

    @classmethod
    def poll(cls, context):
        o = context.active_object
        return o is not None and o.type == "MESH"

    def execute(self, context):
        src = context.active_object
        if context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        me = src.data
        edges = [(me.vertices[e.vertices[0]].co.copy(), me.vertices[e.vertices[1]].co.copy()) for e in me.edges]
        if not edges:
            self.report({"WARNING"}, "The plan has no edges yet. Draw at least two corners")
            return {"CANCELLED"}
        bm = bmesh.new()
        lookup = {}

        def vert(co):
            key = (round(co.x, 5), round(co.y, 5), round(co.z, 5))
            if key not in lookup:
                lookup[key] = bm.verts.new(co)
            return lookup[key]

        up = Vector((0, 0, self.height))
        for a, b in edges:
            bm.faces.new((vert(a), vert(b), vert(b + up), vert(a + up)))
        bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-5)
        col = _layer(context)
        walls = g.mesh_object("Walls", bm, col, "wall")
        walls.matrix_world = src.matrix_world.copy()
        mod = walls.modifiers.new("Wall thickness", "SOLIDIFY")
        mod.thickness = self.thickness
        mod.offset = 0.0
        mod.use_even_offset = True
        src.hide_set(True)
        for o in context.selected_objects:
            o.select_set(False)
        walls.select_set(True)
        context.view_layer.objects.active = walls
        return {"FINISHED"}


class BEN_OT_room(bpy.types.Operator):
    """Add a closed room or cube installation, measured from the inside"""
    bl_idname = "ben.add_room"
    bl_label = "Room / cube"
    bl_options = {"REGISTER", "UNDO"}

    width: FloatProperty(name="Inside width", default=3.0, min=0.1, unit="LENGTH")
    depth: FloatProperty(name="Inside depth", default=3.0, min=0.1, unit="LENGTH")
    height: FloatProperty(name="Inside height", default=3.0, min=0.1, unit="LENGTH")
    thickness: FloatProperty(name="Wall thickness", default=0.1, min=0.005, unit="LENGTH")
    door: FloatProperty(name="Door width", default=0.9, min=0.0, unit="LENGTH",
                        description="0 = closed. The door opens on the front (-Y) face")

    def execute(self, context):
        col = _layer(context)
        W, D, H, T = self.width, self.depth, self.height, self.thickness
        bm = bmesh.new()
        g.add_box(bm, (W + 2 * T, D + 2 * T, T), (0, 0, -T / 2))           # floor
        g.add_box(bm, (W + 2 * T, D + 2 * T, T), (0, 0, H + T / 2))        # ceiling
        g.add_box(bm, (T, D, H), (-(W + T) / 2, 0, H / 2))
        g.add_box(bm, (T, D, H), ((W + T) / 2, 0, H / 2))
        g.add_box(bm, (W + 2 * T, T, H), (0, (D + T) / 2, H / 2))           # back
        door = min(self.door, W)
        if door > 0:
            side = (W + 2 * T - door) / 2
            dh = min(2.1, H)
            g.add_box(bm, (side, T, H), (-(door + side) / 2, -(D + T) / 2, H / 2))
            g.add_box(bm, (side, T, H), ((door + side) / 2, -(D + T) / 2, H / 2))
            if H > dh:
                g.add_box(bm, (door, T, H - dh), (0, -(D + T) / 2, dh + (H - dh) / 2))
        else:
            g.add_box(bm, (W + 2 * T, T, H), (0, -(D + T) / 2, H / 2))
        g.mesh_object("Room", bm, col, "wall", location=cursor(context))
        return {"FINISHED"}


class BEN_OT_units(bpy.types.Operator):
    """Use metres and centimetres, real-world scale"""
    bl_idname = "ben.metric"
    bl_label = "Metric units"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        apply_units(context.scene)
        return {"FINISHED"}


def apply_units(scene):
    us = scene.unit_settings
    us.system = "METRIC"
    us.scale_length = 1.0
    us.length_unit = "METERS"


classes = (BEN_OT_rectangle, BEN_OT_start_plan, BEN_OT_walls_from_plan, BEN_OT_room, BEN_OT_units)
