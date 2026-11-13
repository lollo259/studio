# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Acoustics: put Tape's tracks in the hall as sound sources, put a listener in a seat,
and hear the piece in that room.

The room is the box around the hall (Hall / Chapel collections, or everything if there
is no hall). Each of its six surfaces gets the absorption of the material it is made of
(Acoustic material in the panel can change it); seats, audience and people add their own
absorption. From that:
  * reverberation time (Sabine / Eyring),
  * early reflections with the image-source method (Allen & Berkley), per ear,
  * a late tail of decaying noise, slower in the lows than in the highs.
Every track is convolved with its own response and the result is played back and can be
saved as a WAV (to drop back into Tape).
"""

import math
import os

import bpy
import numpy as np
from bpy.props import (BoolProperty, CollectionProperty, EnumProperty, FloatProperty,
                       IntProperty, StringProperty)
from bpy_extras.io_utils import ImportHelper
from mathutils import Vector

from . import geometry as g

RATE = 48000
C = 343.0  # speed of sound, m/s

# absorption coefficient (mid frequencies, ~500 Hz-1 kHz)
ABSORPTION = {
    "CONCRETE": ("Concrete", 0.02),
    "BRICK": ("Brick", 0.03),
    "PLASTER": ("Plaster", 0.05),
    "STONE": ("Stone", 0.02),
    "GLASS": ("Glass", 0.04),
    "WOOD_FLOOR": ("Wooden floor", 0.07),
    "WOOD_PANEL": ("Wood panelling", 0.10),
    "CARPET": ("Carpet", 0.30),
    "CURTAIN": ("Heavy curtain", 0.55),
    "PANEL": ("Acoustic panel", 0.85),
    "OPEN": ("Open (no surface)", 1.0),
}
ABSORPTION_ITEMS = [(k, v[0], f"absorbs {int(v[1] * 100)}%") for k, v in ABSORPTION.items()]

# guesses from Sketch's own material names
MATERIAL_GUESS = (("glass", "GLASS"), ("carpet", "CARPET"), ("curtain", "CURTAIN"),
                  ("drape", "CURTAIN"), ("floor", "WOOD_FLOOR"), ("stage", "WOOD_FLOOR"),
                  ("wood", "WOOD_PANEL"), ("brick", "BRICK"), ("stone", "STONE"),
                  ("concrete", "CONCRETE"), ("wall", "PLASTER"), ("plaster", "PLASTER"))

SEAT_SABINS = 0.45      # m² per empty upholstered seat
PERSON_SABINS = 0.65    # m² per seated person

_handle = None
_last_mix = None


# ------------------------------------------------------------------ the room

def room_objects(scene):
    rooms = []
    for col in bpy.data.collections:
        if col.name.split(".")[0] in ("Hall", "Chapel") or col.name.startswith("Hall"):
            rooms += [o for o in col.all_objects if o.type == "MESH"]
    return rooms


def _world_bounds(objs):
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for o in objs:
        for c in o.bound_box:
            p = o.matrix_world @ Vector(c)
            lo = Vector((min(lo.x, p.x), min(lo.y, p.y), min(lo.z, p.z)))
            hi = Vector((max(hi.x, p.x), max(hi.y, p.y), max(hi.z, p.z)))
    return lo, hi


def acoustic_key(obj, poly=None):
    if obj is not None and obj.get("sketch_acoustic"):
        return obj["sketch_acoustic"]
    name = ""
    if obj is not None and obj.type == "MESH" and obj.data.materials:
        idx = poly.material_index if poly is not None else 0
        idx = min(idx, len(obj.data.materials) - 1)
        m = obj.data.materials[idx]
        name = (m.name if m else "").lower()
    name += " " + (obj.name.lower() if obj else "")
    for word, key in MATERIAL_GUESS:
        if word in name:
            return key
    return "PLASTER"


class Room:
    """The shoebox the sound lives in, with an absorption per side."""

    SIDES = ("x-", "x+", "y-", "y+", "floor", "ceiling")

    def __init__(self, scene):
        settings = scene.sketch_acoustics
        rooms = room_objects(scene)
        everything = [o for o in scene.objects if o.type == "MESH" and not o.get("sketch_track")]
        objs = rooms or everything
        if objs:
            lo, hi = _world_bounds(objs)
        else:
            lo, hi = Vector((-10, -10, 0)), Vector((10, 10, 8))
        if hi.z - lo.z < 2.5:
            hi.z = lo.z + max(settings.default_height, 2.5)
        self.lo, self.hi = lo, hi
        self.size = hi - lo
        self.alpha = {s: None for s in self.SIDES}
        self.area = {s: 0.0 for s in self.SIDES}
        self._measure(objs, settings)
        self.V = self.size.x * self.size.y * self.size.z
        sides_area = {"x-": self.size.y * self.size.z, "x+": self.size.y * self.size.z,
                      "y-": self.size.x * self.size.z, "y+": self.size.x * self.size.z,
                      "floor": self.size.x * self.size.y, "ceiling": self.size.x * self.size.y}
        self.S = sum(sides_area.values())
        A = 0.0
        for s in self.SIDES:
            A += sides_area[s] * self.alpha[s]
        # seats, people and audience
        seats = people = 0
        for o in scene.objects:
            n = o.name.lower()
            if "seat" in n or n.startswith("chair"):
                seats += 1
            elif "person" in n or "musician" in n or "people" in n:
                people += 1
        if settings.audience:
            A += seats * PERSON_SABINS
        else:
            A += seats * SEAT_SABINS
        A += people * PERSON_SABINS
        A += 4 * 0.0025 * self.V  # air
        self.A = max(A, 0.01)
        self.mean_alpha = min(self.A / self.S, 0.99)
        self.rt60_sabine = 0.161 * self.V / self.A
        self.rt60 = 0.161 * self.V / (-self.S * math.log(1 - self.mean_alpha) + 4 * 0.0025 * self.V)
        self.seats, self.people = seats, people

    def _measure(self, objs, settings):
        lo, hi = self.lo, self.hi
        tol = max(0.6, 0.03 * max(self.size))
        acc = {s: [0.0, 0.0] for s in self.SIDES}
        for o in objs:
            if o.type != "MESH":
                continue
            mw = o.matrix_world
            rot = mw.to_3x3()
            for p in o.data.polygons:
                c = mw @ p.center
                n = (rot @ p.normal).normalized()
                a = p.area * abs(rot.determinant()) ** (2 / 3)
                side = None
                if abs(n.z) > 0.8:
                    if abs(c.z - lo.z) < tol:
                        side = "floor"
                    elif abs(c.z - hi.z) < tol:
                        side = "ceiling"
                elif abs(n.x) > 0.8:
                    side = "x-" if abs(c.x - lo.x) < tol else ("x+" if abs(c.x - hi.x) < tol else None)
                elif abs(n.y) > 0.8:
                    side = "y-" if abs(c.y - lo.y) < tol else ("y+" if abs(c.y - hi.y) < tol else None)
                if side:
                    acc[side][0] += a * ABSORPTION[acoustic_key(o, p)][1]
                    acc[side][1] += a
        missing = "OPEN" if settings.open_air else settings.missing
        for s in self.SIDES:
            if acc[s][1] > 0.01:
                self.alpha[s] = acc[s][0] / acc[s][1]
            else:
                self.alpha[s] = ABSORPTION[missing if s != "floor" else "CONCRETE"][1]
            self.area[s] = acc[s][1]

    def inside(self, p):
        return Vector((min(max(p.x, self.lo.x + 0.05), self.hi.x - 0.05),
                       min(max(p.y, self.lo.y + 0.05), self.hi.y - 0.05),
                       min(max(p.z, self.lo.z + 0.05), self.hi.z - 0.05)))


# ------------------------------------------------------------------ impulse responses

def _image_sources(room, src, ear, order, max_t):
    """Arrival times (samples) and gains of the image sources of `src` heard at `ear`."""
    L = np.array(room.size)
    s = np.array(src - room.lo)
    r = np.array(ear - room.lo)
    beta = {k: math.sqrt(max(0.0, 1.0 - min(room.alpha[k], 1.0))) for k in room.SIDES}
    b = np.array([[beta["x-"], beta["x+"]], [beta["y-"], beta["y+"]], [beta["floor"], beta["ceiling"]]])
    n = np.arange(-order, order + 1)
    times, gains, dirs = [], [], []
    for px in (0, 1):
        for py in (0, 1):
            for pz in (0, 1):
                p = np.array([px, py, pz])
                mx, my, mz = np.meshgrid(n, n, n, indexing="ij")
                m = np.stack([mx.ravel(), my.ravel(), mz.ravel()], axis=1)
                img = (1 - 2 * p) * s + 2 * m * L          # image position
                d = img - r
                dist = np.linalg.norm(d, axis=1)
                # reflections on each pair of walls
                refl_lo = np.abs(m - p)                    # on the "-" wall
                refl_hi = np.abs(m)                        # on the "+" wall
                gain = np.ones(len(m))
                for ax in range(3):
                    gain *= b[ax, 0] ** refl_lo[:, ax] * b[ax, 1] ** refl_hi[:, ax]
                keep = (dist / C < max_t) & (np.sum(refl_lo + refl_hi, axis=1) <= order * 2)
                times.append(dist[keep] / C)
                gains.append(gain[keep] / np.maximum(dist[keep], 0.3))
                dirs.append(-d[keep] / np.maximum(dist[keep], 1e-6)[:, None])
    return np.concatenate(times), np.concatenate(gains), np.concatenate(dirs)


def _band_noise(n, rng, cutoff_hz, high):
    x = rng.standard_normal(n)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1 / RATE)
    mask = 1.0 / (1.0 + (f / cutoff_hz) ** 4)
    X *= (1.0 - mask) if high else mask
    return np.fft.irfft(X, n)


def impulse_response(room, src, ears, right, order, rng):
    """Stereo impulse response (n x 2) from `src` to the two ears."""
    rt = max(room.rt60, 0.08)
    length = int(RATE * min(rt * 1.1 + 0.05, 6.0))
    t_mix = min(0.003 * math.sqrt(room.V) + 0.02, 0.12)  # end of the early part
    ir = np.zeros((length, 2))
    for k, ear in enumerate(ears):
        t, gn, d = _image_sources(room, src, ear, order, t_mix)
        side = (1 if k == 1 else -1) * np.array(right)
        shadow = 1.0 + 0.35 * (d @ side)                    # head shadow
        idx = np.round(t * RATE).astype(int)
        ok = idx < length
        np.add.at(ir[:, k], idx[ok], (gn * shadow)[ok])
        # late tail: lows decay a little slower than highs (air, soft surfaces)
        nt = length
        tt = np.arange(nt) / RATE
        lo = _band_noise(nt, rng, 900.0, False) * np.exp(-6.91 * tt / (rt * 1.15))
        hi = _band_noise(nt, rng, 900.0, True) * np.exp(-6.91 * tt / (rt * 0.75))
        tail = lo + hi
        # level: diffuse energy of the room relative to the direct sound at 1 m
        direct = 1.0 / max((Vector(src) - Vector(ear)).length, 0.3)
        rev_energy = 4.0 * (1 - room.mean_alpha) / (room.mean_alpha * room.S)  # (1/r_c²)
        level = math.sqrt(rev_energy) * math.sqrt(1.0 / (np.mean(tail[:int(0.05 * RATE)] ** 2) + 1e-12))
        fade = np.clip((tt - t_mix * 0.6) / (t_mix * 0.6 + 1e-3), 0.0, 1.0)
        start = max(0, int((t_mix * 0.6 + (Vector(src) - Vector(ear)).length / C) * RATE))
        tail[:start] = 0.0
        ir[:, k] += tail * fade * level * 0.25
        _ = direct
    return ir


def _convolve(x, h):
    """Overlap-add convolution of a mono signal with a stereo response."""
    n_h = len(h)
    block = 1 << 17
    nfft = 1 << int(math.ceil(math.log2(block + n_h - 1)))
    H = np.fft.rfft(h, nfft, axis=0)
    out = np.zeros((len(x) + n_h - 1, 2))
    for i in range(0, len(x), block):
        seg = x[i:i + block]
        Y = np.fft.rfft(seg, nfft)[:, None] * H
        y = np.fft.irfft(Y, nfft, axis=0)[:len(seg) + n_h - 1]
        out[i:i + len(y)] += y
    return out


# ------------------------------------------------------------------ tracks and listener

def tracks(scene):
    return [o for o in scene.objects if o.get("sketch_track")]


def listener(scene):
    for o in scene.objects:
        if o.get("sketch_listener"):
            return o
    return None


def ear_positions(obj):
    """Two ears 18 cm apart. A listener looks along its local +Y; a camera along -Z."""
    mw = obj.matrix_world
    if obj.type == "CAMERA":
        fwd = (mw.to_3x3() @ Vector((0, 0, -1))).normalized()
        head = mw.translation.copy()
    else:
        fwd = (mw.to_3x3() @ Vector((0, 1, 0))).normalized()
        head = mw.translation + Vector((0, 0, 1.15))
    right = fwd.cross(Vector((0, 0, 1)))
    if right.length < 1e-4:
        right = Vector((1, 0, 0))
    right.normalize()
    return [head - right * 0.09, head + right * 0.09], right


def load_mono(path):
    import aud
    snd = aud.Sound(bpy.path.abspath(path)).resample(float(RATE), True)
    data = np.asarray(snd.data(), dtype=np.float64)
    if data.ndim == 2:
        data = data.mean(axis=1)
    return data


def studio_folder(*parts):
    base = os.path.join(os.path.expanduser("~"), "Documents", "Studio", "Sketch", *parts)
    os.makedirs(base, exist_ok=True)
    return base


def render_mix(context, report=None):
    scene = context.scene
    settings = scene.sketch_acoustics
    room = Room(scene)
    who = listener(scene) or scene.camera
    if who is None:
        raise RuntimeError("Put a listener in a seat first (Acoustics → Listener here).")
    ears, right = ear_positions(who)
    ears = [room.inside(e) for e in ears]
    srcs = [o for o in tracks(scene) if not (o.type == "SPEAKER" and o.data.muted)]
    if not srcs:
        raise RuntimeError("Add Tape's tracks first (Acoustics → Add tracks…).")
    rng = np.random.default_rng(7)
    mix = None
    wm = context.window_manager
    wm.progress_begin(0, len(srcs))
    for i, o in enumerate(srcs):
        x = load_mono(o["sketch_track"])
        gain = o.data.volume if o.type == "SPEAKER" else 1.0
        pos = room.inside(o.matrix_world.translation + Vector((0, 0, o.get("sketch_height", 1.1))))
        h = impulse_response(room, pos, ears, right, settings.order, rng)
        y = _convolve(x * gain, h)
        if mix is None or len(y) > len(mix):
            y2 = np.zeros((len(y), 2)) if mix is None else np.pad(mix, ((0, len(y) - len(mix)), (0, 0)))
            mix = y2
        mix[:len(y)] += y
        wm.progress_update(i + 1)
    wm.progress_end()
    peak = float(np.max(np.abs(mix))) or 1.0
    mix = (mix / peak * 0.89).astype(np.float32)
    return mix, room


# ------------------------------------------------------------------ operators

class SKETCH_OT_add_tracks(bpy.types.Operator, ImportHelper):
    """Put audio tracks (exported from Tape, or any audio file) on stage as sound sources"""
    bl_idname = "sketch.add_tracks"
    bl_label = "Add tracks…"
    bl_options = {"REGISTER", "UNDO"}

    filter_glob: StringProperty(default="*.wav;*.flac;*.mp3;*.ogg;*.aif;*.aiff", options={"HIDDEN"})
    files: CollectionProperty(type=bpy.types.OperatorFileListElement)
    directory: StringProperty(subtype="DIR_PATH")
    spacing: FloatProperty(name="Spacing", default=1.5, min=0.3, unit="LENGTH")

    def execute(self, context):
        names = [f.name for f in self.files if f.name] or [os.path.basename(self.filepath)]
        folder = self.directory or os.path.dirname(self.filepath)
        col = g.sketch_collection(context, "Sound")
        o = context.scene.cursor.location.copy()
        n = len(names)
        for i, name in enumerate(sorted(names)):
            path = os.path.join(folder, name)
            spk = bpy.data.speakers.new(os.path.splitext(name)[0])
            try:
                spk.sound = bpy.data.sounds.load(path, check_existing=True)
            except Exception:
                pass
            obj = bpy.data.objects.new(os.path.splitext(name)[0], spk)
            obj["sketch_track"] = path
            obj.location = o + Vector(((i - (n - 1) / 2) * self.spacing, 0, 0))
            obj.empty_display_size = 0.5 if hasattr(obj, "empty_display_size") else 1
            g.link(obj, col)
        self.report({"INFO"}, f"{n} track(s) on stage — move them where the players sit")
        return {"FINISHED"}


class SKETCH_OT_listener(bpy.types.Operator):
    """Sit a listener at the 3D cursor (or on the selected seat), facing the tracks"""
    bl_idname = "sketch.listener_here"
    bl_label = "Listener here"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        scene = context.scene
        lis = listener(scene)
        if lis is None:
            lis = bpy.data.objects.new("Listener", None)
            lis.empty_display_type = "SINGLE_ARROW"
            lis.empty_display_size = 0.6
            lis["sketch_listener"] = True
            g.link(lis, g.sketch_collection(context, "Sound"))
        act = context.active_object
        if act is not None and act is not lis and not act.get("sketch_track") and act.select_get():
            pos = act.matrix_world.translation.copy()
        else:
            pos = scene.cursor.location.copy()
        lis.location = pos
        src = tracks(scene)
        if src:
            c = sum((s.matrix_world.translation for s in src), Vector()) / len(src)
            d = c - pos
            lis.rotation_euler = (0, 0, math.atan2(-d.x, d.y))
        return {"FINISHED"}


class SKETCH_OT_acoustic_material(bpy.types.Operator):
    """What the selected surfaces are made of, for the acoustics"""
    bl_idname = "sketch.acoustic_material"
    bl_label = "Acoustic material"
    bl_options = {"REGISTER", "UNDO"}

    material: EnumProperty(name="Material", items=ABSORPTION_ITEMS, default="PLASTER")

    def execute(self, context):
        for o in context.selected_objects:
            o["sketch_acoustic"] = self.material
        return {"FINISHED"}


class SKETCH_OT_measure(bpy.types.Operator):
    """Work out the room's reverberation time"""
    bl_idname = "sketch.measure_room"
    bl_label = "Measure the room"

    def execute(self, context):
        r = Room(context.scene)
        s = context.scene.sketch_acoustics
        s.rt60 = r.rt60
        s.volume = r.V
        s.clarity = _clarity(r)
        self.report({"INFO"}, f"Reverberation {r.rt60:.2f} s · {r.V:.0f} m³ · {r.seats} seats")
        return {"FINISHED"}


def _clarity(room):
    """C80 estimate (dB) from Barron's revised theory, at 10 m."""
    T = max(room.rt60, 0.05)
    r = 10.0
    d = 100.0 / r ** 2
    e = (31200 * T / room.V) * math.exp(-0.04 * r / T)
    early = d + e * (1 - math.exp(-1.104 / T))
    late = e * math.exp(-1.104 / T)
    return 10 * math.log10(early / max(late, 1e-9))


class SKETCH_OT_listen(bpy.types.Operator):
    """Hear the tracks in this room, from where the listener sits"""
    bl_idname = "sketch.listen"
    bl_label = "Listen"

    def execute(self, context):
        global _handle, _last_mix
        import aud
        try:
            mix, room = render_mix(context)
        except RuntimeError as e:
            self.report({"WARNING"}, str(e))
            return {"CANCELLED"}
        _last_mix = mix
        s = context.scene.sketch_acoustics
        s.rt60, s.volume, s.clarity = room.rt60, room.V, _clarity(room)
        if _handle is not None:
            try:
                _handle.stop()
            except Exception:
                pass
        if not bpy.app.background:
            _handle = aud.Device().play(aud.Sound.buffer(mix, float(RATE)))
        self.report({"INFO"}, f"Playing · reverberation {room.rt60:.2f} s")
        return {"FINISHED"}


class SKETCH_OT_listen_stop(bpy.types.Operator):
    """Stop listening"""
    bl_idname = "sketch.listen_stop"
    bl_label = "Stop"

    def execute(self, context):
        global _handle
        if _handle is not None:
            try:
                _handle.stop()
            except Exception:
                pass
            _handle = None
        return {"FINISHED"}


class SKETCH_OT_save_listening(bpy.types.Operator):
    """Save what the listener hears as a WAV (Documents/Studio/Sketch/Listening) to import in Tape"""
    bl_idname = "sketch.save_listening"
    bl_label = "Save as WAV"

    def execute(self, context):
        import aud
        mix = _last_mix
        if mix is None:
            try:
                mix, _room = render_mix(context)
            except RuntimeError as e:
                self.report({"WARNING"}, str(e))
                return {"CANCELLED"}
        base = os.path.splitext(os.path.basename(bpy.data.filepath))[0] or "Sketch"
        who = listener(context.scene)
        path = os.path.join(studio_folder("Listening"), f"{base} - {who.name if who else 'camera'}.wav")
        aud.Sound.buffer(mix, float(RATE)).write(path, RATE, aud.CHANNELS_STEREO, aud.FORMAT_S24,
                                                 aud.CONTAINER_WAV, aud.CODEC_PCM)
        self.report({"INFO"}, f"Saved {path}")
        context.scene.sketch_acoustics.last_file = path
        return {"FINISHED"}


class SketchAcousticsSettings(bpy.types.PropertyGroup):
    order: IntProperty(name="Reflections", default=6, min=1, max=12,
                       description="How many bounces are traced exactly before the reverb tail")
    audience: BoolProperty(name="Full house", default=True, description="Seats are occupied")
    open_air: BoolProperty(name="Open air", default=False,
                           description="Missing walls and ceiling let the sound out")
    missing: EnumProperty(name="Missing surfaces", items=ABSORPTION_ITEMS, default="PLASTER",
                          description="What a wall or ceiling you hid or didn't draw is made of")
    default_height: FloatProperty(name="Ceiling height", default=8.0, min=2.5, unit="LENGTH")
    rt60: FloatProperty(name="Reverberation", default=0.0, unit="TIME_ABSOLUTE")
    volume: FloatProperty(name="Volume", default=0.0)
    clarity: FloatProperty(name="Clarity C80", default=0.0)
    last_file: StringProperty(name="Last file", default="")


classes = (SketchAcousticsSettings, SKETCH_OT_add_tracks, SKETCH_OT_listener, SKETCH_OT_acoustic_material,
           SKETCH_OT_measure, SKETCH_OT_listen, SKETCH_OT_listen_stop, SKETCH_OT_save_listening)


def register_props():
    bpy.types.Scene.sketch_acoustics = bpy.props.PointerProperty(type=SketchAcousticsSettings)


def unregister_props():
    del bpy.types.Scene.sketch_acoustics


def draw_panel(layout, context):
    s = context.scene.sketch_acoustics
    col = layout.column(align=True)
    col.operator("sketch.add_tracks", icon="SPEAKER")
    col.operator("sketch.listener_here", icon="USER")
    col.operator_menu_enum("sketch.acoustic_material", "material", text="Selected surface is…")
    row = layout.row(align=True)
    row.prop(s, "audience")
    row.prop(s, "open_air")
    layout.prop(s, "missing")
    layout.prop(s, "order")
    row = layout.row(align=True)
    row.scale_y = 1.3
    row.operator("sketch.listen", icon="PLAY")
    row.operator("sketch.listen_stop", text="", icon="SNAP_FACE")
    layout.operator("sketch.save_listening", icon="FILE_SOUND")
    box = layout.box()
    box.operator("sketch.measure_room", icon="DRIVER_DISTANCE")
    if s.rt60 > 0:
        box.label(text=f"Reverberation  {s.rt60:.2f} s")
        box.label(text=f"Volume  {s.volume:.0f} m³ · Clarity {s.clarity:+.1f} dB")
    for o in tracks(context.scene):
        r = layout.row(align=True)
        if o.type == "SPEAKER":
            r.prop(o.data, "muted", text="", icon="HIDE_OFF" if not o.data.muted else "HIDE_ON", emboss=False)
            r.prop(o.data, "volume", text=o.name, slider=True)
        else:
            r.label(text=o.name)
