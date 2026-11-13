# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Technical rider: a printable page made from the model — stage plot (drawn from above),
stage and room sizes, backline and furniture, input list (microphones and tracks),
lighting patch and cue list, power and the people on stage.

Saved as HTML in Documents/Studio/Sketch/Riders and opened in the browser
(print it, or save it as PDF from there)."""

import datetime
import html
import os
import webbrowser
from collections import Counter

import bpy
from bpy.props import StringProperty
from mathutils import Vector

from . import acoustics, lights

POWER_W = {"spot": 750, "wash": 575, "moving": 450, "speaker": 600, "piano": 0, "default_light": 500}

KINDS = (  # (word in the object name, category, label)
    ("grand piano", "Backline", "Grand piano"), ("upright piano", "Backline", "Upright piano"),
    ("piano", "Backline", "Piano"), ("bench", "Backline", "Piano bench"),
    ("music stand", "Furniture", "Music stand"), ("chair", "Furniture", "Chair"),
    ("riser", "Furniture", "Riser"), ("microphone", "Audio", "Microphone + stand"),
    ("mic stand", "Audio", "Microphone + stand"), ("speaker", "Audio", "Speaker"),
    ("screen", "Video", "Projection screen"), ("film camera", "Video", "Camera"),
    ("truss", "Rigging", "Truss"),
)


def _kind(obj):
    n = obj.name.lower().split(".")[0]
    for word, cat, label in KINDS:
        if word in n:
            return cat, label
    return None


def _bounds(objs):
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for o in objs:
        for c in o.bound_box:
            p = o.matrix_world @ Vector(c)
            lo = Vector((min(lo.x, p.x), min(lo.y, p.y), min(lo.z, p.z)))
            hi = Vector((max(hi.x, p.x), max(hi.y, p.y), max(hi.z, p.z)))
    return lo, hi


def stage_objects(scene):
    out = []
    for col in bpy.data.collections:
        if col.name.startswith("Stage"):
            out += [o for o in col.all_objects if o.type == "MESH"]
    return out


def gather(scene):
    info = {}
    st = stage_objects(scene)
    if st:
        lo, hi = _bounds(st)
        info["stage"] = (hi.x - lo.x, hi.y - lo.y, hi.z - lo.z)
        area_lo, area_hi = lo, hi
    else:
        info["stage"] = None
        area_lo = area_hi = None
    room = acoustics.Room(scene)
    info["room"] = room
    items = Counter()
    plot = []
    for o in scene.objects:
        if not o.visible_get():
            continue
        k = _kind(o)
        if k:
            items[k] += 1
        if o.type in ("MESH", "SPEAKER", "LIGHT", "EMPTY") and (k or o.get("sketch_track") or o.type == "LIGHT"):
            plot.append(o)
    info["items"] = items
    info["plot"] = plot
    # inputs: microphones, then tracks played from Tape
    inputs = []
    for o in sorted(scene.objects, key=lambda o: o.name):
        n = o.name.lower()
        if "microphone" in n or "mic stand" in n:
            inputs.append((o.name, "Microphone", "XLR · stand"))
    for o in acoustics.tracks(scene):
        inputs.append((o.name, "Playback (Tape)", os.path.basename(o["sketch_track"])))
    info["inputs"] = inputs
    info["fixtures"] = lights.fixtures(scene)
    info["cues"] = list(scene.sketch_lights.cues)
    power = 0
    for o in scene.objects:
        if o.type == "LIGHT":
            k = o.sketch_fixture.kind if o.sketch_fixture.patched else ""
            power += POWER_W["moving"] if k == "MOVING" else (
                POWER_W["spot"] if o.data.type == "SPOT" else POWER_W["wash"] if o.data.type == "AREA" else 0)
        elif "speaker" in o.name.lower():
            power += POWER_W["speaker"]
    info["power"] = power
    people = [o for o in scene.objects if any(w in o.name.lower() for w in ("person", "musician", "player"))]
    chairs = sum(1 for o in scene.objects if o.name.lower().startswith("chair"))
    info["performers"] = max(len(people), chairs if chairs < 80 else 0)
    if area_lo is None and plot:
        area_lo, area_hi = _bounds([o for o in plot if o.type == "MESH"] or plot)
    info["area"] = (area_lo, area_hi)
    return info


def _svg_plot(info):
    lo, hi = info["area"]
    plot = info["plot"]
    if lo is None:
        return "<p>Nothing on stage yet.</p>"
    pad = 1.0
    x0, y0 = lo.x - pad, lo.y - pad
    w, h = (hi.x - lo.x) + 2 * pad, (hi.y - lo.y) + 2 * pad
    scale = 640 / max(w, h, 1)
    W, H = w * scale, h * scale
    sx = lambda x: (x - x0) * scale
    sy = lambda y: H - (y - y0) * scale
    parts = [f'<svg viewBox="0 0 {W:.0f} {H + 30:.0f}" width="100%" xmlns="http://www.w3.org/2000/svg" '
             f'font-family="Jost, Helvetica, sans-serif" font-size="11">']
    parts.append(f'<rect x="0" y="0" width="{W:.0f}" height="{H:.0f}" fill="#f6f5f1" stroke="#222"/>')
    st = info["stage"]
    for o in plot:
        p = o.matrix_world.translation
        if o.type == "LIGHT":
            parts.append(f'<circle cx="{sx(p.x):.1f}" cy="{sy(p.y):.1f}" r="6" fill="#ffb000" stroke="#222"/>')
            if o.sketch_fixture.patched:
                f = o.sketch_fixture
                parts.append(f'<text x="{sx(p.x) + 8:.1f}" y="{sy(p.y) + 4:.1f}">{f.universe}.{f.address}</text>')
            continue
        if o.get("sketch_track") or o.type == "SPEAKER":
            parts.append(f'<rect x="{sx(p.x) - 6:.1f}" y="{sy(p.y) - 6:.1f}" width="12" height="12" '
                         f'fill="#ff5a1f" stroke="#222"/>')
            parts.append(f'<text x="{sx(p.x) + 9:.1f}" y="{sy(p.y) + 4:.1f}">{html.escape(o.name)}</text>')
            continue
        if o.type == "MESH":
            blo, bhi = _bounds([o])
            parts.append(f'<rect x="{sx(blo.x):.1f}" y="{sy(bhi.y):.1f}" width="{(bhi.x - blo.x) * scale:.1f}" '
                         f'height="{(bhi.y - blo.y) * scale:.1f}" fill="#ddd" stroke="#444"/>')
            k = _kind(o)
            if k and k[1] not in ("Chair", "Music stand"):
                parts.append(f'<text x="{sx(p.x):.1f}" y="{sy(p.y):.1f}" text-anchor="middle">'
                             f'{html.escape(k[1])}</text>')
    # audience side and scale bar
    parts.append(f'<text x="{W / 2:.0f}" y="{H + 20:.0f}" text-anchor="middle" font-weight="600">'
                 f'AUDIENCE ↓ (front of stage)</text>')
    parts.append(f'<line x1="10" y1="{H - 10:.0f}" x2="{10 + scale:.0f}" y2="{H - 10:.0f}" stroke="#222" '
                 f'stroke-width="2"/><text x="{14 + scale:.0f}" y="{H - 6:.0f}">1 m</text>')
    if st:
        parts.append(f'<text x="8" y="16">Stage {st[0]:.1f} × {st[1]:.1f} m, {st[2]:.2f} m high</text>')
    parts.append("</svg>")
    return "".join(parts)


def build_html(scene):
    r = scene.sketch_rider
    info = gather(scene)
    room = info["room"]
    esc = html.escape
    today = datetime.date.today().strftime("%d %B %Y")
    title = r.show or os.path.splitext(os.path.basename(bpy.data.filepath))[0] or "Untitled show"
    rows = lambda cells: "".join(f"<td>{esc(str(c))}</td>" for c in cells)
    out = [f"""<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<title>Technical rider — {esc(title)}</title>
<style>
body{{font-family:Jost,Helvetica,Arial,sans-serif;max-width:860px;margin:32px auto;color:#111;padding:0 20px}}
h1{{font-size:28px;margin:0}} h2{{font-size:15px;text-transform:uppercase;letter-spacing:.08em;
border-bottom:2px solid #111;padding-bottom:4px;margin-top:28px}}
table{{border-collapse:collapse;width:100%;font-size:13px}} td,th{{border-bottom:1px solid #ccc;padding:5px 6px;
text-align:left}} th{{font-weight:600;background:#f0efea}} .meta{{color:#555;font-size:13px}}
.dot{{display:inline-block;width:8px;height:8px;border-radius:4px;background:#ff5a1f;margin-right:6px}}
@media print{{body{{margin:0}} h2{{break-after:avoid}} table,svg{{break-inside:avoid}}}}
</style></head><body>
<h1><span class="dot"></span>{esc(title)}</h1>
<p class="meta">Technical rider · {esc(r.artist)} · {today}</p>"""]
    if r.contact:
        out.append(f"<p class='meta'>Technical contact: {esc(r.contact)}</p>")
    out.append("<h2>Stage plot</h2>" + _svg_plot(info))
    out.append("<h2>Stage and room</h2><table>")
    st = info["stage"]
    if st:
        out.append(f"<tr><th>Stage</th><td>{st[0]:.1f} m wide × {st[1]:.1f} m deep, {st[2]:.2f} m high</td></tr>")
    out.append(f"<tr><th>Room</th><td>{room.size.x:.1f} × {room.size.y:.1f} m, {room.size.z:.1f} m high "
               f"({room.V:.0f} m³)</td></tr>")
    out.append(f"<tr><th>Reverberation</th><td>{room.rt60:.2f} s (estimated from the model)</td></tr>")
    if room.seats:
        out.append(f"<tr><th>Seats</th><td>{room.seats}</td></tr>")
    if info["performers"]:
        out.append(f"<tr><th>Performers on stage</th><td>{info['performers']}</td></tr>")
    out.append("</table>")
    if info["items"]:
        out.append("<h2>Backline and furniture</h2><table><tr><th>Item</th><th>Qty</th><th>Department</th></tr>")
        for (cat, label), n in sorted(info["items"].items()):
            out.append(f"<tr>{rows([label, n, cat])}</tr>")
        out.append("</table>")
    out.append("<h2>Input list</h2>")
    if info["inputs"]:
        out.append("<table><tr><th>#</th><th>Source</th><th>Type</th><th>Notes</th></tr>")
        for i, (name, kind, note) in enumerate(info["inputs"], 1):
            out.append(f"<tr>{rows([i, name, kind, note])}</tr>")
        out.append("</table>")
    else:
        out.append("<p>No microphones or playback tracks in the model.</p>")
    out.append("<h2>Lighting</h2>")
    fx = info["fixtures"]
    if fx:
        out.append("<table><tr><th>Fixture</th><th>Type</th><th>Universe</th><th>Address</th><th>Channels</th></tr>")
        for o in fx:
            f = o.sketch_fixture
            out.append(f"<tr>{rows([o.name, lights.FIXTURES[f.kind][0], f.universe, f.address, lights.FIXTURES[f.kind][1]])}</tr>")
        out.append("</table><p class='meta'>Control: Art-Net (Sketch) or the house console.</p>")
    else:
        n = sum(1 for o in scene.objects if o.type == "LIGHT")
        out.append(f"<p>{n} light(s) in the model, not yet patched on DMX.</p>")
    if info["cues"]:
        out.append("<table><tr><th>Cue</th><th>Name</th><th>Fade</th></tr>")
        for c in info["cues"]:
            out.append(f"<tr>{rows([f'{c.number:g}', c.name, f'{c.fade:.1f} s'])}</tr>")
        out.append("</table>")
    out.append("<h2>Power</h2>")
    out.append(f"<p>About {info['power'] / 1000:.1f} kW for lighting and sound "
               f"({info['power'] / 230:.0f} A at 230 V), plus 2 × 16 A at the stage for backline.</p>")
    if r.notes:
        out.append("<h2>Notes</h2><p>" + esc(r.notes).replace("\n", "<br>") + "</p>")
    out.append("<p class='meta' style='margin-top:40px'>Made with Sketch · Laurence Who Studio</p></body></html>")
    return "".join(out)


class SketchRider(bpy.types.PropertyGroup):
    show: StringProperty(name="Show", default="")
    artist: StringProperty(name="Artist", default="Laurence Who")
    contact: StringProperty(name="Contact", default="")
    notes: StringProperty(name="Notes", default="")


class SKETCH_OT_rider(bpy.types.Operator):
    """Make the technical rider from the model and open it (print or save as PDF from the browser)"""
    bl_idname = "sketch.technical_rider"
    bl_label = "Technical rider"

    open: bpy.props.BoolProperty(default=True, options={"SKIP_SAVE"})

    def execute(self, context):
        page = build_html(context.scene)
        base = context.scene.sketch_rider.show or os.path.splitext(os.path.basename(bpy.data.filepath))[0] or "Rider"
        safe = "".join(c for c in base if c not in '\\/:*?"<>|').strip() or "Rider"
        path = os.path.join(acoustics.studio_folder("Riders"), f"{safe} - technical rider.html")
        with open(path, "w", encoding="utf-8") as f:
            f.write(page)
        if self.open and not bpy.app.background:
            webbrowser.open("file:///" + path.replace("\\", "/"))
        self.report({"INFO"}, f"Saved {path}")
        return {"FINISHED"}


classes = (SketchRider, SKETCH_OT_rider)


def register_props():
    bpy.types.Scene.sketch_rider = bpy.props.PointerProperty(type=SketchRider)


def unregister_props():
    del bpy.types.Scene.sketch_rider


def draw_panel(layout, context):
    r = context.scene.sketch_rider
    col = layout.column(align=True)
    col.prop(r, "show")
    col.prop(r, "artist")
    col.prop(r, "contact")
    col.prop(r, "notes")
    layout.operator("sketch.technical_rider", icon="DOCUMENTS")
