# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Self-checking tests of Sketch's SketchUp geometry and inference (not part of the UI).

Run inside Blender (the CI does):
    blender --background --factory-startup --python sketch/sketch-tests.py
Each test prints PASS/FAIL; run() returns the number of failures.
"""

import math
import traceback

import bmesh
import bpy
from mathutils import Matrix, Vector

from . import sk_geom as sg
from . import sk_infer as si

TESTS = []


def test(fn):
    TESTS.append(fn)
    return fn


def check(cond, msg):
    if not cond:
        raise AssertionError(msg)


def close(a, b, eps=1e-4):
    return (Vector(a) - Vector(b)).length < eps


def first(seq):
    return next(iter(seq))


def new_bm():
    bm = bmesh.new()
    sg.soft_layer(bm)
    return bm


def square(bm, size=1.0, z=0.0, x0=0.0, y0=0.0):
    return sg.add_edges(bm, [(x0, y0, z), (x0 + size, y0, z), (x0 + size, y0 + size, z), (x0, y0 + size, z)],
                        closed=True)


def n_faces(bm):
    """SketchUp faces (Blender faces joined by hidden seams count as one)."""
    return len(sg.regions(bm))


def region_areas(bm):
    return sorted(round(sg.region_area(bm, r), 6) for r in sg.regions(bm))


def bbox(bm):
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    zs = [v.co.z for v in bm.verts]
    return Vector((max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)))


def face_containing(bm, p, normal=None):
    return sg.face_at(bm, Vector(p), normal)


# ------------------------------------------------------------------ Line

@test
def closed_square_of_lines_makes_a_face():
    bm = new_bm()
    pts = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
    made = 0
    for a, b in zip(pts, pts[1:] + pts[:1]):
        made += len(sg.add_edges(bm, [a, b]).faces)
    check(len(bm.edges) == 4, f"4 edges, got {len(bm.edges)}")
    check(n_faces(bm) == 1 and made == 1, f"1 face, got {n_faces(bm)} (made {made})")
    check(region_areas(bm) == [1.0], f"area 1, got {region_areas(bm)}")


@test
def open_chain_makes_no_face():
    bm = new_bm()
    sg.add_edges(bm, [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)])
    check(len(bm.edges) == 3 and n_faces(bm) == 0, f"3 edges and no face, got {len(bm.edges)}, {n_faces(bm)}")


@test
def vertical_loop_makes_a_face():
    bm = new_bm()
    sg.add_edges(bm, [(0, 0, 0), (2, 0, 0), (2, 0, 1), (0, 0, 1)], closed=True)
    check(n_faces(bm) == 1, f"1 face, got {n_faces(bm)}")
    f = first(bm.faces)
    f.normal_update()
    check(abs(abs(f.normal.y) - 1) < 1e-6, f"face in the XZ plane, normal {tuple(f.normal)}")


@test
def line_across_face_splits_it():
    bm = new_bm()
    square(bm)
    res = sg.add_edges(bm, [(0.5, 0, 0), (0.5, 1, 0)])
    check(n_faces(bm) == 2, f"2 faces, got {n_faces(bm)}")
    check(region_areas(bm) == [0.5, 0.5], f"two halves, got {region_areas(bm)}")
    check(len(bm.edges) == 7, f"7 edges (2 sides split), got {len(bm.edges)}")
    check(len(res.edges) == 1, f"one new edge, got {len(res.edges)}")


@test
def line_inside_face_not_touching_its_edges_does_not_split():
    bm = new_bm()
    square(bm, 2)
    sg.add_edges(bm, [(0.5, 0.5, 0), (1.5, 0.5, 0)])
    check(n_faces(bm) == 1, f"still 1 face, got {n_faces(bm)}")
    check(len(bm.edges) == 5, f"edge lies on the face, got {len(bm.edges)} edges")


@test
def line_ending_on_edge_splits_the_edge():
    bm = new_bm()
    sg.add_edges(bm, [(0, 0, 0), (2, 0, 0)])
    sg.add_edges(bm, [(1, 1, 0), (1, 0, 0)])
    check(len(bm.edges) == 3, f"edge split in two + new edge = 3, got {len(bm.edges)}")
    check(len(bm.verts) == 4, f"4 vertices, got {len(bm.verts)}")
    mid = [v for v in bm.verts if close(v.co, (1, 0, 0))]
    check(len(mid) == 1 and len(mid[0].link_edges) == 3, "the new line ends on the split point")


@test
def crossing_lines_split_each_other():
    bm = new_bm()
    sg.add_edges(bm, [(0, 0, 0), (2, 0, 0)])
    sg.add_edges(bm, [(1, -1, 0), (1, 1, 0)])
    check(len(bm.verts) == 5 and len(bm.edges) == 4, f"5 verts / 4 edges, got {len(bm.verts)} / {len(bm.edges)}")


@test
def coincident_endpoints_weld():
    bm = new_bm()
    sg.add_edges(bm, [(0, 0, 0), (1, 0, 0)])
    sg.add_edges(bm, [(1, 0, 0.00001), (1, 1, 0)])
    check(len(bm.verts) == 3, f"endpoints within 0.1 mm are one point, got {len(bm.verts)} verts")


# ------------------------------------------------------------------ Rectangle, Circle, Arc

@test
def rectangle_on_face_splits_it():
    bm = new_bm()
    square(bm, 2)
    pts = sg.rectangle_points((0.5, 0.5, 0), (1.5, 1.5, 0), (0, 0, 1))
    res = sg.add_edges(bm, pts, closed=True)
    check(n_faces(bm) == 2, f"inner + outer face, got {n_faces(bm)}")
    check(region_areas(bm) == [1.0, 3.0], f"areas 1 and 3, got {region_areas(bm)}")
    inner = face_containing(bm, (1, 1, 0))
    check(inner is not None and len(inner.verts) == 4, "the inner face is the rectangle")
    check(len(res.faces) >= 1, "the rectangle reports a new face")
    # the outer face has a hole: the hidden seams are not drawn
    lay = sg.soft_layer(bm)
    hard = [e for e in bm.edges if e[lay] == sg.HARD]
    check(len(hard) == 8, f"8 visible edges, got {len(hard)}")


@test
def rectangle_across_face_edge():
    bm = new_bm()
    square(bm, 2)
    # a rectangle sticking out of the face: the face gets split where they overlap
    sg.add_edges(bm, sg.rectangle_points((1, 1, 0), (3, 3, 0), (0, 0, 1)), closed=True)
    check(n_faces(bm) == 3, f"3 faces (overlap, rest of the square, rest of the rectangle), got {n_faces(bm)}")
    check(region_areas(bm) == [1.0, 3.0, 3.0], f"areas, got {region_areas(bm)}")


@test
def circle_makes_a_face():
    bm = new_bm()
    pts = sg.circle_points((0, 0, 0), 1.0, (0, 0, 1), 24)
    sg.add_edges(bm, pts, closed=True)
    check(n_faces(bm) == 1 and len(first(bm.faces).verts) == 24, "one 24-sided face")
    check(all(abs(v.co.length - 1) < 1e-6 for v in bm.verts), "on the radius")
    c = si.circle_center([v.co for v in first(bm.faces).verts])
    check(c is not None and c.length < 1e-6, f"centre inferred at the origin, got {c}")


@test
def two_point_arc():
    pts = sg.arc_points((0, 0, 0), (2, 0, 0), 1.0, (0, 0, 1), 12)
    check(len(pts) == 13, f"13 points, got {len(pts)}")
    check(close(pts[0], (0, 0, 0)) and close(pts[-1], (2, 0, 0)), "ends at the two points")
    check(close(pts[6], (1, 1, 0)), f"half circle bulges to (1,1,0), got {tuple(pts[6])}")
    check(all(abs((p - Vector((1, 0, 0))).length - 1) < 1e-6 for p in pts), "all on radius 1")
    small = sg.arc_points((0, 0, 0), (2, 0, 0), 0.25, (0, 0, 1), 8)
    check(close(small[4], (1, 0.25, 0)), f"bulge 0.25 at the middle, got {tuple(small[4])}")
    big = sg.arc_points((0, 0, 0), (2, 0, 0), 1.5, (0, 0, 1), 16)
    check(close(big[8], (1, 1.5, 0)), f"bulge 1.5 (more than half a circle), got {tuple(big[8])}")
    check(abs(sg.arc_bulge_from_point((0, 0, 0), (2, 0, 0), (1, 0.7, 0), (0, 0, 1)) - 0.7) < 1e-6, "bulge from point")


@test
def arc_closing_a_loop_makes_a_face():
    bm = new_bm()
    sg.add_edges(bm, [(0, 0, 0), (2, 0, 0)])
    sg.add_edges(bm, sg.arc_points((2, 0, 0), (0, 0, 0), 1.0, (0, 0, 1), 12))
    check(n_faces(bm) == 1, f"half disc, got {n_faces(bm)} faces")
    check(abs(region_areas(bm)[0] - math.pi / 2) < 0.05, f"area about pi/2, got {region_areas(bm)}")


# ------------------------------------------------------------------ Push/Pull

def box(bm, size=1.0, height=2.0):
    square(bm, size)
    f = first(bm.faces)
    sg.push_pull_to(bm, f, Vector((0, 0, height)))
    return bm


@test
def push_pull_square_makes_a_box():
    bm = new_bm()
    box(bm, 1.0, 2.0)
    check(len(bm.faces) == 6, f"6 faces, got {len(bm.faces)}")
    check(sg.is_closed(bm), "closed solid")
    check(close(bbox(bm), (1, 1, 2)), f"1 x 1 x 2, got {tuple(bbox(bm))}")
    check(abs(sg.signed_volume(bm) - 2.0) < 1e-6, f"volume 2 with faces outwards, got {sg.signed_volume(bm)}")


@test
def push_pull_top_of_box_moves_it():
    bm = new_bm()
    box(bm, 1.0, 2.0)
    top = face_containing(bm, (0.5, 0.5, 2.0))
    check(top is not None, "top face")
    sg.push_pull_to(bm, top, Vector((0, 0, 1)))
    check(len(bm.faces) == 6, f"still 6 faces (sides stretch), got {len(bm.faces)}")
    check(close(bbox(bm), (1, 1, 3)), f"1 x 1 x 3, got {tuple(bbox(bm))}")
    check(abs(sg.signed_volume(bm) - 3.0) < 1e-6, f"volume 3, got {sg.signed_volume(bm)}")


@test
def push_pull_ctrl_keeps_the_starting_face():
    bm = new_bm()
    box(bm, 1.0, 2.0)
    top = face_containing(bm, (0.5, 0.5, 2.0))
    sg.push_pull(bm, top, 1.0, keep=True)
    check(len(bm.faces) == 11, f"6 + 5 faces, got {len(bm.faces)}")
    check(face_containing(bm, (0.5, 0.5, 2.0)) is not None, "the starting face is still there")
    check(close(bbox(bm), (1, 1, 3)), f"1 x 1 x 3, got {tuple(bbox(bm))}")


@test
def push_pull_inner_face_up_grows_a_block():
    bm = new_bm()
    square(bm, 2)
    slab = first(bm.faces)
    sg.push_pull_to(bm, slab, Vector((0, 0, 0.2)))
    sg.add_edges(bm, sg.rectangle_points((0.5, 0.5, 0.2), (1.5, 1.5, 0.2), (0, 0, 1)), closed=True)
    inner = face_containing(bm, (1, 1, 0.2))
    sg.push_pull_to(bm, inner, Vector((0, 0, 1)))
    check(sg.is_closed(bm), "closed solid")
    check(face_containing(bm, (1, 1, 0.2)) is None, "no face left inside the solid")
    check(face_containing(bm, (1, 1, 1.2)) is not None, "top of the block")
    check(abs(sg.signed_volume(bm) - (0.8 + 1.0)) < 1e-6, f"volume 1.8, got {sg.signed_volume(bm)}")


@test
def push_face_through_slab_cuts_a_hole():
    bm = new_bm()
    square(bm, 2)
    sg.push_pull_to(bm, first(bm.faces), Vector((0, 0, 0.2)))
    check(len(bm.faces) == 6, "slab")
    sg.add_edges(bm, sg.rectangle_points((0.5, 0.5, 0.2), (1.5, 1.5, 0.2), (0, 0, 1)), closed=True)
    inner = face_containing(bm, (1, 1, 0.2))
    check(inner is not None, "rectangle on the slab")
    sg.push_pull_to(bm, inner, Vector((0, 0, -0.2)))
    check(face_containing(bm, (1, 1, 0.2)) is None, "no face over the hole (top)")
    check(face_containing(bm, (1, 1, 0.0)) is None, "no face over the hole (bottom)")
    check(face_containing(bm, (0.25, 0.25, 0.0)) is not None, "the bottom is still there around the hole")
    check(sg.is_closed(bm), "closed solid")
    check(sg.euler_characteristic(bm) == 0, f"a ring (genus 1): V-E+F = 0, got {sg.euler_characteristic(bm)}")
    check(abs(sg.signed_volume(bm) - (0.8 - 0.2)) < 1e-6, f"volume 0.6, got {sg.signed_volume(bm)}")
    check(n_faces(bm) == 10, f"SketchUp faces: 4 outer sides, top, bottom, 4 inner sides = 10, got {n_faces(bm)}")


@test
def push_pull_pocket_then_deeper():
    bm = new_bm()
    square(bm, 2)
    sg.push_pull_to(bm, first(bm.faces), Vector((0, 0, 1)))
    sg.add_edges(bm, sg.rectangle_points((0.5, 0.5, 1), (1.5, 1.5, 1), (0, 0, 1)), closed=True)
    inner = face_containing(bm, (1, 1, 1))
    sg.push_pull_to(bm, inner, Vector((0, 0, -0.3)))
    floor = face_containing(bm, (1, 1, 0.7))
    check(floor is not None, "pocket floor at 0.7")
    check(abs(sg.signed_volume(bm) - (4 - 0.3)) < 1e-6, f"volume 3.7, got {sg.signed_volume(bm)}")
    sg.push_pull_to(bm, floor, Vector((0, 0, -0.2)))   # sides are perpendicular: the floor just moves
    check(face_containing(bm, (1, 1, 0.5)) is not None, "pocket floor at 0.5")
    check(abs(sg.signed_volume(bm) - (4 - 0.5)) < 1e-6, f"volume 3.5, got {sg.signed_volume(bm)}")
    check(sg.is_closed(bm), "closed solid")


# ------------------------------------------------------------------ Offset, Eraser

@test
def offset_face():
    bm = new_bm()
    square(bm, 1)
    sg.offset_face(bm, first(bm.faces), 0.1)
    check(n_faces(bm) == 2, f"2 faces, got {n_faces(bm)}")
    check(region_areas(bm) == [0.36, 0.64], f"inner 0.8 x 0.8, got {region_areas(bm)}")


@test
def erase_edge_between_coplanar_faces_merges_them():
    bm = new_bm()
    square(bm)
    sg.add_edges(bm, [(0.5, 0, 0), (0.5, 1, 0)])
    lay = sg.soft_layer(bm)
    mid = [e for e in bm.edges if close((e.verts[0].co + e.verts[1].co) / 2, (0.5, 0.5, 0))]
    check(len(mid) == 1, "the splitting line")
    sg.erase_edges(bm, mid)
    check(n_faces(bm) == 1, f"one face again, got {n_faces(bm)}")
    check(len([e for e in bm.edges if e[lay] == sg.HARD]) == 4, f"4 edges (split sides healed), got {len(bm.edges)}")


@test
def erase_boundary_edge_removes_the_face():
    bm = new_bm()
    square(bm)
    e = [e for e in bm.edges if close((e.verts[0].co + e.verts[1].co) / 2, (0.5, 0, 0))]
    sg.erase_edges(bm, e)
    check(n_faces(bm) == 0, f"no face, got {n_faces(bm)}")
    check(len(bm.edges) == 3, f"3 edges left, got {len(bm.edges)}")


@test
def erase_soften_keeps_faces():
    bm = new_bm()
    box(bm)
    e = first(bm.edges)
    sg.erase_edges(bm, [e], soften=True)
    check(len(bm.faces) == 6 and sg.soft_layer(bm) is not None and e[sg.soft_layer(bm)] == sg.SOFTENED,
          "softened, faces kept")


# ------------------------------------------------------------------ Move, Rotate, groups

@test
def move_top_face_stretches_the_box():
    bm = new_bm()
    box(bm, 1, 1)
    top = face_containing(bm, (0.5, 0.5, 1))
    sg.move(bm, list(top.verts), Vector((0, 0, 0.5)))
    check(close(bbox(bm), (1, 1, 1.5)) and sg.is_closed(bm), "1 x 1 x 1.5 closed")


@test
def copy_and_extract_group():
    bm = new_bm()
    box(bm, 1, 1)
    nv, ne, nf = sg.duplicate(bm, faces=list(bm.faces))
    sg.move(bm, nv, Vector((3, 0, 0)), do_weld=False)
    check(len(bm.faces) == 12, f"two boxes, got {len(bm.faces)}")
    part = sg.extract(bm, nf, ne)
    check(len(bm.faces) == 6 and len(part.faces) == 6, "the copy left the model for its own mesh")
    check(sg.is_closed(part) and sg.is_closed(bm), "both closed")
    part.free()


@test
def rotate_quarter_turn():
    bm = new_bm()
    sg.add_edges(bm, [(1, 0, 0), (2, 0, 0)])
    sg.rotate(bm, list(bm.verts), (0, 0, 0), (0, 0, 1), math.radians(90))
    check(any(close(v.co, (0, 1, 0)) for v in bm.verts) and any(close(v.co, (0, 2, 0)) for v in bm.verts), "turned")


# ------------------------------------------------------------------ inference

def inference_scene():
    bm = new_bm()
    sg.add_edges(bm, [(0, 0, 0), (2, 0, 0), (2, 1, 0), (0, 1, 0)], closed=True)
    snap = si.Snapshot()
    snap.add_bmesh(bm)
    snap.finalize()
    bm.free()
    view = si.View.look_at((4, -6, 5), (1, 0.5, 0), 1200, 900)
    return snap, view


def at(view, p, dx=0.0, dy=0.0):
    s = view.project(Vector(p))
    return (s.x + dx, s.y + dy)


@test
def view_projection_round_trip():
    _snap, view = inference_scene()
    p = Vector((0.7, 0.3, 0.2))
    o, d = view.ray(at(view, p))
    q = o + d * (p - o).dot(d)
    check((q - p).length < 1e-4, f"ray through the projected point passes the point ({(q - p).length})")


@test
def infer_endpoint():
    snap, view = inference_scene()
    inf = si.infer(view, at(view, (2, 0, 0), 3, 2), snap)
    check(inf.kind == "endpoint" and close(inf.point, (2, 0, 0)), f"Endpoint (2,0,0), got {inf}")
    check(inf.label == "Endpoint" and inf.color == si.GREEN, "green 'Endpoint'")


@test
def infer_midpoint():
    snap, view = inference_scene()
    inf = si.infer(view, at(view, (1, 0, 0), 2, -2), snap)
    check(inf.kind == "midpoint" and close(inf.point, (1, 0, 0)), f"Midpoint (1,0,0), got {inf}")
    check(inf.color == si.CYAN, "cyan")


@test
def infer_on_edge():
    snap, view = inference_scene()
    inf = si.infer(view, at(view, (0.45, 0, 0), 0, 3), snap)
    check(inf.kind == "on_edge", f"On Edge, got {inf}")
    check(abs(inf.point.y) < 1e-6 and abs(inf.point.z) < 1e-6 and abs(inf.point.x - 0.45) < 0.05,
          f"on the edge near x=0.45, got {tuple(inf.point)}")
    check(inf.color == si.RED, "red")


@test
def infer_on_face():
    snap, view = inference_scene()
    inf = si.infer(view, at(view, (1.3, 0.6, 0)), snap)
    check(inf.kind == "on_face" and close(inf.point, (1.3, 0.6, 0), 1e-4), f"On Face (1.3,0.6,0), got {inf}")
    check(inf.color == si.BLUE and inf.normal is not None and abs(abs(inf.normal.z) - 1) < 1e-6, "blue, normal Z")


@test
def infer_ground_plane_fallback():
    snap, view = inference_scene()
    inf = si.infer(view, at(view, (-2, 3, 0)), snap)
    check(inf.kind == "plane" and close(inf.point, (-2, 3, 0), 1e-4), f"ground point, got {inf}")


@test
def infer_axes_from_last_point():
    snap, view = inference_scene()
    inf = si.infer(view, at(view, (2, 1, 1.3)), snap, last=(2, 1, 0))
    check(inf.kind == "axis_z" and close(inf.point, (2, 1, 1.3), 1e-3), f"On Blue Axis, got {inf}")
    check(inf.label == "On Blue Axis", "label")
    inf = si.infer(view, at(view, (-1.2, 1, 0), 0, 4), snap, last=(0, 1, 0))
    check(inf.kind == "axis_x" and abs(inf.point.y - 1) < 1e-6 and abs(inf.point.z) < 1e-6,
          f"On Red Axis through (0,1,0), got {inf}")
    inf = si.infer(view, at(view, (2.0, 2.5, 0), -3, 0), snap, last=(2, 1, 0))
    check(inf.kind == "axis_y" and abs(inf.point.x - 2) < 1e-6, f"On Green Axis, got {inf}")


@test
def infer_parallel_to_edge():
    bm = new_bm()
    sg.add_edges(bm, [(0, 0, 0), (2, 1, 0)])
    snap = si.Snapshot()
    snap.add_bmesh(bm)
    snap.finalize()
    view = si.View.look_at((1, -1, 12), (1, 1, 0), 1200, 900)
    st = si.InferState()
    st.ref_edge = Vector((2, 1, 0)).normalized()
    last = Vector((0, 2, 0))
    target = last + Vector((2, 1, 0)) * 0.8
    inf = si.infer(view, at(view, target, 2, -2), snap, last=last, state=st)
    check(inf.kind == "parallel" and inf.label == "Parallel to Edge", f"Parallel to Edge, got {inf}")
    d = (inf.point - last).normalized()
    check(abs(d.dot(Vector((2, 1, 0)).normalized())) > 1 - 1e-6, "parallel")
    check(inf.color == si.MAGENTA, "magenta")


@test
def infer_from_point():
    snap, view = inference_scene()
    st = si.InferState()
    st.tracked = [Vector((2, 0, 0))]
    inf = si.infer(view, at(view, (2, -1.5, 0), 2, 1), snap, state=st)
    check(inf.kind == "from_point" and abs(inf.point.x - 2) < 1e-6 and abs(inf.point.z) < 1e-6,
          f"From Point along green, got {inf}")
    check(inf.from_pt is not None and close(inf.from_pt, (2, 0, 0)), "dotted line from the tracked point")
    # with a last point: the red axis from (0,-1.5,0) meets the green line from the tracked point
    inf = si.infer(view, at(view, (2, -1.5, 0), 3, 0), snap, last=(0, -1.5, 0), state=st)
    check(inf.kind == "axis_x" and close(inf.point, (2, -1.5, 0), 1e-6) and inf.from_pt is not None,
          f"On Red Axis from point, got {inf}")


@test
def infer_hover_tracks_points():
    snap, view = inference_scene()
    st = si.InferState()
    inf = si.infer(view, at(view, (2, 1, 0), 2, 2), snap, state=st)
    st.observe(inf, now=10.0)
    st.observe(inf, now=10.5)
    check(len(st.tracked) == 1 and close(st.tracked[0], (2, 1, 0)), "hovering an endpoint tracks it")


@test
def infer_arrow_lock():
    snap, view = inference_scene()
    st = si.InferState()
    st.lock_axis("axis_y")
    inf = si.infer(view, at(view, (1.5, 0.7, 0)), snap, last=(0, 0, 0), state=st)
    check(inf.kind == "axis_y" and abs(inf.point.x) < 1e-6 and abs(inf.point.z) < 1e-6, f"locked to green, got {inf}")
    # a hovered point is projected onto the locked axis
    inf = si.infer(view, at(view, (2, 1, 0), 1, 1), snap, last=(0, 0, 0), state=st)
    check(close(inf.point, (0, 1, 0)), f"endpoint (2,1,0) projected on the green axis = (0,1,0), got {inf}")
    st.lock_axis("axis_y")
    check(st.lock is None, "pressing the same arrow again unlocks")


@test
def infer_shift_lock_on_face():
    snap, view = inference_scene()
    st = si.InferState()
    inf = si.infer(view, at(view, (1.0, 0.5, 0)), snap, state=st)
    st.lock_current(inf, None)
    check(st.lock is not None and st.lock[0] == "plane", "Shift locks the face's plane")
    inf = si.infer(view, at(view, (-3, 4, 0)), snap, state=st)
    check(abs(inf.point.z) < 1e-6, f"stays on the plane, got {inf}")


@test
def infer_hidden_points_are_skipped():
    bm = new_bm()
    box(bm, 1, 1)
    snap = si.Snapshot()
    snap.add_bmesh(bm)
    snap.finalize()
    view = si.View.look_at((0.5, -5, 0.5), (0.5, 0.5, 0.5), 1000, 800)
    # (0.5,1,0.5) lies behind the front face (y=0): looking at it hits the front face instead
    inf = si.infer(view, at(view, (0.5, 0.5, 0.5)), snap)
    check(inf.kind == "on_face" and abs(inf.point.y) < 1e-6, f"front face, got {inf}")


# ------------------------------------------------------------------ Measurements box

@test
def measurements_box():
    check(si.parse_vcb("2.5") == ("values", [2.5]), "length")
    check(si.parse_vcb("4,3") == ("values", [4.0, 3.0]), "width,depth")
    check(si.parse_vcb("250cm") == ("values", [2.5]), "units")
    check(si.parse_vcb("24s") == ("segments", 24), "segments")
    check(si.parse_vcb("5x") == ("array", 5) and si.parse_vcb("*5") == ("array", 5), "array")
    check(si.parse_vcb("/4") == ("divide", 4), "divide")
    k, v = si.parse_vcb("[1,2,3]")
    check(k == "absolute" and close(v, (1, 2, 3)), "absolute")
    k, v = si.parse_vcb("<0,0,2>")
    check(k == "relative" and close(v, (0, 0, 2)), "relative")
    check(si.parse_vcb("hello") is None, "nonsense")


# ------------------------------------------------------------------ the tools on a real scene (no mouse)

@test
def tools_draw_into_one_model_and_group():
    from . import tools
    ctx = bpy.context
    for obj in list(bpy.data.objects):
        if obj.get("sketch_kind"):
            bpy.data.objects.remove(obj, do_unlink=True)
    tools.action_line(ctx, [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)], closed=True)
    tools.action_line(ctx, [(3, 0, 0), (4, 0, 0), (4, 1, 0), (3, 1, 0)], closed=True)
    model = tools.model_object(ctx, create=False)
    check(model is not None and model["sketch_kind"] == "model", "a Model object")
    check(len(model.data.polygons) == 2, f"both squares in the one model, got {len(model.data.polygons)} faces")
    check(model.data.attributes.get(sg.SOFT) is not None, "edge softness stored on the mesh")
    me = model.data
    fi = [p.index for p in me.polygons if abs(p.center.x - 0.5) < 1e-6][0]
    up = 2.0 if me.polygons[fi].normal.z > 0 else -2.0     # ground faces face down, as in SketchUp
    tools.action_push_pull(ctx, model, fi, up)
    zs = [v.co.z for v in model.data.vertices]
    check(abs(max(zs) - 2.0) < 1e-6 and abs(min(zs)) < 1e-6, f"pulled up to 2 m, got {min(zs)}..{max(zs)}")
    check(len(model.data.polygons) == 7, f"box + square, got {len(model.data.polygons)}")
    # select the box (triple click = all connected) and make it a group
    with tools.edit_mesh(model) as bm:
        f = sg.face_at(bm, Vector((0.5, 0.5, 0)))
        if f is None:
            f = [x for x in bm.faces if len(x.verts) == 4 and abs(x.calc_center_median().x - 0.5) < 1e-6][0]
        _v, es, fs = sg.connected(bm, [f])
        for x in list(es) + list(fs):
            x.select = True
    sel = tools.read_selection(ctx)
    check(not sel.empty, "selection read back")
    grp = tools.make_group(ctx, sel)
    check(grp is not None and grp["sketch_kind"] == "group", "a group object")
    check(len(grp.data.polygons) == 6, f"the box is in the group, got {len(grp.data.polygons)}")
    check(len(model.data.polygons) == 1, f"the square stays loose, got {len(model.data.polygons)}")
    # editing inside the group: tools draw into it
    tools.open_context(ctx, grp)
    check(tools.target_object(ctx) is grp, "the open group is the target")
    tools.close_context(ctx)
    check(tools.target_object(ctx) is model, "closed again")
    snap = tools.build_snapshot(ctx)
    check(len(snap.T) > 0 and len(snap.E) > 0, "snapshot of the scene for the inference engine")


class FakeTool:
    """What pick_entity needs from a tool: a view, the mouse and the snapshot."""

    def __init__(self, ctx, view, mouse):
        from . import tools
        self.snap = tools.build_snapshot(ctx)
        self._view = view
        self.mouse = Vector(mouse)

    def view(self):
        return self._view

    def refresh(self, ctx):
        from . import tools
        self.snap = tools.build_snapshot(ctx)


@test
def tools_select_move_copy_array_erase():
    from . import tools
    ctx = bpy.context
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    tools.action_line(ctx, [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)], closed=True)
    model = tools.model_object(ctx, create=False)
    fi = 0
    tools.action_push_pull(ctx, model, fi, 1.0 if model.data.polygons[fi].normal.z > 0 else -1.0)
    check(len(model.data.polygons) == 6, "a 1 m cube")
    view = si.View.look_at((3, -4, 5), (0.5, 0.5, 0.5), 1200, 900)
    # click on the top face: Select picks it; double-click: the face and its edges
    tool = FakeTool(ctx, view, at(view, (0.5, 0.5, 1.0)))
    ent = tools.pick_entity(tool, ctx)
    check(ent is not None and ent[0] == "face" and ent[1] is model, f"picked the top face, got {ent}")
    check(abs(model.data.polygons[ent[2]].center.z - 1) < 1e-6, "it is the top")
    tools.select_entity(ctx, ent, "set", "bounded")
    sel = tools.read_selection(ctx)
    f, e, _v = sel.items[model.name]
    check(len(f) == 1 and len(e) == 4, f"face + 4 edges, got {len(f)} faces {len(e)} edges")
    # click on an edge picks the edge
    tool = FakeTool(ctx, view, at(view, (0.5, 0.0, 1.0), 0, 1))
    ent_e = tools.pick_entity(tool, ctx)
    check(ent_e is not None and ent_e[0] == "edge", f"picked an edge, got {ent_e}")
    # Move the top face up: the box stretches
    tools.transform_selection(ctx, sel, Matrix.Translation((0, 0, 0.5)))
    zs = [v.co.z for v in model.data.vertices]
    check(abs(max(zs) - 1.5) < 1e-6 and len(model.data.polygons) == 6, f"stretched to 1.5 m, got {max(zs)}")
    # triple click: everything connected; Ctrl+Move copies it, then 3x makes an array
    tools.select_entity(ctx, ent, "set", "connected")
    sel = tools.read_selection(ctx)
    tools.transform_selection(ctx, sel, Matrix.Translation((2, 0, 0)), copy=True)
    check(len(model.data.polygons) == 12, f"copied, got {len(model.data.polygons)}")
    tools.copy_selection_array(ctx, sel, [Matrix.Translation((2 * k, 0, 0)) for k in (2, 3)])
    check(len(model.data.polygons) == 24, f"array of 4 boxes, got {len(model.data.polygons)}")
    xs = [v.co.x for v in model.data.vertices]
    check(abs(max(xs) - 7) < 1e-6, f"last box ends at x=7, got {max(xs)}")
    # Rotate a copy a quarter turn about the blue axis
    sel = tools.read_selection(ctx)
    rot = Matrix.Translation((0, 0, 0)) @ Matrix.Rotation(math.radians(90), 4, "Z")
    tools.transform_selection(ctx, sel, rot)
    check(any(v.co.y > 5.9 for v in model.data.vertices), "the selected box turned onto the green axis")
    # Delete erases the selection
    tools.erase_selection(ctx)
    check(len(model.data.polygons) == 18, f"one box erased, got {len(model.data.polygons)}")
    # the edge softness survives the round trips through the mesh
    check(model.data.attributes.get(sg.SOFT) is not None, "soft layer kept")


def run():
    passed, failed = 0, 0
    for fn in TESTS:
        try:
            fn()
            print(f"PASS  {fn.__name__}")
            passed += 1
        except Exception as exc:
            print(f"FAIL  {fn.__name__}: {exc}")
            traceback.print_exc()
            failed += 1
    print(f"SKETCH TESTS: {passed} passed, {failed} failed")
    if failed == 0:
        print("SKETCH TESTS PASSED")
    return failed
