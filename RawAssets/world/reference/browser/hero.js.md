# `world/hero.js`

The reasoning for this script, kept out of the source (decisions 18 and 19 on #429). The source keeps
its code and the JSDoc types `tsc` reads; the server strips every comment from what it serves
(`agentdata/fleet/strip.py`).

A `###` heading is the declaration or statement the notes sit in or above, in source order.

### The file

The player's character in the world (#626). The operator, 2026-10-02, with a picture of a friendly 3D
cartoon figure (a big round head with a wide smile, swept dark hair, a light-grey button-up shirt with
the collar open, navy trousers, navy trainers with white soles, one arm out, palm up): *"Use some one
like this as the main character. Create diverse character options so everyone feels included."*

A classic script loaded before `world/world.js`. It defines one global, `WorldHero`, and needs
nothing else from the page: `world.js` hands it the three.js module it imported. Everything is built
from three.js's own primitives (spheres, capsules, cylinders, boxes, tori), with no model file and
nothing fetched.

Since the people pass (2026-10-03) this file is also the facade for the realistic character:
`build` asks `WorldPeople` for a skinned human and falls back to the character below when there is
none, so a page whose people files fail to load still has its character.

### `var SKIN`

Eight skin tones, deep to light, warm and neutral. A character creator that offered three would tell
most people that it was not made for them.

### `var HAIR`

Black, dark brown, brown, auburn, blond and silver (age is part of who people are), and one blue.

### `var STYLES`

Hair as people wear it:
- straight and wavy: a side part, long hair, a bun, a buzz cut;
- curly and coily: curls and coils, as their own shapes, not one "afro";
- locs;
- none: bald;
- covered: a headscarf (a hijab-like scarf with a drape over the neck) and a wrap (a gele or turban,
  tied high).

The scarf and the wrap take their own colour (`SCARF`), not the hair's.

### `var OPTIONS`

The picker's rows, in the order a person thinks about themselves: skin, hair, then the details, then
clothes and build, and how they move. Every row is a choice, and none of them is labelled by gender:
the looks are what a person chooses, not a category they are put in.

### `var PRESETS`

Ten complete looks to start from, named by what they look like, never by a name that implies a
gender. The first is the operator's picture. Between them they span every skin tone, every hair
texture, both coverings, both kinds of glasses, a beard and a moustache, all three builds, and a
wheelchair user. Each is only a starting point: every option can be changed after.

### `var FIGURES`

The body's figure, angular to curved, and between: a realistic body has a shape, and the picker
offers it as a shape, never as a gender. With `AGES` (young, middle, older) these are morph targets of
the realistic character; the rows are hidden when the character is the procedural one (`shown`).

### `function normal`

A look from anywhere (the browser's storage, a preset, a click) is reduced to known values, so a
stored look from an older version, or one edited by hand, can never build something broken.

### `function material`

One material for every character the page builds, made once and kept. A character is rebuilt on
every change in the picker; a material made each time was a shader compiled each time, which
stalls even a real GPU for a moment and stalled SwiftShader long enough to draw black.

The material adds a fill of its own colour (`uFill`, a line patched into three.js's shader). Under
the overcast sky a body's sides and front, which face the horizon rather than the zenith, took about
a third of the light the plaza does, and a face in the picker read as a shadow. The fill lights only
the character, so the scene keeps its rainy look; `fill` sets it with the daylight (`vWeather`).
The material also takes the city's lights and the rain (`WorldKit.lit`): under a street lamp or by
a neon sign your character is lit in its colour, and its coat is darker for the wet.

### `function piece`

One part of the character: a primitive placed, scaled and turned into the character's space, painted
in one colour as a vertex colour.

### `function mesh`

The parts that move together become one mesh with one draw call: the head with its face and hair, the
torso with its collar and buttons, each upper arm, each forearm with its hand, each thigh, each shin
with its shoe. A character is ten draw calls (twelve in a wheelchair), whatever its look.

### `function hair`

The caps sit inside the head at the face and outside it at the top and back, so they cover the crown
without hiding the forehead or the eyes. The swept fringe of the side part follows the picture.

### `function face`

The picture's face: round dark eyes, raised brows, a small nose, and an open smile with its teeth.
The eyes have whites and a catchlight, so they read on every skin tone, the deepest included.
Eyebrows take the hair's colour, or dark brown where the hair is covered or gone.

### `function build`

The bones a pose needs: the hips (everything above them), the head, each arm at the shoulder and its
forearm at the elbow, each leg at the hip and its shin at the knee. The character faces `-Z`, the way
the camera looks at a yaw of 0, so turning it to the player's yaw faces it where the player faces.
A wheelchair is part of the character, not an accessory: a seat, a back, a footrest, casters, and two
wheels with push rims that turn as it rolls.

### `function chair`

The wheelchair, shared by both characters.

### `function build`

The realistic character when `WorldPeople` has one, with the wheelchair added; otherwise the doll.

### `function veil`

The headscarf for the realistic character: a shell round the head with an opening for the face
(an oval from the brows to the chin), and a drape laid over the neck, shoulders and chest
(`WorldPeople`'s hull of the body) instead of a cone through them.

### `function dress`

What no file supplies, drawn for the realistic head from its anchors (eyes, crown, chin, nose, back,
sides, moved with the body): the headscarf, the wrap (a cap with folded bands and a knot), locs, a cap
for any hair style a set of exports lacks, and a beard, moustache or glasses where none came with the
character. `WorldPeople` skins it to the nearest body vertices.

### `function measure`

What the page reports of the character's pose, the same three numbers for either character.

### `function shown`

The picker rows that apply: the scarf colour with a scarf or wrap, figure and age with the realistic
character.

### `function pose`

Walking: the legs swing with the stride (`phase` is distance walked, not time, so the feet do not
slide), the knees bend on the back swing, the arms swing against the legs, and the hips rise a
little with each step. Rolling: the character sits, the wheels turn by the distance rolled, and the
arms push the rims. Talking (`talk`, blended from 0 to 1): the presenting pose of the picture, one
arm out and open and the other forearm forward, the head tilted a little. Under reduced motion the
breathing stops.

### `function fill`

The character's fill, set by the world with the daylight: lower at night, where the lamps light it.

### `function dispose`

A character's geometry is freed when it is replaced; the shared material is kept.
