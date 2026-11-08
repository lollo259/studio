# Ben - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Liquids, smoke and fire made simple.

Still liquid: fills a container instantly (no simulation), ideal for pictures.
Pour liquid: sets up a real liquid simulation (container = solid wall,
liquid falls in from above). Press Simulate, then scrub to a frame.
"""

import math
import os
import tempfile

import bmesh
import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, FloatVectorProperty, IntProperty
from mathutils import Vector

from . import geometry as g

# name: (colour, transparency 0-1, roughness, ior, viscosity 0 = water)
LIQUIDS = {
    "WATER": ((0.80, 0.90, 0.95), 1.0, 0.02, 1.333, 0.0),
    "SEA": ((0.10, 0.45, 0.50), 0.9, 0.05, 1.34, 0.0),
    "WINE": ((0.25, 0.01, 0.04), 0.6, 0.03, 1.36, 0.0),
    "OIL": ((0.75, 0.55, 0.10), 0.9, 0.05, 1.47, 0.08),
    "HONEY": ((0.80, 0.42, 0.05), 0.8, 0.08, 1.50, 0.6),
    "MILK": ((0.95, 0.94, 0.90), 0.0, 0.25, 1.35, 0.01),
    "INK": ((0.01, 0.01, 0.015), 0.0, 0.05, 1.35, 0.0),
}
LIQUID_ITEMS = [(k, k.title(), "") for k in LIQUIDS] + [("CUSTOM", "Custom", "")]


def bounds_world(obj):
    pts = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return lo, hi


def liquid_material(name, rgb, transparency, roughness, ior):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    if hasattr(mat, "use_nodes"):
        try:
            mat.use_nodes = True
        except Exception:
            pass
    mat.diffuse_color = (*rgb, 1.0 - 0.6 * transparency)
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
        bsdf.inputs["Roughness"].default_value = roughness
        bsdf.inputs["IOR"].default_value = ior
        for key in ("Transmission Weight", "Transmission"):
            if key in bsdf.inputs:
                bsdf.inputs[key].default_value = transparency
                break
    # Eevee: let light through
    for attr, val in (("surface_render_method", "DITHERED"), ("use_raytrace_refraction", True)):
        if hasattr(mat, attr):
            try:
                setattr(mat, attr, val)
            except Exception:
                pass
    return mat


def looks_round(obj):
    """Rough guess: is the container's footprint circular?"""
    lo, hi = bounds_world(obj)
    w, d = hi.x - lo.x, hi.y - lo.y
    if w <= 0 or d <= 0 or abs(w - d) / max(w, d) > 0.05:
        return False
    me = obj.data
    # many vertices near the footprint circle = round
    cx, cy, r = (lo.x + hi.x) / 2, (lo.y + hi.y) / 2, w / 2
    mw = obj.matrix_world
    near = sum(1 for v in me.vertices
               if abs(((mw @ v.co).xy - Vector((cx, cy))).length - r) < r * 0.03)
    return near >= 16


class LiquidSettings:
    liquid: EnumProperty(name="Liquid", items=LIQUID_ITEMS, default="WATER")
    color: FloatVectorProperty(name="Colour", subtype="COLOR", size=3, min=0, max=1, default=(0.8, 0.9, 0.95))
    transparency: FloatProperty(name="Transparency", default=1.0, min=0.0, max=1.0, subtype="FACTOR")
    reflect: FloatProperty(name="Reflectiveness", default=0.98, min=0.0, max=1.0, subtype="FACTOR",
                           description="1 = mirror-like surface, 0 = matte")
    viscosity: FloatProperty(name="Viscosity", default=0.0, min=0.0, max=1.0, subtype="FACTOR",
                             description="0 = water, 1 = very thick (honey)")

    def apply_preset(self):
        if self.liquid in LIQUIDS:
            col, tr, rough, _ior, visc = LIQUIDS[self.liquid]
            self.color, self.transparency, self.reflect, self.viscosity = col, tr, 1.0 - rough, visc

    def material(self):
        ior = LIQUIDS.get(self.liquid, (None, None, None, 1.34, None))[3]
        return liquid_material(f"Ben liquid {self.liquid.title()}", tuple(self.color), self.transparency,
                               max(0.0, 1.0 - self.reflect), ior)

    def draw_settings(self, layout):
        layout.prop(self, "liquid")
        if self.liquid == "CUSTOM":
            col = layout.column(align=True)
            col.prop(self, "color")
            col.prop(self, "transparency")
            col.prop(self, "reflect")
            col.prop(self, "viscosity")


class BEN_OT_liquid_still(LiquidSettings, bpy.types.Operator):
    """Fill the selected container with liquid instantly (no simulation)"""
    bl_idname = "ben.liquid_still"
    bl_label = "Fill with liquid (still)"
    bl_options = {"REGISTER", "UNDO"}

    level: FloatProperty(name="Fill level", default=0.75, min=0.01, max=1.0, subtype="FACTOR")
    wall: FloatProperty(name="Wall thickness", default=0.01, min=0.0, unit="LENGTH",
                        description="How thick the container's walls and bottom are")
    shape: EnumProperty(name="Inside shape", items=[("AUTO", "Automatic", ""), ("BOX", "Box", ""),
                                                    ("ROUND", "Round", "")], default="AUTO")

    @classmethod
    def poll(cls, context):
        o = context.active_object
        return o is not None and o.type == "MESH"

    def invoke(self, context, event):
        lo, hi = bounds_world(context.active_object)
        self.wall = max(0.003, min(hi.x - lo.x, hi.y - lo.y) * 0.03)
        return self.execute(context)

    def draw(self, context):
        self.draw_settings(self.layout)
        self.layout.prop(self, "level")
        self.layout.prop(self, "wall")
        self.layout.prop(self, "shape")

    def execute(self, context):
        if self.liquid != "CUSTOM":
            self.apply_preset()
        cont = context.active_object
        lo, hi = bounds_world(cont)
        t = self.wall
        z0 = lo.z + t
        z1 = z0 + (hi.z - z0) * self.level
        round_ = self.shape == "ROUND" or (self.shape == "AUTO" and looks_round(cont))
        bm = bmesh.new()
        cx, cy = (lo.x + hi.x) / 2, (lo.y + hi.y) / 2
        if round_:
            r = (hi.x - lo.x) / 2 - t
            g.add_cylinder(bm, r * 0.998, z1 - z0, (0, 0, (z1 - z0) / 2), segments=64)
        else:
            g.add_box(bm, (hi.x - lo.x - 2 * t, hi.y - lo.y - 2 * t, z1 - z0), (0, 0, (z1 - z0) / 2))
        coll = cont.users_collection[0] if cont.users_collection else context.scene.collection
        obj = g.mesh_object(f"{self.liquid.title()} (still)", bm, coll, location=(cx, cy, z0))
        obj.data.materials.append(self.material())
        for poly in obj.data.polygons:
            poly.use_smooth = round_
        return {"FINISHED"}


class BEN_OT_liquid_pour(LiquidSettings, bpy.types.Operator):
    """Set up a real liquid simulation: liquid falls into the selected container. Then press Simulate"""
    bl_idname = "ben.liquid_pour"
    bl_label = "Pour liquid (simulation)"
    bl_options = {"REGISTER", "UNDO"}

    amount: FloatProperty(name="Amount", default=0.5, min=0.05, max=1.0, subtype="FACTOR",
                          description="How much of the container to fill")
    quality: EnumProperty(name="Quality", items=[
        ("48", "Draft (fast)", ""), ("96", "Good", ""), ("160", "High (slow)", "")], default="96")
    seconds: FloatProperty(name="Length (s)", default=3.0, min=0.5, max=30.0)

    @classmethod
    def poll(cls, context):
        o = context.active_object
        return o is not None and o.type == "MESH"

    def draw(self, context):
        self.draw_settings(self.layout)
        for p in ("amount", "quality", "seconds"):
            self.layout.prop(self, p)

    def execute(self, context):
        if self.liquid != "CUSTOM":
            self.apply_preset()
        scene = context.scene
        cont = context.active_object
        lo, hi = bounds_world(cont)
        size = hi - lo
        coll = g.ben_collection(context, "Simulation")

        # container = solid obstacle
        eff = cont.modifiers.get("Ben fluid") or cont.modifiers.new("Ben fluid", "FLUID")
        eff.fluid_type = "EFFECTOR"
        eff.effector_settings.effector_type = "COLLISION"
        eff.effector_settings.use_plane_init = True   # thin-walled containers stay watertight

        # liquid block above the container, same volume as the requested amount
        h = max(size.z * self.amount, 0.02)
        src_z = hi.z + size.z * 0.1
        centre = ((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, src_z + h / 2)
        if looks_round(cont):
            # a column of liquid narrower than the opening, so nothing spills on the rim
            r = min(size.x, size.y) * 0.32
            bmc = bmesh.new()
            g.add_cylinder(bmc, r, h * 1.6, (0, 0, 0), segments=32)
            src = g.mesh_object(f"{self.liquid.title()} source", bmc, coll, location=centre)
        else:
            src = g.box(f"{self.liquid.title()} source", (size.x * 0.7, size.y * 0.7, h), centre, coll)
        f = src.modifiers.new("Ben fluid", "FLUID")
        f.fluid_type = "FLOW"
        f.flow_settings.flow_type = "LIQUID"
        f.flow_settings.flow_behavior = "GEOMETRY"
        src.display_type = "WIRE"
        src.hide_render = True

        # domain around everything
        top = src_z + h + size.z * 0.1
        dlo = Vector((lo.x - size.x * 0.15, lo.y - size.y * 0.15, lo.z))  # floor = container base, no gap to leak under
        dhi = Vector((hi.x + size.x * 0.15, hi.y + size.y * 0.15, top))
        dom = g.box("Liquid", tuple(dhi - dlo), tuple((dlo + dhi) / 2), coll)
        m = dom.modifiers.new("Ben fluid", "FLUID")
        m.fluid_type = "DOMAIN"
        d = m.domain_settings
        d.domain_type = "LIQUID"
        d.resolution_max = int(self.quality)
        # walls thinner than one simulation cell would leak: thicken them for the solver only
        cell = max(dhi - dlo) / int(self.quality)
        eff.effector_settings.surface_distance = cell * 1.5
        d.use_mesh = True
        d.cache_type = "ALL"
        d.cache_frame_start = scene.frame_start
        d.cache_frame_end = scene.frame_start + int(self.seconds * scene.render.fps)
        d.cache_directory = os.path.join(tempfile.gettempdir(), "ben_cache", dom.name.replace(" ", "_"))
        if self.viscosity > 0:
            d.use_viscosity = True
            d.viscosity_value = 0.05 + self.viscosity * 2.0
        dom.data.materials.append(self.material())
        dom.display_type = "WIRE"
        scene.frame_end = d.cache_frame_end
        for o in context.selected_objects:
            o.select_set(False)
        dom.select_set(True)
        context.view_layer.objects.active = dom
        self.report({"INFO"}, "Ready. Press Simulate (this can take a while)")
        return {"FINISHED"}


def find_domain(context):
    o = context.active_object
    if o is not None:
        for m in o.modifiers:
            if m.type == "FLUID" and m.fluid_type == "DOMAIN":
                return o, m
    for o in context.scene.objects:
        for m in o.modifiers:
            if m.type == "FLUID" and m.fluid_type == "DOMAIN":
                return o, m
    return None, None


class BEN_OT_simulate(bpy.types.Operator):
    """Run the liquid / smoke / fire simulation"""
    bl_idname = "ben.simulate"
    bl_label = "Simulate"

    def invoke(self, context, event):
        dom, _m = find_domain(context)
        if dom is None:
            self.report({"WARNING"}, "Set up a liquid, smoke or fire first")
            return {"CANCELLED"}
        for o in context.selected_objects:
            o.select_set(False)
        dom.select_set(True)
        context.view_layer.objects.active = dom
        return bpy.ops.fluid.bake_all("INVOKE_DEFAULT")

    def execute(self, context):
        dom, _m = find_domain(context)
        if dom is None:
            return {"CANCELLED"}
        context.view_layer.objects.active = dom
        return bpy.ops.fluid.bake_all()


class BEN_OT_clear_sim(bpy.types.Operator):
    """Delete the simulated result so you can change settings and simulate again"""
    bl_idname = "ben.clear_simulation"
    bl_label = "Clear simulation"

    def execute(self, context):
        dom, _m = find_domain(context)
        if dom is None:
            return {"CANCELLED"}
        context.view_layer.objects.active = dom
        return bpy.ops.fluid.free_all()


class BEN_OT_smoke_fire(bpy.types.Operator):
    """Make the selected object emit smoke, fire, or both"""
    bl_idname = "ben.smoke_fire"
    bl_label = "Smoke / fire"
    bl_options = {"REGISTER", "UNDO"}

    kind: EnumProperty(name="Type", items=[("SMOKE", "Smoke", ""), ("FIRE", "Fire", ""), ("BOTH", "Fire + smoke", "")],
                       default="BOTH")
    quality: EnumProperty(name="Quality", items=[("48", "Draft", ""), ("96", "Good", ""), ("160", "High", "")],
                          default="48")
    seconds: FloatProperty(name="Length (s)", default=4.0, min=0.5, max=30.0)

    @classmethod
    def poll(cls, context):
        o = context.active_object
        return o is not None and o.type == "MESH"

    def execute(self, context):
        scene = context.scene
        src = context.active_object
        lo, hi = bounds_world(src)
        size = hi - lo
        s = max(size.x, size.y, 0.3)
        f = src.modifiers.get("Ben fluid") or src.modifiers.new("Ben fluid", "FLUID")
        f.fluid_type = "FLOW"
        f.flow_settings.flow_type = self.kind
        f.flow_settings.flow_behavior = "INFLOW"
        coll = g.ben_collection(context, "Simulation")
        c = (lo + hi) / 2
        dom = g.box("Smoke / fire", (s * 3, s * 3, s * 6), (c.x, c.y, lo.z + s * 3 - 0.05), coll)
        m = dom.modifiers.new("Ben fluid", "FLUID")
        m.fluid_type = "DOMAIN"
        d = m.domain_settings
        d.domain_type = "GAS"
        d.resolution_max = int(self.quality)
        d.cache_type = "ALL"
        d.cache_frame_start = scene.frame_start
        d.cache_frame_end = scene.frame_start + int(self.seconds * scene.render.fps)
        d.cache_directory = os.path.join(tempfile.gettempdir(), "ben_cache", dom.name.replace(" ", "_").replace("/", ""))
        scene.frame_end = d.cache_frame_end
        dom.display_type = "WIRE"
        # simple volume shader; fire glows through blackbody emission
        mat = bpy.data.materials.new("Ben smoke")
        if hasattr(mat, "use_nodes"):
            try:
                mat.use_nodes = True
            except Exception:
                pass
        nt = mat.node_tree
        nt.nodes.clear()
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        vol = nt.nodes.new("ShaderNodeVolumePrincipled")
        vol.inputs["Density"].default_value = 5.0
        if "Blackbody Intensity" in vol.inputs:
            vol.inputs["Blackbody Intensity"].default_value = 1.0 if self.kind != "SMOKE" else 0.0
        nt.links.new(vol.outputs["Volume"], out.inputs["Volume"])
        dom.data.materials.append(mat)
        self.report({"INFO"}, "Ready. Press Simulate")
        return {"FINISHED"}


classes = (BEN_OT_liquid_still, BEN_OT_liquid_pour, BEN_OT_simulate, BEN_OT_clear_sim, BEN_OT_smoke_fire)
