# Browser World Lessons (imported knowledge, 2026-10-08)

What `agentchieflou/this-next-please` learned building a walkable, rainy 3D city in a browser at
100 fps (its fleet desk's `/world` page, Sept–Oct 2026), written down for the Unreal work here.
The measured numbers are a browser's on one laptop; the *techniques* and the *budgets' shape* carry
over. The implementation itself is in `RawAssets/world/reference/browser/` (read its `README.md`);
the assets are in `RawAssets/world/`; the open work is Track R (`roadmap/world-kit.md`).

## 1. Budgets, measured

The page's frame budget was 10 ms (100 fps), chosen by the operator because full games run in a
browser at 120 fps. Measured on the operator's laptop (NVIDIA RTX 3050 Ti Laptop GPU 4 GB, Chromium,
1600×900, the `high` tier, held at full resolution):

| Scene | Frame | Draw calls | Triangles |
|---|---|---|---|
| city, plaza, robots, rain | 2.5 ms | 107 | 1.32 M |
| + grown trees (near 3–5k tris, far ⅓) | 2.7–2.8 ms | 131–135 | 1.43 M |
| + lofted cars (near ~3.9k, far ~850) | 3.1 ms | 147–163 | — |
| + MetaHuman hero (56k tris, 6.3 MB, 9 parts) and 5-character crowd | 2.6–2.9 ms (ultra/high/medium) | 107–111 | 1.46 M |

About 2.5 ms of the frame was the page's own CPU work, not the GPU. Lesson for UE: **draw calls and
CPU per frame, not triangle count, were the first wall**; everything that scales with the number of
agents (characters, labels, screens) had to be instanced or merged so draw calls stayed flat.

On a software renderer (SwiftShader in CI, a virtual desktop without a GPU) the lightest path was
held to **≤ 64 draw calls and < 400,000 triangles**: that is the shape of a "null RHI / CI" budget
this project's `-nullrhi` automation runs can borrow if a visual test ever needs numbers.

Load time, same machine, ten agents (the "click a link → first real frame" measurement):

| | before | after |
|---|---|---|
| link hovered ~2 s first (prerendered) | 2.6 s | ~0.1 s |
| click straight away | 2.6 s | 1.2–1.4 s |
| very first load of a browser profile (cold shader cache) | 7.1 s | ~4 s |

What fixed it: cache what was refetched (ETag/304), **compile shaders for the render target they are
drawn into** (compiling for the screen then redrawing into an HDR target compiled ~50 programs twice),
warm up by *time* (draw as many objects as fit in 8 ms a frame) not one object a frame, and prerender
behind the previous page. UE's equivalent is PSO caching and a loading-screen warm-up; the lesson is
to measure "click to first real frame" and attack the largest term first.

## 2. Techniques worth reproducing

- **Four quality tiers that draw the same scene at different cost** (low/medium/high/ultra:
  MSAA 0/0/4/4, AO off/half/half/full, mirror off/⅓/½/0.6, lights per pixel 8/16/24/32), chosen from
  the GPU's name (software → low, integrated → medium, discrete → high; ultra only when asked). Then
  **adaptive resolution**: once a second compare the median frame with the display's rate (or the
  10 ms budget on a display faster than 100 Hz), step render resolution down to half at worst, drop a
  tier only after that and never in the first 15 s after warm-up (late shader compiles make frames
  long), step back up when frames run under half the target. UE has `r.DynamicRes` and scalability
  groups; the lesson is the *policy*: resolution first, tier second, hysteresis, and a warm-up grace.
- **The frame pipeline that reads as "AAA"** was not polygons: HDR scene colour, a planar reflection
  of the scene in the wet ground (sharp in puddles, smeared on asphalt by roughness picking a mip),
  ambient occlusion in creases, bloom round every light, ACES tone curve, a grade, grain to hide
  banding in fog. No shadow maps at all: under overcast cloud and rain, occlusion was the shadow.
- **Hundreds of lights, a pixel adds the nearest few** (clustered/forward+ by hand). Lamps, signs,
  neon, billboards, headlights each light their surroundings in their own colour at night.
- **Draw calls do not grow with the fleet**: the city merged by material (~12 calls for ~150
  buildings), robots/cars/people/props instanced (one call per kind). Labels were page text, not
  textures, so they stayed sharp. The agents' monitor screens were one shared 8×8 atlas texture
  redrawn only when an agent's row changed.
- **Interior mapping**: a room behind every window drawn by the glass itself (walls, floor, ceiling,
  furniture from a cubemap-like parallax), some lit at night, some flickering like a television.
- **Crowd skinning in the vertex shader** from a float texture of baked bone matrices, instanced, a
  mesh per character and LOD (detailed within 24 m), per-instance tints by a `_ROLE` vertex
  attribute, walk cycle baked at N poses and blended between two, breathing baked over exactly one
  breath so the loop has no seam. Six motions: walk with umbrella, stand, present, sit, walk with
  free arms, type at a desk. UE's Animation-to-Texture / vertex animation textures are the same idea.
- **Procedural locomotion on the UE body skeleton with no animation files**: every limb posed by
  *frames* (an aim and a pole), never Euler angles, so the animation does not care how an exporter
  rolled its bones or whether the rest pose is an A or a T. Idle (breathing, slow weight shift),
  walk→jog→run with speed, step round on a turn in place, "present" (one arm out, palm up) when
  talking, sit in a wheelchair pushing the rims. Legs swing with distance walked so feet never slide.
  A pipeline folded MetaHuman's ~800 facial/twist/corrective bones into their nearest kept parent
  (four influences per vertex) so one rig served MakeHuman and MetaHuman exports alike.
- **Skin in a cheap shader**: a wrapped diffuse term `(N·L + 0.45) / 1.45` minus plain `N·L`,
  coloured blood-red and weighted by surface colour, adds light only near and past the terminator;
  thin parts (ears, fingers) take light from behind (`-N·L`) weighted by a thinness map packed into
  the unused metalness channel. Hair cards cap their grazing reflection (`specularF90` ≈ ¼); lashes
  reflect nothing. UE's Subsurface Profile and hair shading make this moot, but the *artifacts* it
  fixed (plastic skin, grey-helmet hair, white-spark lashes under a bright wet sky) are the ones to
  check for in any custom material.
- **Detail maps with the mean colour normalised**, so one texture takes every skin tone and clothing
  colour the picker offers (dye at runtime, no texture per colour).
- **GPU-baked procedural materials** (stone, granite, metal, roofing, wood, leaves) made at load;
  only the five photographed PBR sets and the props were files.
- **Trees grown from numbers** (leaf outline, lobes, teeth, veins → twig clusters rendered into an
  atlas → trunk with root flare, scaffold limbs, branches, twig cards spread through the crown's outer
  shell, sky visibility baked into vertex colour) and **cars lofted from side/top/end profiles**, both
  as Blender scripts, so every asset is the repository's own and can be regrown at any resolution.
- **Reduced motion is a first-class path**: robots hold still, rain slows, time runs at 0.35×, the
  walk snaps instead of easing. Map to UE's accessibility settings (Epic 103).
- **Stop drawing when hidden**; measure `fps`, `frameMs`, `workMs`, `calls`, `triangles`, `passes`
  through one `inspect()` so tests hold budgets without reading pixels.

## 3. The one bug to remember

`pow(x, y)` with a negative base is undefined in GLSL and NaN on real GPUs (`exp2(y·log2(x))`) while
SwiftShader happened to return a number, so CI never saw it. One NaN pixel entered the HDR target,
bloom's mip chain spread it into blocks a sixty-fourth of the screen wide, and the grade drew them
black: "two enormous near-black flat-shaded shapes" over the plaza, reported by the operator on a
real GPU. Fix: clamp every fade to 0..1 before `pow`; then a test that holds every `pow` in the
scripts to a non-negative base. In UE materials: `Power` with a `Saturate` or `Max(0)` on the base,
always, and a null-RHI run proves nothing about this class of bug.

## 4. Licensing, learned the hard way

- **Twinmotion** EULA §1.2 and Unreal's "UE-only" content may be used only inside Unreal Engine,
  Twinmotion, RealityCapture and UEFN, and to export images and video. They could not be drawn by a
  web page. **In play-sports, an Unreal project, that restriction does not bind**: Quixel/Fab UE-only
  content and Twinmotion's library are fair game for `Content/` (not for `RawAssets/`, which is
  redistributable source).
- **Poly Haven is CC0**, credited anyway per asset and author; fetched through its API with the md5
  it publishes. **MakeHuman's** system assets and the `bodyparts05` pack are CC0 (verified on the
  project's own licence pages; sha256 of the zips recorded in `RawAssets/world/people/LICENSE`).
- **MetaHuman** (post June 2025 licence): usable in other engines, commercially, royalty-free; seat
  licences apply above $1M/yr revenue; **redistribution of raw exports in a public repository was
  the unanswered question**, so they were kept out of git; may not be used to train AI. In an Unreal
  project the exports live in `Content/` like any other Epic asset, which answers the redistribution
  question for play-sports but not for anything exported out of it.
- Blender is only a tool: its GPL does not reach what it renders.

## 5. What the browser world was not, and what UE gives for free

No physics, no collision with cars, no entering buildings, no on-screen keyboard for a controller,
no voice or facial animation, no multiplayer. Unreal provides physics, navigation, Chaos cloth,
MetaHuman animation, Enhanced Input and networking; the lessons above are about *budgets, policy and
the look*, which no engine provides.
