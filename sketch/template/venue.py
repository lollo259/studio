# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Venue generators: halls, chapels, stages, seating, pianos, ensembles.

Convention: the audience looks towards +Y. Every generator builds at the
3D cursor, so pieces can be placed by moving the cursor first.
"""

import math
import bmesh
import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, IntProperty
from mathutils import Matrix, Vector

from . import geometry as g


def cursor(context):
    return Vector(context.scene.cursor.location)


# ------------------------------------------------------------------ hall

def build_hall(context, width, length, height, wall, ceiling, origin, name="Hall"):
    col = g.sketch_collection(context, name)
    ox, oy, oz = origin
    g.box(f"{name} floor", (width, length, 0.05), (ox, oy + length / 2, oz - 0.025), col, "floor")
    walls = bmesh.new()
    half_w = width / 2 + wall / 2
    g.add_box(walls, (wall, length + 2 * wall, height), (-half_w, length / 2, height / 2))
    g.add_box(walls, (wall, length + 2 * wall, height), (half_w, length / 2, height / 2))
    g.add_box(walls, (width, wall, height), (0, -wall / 2, height / 2))
    g.add_box(walls, (width, wall, height), (0, length + wall / 2, height / 2))
    g.mesh_object(f"{name} walls", walls, col, "wall", location=(ox, oy, oz))
    if ceiling:
        g.box(f"{name} ceiling", (width + 2 * wall, length + 2 * wall, 0.1),
              (ox, oy + length / 2, oz + height + 0.05), col, "wall")
    return col


class SKETCH_OT_hall(bpy.types.Operator):
    """Add a rectangular hall: floor, four walls and an optional ceiling"""
    bl_idname = "sketch.add_hall"
    bl_label = "Hall"
    bl_options = {"REGISTER", "UNDO"}

    width: FloatProperty(name="Width", default=14.0, min=1.0, unit="LENGTH")
    length: FloatProperty(name="Length", default=24.0, min=1.0, unit="LENGTH")
    height: FloatProperty(name="Height", default=8.0, min=1.0, unit="LENGTH")
    wall: FloatProperty(name="Wall thickness", default=0.4, min=0.05, unit="LENGTH")
    ceiling: BoolProperty(name="Ceiling", default=False,
                          description="Off by default so you can look in from above")

    def execute(self, context):
        build_hall(context, self.width, self.length, self.height, self.wall, self.ceiling, cursor(context))
        return {"FINISHED"}


# ------------------------------------------------------------------ chapel

class SKETCH_OT_chapel(bpy.types.Operator):
    """Add a small single-nave church: walls, gable roof and a round apse"""
    bl_idname = "sketch.add_chapel"
    bl_label = "Chapel"
    bl_options = {"REGISTER", "UNDO"}

    width: FloatProperty(name="Width", default=6.0, min=2.0, unit="LENGTH")
    length: FloatProperty(name="Length", default=12.0, min=2.0, unit="LENGTH")
    height: FloatProperty(name="Wall height", default=5.0, min=2.0, unit="LENGTH")
    pitch: FloatProperty(name="Roof pitch", default=math.radians(30), min=math.radians(5),
                         max=math.radians(60), subtype="ANGLE")
    wall: FloatProperty(name="Wall thickness", default=0.6, min=0.1, unit="LENGTH")
    apse: BoolProperty(name="Apse", default=True)
    roof: BoolProperty(name="Roof", default=True)

    def execute(self, context):
        o = cursor(context)
        col = g.sketch_collection(context, "Chapel")
        W, L, H, T = self.width, self.length, self.height, self.wall
        g.box("Chapel floor", (W, L, 0.05), (o.x, o.y + L / 2, o.z - 0.025), col, "stone")
        walls = bmesh.new()
        g.add_box(walls, (T, L + T, H), (-(W + T) / 2, L / 2 - T / 2, H / 2))
        g.add_box(walls, (T, L + T, H), ((W + T) / 2, L / 2 - T / 2, H / 2))
        g.add_box(walls, (W + 2 * T, T, H), (0, -T / 2, H / 2))  # entrance wall
        if not self.apse:
            g.add_box(walls, (W + 2 * T, T, H), (0, L + T / 2, H / 2))
        else:
            # front wall with an opening for the apse
            side = T * 1.5
            g.add_box(walls, (side, T, H), (-(W / 2 + T - side / 2), L + T / 2, H / 2))
            g.add_box(walls, (side, T, H), ((W / 2 + T - side / 2), L + T / 2, H / 2))
            # half-cylinder apse shell
            r_out = W / 2 + T - side
            segs = 24
            outline_out = [(r_out * math.cos(math.pi * i / segs), L + T + r_out * math.sin(math.pi * i / segs))
                           for i in range(segs + 1)]
            r_in = max(r_out - T, 0.2)
            outline_in = [(r_in * math.cos(math.pi * i / segs), L + T + r_in * math.sin(math.pi * i / segs))
                          for i in range(segs, -1, -1)]
            g.add_prism(walls, outline_out + outline_in, 0.0, H * 0.8)
        g.mesh_object("Chapel walls", walls, col, "stone", location=o)
        if self.roof:
            span = W / 2 + T + 0.3
            rise = span * math.tan(self.pitch)
            roof = bmesh.new()
            outline = [(-span, H), (span, H), (0.0, H + rise)]
            # build the gable as a prism along Y (outline in XZ plane)
            verts = [roof.verts.new((x, -T - 0.3, z)) for x, z in outline]
            face = roof.faces.new(verts)
            res = bmesh.ops.extrude_face_region(roof, geom=[face])
            moved = [e for e in res["geom"] if isinstance(e, bmesh.types.BMVert)]
            bmesh.ops.translate(roof, verts=moved, vec=(0, L + 2 * T + 0.6, 0))
            bmesh.ops.recalc_face_normals(roof, faces=roof.faces[:])
            g.mesh_object("Chapel roof", roof, col, "roof", location=o)
        return {"FINISHED"}


# ------------------------------------------------------------------ stage

def build_stage(context, width, depth, height, origin, steps=True):
    col = g.sketch_collection(context, "Stage")
    ox, oy, oz = origin
    stage = g.box("Stage", (width, depth, height), (ox, oy + depth / 2, oz + height / 2), col, "stage")
    if steps and height > 0.12:
        n = max(1, round(height / 0.17))
        step_h = height / n
        st = bmesh.new()
        for i in range(n):
            g.add_box(st, (1.2, 0.3, step_h * (i + 1)), (0, -0.3 * (n - i) + 0.15, step_h * (i + 1) / 2))
        g.mesh_object("Stage steps", st, col, "stage", location=(ox + width / 2 - 0.8, oy, oz))
    return stage


class SKETCH_OT_stage(bpy.types.Operator):
    """Add a stage platform. Its front edge sits at the 3D cursor"""
    bl_idname = "sketch.add_stage"
    bl_label = "Stage"
    bl_options = {"REGISTER", "UNDO"}

    width: FloatProperty(name="Width", default=10.0, min=1.0, unit="LENGTH")
    depth: FloatProperty(name="Depth", default=6.0, min=1.0, unit="LENGTH")
    height: FloatProperty(name="Height", default=0.8, min=0.0, unit="LENGTH")
    steps: BoolProperty(name="Steps", default=True)

    def execute(self, context):
        build_stage(context, self.width, self.depth, self.height, cursor(context), self.steps)
        return {"FINISHED"}


# ------------------------------------------------------------------ seating

def seat_mesh():
    me = bpy.data.meshes.get("Sketch seat")
    if me is not None:
        return me
    bm = bmesh.new()
    g.add_box(bm, (0.50, 0.48, 0.08), (0, 0, 0.44))        # seat
    g.add_box(bm, (0.50, 0.07, 0.48), (0, -0.22, 0.70))    # back
    g.add_box(bm, (0.05, 0.45, 0.62), (-0.26, -0.02, 0.31))  # arms / sides
    g.add_box(bm, (0.05, 0.45, 0.62), (0.26, -0.02, 0.31))
    me = bpy.data.meshes.new("Sketch seat")
    bm.to_mesh(me)
    bm.free()
    me.materials.append(g.mat("seat"))
    return me


def build_seating(context, rows, per_row, seat_w, pitch, rise, aisle, curve, origin):
    """Front row at origin, rows going back (-Y), everyone facing +Y."""
    col = g.sketch_collection(context, "Audience")
    me = seat_mesh()
    ox, oy, oz = origin
    risers = bmesh.new()
    count = 0
    for r in range(rows):
        y = -r * pitch
        z = r * rise
        n = per_row
        row_w = n * seat_w + aisle
        for s in range(n):
            x = -row_w / 2 + seat_w / 2 + s * seat_w
            if aisle > 0 and s >= n // 2:
                x += aisle
            rot = 0.0
            yy = y
            if curve > 0:
                # rows bend on circles centred `curve` metres in front of the first row
                radius = curve + r * pitch
                angle = x / radius
                x, yy = radius * math.sin(angle), curve - radius * math.cos(angle)
                rot = angle  # turn each seat towards the centre
            g.instance(f"Seat R{r + 1:02d} S{s + 1:02d}", me, col, (ox + x, oy + yy, oz + z), rot)
            count += 1
        if rise > 0 and r > 0:
            g.add_box(risers, (row_w + 1.0, pitch, z), (0, y, z / 2))
    if len(risers.verts):
        g.mesh_object("Audience risers", risers, col, "floor", location=origin)
    else:
        risers.free()
    return count


class SKETCH_OT_seating(bpy.types.Operator):
    """Add audience seating facing +Y. The front row sits at the 3D cursor"""
    bl_idname = "sketch.add_seating"
    bl_label = "Audience seating"
    bl_options = {"REGISTER", "UNDO"}

    rows: IntProperty(name="Rows", default=12, min=1, max=80)
    per_row: IntProperty(name="Seats per row", default=16, min=1, max=80)
    seat_w: FloatProperty(name="Seat width", default=0.55, min=0.4, unit="LENGTH")
    pitch: FloatProperty(name="Row spacing", default=0.95, min=0.6, unit="LENGTH")
    rise: FloatProperty(name="Rise per row", default=0.12, min=0.0, unit="LENGTH",
                        description="Raked seating: how much each row is higher than the one in front")
    aisle: FloatProperty(name="Centre aisle", default=1.2, min=0.0, unit="LENGTH")
    curve: FloatProperty(name="Curve radius", default=0.0, min=0.0, unit="LENGTH",
                         description="0 = straight rows; otherwise rows bend around the stage")

    def execute(self, context):
        n = build_seating(context, self.rows, self.per_row, self.seat_w, self.pitch,
                          self.rise, self.aisle, self.curve, cursor(context))
        self.report({"INFO"}, f"{n} seats")
        return {"FINISHED"}


# ------------------------------------------------------------------ piano

PIANOS = {
    "CONCERT": (2.74, 1.57),
    "SEMI": (2.11, 1.52),
    "BABY": (1.55, 1.48),
}


def grand_outline(L, W, samples=14):
    """Plan of a grand piano. Keyboard edge at x=0, bass (straight) side at y=W."""
    pts = [(0.0, 0.0), (0.0, W), (L * 0.97, W), (L, W - 0.12), (L * 0.985, W - 0.30)]
    p0 = Vector((L * 0.93, W - 0.42))
    c = Vector((L * 0.60, W * 0.55))
    p2 = Vector((L * 0.30, 0.0))
    for i in range(samples + 1):
        t = i / samples
        p = (1 - t) ** 2 * p0 + 2 * (1 - t) * t * c + t ** 2 * p2
        pts.append((p.x, p.y))
    return pts


def build_piano(context, model, lid_open, origin, rot_z, bench=True):
    col = g.sketch_collection(context, "Instruments")
    if model == "UPRIGHT":
        bm = bmesh.new()
        g.add_box(bm, (1.50, 0.60, 1.30), (0, 0.30, 0.65))
        g.add_box(bm, (1.25, 0.22, 0.06), (0, -0.11, 0.72))
        piano = g.mesh_object("Upright piano", bm, col, "piano", location=origin)
        piano.rotation_euler.z = rot_z
        return piano

    L, W = PIANOS[model]
    outline = grand_outline(L, W)
    body = bmesh.new()
    g.add_prism(body, outline, 0.62, 0.99)
    for (lx, ly) in ((0.28, 0.18), (0.28, W - 0.18), (L * 0.80, W - 0.28)):
        g.add_cylinder(body, 0.05, 0.62, (lx, ly, 0.31))
    g.add_box(body, (0.10, 0.10, 0.62), (L * 0.55, W * 0.5, 0.31))  # lyre/pedals
    piano = g.mesh_object(f"Grand piano {L:.2f} m", body, col, "piano", location=origin)
    piano.rotation_euler.z = rot_z

    keys = bmesh.new()
    g.add_box(keys, (0.17, W - 0.24, 0.04), (-0.085, W / 2, 0.72))
    k = g.mesh_object("Keyboard", keys, col, "keys")
    k.parent = piano

    lid = bmesh.new()
    g.add_prism(lid, [(x, y - W) for x, y in outline], 0.0, 0.02)
    lid_obj = g.mesh_object("Lid", lid, col, "piano")
    lid_obj.parent = piano
    lid_obj.location = (0.0, W, 0.99)
    if lid_open:
        lid_obj.rotation_euler.x = -math.radians(45)

    if bench:
        bmb = bmesh.new()
        g.add_box(bmb, (0.38, 0.75, 0.48), (-0.62, W * 0.45, 0.24))
        bobj = g.mesh_object("Piano bench", bmb, col, "piano")
        bobj.parent = piano
    return piano


class SKETCH_OT_piano(bpy.types.Operator):
    """Add a piano at real size. The keyboard faces stage left (-X), the lid opens towards the audience"""
    bl_idname = "sketch.add_piano"
    bl_label = "Piano"
    bl_options = {"REGISTER", "UNDO"}

    model: EnumProperty(name="Model", items=[
        ("CONCERT", "Concert grand (274 cm)", ""),
        ("SEMI", "Semi-concert grand (211 cm)", ""),
        ("BABY", "Baby grand (155 cm)", ""),
        ("UPRIGHT", "Upright", ""),
    ], default="CONCERT")
    lid_open: BoolProperty(name="Lid open", default=True)
    bench: BoolProperty(name="Bench", default=True)
    rotation: FloatProperty(name="Rotation", default=0.0, subtype="ANGLE")

    def execute(self, context):
        build_piano(context, self.model, self.lid_open, cursor(context), self.rotation, self.bench)
        return {"FINISHED"}


# ------------------------------------------------------------------ ensemble

def chair_stand_meshes():
    chair = bpy.data.meshes.get("Sketch chair")
    if chair is None:
        bm = bmesh.new()
        g.add_box(bm, (0.44, 0.44, 0.04), (0, 0, 0.46))
        g.add_box(bm, (0.44, 0.04, 0.42), (0, -0.20, 0.69))
        for sx in (-0.19, 0.19):
            for sy in (-0.19, 0.19):
                g.add_box(bm, (0.03, 0.03, 0.46), (sx, sy, 0.23))
        chair = bpy.data.meshes.new("Sketch chair")
        bm.to_mesh(chair)
        bm.free()
        chair.materials.append(g.mat("seat"))
    stand = bpy.data.meshes.get("Sketch music stand")
    if stand is None:
        bm = bmesh.new()
        g.add_cylinder(bm, 0.012, 1.05, (0, 0, 0.525), segments=8)
        g.add_box(bm, (0.50, 0.02, 0.34), (0, 0, 1.15))
        stand = bpy.data.meshes.new("Sketch music stand")
        bm.to_mesh(stand)
        bm.free()
        stand.materials.append(g.mat("metal"))
    return chair, stand


class SKETCH_OT_ensemble(bpy.types.Operator):
    """Add chairs and music stands for an ensemble, in an arc facing the audience"""
    bl_idname = "sketch.add_ensemble"
    bl_label = "Ensemble chairs"
    bl_options = {"REGISTER", "UNDO"}

    players: IntProperty(name="Players", default=3, min=1, max=60)
    radius: FloatProperty(name="Arc radius", default=2.5, min=0.5, unit="LENGTH")
    spread: FloatProperty(name="Spread", default=math.radians(100), min=0.0,
                          max=math.radians(200), subtype="ANGLE")

    def execute(self, context):
        col = g.sketch_collection(context, "Ensemble")
        chair, stand = chair_stand_meshes()
        o = cursor(context)
        n = self.players
        for i in range(n):
            a = 0.0 if n == 1 else -self.spread / 2 + self.spread * i / (n - 1)
            # arc centred `radius` metres towards the audience (-Y); the ends come forward
            R = self.radius
            pos = Vector((R * math.sin(a), -R + R * math.cos(a), 0.0))
            to_centre = Vector((-math.sin(a), -math.cos(a), 0.0))
            face = math.pi - a
            g.instance(f"Chair {i + 1}", chair, col, o + pos, face)
            g.instance(f"Stand {i + 1}", stand, col, o + pos + to_centre * 0.6, face)
        return {"FINISHED"}


# ------------------------------------------------------------------ figure

class SKETCH_OT_figure(bpy.types.Operator):
    """Add a simple standing figure for scale"""
    bl_idname = "sketch.add_figure"
    bl_label = "Scale figure"
    bl_options = {"REGISTER", "UNDO"}

    height: FloatProperty(name="Height", default=1.75, min=0.5, max=2.5, unit="LENGTH")
    seated: BoolProperty(name="Seated", default=False)

    def execute(self, context):
        col = g.sketch_collection(context, "People")
        h = self.height * (0.72 if self.seated else 1.0)
        bm = bmesh.new()
        head = self.height * 0.13
        g.add_cylinder(bm, self.height * 0.12, h - head * 1.1, (0, 0, (h - head * 1.1) / 2), segments=12)
        bmesh.ops.create_uvsphere(bm, u_segments=12, v_segments=8, radius=head / 2,
                                  matrix=Matrix.Translation((0, 0, h - head / 2)))
        g.mesh_object(f"Figure {self.height * 100:.0f} cm", bm, col, "figure", location=cursor(context))
        return {"FINISHED"}


# ------------------------------------------------------------------ whole venue

class SKETCH_OT_concert_hall(bpy.types.Operator):
    """Build a complete concert setup: hall, stage, raked seating, piano, lights and an audience camera"""
    bl_idname = "sketch.add_concert_hall"
    bl_label = "Concert hall (complete)"
    bl_options = {"REGISTER", "UNDO"}

    width: FloatProperty(name="Hall width", default=16.0, min=6.0, unit="LENGTH")
    length: FloatProperty(name="Hall length", default=26.0, min=8.0, unit="LENGTH")
    height: FloatProperty(name="Hall height", default=9.0, min=3.0, unit="LENGTH")
    stage_depth: FloatProperty(name="Stage depth", default=7.0, min=2.0, unit="LENGTH")
    stage_height: FloatProperty(name="Stage height", default=0.9, min=0.0, unit="LENGTH")
    rows: IntProperty(name="Rows", default=14, min=1, max=60)
    per_row: IntProperty(name="Seats per row", default=20, min=1, max=60)
    rise: FloatProperty(name="Rise per row", default=0.15, min=0.0, unit="LENGTH")
    piano: EnumProperty(name="Piano", items=[
        ("CONCERT", "Concert grand", ""), ("SEMI", "Semi-concert grand", ""),
        ("UPRIGHT", "Upright", ""), ("NONE", "No piano", "")], default="CONCERT")
    lights: BoolProperty(name="Stage lights", default=True)

    def execute(self, context):
        from . import scenography
        o = cursor(context)
        W, L = self.width, self.length
        build_hall(context, W, L, self.height, 0.4, False, o, "Concert hall")
        stage_front = L - self.stage_depth
        build_stage(context, W - 1.0, self.stage_depth, self.stage_height,
                    o + Vector((0, stage_front, 0)), steps=True)
        first_row = stage_front - 2.5
        seats_fit = int((W - 2.0 - 1.2) / 0.55)
        per_row = min(self.per_row, max(1, seats_fit))
        rows_fit = int((first_row - 1.0) / 0.95) + 1
        rows = min(self.rows, max(1, rows_fit))
        build_seating(context, rows, per_row, 0.55, 0.95, self.rise, 1.2, 0.0,
                      o + Vector((0, first_row, 0)))
        centre = o + Vector((0, stage_front + self.stage_depth * 0.45, self.stage_height))
        if self.piano != "NONE":
            L_p = PIANOS.get(self.piano, (1.5, 0.6))[0]
            build_piano(context, self.piano, True, centre + Vector((-L_p / 2, -0.8, 0)), 0.0)
        if self.lights:
            scenography.build_concert_lights(context, centre, W, self.height)
        mid_row = max(1, rows // 2)
        eye = o + Vector((0, first_row - (mid_row - 1) * 0.95, (mid_row - 1) * self.rise + 1.15))
        scenography.audience_camera(context, eye, centre + Vector((0, 0, 1.0)))
        scenography.dark_world(context)
        self.report({"INFO"}, f"{rows} rows x {per_row} seats = {rows * per_row} seats")
        return {"FINISHED"}


classes = (
    SKETCH_OT_hall, SKETCH_OT_chapel, SKETCH_OT_stage, SKETCH_OT_seating, SKETCH_OT_piano,
    SKETCH_OT_ensemble, SKETCH_OT_figure, SKETCH_OT_concert_hall,
)
