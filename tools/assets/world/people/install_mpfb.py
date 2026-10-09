"""Install MPFB 2 (MakeHuman's Blender extension) into the `bpy` in `bvenv/`, from `dl/`.

Usage: bvenv/bin/python install_mpfb.py   (once, before specs.py; see README.md)
MPFB's code is GPL-3.0 and is only run as a tool: none of it is in the files the world loads.
"""
import bpy, os, sys, addon_utils
here = os.path.dirname(os.path.abspath(__file__))
zipf = os.path.join(here, "dl", "mpfb2-20260911.zip")  # from README.md, "The stand-in"
print("repos", [r.module for r in bpy.context.preferences.extensions.repos])
r = bpy.ops.extensions.package_install_files(filepath=zipf, repo="user_default", enable_on_install=True)
print("install", r)
bpy.ops.wm.save_userpref()
print([m.__name__ for m in addon_utils.modules() if "mpfb" in m.__name__])
print(bpy.utils.extension_path_user("bl_ext.user_default.mpfb"))
