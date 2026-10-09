# `world/kit.js`

The reasoning for this script, kept out of the source (decisions 18 and 19 on #429). The source keeps
its code and the JSDoc types `tsc` reads; the server strips every comment from what it serves
(`agentdata/fleet/strip.py`).

A `###` heading is the declaration or statement the notes sit in or above, in source order.

### The file

What every part of the world shares: noise, the helpers that build merged models, the uniforms the
frame writes, and the city's many lights. The operator, 2026-10-02: *"We're looking for a far cry 3 /
rdr2 / gta 6 / cyberpunk quality world with rich assets and quality features that can run in
browser."* Most of what makes those worlds read as real is light: many lights at night, wet surfaces
that catch them, and reflections. This file is where that light is shared.

A classic script, the first of the world's. It defines one global, `WorldKit`.

### `var NOISE`

A hash, value noise and a three-octave fbm in GLSL, for the sky, the ground, the windows and the
signs. `ws` prefixes every name so the chunks three.js splices around them cannot collide with them.

### `var u`

The uniforms the scene shares, as objects, so a material patched in `onBeforeCompile` reads the same
value `world.js` writes: the time, the night, how wet everything is, the share of windows lit, and
the wet street's mirror (`refl`, `reflMat`, `reflOn`, written by `world/render.js`).

### `var L`

The lights. `all` are the fixed ones (lamps, signs, shopfronts), `moving` the cars' head and tail
lights, refreshed each frame; `pos` and `col` are the uniform arrays the shaders read.

### `function rnd`

A repeatable random number from a seed: the city is the same on every load and every machine, so a
screenshot today matches one tomorrow.

### `function piece`

One part of a merged model: a primitive, made non-indexed, moved, scaled, turned and painted with a
vertex colour. A car is twenty pieces and costs nothing in draw calls, because its pieces are merged.

### `function merge`

Pieces into one `BufferGeometry`: position, normal, colour, uv and any extra attribute they all
carry. Merging is what keeps the detail free in draw calls.

### `function light`

A fixed light: where, its colour (already times its intensity), how far it reaches, and a tag
(`plaza`, `city`, `street`) so a part of the world can take its lights back when it is built again
(`forget`). `f` makes it flicker, as a failing tube does.

### `function lights`

How many lights a fragment may add up, which the quality tier sets (8 to 32), and whether the
mirror is on: both are `#define`s, so they are fixed when the materials compile.

### `function pick`

Each frame, the lights nearest the camera, those ahead counted nearer than those behind, go to the
shaders. The city has hundreds of light sources; a pixel adds up only the few that reach it, so the
cost is the tier's, not the city's. By day only lights marked `day` stay on.

### `var LOOP`

The many-light loop, put into three.js's own lighting (`RE_Direct`) right after its lights, so every
lamp, sign and headlight lights a surface through the same physically based shading as the sun:
diffuse, specular highlights, the clear coat of a car. Lights are in world space and moved into view
space with `viewMatrix`, so the same shader is right for the camera and for the mirror.

### `var WET`

Rain on everything: surfaces facing up are wettest, walls a little. Wet is darker and smoother, which
is what makes a street catch the lights. `WK_POROUS` says how much a material takes the water: brick
and cloth soak, glass and paint do not darken.

### `var REFLECT`

The wet street's mirror: the scene drawn upside down under the street (`world/render.js`) is looked
up where the fragment is, blurred by the surface's roughness (a puddle is sharp, wet asphalt smeared)
and stretched up and down the screen, as reflections in rain are. It replaces the environment's
reflection in three.js's own specular term, so Fresnel still decides how much shows: little looking
down, nearly all at a grazing angle.

### `function lit`

Gives a material the lights, the wet and, when asked, the mirror, around whatever patch the material
brings itself (`extra`). `customProgramCacheKey` names each patch, because three.js would otherwise
share one compiled program between materials patched differently.

### `function smooth`

Normals averaged across faces that meet at less than an angle, so a car's body is smooth where it
curves and sharp at its creases, without an index.

### `function canopy`

A tree's crown as leaf cards: quads scattered through a squashed sphere, each turned at random, with
normals pointing out from the crown's centre rather than off each card, so the crown is lit as one
soft volume, as foliage is, instead of as many flat cards.

### `var LEAF`

Leaves let light through: a crown seen from below or against the sky glows yellow-green where light
comes through its leaves from behind. The environment and hemisphere light seen from behind the
card (`-N`) are added, weighted to what a leaf passes (green, a little red, almost no blue). It is
the same idea as the people's thin skin (`people.js.md`, `SCATTER`), and like it touches only this
material.

### `function crown`

A crown's material, whichever leaves it is given: their alpha cut out, both sides drawn, and the crown
swaying a little in the wind, the phase from where the tree stands (with the instance's place, so a
street of instanced trees does not sway as one).

Its grazing reflection is capped (`specularF90` 0.3): a crown's normals point out from its centre, so
its top and rim face the bright, rain-wet sky at a grazing angle, and at full Fresnel every crown read
as grey-white, a sheet of reflected sky, rather than green. Light comes through it (`LEAF`).

A card seen from behind keeps the crown's normal. three.js turns a double-sided surface's normal (and
its tangent frame) round when its back faces the eye, which on a card whose normal points out of the
crown made every leaf seen from behind face into the tree, dark: a crown looked half dead from any
side. A card seen edge-on fades out (its face against the eye, from the screen-space derivatives of
the view position), so turning cards are not hard streaks. And the further away, the more its alpha is
raised with the texture's mip level (`fwidth` of the UV, for a 1024 px atlas): mipmaps average the
leaves' cut-outs with the gaps between them, and without it a distant crown thinned to specks.

### `function foliage`

The page's own crowns (`canopy`, on the `low` quality and wherever the trees' file did not load): the
baked leaf texture through `crown`.

### `function grove`

Trees from the trees' file (`static/world/trees/trees.glb`, through `WorldAssets.scans`), where a list
says (`[x, y, z, yaw, scale, kind]`): an instanced mesh per kind, level of detail and part (bark,
leaves), so a street of trees costs a few draw calls however long it is. A tree within 42 m of the eye
is drawn whole (`<kind>_lod0`), further away with fewer and larger twigs (`_lod1`, about a third of
the triangles); the split is made again only when the eye has moved 4 m, and a mesh with no tree in it
is hidden rather than drawn empty. Returns the update and how many trees it placed.
