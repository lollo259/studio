# Ben - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Small mesh/material helpers shared by Ben's generators. All sizes in metres."""

import math
import bmesh
import bpy
from mathutils import Matrix, Vector

COLLECTION_NAME = "Ben"


# ---------------------------------------------------------------- collections

def ben_collection(context, sub=None):
    """Return (and create) the 'Ben' collection, optionally a sub-collection."""
    scene_col = context.scene.collection
    root = bpy.data.collections.get(COLLECTION_NAME)
    if root is None:
        root = bpy.data.collections.new(COLLECTION_NAME)
    if root.name not in scene_col.children:
        scene_col.children.link(root)
    if not sub:
        return root
    name = f"{sub}"
    col = root.children.get(name)
    if col is None:
        col = bpy.data.collections.new(name)
        root.children.link(col)
    return col


def link(obj, collection):
    collection.objects.link(obj)
    return obj


# ---------------------------------------------------------------- materials

def material(name, rgb, roughness=0.6, emission=None, strength=1.0):
    mat = bpy.data.materials.get(name)
    if mat is not None:
        return mat
    mat = bpy.data.materials.new(name)
    if hasattr(mat, "use_nodes"):
        try:
            mat.use_nodes = True
        except Exception:
            pass
    mat.diffuse_color = (*rgb, 1.0)
    nt = mat.node_tree
    bsdf = nt.nodes.get("Principled BSDF") if nt else None
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
        bsdf.inputs["Roughness"].default_value = roughness
        if emission is not None:
            for key in ("Emission Color", "Emission"):
                if key in bsdf.inputs:
                    bsdf.inputs[key].default_value = (*emission, 1.0)
                    break
            if "Emission Strength" in bsdf.inputs:
                bsdf.inputs["Emission Strength"].default_value = strength
    return mat


MATS = {
    "floor": ((0.28, 0.20, 0.13), 0.45),   # wooden floor
    "stage": ((0.06, 0.06, 0.06), 0.5),    # black stage floor
    "wall": ((0.85, 0.84, 0.81), 0.8),     # plaster
    "seat": ((0.10, 0.10, 0.12), 0.7),     # dark fabric
    "piano": ((0.01, 0.01, 0.01), 0.15),   # polished black
    "metal": ((0.55, 0.55, 0.56), 0.3),    # truss, stands
    "figure": ((0.62, 0.62, 0.62), 0.8),   # neutral scale figure
    "stone": ((0.62, 0.60, 0.56), 0.9),
    "roof": ((0.36, 0.22, 0.17), 0.8),
    "keys": ((0.92, 0.91, 0.88), 0.3),
}


def mat(key):
    rgb, rough = MATS[key]
    return material(f"Ben {key}", rgb, rough)


# ---------------------------------------------------------------- meshes

def mesh_object(name, bm, collection, material_key=None, location=(0, 0, 0)):
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    obj.location = location
    if material_key:
        me.materials.append(mat(material_key))
    return link(obj, collection)


def add_box(bm, size, center):
    """Axis-aligned box into bm. size=(x,y,z), center=(x,y,z)."""
    sx, sy, sz = size
    m = Matrix.Translation(Vector(center)) @ Matrix.Diagonal((sx, sy, sz, 1.0))
    bmesh.ops.create_cube(bm, size=1.0, matrix=m)


def box(name, size, center, collection, material_key=None):
    bm = bmesh.new()
    add_box(bm, size, (0, 0, 0))
    return mesh_object(name, bm, collection, material_key, location=center)


def add_cylinder(bm, radius, depth, center, segments=16, axis="Z"):
    rot = Matrix.Identity(4)
    if axis == "X":
        rot = Matrix.Rotation(math.radians(90), 4, "Y")
    elif axis == "Y":
        rot = Matrix.Rotation(math.radians(90), 4, "X")
    m = Matrix.Translation(Vector(center)) @ rot
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments,
                          radius1=radius, radius2=radius, depth=depth, matrix=m)


def add_prism(bm, outline, z0, z1):
    """Extrude a closed 2D outline [(x,y),...] from z0 to z1."""
    verts = [bm.verts.new((x, y, z0)) for x, y in outline]
    face = bm.faces.new(verts)
    res = bmesh.ops.extrude_face_region(bm, geom=[face])
    top = [e for e in res["geom"] if isinstance(e, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, verts=top, vec=(0, 0, z1 - z0))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])


def look_at(obj, target):
    direction = Vector(target) - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def instance(name, mesh, collection, location, rot_z=0.0):
    obj = bpy.data.objects.new(name, mesh)
    obj.location = location
    obj.rotation_euler = (0, 0, rot_z)
    return link(obj, collection)
