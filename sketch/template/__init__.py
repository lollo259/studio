# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Sketch: venues, stages, scenography and architecture, built on Blender.

A Blender application template by Laurence Who. Start it with
`blender --app-template sketch` or from File > New > Sketch.
"""

import os
import bpy
from bpy.app.handlers import persistent

from . import (acoustics, fabric, geometry, layers, lights, modelling, objects, physics, rider, scenography,
               tools, ui, venue)

HERE = os.path.dirname(__file__)

# ------------------------------------------------------------------ look

# Studio look E: light aluminium, flat keys, orange accent (same palette as Score and Tape)
def _hex(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


BLACK = _hex("#26262A")          # headers' text and lines on aluminium
NIGHT = _hex("#C3C4C8")          # editor backgrounds (one step darker than panels)
PANEL = _hex("#CDCED1")          # panels: the aluminium body
WIDGET = _hex("#E3E4E6")         # keys
LINE = _hex("#A9AAAD")
TEXT = _hex("#26262A")
MUTED = _hex("#6C6C70")
BLUE = _hex("#FF6A13")           # accent (orange; the name stays for the code below)
BLUE_HI = _hex("#FF8A40")

_saved = []  # (owner, attribute, old value) so the template can be switched off cleanly


def _set(owner, attr, value):
    if not hasattr(owner, attr):
        return
    old = getattr(owner, attr)
    try:
        old = tuple(old)
    except TypeError:
        pass
    try:
        cur = getattr(owner, attr)
        if hasattr(cur, "__len__") and len(cur) == 4 and len(value) == 3:
            value = (*value, cur[3])
        setattr(owner, attr, value)
        _saved.append((owner, attr, old))
    except Exception as exc:  # theme layout differs between Blender versions
        print(f"Sketch: could not set {attr}: {exc}")


def apply_look():
    prefs = bpy.context.preferences
    theme = prefs.themes[0]
    ui_t = theme.user_interface

    for name in ("wcol_regular", "wcol_tool", "wcol_toolbar_item", "wcol_radio", "wcol_text", "wcol_option",
                 "wcol_toggle", "wcol_num", "wcol_numslider", "wcol_box", "wcol_menu", "wcol_pulldown",
                 "wcol_menu_back", "wcol_pie_menu", "wcol_menu_item", "wcol_list_item", "wcol_tab",
                 "wcol_tooltip", "wcol_scroll"):
        w = getattr(ui_t, name, None)
        if w is None:
            continue
        _set(w, "inner", WIDGET if name not in ("wcol_menu_back", "wcol_tooltip") else PANEL)
        _set(w, "inner_sel", BLUE)
        _set(w, "outline", LINE)
        _set(w, "text", TEXT)
        _set(w, "text_sel", (1.0, 1.0, 1.0))
        _set(w, "roundness", 0.3)
        _set(w, "item", BLUE_HI if name in ("wcol_numslider", "wcol_scroll", "wcol_option") else MUTED)
        _set(w, "show_shaded", False)

    for attr, val in (("panel_back", PANEL), ("panel_sub_back", NIGHT), ("panel_header", PANEL),
                      ("panel_title", TEXT), ("panel_text", TEXT), ("panel_outline", LINE),
                      ("panel_active", BLUE), ("editor_border", BLACK), ("editor_outline", LINE),
                      ("editor_outline_active", BLUE), ("widget_text_cursor", BLUE_HI)):
        _set(ui_t, attr, val)

    for section in ("properties", "outliner", "topbar", "statusbar", "file_browser", "preferences",
                    "info", "text_editor", "console", "image_editor", "node_editor", "sequence_editor"):
        sp = getattr(getattr(theme, section, None), "space", None)
        if sp is None:
            continue
        _set(sp, "back", NIGHT)
        _set(sp, "header", PANEL)
        _set(sp, "text", TEXT)
        _set(sp, "header_text", TEXT)
        _set(sp, "text_hi", BLUE)

    v3d = theme.view_3d
    _set(v3d.space.gradients, "background_type", "SINGLE_COLOR")
    _set(v3d.space.gradients, "high_gradient", _hex("#D7D8DB"))
    _set(v3d.space.gradients, "gradient", _hex("#D7D8DB"))
    _set(v3d.space, "header", PANEL)
    _set(v3d.space, "text", TEXT)
    _set(v3d.space, "header_text", TEXT)
    _set(v3d, "object_active", BLUE_HI)
    _set(v3d, "object_selected", BLUE)
    _set(v3d, "grid", _hex("#A9AAAD"))
    _set(v3d, "wire", _hex("#26262A"))
    sidebars = getattr(getattr(theme, "regions", None), "sidebars", None)
    if sidebars is not None:
        _set(sidebars, "back", PANEL)
        _set(sidebars, "tab_back", NIGHT)

    font = os.path.join(HERE, "fonts", "Jost-Regular.ttf")
    if os.path.exists(font):
        _set(prefs.view, "font_path_ui", font)


def restore_look():
    while _saved:
        owner, attr, old = _saved.pop()
        try:
            setattr(owner, attr, old)
        except Exception:
            pass


# ------------------------------------------------------------------ new files

@persistent
def factory_startup(_):
    """Every new Sketch file: empty metric scene, dark world, real-size grid, Eevee."""
    for scene in bpy.data.scenes:
        modelling.apply_units(scene)
        for name in ("Cube", "Light", "Camera"):
            obj = bpy.data.objects.get(name)
            if obj is not None:
                bpy.data.objects.remove(obj, do_unlink=True)
        for engine in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"):
            try:
                scene.render.engine = engine
                break
            except TypeError:
                continue
        world = scene.world or bpy.data.worlds.new("Sketch world")
        scene.world = world
        world.color = (0.35, 0.36, 0.38)
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type != "VIEW_3D":
                continue
            for space in area.spaces:
                if space.type != "VIEW_3D":
                    continue
                space.show_region_ui = True          # Sketch sidebar visible
                space.show_region_toolbar = True     # Blender's real interface, as it is
                space.show_region_tool_header = True
                space.show_gizmo_navigate = False
                ov = space.overlay
                for attr in ("show_text", "show_stats", "show_cursor", "show_object_origins",
                             "show_relationship_lines", "show_extras"):
                    if hasattr(ov, attr):
                        setattr(ov, attr, False)
                ov.show_extras = True                 # keep lights and cameras visible
                shading = space.shading
                shading.light = "STUDIO"
                shading.show_cavity = True
                shading.show_shadows = True
                space.overlay.show_floor = True
                space.clip_end = 1000.0
                if space.region_3d is not None:
                    space.region_3d.view_distance = 30.0
        screen.show_statusbar = True


# ------------------------------------------------------------------ Select is on from the start (SketchUp)

def _start_select():
    """Start Sketch's Select tool in the first 3D view, as SketchUp starts with Select."""
    wm = bpy.context.window_manager
    for window in wm.windows:
        for area in window.screen.areas:
            if area.type != "VIEW_3D":
                continue
            region = next((r for r in area.regions if r.type == "WINDOW"), None)
            if region is None:
                continue
            try:
                with bpy.context.temp_override(window=window, area=area, region=region):
                    bpy.ops.sketch.select_tool("INVOKE_DEFAULT")
            except Exception as exc:
                print(f"Sketch: could not start Select: {exc}")
            return None
    return 0.5  # no 3D view yet: try again shortly


@persistent
def start_select_on_load(_):
    if not bpy.app.background:
        bpy.app.timers.register(_start_select, first_interval=0.3)


# ------------------------------------------------------------------ register

_modules = (venue, scenography, objects, modelling, tools, layers, physics, fabric, acoustics, lights, rider, ui)


def register():
    for mod in _modules:
        for cls in mod.classes:
            bpy.utils.register_class(cls)
    bpy.types.Scene.sketch_paint_color = bpy.props.FloatVectorProperty(
        name="Paint colour", subtype="COLOR", size=3, min=0.0, max=1.0, default=(0.85, 0.84, 0.81))
    layers.register_props()
    for mod in (acoustics, lights, rider):
        mod.register_props()
    tools.register_keymaps()
    ui.install()
    bpy.types.VIEW3D_MT_add.prepend(ui.add_menu)
    bpy.app.handlers.load_factory_startup_post.append(factory_startup)
    bpy.app.handlers.load_post.append(start_select_on_load)
    if not bpy.app.background:
        bpy.app.timers.register(_start_select, first_interval=1.0)
    apply_look()


def unregister():
    restore_look()
    if factory_startup in bpy.app.handlers.load_factory_startup_post:
        bpy.app.handlers.load_factory_startup_post.remove(factory_startup)
    if start_select_on_load in bpy.app.handlers.load_post:
        bpy.app.handlers.load_post.remove(start_select_on_load)
    bpy.types.VIEW3D_MT_add.remove(ui.add_menu)
    tools.unregister_keymaps()
    ui.uninstall()
    layers.unregister_props()
    for mod in (acoustics, lights, rider):
        mod.unregister_props()
    del bpy.types.Scene.sketch_paint_color
    for mod in reversed(_modules):
        for cls in reversed(mod.classes):
            bpy.utils.unregister_class(cls)
