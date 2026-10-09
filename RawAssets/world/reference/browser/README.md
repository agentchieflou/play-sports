# The browser world, as a reference

The three.js implementation of the fleet desk's `/world` page, copied here unchanged on 2026-10-08
from `agentchieflou/this-next-please` (`agentdata/fleet/static/world/`). **It is not built, served or
tested in this repository.** It is kept because the techniques in it are the ones the Unreal work
should reproduce, and because each `<file>.js.md` is the reasoning behind its script (a `###` heading
per function or declaration, in source order), which is the knowledge the operator asked to keep.

Start with `WORLD.md`, the page's user-facing document: controls, the character, the agents, the
place, rain and day/night, the frame budget, loading, and the MetaHuman decisions. Then, by topic:

| Topic | Read |
|---|---|
| the frame pipeline: HDR, mirror reflection of the wet street, AO, bloom, ACES, quality tiers, adaptive resolution | `render.js.md`, `world.js.md` §`vTune`, §`vMaxScale` |
| skinned player character, procedural locomotion on UE bone names (idle, walk→jog→run, turn in place, present, sit, wheelchair), no animation files | `people.js.md` §`rig`, §`frameQ`, §`posture`; `hero.js.md` §`pose` |
| instanced crowd skinned in the vertex shader from a baked bone-matrix texture, per-instance tints by `_ROLE` | `people.js.md` §`CROWD`, §`FRAMES`, §`MOTIONS` |
| skin scattering and thin-part transmission in a cheap shader | `people.js.md` §`SCATTER`, §`THIN`, §`BEHIND` |
| hair cards: capped grazing reflection, matte lashes | `people.js.md` §`CARDS`, §`GRAZE`, §`MATTE` |
| the look picker: eight skin tones, hair textures and coverings, no option named for a gender | `hero.js.md` §`SKIN` … §`PRESETS` |
| the city: merged-by-material buildings, interior mapping behind windows, shopfronts, roofs | `city.js.md` |
| streets: lamps and their light cones, traffic that stops at red, walkers with umbrellas, props instanced per kind | `street.js.md` |
| rain: 6,000 lit streaks, splash rings, puddles in the low spots; day/night from the local clock | `world.js.md` §`vRain`, §`vSplash`, §`vWeather`, §`V_DAY` |
| many lights per pixel (the nearest N of hundreds) | `kit.js.md` |
| GPU-baked materials (stone, granite, metal, roofing, wood, leaves) at load | `bake.js.md` |
| the agents' desks and the SVG screen atlas redrawn only on change | `scenery.js.md` |
| warm-up (compile for the target the scene is drawn into; draw as much as fits in 8 ms a frame) and prerender on hover | `world.js.md` §`vPrewarm`, §`vWarm`; `WORLD.md` §Loading |
| the one bug worth remembering: `pow()` of a negative base is NaN on a GPU, and bloom spreads one NaN pixel into a black screen | `regression_black_shapes_pow_nan.py` |

Where the notes cite issue numbers (`#400`, `#626`, `#632` …) they are this-next-please's issues.
