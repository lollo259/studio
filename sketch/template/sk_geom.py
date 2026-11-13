# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""SketchUp's geometry model, on bmesh: edges and faces that stick together.

No bpy in here. Every function takes a bmesh (and mathutils values), so the
tools only call these and the tests can drive them without a mouse.

The rules are SketchUp's:
- every edge drawn is split where it meets or crosses another edge, and splits
  the edges it ends on;
- a closed loop of coplanar edges makes a face by itself; a loop inside a face
  splits it into an inner face and an outer face with a hole; a line across a
  face from edge to edge splits it in two;
- Push/Pull extrudes a face (or just moves it when its sides are perpendicular
  to it), and a face pushed onto an opposite face cancels it: a hole.

A face with holes is stored as a few Blender faces joined by hidden seam edges
(SOFT layer = SEAM); one "SketchUp face" = faces connected through seams.
"""

import math
import bmesh
from mathutils import Matrix, Vector, geometry as mgeo

WELD = 1e-4          # points closer than this (0.1 mm) are the same point
PLANE_EPS = 1e-4
SOFT = "sketch_soft"  # edge layer: 0 hard edge, 1 softened by the user, 2 seam inside a face
HARD, SOFTENED, SEAM = 0, 1, 2
X = Vector((1, 0, 0))
Y = Vector((0, 1, 0))
Z = Vector((0, 0, 1))


def soft_layer(bm):
    lay = bm.edges.layers.int.get(SOFT)
    if lay is None:
        lay = bm.edges.layers.int.new(SOFT)
    return lay


def ensure_lookup(bm):
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()
    bm.faces.ensure_lookup_table()


# ------------------------------------------------------------------ planes and 2D helpers

def plane_axes(normal):
    n = Vector(normal).normalized()
    if abs(n.dot(Z)) > 0.999:
        u = X.copy()
    else:
        u = Z.cross(n).normalized()
    v = n.cross(u).normalized()
    return u, v


class Plane:
    """A plane with a canonical normal (first non-zero component positive)."""
    __slots__ = ("co", "no")

    def __init__(self, co, no):
        no = Vector(no).normalized()
        for c in no:
            if abs(c) > 1e-6:
                if c < 0:
                    no = -no
                break
        self.no = no
        self.co = Vector(co)

    def dist(self, p):
        return (Vector(p) - self.co).dot(self.no)

    def has(self, p, eps=PLANE_EPS):
        return abs(self.dist(p)) < eps

    def same(self, other):
        return abs(self.no.dot(other.no)) > 1 - 1e-6 and self.has(other.co)

    def axes(self):
        return plane_axes(self.no)

    def to2(self, p):
        u, v = self.axes()
        d = Vector(p) - self.co
        return (d.dot(u), d.dot(v))

    def __repr__(self):
        return f"Plane({tuple(round(c, 4) for c in self.co)}, {tuple(round(c, 4) for c in self.no)})"


def face_plane(f):
    f.normal_update()
    n = f.normal
    if n.length < 1e-9:
        n = mgeo.normal([v.co for v in f.verts]) if len(f.verts) >= 3 else Z
    return Plane(f.verts[0].co, n)


def poly_area(pts):
    a = 0.0
    n = len(pts)
    for i in range(n):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % n]
        a += x1 * y2 - x2 * y1
    return a / 2


def point_in_poly(p, poly):
    x, y = p
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y):
            xc = xi + (y - yi) * (xj - xi) / (yj - yi)
            if x < xc:
                inside = not inside
        j = i
    return inside


def point_seg_dist2(p, a, b):
    ax, ay = a
    bx, by = b
    px, py = p
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0.0 if L < 1e-18 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    qx, qy = ax + dx * t - px, ay + dy * t - py
    return qx * qx + qy * qy


def point_in_region(p, outer, holes=()):
    return point_in_poly(p, outer) and not any(point_in_poly(p, h) for h in holes)


# ------------------------------------------------------------------ vertices and edges

def find_vert(bm, co, eps=WELD):
    best, bd = None, eps
    for v in bm.verts:
        d = (v.co - co).length
        if d < bd:
            best, bd = v, d
    return best


def split_edge(bm, e, co):
    """Split edge e at point co (on it); returns the new vertex."""
    lay = soft_layer(bm)
    s = e[lay]
    a = e.verts[0]
    L = (e.verts[1].co - a.co).length
    fac = 0.5 if L < 1e-12 else (Vector(co) - a.co).length / L
    ne, nv = bmesh.utils.edge_split(e, a, min(max(fac, 1e-6), 1 - 1e-6))
    nv.co = co
    ne[lay] = s
    e[lay] = s
    return nv


def ensure_vert(bm, co):
    """The vertex at co: an existing one, a new one splitting the edge co lies on, or a new loose one."""
    co = Vector(co)
    v = find_vert(bm, co)
    if v is not None:
        return v
    for e in bm.edges:
        a, b = e.verts[0].co, e.verts[1].co
        ab = b - a
        L2 = ab.length_squared
        if L2 < 1e-12:
            continue
        t = (co - a).dot(ab) / L2
        if 0 < t < 1 and (a + ab * t - co).length < WELD:
            return split_edge(bm, e, co)
    return bm.verts.new(co)


def add_segment(bm, a, b):
    """One edge from a to b, split wherever it meets existing vertices or crosses existing edges.

    Returns the list of edges along a-b (new or existing ones)."""
    lay = soft_layer(bm)
    va = ensure_vert(bm, a)
    vb = ensure_vert(bm, b)
    if va is vb:
        return []
    a, b = va.co.copy(), vb.co.copy()
    d = b - a
    L = d.length
    cuts = {va: 0.0, vb: 1.0}
    # existing vertices lying on the new segment
    for v in bm.verts:
        if v in cuts:
            continue
        t = (v.co - a).dot(d) / (L * L)
        if WELD / L < t < 1 - WELD / L and (a + d * t - v.co).length < WELD:
            cuts[v] = t
    # existing edges crossing the new segment
    for e in list(bm.edges):
        p, q = e.verts[0], e.verts[1]
        if p in cuts or q in cuts:
            continue
        pq = q.co - p.co
        Lq = pq.length
        if Lq < 1e-12:
            continue
        if abs(pq.normalized().dot(d / L)) > 1 - 1e-9:
            continue  # parallel
        res = mgeo.intersect_line_line(a, b, p.co, q.co)
        if res is None:
            continue
        pa, pe = res
        if (pa - pe).length > WELD:
            continue
        t = (pa - a).dot(d) / (L * L)
        s = (pe - p.co).dot(pq) / (Lq * Lq)
        if WELD / L < t < 1 - WELD / L and WELD / Lq < s < 1 - WELD / Lq:
            nv = split_edge(bm, e, pa)
            cuts[nv] = t
    order = sorted(cuts.items(), key=lambda kv: kv[1])
    out = []
    for (v1, _t1), (v2, _t2) in zip(order, order[1:]):
        if v1 is v2:
            continue
        e = bm.edges.get((v1, v2))
        if e is None:
            e = bm.edges.new((v1, v2))
            e[lay] = HARD
        elif e[lay] == SEAM:
            e[lay] = HARD  # drawing over a hidden seam makes it a real edge
        out.append(e)
    return out


def dedupe_planes(planes):
    out = []
    for p in planes:
        if not any(p.same(q) for q in out):
            out.append(p)
    return out


def candidate_planes(bm, edges):
    """Planes where the given edges may close or split faces."""
    planes = []
    for e in edges:
        if not e.is_valid:
            continue
        va, vb = e.verts
        a, b = va.co, vb.co
        d = b - a
        if d.length < WELD:
            continue
        for f in bm.faces:
            pl = face_plane(f)
            if pl.has(a) and pl.has(b):
                planes.append(pl)
        for v in (va, vb):
            for e2 in v.link_edges:
                if e2 is e:
                    continue
                o = e2.other_vert(v).co
                n = d.cross(o - a)
                if n.length > 1e-9 * max(1.0, d.length * (o - a).length):
                    planes.append(Plane(a, n))
    return dedupe_planes(planes)


# ------------------------------------------------------------------ "SketchUp faces": faces joined by seams

def region_of(bm, f):
    """SketchUp face containing Blender face f."""
    lay = soft_layer(bm)
    seen = {f}
    stack = [f]
    while stack:
        x = stack.pop()
        for e in x.edges:
            if e[lay] != SEAM:
                continue
            for g in e.link_faces:
                if g not in seen:
                    seen.add(g)
                    stack.append(g)
    return seen


def regions(bm):
    """Every SketchUp face of the mesh, as a list of sets of Blender faces."""
    out = []
    done = set()
    for f in bm.faces:
        if f in done:
            continue
        r = region_of(bm, f)
        done |= r
        out.append(r)
    return out


def region_boundary_edges(bm, region):
    lay = soft_layer(bm)
    count = {}
    for f in region:
        for e in f.edges:
            count[e] = count.get(e, 0) + 1
    return [e for e, c in count.items() if c == 1 and e[lay] != SEAM]


def region_loops(bm, region):
    """Boundary loops of a SketchUp face, each an ordered list of verts following the faces' winding."""
    region = set(region)
    bedges = set(region_boundary_edges(bm, region))
    nxt = {}
    for f in region:
        for loop in f.loops:
            if loop.edge in bedges:
                nxt[loop.vert] = loop.link_loop_next.vert
    loops = []
    used = set()
    for start in list(nxt):
        if start in used:
            continue
        loop = []
        v = start
        while v not in used and v in nxt:
            used.add(v)
            loop.append(v)
            v = nxt[v]
        if len(loop) >= 3:
            loops.append(loop)
    return loops


def region_area(bm, region):
    return sum(f.calc_area() for f in region)


def face_at(bm, p, normal=None):
    """The Blender face whose plane holds p and whose polygon contains it."""
    p = Vector(p)
    for f in bm.faces:
        pl = face_plane(f)
        if not pl.has(p, 1e-4):
            continue
        if normal is not None and abs(pl.no.dot(Vector(normal).normalized())) < 0.999:
            continue
        poly = [pl.to2(v.co) for v in f.verts]
        if point_in_poly(pl.to2(p), poly):
            return f
    return None


# ------------------------------------------------------------------ rebuilding the faces of one plane

class _Old:
    __slots__ = ("poly", "sign", "mat", "face")

    def __init__(self, poly, sign, mat, face=None):
        self.poly, self.sign, self.mat, self.face = poly, sign, mat, face


def faces_in_plane(bm, plane):
    return [f for f in bm.faces if all(plane.has(v.co) for v in f.verts)]


def record_faces(faces, plane):
    out = []
    for f in faces:
        f.normal_update()
        out.append(_Old([plane.to2(v.co) for v in f.verts], 1 if f.normal.dot(plane.no) >= 0 else -1,
                        f.material_index, f))
    return out


def _interior_point(outer, holes):
    """A point strictly inside the region (outer minus holes), 2D."""
    polys = [[(x, y, 0.0) for x, y in outer]] + [[(x, y, 0.0) for x, y in h] for h in holes]
    flat = [p for poly in polys for p in poly]
    try:
        tris = mgeo.tessellate_polygon(polys)
    except Exception:
        tris = []
    best, best_a = None, -1.0
    for i, j, k in tris:
        a, b, c = flat[i], flat[j], flat[k]
        area = abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) / 2
        if area <= best_a:
            continue
        cen = ((a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3)
        if point_in_region(cen, outer, holes):
            best, best_a = cen, area
    if best is None:
        n = len(outer)
        best = (sum(p[0] for p in outer) / n, sum(p[1] for p in outer) / n)
    return best


def _join_seams(bm, faces, lay):
    """Merge seam-split triangles back into as few faces as possible."""
    faces = set(faces)
    changed = True
    while changed:
        changed = False
        for f in list(faces):
            if not f.is_valid:
                faces.discard(f)
                continue
            for e in f.edges:
                if e[lay] != SEAM or len(e.link_faces) != 2:
                    continue
                f1, f2 = e.link_faces
                if f1 not in faces or f2 not in faces:
                    continue
                if f1.material_index != f2.material_index:
                    continue
                if len(set(f1.verts) & set(f2.verts)) != 2:
                    continue
                try:
                    nf = bmesh.utils.face_join([f1, f2], True)
                except Exception:
                    nf = None
                if nf is not None:
                    faces.discard(f1)
                    faces.discard(f2)
                    faces.add(nf)
                    changed = True
                    break
            if changed:
                break
    return [f for f in faces if f.is_valid]


def rebuild_plane(bm, plane, new_edges=(), olds=None, classify=None, default_sign=1):
    """Recompute every face lying in `plane` from the hard edges in it.

    Regions inside an old face keep its side and material; regions closed by a
    new edge become faces; `classify(point3d, point2d, boundary_edges)` may
    decide instead (returns None for no face or (sign, material_index)).
    Returns the list of new Blender faces."""
    lay = soft_layer(bm)
    u, v = plane.axes()
    co = plane.co

    def to2(p):
        d = p - co
        return (d.dot(u), d.dot(v))

    pf = faces_in_plane(bm, plane)
    if olds is None:
        olds = record_faces(pf, plane)
    if pf:
        bmesh.ops.delete(bm, geom=pf, context="FACES_ONLY")
    for e in [e for e in bm.edges if e[lay] == SEAM and not e.link_faces
              and plane.has(e.verts[0].co) and plane.has(e.verts[1].co)]:
        bm.edges.remove(e)

    hard = [e for e in bm.edges if e[lay] != SEAM and plane.has(e.verts[0].co) and plane.has(e.verts[1].co)
            and (e.verts[0].co - e.verts[1].co).length > WELD * 0.5]
    newset = set(new_edges)
    adj = {}
    for e in hard:
        a, b = e.verts
        adj.setdefault(a, []).append((b, e))
        adj.setdefault(b, []).append((a, e))
    pos = {x: to2(x.co) for x in adj}

    # drop dangling edges (they stay as edges lying on the face)
    alive = set(hard)
    deg = {x: len(n) for x, n in adj.items()}
    stack = [x for x, d in deg.items() if d <= 1]
    while stack:
        x = stack.pop()
        for y, e in adj[x]:
            if e in alive:
                alive.discard(e)
                deg[x] -= 1
                deg[y] -= 1
                if deg[y] == 1:
                    stack.append(y)

    ring, index = {}, {}
    for x, nbrs in adj.items():
        live = [(y, e) for y, e in nbrs if e in alive]
        if not live:
            continue
        px, py = pos[x]
        live.sort(key=lambda ye: math.atan2(pos[ye[0]][1] - py, pos[ye[0]][0] - px))
        ring[x] = live
        index[x] = {y: i for i, (y, _e) in enumerate(live)}

    # components (to tell holes from faces)
    parent = {x: x for x in ring}

    def find(x):
        while parent[x] is not x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for e in alive:
        a, b = e.verts
        ra, rb = find(a), find(b)
        if ra is not rb:
            parent[ra] = rb

    # walk the faces of the planar graph (each bounded face counter-clockwise)
    seen = set()
    cycles = []
    for x, live in ring.items():
        for y, e in live:
            if (x, y) in seen:
                continue
            verts, edges = [], []
            a, b, eab = x, y, e
            guard = 0
            while (a, b) not in seen and guard < 1000000:
                seen.add((a, b))
                verts.append(a)
                edges.append(eab)
                lb = ring[b]
                nb, ne = lb[(index[b][a] - 1) % len(lb)]
                a, b, eab = b, nb, ne
                guard += 1
            poly = [pos[p] for p in verts]
            cycles.append((verts, edges, poly, poly_area(poly)))

    positive = [c for c in cycles if c[3] > 1e-12]
    negative = [c for c in cycles if c[3] < -1e-12]
    holes = {id(c): [] for c in positive}
    for nc in negative:
        p = nc[2][0]
        comp = find(nc[0][0])
        best = None
        for pc in positive:
            if find(pc[0][0]) is comp or pc[3] <= -nc[3]:
                continue
            if point_in_poly(p, pc[2]) and (best is None or pc[3] < best[3]):
                best = pc
        if best is not None:
            holes[id(best)].append(nc)

    hardset = set(hard)
    created = []
    for pc in positive:
        verts, edges, poly, _area = pc
        hs = holes[id(pc)]
        sample = _interior_point(poly, [h[2] for h in hs])
        p3 = co + u * sample[0] + v * sample[1]
        if classify is not None:
            res = classify(p3, sample, edges)
        else:
            res = None
            for o in olds:
                if point_in_poly(sample, o.poly):
                    res = (o.sign, o.mat)
                    break
            if res is None and any(e in newset for e in edges):
                res = (default_sign, 0)
        if res is None:
            continue
        sign, mat = res
        want = plane.no * sign
        faces = []
        if not hs and len(set(verts)) == len(verts):
            f = bm.faces.get(verts)
            if f is None:
                try:
                    f = bm.faces.new(verts)
                except ValueError:
                    f = None
            if f is not None:
                faces = [f]
        else:
            loops = [verts] + [h[0][::-1] for h in hs]
            polys = [[(pos[x][0], pos[x][1], 0.0) for x in loop] for loop in loops]
            flat = [x for loop in loops for x in loop]
            try:
                tris = mgeo.tessellate_polygon(polys)
            except Exception:
                tris = []
            for i, j, k in tris:
                tri = (flat[i], flat[j], flat[k])
                if len(set(tri)) < 3:
                    continue
                f = bm.faces.get(tri)
                if f is None:
                    try:
                        f = bm.faces.new(tri)
                    except ValueError:
                        continue
                faces.append(f)
            for f in faces:
                for e in f.edges:
                    if e not in hardset:
                        e[lay] = SEAM
        for f in faces:
            f.material_index = mat
            f.normal_update()
            if f.normal.dot(want) < 0:
                f.normal_flip()
                f.normal_update()
        if len(faces) > 1:
            faces = _join_seams(bm, faces, lay)
        created += faces
    return created


# ------------------------------------------------------------------ drawing edges

class Result:
    def __init__(self, edges=(), faces=()):
        self.edges = list(edges)
        self.faces = list(faces)


def add_edges(bm, points, closed=False, default_sign=None):
    """Draw a chain of edges through points (SketchUp Line). Faces close and split by themselves."""
    pts = [Vector(p) for p in points]
    if closed and len(pts) > 2:
        pts.append(pts[0])
    new = []
    for a, b in zip(pts, pts[1:]):
        if (a - b).length < WELD:
            continue
        new += add_segment(bm, a, b)
    faces = []
    newset = set(new)
    for pl in candidate_planes(bm, new):
        sign = 1
        if default_sign is not None:
            sign = default_sign(pl)
        elif abs(pl.no.dot(Z)) > 0.999 and abs(pl.co.z) < PLANE_EPS:
            sign = -1  # faces drawn on the ground face down, as in SketchUp
        made = rebuild_plane(bm, pl, newset, default_sign=sign)
        faces += [f for f in made if any(e in newset for e in f.edges)]
    return Result(new, [f for f in faces if f.is_valid])


def heal_verts(bm, verts):
    """Remove vertices left between two collinear hard edges only (SketchUp heals them)."""
    lay = soft_layer(bm)
    for v in list(verts):
        if not v.is_valid or len(v.link_edges) != 2:
            continue
        e1, e2 = v.link_edges
        if e1[lay] != e2[lay]:
            continue
        a = e1.other_vert(v).co - v.co
        b = e2.other_vert(v).co - v.co
        if a.length < 1e-12 or b.length < 1e-12:
            continue
        if a.normalized().dot(b.normalized()) < -1 + 1e-9:
            try:
                bmesh.ops.dissolve_verts(bm, verts=[v])
            except Exception:
                pass


def remove_loose(bm):
    lay = soft_layer(bm)
    for e in [e for e in bm.edges if e[lay] == SEAM and not e.link_faces]:
        bm.edges.remove(e)
    for v in [v for v in bm.verts if not v.link_edges]:
        bm.verts.remove(v)


# ------------------------------------------------------------------ Push/Pull

def _coplanar(f, g):
    f.normal_update()
    g.normal_update()
    return abs(f.normal.dot(g.normal)) > 1 - 1e-6 and face_plane(f).has(g.verts[0].co)


def push_pull(bm, face, distance, keep=False):
    """SketchUp Push/Pull of the face containing `face`, by `distance` along its normal.

    keep (Ctrl) keeps the starting face and always builds new side faces.
    Returns the list of faces of the moved (cap) face."""
    lay = soft_layer(bm)
    region = region_of(bm, face)
    face.normal_update()
    n = face.normal.copy()
    if abs(distance) < WELD or n.length < 1e-9:
        return list(region)
    d = n * distance
    bedges = region_boundary_edges(bm, region)
    verts = {v for f in region for v in f.verts}
    others = {e: [g for g in e.link_faces if g not in region] for e in bedges}
    for gs in others.values():
        for g in gs:
            g.normal_update()
    lone = all(not o for o in others.values())
    perpendicular = (not lone and all(others[e] for e in bedges)
                     and all(abs(g.normal.dot(n)) < 1e-6 for e in bedges for g in others[e]))

    if perpendicular and not keep:
        # the sides only stretch: SketchUp just moves the face
        bmesh.ops.translate(bm, vec=d, verts=list(verts))
        return list(region)

    # extrude: a copy of the face at the new place, with side faces
    vmap = {v: bm.verts.new(v.co + d) for v in verts}
    caps = []
    for f in region:
        nf = bm.faces.new([vmap[v] for v in f.verts])
        nf.material_index = f.material_index
        for e_old, e_new in zip(f.edges, [bm.edges.get((vmap[e.verts[0]], vmap[e.verts[1]])) for e in f.edges]):
            if e_new is not None:
                e_new[lay] = e_old[lay]
        caps.append(nf)
    walls = []
    for f in region:
        for loop in f.loops:
            e = loop.edge
            if e not in others:
                continue
            a, b = loop.vert, loop.link_loop_next.vert
            w = bm.faces.new((a, b, vmap[b], vmap[a]))
            w.material_index = f.material_index
            walls.append(w)
    for x in caps + walls:
        x.normal_update()

    if lone and not keep:
        # a lone face becomes the base of a solid: orient the shell outwards
        shell = list(region) + caps + walls
        bmesh.ops.recalc_face_normals(bm, faces=shell)
    elif not keep:
        bmesh.ops.delete(bm, geom=list(region), context="FACES_ONLY")
        remove_loose(bm)
        # side faces in the plane of a neighbour merge with it
        for e in list(others):
            if not e.is_valid or len(e.link_faces) != 2:
                continue
            f1, f2 = e.link_faces
            f1.normal_update()
            f2.normal_update()
            if f1.normal.dot(f2.normal) > 1 - 1e-6 and _coplanar(f1, f2) and e[lay] == HARD:
                ends = list(e.verts)
                try:
                    bmesh.ops.dissolve_edges(bm, edges=[e], use_verts=False, use_face_split=False)
                except Exception:
                    continue
                heal_verts(bm, ends)

    caps = [c for c in caps if c.is_valid]
    if caps:
        caps = cancel_coincident(bm, caps)
    return caps


def push_pull_to(bm, face, offset):
    """Push/Pull so that the face moves by the component of `offset` along its normal."""
    face.normal_update()
    return push_pull(bm, face, Vector(offset).dot(face.normal))


def cancel_coincident(bm, caps):
    """A face landing on another face of the same plane: opposite faces cancel (holes),
    same-side faces merge."""
    lay = soft_layer(bm)
    pl = face_plane(caps[0])
    capset = set(caps)
    infaces = faces_in_plane(bm, pl)
    other = [f for f in infaces if f not in capset]
    if not other:
        return caps
    caps[0].normal_update()
    cap_sign = 1 if caps[0].normal.dot(pl.no) >= 0 else -1
    cap_mat = caps[0].material_index
    cap_polys = [[pl.to2(v.co) for v in f.verts] for f in caps]
    olds = record_faces(other, pl)
    # weld the cap onto what is there, and split edges where they cross
    segs = [(e.verts[0].co.copy(), e.verts[1].co.copy()) for e in {e for f in caps for e in f.edges}
            if e[lay] != SEAM]
    plane_verts = [v for v in bm.verts if pl.has(v.co)]
    bmesh.ops.remove_doubles(bm, verts=plane_verts, dist=WELD)
    for a, b in segs:
        add_segment(bm, a, b)

    def classify(_p3, p2, _edges):
        in_cap = any(point_in_poly(p2, c) for c in cap_polys)
        same = [o for o in olds if o.sign == cap_sign and point_in_poly(p2, o.poly)]
        opp = [o for o in olds if o.sign != cap_sign and point_in_poly(p2, o.poly)]
        if in_cap and opp and not same:
            return None
        if in_cap:
            return (cap_sign, cap_mat)
        if same:
            return (same[0].sign, same[0].mat)
        if opp:
            return (opp[0].sign, opp[0].mat)
        return None

    made = rebuild_plane(bm, pl, olds=olds, classify=classify)
    remove_loose(bm)
    return [f for f in made if f.is_valid and any(point_in_poly(_center2(pl, f), c) for c in cap_polys)]


def _center2(pl, f):
    return pl.to2(f.calc_center_median())


# ------------------------------------------------------------------ Offset

def offset_polygon(pts, dist):
    """Offset a closed counter-clockwise 2D polygon inwards by dist (negative: outwards)."""
    n = len(pts)
    out = []
    for i in range(n):
        p0, p1, p2 = Vector(pts[i - 1]), Vector(pts[i]), Vector(pts[(i + 1) % n])
        d1 = (p1 - p0).normalized()
        d2 = (p2 - p1).normalized()
        n1 = Vector((-d1.y, d1.x))   # inward (left) normal
        n2 = Vector((-d2.y, d2.x))
        cross = d1.x * d2.y - d1.y * d2.x
        if abs(cross) < 1e-9:
            out.append(p1 + n1 * dist)
            continue
        # intersection of the two offset lines
        q1 = p1 + n1 * dist
        q2 = p1 + n2 * dist
        t = ((q2 - q1).x * d2.y - (q2 - q1).y * d2.x) / cross
        out.append(q1 + d1 * t)
    return [(p.x, p.y) for p in out]


def offset_face(bm, face, dist):
    """SketchUp Offset: a copy of the face's outer edges, dist inside (negative: outside)."""
    region = region_of(bm, face)
    pl = face_plane(face)
    loops = region_loops(bm, region)
    if not loops:
        return Result()
    outer = max(loops, key=lambda lp: abs(poly_area([pl.to2(v.co) for v in lp])))
    pts = [pl.to2(v.co) for v in outer]
    if poly_area(pts) < 0:
        pts = pts[::-1]
    off = offset_polygon(pts, dist)
    u, v = pl.axes()
    pts3 = [pl.co + u * x + v * y for x, y in off]
    return add_edges(bm, pts3, closed=True)


def outer_loop_points(bm, face):
    """(3D points of the face's outer boundary, counter-clockwise on its plane; the plane)."""
    region = region_of(bm, face)
    pl = face_plane(face)
    loops = region_loops(bm, region)
    if not loops:
        return [], pl
    outer = max(loops, key=lambda lp: abs(poly_area([pl.to2(v.co) for v in lp])))
    pts = [v.co.copy() for v in outer]
    if poly_area([pl.to2(p) for p in pts]) < 0:
        pts = pts[::-1]
    return pts, pl


def offset_points3(pts3, pl, dist):
    """Offset a CCW loop of 3D points lying on plane pl (positive: inwards)."""
    u, v = pl.axes()
    off = offset_polygon([pl.to2(p) for p in pts3], dist)
    return [pl.co + u * x + v * y for x, y in off]


def offset_preview(bm, face, dist):
    """The offset loop (3D points) without changing the mesh."""
    region = region_of(bm, face)
    pl = face_plane(face)
    loops = region_loops(bm, region)
    if not loops:
        return []
    outer = max(loops, key=lambda lp: abs(poly_area([pl.to2(v.co) for v in lp])))
    pts = [pl.to2(v.co) for v in outer]
    if poly_area(pts) < 0:
        pts = pts[::-1]
    u, v = pl.axes()
    return [pl.co + u * x + v * y for x, y in offset_polygon(pts, dist)]


def signed_distance_to_region(bm, face, p):
    """Distance from p (on the face plane) to the face's outer boundary; positive inside."""
    region = region_of(bm, face)
    pl = face_plane(face)
    loops = region_loops(bm, region)
    if not loops:
        return 0.0
    outer = max(loops, key=lambda lp: abs(poly_area([pl.to2(v.co) for v in lp])))
    poly = [pl.to2(v.co) for v in outer]
    q = pl.to2(p)
    d = min(point_seg_dist2(q, poly[i], poly[(i + 1) % len(poly)]) for i in range(len(poly))) ** 0.5
    return d if point_in_poly(q, poly) else -d


# ------------------------------------------------------------------ Eraser

def erase_edges(bm, edges, soften=False):
    """SketchUp Eraser: delete edges and the faces they bound; an edge between two
    coplanar faces just disappears and the faces merge. soften (Ctrl): hide the edge."""
    lay = soft_layer(bm)
    edges = [e for e in edges if e.is_valid]
    if soften:
        for e in edges:
            e[lay] = SOFTENED
        return
    merges = []   # (plane, olds)
    doomed = set()
    ends = set()
    for e in edges:
        if not e.is_valid:
            continue
        ends |= set(e.verts)
        lf = list(e.link_faces)
        if len(lf) == 2 and _coplanar(lf[0], lf[1]) and lf[0].normal.dot(lf[1].normal) > 0:
            pl = face_plane(lf[0])
            if not any(pl.same(m[0]) for m in merges):
                merges.append((pl, record_faces(faces_in_plane(bm, pl), pl)))
        else:
            for f in lf:
                doomed |= region_of(bm, f)
    doomed = [f for f in doomed if f.is_valid]
    merges = [(pl, [o for o in olds if o.face not in doomed]) for pl, olds in merges]
    if doomed:
        bmesh.ops.delete(bm, geom=doomed, context="FACES_ONLY")
    for e in edges:
        if e.is_valid:
            bm.edges.remove(e)
    for pl, olds in merges:
        rebuild_plane(bm, pl, olds=olds)
    remove_loose(bm)
    heal_verts(bm, [v for v in ends if v.is_valid])


def erase_faces(bm, faces):
    faces = {f for f in faces if f.is_valid}
    allf = set()
    for f in faces:
        allf |= region_of(bm, f)
    if allf:
        bmesh.ops.delete(bm, geom=list(allf), context="FACES_ONLY")
    remove_loose(bm)


# ------------------------------------------------------------------ Move, Rotate, Scale, copies

def weld(bm):
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=WELD)


def selection_verts(verts=(), edges=(), faces=()):
    out = set(verts)
    for e in edges:
        out |= set(e.verts)
    for f in faces:
        out |= set(f.verts)
    return out


def duplicate(bm, verts=(), edges=(), faces=()):
    """Copy geometry; returns (new_verts, new_edges, new_faces)."""
    geom = list(set(verts) | set(edges) | set(faces))
    for f in faces:
        geom += list(f.edges) + list(f.verts)
    for e in edges:
        geom += list(e.verts)
    geom = list(set(geom))
    res = bmesh.ops.duplicate(bm, geom=geom)
    g = res["geom"]
    return ([x for x in g if isinstance(x, bmesh.types.BMVert)],
            [x for x in g if isinstance(x, bmesh.types.BMEdge)],
            [x for x in g if isinstance(x, bmesh.types.BMFace)])


def move(bm, verts, vec, do_weld=True):
    bmesh.ops.translate(bm, vec=Vector(vec), verts=list(verts))
    if do_weld:
        weld(bm)


def rotate(bm, verts, center, axis, angle, do_weld=True):
    m = Matrix.Rotation(angle, 3, Vector(axis).normalized())
    c = Vector(center)
    for v in verts:
        v.co = c + m @ (v.co - c)
    if do_weld:
        weld(bm)


def scale(bm, verts, origin, factors, basis=None):
    """Scale verts about origin by factors (fx, fy, fz) along basis axes (default world)."""
    o = Vector(origin)
    if basis is None:
        basis = (X, Y, Z)
    for v in verts:
        d = v.co - o
        v.co = o + sum((ax * (d.dot(ax) * f) for ax, f in zip(basis, factors)), Vector())
    weld(bm)


def connected(bm, seeds):
    """All geometry connected to the seed elements: (verts, edges, faces)."""
    vs = set()
    for s in seeds:
        if isinstance(s, bmesh.types.BMVert):
            vs.add(s)
        else:
            vs |= set(s.verts)
    stack = list(vs)
    while stack:
        v = stack.pop()
        for e in v.link_edges:
            o = e.other_vert(v)
            if o not in vs:
                vs.add(o)
                stack.append(o)
    edges = {e for v in vs for e in v.link_edges}
    faces = {f for v in vs for f in v.link_faces}
    return vs, edges, faces


def extract(bm, faces, edges):
    """Move faces/edges (and what they need) out of bm into a new bmesh (Make Group)."""
    lay = soft_layer(bm)
    out = bmesh.new()
    lay_o = soft_layer(out)
    faces = set(faces)
    for f in list(faces):
        faces |= region_of(bm, f)
    edges = set(edges) | {e for f in faces for e in f.edges}
    vmap = {}

    def V(v):
        if v not in vmap:
            vmap[v] = out.verts.new(v.co)
        return vmap[v]
    for f in faces:
        nf = out.faces.new([V(v) for v in f.verts])
        nf.material_index = f.material_index
    for e in edges:
        a, b = V(e.verts[0]), V(e.verts[1])
        ne = out.edges.get((a, b)) or out.edges.new((a, b))
        ne[lay_o] = e[lay]
    # remove from the source: the faces, then edges no longer bounding anything left
    if faces:
        bmesh.ops.delete(bm, geom=list(faces), context="FACES_ONLY")
    for e in [e for e in edges if e.is_valid and not e.link_faces]:
        bm.edges.remove(e)
    for v in [v for v in bm.verts if not v.link_edges]:
        bm.verts.remove(v)
    return out


# ------------------------------------------------------------------ shapes

def rectangle_points(a, b, normal):
    """Corners of the rectangle with opposite corners a, b on the plane with this normal."""
    u, v = plane_axes(normal)
    a, b = Vector(a), Vector(b)
    d = b - a
    du, dv = d.dot(u), d.dot(v)
    return [a, a + u * du, a + u * du + v * dv, a + v * dv]


def circle_points(center, radius, normal, segments=24, start=None):
    u, v = plane_axes(normal)
    if start is not None and Vector(start).length > 1e-9:
        n = Vector(normal).normalized()
        s = Vector(start) - n * Vector(start).dot(n)
        if s.length > 1e-9:
            u = s.normalized()
            v = n.cross(u)
    c = Vector(center)
    return [c + (u * math.cos(2 * math.pi * i / segments) + v * math.sin(2 * math.pi * i / segments)) * radius
            for i in range(segments)]


def arc_points(a, b, bulge, normal, segments=12):
    """SketchUp 2-point arc from a to b with bulge (sagitta) height on the side of `normal` x (b-a)."""
    a, b = Vector(a), Vector(b)
    n = Vector(normal).normalized()
    chord = b - a
    c = chord.length
    if c < 1e-9:
        return [a, b]
    if abs(bulge) < 1e-9:
        return [a + chord * (i / segments) for i in range(segments + 1)]
    side = n.cross(chord).normalized()   # bulge direction
    h = bulge
    R = (c * c / 4 + h * h) / (2 * h)    # signed radius
    mid = (a + b) / 2
    centre = mid + side * (h - R)
    ua = a - centre
    ub = b - centre
    # sweep angle
    half = math.asin(max(-1.0, min(1.0, (c / 2) / abs(R))))
    sweep = 2 * half if abs(h) <= abs(R) else 2 * math.pi - 2 * half
    axis = ua.cross(ub)
    if axis.length < 1e-12:
        axis = -n if h > 0 else n
    else:
        axis = axis.normalized()
        if abs(h) > abs(R):
            axis = -axis
    pts = []
    for i in range(segments + 1):
        m = Matrix.Rotation(sweep * i / segments, 3, axis)
        pts.append(centre + m @ ua)
    pts[0], pts[-1] = a.copy(), b.copy()
    return pts


def arc_bulge_from_point(a, b, p, normal):
    """Bulge (signed sagitta) of the arc a-b passing through the projection of p."""
    a, b, p = Vector(a), Vector(b), Vector(p)
    n = Vector(normal).normalized()
    chord = b - a
    if chord.length < 1e-9:
        return 0.0
    side = n.cross(chord).normalized()
    return (p - (a + b) / 2).dot(side)


# ------------------------------------------------------------------ checks used by the tests

def euler_characteristic(bm):
    return len(bm.verts) - len(bm.edges) + len(bm.faces)


def is_closed(bm):
    return all(len(e.link_faces) == 2 for e in bm.edges)


def signed_volume(bm):
    vol = 0.0
    for f in bm.faces:
        vs = [v.co for v in f.verts]
        for i in range(1, len(vs) - 1):
            vol += vs[0].dot(vs[i].cross(vs[i + 1])) / 6
    return vol
