# Ben - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Layers (SketchUp tags): named groups you can show, hide and lock.

Every layer is a collection inside the 'Ben' collection. New drawings go
to the active layer.
"""

import bpy
from bpy.props import IntProperty, StringProperty

from . import geometry as g


def root(context):
    return g.ben_collection(context)


def layers(context):
    return list(root(context).children)


def layer_collection(context, coll):
    def walk(lc):
        if lc.collection == coll:
            return lc
        for child in lc.children:
            found = walk(child)
            if found:
                return found
        return None
    return walk(context.view_layer.layer_collection)


def active_layer(context):
    """The layer new geometry goes to (created on first use)."""
    r = root(context)
    idx = context.scene.ben_layer_index
    kids = list(r.children)
    if 0 <= idx < len(kids):
        return kids[idx]
    if kids:
        return kids[0]
    coll = bpy.data.collections.new("Layer 1")
    r.children.link(coll)
    return coll


class BEN_UL_layers(bpy.types.UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        row = layout.row(align=True)
        row.prop(item, "name", text="", emboss=False)
        lc = layer_collection(context, item)
        if lc is not None:
            row.prop(lc, "hide_viewport", text="", emboss=False,
                     icon="HIDE_ON" if lc.hide_viewport else "HIDE_OFF")
        row.prop(item, "hide_select", text="", emboss=False,
                 icon="LOCKED" if item.hide_select else "UNLOCKED")


class BEN_OT_layer_add(bpy.types.Operator):
    """Add a new layer and make it active"""
    bl_idname = "ben.layer_add"
    bl_label = "Add layer"
    bl_options = {"REGISTER", "UNDO"}

    name: StringProperty(name="Name", default="Layer")

    def execute(self, context):
        r = root(context)
        coll = bpy.data.collections.new(self.name)
        r.children.link(coll)
        context.scene.ben_layer_index = len(r.children) - 1
        return {"FINISHED"}


class BEN_OT_layer_remove(bpy.types.Operator):
    """Delete the active layer. Its objects move to the first layer"""
    bl_idname = "ben.layer_remove"
    bl_label = "Delete layer"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        r = root(context)
        kids = list(r.children)
        idx = context.scene.ben_layer_index
        if not (0 <= idx < len(kids)) or len(kids) < 2:
            return {"CANCELLED"}
        coll = kids[idx]
        keep = kids[0] if idx != 0 else kids[1]
        for obj in list(coll.all_objects):
            if keep not in obj.users_collection:
                keep.objects.link(obj)
        bpy.data.collections.remove(coll)
        context.scene.ben_layer_index = max(0, idx - 1)
        return {"FINISHED"}


class BEN_OT_move_to_layer(bpy.types.Operator):
    """Move the selected objects to the active layer"""
    bl_idname = "ben.move_to_layer"
    bl_label = "Move selection here"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return bool(context.selected_objects)

    def execute(self, context):
        target = active_layer(context)
        for obj in context.selected_objects:
            for c in list(obj.users_collection):
                c.objects.unlink(obj)
            target.objects.link(obj)
        return {"FINISHED"}


def register_props():
    bpy.types.Scene.ben_layer_index = IntProperty(name="Active layer", default=0, min=0)


def unregister_props():
    del bpy.types.Scene.ben_layer_index


classes = (BEN_UL_layers, BEN_OT_layer_add, BEN_OT_layer_remove, BEN_OT_move_to_layer)
