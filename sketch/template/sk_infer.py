# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""SketchUp's inference engine, without bpy.

Given a view (a 4x4 perspective matrix and the region size), the mouse
position and a snapshot of the model, `infer` finds the point SketchUp would
pick: Endpoint, Midpoint, Intersection, Center, On Edge, On Face, On Red /
Green / Blue Axis, Parallel / Perpendicular to Edge, From Point, guides, and
the drawing plane as a fallback. Locks (arrow keys, Shift) constrain it.
"""

import math
import time
import numpy as np
from mathutils import Matrix, Vector, geometry as mgeo

SNAP_PX = 10.0       # point inferences
EDGE_PX = 7.0        # On Edge
AXIS_PX = 12.0       # axis / parallel / from-point lines
HOVER_TRACK_S = 0.35  # hovering a point this long tracks it (From Point)

X = Vector((1, 0, 0))
Y = Vector((0, 1, 0))
Z = Vector((0, 0, 1))
AXES = {"axis_x": X, "axis_y": Y, "axis_z": Z}

# SketchUp's inference colours
GREEN = (0.0, 0.72, 0.0, 1.0)
CYAN = (0.0, 0.78, 0.82, 1.0)
RED = (0.9, 0.05, 0.05, 1.0)
BLUE = (0.1, 0.25, 1.0, 1.0)
MAGENTA = (0.85, 0.0, 0.85, 1.0)
BLACK = (0.08, 0.08, 0.08, 1.0)
AXIS_RED = (0.9, 0.1, 0.1, 1.0)
AXIS_GREEN = (0.0, 0.65, 0.0, 1.0)
AXIS_BLUE = (0.1, 0.25, 1.0, 1.0)

COLORS = {
    "endpoint": GREEN, "midpoint": CYAN, "on_edge": RED, "on_face": BLUE, "center": GREEN,
    "intersection": BLACK, "guide_point": BLACK, "on_guide": BLACK,
    "axis_x": AXIS_RED, "axis_y": AXIS_GREEN, "axis_z": AXIS_BLUE,
    "parallel": MAGENTA, "perpendicular": MAGENTA, "from_point": BLACK, "plane": BLACK, "none": BLACK,
}
LABELS = {
    "endpoint": "Endpoint", "midpoint": "Midpoint", "on_edge": "On Edge", "on_face": "On Face",
    "center": "Center", "intersection": "Intersection", "guide_point": "Guide Point", "on_guide": "On Line",
    "axis_x": "On Red Axis", "axis_y": "On Green Axis", "axis_z": "On Blue Axis",
    "parallel": "Parallel to Edge", "perpendicular": "Perpendicular to Edge", "from_point": "From Point",
    "plane": "", "none": "",
}
POINT_KINDS = ("endpoint", "midpoint", "intersection", "center", "guide_point")
LINE_KINDS = ("axis_x", "axis_y", "axis_z", "parallel", "perpendicular")


def axis_kind_of(direction):
    d = Vector(direction).normalized()
    for k, a in AXES.items():
        if abs(d.dot(a)) > 1 - 1e-6:
            return k
    return None


# ------------------------------------------------------------------ view

class View:
    """Projection between 3D and region pixels, from the region's perspective matrix."""

    def __init__(self, persp_matrix, width, height):
        self.M = Matrix(persp_matrix)
        self.Mi = self.M.inverted()
        self.W = float(width)
        self.H = float(height)
        self.Mnp = np.array([list(r) for r in self.M], dtype=np.float64)

    @classmethod
    def look_at(cls, eye, target, width=1000, height=800, fov_deg=50.0, near=0.1, far=1000.0, ortho_scale=None):
        """A view as a camera at `eye` looking at `target` (Z up) would see it. For tests."""
        eye, target = Vector(eye), Vector(target)
        f = (target - eye).normalized()
        up = Z if abs(f.dot(Z)) < 0.999 else Y
        s = f.cross(up).normalized()
        u = s.cross(f)
        view = Matrix(((s.x, s.y, s.z, -s.dot(eye)),
                       (u.x, u.y, u.z, -u.dot(eye)),
                       (-f.x, -f.y, -f.z, f.dot(eye)),
                       (0, 0, 0, 1)))
        aspect = width / height
        if ortho_scale:
            r = ortho_scale / 2
            t = r / aspect
            proj = Matrix(((1 / r, 0, 0, 0), (0, 1 / t, 0, 0),
                           (0, 0, -2 / (far - near), -(far + near) / (far - near)), (0, 0, 0, 1)))
        else:
            fy = 1 / math.tan(math.radians(fov_deg) / 2)
            proj = Matrix(((fy / aspect, 0, 0, 0), (0, fy, 0, 0),
                           (0, 0, (far + near) / (near - far), 2 * far * near / (near - far)),
                           (0, 0, -1, 0)))
        return cls(proj @ view, width, height)

    def project(self, co):
        v = self.M @ Vector((co[0], co[1], co[2], 1.0))
        if v.w <= 1e-9:
            return None
        return Vector(((v.x / v.w + 1) * 0.5 * self.W, (v.y / v.w + 1) * 0.5 * self.H))

    def project_many(self, arr):
        """(N,3) -> ((N,2) pixels, (N,) visible-in-front mask)."""
        if len(arr) == 0:
            return np.zeros((0, 2)), np.zeros((0,), dtype=bool)
        M = self.Mnp
        h = arr @ M[:, :3].T + M[:, 3]
        w = h[:, 3]
        ok = w > 1e-9
        ws = np.where(ok, w, 1.0)
        scr = np.empty((len(arr), 2))
        scr[:, 0] = (h[:, 0] / ws + 1) * 0.5 * self.W
        scr[:, 1] = (h[:, 1] / ws + 1) * 0.5 * self.H
        return scr, ok

    def ray(self, mouse):
        x = 2 * mouse[0] / self.W - 1
        y = 2 * mouse[1] / self.H - 1
        a = self.Mi @ Vector((x, y, -1, 1))
        b = self.Mi @ Vector((x, y, 1, 1))
        a = a.xyz / a.w
        b = b.xyz / b.w
        return a, (b - a).normalized()


# ------------------------------------------------------------------ snapshot of the model

class Snapshot:
    """Everything the engine can infer from, in world coordinates."""

    def __init__(self):
        self._V, self._E, self._T, self._TN = [], [], [], []
        self._Vg, self._Eg, self._Tg = [], [], []
        self._Esrc, self._Tsrc = [], []
        self.centers = []
        self.guide_points = []
        self.guide_lines = []     # (point, direction)
        self.objects = []         # names (or anything) by index
        self.finalize()

    def add(self, verts, edges, tris, tri_normals, tri_faces=None, edge_ids=None, obj=None, in_group=False,
            centers=()):
        """verts (N,3) world; edges (E,2) vertex indices; tris (K,3) vertex indices."""
        verts = np.asarray(verts, dtype=np.float64).reshape(-1, 3)
        edges = np.asarray(edges, dtype=np.int64).reshape(-1, 2)
        tris = np.asarray(tris, dtype=np.int64).reshape(-1, 3)
        oi = len(self.objects)
        self.objects.append(obj)
        self._V.append(verts)
        self._Vg.append(np.full(len(verts), in_group))
        if len(edges):
            self._E.append(verts[edges])
            self._Eg.append(np.full(len(edges), in_group))
            ids = np.arange(len(edges)) if edge_ids is None else np.asarray(edge_ids)
            self._Esrc.append(np.stack([np.full(len(edges), oi), ids], 1))
        if len(tris):
            self._T.append(verts[tris])
            self._TN.append(np.asarray(tri_normals, dtype=np.float64).reshape(-1, 3))
            self._Tg.append(np.full(len(tris), in_group))
            fids = np.arange(len(tris)) if tri_faces is None else np.asarray(tri_faces)
            self._Tsrc.append(np.stack([np.full(len(tris), oi), fids], 1))
        self.centers += [Vector(c) for c in centers]

    def add_bmesh(self, bm, matrix=None, obj=None, in_group=False, soft_layer_name="sketch_soft"):
        m = matrix if matrix is not None else Matrix.Identity(4)
        bm.verts.index_update()
        bm.edges.index_update()
        bm.faces.index_update()
        verts = [tuple(m @ v.co) for v in bm.verts]
        lay = bm.edges.layers.int.get(soft_layer_name)
        edges, eids = [], []
        for e in bm.edges:
            if lay is not None and e[lay] != 0:
                continue
            edges.append((e.verts[0].index, e.verts[1].index))
            eids.append(e.index)
        nm = m.to_3x3().inverted_safe().transposed()
        centers = []
        for f in bm.faces:
            f.normal_update()
            c = circle_center([m @ v.co for v in f.verts])
            if c is not None:
                centers.append(c)
        tris, tn, tf = [], [], []
        for loop_tri in bm.calc_loop_triangles():
            f = loop_tri[0].face
            n = (nm @ f.normal).normalized()
            tris.append(tuple(lp.vert.index for lp in loop_tri))
            tn.append(tuple(n))
            tf.append(f.index)
        self.add(verts, edges, tris, tn, tf, eids, obj, in_group, centers)

    def finalize(self):
        def cat(lst, shape, dtype=np.float64):
            return np.concatenate(lst) if lst else np.zeros(shape, dtype=dtype)
        self.V = cat(self._V, (0, 3))
        self.Vg = cat(self._Vg, (0,), bool)
        self.E = cat(self._E, (0, 2, 3))
        self.Eg = cat(self._Eg, (0,), bool)
        self.Esrc = cat(self._Esrc, (0, 2), np.int64)
        self.T = cat(self._T, (0, 3, 3))
        self.TN = cat(self._TN, (0, 3))
        self.Tg = cat(self._Tg, (0,), bool)
        self.Tsrc = cat(self._Tsrc, (0, 2), np.int64)
        self.M = (self.E[:, 0] + self.E[:, 1]) / 2 if len(self.E) else np.zeros((0, 3))
        self.C = np.array([tuple(c) for c in self.centers]) if self.centers else np.zeros((0, 3))
        self.GP = np.array([tuple(p) for p in self.guide_points]) if self.guide_points else np.zeros((0, 3))
        return self


def circle_center(pts, min_sides=8):
    """Centre of a face whose corners lie on a circle (SketchUp keeps circles' centres)."""
    if len(pts) < min_sides:
        return None
    c = sum(pts, Vector()) / len(pts)
    rs = [(p - c).length for p in pts]
    r = sum(rs) / len(rs)
    if r < 1e-6 or max(abs(x - r) for x in rs) > r * 0.01:
        return None
    return c


def ray_tris(o, d, T):
    """Nearest triangle hit: (t, index) or (None, None)."""
    if len(T) == 0:
        return None, None
    o = np.asarray(o, dtype=np.float64)
    d = np.asarray(d, dtype=np.float64)
    v0 = T[:, 0]
    e1 = T[:, 1] - v0
    e2 = T[:, 2] - v0
    p = np.cross(d, e2)
    det = (e1 * p).sum(1)
    ok = np.abs(det) > 1e-14
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    s = o - v0
    u = (s * p).sum(1) * inv
    q = np.cross(s, e1)
    v = (q * d).sum(1) * inv
    t = (e2 * q).sum(1) * inv
    m = ok & (u >= -1e-9) & (v >= -1e-9) & (u + v <= 1 + 1e-9) & (t > 1e-9)
    if not m.any():
        return None, None
    tt = np.where(m, t, np.inf)
    i = int(np.argmin(tt))
    return float(tt[i]), i


def seg_dist_px(scr_a, scr_b, mouse):
    ab = scr_b - scr_a
    L = (ab * ab).sum(1)
    t = np.clip(((mouse - scr_a) * ab).sum(1) / np.where(L > 1e-12, L, 1.0), 0, 1)
    q = scr_a + ab * t[:, None]
    return np.sqrt(((q - mouse) ** 2).sum(1))


def closest_on_line_to_ray(p, d, origin, ray):
    res = mgeo.intersect_line_line(p, p + d, origin, origin + ray)
    if res is None:
        return None
    return res[0]


def line_line_point(p1, d1, p2, d2, tol=1e-6):
    res = mgeo.intersect_line_line(p1, p1 + d1, p2, p2 + d2)
    if res is None:
        return None
    a, b = res
    if (a - b).length > tol * max(1.0, (a - p1).length):
        return None
    return a


# ------------------------------------------------------------------ result and state

class Inference:
    __slots__ = ("point", "kind", "edge", "normal", "line", "from_pt", "src", "in_group", "plane_pt")

    def __init__(self, point, kind, edge=None, normal=None, line=None, from_pt=None, src=None, in_group=False):
        self.point = point
        self.kind = kind
        self.edge = edge          # (a, b) for On Edge / Midpoint
        self.normal = normal      # face normal for On Face
        self.line = line          # (start, end, kind) of a coloured inference line to draw
        self.from_pt = from_pt    # dotted line start (From Point)
        self.src = src            # (object index, element index) in the snapshot
        self.in_group = in_group
        self.plane_pt = None

    @property
    def label(self):
        lab = LABELS.get(self.kind, "")
        if lab and self.in_group and self.kind in ("endpoint", "midpoint", "on_edge", "on_face", "center"):
            lab += " in Group"
        return lab

    @property
    def color(self):
        return COLORS.get(self.kind, BLACK)

    def __repr__(self):
        return f"<{self.kind} {tuple(round(c, 4) for c in self.point) if self.point is not None else None}>"


class InferState:
    """What the engine remembers between mouse moves: tracked points, the reference edge, locks."""

    def __init__(self):
        self.tracked = []        # points to infer From (hovered for a moment)
        self.ref_edge = None     # direction of the last edge hovered (parallel/perpendicular)
        self.lock = None         # ("dir", vector) | ("line", point, dir) | ("plane", point, normal) | ("point", p)
        self.lock_kind = None
        self._hover = None
        self._since = 0.0

    def observe(self, inf, now=None):
        now = time.monotonic() if now is None else now
        if inf is None or inf.point is None:
            self._hover = None
            return
        if inf.kind in ("on_edge", "midpoint") and inf.edge is not None:
            a, b = inf.edge
            if (b - a).length > 1e-9:
                self.ref_edge = (b - a).normalized()
        if inf.kind in POINT_KINDS:
            if self._hover is not None and (self._hover - inf.point).length < 1e-6:
                if now - self._since > HOVER_TRACK_S and not any((t - inf.point).length < 1e-6 for t in self.tracked):
                    self.tracked = (self.tracked + [inf.point.copy()])[-2:]
            else:
                self._hover = inf.point.copy()
                self._since = now
        else:
            self._hover = None

    def lock_current(self, inf, last):
        """Shift: lock the current inference."""
        if inf is None or inf.point is None:
            return
        if inf.kind in LINE_KINDS and inf.line is not None:
            s, e, _k = inf.line
            self.lock = ("dir", (e - s).normalized())
        elif inf.kind == "on_edge" and inf.edge is not None:
            a, b = inf.edge
            self.lock = ("line", a.copy(), (b - a).normalized())
        elif inf.kind == "on_face" and inf.normal is not None:
            self.lock = ("plane", inf.point.copy(), inf.normal.copy())
        elif inf.kind in POINT_KINDS and last is not None and (inf.point - last).length > 1e-9:
            self.lock = ("dir", (inf.point - last).normalized())
        else:
            return
        self.lock_kind = inf.kind

    def lock_axis(self, axis_kind, last=None, plane_normal=None):
        """Arrow keys: right = red, left = green, up = blue, down = parallel/perpendicular."""
        if self.lock is not None and self.lock_kind == axis_kind:
            self.unlock()
            return
        if axis_kind in AXES:
            self.lock = ("dir", AXES[axis_kind].copy())
            self.lock_kind = axis_kind
        elif axis_kind == "parallel" and self.ref_edge is not None:
            self.lock = ("dir", self.ref_edge.copy())
            self.lock_kind = "parallel"

    def unlock(self):
        self.lock = None
        self.lock_kind = None


# ------------------------------------------------------------------ the engine

def _point_candidates(view, mouse, snap, extra_points, origin, ray, hit_t):
    """Best point inference (kind, point, src, in_group) within SNAP_PX, honouring occlusion."""
    best = None
    groups = [
        (snap.V, "endpoint", 0.0, snap.Vg),
        (np.array([tuple(p) for p in extra_points]) if extra_points else np.zeros((0, 3)), "endpoint", 0.0, None),
        (snap.M, "midpoint", 2.0, snap.Eg),
        (snap.C, "center", 3.0, None),
        (snap.GP, "guide_point", 1.0, None),
    ]
    for arr, kind, prio, grp in groups:
        if len(arr) == 0:
            continue
        scr, ok = view.project_many(arr)
        dist = np.sqrt(((scr - mouse) ** 2).sum(1))
        dist[~ok] = np.inf
        if hit_t is not None:
            t = (arr - np.asarray(origin)) @ np.asarray(ray)
            dist[t > hit_t * (1 + 1e-4) + 1e-4] = np.inf
        i = int(np.argmin(dist))
        if dist[i] < SNAP_PX:
            score = dist[i] + prio
            if best is None or score < best[0]:
                best = (score, kind, Vector(arr[i]), i, bool(grp[i]) if grp is not None else False)
    return best


def _edge_candidates(view, mouse, snap, origin, ray, hit_t, radius):
    """Edges whose projection passes within radius px of the mouse, nearest first: [(dist, index)]."""
    if len(snap.E) == 0:
        return []
    sa, oka = view.project_many(snap.E[:, 0])
    sb, okb = view.project_many(snap.E[:, 1])
    dist = seg_dist_px(sa, sb, mouse)
    dist[~(oka & okb)] = np.inf
    idx = np.nonzero(dist < radius)[0]
    out = []
    for i in idx[np.argsort(dist[idx])][:24]:
        a, b = Vector(snap.E[i, 0]), Vector(snap.E[i, 1])
        q = closest_on_line_to_ray(a, b - a, origin, ray)
        if q is None:
            continue
        ab = b - a
        t = max(0.0, min(1.0, (q - a).dot(ab) / max(ab.length_squared, 1e-18)))
        q = a + ab * t
        if hit_t is not None and (q - origin).dot(ray) > hit_t * (1 + 1e-4) + 1e-4:
            continue
        out.append((float(dist[i]), int(i), q))
    return out


def _guide_line_hits(view, mouse, snap, origin, ray):
    out = []
    for p, d in snap.guide_lines:
        q = closest_on_line_to_ray(p, d, origin, ray)
        if q is None:
            continue
        s = view.project(q)
        if s is None:
            continue
        dist = (s - Vector(mouse)).length
        if dist < EDGE_PX:
            out.append((dist, p, d, q))
    out.sort(key=lambda x: x[0])
    return out


def _line_candidates(last, state, plane_normal):
    lines = [(k, a) for k, a in AXES.items()]
    if state is not None and state.ref_edge is not None:
        r = state.ref_edge
        if axis_kind_of(r) is None:
            lines.append(("parallel", r))
        n = plane_normal if plane_normal is not None else Z
        perp = r.cross(n)
        if perp.length > 1e-6 and axis_kind_of(perp) is None:
            lines.append(("perpendicular", perp.normalized()))
    return lines


def infer(view, mouse, snap, last=None, plane=None, state=None, extra_points=(), allow_lines=True):
    """The inferred point under the mouse (an Inference, its point may be None)."""
    mouse = np.asarray(mouse, dtype=np.float64)
    mv = Vector((float(mouse[0]), float(mouse[1])))
    origin, ray = view.ray(mouse)
    hit_t, tri = ray_tris(origin, ray, snap.T)
    last = Vector(last) if last is not None else None
    plane_normal = Vector(plane[1]).normalized() if plane is not None else None

    def near(p, px):
        s = view.project(p)
        return s is not None and (s - mv).length < px

    pt = _point_candidates(view, mouse, snap, extra_points, origin, ray, hit_t)
    edges = _edge_candidates(view, mouse, snap, origin, ray, hit_t, EDGE_PX)

    # ---- locks
    if state is not None and state.lock is not None:
        lk = state.lock
        if lk[0] in ("dir", "line"):
            if lk[0] == "dir":
                if last is None:
                    state.unlock()
                    return infer(view, mouse, snap, last, plane, state, extra_points, allow_lines)
                p0, d = last, lk[1]
            else:
                p0, d = lk[1], lk[2]
            kind = state.lock_kind if state.lock_kind in LINE_KINDS else (axis_kind_of(d) or "parallel")
            if pt is not None:
                # a hovered point is projected onto the locked line
                q = p0 + d * (pt[2] - p0).dot(d)
                inf = Inference(q, kind, line=(p0, q, kind), from_pt=pt[2])
                return inf
            q = closest_on_line_to_ray(p0, d, origin, ray)
            if q is None:
                return Inference(None, "none")
            return Inference(q, kind, line=(p0, q, kind))
        if lk[0] == "plane":
            if pt is not None:
                n = lk[2]
                q = pt[2] - n * (pt[2] - lk[1]).dot(n)
                return Inference(q, "on_face", normal=n, from_pt=pt[2])
            q = mgeo.intersect_line_plane(origin, origin + ray, lk[1], lk[2])
            return Inference(q, "on_face", normal=lk[2])

    # ---- points: Endpoint, Midpoint, Center, Guide Point
    if pt is not None:
        _s, kind, p, i, grp = pt
        inf = Inference(p, kind, in_group=grp)
        if kind == "midpoint":
            inf.edge = (Vector(snap.E[i, 0]), Vector(snap.E[i, 1]))
            inf.src = tuple(snap.Esrc[i]) if len(snap.Esrc) else None
        if last is not None:
            k = axis_kind_of(p - last) if (p - last).length > 1e-9 else None
            if k:
                inf.line = (last, p, k)
        return inf

    # ---- Intersection of two edges near the mouse
    if len(edges) >= 2:
        for i in range(min(len(edges), 8)):
            for j in range(i + 1, min(len(edges), 8)):
                a1, b1 = Vector(snap.E[edges[i][1], 0]), Vector(snap.E[edges[i][1], 1])
                a2, b2 = Vector(snap.E[edges[j][1], 0]), Vector(snap.E[edges[j][1], 1])
                q = line_line_point(a1, b1 - a1, a2, b2 - a2)
                if q is None:
                    continue
                t1 = (q - a1).dot(b1 - a1) / max((b1 - a1).length_squared, 1e-18)
                t2 = (q - a2).dot(b2 - a2) / max((b2 - a2).length_squared, 1e-18)
                if 0 <= t1 <= 1 and 0 <= t2 <= 1 and near(q, SNAP_PX):
                    return Inference(q, "intersection")

    # ---- lines from the last point: axes, parallel/perpendicular, combined with From Point
    line_hit = None
    if last is not None and allow_lines:
        for kind, d in _line_candidates(last, state, plane_normal):
            q = closest_on_line_to_ray(last, d, origin, ray)
            if q is None or (q - last).length < 1e-9:
                continue
            s = view.project(q)
            if s is None:
                continue
            dist = (s - mv).length
            if dist < AXIS_PX and (line_hit is None or dist < line_hit[0]):
                line_hit = (dist, kind, d, q)
        if line_hit is not None:
            _dist, kind, d, q = line_hit
            # On Edge where the axis crosses an edge
            for _ed, ei, _q in edges:
                a, b = Vector(snap.E[ei, 0]), Vector(snap.E[ei, 1])
                x = line_line_point(last, d, a, b - a)
                if x is not None and near(x, SNAP_PX):
                    t = (x - a).dot(b - a) / max((b - a).length_squared, 1e-18)
                    if 0 <= t <= 1:
                        return Inference(x, "on_edge", edge=(a, b), line=(last, x, kind),
                                         src=tuple(snap.Esrc[ei]), in_group=bool(snap.Eg[ei]))
            # From Point: the axis line meets an axis line through a tracked point
            if state is not None:
                for tp in state.tracked:
                    for k2, d2 in AXES.items():
                        if abs(d2.dot(d)) > 0.999:
                            continue
                        x = line_line_point(last, d, tp, d2)
                        if x is not None and near(x, AXIS_PX):
                            return Inference(x, kind, line=(last, x, kind), from_pt=tp.copy())
            # On Face: the axis line pierces a face near the mouse? keep the axis point
            return Inference(q, kind, line=(last, q, kind))

    # ---- On Edge
    if edges:
        _d, ei, q = edges[0]
        a, b = Vector(snap.E[ei, 0]), Vector(snap.E[ei, 1])
        return Inference(q, "on_edge", edge=(a, b), src=tuple(snap.Esrc[ei]), in_group=bool(snap.Eg[ei]))

    # ---- guide lines
    gl = _guide_line_hits(view, mouse, snap, origin, ray)
    if gl:
        _d, p, d, q = gl[0]
        return Inference(q, "on_guide", edge=(p, p + d))

    # ---- From Point without a last point (the dotted line from a tracked point along an axis)
    if state is not None and state.tracked:
        best = None
        for tp in state.tracked:
            for kind, d in AXES.items():
                q = closest_on_line_to_ray(tp, d, origin, ray)
                if q is None or (q - tp).length < 1e-6:
                    continue
                s = view.project(q)
                if s is None:
                    continue
                dist = (s - mv).length
                if dist < AXIS_PX * 0.7 and (best is None or dist < best[0]):
                    best = (dist, kind, q, tp)
        if best is not None:
            _d, kind, q, tp = best
            return Inference(q, "from_point", from_pt=tp.copy(), line=(tp, q, kind))

    # ---- On Face
    if tri is not None:
        p = origin + ray * hit_t
        n = Vector(snap.TN[tri])
        return Inference(p, "on_face", normal=n, src=tuple(snap.Tsrc[tri]), in_group=bool(snap.Tg[tri]))

    # ---- the drawing plane, else the ground
    if plane is not None:
        p = mgeo.intersect_line_plane(origin, origin + ray, Vector(plane[0]), Vector(plane[1]))
    else:
        p = mgeo.intersect_line_plane(origin, origin + ray, Vector((0, 0, 0)), Z)
        if p is not None and (p - origin).dot(ray) < 0:
            p = None
    if p is None:
        # looking at the horizon: a point at some depth along the ray
        p = origin + ray * 10.0
    return Inference(p, "plane")


# ------------------------------------------------------------------ Measurements box (VCB)

UNITS = {"mm": 0.001, "cm": 0.01, "m": 1.0, "km": 1000.0, "\"": 0.0254, "in": 0.0254, "'": 0.3048, "ft": 0.3048}


def parse_length(text):
    """'2.5' / '250cm' / '25mm' / '3\\'' -> metres (None if not a length)."""
    t = text.strip().lower().replace(" ", "")
    if not t:
        return None
    for suf in sorted(UNITS, key=len, reverse=True):
        if t.endswith(suf) and suf != t:
            try:
                return float(t[: -len(suf)].replace(",", ".")) * UNITS[suf]
            except ValueError:
                return None
    try:
        return float(t)
    except ValueError:
        return None


def parse_vcb(text):
    """What was typed in the Measurements box.

    Returns one of:
      ("absolute", Vector)   [x,y,z]
      ("relative", Vector)   <x,y,z>
      ("segments", n)        24s
      ("array", n)           5x  or  *5
      ("divide", n)          /5
      ("values", [floats])   2.5   or   4,3   (lengths)
      None
    """
    t = text.strip()
    if not t:
        return None
    low = t.lower()
    try:
        if low.endswith("s") and low[:-1].isdigit():
            return ("segments", int(low[:-1]))
        if (low.endswith("x") and low[:-1].isdigit()):
            return ("array", int(low[:-1]))
        if low.startswith("*") and low[1:].isdigit():
            return ("array", int(low[1:]))
        if low.startswith("x") and low[1:].isdigit():
            return ("array", int(low[1:]))
        if low.startswith("/") and low[1:].isdigit():
            return ("divide", int(low[1:]))
        if (low.startswith("[") and low.endswith("]")) or (low.startswith("<") and low.endswith(">")):
            parts = [parse_length(p) for p in low[1:-1].replace(";", ",").split(",")]
            if len(parts) == 3 and all(p is not None for p in parts):
                return ("absolute" if low.startswith("[") else "relative", Vector(parts))
            return None
    except ValueError:
        return None
    parts = [p for p in low.replace(";", ",").split(",")]
    vals = [parse_length(p) if p.strip() else None for p in parts]
    if vals and all(v is not None or not p.strip() for v, p in zip(vals, parts)):
        return ("values", vals)
    return None


def fmt_len(m):
    if m is None:
        return ""
    a = abs(m)
    if a < 1.0:
        return f"{m * 100:.1f} cm" if a >= 0.01 else f"{m * 1000:.1f} mm"
    return f"{m:.3f} m"
