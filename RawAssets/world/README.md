# The world kit

On 2026-10-08 the operator decided to semi-scrap the 3D "world" page of the fleet desk in
`agentchieflou/this-next-please` (a rainy city plaza with one character per AI agent, walked in the
browser with a controller at 100 fps) and move what was learned, and the assets made for it, here, so
the 3D work continues in Unreal Engine where play-sports needs it. This folder is the assets; the
knowledge is in `Specs/Browser_World_Lessons.md`, `Specs/Character_Customization_Spec.md`,
`Specs/Input_Architecture.md` and `Specs/Weather_DayNight_Spec.md`; the open work is Track R
(`roadmap/world-kit.md`, Epics 142–144).

## What is here

| Folder | Files | Size | Made by | Licence |
|---|---|---|---|---|
| `cc0/` | 15 PBR texture maps (brick, painted plaster, concrete slab, asphalt, concrete pavement: `*_diff/nor/arm.webp`), 2 HDR skies (`potsdamer_platz.hdr`, `hansaplatz.hdr`, 512×256 RGBE), 9 scanned props (`fire_hydrant`, `metal_trash_can`, `trashbag`, `cardboard_box_01`, `utility_box_01/02`, `exterior_aircon_unit`, `concrete_road_barrier`, `covered_car` as `.glb`) | 3.9 MB | Poly Haven, shrunk by `tools/assets/world/cc0/` | CC0, credited in `cc0/LICENSE` |
| `people/` | `standin.glb` (the player's character: ~35k triangles, 6 hair styles, beards, glasses, 5 body morphs), `standin_crowd.glb` (5 pedestrians × 2 LODs in one atlas), `people.json` (manifest: look → style/morph map) | 4.2 MB | MakeHuman CC0 assets, built in Blender with MPFB2, packed by `tools/assets/world/people/` | CC0, `people/LICENSE` |
| `trees/` | `trees.glb` (London plane and linden, near/far LODs, leaf atlas) | 1.2 MB | grown by `tools/assets/world/trees/trees.py` (Blender) | this repository's licence, `trees/LICENSE` |
| `cars/` | `cars.glb` (sedan, hatchback, SUV, van; ~3,900 / ~850 triangles per LOD) | 0.4 MB | lofted by `tools/assets/world/cars/cars.py` | this repository's licence, `cars/LICENSE` |
| `office/` | `office.glb` (bench desk, monitor with a separate screen part, keyboard, mouse, mug, chair, planter) | 0.12 MB | built by `tools/assets/world/office/office.py` | this repository's licence, `office/LICENSE` |
| `reference/browser/` | the three.js implementation (eleven scripts with their reasoning notes, the page, its stylesheet), the original user-facing document `WORLD.md`, and the one regression test worth keeping (`regression_black_shapes_pow_nan.py`) | 0.6 MB | this-next-please | this repository's licence |

## Why these matter to play-sports

- **The people are already on the Unreal body skeleton.** `standin.glb`'s joints are the UE bone names
  (`pelvis`, `spine_01..05`, `neck_01/02`, `head`, `clavicle/upperarm/lowerarm/hand_l/r`,
  `thigh/calf/foot/ball_l/r`, 30 finger bones). They retarget onto the engine's Mannequin/MetaHuman
  rigs without a bone map, and the five morph targets (angular, curved, older, slim, broad) are the
  starting point for Epic 58's body-type morphs and Epic 57's parameterised heads.
- **The crowd is the shape Epic 48 needs**: a few characters, two LODs, one atlas, per-instance tints
  by a vertex role attribute (`_ROLE`: 0 skin, 1 top, 2 bottom, 3 hair, 4 shoes, 5 other), designed to be
  skinned from a baked bone-matrix texture and drawn instanced.
- **Trees, cars, props and materials** fill a stadium's surroundings (parking, approaches, concourse
  clutter) for Epic 52's architecture kit, and the two skies are honest overcast references for
  Epic 47's lighting.
- **The office** is not football. It is kept because the "walk up to an agent and take over its
  screen" interaction it was built for is the seed of Track P's in-engine agent presence (the
  Autonomix/AgenticLink bridge, Epic 25), should that ever get a 3D front end.

## Importing into Unreal

**The content pipeline does it now (Epics 142, 146.5).** `tools/assets/world/unreal.mjs` makes these
files engine-ready (PNG textures, dequantised geometry). The pipeline's `world_kit` step then
imports them through Interchange, headlessly on the CI runner, into `Content/Characters/Standin/`,
`Content/Stadium/Kit/` and `Content/Office/`, with each folder's `LICENSE` beside its assets.
`Specs/World_Kit_Import_Spec.md` is the plan. Change a file here and the Content workflow
re-imports it. The notes below are the background the plan was made from:

1. **GLB models** import through Interchange (File → Import, or the `InterchangeImport` commandlet).
   Skinned meshes (`people/*.glb`) come in as Skeletal Mesh + Skeleton; the morph targets import as
   morph targets. Set the import scale to 1 (the files are in metres, Y up, facing -Z, feet on y = 0:
   Interchange converts to Z up).
2. **WebP textures** are not imported by the engine. Convert to PNG first
   (`tools/assets/world/cc0/tex.mjs` can be pointed at a PNG output, or re-fetch the 1k/2k originals
   from Poly Haven with `fetch.mjs`: the originals are CC0 too and give better texel density than the
   browser-sized copies here).
3. **HDR skies** import as HDR cubemaps / Sky Light sources as they are (512×256 is small: re-fetch
   the 2k originals for a real sky).
4. **Packed `.glb` textures** (props, people) are WebP inside the glTF (`EXT_texture_webp`). Interchange
   does not decode WebP: unpack with `gltf-transform` (`npx @gltf-transform/cli copy in.glb out.glb
   --texture-format png` after `webp` → `png` with `sharp`), or re-run the pipeline with PNG output.
   `tools/assets/world/README.md` has the pinned versions.
5. **Quantised geometry** (`KHR_mesh_quantization`, `EXT_meshopt_compression` where used) is read by
   Interchange in 5.3+; if an import comes in empty, dequantise with `gltf-transform dequantize`.
6. Put the results under `Content/Characters/` (people), `Content/Stadium/` (props, trees, cars,
   materials, skies) and `Content/Office/` (office), per `roadmap/PARALLEL.md`'s track scopes.

Keep the `LICENSE` files beside anything you redistribute in a build.
