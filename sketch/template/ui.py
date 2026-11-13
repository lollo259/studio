# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Sketch's additions to Blender's interface: the SketchUp-style tool row, the Sketch
sidebar tab and the Sketch Add menu. Blender's own interface stays as it is."""

import bpy

# ------------------------------------------------------------------ menus

class SKETCH_MT_file(bpy.types.Menu):
    bl_idname = "SKETCH_MT_file"
    bl_label = "File"

    def draw(self, context):
        l = self.layout
        l.operator("wm.read_homefile", text="New", icon="FILE_NEW").app_template = "sketch"
        l.operator("wm.open_mainfile", text="Open…", icon="FILE_FOLDER")
        l.menu("TOPBAR_MT_file_open_recent", text="Open recent")
        l.separator()
        l.operator("wm.save_mainfile", text="Save", icon="FILE_TICK")
        l.operator("wm.save_as_mainfile", text="Save as…")
        l.separator()
        l.menu("SKETCH_MT_import", text="Import")
        l.menu("SKETCH_MT_export", text="Export")
        l.separator()
        l.operator("wm.quit_blender", text="Quit", icon="QUIT")


def _op_if(layout, idname, text):
    mod, name = idname.split(".")
    if hasattr(getattr(bpy.ops, mod), name):
        layout.operator(idname, text=text)


class SKETCH_MT_import(bpy.types.Menu):
    bl_idname = "SKETCH_MT_import"
    bl_label = "Import"

    def draw(self, context):
        l = self.layout
        _op_if(l, "wm.obj_import", "OBJ (.obj)")
        _op_if(l, "import_scene.fbx", "FBX (.fbx)")
        _op_if(l, "import_scene.gltf", "glTF (.glb/.gltf)")
        _op_if(l, "wm.stl_import", "STL (.stl)")
        _op_if(l, "wm.usd_import", "USD (.usd)")
        l.operator("sketch.add_screen", text="Image / video as a screen")


class SKETCH_MT_export(bpy.types.Menu):
    bl_idname = "SKETCH_MT_export"
    bl_label = "Export"

    def draw(self, context):
        l = self.layout
        _op_if(l, "wm.obj_export", "OBJ (.obj)")
        _op_if(l, "export_scene.fbx", "FBX (.fbx)")
        _op_if(l, "export_scene.gltf", "glTF (.glb/.gltf)")
        _op_if(l, "wm.stl_export", "STL (.stl, 3D print)")
        _op_if(l, "wm.usd_export", "USD (.usd)")


class SKETCH_MT_edit(bpy.types.Menu):
    bl_idname = "SKETCH_MT_edit"
    bl_label = "Edit"

    def draw(self, context):
        l = self.layout
        l.operator("ed.undo", text="Undo", icon="LOOP_BACK")
        l.operator("ed.redo", text="Redo", icon="LOOP_FORWARDS")
        l.separator()
        l.operator("object.duplicate_move", text="Copy in place + move")
        l.operator("object.delete", text="Delete").use_global = False
        l.operator("object.select_all", text="Select all").action = "SELECT"
        l.operator("object.select_all", text="Select none").action = "DESELECT"
        l.separator()
        l.operator("sketch.make_group", text="Make group")
        l.operator("object.hide_view_set", text="Hide").unselected = False
        l.operator("object.hide_view_clear", text="Unhide all")


class SKETCH_MT_view(bpy.types.Menu):
    bl_idname = "SKETCH_MT_view"
    bl_label = "View"

    def draw(self, context):
        l = self.layout
        l.operator("sketch.view", text="Iso").view = "ISO"
        l.operator("sketch.view", text="Top (plan)").view = "TOP"
        l.operator("sketch.view", text="Front").view = "FRONT"
        l.operator("sketch.view", text="Right").view = "RIGHT"
        l.separator()
        l.operator("view3d.view_persportho", text="Perspective / Parallel")
        l.operator("view3d.view_all", text="Zoom extents").center = False
        l.operator("view3d.clip_border", text="Section (drag a box)")
        l.operator("view3d.walk", text="Walk")
        l.separator()
        l.operator("view3d.view_camera", text="Camera view")
        l.operator("sketch.camera_from_seat", text="New camera from here")


class SKETCH_MT_render(bpy.types.Menu):
    bl_idname = "SKETCH_MT_render"
    bl_label = "Render"

    def draw(self, context):
        l = self.layout
        l.operator("render.render", text="Render image", icon="RENDER_STILL")
        l.operator("render.view_show", text="Show last render")
        l.separator()
        l.prop(context.scene.render, "resolution_x", text="Width")
        l.prop(context.scene.render, "resolution_y", text="Height")


# ------------------------------------------------------------------ top bar

def topbar_draw(self, context):
    layout = self.layout
    if context.region.alignment == "RIGHT":
        return
    row = layout.row(align=True)
    row.label(text="Sketch")
    row.separator(factor=2.0)
    row.menu("SKETCH_MT_file")
    row.menu("SKETCH_MT_edit")
    row.menu("SKETCH_MT_view")
    row.menu("SKETCH_MT_render")


# ------------------------------------------------------------------ tool row (3D view header)

TOOLS = (
    ("sketch.select_tool", "RESTRICT_SELECT_OFF", "Select (Space)"),
    ("sketch.erase", "TRASH", "Eraser (E)"),
    None,
    ("sketch.line", "GREASEPENCIL", "Line (L)"),
    ("sketch.draw_arc", "SPHERECURVE", "Arc (A)"),
    ("sketch.draw_rectangle", "MESH_PLANE", "Rectangle (R)"),
    ("sketch.draw_circle", "MESH_CIRCLE", "Circle (C)"),
    None,
    ("sketch.push_pull", "EMPTY_SINGLE_ARROW", "Push/Pull (P)"),
    ("sketch.offset", "FULLSCREEN_EXIT", "Offset (F)"),
    ("sketch.move", "ORIENTATION_GLOBAL", "Move (M)"),
    ("sketch.rotate", "ORIENTATION_GIMBAL", "Rotate (Q)"),
    ("sketch.scale", "FULLSCREEN_ENTER", "Scale (S)"),
    None,
    ("sketch.tape", "DRIVER_DISTANCE", "Tape Measure (T)"),
    ("sketch.paint", "BRUSH_DATA", "Paint Bucket (B)"),
    ("sketch.make_group", "GROUP", "Make Group (G)"),
)


def header_draw(self, context):
    layout = self.layout
    row = layout.row(align=True)
    row.scale_x = 1.15
    for item in TOOLS:
        if item is None:
            row.separator(factor=1.5)
            continue
        op, icon, _tip = item
        row.operator(op, text="", icon=icon)
    layout.separator_spacer()
    row = layout.row(align=True)
    row.operator("sketch.view", text="", icon="VIEW3D").view = "ISO"
    row.operator("sketch.view", text="", icon="AXIS_TOP").view = "TOP"
    row.operator("sketch.view", text="", icon="AXIS_FRONT").view = "FRONT"
    row.operator("view3d.view_all", text="", icon="ZOOM_ALL").center = False
    layout.separator(factor=1.5)
    shading = context.space_data.shading
    row = layout.row(align=True)
    row.prop_enum(shading, "type", "SOLID", text="", icon="SHADING_SOLID")
    row.prop_enum(shading, "type", "MATERIAL", text="", icon="SHADING_TEXTURE")
    row.prop_enum(shading, "type", "RENDERED", text="", icon="SHADING_RENDERED")


class SKETCH_OT_view(bpy.types.Operator):
    """Standard SketchUp views"""
    bl_idname = "sketch.view"
    bl_label = "View"

    view: bpy.props.EnumProperty(items=[("ISO", "Iso", ""), ("TOP", "Top", ""),
                                        ("FRONT", "Front", ""), ("RIGHT", "Right", "")])

    def execute(self, context):
        import math
        from mathutils import Euler
        r3d = context.space_data.region_3d if context.space_data and context.space_data.type == "VIEW_3D" else None
        if r3d is None:
            for area in context.screen.areas:
                if area.type == "VIEW_3D":
                    r3d = area.spaces.active.region_3d
        if r3d is None:
            return {"CANCELLED"}
        if self.view == "ISO":
            r3d.view_perspective = "PERSP"
            r3d.view_rotation = Euler((math.radians(60), 0, math.radians(45))).to_quaternion()
        else:
            bpy.ops.view3d.view_axis(type=self.view)
        return {"FINISHED"}


# ------------------------------------------------------------------ sidebar

class SketchPanel:
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Sketch"


class SKETCH_PT_entity(SketchPanel, bpy.types.Panel):
    bl_label = "Entity info"

    def draw(self, context):
        layout = self.layout
        obj = context.active_object
        if obj is None or not obj.select_get():
            layout.label(text="Nothing selected")
            return
        layout.prop(obj, "name", text="")
        col = layout.column(align=True)
        col.prop(obj, "dimensions", text="Size")
        col = layout.column(align=True)
        col.prop(obj, "location", text="Position")
        layout.prop(obj, "rotation_euler", index=2, text="Turn")


class SKETCH_PT_layers(SketchPanel, bpy.types.Panel):
    bl_label = "Layers"

    def draw(self, context):
        from . import layers
        layout = self.layout
        r = layers.root(context)
        if not r.children:
            layers.active_layer(context)
        row = layout.row()
        row.template_list("SKETCH_UL_layers", "", r, "children", context.scene, "sketch_layer_index", rows=4)
        col = row.column(align=True)
        col.operator("sketch.layer_add", text="", icon="ADD")
        col.operator("sketch.layer_remove", text="", icon="REMOVE")
        layout.operator("sketch.move_to_layer", icon="FORWARD")


class SKETCH_PT_materials(SketchPanel, bpy.types.Panel):
    bl_label = "Materials"

    def draw(self, context):
        layout = self.layout
        layout.prop(context.scene, "sketch_paint_color", text="Paint colour")
        layout.label(text="B: click a face · Shift+B click: whole object")


class SKETCH_PT_library(SketchPanel, bpy.types.Panel):
    bl_label = "Library"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        for title, ops in (
            ("Rooms", ("sketch.add_room", "sketch.add_hall", "sketch.add_chapel", "sketch.walls_from_plan")),
            ("Stage & audience", ("sketch.add_concert_hall", "sketch.add_stage", "sketch.add_riser",
                                  "sketch.add_seating", "sketch.add_ensemble")),
            ("Instruments & people", ("sketch.add_piano", "sketch.add_chair", "sketch.add_music_stand",
                                      "sketch.add_figure")),
            ("Sound", ("sketch.add_speaker", "sketch.add_mic_stand")),
            ("Outside", ("sketch.sun_sky",)),
            ("Video", ("sketch.video",)),
        ):
            layout.label(text=title)
            col = layout.column(align=True)
            for op in ops:
                col.operator(op)


class SKETCH_PT_lighting(SketchPanel, bpy.types.Panel):
    bl_label = "Lights, cameras & screens"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        col = layout.column(align=True)
        for op in ("sketch.add_spot", "sketch.add_wash", "sketch.add_truss", "sketch.add_screen"):
            col.operator(op)
        layout.label(text="Film & live screens")
        col = layout.column(align=True)
        col.operator("sketch.add_film_camera", icon="CAMERA_DATA")
        col.operator("sketch.live_screen", text="Selected screen shows camera…", icon="IMAGE_PLANE")
        col.operator("sketch.update_live_screens", icon="FILE_REFRESH")
        row = col.row(align=True)
        row.operator("sketch.render_with_screens", text="Render image", icon="RENDER_STILL").video = False
        row.operator("sketch.render_with_screens", text="Render video", icon="RENDER_ANIMATION").video = True
        row = layout.row(align=True)
        row.operator("sketch.lighting_mood", text="Concert").mood = "CONCERT"
        row.operator("sketch.lighting_mood", text="House").mood = "HOUSE"
        row.operator("sketch.lighting_mood", text="Dark").mood = "BLACKOUT"


class SKETCH_PT_acoustics(SketchPanel, bpy.types.Panel):
    bl_label = "Acoustics: hear the room"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        from . import acoustics
        acoustics.draw_panel(self.layout, context)


class SKETCH_PT_dmx(SketchPanel, bpy.types.Panel):
    bl_label = "Lighting control (DMX)"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        from . import lights
        lights.draw_panel(self.layout, context)


class SKETCH_PT_rider(SketchPanel, bpy.types.Panel):
    bl_label = "Technical rider"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        from . import rider
        rider.draw_panel(self.layout, context)


class SKETCH_PT_physics(SketchPanel, bpy.types.Panel):
    bl_label = "Liquids, smoke & fire"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        layout.label(text="Select a container or object, then:")
        col = layout.column(align=True)
        col.operator("sketch.liquid_still", icon="MOD_FLUIDSIM")
        col.operator("sketch.liquid_pour", icon="MOD_FLUID")
        col.operator("sketch.smoke_fire", icon="FORCE_FORCE")
        row = layout.row(align=True)
        row.scale_y = 1.3
        row.operator("sketch.simulate", icon="PLAY")
        row.operator("sketch.clear_simulation", text="", icon="TRASH")
        scene = context.scene
        row = layout.row(align=True)
        row.prop(scene, "frame_current", text="Moment")
        row.operator("screen.animation_play", text="", icon="PLAY")


class SKETCH_PT_fabric(SketchPanel, bpy.types.Panel):
    bl_label = "Fabric, plastic & cardboard"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        layout.label(text="Draw a shape (R, C, L), then:")
        col = layout.column(align=True)
        col.scale_y = 1.2
        col.operator("sketch.make_fabric", icon="MOD_CLOTH")
        col.operator("sketch.nail", icon="PINNED")
        row = col.row(align=True)
        row.operator("sketch.nail_quick", text="Corners").where = "CORNERS"
        row.operator("sketch.nail_quick", text="Back edge").where = "EDGE_BACK"
        row.operator("sketch.nail_quick", text="All edges").where = "EDGES"
        col.operator("sketch.drop", icon="SORT_ASC")
        col.operator("sketch.keep_shape", icon="FREEZE")
        col.operator("sketch.soften", icon="MOD_SOFT")
        col.separator()
        col.operator("sketch.metal_web", icon="MOD_WIREFRAME")
        layout.label(text="Floating fabric: move it with M, then Drop")


class SKETCH_MT_add(bpy.types.Menu):
    bl_idname = "SKETCH_MT_add"
    bl_label = "Sketch"

    def draw(self, context):
        layout = self.layout
        for op in ("sketch.add_room", "sketch.add_hall", "sketch.add_chapel", "sketch.add_concert_hall", "sketch.add_stage",
                   "sketch.add_seating", "sketch.add_piano", "sketch.add_ensemble", "sketch.add_chair",
                   "sketch.add_music_stand", "sketch.add_riser", "sketch.add_speaker", "sketch.add_mic_stand",
                   "sketch.add_figure",
                   "sketch.add_spot", "sketch.add_wash", "sketch.add_truss", "sketch.add_screen"):
            layout.operator(op)


def add_menu(self, context):
    self.layout.menu(SKETCH_MT_add.bl_idname)
    self.layout.separator()


# ------------------------------------------------------------------ hide / restore Blender

_original = {}
_hidden_panels = []


def tool_row(self, context):
    """Sketch's SketchUp-style tools, in front of Blender's own 3D-view header."""
    row = self.layout.row(align=True)
    for item in TOOLS:
        if item is None:
            row.separator(factor=1.0)
            continue
        op, icon, _tip = item
        row.operator(op, text="", icon=icon)
    self.layout.separator(factor=2.0)


def install():
    # Studio decision: Sketch keeps Blender's real interface (its top bar, header and sidebar
    # tabs stay); Sketch adds its tool row, its sidebar tab and its Add menu on top of it
    bpy.types.VIEW3D_HT_header.prepend(tool_row)


def uninstall():
    try:
        bpy.types.VIEW3D_HT_header.remove(tool_row)
    except Exception:
        pass


classes = (SKETCH_MT_file, SKETCH_MT_import, SKETCH_MT_export, SKETCH_MT_edit, SKETCH_MT_view, SKETCH_MT_render,
           SKETCH_OT_view, SKETCH_PT_entity, SKETCH_PT_layers, SKETCH_PT_materials, SKETCH_PT_library,
           SKETCH_PT_lighting, SKETCH_PT_acoustics, SKETCH_PT_dmx, SKETCH_PT_rider, SKETCH_PT_physics, SKETCH_PT_fabric, SKETCH_MT_add)
