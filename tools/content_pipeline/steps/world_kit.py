"""Step world_kit: the world kit imported into Content/ (Epic 146.5, closes Epic 142's editor pass).

The Content workflow first makes RawAssets/world's browser-sized files engine-ready
(tools/assets/world/unreal.mjs: PNG textures, dequantised geometry) into Saved/WorldKit, or the
folder PS_WORLD_KIT_DIR names. This step imports them through the editor's importer (Interchange
for glTF; textures and HDR skies), as Specs/World_Kit_Import_Spec.md plans:

  people/standin.glb        /Game/Characters/Standin        skeletal mesh, skeleton, morph targets
  people/standin_crowd.glb  /Game/Characters/Standin/Crowd  the crowd's skinned meshes
  cc0/*.glb                 /Game/Stadium/Kit/Props/<name>  CC0 props, one folder each
  cc0/*.png                 /Game/Stadium/Kit/Textures      CC0 tiling materials' maps
  cc0/*.hdr                 /Game/Stadium/Kit/Skies         CC0 skies
  trees/trees.glb           /Game/Stadium/Kit/Trees
  cars/cars.glb             /Game/Stadium/Kit/Cars
  office/office.glb         /Game/Office

Each folder's LICENSE is copied beside what came from it. Normal maps (*_nor) and the packed
occlusion/roughness/metalness maps (*_arm) are set to linear, with normal-map and mask compression.

An import can't be compared piece by piece the way a level can, so the step re-imports only when
its sources changed (or an output is missing or changed): it deletes the folders it owns and
imports everything again. Otherwise it only checks that every asset it made still loads. So a
second run changes nothing.
"""

import glob
import os
import shutil

import unreal

import ue_content as content

PLAN = [
    ("people/standin.glb", "/Game/Characters/Standin"),
    ("people/standin_crowd.glb", "/Game/Characters/Standin/Crowd"),
    ("cc0/*.glb", "/Game/Stadium/Kit/Props/{stem}"),
    ("cc0/*.png", "/Game/Stadium/Kit/Textures"),
    ("cc0/*.hdr", "/Game/Stadium/Kit/Skies"),
    ("trees/trees.glb", "/Game/Stadium/Kit/Trees"),
    ("cars/cars.glb", "/Game/Stadium/Kit/Cars"),
    ("office/office.glb", "/Game/Office"),
]

# The folders the step owns whole (pipeline.json's Outputs), and where each LICENSE goes.
ROOTS = ["/Game/Characters/Standin", "/Game/Stadium/Kit", "/Game/Office"]
LICENSES = {
    "people": "Characters/Standin",
    "cc0": "Stadium/Kit",
    "trees": "Stadium/Kit/Trees",
    "cars": "Stadium/Kit/Cars",
    "office": "Office",
}


def kit_dir(ctx):
    return os.environ.get("PS_WORLD_KIT_DIR") or os.path.join(ctx.repo, "Saved", "WorldKit")


def set_texture_look(ctx, package):
    """Normal and mask maps hold data, not colour: linear, with the matching compression."""
    name = package.rsplit("/", 1)[-1].lower()
    if name.endswith("_nor"):
        settings = unreal.TextureCompressionSettings.TC_NORMALMAP
    elif name.endswith("_arm"):
        settings = unreal.TextureCompressionSettings.TC_MASKS
    else:
        return
    texture = unreal.EditorAssetLibrary.load_asset(package)
    if not isinstance(texture, unreal.Texture):
        raise RuntimeError(f"{package} didn't import as a texture.")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", settings)
    if not unreal.EditorAssetLibrary.save_loaded_asset(texture, False):
        raise RuntimeError(f"Couldn't save {package}.")
    ctx.change(f"{package}: linear, {settings}")


def build(ctx):
    if not ctx.drift:
        # Imported from these very sources already: make sure it all still loads.
        content.check_assets_load(ctx, ctx.recorded_outputs)
        return

    for problem in ctx.drift:
        unreal.log(f"[ContentPipeline] world_kit re-imports: {problem}")
    if ctx.dry_run:
        ctx.change("re-import the world kit: " + "; ".join(ctx.drift))
        return

    source = kit_dir(ctx)
    if not os.path.isdir(source):
        raise RuntimeError(f"No engine-ready world kit at {source}: run tools/assets/world/unreal.mjs "
                           "(the Content workflow does, before the editor) or set PS_WORLD_KIT_DIR.")

    for root in ROOTS:
        content.delete_folder(ctx, root)

    for pattern, destination in PLAN:
        files = sorted(glob.glob(os.path.join(source, *pattern.split("/"))))
        if not files:
            raise RuntimeError(f"Nothing matches {pattern} in {source}.")
        for filename in files:
            target = destination.replace("{stem}", os.path.splitext(os.path.basename(filename))[0])
            imported = content.import_file(ctx, filename, target)
            ctx.change(f"import {os.path.relpath(filename, source)} -> {target}: {len(imported)} asset(s)")
            if pattern.endswith(".png"):
                for path in imported:
                    set_texture_look(ctx, path.split(".", 1)[0])

    content_dir = os.path.join(ctx.repo, "Content")
    for folder, target in LICENSES.items():
        licence = os.path.join(source, folder, "LICENSE")
        if not os.path.isfile(licence):
            raise RuntimeError(f"{folder} has no LICENSE in {source}.")
        destination = os.path.join(content_dir, *target.split("/"))
        os.makedirs(destination, exist_ok=True)
        shutil.copyfile(licence, os.path.join(destination, "LICENSE"))
        ctx.change(f"copy {folder}/LICENSE -> Content/{target}/LICENSE")
