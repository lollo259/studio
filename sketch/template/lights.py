# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Lighting control: patch the lights on DMX, record cues, run them, and see them in the
model. The same cues drive a real rig over Art-Net (any Art-Net node or console), and a
real console can drive the model (Listen to console) to preview a show.

Fixture types and their DMX footprint:
  Dimmer           1 ch  intensity
  RGB              4 ch  intensity, red, green, blue
  RGBW             5 ch  intensity, red, green, blue, white
  Moving head      8 ch  pan, pan fine, tilt, tilt fine, intensity, red, green, blue
"""

import json
import math
import socket
import struct
import time

import bpy
from bpy.props import (BoolProperty, CollectionProperty, EnumProperty, FloatProperty,
                       FloatVectorProperty, IntProperty, StringProperty)

FIXTURES = {
    "DIMMER": ("Dimmer", 1),
    "RGB": ("RGB", 4),
    "RGBW": ("RGBW", 5),
    "MOVING": ("Moving head", 8),
}
FIXTURE_ITEMS = [(k, v[0], f"{v[1]} DMX channel(s)") for k, v in FIXTURES.items()]
PAN_RANGE = math.radians(540)
TILT_RANGE = math.radians(270)

_sock = None
_listen_sock = None
_fade = None          # (start_time, duration, from_state, to_state)
_last_sent = 0.0


# ------------------------------------------------------------------ fixtures

def fixtures(scene):
    out = [o for o in scene.objects if o.type == "LIGHT" and o.sketch_fixture.patched]
    out.sort(key=lambda o: (o.sketch_fixture.universe, o.sketch_fixture.address))
    return out


def full_power(obj):
    if "sketch_full_power" not in obj:
        obj["sketch_full_power"] = float(obj.data.energy) if obj.data.energy > 0 else 800.0
    return obj["sketch_full_power"]


def state_of(obj):
    """The light's current look: intensity 0..1, colour, pan/tilt (radians)."""
    f = obj.sketch_fixture
    return {"i": f.intensity, "c": list(f.color), "w": f.white, "pan": f.pan, "tilt": f.tilt}


def apply_state(obj, st):
    f = obj.sketch_fixture
    f.intensity = st.get("i", f.intensity)
    if "c" in st:
        f.color = st["c"]
    f.white = st.get("w", f.white)
    f.pan = st.get("pan", f.pan)
    f.tilt = st.get("tilt", f.tilt)
    show(obj)


def show(obj):
    """Make the Blender light look like the fixture."""
    f = obj.sketch_fixture
    obj.data.energy = full_power(obj) * f.intensity
    if f.kind != "DIMMER":
        w = f.white if f.kind == "RGBW" else 0.0
        obj.data.color = tuple(min(1.0, c + w) for c in f.color)
    if f.kind == "MOVING":
        for c in obj.constraints:
            if c.type == "TRACK_TO":
                c.mute = True
        obj.rotation_euler = (f.tilt, 0.0, f.pan)


def dmx_values(obj):
    f = obj.sketch_fixture
    b = lambda v: max(0, min(255, int(round(v * 255))))
    if f.kind == "DIMMER":
        return [b(f.intensity)]
    rgb = [b(c) for c in f.color]
    if f.kind == "RGB":
        return [b(f.intensity)] + rgb
    if f.kind == "RGBW":
        return [b(f.intensity)] + rgb + [b(f.white)]
    pan = int(max(0.0, min(1.0, (f.pan + PAN_RANGE / 2) / PAN_RANGE)) * 65535)
    tilt = int(max(0.0, min(1.0, (f.tilt + TILT_RANGE / 2) / TILT_RANGE)) * 65535)
    return [pan >> 8, pan & 255, tilt >> 8, tilt & 255, b(f.intensity)] + rgb


def from_dmx(obj, ch):
    f = obj.sketch_fixture
    if f.kind == "DIMMER":
        f.intensity = ch[0] / 255
    elif f.kind in ("RGB", "RGBW"):
        f.intensity = ch[0] / 255
        f.color = (ch[1] / 255, ch[2] / 255, ch[3] / 255)
        if f.kind == "RGBW":
            f.white = ch[4] / 255
    else:
        f.pan = ((ch[0] << 8 | ch[1]) / 65535) * PAN_RANGE - PAN_RANGE / 2
        f.tilt = ((ch[2] << 8 | ch[3]) / 65535) * TILT_RANGE - TILT_RANGE / 2
        f.intensity = ch[4] / 255
        f.color = (ch[5] / 255, ch[6] / 255, ch[7] / 255)
    show(obj)


def universes(scene):
    """{universe: bytearray(512)} from every patched fixture."""
    out = {}
    for o in fixtures(scene):
        f = o.sketch_fixture
        u = out.setdefault(f.universe, bytearray(512))
        for i, v in enumerate(dmx_values(o)):
            a = f.address - 1 + i
            if 0 <= a < 512:
                u[a] = v
    return out


# ------------------------------------------------------------------ Art-Net

def artdmx_packet(universe, data, sequence=0):
    data = bytes(data)
    if len(data) % 2:
        data += b"\0"
    return (b"Art-Net\0" + struct.pack("<H", 0x5000) + struct.pack(">H", 14)
            + bytes([sequence & 255, 0]) + struct.pack("<H", universe & 0x7FFF)
            + struct.pack(">H", len(data)) + data)


def parse_artdmx(packet):
    if len(packet) < 18 or not packet.startswith(b"Art-Net\0"):
        return None
    if struct.unpack("<H", packet[8:10])[0] != 0x5000:
        return None
    universe = struct.unpack("<H", packet[14:16])[0]
    n = struct.unpack(">H", packet[16:18])[0]
    return universe, packet[18:18 + n]


def send(scene, force=False):
    global _sock, _last_sent
    s = scene.sketch_lights
    if not s.artnet:
        return
    now = time.time()
    if not force and now - _last_sent < 1 / 40:
        return
    _last_sent = now
    if _sock is None:
        _sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        _sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        _sock.setblocking(False)
    s.sequence = (s.sequence % 255) + 1
    for u, data in universes(scene).items():
        try:
            _sock.sendto(artdmx_packet(u, data, s.sequence), (s.target or "255.255.255.255", 6454))
        except OSError:
            pass


def _listen_tick():
    global _listen_sock
    scene = bpy.context.scene
    if scene is None or not scene.sketch_lights.listen:
        if _listen_sock is not None:
            _listen_sock.close()
            _listen_sock = None
        return None
    if _listen_sock is None:
        try:
            _listen_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            _listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            _listen_sock.bind(("", 6454))
            _listen_sock.setblocking(False)
        except OSError:
            scene.sketch_lights.listen = False
            return None
    latest = {}
    while True:
        try:
            pkt, _addr = _listen_sock.recvfrom(1024)
        except (BlockingIOError, OSError):
            break
        r = parse_artdmx(pkt)
        if r:
            latest[r[0]] = r[1]
    for o in fixtures(scene):
        f = o.sketch_fixture
        data = latest.get(f.universe)
        if data is None:
            continue
        n = FIXTURES[f.kind][1]
        ch = list(data[f.address - 1:f.address - 1 + n])
        if len(ch) == n:
            from_dmx(o, ch)
    return 1 / 30


# ------------------------------------------------------------------ cues

def snapshot(scene):
    return {o.name: state_of(o) for o in fixtures(scene)}


def _mix(a, b, t):
    out = {}
    for k in b:
        va, vb = a.get(k, b[k]), b[k]
        if isinstance(vb, list):
            out[k] = [x + (y - x) * t for x, y in zip(va, vb)]
        else:
            out[k] = va + (vb - va) * t
    return out


def _fade_tick():
    global _fade
    scene = bpy.context.scene
    if _fade is None or scene is None:
        return None
    t0, dur, a, b = _fade
    t = 1.0 if dur <= 0 else min(1.0, (time.time() - t0) / dur)
    k = t * t * (3 - 2 * t)
    for name, target in b.items():
        o = scene.objects.get(name)
        if o is not None:
            apply_state(o, _mix(a.get(name, target), target, k))
    send(scene, force=t >= 1.0)
    if t >= 1.0:
        _fade = None
        return None
    return 1 / 40


def go_to(scene, index):
    global _fade
    s = scene.sketch_lights
    if not (0 <= index < len(s.cues)):
        return False
    cue = s.cues[index]
    target = json.loads(cue.state or "{}")
    _fade = (time.time(), cue.fade, snapshot(scene), target)
    s.current = index
    if bpy.app.background:
        _fade = (time.time() - cue.fade - 1, cue.fade, _fade[2], target)
        _fade_tick()
    elif not bpy.app.timers.is_registered(_fade_tick):
        bpy.app.timers.register(_fade_tick)
    return True


# ------------------------------------------------------------------ properties

def _changed(self, context):
    obj = self.id_data
    if isinstance(obj, bpy.types.Object) and obj.type == "LIGHT" and self.patched:
        show(obj)
        send(context.scene)


class SketchFixture(bpy.types.PropertyGroup):
    patched: BoolProperty(name="On DMX", default=False)
    kind: EnumProperty(name="Type", items=FIXTURE_ITEMS, default="RGB", update=_changed)
    universe: IntProperty(name="Universe", default=0, min=0, max=32767)
    address: IntProperty(name="Address", default=1, min=1, max=512)
    intensity: FloatProperty(name="Intensity", default=1.0, min=0.0, max=1.0, subtype="FACTOR", update=_changed)
    color: FloatVectorProperty(name="Colour", subtype="COLOR", size=3, min=0.0, max=1.0,
                               default=(1.0, 1.0, 1.0), update=_changed)
    white: FloatProperty(name="White", default=0.0, min=0.0, max=1.0, subtype="FACTOR", update=_changed)
    pan: FloatProperty(name="Pan", default=0.0, subtype="ANGLE", min=-PAN_RANGE / 2, max=PAN_RANGE / 2,
                       update=_changed)
    tilt: FloatProperty(name="Tilt", default=0.0, subtype="ANGLE", min=-TILT_RANGE / 2, max=TILT_RANGE / 2,
                        update=_changed)


class SketchCue(bpy.types.PropertyGroup):
    name: StringProperty(name="Name", default="Cue")
    number: FloatProperty(name="Cue", default=1.0, precision=1)
    fade: FloatProperty(name="Fade", default=3.0, min=0.0, unit="TIME_ABSOLUTE")
    state: StringProperty(default="{}")


def _listen_changed(self, context):
    if self.listen and not bpy.app.timers.is_registered(_listen_tick):
        bpy.app.timers.register(_listen_tick)


class SketchLights(bpy.types.PropertyGroup):
    cues: CollectionProperty(type=SketchCue)
    current: IntProperty(name="Current cue", default=-1)
    active: IntProperty(default=0)
    artnet: BoolProperty(name="Send Art-Net", default=False,
                         description="Drive the real lights through an Art-Net node")
    target: StringProperty(name="Node IP", default="255.255.255.255",
                           description="Art-Net node address (255.255.255.255 = everyone on the network)")
    listen: BoolProperty(name="Listen to console", default=False, update=_listen_changed,
                         description="Show what a real lighting console sends over Art-Net")
    sequence: IntProperty(default=0)


# ------------------------------------------------------------------ operators

class SKETCH_OT_patch(bpy.types.Operator):
    """Put the selected lights on DMX, at the next free addresses"""
    bl_idname = "sketch.dmx_patch"
    bl_label = "Patch selected lights"
    bl_options = {"REGISTER", "UNDO"}

    kind: EnumProperty(name="Type", items=FIXTURE_ITEMS, default="RGB")
    universe: IntProperty(name="Universe", default=0, min=0)

    def execute(self, context):
        scene = context.scene
        used = 0
        for o in fixtures(scene):
            f = o.sketch_fixture
            if f.universe == self.universe:
                used = max(used, f.address - 1 + FIXTURES[f.kind][1])
        lights = sorted([o for o in context.selected_objects if o.type == "LIGHT"], key=lambda o: o.name)
        if not lights:
            self.report({"WARNING"}, "Select some lights first")
            return {"CANCELLED"}
        for o in lights:
            f = o.sketch_fixture
            n = FIXTURES[self.kind][1]
            if used + n > 512:
                self.report({"WARNING"}, f"Universe {self.universe} is full")
                break
            full_power(o)
            f.kind = self.kind
            f.universe = self.universe
            f.address = used + 1
            f.color = tuple(o.data.color)
            f.patched = True
            used += n
            show(o)
        self.report({"INFO"}, f"Patched {len(lights)} light(s)")
        return {"FINISHED"}


class SKETCH_OT_unpatch(bpy.types.Operator):
    """Take the selected lights off DMX"""
    bl_idname = "sketch.dmx_unpatch"
    bl_label = "Unpatch"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        for o in context.selected_objects:
            if o.type == "LIGHT":
                o.sketch_fixture.patched = False
        return {"FINISHED"}


class SKETCH_OT_record_cue(bpy.types.Operator):
    """Record how every patched light looks now as a cue"""
    bl_idname = "sketch.cue_record"
    bl_label = "Record cue"
    bl_options = {"REGISTER", "UNDO"}

    fade: FloatProperty(name="Fade", default=3.0, min=0.0, unit="TIME_ABSOLUTE")

    def execute(self, context):
        s = context.scene.sketch_lights
        cue = s.cues.add()
        cue.number = (s.cues[-2].number + 1) if len(s.cues) > 1 else 1.0
        cue.name = f"Cue {cue.number:g}"
        cue.fade = self.fade
        cue.state = json.dumps(snapshot(context.scene))
        s.active = len(s.cues) - 1
        s.current = s.active
        return {"FINISHED"}


class SKETCH_OT_update_cue(bpy.types.Operator):
    """Store the lights as they are now into the selected cue"""
    bl_idname = "sketch.cue_update"
    bl_label = "Update cue"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        s = context.scene.sketch_lights
        if 0 <= s.active < len(s.cues):
            s.cues[s.active].state = json.dumps(snapshot(context.scene))
        return {"FINISHED"}


class SKETCH_OT_delete_cue(bpy.types.Operator):
    """Delete the selected cue"""
    bl_idname = "sketch.cue_delete"
    bl_label = "Delete cue"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        s = context.scene.sketch_lights
        if 0 <= s.active < len(s.cues):
            s.cues.remove(s.active)
            s.active = max(0, s.active - 1)
        return {"FINISHED"}


class SKETCH_OT_go(bpy.types.Operator):
    """Go: fade to the next cue (Back goes to the previous one, or pick a cue to jump)"""
    bl_idname = "sketch.cue_go"
    bl_label = "Go"

    step: IntProperty(default=1)
    jump: BoolProperty(default=False, options={"SKIP_SAVE"})

    def execute(self, context):
        s = context.scene.sketch_lights
        idx = s.active if self.jump else s.current + self.step
        if not go_to(context.scene, idx):
            self.report({"INFO"}, "End of the cue list")
            return {"CANCELLED"}
        s.active = idx
        return {"FINISHED"}


class SKETCH_OT_blackout(bpy.types.Operator):
    """All patched lights out"""
    bl_idname = "sketch.blackout"
    bl_label = "Blackout"

    def execute(self, context):
        for o in fixtures(context.scene):
            o.sketch_fixture.intensity = 0.0
        send(context.scene, force=True)
        return {"FINISHED"}


class SKETCH_UL_cues(bpy.types.UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        s = context.scene.sketch_lights
        row = layout.row(align=True)
        row.label(text="▶" if index == s.current else "", icon="NONE")
        row.prop(item, "name", text="", emboss=False)
        row.prop(item, "fade", text="")


classes = (SketchFixture, SketchCue, SketchLights, SKETCH_OT_patch, SKETCH_OT_unpatch, SKETCH_OT_record_cue,
           SKETCH_OT_update_cue, SKETCH_OT_delete_cue, SKETCH_OT_go, SKETCH_OT_blackout, SKETCH_UL_cues)


def register_props():
    bpy.types.Object.sketch_fixture = bpy.props.PointerProperty(type=SketchFixture)
    bpy.types.Scene.sketch_lights = bpy.props.PointerProperty(type=SketchLights)


def unregister_props():
    del bpy.types.Object.sketch_fixture
    del bpy.types.Scene.sketch_lights


def draw_panel(layout, context):
    s = context.scene.sketch_lights
    obj = context.active_object
    row = layout.row(align=True)
    row.operator_menu_enum("sketch.dmx_patch", "kind", text="Patch selected lights", icon="LIGHT_SPOT")
    row.operator("sketch.dmx_unpatch", text="", icon="X")
    if obj is not None and obj.type == "LIGHT" and obj.sketch_fixture.patched:
        f = obj.sketch_fixture
        box = layout.box()
        r = box.row(align=True)
        r.prop(f, "kind", text="")
        r.prop(f, "universe", text="U")
        r.prop(f, "address", text="@")
        box.prop(f, "intensity", slider=True)
        if f.kind != "DIMMER":
            box.prop(f, "color")
        if f.kind == "RGBW":
            box.prop(f, "white", slider=True)
        if f.kind == "MOVING":
            r = box.row(align=True)
            r.prop(f, "pan")
            r.prop(f, "tilt")
    layout.template_list("SKETCH_UL_cues", "", s, "cues", s, "active", rows=4)
    row = layout.row(align=True)
    row.operator("sketch.cue_record", icon="REC")
    row.operator("sketch.cue_update", text="", icon="FILE_REFRESH")
    row.operator("sketch.cue_delete", text="", icon="TRASH")
    row = layout.row(align=True)
    row.scale_y = 1.4
    row.operator("sketch.cue_go", text="Back", icon="TRIA_LEFT").step = -1
    row.operator("sketch.cue_go", text="Go", icon="TRIA_RIGHT").step = 1
    row.operator("sketch.blackout", text="", icon="LIGHT")
    layout.operator("sketch.cue_go", text="Jump to selected cue").jump = True
    box = layout.box()
    box.prop(s, "artnet")
    if s.artnet:
        box.prop(s, "target")
    box.prop(s, "listen")
    n = len(fixtures(context.scene))
    box.label(text=f"{n} light(s) on DMX")
