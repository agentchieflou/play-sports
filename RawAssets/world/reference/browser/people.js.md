# `world/people.js`

The reasoning for this script, kept out of the source (decisions 18 and 19 on #429). The source keeps
its code and the JSDoc types `tsc` reads; the server strips every comment from what it serves
(`agentdata/fleet/strip.py`).

A `###` heading is the declaration or statement the notes sit in or above, in source order.

### The file

The world's people: the player's character as a skinned human, and the pedestrians as an instanced,
skinned crowd. Poly Haven has no people, and the operator asked for the same pass for them: *"Well then
we need to kick off a separate background agent to accomplish the same task Poly Haven is doing but for
people"* (2026-10-03), with the bar *"far cry 3 / rdr2 / gta 6 / cyberpunk quality world ... that can
run in browser"*, and the standing requirement *"Create diverse character options so everyone feels
included"*. The realistic people are to be MetaHumans the operator exports from Unreal Engine; until
then a CC0 stand-in built from MakeHuman's assets proves the whole path (`static/world/people/LICENSE`).

Nothing here is tied to one character. The files come from `people/people.json` (read by
`WorldAssets.people`), and the code reads only what the world's people pipeline writes into a binary
glTF: a skin whose joints carry Unreal Engine body bone names, primitives whose materials say their
role (skin, top, bottom, shoes, hair, brows, lashes, beard, glasses, eyes) and their tone, optional
morph targets with the joint and anchor offsets that go with them, and optional animation clips.
Dropping MetaHuman exports in is a change to the manifest and the files.

When anything is missing or fails to load, `ready` is false and the world keeps its procedural people
(`world/hero.js`'s character, `world/street.js`'s walkers). A classic script after `world/assets.js`;
it defines one global, `WorldPeople`.

### `var D`

The people's data: the parsed hero and crowd files, and from the manifest the look-to-style maps
(which hair, beard or glasses file a look's `hair`, `face` or `glasses` value names), the morph
weights each `figure`, `build` and `age` value sets, an optional idle clip, and what each hero file
fits (`fits`: a MetaHuman set picks the export closest to the look).

### `var S`

What is made once and kept: textures and materials by the part they belong to (a picker change
rebuilds the character, never a texture upload or a shader compile), the fill uniform the world sets
with the daylight, and the crowd.

### `var SCATTER`

Skin is not plastic: light enters it and comes out a little further on, reddened, so a lit face has a
soft, warm edge where the light falls away instead of a hard line. A full subsurface model costs far
more than a frame here allows, so skin replaces three.js's direct-light function with itself plus a
wrapped diffuse term (`(N·L + 0.45) / 1.45`, less the ordinary `N·L`), coloured blood-red and
weighted by the surface colour. It adds light only near and past the terminator, costs a few
instructions a light, and touches no other material. The kit's light loop calls `RE_Direct`, so the
city's own lights scatter too.

Where skin is thin (an ear, a finger) light also comes through it: a light behind the surface
(`-N·L`) adds its share, weighted by `pkThin` and coloured `PK_THROUGH`, what is left of daylight
after a few millimetres of flesh (red keeps, blue goes). `pkThin` stays 0 unless the part's map says
otherwise (`THIN`), so a skin without a thinness map draws as before. The world casts no shadow maps,
so nothing darkens an ear's front face when the light is behind it; were shadows added, this term would
need the light unshadowed, or it would vanish exactly where it shows.

`pkSkin` gates the wrapped term: 1 for a skin material, set per fragment where one material draws skin
and cloth together (the crowd, `CROWD`).

### `var CARDS`

The roles drawn as alpha-tested cards (strands painted on flat strips). A card's one normal stands for
a tuft of hair, so at a grazing view it reflects as a sheet: under the city's bright, rain-wet sky a
hair cap read as a grey helmet and the lashes as white sparks along the lid.

### `var GRAZE`

Hair, brows and beard keep their sheen but cap the grazing reflection (`specularF90`) at a quarter, the
most a tuft of real strands, which shade each other, sends back edge-on.

### `var MATTE`

Lashes reflect nothing: at their size a highlight is only ever a spark on the eye's edge.

### `var THIN`

Reads the thinness from the packed map's blue channel, where the pipeline puts it
(`tools/world/people/README.md`, `thin`); glTF calls that channel metalness, which the material's
`metallicFactor` of 0 switches off for any other viewer.

### `var CROWD`

The crowd's share of what the hero's materials do, decided per fragment because one material draws a
whole pedestrian: skin (`_ROLE` 0) scatters (`pkSkin`), hair cards (3: hair, brows, lashes, beard) cap
their grazing reflection as `GRAZE` does. Without it the crowd's skin was plastic beside the hero's and
their hair a grey sheen under the rain.

### `var BEHIND`

The light through a thin part from the sky and the city's hemisphere light, as `SCATTER` does for
the sun and lamps: the environment and hemisphere light seen from behind the surface (`-N`), weighted
and coloured the same way. The sun is high and dim here and the light mostly comes from the overcast
sky, so without this an ear would glow only on the rare frame the sun is behind it.

### `var FRAMES`

The crowd's walk cycle is baked at this many poses; the shader blends between two of them.

### `var BREATH`

One breath, in seconds, at the rate `posture` breathes (1.7 rad/s): the standing, presenting and
sitting poses are baked over exactly one, so their loop has no seam.

### `var MOTIONS`

How many motions the crowd's bone texture holds: walking with an umbrella (the street's), standing,
presenting, sitting, walking with free arms (the agents', who stroll without umbrellas) and typing (an
agent at its desk).

### `function use`

Takes what `WorldAssets.load` read. `world.js` hands it `null` where vertex textures cannot hold
floats, where a skinned mesh cannot draw.

### `function ready`

True when there is a hero to build.

### `function parts`

What the character last built draws, one entry a part: its role, whether the look dyed it, whether it
reads a roughness and an occlusion map, whether light shows through it (`thin`), and its colour. `FleetWorld.inspect().people.parts` reports it,
so a test can hold a realistic export to its own colours and maps without reading pixels.

### `function frameQ`

The rotation of a frame given by an aim and a pole: the bone's axis and the direction its hinge bends
towards. Every limb is posed by frames, never by Euler angles on its own axes, so the animation does
not care how an exporter rolled its bones or whether the rest pose is an A or a T.

### `function rig`

A skeleton as the animation needs it: world rest rotations and positions, the spine and neck chains
(as many bones as there are: MetaHuman's five spine bones, MakeHuman's three), each leg and arm with
the frame its rest pose has, and the fingers with the axis they curl about. `move` is the joints' morph
offset for the chosen body. The palm's normal comes from the pipeline's anchors, or from the knuckles.

### `function posture`

The animation, as a handful of numbers a pose is built from: thigh and arm pitch and abduction, knee
and elbow flex, foot pitch, palm direction, finger curl, the pelvis, spine and head. A gait (walk
blending into a run with speed, from the distance covered, so feet do not slide), idle (breathing, a
slow weight shift, hands relaxed), presenting (one arm out and open, palm up, the other forearm
forward, the head tilted), seated in the wheelchair (pushing the rims as it rolls) and holding an
umbrella (the crowd). The character's world moves at 4.5 m/s when walking, which is a jog; the stride
lengthens with speed.

### `function solve`

From those numbers to every bone's local rotation and the pelvis position. Legs and arms are aimed
by frames, the spine and head turned in body space, the fingers curled in their own, every other bone
kept at rest. A standing pose is put on the ground: the lowest ankle or toe is lowered to where it
stands at rest. It returns what the page reports: the left thigh's forward swing (`legL`), the right
arm's angle out from the side (`armR`) and the pelvis height (`hips`), measured from the posed bones.

### `function choose`

The hero file whose `fits` matches most of the look.

### `function material`

A part's material, through the city's lights and rain (`WorldKit.lit`), with the character's own fill.
A part whose file carries a packed map (`orm`, glTF's layout: R occlusion, G roughness) reads its
roughness from it, and its occlusion when the file says it has one (`ao`); the pipeline writes it
(`tools/world/people/README.md`, `orm`). Metalness stays 0: people are not metal. Skin adds `SCATTER`,
and a skin whose file marks it `thin` reads its thinness (`THIN`) and adds the light from behind
(`BEHIND`). Cards (`CARDS`) cap or drop their grazing reflection. Each combination is its own program
key, fixed per part, so a change of look still compiles nothing.

### `function tint`

The colour a role takes from the look. The pipeline turned each tintable colour map into a detail map
(its mean is the part's `tone`), so a material's colour is the look's colour over that tone: the same
texture becomes every skin tone, hair colour and cloth colour the picker offers. Brows take the hair
colour, or dark brown where the hair is covered or gone. A part the pipeline kept in its authored colour
(`tint: false`, a realistic export's skin or cloth) takes none: dyeing a photographed skin another tone
reads as paint, so a realistic character's look is chosen by its `fits` among several exports instead.

### `function geometry`

A part's geometry with the body's morph targets applied on the CPU, once per build.

### `function anchored`

The head's anchors (eyes, crown, chin, nose, back, sides) moved with the chosen body.

### `function hull`

The body's outline around its axis, height by height, arms left out: what the headscarf's drape is
laid over.

### `function transfer`

Gives each vertex of the procedural accessories the bones and weights of the nearest body vertex, so
a headscarf moves with the shoulders and locs with the head.

### `function hero`

The player's character for a look: the body and the parts the look chooses (one hair, beard and
glasses style each), dyed, bound to one skeleton; the styles no file has (locs, the headscarf, the
wrap, or any a set of exports lacks) are drawn by `WorldHero.dress` and skinned by `transfer`.

### `function sample`

An animation clip's rotations, blended over the procedural idle when the manifest names one.

### `function pose`

A frame of the character: the gait advanced by the distance walked, its amplitude eased with speed
(at once under reduced motion), turning in place stepping, and the clip on top when there is one.

### `function bake`

The crowd's walk, holding an umbrella, posed `FRAMES` times and written as bone matrices; the right
hand's path is kept for the umbrella. With `motion` 1 it bakes standing instead, with 2 presenting
(the hero's talking pose) and with 3 sitting (the seated pose, hands in the lap): no stride, one breath
(`BREATH`), and no slow sway, which would not loop. With 4 it bakes the walk again, its arms free; with
5 typing: seated, upright, forearms out to the keyboard, the hands tapping six times a breath so the
loop has no seam.

### `function crowd`

The pedestrians: every crowd character at two levels of detail, each an `InstancedMesh` of one
geometry and one atlas, skinned in the vertex shader from a float texture of baked bone matrices (a
row per character and frame; `texelFetch`, so WebGL 2, which every tier with pedestrians has). Each
instance has its phase and pace, its row, and its skin, top, bottom and hair colours, which the
`_ROLE` attribute picks between; the role also reaches the fragment shader, where skin scatters and
hair cards stop mirroring the sky (`CROWD`). Draw calls are characters times levels, whatever the number
of people; the low tier has no pedestrians.

The texture holds `MOTIONS` motions: every character's walk first (rows `character × FRAMES`, as the
street places them), then every character's standing, presenting, sitting and free-armed walk, for the
agents.

### `function agents`

The agents as people: per crowd character one more `InstancedMesh`, sharing that character's near
geometry and the crowd's material, with instance attributes of its own, up to `cap` agents. Null
without a crowd, and the world keeps its robots.

### `function cast`

Which crowd character an agent is: a hash of its name, so the same agent is the same person on every
visit and every machine.

### `function placeAgents`

Each frame: every agent's person where the world put it, walking (motion 4, arms free, at its own `rate`
of strides; 0 is the street's walk with its umbrella), standing (1), presenting (2) or sitting (3), at its
own phase, in its colours. Says how many it
placed.

### `function hand`

Where a crowd character's right hand is at a point of its walk, for the umbrella.

### `function draw`

Each frame, every pedestrian into the mesh of its character and level of detail (near ones detailed,
far ones not); an empty mesh is not drawn.
