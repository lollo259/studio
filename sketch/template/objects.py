# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ready-made objects at real size, sun and sky, and the video workspace.

Objects land at the 3D cursor. Sizes in metres.
"""

import math
import bmesh
import bpy
from bpy.props import EnumProperty, FloatProperty, IntProperty
from mathutils import Vector

from . import geometry as g
from .venue import chair_stand_meshes


def cursor(context):
    return context.scene.cursor.location.copy()


def _mesh(name, build, material_key):
    """A shared mesh (instanced every time the object is added again)."""
    me = bpy.data.meshes.get(name)
    if me is None:
        bm = bmesh.new()
        build(bm)
        me = bpy.data.meshes.new(name)
        bm.normal_update()
        bm.to_mesh(me)
        bm.free()
        me.materials.append(g.mat(material_key))
    return me


class _AddOne:
    bl_options = {"REGISTER", "UNDO"}
    rotation: FloatProperty(name="Rotation", default=0.0, subtype="ANGLE")


class SKETCH_OT_add_chair(_AddOne, bpy.types.Operator):
    """Add one orchestra chair"""
    bl_idname = "sketch.add_chair"
    bl_label = "Chair"

    def execute(self, context):
        chair, _stand = chair_stand_meshes()
        g.instance("Chair", chair, g.sketch_collection(context, "Objects"), cursor(context), self.rotation)
        return {"FINISHED"}


class SKETCH_OT_add_music_stand(_AddOne, bpy.types.Operator):
    """Add one music stand"""
    bl_idname = "sketch.add_music_stand"
    bl_label = "Music stand"

    def execute(self, context):
        _chair, stand = chair_stand_meshes()
        g.instance("Music stand", stand, g.sketch_collection(context, "Objects"), cursor(context), self.rotation)
        return {"FINISHED"}


class SKETCH_OT_add_riser(_AddOne, bpy.types.Operator):
    """Add a stage riser (platform), standard 2 x 1 m"""
    bl_idname = "sketch.add_riser"
    bl_label = "Riser"

    width: FloatProperty(name="Width", default=2.0, min=0.2, unit="LENGTH")
    depth: FloatProperty(name="Depth", default=1.0, min=0.2, unit="LENGTH")
    height: FloatProperty(name="Height", default=0.4, min=0.05, unit="LENGTH")

    def execute(self, context):
        obj = g.box("Riser", (self.width, self.depth, self.height), cursor(context) + Vector((0, 0, self.height / 2)),
                    g.sketch_collection(context, "Objects"), "stage")
        obj.rotation_euler = (0, 0, self.rotation)
        return {"FINISHED"}


def _speaker(bm):
    g.add_box(bm, (0.36, 0.32, 0.56), (0, 0, 1.6))                 # cabinet on a pole
    g.add_cylinder(bm, 0.018, 1.32, (0, 0, 0.66), segments=8)       # pole
    for a in range(3):                                               # tripod legs
        ang = a * 2 * math.pi / 3
        g.add_box(bm, (0.03, 0.55, 0.03), (0.22 * math.cos(ang), 0.22 * math.sin(ang), 0.05))


class SKETCH_OT_add_speaker(_AddOne, bpy.types.Operator):
    """Add a PA speaker on a stand"""
    bl_idname = "sketch.add_speaker"
    bl_label = "Speaker"

    def execute(self, context):
        me = _mesh("Sketch speaker", _speaker, "piano")
        g.instance("Speaker", me, g.sketch_collection(context, "Objects"), cursor(context), self.rotation)
        return {"FINISHED"}


def _mic_stand(bm):
    g.add_cylinder(bm, 0.12, 0.02, (0, 0, 0.01), segments=16)        # round base
    g.add_cylinder(bm, 0.009, 1.5, (0, 0, 0.76), segments=8)         # pole
    g.add_cylinder(bm, 0.012, 0.16, (0, 0.06, 1.55), segments=8, axis="Y")  # microphone


class SKETCH_OT_add_mic_stand(_AddOne, bpy.types.Operator):
    """Add a microphone on a round-base stand"""
    bl_idname = "sketch.add_mic_stand"
    bl_label = "Microphone stand"

    def execute(self, context):
        me = _mesh("Sketch mic stand", _mic_stand, "metal")
        g.instance("Microphone", me, g.sketch_collection(context, "Objects"), cursor(context), self.rotation)
        return {"FINISHED"}


# ------------------------------------------------------------------ sun and sky

class SKETCH_OT_sun_sky(bpy.types.Operator):
    """Real sun and sky for a place, date and time (like SketchUp's Shadows)"""
    bl_idname = "sketch.sun_sky"
    bl_label = "Sun and sky"
    bl_options = {"REGISTER", "UNDO"}

    latitude: FloatProperty(name="Latitude", default=46.06, min=-90, max=90)    # Udine
    longitude: FloatProperty(name="Longitude", default=13.24, min=-180, max=180)
    month: IntProperty(name="Month", default=6, min=1, max=12)
    day: IntProperty(name="Day", default=21, min=1, max=31)
    hour: FloatProperty(name="Hour (local)", default=15.0, min=0.0, max=24.0)
    utc_offset: FloatProperty(name="UTC offset", default=1.0, min=-12, max=14)
    north: FloatProperty(name="North", default=0.0, subtype="ANGLE", description="Where north is in the model (0 = +Y)")

    def sun_direction(self):
        """Elevation and azimuth of the sun (simple solar position, good to a degree)."""
        doy = sum((31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)[: self.month - 1]) + self.day
        decl = math.radians(23.44) * math.sin(math.radians(360.0 / 365.0 * (doy - 81)))
        b = math.radians(360.0 / 365.0 * (doy - 81))
        eot = 9.87 * math.sin(2 * b) - 7.53 * math.cos(b) - 1.5 * math.sin(b)  # minutes
        solar = self.hour + (4 * (self.longitude - 15 * self.utc_offset) + eot) / 60.0
        h = math.radians(15.0 * (solar - 12.0))
        lat = math.radians(self.latitude)
        el = math.asin(math.sin(lat) * math.sin(decl) + math.cos(lat) * math.cos(decl) * math.cos(h))
        az = math.atan2(math.sin(h), math.cos(h) * math.sin(lat) - math.tan(decl) * math.cos(lat))
        return el, az + math.pi  # azimuth from north, clockwise

    def execute(self, context):
        el, az = self.sun_direction()
        scene = context.scene
        sun = bpy.data.objects.get("Sun")
        if sun is None or sun.type != "LIGHT":
            sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
            g.sketch_collection(context, "Lighting").objects.link(sun)
        sun.data.energy = 4.0 if el > 0 else 0.0
        sun.data.angle = math.radians(0.53)
        # point the light along the sun's rays (from the sky down to the ground)
        sun.rotation_euler = (math.pi / 2 - el, 0.0, -(az - self.north) + math.pi)

        world = scene.world or bpy.data.worlds.new("Sketch world")
        scene.world = world
        world.use_nodes = True
        nt = world.node_tree
        sky = nt.nodes.get("Sketch sky") or nt.nodes.new("ShaderNodeTexSky")
        sky.name = "Sketch sky"
        for kind in ("NISHITA", "MULTIPLE_SCATTERING", "SINGLE_SCATTERING", "HOSEK_WILKIE"):
            try:
                sky.sky_type = kind
                break
            except TypeError:
                continue
        if hasattr(sky, "sun_elevation"):
            sky.sun_elevation = max(el, -0.1)
            sky.sun_rotation = -(az - self.north) + math.pi
        if hasattr(sky, "sun_direction"):
            sky.sun_direction = Vector((math.cos(el) * math.sin(az), math.cos(el) * math.cos(az), math.sin(el)))
        bg = nt.nodes.get("Background")
        if bg is not None:
            nt.links.new(sky.outputs[0], bg.inputs[0])
            bg.inputs[1].default_value = 0.3
        self.report({"INFO"}, f"Sun {math.degrees(el):.0f}° high, from {math.degrees(az) % 360:.0f}°")
        return {"FINISHED"}


# ------------------------------------------------------------------ video editing

class SKETCH_OT_video(bpy.types.Operator):
    """Video editing: a Video scene with Blender's video editor (cut, trim, titles, colour, export)"""
    bl_idname = "sketch.video"
    bl_label = "Video editing"

    def execute(self, context):
        scene = bpy.data.scenes.get("Video") or bpy.data.scenes.new("Video")
        scene.sequence_editor_create()
        scene.render.fps = 25
        context.window.scene = scene
        # use the biggest area as the video editor, a preview above it
        areas = sorted(context.screen.areas, key=lambda a: a.width * a.height, reverse=True)
        if areas:
            areas[0].type = "SEQUENCE_EDITOR"
            areas[0].spaces.active.view_type = "SEQUENCER_PREVIEW"
        return {"FINISHED"}


classes = (SKETCH_OT_add_chair, SKETCH_OT_add_music_stand, SKETCH_OT_add_riser, SKETCH_OT_add_speaker,
           SKETCH_OT_add_mic_stand, SKETCH_OT_sun_sky, SKETCH_OT_video)
