# Track R — World Kit Import (Epics 142–144)

On 2026-10-08 the operator semi-scrapped the browser "world" of `agentchieflou/this-next-please`
(a rainy 3D plaza with a character per AI agent, walked at 100 fps in a browser) and moved its
assets and knowledge here: `RawAssets/world/` (CC0 and own-made models, textures, skies, the
three.js reference implementation), `tools/assets/world/` (the Blender/Node pipeline that made
them) and four specs (`Specs/Browser_World_Lessons.md`, `Specs/Character_Customization_Spec.md`,
`Specs/Input_Architecture.md`, `Specs/Weather_DayNight_Spec.md`). This track turns that material
into play-sports work. It feeds Track C (47, 48, 52), Track D (57, 58), Epic 22 and Track M, and
depends on little, so it can run in parallel with the MVP tiers. Sizing/mode legend: see
`ROADMAP.md`.

### Epic 142: Import the World Kit into Content

**Size/Mode:** M / mixed
**Goal:** Every file in `RawAssets/world/` that Unreal can use exists as a `Content/` asset with its licence beside it, and the import is repeatable.
**Depends on:** Core 2 (the default map exists to drop things into)

- [ ] Import plan: for each `RawAssets/world/` subfolder, the target `Content/` path, the Interchange settings, and which files need WebP→PNG or dequantising first (`RawAssets/world/README.md` §Importing into Unreal) — written as `Specs/World_Kit_Import_Spec.md` — *code*
- [ ] `tools/assets/world/`: add an `--unreal` output to the `.mjs` packers (PNG textures, no quantisation, full-resolution atlases) so the pipeline makes engine-ready GLB without a hand step; document the Blender → FBX path for the people — *code*
- [ ] Editor pass: import people (Skeletal Mesh + Skeleton + morph targets), trees, cars, office, props, materials and the two HDR skies; commit `Content/Characters/Standin/`, `Content/Stadium/Kit/`, `Content/Office/` with the `LICENSE` files copied alongside — *editor*
- [ ] `Data/input_actions.json` + `UPSInputConfig` reads it through `UPSDataIngestion`; headless test per `Specs/Input_Architecture.md` §4 — *code* (this is the first story of that spec; Track M's Epic 126 consumes it)
- [ ] Automation test: a commandlet or functional test loads each imported asset by soft path and asserts it is not null (proves the import survived a fresh checkout) — *code after the editor pass*

### Epic 143: Character Looks Port

**Size/Mode:** L / mixed
**Goal:** The world's inclusive character picker becomes play-sports' off-field character system: ten looks, eight skin tones, hair textures and coverings, figure/build/age morphs, a wheelchair, no option named for a gender.
**Depends on:** 142, Core 22 (the shared rig), C1 (the bus)

- [ ] `FPSLookRow` / `FPSLookPresetRow` DataTables from `RawAssets/world/people/people.json` and `Specs/Character_Customization_Spec.md` §2–3; `Normalize()` that clamps any record to a valid look — *code*
- [ ] `UPSCharacterLookComponent` (rule 1): applies tints, styles, morphs and the wheelchair flag to a skeletal mesh; publishes `LookChanged` on `UPSTelemetryBus` — *code*
- [ ] Headless tests: ten presets normalise to themselves; a corrupt record normalises; coverage of every row value across the presets; no gendered string in the tables — *code*
- [ ] Build derived from `WeightKg`/`HeightCm` and role on the field (Epic 58's morph axes) and editable only off-field; the row stays a *shape*, never a sex — *code*
- [ ] Editor pass: morph targets and groom/card hair on the stand-in, then the MetaHuman set (diverse, none based on a real person; hair as cards at the crowd LOD) mapped through the same DataTable — *editor*

### Epic 144: Rain, Wet Field and Day/Night Port

**Size/Mode:** L / mixed
**Goal:** A game at any local hour under the world's always-raining sky, with the wet-surface look and the frame-budget policy the browser version proved, as the first slice of Epic 47.
**Depends on:** 142, Core 2

- [ ] `UPSTimeOfDaySubsystem` (rule 1): local-clock hour with a fixed-hour override for tests and screenshots (`Specs/Weather_DayNight_Spec.md` §2); publishes `HourChanged` on the bus; headless test that a fixed hour never reads the wall clock — *code*
- [ ] Weather tuning DataTable (`FWeatherTuningRow`: rain density, wetness, puddle level, lights-per-pixel cap per scalability group) — no magic numbers (rule 4) — *code*
- [ ] Editor pass: Niagara rain lit by the scene, wetness material parameter collection on field and concourse materials, puddle mask, planar/SSR reflections, the two CC0 skies blended by hour, night lights for the stadium approaches from the world's lamp/sign palette — *editor*
- [ ] Adaptive quality policy from `Specs/Browser_World_Lessons.md` §2 (resolution first, tier second, warm-up grace, hysteresis) expressed as scalability settings + a `PSPerf` budget test that CI can run with `-nullrhi` for the policy's arithmetic — *code* (Track K's Epic 115 owns the frame-time harness; coordinate)
- [ ] Reduced-motion path: rain slows, cameras cut instead of swing (Epic 103 reads the setting) — *code*
