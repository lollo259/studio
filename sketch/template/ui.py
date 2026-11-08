# Ben - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ben's own interface: top menus, SketchUp-style tool row and the right sidebar.

Blender's top bar, 3D-view header and sidebar panels are replaced while Ben
is active and restored when you switch back to plain Blender.
"""

import bpy

# ------------------------------------------------------------------ menus

class BEN_MT_file(bpy.types.Menu):
    bl_idname = "BEN_MT_file"
    bl_label = "File"

    def draw(self, context):
        l = self.layout
        l.operator("wm.read_homefile", text="New", icon="FILE_NEW").app_template = "ben"
        l.operator("wm.open_mainfile", text="Open…", icon="FILE_FOLDER")
        l.menu("TOPBAR_MT_file_open_recent", text="Open recent")
        l.separator()
        l.operator("wm.save_mainfile", text="Save", icon="FILE_TICK")
        l.operator("wm.save_as_mainfile", text="Save as…")
        l.separator()
        l.menu("BEN_MT_import", text="Import")
        l.menu("BEN_MT_export", text="Export")
        l.separator()
        l.operator("wm.quit_blender", text="Quit", icon="QUIT")


def _op_if(layout, idname, text):
    mod, name = idname.split(".")
    if hasattr(getattr(bpy.ops, mod), name):
        layout.operator(idname, text=text)


class BEN_MT_import(bpy.types.Menu):
    bl_idname = "BEN_MT_import"
    bl_label = "Import"

    def draw(self, context):
        l = self.layout
        _op_if(l, "wm.obj_import", "OBJ (.obj)")
        _op_if(l, "import_scene.fbx", "FBX (.fbx)")
        _op_if(l, "import_scene.gltf", "glTF (.glb/.gltf)")
        _op_if(l, "wm.stl_import", "STL (.stl)")
        _op_if(l, "wm.usd_import", "USD (.usd)")
        l.operator("ben.add_screen", text="Image / video as a screen")


class BEN_MT_export(bpy.types.Menu):
    bl_idname = "BEN_MT_export"
    bl_label = "Export"

    def draw(self, context):
        l = self.layout
        _op_if(l, "wm.obj_export", "OBJ (.obj)")
        _op_if(l, "export_scene.fbx", "FBX (.fbx)")
        _op_if(l, "export_scene.gltf", "glTF (.glb/.gltf)")
        _op_if(l, "wm.stl_export", "STL (.stl, 3D print)")
        _op_if(l, "wm.usd_export", "USD (.usd)")


class BEN_MT_edit(bpy.types.Menu):
    bl_idname = "BEN_MT_edit"
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
        l.operator("ben.make_group", text="Make group")
        l.operator("object.hide_view_set", text="Hide").unselected = False
        l.operator("object.hide_view_clear", text="Unhide all")


class BEN_MT_view(bpy.types.Menu):
    bl_idname = "BEN_MT_view"
    bl_label = "View"

    def draw(self, context):
        l = self.layout
        l.operator("ben.view", text="Iso").view = "ISO"
        l.operator("ben.view", text="Top (plan)").view = "TOP"
        l.operator("ben.view", text="Front").view = "FRONT"
        l.operator("ben.view", text="Right").view = "RIGHT"
        l.separator()
        l.operator("view3d.view_persportho", text="Perspective / Parallel")
        l.operator("view3d.view_all", text="Zoom extents").center = False
        l.operator("view3d.clip_border", text="Section (drag a box)")
        l.operator("view3d.walk", text="Walk")
        l.separator()
        l.operator("view3d.view_camera", text="Camera view")
        l.operator("ben.camera_from_seat", text="New camera from here")


class BEN_MT_render(bpy.types.Menu):
    bl_idname = "BEN_MT_render"
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
    row.label(text="Ben")
    row.separator(factor=2.0)
    row.menu("BEN_MT_file")
    row.menu("BEN_MT_edit")
    row.menu("BEN_MT_view")
    row.menu("BEN_MT_render")


# ------------------------------------------------------------------ tool row (3D view header)

TOOLS = (
    ("ben.select_tool", "RESTRICT_SELECT_OFF", "Select (Space)"),
    ("ben.erase", "TRASH", "Eraser (E)"),
    None,
    ("ben.line", "GREASEPENCIL", "Line (L)"),
    ("ben.draw_arc", "SPHERECURVE", "Arc (A)"),
    ("ben.draw_rectangle", "MESH_PLANE", "Rectangle (R)"),
    ("ben.draw_circle", "MESH_CIRCLE", "Circle (C)"),
    None,
    ("ben.push_pull", "EMPTY_SINGLE_ARROW", "Push/Pull (P)"),
    ("ben.offset", "FULLSCREEN_EXIT", "Offset (F)"),
    ("transform.translate", "ORIENTATION_GLOBAL", "Move (M)"),
    ("transform.rotate", "ORIENTATION_GIMBAL", "Rotate (Q)"),
    ("transform.resize", "FULLSCREEN_ENTER", "Scale (S)"),
    None,
    ("ben.tape", "DRIVER_DISTANCE", "Tape Measure (T)"),
    ("ben.paint", "BRUSH_DATA", "Paint Bucket (B)"),
    ("ben.make_group", "GROUP", "Make Group (G)"),
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
    row.operator("ben.view", text="", icon="VIEW3D").view = "ISO"
    row.operator("ben.view", text="", icon="AXIS_TOP").view = "TOP"
    row.operator("ben.view", text="", icon="AXIS_FRONT").view = "FRONT"
    row.operator("view3d.view_all", text="", icon="ZOOM_ALL").center = False
    layout.separator(factor=1.5)
    shading = context.space_data.shading
    row = layout.row(align=True)
    row.prop_enum(shading, "type", "SOLID", text="", icon="SHADING_SOLID")
    row.prop_enum(shading, "type", "MATERIAL", text="", icon="SHADING_TEXTURE")
    row.prop_enum(shading, "type", "RENDERED", text="", icon="SHADING_RENDERED")


class BEN_OT_view(bpy.types.Operator):
    """Standard SketchUp views"""
    bl_idname = "ben.view"
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

class BenPanel:
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Ben"


class BEN_PT_entity(BenPanel, bpy.types.Panel):
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


class BEN_PT_layers(BenPanel, bpy.types.Panel):
    bl_label = "Layers"

    def draw(self, context):
        from . import layers
        layout = self.layout
        r = layers.root(context)
        if not r.children:
            layers.active_layer(context)
        row = layout.row()
        row.template_list("BEN_UL_layers", "", r, "children", context.scene, "ben_layer_index", rows=4)
        col = row.column(align=True)
        col.operator("ben.layer_add", text="", icon="ADD")
        col.operator("ben.layer_remove", text="", icon="REMOVE")
        layout.operator("ben.move_to_layer", icon="FORWARD")


class BEN_PT_materials(BenPanel, bpy.types.Panel):
    bl_label = "Materials"

    def draw(self, context):
        layout = self.layout
        layout.prop(context.scene, "ben_paint_color", text="Paint colour")
        layout.label(text="B: click a face · Shift+B click: whole object")


class BEN_PT_library(BenPanel, bpy.types.Panel):
    bl_label = "Library"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        for title, ops in (
            ("Rooms", ("ben.add_room", "ben.add_hall", "ben.add_chapel", "ben.walls_from_plan")),
            ("Stage & audience", ("ben.add_concert_hall", "ben.add_stage", "ben.add_seating", "ben.add_ensemble")),
            ("Instruments & people", ("ben.add_piano", "ben.add_figure")),
        ):
            layout.label(text=title)
            col = layout.column(align=True)
            for op in ops:
                col.operator(op)


class BEN_PT_lighting(BenPanel, bpy.types.Panel):
    bl_label = "Lights, cameras & screens"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        col = layout.column(align=True)
        for op in ("ben.add_spot", "ben.add_wash", "ben.add_truss", "ben.add_screen"):
            col.operator(op)
        layout.label(text="Film & live screens")
        col = layout.column(align=True)
        col.operator("ben.add_film_camera", icon="CAMERA_DATA")
        col.operator("ben.live_screen", text="Selected screen shows camera…", icon="IMAGE_PLANE")
        col.operator("ben.update_live_screens", icon="FILE_REFRESH")
        row = col.row(align=True)
        row.operator("ben.render_with_screens", text="Render image", icon="RENDER_STILL").video = False
        row.operator("ben.render_with_screens", text="Render video", icon="RENDER_ANIMATION").video = True
        row = layout.row(align=True)
        row.operator("ben.lighting_mood", text="Concert").mood = "CONCERT"
        row.operator("ben.lighting_mood", text="House").mood = "HOUSE"
        row.operator("ben.lighting_mood", text="Dark").mood = "BLACKOUT"


class BEN_PT_physics(BenPanel, bpy.types.Panel):
    bl_label = "Liquids, smoke & fire"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        layout.label(text="Select a container or object, then:")
        col = layout.column(align=True)
        col.operator("ben.liquid_still", icon="MOD_FLUIDSIM")
        col.operator("ben.liquid_pour", icon="MOD_FLUID")
        col.operator("ben.smoke_fire", icon="FORCE_FORCE")
        row = layout.row(align=True)
        row.scale_y = 1.3
        row.operator("ben.simulate", icon="PLAY")
        row.operator("ben.clear_simulation", text="", icon="TRASH")
        scene = context.scene
        row = layout.row(align=True)
        row.prop(scene, "frame_current", text="Moment")
        row.operator("screen.animation_play", text="", icon="PLAY")


class BEN_PT_fabric(BenPanel, bpy.types.Panel):
    bl_label = "Fabric, plastic & cardboard"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        layout.label(text="Draw a shape (R, C, L), then:")
        col = layout.column(align=True)
        col.scale_y = 1.2
        col.operator("ben.make_fabric", icon="MOD_CLOTH")
        col.operator("ben.nail", icon="PINNED")
        row = col.row(align=True)
        row.operator("ben.nail_quick", text="Corners").where = "CORNERS"
        row.operator("ben.nail_quick", text="Back edge").where = "EDGE_BACK"
        row.operator("ben.nail_quick", text="All edges").where = "EDGES"
        col.operator("ben.drop", icon="SORT_ASC")
        col.operator("ben.keep_shape", icon="FREEZE")
        col.operator("ben.soften", icon="MOD_SOFT")
        col.separator()
        col.operator("ben.metal_web", icon="MOD_WIREFRAME")
        layout.label(text="Floating fabric: move it with M, then Drop")


class BEN_MT_add(bpy.types.Menu):
    bl_idname = "BEN_MT_add"
    bl_label = "Ben"

    def draw(self, context):
        layout = self.layout
        for op in ("ben.add_room", "ben.add_hall", "ben.add_chapel", "ben.add_concert_hall", "ben.add_stage",
                   "ben.add_seating", "ben.add_piano", "ben.add_ensemble", "ben.add_figure",
                   "ben.add_spot", "ben.add_wash", "ben.add_truss", "ben.add_screen"):
            layout.operator(op)


def add_menu(self, context):
    self.layout.menu(BEN_MT_add.bl_idname)
    self.layout.separator()


# ------------------------------------------------------------------ hide / restore Blender

_original = {}
_hidden_panels = []


def install():
    _original["topbar"] = bpy.types.TOPBAR_HT_upper_bar.draw
    _original["header"] = bpy.types.VIEW3D_HT_header.draw
    bpy.types.TOPBAR_HT_upper_bar.draw = topbar_draw
    bpy.types.VIEW3D_HT_header.draw = header_draw
    # Blender's own sidebar tabs (Item, Tool, View, ...) are hidden while Ben is active
    panels = [cls for cls in bpy.types.Panel.__subclasses__()
              if getattr(cls, "bl_space_type", "") == "VIEW_3D" and getattr(cls, "bl_region_type", "") == "UI"
              and getattr(cls, "bl_category", "") != "Ben" and getattr(cls, "is_registered", False)]
    # children before parents
    panels.sort(key=lambda c: 0 if getattr(c, "bl_parent_id", "") else 1)
    for cls in panels:
        try:
            bpy.utils.unregister_class(cls)
            _hidden_panels.append(cls)
        except RuntimeError:
            pass


def uninstall():
    if "topbar" in _original:
        bpy.types.TOPBAR_HT_upper_bar.draw = _original.pop("topbar")
    if "header" in _original:
        bpy.types.VIEW3D_HT_header.draw = _original.pop("header")
    for cls in reversed(_hidden_panels):  # parents first
        try:
            bpy.utils.register_class(cls)
        except Exception:
            pass
    _hidden_panels.clear()


classes = (BEN_MT_file, BEN_MT_import, BEN_MT_export, BEN_MT_edit, BEN_MT_view, BEN_MT_render,
           BEN_OT_view, BEN_PT_entity, BEN_PT_layers, BEN_PT_materials, BEN_PT_library,
           BEN_PT_lighting, BEN_PT_physics, BEN_PT_fabric, BEN_MT_add)
