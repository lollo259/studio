# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Scenography: stage lights, truss, projection screens, cameras and lighting moods."""

import math
import os
import bmesh
import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, FloatVectorProperty, StringProperty
from bpy_extras.io_utils import ImportHelper
from mathutils import Matrix, Vector

from . import geometry as g


def cursor(context):
    return Vector(context.scene.cursor.location)


# colour temperatures (approximate linear RGB)
TEMPS = {
    "2700": (1.0, 0.66, 0.38),
    "3200": (1.0, 0.75, 0.52),
    "4300": (1.0, 0.87, 0.74),
    "5600": (1.0, 0.96, 0.93),
    "BLUE": (0.25, 0.45, 1.0),
}


def focus_target(context, location, name="Light focus"):
    col = g.sketch_collection(context, "Lights")
    empty = bpy.data.objects.new(name, None)
    empty.empty_display_type = "SPHERE"
    empty.empty_display_size = 0.2
    empty.location = location
    return g.link(empty, col)


def spot(context, name, location, target, power=800.0, angle=25.0, blend=0.3, temp="3200"):
    col = g.sketch_collection(context, "Lights")
    data = bpy.data.lights.new(name, "SPOT")
    data.energy = power
    data.spot_size = math.radians(angle)
    data.spot_blend = blend
    data.color = TEMPS.get(temp, TEMPS["3200"])
    if hasattr(data, "shadow_soft_size"):
        data.shadow_soft_size = 0.05
    obj = bpy.data.objects.new(name, data)
    obj.location = location
    g.link(obj, col)
    if target is not None:
        c = obj.constraints.new("TRACK_TO")
        c.target = target
        c.track_axis = "TRACK_NEGATIVE_Z"
        c.up_axis = "UP_Y"
    return obj


def area(context, name, location, size, power, temp="3200", rotation=(0, 0, 0)):
    col = g.sketch_collection(context, "Lights")
    data = bpy.data.lights.new(name, "AREA")
    data.energy = power
    data.size = size
    data.color = TEMPS.get(temp, TEMPS["3200"])
    obj = bpy.data.objects.new(name, data)
    obj.location = location
    obj.rotation_euler = rotation
    return g.link(obj, col)


def truss(context, length, location, name="Truss"):
    """Square box truss along X, 30 cm section."""
    col = g.sketch_collection(context, "Lights")
    bm = bmesh.new()
    s = 0.15
    for dy in (-s, s):
        for dz in (-s, s):
            g.add_cylinder(bm, 0.024, length, (0, dy, dz), segments=8, axis="X")
    n = max(2, int(length / 0.5))
    for i in range(n + 1):
        x = -length / 2 + length * i / n
        for (dy, dz, ax, dep) in ((-s, 0, "Z", 2 * s), (s, 0, "Z", 2 * s), (0, -s, "Y", 2 * s), (0, s, "Y", 2 * s)):
            g.add_cylinder(bm, 0.012, dep, (x, dy, dz), segments=6, axis=ax)
    return g.mesh_object(name, bm, col, "metal", location=location)


def build_concert_lights(context, centre, hall_width, hall_height):
    """Front truss with warm spots, a soft wash and one key light on the piano."""
    centre = Vector(centre)
    trim = min(hall_height - 0.5, 7.0)
    t_loc = centre + Vector((0, -5.0, trim - centre.z))
    truss(context, min(hall_width - 2.0, 12.0), t_loc, "Front truss")
    target = focus_target(context, centre + Vector((0, 0, 1.0)), "Stage centre")
    for i, dx in enumerate((-4.0, -1.5, 1.5, 4.0)):
        spot(context, f"Front spot {i + 1}", t_loc + Vector((dx, 0, -0.3)), target,
             power=600.0, angle=28.0, blend=0.35, temp="3200")
    area(context, "Stage wash", centre + Vector((0, 0.5, trim - centre.z)), 6.0, 900.0, "4300")
    key_target = focus_target(context, centre + Vector((0, -0.8, 0.9)), "Piano key focus")
    spot(context, "Piano key light", centre + Vector((-3.0, -3.0, trim - centre.z)), key_target,
         power=500.0, angle=18.0, blend=0.5, temp="2700")


def audience_camera(context, eye, target, name="Audience view"):
    col = g.sketch_collection(context, "Cameras")
    data = bpy.data.cameras.new(name)
    data.lens = 35.0
    data.clip_end = 500.0
    cam = bpy.data.objects.new(name, data)
    cam.location = eye
    g.look_at(cam, target)
    g.link(cam, col)
    context.scene.camera = cam
    return cam


def dark_world(context, strength=0.02):
    world = context.scene.world or bpy.data.worlds.new("Sketch world")
    context.scene.world = world
    world.color = (0.0, 0.0, 0.0)
    if world.node_tree:
        bg = world.node_tree.nodes.get("Background")
        if bg:
            bg.inputs["Color"].default_value = (0.02, 0.02, 0.025, 1.0)
            bg.inputs["Strength"].default_value = strength


# ------------------------------------------------------------------ operators

class SKETCH_OT_spot(bpy.types.Operator):
    """Add a stage spotlight at the 3D cursor, aimed at a focus point you can move"""
    bl_idname = "sketch.add_spot"
    bl_label = "Spotlight"
    bl_options = {"REGISTER", "UNDO"}

    power: FloatProperty(name="Power (W)", default=800.0, min=1.0)
    angle: FloatProperty(name="Beam angle (°)", default=25.0, min=1.0, max=160.0)
    blend: FloatProperty(name="Soft edge", default=0.3, min=0.0, max=1.0)
    temp: EnumProperty(name="Colour", items=[
        ("2700", "Warm 2700 K", ""), ("3200", "Tungsten 3200 K", ""),
        ("4300", "Neutral 4300 K", ""), ("5600", "Daylight 5600 K", ""), ("BLUE", "Blue", "")],
        default="3200")
    aim: FloatVectorProperty(name="Aim at", default=(0.0, 4.0, 1.0), subtype="TRANSLATION", unit="LENGTH",
                             description="Offset of the focus point from the light")

    def execute(self, context):
        loc = cursor(context)
        target = focus_target(context, loc + Vector(self.aim))
        spot(context, "Spot", loc, target, self.power, self.angle, self.blend, self.temp)
        return {"FINISHED"}


class SKETCH_OT_wash(bpy.types.Operator):
    """Add a soft wash light (area light) pointing down"""
    bl_idname = "sketch.add_wash"
    bl_label = "Wash light"
    bl_options = {"REGISTER", "UNDO"}

    size: FloatProperty(name="Size", default=4.0, min=0.1, unit="LENGTH")
    power: FloatProperty(name="Power (W)", default=600.0, min=1.0)
    temp: EnumProperty(name="Colour", items=[
        ("2700", "Warm 2700 K", ""), ("3200", "Tungsten 3200 K", ""),
        ("4300", "Neutral 4300 K", ""), ("5600", "Daylight 5600 K", ""), ("BLUE", "Blue", "")],
        default="4300")

    def execute(self, context):
        area(context, "Wash", cursor(context), self.size, self.power, self.temp)
        return {"FINISHED"}


class SKETCH_OT_truss(bpy.types.Operator):
    """Add a square box truss (30 cm) along X at the 3D cursor"""
    bl_idname = "sketch.add_truss"
    bl_label = "Truss"
    bl_options = {"REGISTER", "UNDO"}

    length: FloatProperty(name="Length", default=8.0, min=0.5, unit="LENGTH")

    def execute(self, context):
        truss(context, self.length, cursor(context))
        return {"FINISHED"}


class SKETCH_OT_screen(bpy.types.Operator, ImportHelper):
    """Add a projection screen showing an image or a video"""
    bl_idname = "sketch.add_screen"
    bl_label = "Projection screen"
    bl_options = {"REGISTER", "UNDO"}

    filter_glob: StringProperty(default="*.png;*.jpg;*.jpeg;*.webp;*.tif;*.mp4;*.mov;*.webm;*.avi",
                                options={"HIDDEN"})
    width: FloatProperty(name="Width", default=6.0, min=0.2, unit="LENGTH")
    bottom: FloatProperty(name="Bottom edge height", default=2.5, min=0.0, unit="LENGTH")
    brightness: FloatProperty(name="Brightness", default=1.5, min=0.0)
    filepath: StringProperty(subtype="FILE_PATH")

    def invoke(self, context, event):
        return ImportHelper.invoke(self, context, event)

    def execute(self, context):
        col = g.sketch_collection(context, "Projection")
        img = None
        aspect = 16 / 9
        if self.filepath and os.path.exists(self.filepath):
            img = bpy.data.images.load(self.filepath, check_existing=True)
            w, h = img.size[:]
            if w and h:
                aspect = w / h
        W = self.width
        H = W / aspect
        bm = bmesh.new()
        verts = [bm.verts.new(v) for v in ((-W / 2, 0, 0), (W / 2, 0, 0), (W / 2, 0, H), (-W / 2, 0, H))]
        face = bm.faces.new(verts)
        uv = bm.loops.layers.uv.new("UVMap")
        for loop, co in zip(face.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
            loop[uv].uv = co
        loc = cursor(context) + Vector((0, 0, self.bottom))
        obj = g.mesh_object("Projection screen", bm, col, location=loc)
        m = bpy.data.materials.new("Projection")
        if hasattr(m, "use_nodes"):
            try:
                m.use_nodes = True
            except Exception:
                pass
        nt = m.node_tree
        nt.nodes.clear()
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        emit = nt.nodes.new("ShaderNodeEmission")
        emit.inputs["Strength"].default_value = self.brightness
        nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
        if img is not None:
            tex = nt.nodes.new("ShaderNodeTexImage")
            tex.image = img
            if img.source == "MOVIE":
                tex.image_user.use_auto_refresh = True
                tex.image_user.frame_duration = max(1, img.frame_duration)
            nt.links.new(tex.outputs["Color"], emit.inputs["Color"])
        obj.data.materials.append(m)
        return {"FINISHED"}


class SKETCH_OT_mood(bpy.types.Operator):
    """Switch the whole room between lighting moods"""
    bl_idname = "sketch.lighting_mood"
    bl_label = "Lighting mood"
    bl_options = {"REGISTER", "UNDO"}

    mood: EnumProperty(name="Mood", items=[
        ("CONCERT", "Concert", "Stage lights on, house dark"),
        ("HOUSE", "House lights", "Everything evenly lit, like before the concert"),
        ("BLACKOUT", "Blackout", "All Sketch lights off"),
    ], default="CONCERT")

    def execute(self, context):
        col = bpy.data.collections.get("Lights")
        lights = [o for o in (col.all_objects if col else []) if o.type == "LIGHT"]
        house = [o for o in context.scene.objects if o.name.startswith("House light")]
        for o in lights:
            o.hide_render = o.hide_viewport = self.mood == "BLACKOUT"
        if self.mood == "HOUSE":
            if not house:
                b = None
                for ob in context.scene.objects:
                    if ob.type == "MESH" and ob.name.endswith("floor"):
                        b = ob
                        break
                c = b.location if b else cursor(context)
                dims = b.dimensions if b else Vector((12, 20, 0))
                for i, dy in enumerate((-dims.y * 0.3, 0.0, dims.y * 0.3)):
                    area(context, f"House light {i + 1}", Vector((c.x, c.y + dy, c.z + 6.0)),
                         max(dims.x * 0.8, 2.0), 1500.0, "4300")
            dark_world(context, 0.3)
        else:
            for o in house:
                bpy.data.objects.remove(o, do_unlink=True)
            dark_world(context, 0.02)
        return {"FINISHED"}


class SKETCH_OT_camera_here(bpy.types.Operator):
    """Make a seated-audience camera from the 3D cursor, looking at the stage centre"""
    bl_idname = "sketch.camera_from_seat"
    bl_label = "Camera from seat"
    bl_options = {"REGISTER", "UNDO"}

    eye: FloatProperty(name="Eye height (seated)", default=1.15, min=0.3, max=3.0, unit="LENGTH")

    def execute(self, context):
        target = bpy.data.objects.get("Stage centre")
        aim = target.location if target else cursor(context) + Vector((0, 10, 0))
        audience_camera(context, cursor(context) + Vector((0, 0, self.eye)), aim, "Seat view")
        return {"FINISHED"}


classes = (SKETCH_OT_spot, SKETCH_OT_wash, SKETCH_OT_truss, SKETCH_OT_screen, SKETCH_OT_mood, SKETCH_OT_camera_here)


# ------------------------------------------------------------------ film cameras and live screens

def camera_body(context, cam):
    """A small video camera on a tripod, attached to the Blender camera so you can see it in the scene."""
    col = g.sketch_collection(context, "Cameras")
    bm = bmesh.new()
    # everything sits behind the lens (+Z), so the camera never films itself
    g.add_cylinder(bm, 0.035, 0.10, (0, 0, 0.05), segments=16)           # lens
    g.add_box(bm, (0.12, 0.10, 0.22), (0, 0, 0.21))                       # body
    g.add_box(bm, (0.05, 0.05, 0.08), (0, 0.075, 0.22))                   # handle / viewfinder
    body = g.mesh_object(f"{cam.name} body", bm, col, "piano")
    body.parent = cam
    body.hide_select = True
    tri = bmesh.new()
    top_z = max(cam.location.z - 0.12, 0.2)
    head = tri.verts.new((0, 0, top_z))
    for a in (90, 210, 330):
        r = math.radians(a)
        foot = Vector((0.35 * math.cos(r), 0.35 * math.sin(r), 0.0))
        mid = (Vector(head.co) + foot) / 2
        axis = (Vector(head.co) - foot)
        m = Matrix.Translation(mid) @ axis.to_track_quat("Z", "Y").to_matrix().to_4x4()
        bmesh.ops.create_cone(tri, cap_ends=True, segments=6, radius1=0.012, radius2=0.012,
                              depth=axis.length, matrix=m)
    tri.verts.remove(head)
    legs = g.mesh_object(f"{cam.name} tripod", tri, col, "metal")
    legs.location = (cam.location.x, cam.location.y, 0.0)
    return body, legs


class SKETCH_OT_film_camera(bpy.types.Operator):
    """Add a film camera at the 3D cursor: a real Blender camera with a visible camera body and tripod"""
    bl_idname = "sketch.add_film_camera"
    bl_label = "Film camera"
    bl_options = {"REGISTER", "UNDO"}

    height: FloatProperty(name="Lens height", default=1.5, min=0.1, unit="LENGTH")
    lens: FloatProperty(name="Focal length (mm)", default=35.0, min=8.0, max=300.0)
    aim: FloatVectorProperty(name="Look at", default=(0.0, 3.0, 1.2), subtype="TRANSLATION", unit="LENGTH",
                             description="Point to look at, relative to the camera")

    def execute(self, context):
        col = g.sketch_collection(context, "Cameras")
        n = len([o for o in bpy.data.objects if o.type == "CAMERA" and o.name.startswith("Film camera")]) + 1
        data = bpy.data.cameras.new(f"Film camera {n}")
        data.lens = self.lens
        cam = bpy.data.objects.new(f"Film camera {n}", data)
        cam.location = cursor(context) + Vector((0, 0, self.height))
        g.look_at(cam, cam.location + Vector(self.aim))
        g.link(cam, col)
        camera_body(context, cam)
        return {"FINISHED"}


def live_dir():
    path = bpy.path.abspath("//sketch_live") if bpy.data.filepath else os.path.join(
        __import__("tempfile").gettempdir(), "sketch_live")
    os.makedirs(path, exist_ok=True)
    return path


def live_screens(scene):
    return [o for o in scene.objects if o.get("sketch_live_camera")]


def screen_feed_material(screen, image, brightness=1.5):
    mat = bpy.data.materials.get(f"Live {screen.name}") or bpy.data.materials.new(f"Live {screen.name}")
    if hasattr(mat, "use_nodes"):
        try:
            mat.use_nodes = True
        except Exception:
            pass
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.inputs["Strength"].default_value = brightness
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    nt.links.new(tex.outputs["Color"], emit.inputs["Color"])
    nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    screen.data.materials.clear()
    screen.data.materials.append(mat)


def update_live_screens(context):
    """Pass 1: render every film camera that feeds a screen and put the picture on it."""
    scene = context.scene
    keep_cam, keep_path = scene.camera, scene.render.filepath
    keep_res = (scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage)
    done = 0
    for screen in live_screens(scene):
        cam = bpy.data.objects.get(screen["sketch_live_camera"])
        if cam is None:
            continue
        scene.camera = cam
        scene.render.resolution_x, scene.render.resolution_y = 1280, 720
        scene.render.resolution_percentage = 100
        path = os.path.join(live_dir(), f"{cam.name.replace(' ', '_')}.png")
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(path, check_existing=True)
        img.reload()
        screen_feed_material(screen, img)
        done += 1
    scene.camera, scene.render.filepath = keep_cam, keep_path
    scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = keep_res
    return done


class SKETCH_OT_live_screen(bpy.types.Operator):
    """Show what a film camera sees on the selected screen"""
    bl_idname = "sketch.live_screen"
    bl_label = "Live screen"
    bl_options = {"REGISTER", "UNDO"}

    def camera_items(self, context):
        cams = [o for o in context.scene.objects if o.type == "CAMERA"]
        return [(c.name, c.name, "") for c in cams] or [("NONE", "No camera", "")]

    camera: EnumProperty(name="Camera", items=camera_items)

    @classmethod
    def poll(cls, context):
        o = context.active_object
        return o is not None and o.type == "MESH"

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        screen = context.active_object
        if self.camera == "NONE":
            self.report({"WARNING"}, "Add a film camera first")
            return {"CANCELLED"}
        screen["sketch_live_camera"] = self.camera
        if not screen.data.uv_layers:
            screen.data.uv_layers.new(name="UVMap")
        n = update_live_screens(context)
        self.report({"INFO"}, f"{n} live screen(s) updated")
        return {"FINISHED"}


class SKETCH_OT_update_screens(bpy.types.Operator):
    """Re-film every live screen (do this after you change the scene)"""
    bl_idname = "sketch.update_live_screens"
    bl_label = "Update live screens"

    def execute(self, context):
        n = update_live_screens(context)
        self.report({"INFO"}, f"{n} live screen(s) updated")
        return {"FINISHED"}


class SKETCH_OT_render_with_screens(bpy.types.Operator):
    """Render the main camera with live screens up to date (image or whole video)"""
    bl_idname = "sketch.render_with_screens"
    bl_label = "Render with live screens"

    video: BoolProperty(name="Whole video", default=False,
                        description="Render every frame: the screens follow what the film cameras see")

    def execute(self, context):
        scene = context.scene
        if not self.video:
            update_live_screens(context)
            out = scene.render.filepath or os.path.join(live_dir(), "render.png")
            bpy.ops.render.render(write_still=bool(scene.render.filepath))
            return {"FINISHED"}
        base = scene.render.filepath or os.path.join(live_dir(), "frames", "frame_")
        os.makedirs(os.path.dirname(base), exist_ok=True)
        keep = scene.frame_current
        for f in range(scene.frame_start, scene.frame_end + 1):
            scene.frame_set(f)
            update_live_screens(context)
            scene.render.filepath = f"{base}{f:04d}"
            bpy.ops.render.render(write_still=True)
        scene.render.filepath = base
        scene.frame_set(keep)
        self.report({"INFO"}, f"Frames saved next to {base}")
        return {"FINISHED"}


classes = classes + (SKETCH_OT_film_camera, SKETCH_OT_live_screen, SKETCH_OT_update_screens, SKETCH_OT_render_with_screens)
