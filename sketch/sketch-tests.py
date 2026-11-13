# Sketch - Laurence Who Studio
# SPDX-License-Identifier: GPL-3.0-or-later
"""Load the Sketch application template in Blender and run its geometry tests.

    blender --background --factory-startup --python sketch/sketch-tests.py

Prints SKETCH OK when the template loads and SKETCH TESTS PASSED when every
test passes; exits with 1 otherwise.
"""

import importlib
import sys
import traceback

import bpy
import bl_app_template_utils


def main():
    try:
        bl_app_template_utils.activate(template_id="sketch")
    except Exception:
        traceback.print_exc()
        print("SKETCH FAILED TO LOAD")
        return 1
    mod = None
    for name, m in list(sys.modules.items()):
        if (name == "sketch" or name.endswith(".sketch")) and hasattr(m, "tools") and hasattr(m, "sk_geom"):
            mod = name
            break
    if mod is None:
        print("SKETCH FAILED TO LOAD (module not found)")
        return 1
    ops = [o for o in dir(bpy.ops.sketch)]
    print("SKETCH OK", bpy.app.version_string, len(ops), "tools", mod)
    stage = importlib.import_module(mod + ".tests_stage")
    bad = stage.run()
    tests = importlib.import_module(mod + ".tests_geometry")
    bad += tests.run()
    return 1 if bad else 0


code = main()
sys.stdout.flush()
if code:
    sys.exit(code)
