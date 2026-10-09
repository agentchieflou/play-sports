"""Make one MakeHuman (CC0) character in Blender with MPFB2 and export it as a raw GLB.

Usage: bvenv/bin/python mh_make.py spec.json out.glb  (specs.py runs it for every character; see README.md)

The spec names the macros (gender, age, muscle, weight, height, proportions, cupsize, firmness,
race), the rig (game_engine: Unreal Engine bone names) and the assets by their folder under the
MakeHuman system assets: eyes, eyebrows, eyelashes, clothes (several), hair (several, each kept as
its own mesh). Every asset is CC0 (makehuman_system). Materials are rebuilt as plain Principled
BSDFs with the asset's own textures, named by role so the web pipeline can sort them.
The body's faces hidden by clothes are deleted (each asset's own delete group), shape keys are
baked, and the result is a skinned GLB in metres, Y up, facing +Z (Blender -Y).
"""
import bpy, sys, json, os, addon_utils

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
spec = json.load(open(argv[0]))
out = argv[1]

addon_utils.enable("bl_ext.user_default.mpfb", default_set=True)
from bl_ext.user_default.mpfb.services.humanservice import HumanService
from bl_ext.user_default.mpfb.services.targetservice import TargetService
from bl_ext.user_default.mpfb.services.locationservice import LocationService

DATA = LocationService.get_user_data()

bpy.ops.wm.read_factory_settings(use_empty=True)

macros = TargetService.get_default_macro_info_dict()
for k, v in spec.get("macros", {}).items():
    if k == "race":
        macros["race"].update(v)
    else:
        macros[k] = v

basemesh = HumanService.create_human(mask_helpers=True, detailed_helpers=True, extra_vertex_groups=True,
                                     feet_on_ground=True, scale=0.1, macro_detail_dict=macros)
for name, w in spec.get("targets", {}).items():
    TargetService.load_target(basemesh, os.path.join(LocationService.get_mpfb_data("targets"), name), weight=w)
rig = HumanService.add_builtin_rig(basemesh, spec.get("rig", "game_engine"))


def mhclo(kind, name):
    folder = os.path.join(DATA, kind, name)
    for f in os.listdir(folder):
        if f.endswith(".mhclo") or f.endswith(".proxy"):
            return os.path.join(folder, f)
    raise IOError(folder)


def mhmat(path):
    out = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        bits = line.strip().split(None, 1)
        if len(bits) == 2 and bits[0] in ("diffuseTexture", "normalmapTexture", "transparent", "name", "diffuseColor"):
            out[bits[0]] = bits[1]
    return out


def material(obj, role, matfile):
    info = mhmat(matfile) if matfile else {}
    mat = bpy.data.materials.new(role)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    bsdf.inputs["Roughness"].default_value = 0.6
    tex = info.get("diffuseTexture")
    if tex:
        p = os.path.join(os.path.dirname(matfile), tex)
        if not os.path.isfile(p):
            p = os.path.join(os.path.dirname(matfile), os.path.basename(tex))
        img = bpy.data.images.load(p)
        node = nt.nodes.new("ShaderNodeTexImage")
        node.image = img
        nt.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
        if role in ("hair", "brows", "lashes", "beard", "glasses"):
            nt.links.new(node.outputs["Alpha"], bsdf.inputs["Alpha"])
            mat.blend_method = "CLIP" if hasattr(mat, "blend_method") else None
    nor = info.get("normalmapTexture")
    if nor:
        p = os.path.join(os.path.dirname(matfile), nor)
        if os.path.isfile(p):
            img = bpy.data.images.load(p)
            img.colorspace_settings.name = "Non-Color"
            node = nt.nodes.new("ShaderNodeTexImage")
            node.image = img
            nm = nt.nodes.new("ShaderNodeNormalMap")
            nt.links.new(node.outputs["Color"], nm.inputs["Color"])
            nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    obj.data.materials.clear()
    obj.data.materials.append(mat)


def add(kind, name, role, asset_type):
    path = mhclo(kind, name)
    obj = HumanService.add_mhclo_asset(path, basemesh, asset_type=asset_type, subdiv_levels=0, material_type="MAKESKIN")
    obj.name = role + "." + name
    mats = [f for f in os.listdir(os.path.dirname(path)) if f.endswith(".mhmat")]
    material(obj, role, os.path.join(os.path.dirname(path), mats[0]) if mats else None)
    return obj


if spec.get("eyes"):
    eyes = add("eyes", spec["eyes"], "eyes", "Eyes")
    em = os.path.join(DATA, "eyes", "materials", spec.get("eyeColour", "brown") + ".mhmat")
    if os.path.isfile(em):
        material(eyes, "eyes", em)
if spec.get("eyebrows"):
    add("eyebrows", spec["eyebrows"], "brows", "Eyebrows")
if spec.get("eyelashes"):
    add("eyelashes", spec["eyelashes"], "lashes", "Eyelashes")
for c in spec.get("clothes", []):
    add("clothes", c["name"], c["role"], "Clothes")
    if c.get("keepBody"):
        for m in list(basemesh.modifiers):
            if m.type == "MASK" and m.name == "Delete." + c["name"]:
                basemesh.modifiers.remove(m)
for h in spec.get("hair", []):
    add("hair", h, "hair", "Hair")

skin = os.path.join(DATA, "skins", spec.get("skin", "young_caucasian_male"))
skinmat = [f for f in os.listdir(skin) if f.endswith(".mhmat")][0]
basemesh.name = "skin.body"
material(basemesh, "skin", os.path.join(skin, skinmat))

bpy.ops.object.select_all(action="DESELECT")
for obj in [o for o in bpy.data.objects if o.type == "MESH"]:
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    if obj.data.shape_keys:
        bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
    for m in list(obj.modifiers):
        if m.type in ("MASK", "SUBSURF"):
            bpy.ops.object.modifier_apply(modifier=m.name)
    obj.select_set(False)

body = bpy.data.objects["skin.body"]
for vg in list(body.vertex_groups):
    if vg.name not in {b.name for b in rig.data.bones}:
        body.vertex_groups.remove(vg)

bones = {b.name: [round(x, 6) for x in (rig.matrix_world @ b.head_local)] for b in rig.data.bones}
meta = {"macros": macros, "bones": bones,
        "meshes": {o.name: len(o.data.vertices) for o in bpy.data.objects if o.type == "MESH"}}
json.dump(meta, open(out + ".meta.json", "w"), indent=1)

bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", use_selection=False, export_skins=True,
                          export_morph=False, export_animations=False, export_apply=True, export_yup=True,
                          export_texcoords=True, export_normals=True, export_tangents=False,
                          export_materials="EXPORT", export_image_format="AUTO", export_def_bones=False,
                          export_extras=True)
print("WROTE", out, meta["meshes"])
sys.stdout.flush()
os._exit(0)
