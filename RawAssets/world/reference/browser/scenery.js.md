# `world/scenery.js`

The reasoning for this script, kept out of the source (decisions 18 and 19 on #429). The source keeps
its code and the JSDoc types `tsc` reads; the server strips every comment from what it serves
(`agentdata/fleet/strip.py`).

A `###` heading is the declaration or statement the notes sit in or above, in source order.

### The file

The sky and the office in the middle of the district, which took the plaza's place (the operator,
2026-10-06: an office building with the agents as people, whose screens you take over). A classic
script loaded after `world/kit.js` (its noise, `piece` and `merge`) and `world/bake.js` (the desks' wood),
and before `world/world.js`. It defines one global, `WorldScenery`. The ground, the streets and the city
are `world/city.js`'s, what stands on the streets `world/street.js`'s.

### `var NOISE`

The kit's noise (`WorldKit.NOISE`), for the sky's clouds.

### `var HIGH`

The office's height to its ceiling, 4.2 m: room for the light rings and a third-person camera under
them.

### `var DOOR`

Each of the office's four doors, 3.2 m of the glass ring left open where an avenue meets it.

### `function sky`

A sphere drawn from inside, before everything, without depth: the gradient from horizon to zenith,
two layers of fbm cloud (bright by day, dark by night), and by night a warm glow at the horizon, a
city's light on low cloud. The clouds drift with the kit's clock, the upper layer a little faster than
the lower, so the sky is weather rather than a painted ceiling. The environment map comes from the
Poly Haven skies when they load (`WorldAssets.dome`, `vReflect`), else from this sphere.

### `function lamp`

A street lamp in cast iron: a base, a pole, a collar, an arm with a scroll under it, a lantern (cap,
finial, gallery) and its glass. The glass goes to its own list, an unlit mesh, so it glows at night
without a light of its own; the light it casts is one of the kit's lights (`vPlaza`).

### `function tree`

A stone planter with soil, a trunk and a branch; the crown is leaf cards (`WorldKit.canopy`), as on
the streets. With the trees' file only the planter: its tree is a young linden from the file.

### `function opening`

Whether an angle round the office falls in one of its doors (within `half`, in radians, of an
avenue's axis): where the glass is left out and where you may walk through.

### `function plaza`

The office, built for its edge (2.6 m outside the desks' ring): the plaza's low kerb; the glass ring
(`G`, 1.7 m outside the desks) as panes between dark mullions, with a door to each avenue (`opening`)
and a rail at its foot and head; a flat roof overhanging the glass by 1.4 m, its ceiling light grey
and unlit (a lit office's ceiling, evenly lit, rather than one lit only by the lamps near it, which
read brown), with two rings of light; a lamp each side of every door; four planters with young lindens
inside, in the walk behind the desks, where they block no path from the middle to an agent (a planter
and the columns once stood on those lines, and walking up to an agent stopped against them); eight
outside, between the doors. The floor is a ring round the plaza's brass compass, dry and polished. It
is made again only when the edge moves, never per frame. It returns the lamps' glass (halos and
lights), the lights inside (`inner`, one over each desk is `vPlace`'s), each footprint, and the
wall: its radius, its doors' half angle, the roof's radius and height (no rain under it, `vRain`).

### `function desks`

The workstations from the office's file (`static/world/office/office.glb`): an instanced mesh for
each of its parts, the desk's top in the baked wood, the fittings in their colours, and the screen,
which reads its agent's cell of one texture (`aCell`, an 8 x 8 atlas of 256 x 144 cells) and glows a
little. Null without the file: the office stands without desks.

### `function placeDesks`

A desk where each agent's is, facing out to the glass, and its screen's cell: the agent's place in the
list.

### `function screens`

Each agent's screen, a terminal's look: its name and state in its colour on a title bar, what it says
it is doing and the last thing it said, wrapped, and an amber frame when it needs you. A cell is made
again only when what it shows has changed, and the texture uploaded only then.

The atlas is an SVG image, not a canvas: #257 keeps every file under `static/` off 2D contexts (one
platform, `tests/test_fleet_trace.py`). Each cell is its SVG (rectangles, a circle, monospace text,
which wraps by count of characters as a terminal's does), the cells are one SVG drawn as an image
from a `data:` URL (the page's CSP allows `data:` images), and the image is the texture's. A newer
redraw that lands first wins: an older image that finishes loading after it is dropped (`seq`).

### `function glows`

A soft halo around each lamp at night: a quad turned to the camera in the vertex shader, one draw
call for as many lamps as it is given (the plaza's eight, the streets' many), added to what is behind
it. Hidden by day.

### `function placeGlows`

The halos where the lamps are, each in its lamp's colour when the lamp gives one (a street lamp's
sodium or LED), else the plaza lamps' warm white.
