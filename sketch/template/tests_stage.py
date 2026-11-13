# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for acoustics, lighting control and the technical rider (run by sketch-tests.py)."""

import math
import os
import tempfile
import traceback

import bpy
import numpy as np

from . import acoustics, lights, rider


def _fresh():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    s = bpy.context.scene
    s.sketch_lights.cues.clear()
    s.sketch_lights.current = -1
    s.cursor.location = (0, 0, 0)


def _hall(ceiling=True):
    bpy.context.scene.cursor.location = (0, 0, 0)
    bpy.ops.sketch.add_hall(width=14, length=24, height=8, ceiling=ceiling)


def _schroeder_rt(ir):
    e = ir ** 2
    edc = np.cumsum(e[::-1])[::-1]
    db = 10 * np.log10(edc / edc[0] + 1e-30)
    t = np.arange(len(db)) / acoustics.RATE
    i5, i25 = np.argmax(db < -5), np.argmax(db < -25)
    return (t[i25] - t[i5]) * 3.0  # T20 -> T60


def test_room_reverb_reasonable():
    _fresh()
    _hall()
    room = acoustics.Room(bpy.context.scene)
    assert abs(room.size.x - 14.8) < 0.5 and abs(room.size.y - 24.8) < 0.5, room.size
    assert 1.0 < room.rt60 < 6.0, room.rt60
    bpy.context.scene.sketch_acoustics.open_air = True
    _fresh()
    _hall(ceiling=False)
    open_room = acoustics.Room(bpy.context.scene)
    assert open_room.rt60 < room.rt60, (open_room.rt60, room.rt60)
    bpy.context.scene.sketch_acoustics.open_air = False


def test_curtains_shorten_reverb():
    _fresh()
    _hall()
    hard = acoustics.Room(bpy.context.scene).rt60
    for o in bpy.context.scene.objects:
        if "walls" in o.name:
            o["sketch_acoustic"] = "CURTAIN"
    soft = acoustics.Room(bpy.context.scene).rt60
    assert soft < hard * 0.6, (soft, hard)


def test_impulse_response_decays_like_the_room():
    _fresh()
    _hall()
    room = acoustics.Room(bpy.context.scene)
    from mathutils import Vector
    rng = np.random.default_rng(1)
    src = Vector((0, 20, 1.2))
    ears = [Vector((-0.09, 8, 1.2)), Vector((0.09, 8, 1.2))]
    ir = acoustics.impulse_response(room, src, ears, Vector((1, 0, 0)), 4, rng)
    direct = int(round((src - ears[0]).length / acoustics.C * acoustics.RATE))
    assert np.argmax(np.abs(ir[:, 0]) > 1e-6) <= direct + 1
    rt = _schroeder_rt(ir[:, 0])
    assert 0.5 * room.rt60 < rt < 1.6 * room.rt60, (rt, room.rt60)


def test_listen_mix():
    _fresh()
    _hall()
    folder = tempfile.mkdtemp()
    import aud
    paths = []
    for i, f in enumerate((440.0, 660.0)):
        p = os.path.join(folder, f"track{i}.wav")
        aud.Sound.sine(f, 48000.0).limit(0, 0.5).write(p, 48000, aud.CHANNELS_MONO, aud.FORMAT_S16,
                                                       aud.CONTAINER_WAV, aud.CODEC_PCM)
        paths.append(p)
    bpy.context.scene.cursor.location = (0, 20, 0)
    bpy.ops.sketch.add_tracks(directory=folder, files=[{"name": "track0.wav"}, {"name": "track1.wav"}])
    tr = acoustics.tracks(bpy.context.scene)
    assert len(tr) == 2, tr
    bpy.context.scene.cursor.location = (2, 6, 0)
    bpy.ops.sketch.listener_here()
    lis = acoustics.listener(bpy.context.scene)
    assert lis is not None
    fwd = lis.matrix_world.to_3x3() @ __import__("mathutils").Vector((0, 1, 0))
    assert fwd.y > 0.9, fwd
    mix, room = acoustics.render_mix(bpy.context)
    assert mix.shape[1] == 2 and len(mix) > 0.5 * 48000 + room.rt60 * 0.5 * 48000, mix.shape
    assert 0.5 < float(np.max(np.abs(mix))) <= 0.9
    # the listener sits left of centre: the right ear (towards the tracks' side) is not quieter
    assert np.std(mix[:, 0] - mix[:, 1]) > 1e-4
    assert bpy.ops.sketch.save_listening() == {"FINISHED"}
    assert os.path.exists(bpy.context.scene.sketch_acoustics.last_file)


def _three_lights():
    _fresh()
    col = []
    for i in range(3):
        d = bpy.data.lights.new(f"Spot {i}", "SPOT")
        d.energy = 1000
        o = bpy.data.objects.new(f"Spot {i}", d)
        bpy.context.scene.collection.objects.link(o)
        o.location = (i * 2.0, 0, 6)
        col.append(o)
    for o in bpy.context.scene.objects:
        o.select_set(o in col)
    return col


def test_patch_addresses():
    ls = _three_lights()
    bpy.ops.sketch.dmx_patch(kind="RGB")
    assert [o.sketch_fixture.address for o in ls] == [1, 5, 9]
    ls[2].select_set(False)
    ls[0].select_set(False)
    ls[1].sketch_fixture.patched = False
    bpy.ops.sketch.dmx_patch(kind="MOVING")
    assert ls[1].sketch_fixture.address == 13 and ls[1].sketch_fixture.kind == "MOVING"


def test_dmx_roundtrip_and_artnet():
    ls = _three_lights()
    bpy.ops.sketch.dmx_patch(kind="RGBW")
    f = ls[0].sketch_fixture
    f.intensity, f.color, f.white = 0.5, (1.0, 0.0, 0.25), 0.2
    assert abs(ls[0].data.energy - 500) < 1
    u = lights.universes(bpy.context.scene)[0]
    assert list(u[:5]) == [128, 255, 0, 64, 51], list(u[:5])
    pkt = lights.artdmx_packet(0, u, 7)
    assert len(pkt) == 18 + 512 and pkt[:8] == b"Art-Net\0"
    uni, data = lights.parse_artdmx(pkt)
    assert uni == 0 and data[:5] == bytes([128, 255, 0, 64, 51])
    f.intensity = 0.0
    lights.from_dmx(ls[0], list(data[:5]))
    assert abs(f.intensity - 128 / 255) < 1e-6 and abs(ls[0].data.energy - 1000 * 128 / 255) < 1


def test_moving_head_pan_tilt():
    ls = _three_lights()
    bpy.ops.sketch.dmx_patch(kind="MOVING")
    f = ls[0].sketch_fixture
    f.pan, f.tilt = math.radians(90), math.radians(-45)
    v = lights.dmx_values(ls[0])
    o2 = ls[1]
    lights.from_dmx(o2, v)
    assert abs(o2.sketch_fixture.pan - f.pan) < 1e-3 and abs(o2.sketch_fixture.tilt - f.tilt) < 1e-3
    assert abs(ls[0].rotation_euler.z - f.pan) < 1e-6


def test_cues_go_back():
    ls = _three_lights()
    bpy.ops.sketch.dmx_patch(kind="DIMMER")
    for o in ls:
        o.sketch_fixture.intensity = 1.0
    bpy.ops.sketch.cue_record(fade=0.0)
    for o in ls:
        o.sketch_fixture.intensity = 0.1
    bpy.ops.sketch.cue_record(fade=2.0)
    s = bpy.context.scene.sketch_lights
    assert len(s.cues) == 2 and s.cues[1].number == 2.0
    bpy.ops.sketch.cue_go(step=-1)
    assert s.current == 0 and abs(ls[0].sketch_fixture.intensity - 1.0) < 1e-6
    bpy.ops.sketch.cue_go(step=1)
    assert s.current == 1 and abs(ls[2].sketch_fixture.intensity - 0.1) < 1e-6
    assert bpy.ops.sketch.cue_go(step=1) == {"CANCELLED"}
    bpy.ops.sketch.blackout()
    assert all(o.data.energy == 0 for o in ls)


def test_rider():
    _fresh()
    _hall()
    bpy.context.scene.cursor.location = (0, 18, 0)
    bpy.ops.sketch.add_stage()
    bpy.context.scene.cursor.location = (1, 19, 0.8)
    bpy.ops.sketch.add_chair()
    bpy.ops.sketch.add_music_stand()
    d = bpy.data.lights.new("Front spot", "SPOT")
    o = bpy.data.objects.new("Front spot", d)
    bpy.context.scene.collection.objects.link(o)
    o.select_set(True)
    bpy.ops.sketch.dmx_patch(kind="DIMMER")
    bpy.ops.sketch.cue_record(fade=3.0)
    bpy.context.scene.sketch_rider.show = "Test show"
    page = rider.build_html(bpy.context.scene)
    for want in ("Test show", "<svg", "Stage plot", "Music stand", "Chair", "Front spot", "Cue 1", "Reverberation"):
        assert want in page, want
    assert bpy.ops.sketch.technical_rider(open=False) == {"FINISHED"}


TESTS = [test_room_reverb_reasonable, test_curtains_shorten_reverb, test_impulse_response_decays_like_the_room,
         test_listen_mix, test_patch_addresses, test_dmx_roundtrip_and_artnet, test_moving_head_pan_tilt,
         test_cues_go_back, test_rider]


def run():
    passed = failed = 0
    for fn in TESTS:
        try:
            fn()
            print(f"PASS  {fn.__name__}")
            passed += 1
        except Exception as exc:
            print(f"FAIL  {fn.__name__}: {exc}")
            traceback.print_exc()
            failed += 1
    print(f"SKETCH STAGE TESTS: {passed} passed, {failed} failed")
    return failed
