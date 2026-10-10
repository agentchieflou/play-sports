# Specification: World Kit Import (Epics 142, 146.5)

How `RawAssets/world/` gets into `Content/`. Nobody imports by hand. The content pipeline's
`world_kit` step does it headlessly on the CI runner, and the result is committed through Git LFS
(`Specs/ADR_Content_Pipeline.md`). This file is the import plan: what goes where, what has to be
converted first, and the importer's settings.

## 1. Two stages

1. **Make it engine-ready** (`tools/assets/world/unreal.mjs`, Epic 142.2). The files in
   `RawAssets/world/` are tuned for a browser. Interchange can't read two things in them:
   - WebP textures (standalone, and packed in the `.glb` files as `EXT_texture_webp`);
   - quantised geometry (`KHR_mesh_quantization` in the people).

   The converter re-encodes every texture as PNG, dequantises positions, normals and UVs to
   floats, decodes any meshopt-compressed buffers and drops those extensions. Nothing else
   changes: nodes, names, skins, morph targets and materials stay as they are. It copies `.hdr`
   skies and each folder's `LICENSE` as they are. The Content workflow runs it into
   `Saved/WorldKit` (it is never committed). With the versions pinned in
   `tools/assets/world/package.json` the output is byte-identical from run to run.
2. **Import** (`tools/content_pipeline/steps/world_kit.py`, Epic 146.5). Each file goes through
   the editor's importer (`AssetImportTask`, automated, saved): Interchange for glTF, and the
   texture and HDR importers.

## 2. What goes where

| Source (`RawAssets/world/`) | Converted | Imported to | What it makes |
|---|---|---|---|
| `people/standin.glb` | WebP→PNG, dequantised | `/Game/Characters/Standin` | The stand-in: a skeletal mesh on the Unreal body bone names, its skeleton, 90 morph targets, 18 materials and their textures |
| `people/standin_crowd.glb` | WebP→PNG, dequantised | `/Game/Characters/Standin/Crowd` | The crowd: ten meshes on five skins, on the same bones |
| `cc0/<prop>.glb` (9) | WebP→PNG | `/Game/Stadium/Kit/Props/<prop>` | One static mesh, one material and three textures each. One folder per prop, so names never collide |
| `cc0/<material>_{diff,nor,arm}.webp` (5 sets) | →PNG | `/Game/Stadium/Kit/Textures` | Tiling textures. `_nor` is linear with normal-map compression; `_arm` (occlusion, roughness, metalness) is linear with mask compression |
| `cc0/*.hdr` (2) | copied | `/Game/Stadium/Kit/Skies` | The two CC0 skies, for a sky light |
| `trees/trees.glb` | WebP→PNG | `/Game/Stadium/Kit/Trees` | The grown trees: meshes, their card and bark materials |
| `cars/cars.glb` | none (no textures) | `/Game/Stadium/Kit/Cars` | The procedural cars |
| `office/office.glb` | none (no textures) | `/Game/Office` | The office furniture |
| each folder's `LICENSE` | copied | beside its assets (`Content/Characters/Standin/LICENSE`, `Content/Stadium/Kit/LICENSE`, `.../Trees/LICENSE`, `.../Cars/LICENSE`, `Content/Office/LICENSE`) | The provenance of every file, kept with what came from it |

`RawAssets/world/reference/` is the three.js reference implementation, not assets. It is never
imported.

## 3. Importer settings

- **Interchange's defaults for glTF**, automated, with nothing overridden:
  - glTF is in metres and Y up; Interchange converts it to centimetres and Z up, so the scale is 1;
  - a skinned mesh comes in as a skeletal mesh with a skeleton, with its morph targets;
  - materials come in as Interchange's glTF material instances (its default; to be confirmed from the first import).
- **Re-imports replace.** The step owns `/Game/Characters/Standin`, `/Game/Stadium/Kit` and
  `/Game/Office` whole (`pipeline.json` Outputs). When anything it reads changes, it deletes those
  folders and imports everything again, so nothing stale survives. Otherwise it imports nothing; it
  only checks that every asset it made still loads.
- **Its sources** (what the drift check fingerprints) are the five imported folders of
  `RawAssets/world/`, `unreal.mjs` and the tools' lockfile.

## 4. Known limits

- **Web-sized textures.** The props' maps are 512 px and the tiling textures 1024 px, as the
  browser had them. For better texel density, re-fetch the 1k/2k Poly Haven originals
  (`tools/assets/world/cc0/fetch.mjs`) and re-pack them at full size, or export from the Blender
  scripts at full resolution (`tools/assets/world/README.md`). Then the step imports those instead.
  That needs Blender and network access, so it happens on the owner's machine.
- **Materials are importer defaults.** The CC0 tiling sets are imported as textures only. The
  stadium's materials that use them come with Epic 147.1.
- **Packaging.** The glTF material instances' parents live in the engine's Interchange content. A
  packaged build has to cook them; Epic 145's packaging run is where that is checked.
- **Looks.** How anything looks is a human check in the editor or a packaged build.

## 5. Verification

- The converter: run locally on 2026-10-10. Every output `.glb` has PNG textures only and no
  WebP, quantisation or meshopt extension, and a second run is byte-identical.
- The import: the Content workflow's run, its `generated-content` artifact, and
  `PlaySports.Content.WorldKit` (Epic 142.5). That test loads every imported asset by its soft path
  and checks the stand-in is a skeletal mesh with morph targets.
