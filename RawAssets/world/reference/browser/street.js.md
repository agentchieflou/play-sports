# `world/street.js`

The reasoning for this script, kept out of the source (decisions 18 and 19 on #429). The source keeps
its code and the JSDoc types `tsc` reads; the server strips every comment from what it serves
(`agentdata/fleet/strip.py`).

A `###` heading is the declaration or statement the notes sit in or above, in source order.

### The file

What is on the streets: lamps, trees, traffic lights, hydrants, bins, newspaper boxes, parking meters,
bus shelters, parked cars, traffic and people under umbrellas. A city at night is its lights and the
things that move; this is where both are.

A classic script after `world/city.js` (its plan). It defines one global, `WorldStreet`. The street
furniture is one merged mesh, the trees one more, the cars four instanced meshes for each of two
models, the people two: a little over twenty draw calls for every street, however many cars.

### `var PAINT`

Car colours as they are on real streets: mostly black, white, grey and silver, some dark blue, red
and green, a few yellow (taxis).

### `var BARK`

Bark in the trunk's own UVs: two noises stretched along the trunk (fine around it, coarse up it) read
as furrows and plates, darkening the colour between them. No texture to load or bake.

### `var SODIUM`

A sodium lamp's colours, its cone's and its halo's (each linear, as the shaders take them; the halo's is the
plaza lamps' warm white, `0xffc98a` in sRGB), which before this were the colour of every lamp.

### `var LED`

An LED lamp's colours, cone and halo, cool white, so they match the light it casts (`lampPost`).

### `var REACH`

How far down the avenues the traffic drives before it turns, in the fog where the turn is not seen.

### `function car`

A car from three.js's own shapes: the body extruded from a side profile with wheel arches and
bevelled edges, the cabin from its own profile, roof and pillars, wheels with rims, bumpers, plates,
a grille, head and tail lights. A sedan and an SUV. They are the traffic on the `low` quality and
wherever the cars' file did not load; elsewhere the cars are the file's (`traffic`).

### `function shape`

The body narrows at the nose and tail and the cabin leans in towards the roof, as a car's does, and
then `WorldKit.smooth` shades it smooth where it curves and sharp at its creases.

### `function person`

A walker in a coat, its legs and one arm marked (`aLimb`) so the vertex shader can swing them, the
other arm up, holding an umbrella.

### `function umbrella`

The canopy, its underside and its stick.

### `function materials`

Car paint with a clear coat (`MeshPhysicalMaterial`), dark glass, trim, lamps (unlit, brighter at
night); people whose legs and arms swing as they walk, and who bob; umbrellas; street furniture;
the shelters' glass; the traffic lights, whose shader switches green, amber and red on a 30-second
cycle, the two directions half a cycle apart; and the light cones under the street lamps, the rain
lit in them, added to what is behind, each in its lamp's colour (an instance colour, `SODIUM` or `LED`).

The trees' trunks have a material of their own (`bark`): rough, not metal, furrowed (`BARK`). They
shared the furniture's, whose metalness (0.35) suits iron poles and made the trunks read as metal.

The cone's two fades are clamped to 0..1 before `pow`. Its facing fade is near zero along the cone's
silhouette, and with multisampling a fragment there is shaded at the pixel's centre, outside the
triangle, where the interpolated value goes a hair below zero; `pow` of a negative base is undefined,
and on many GPUs (Direct3D's `pow` is `exp2(y * log2(x))`) it is NaN. Added into the HDR target, one
NaN pixel per cone edge went through the bloom's mip chain, which spread it into blocks a sixty-fourth
of the screen wide and wider, and the grade drew them black: two great dark shapes over the plaza that
followed the camera wherever a lamp was in view (operator report, 2026-10-05). SwiftShader's `pow`
gave a number there, which is why CI never saw it; Mesa's llvmpipe gives NaN, as the operator's GPU did.
`tests/regressions/test_20261005_any_chrome_world_black_shapes_over_the_plaza.py` holds every GLSL
`pow` in the world's scripts to a base that cannot go negative.

### `function segments`

Each side of each street between two crossings, stopping at the ring road.

### `function lampPost`

A street lamp every 24 m on both sides: a pole, an arm over the road and a head, sodium orange or LED
white, one in a few dozen failing. Each is one of the kit's lights, a halo and a cone, all three in
the lamp's colour.

### `function tree`

A street tree on the avenues: a pit, a trunk, two branches and a crown of leaf cards. The pit, trunk
and branches go to the bark mesh (`street-bark`), the crown to the trees' (`street-trees`). With the
trees' file only the pit is built here: the tree is the file's (`build`).

### `function put`

One of Poly Haven's scanned props (`WorldAssets`) at a spot, if it loaded and the spot is within reach
(110 m, 75 m on the `low` path; past that the fog has it). Says whether it did, so the caller can build
its own instead.

### `function facing`

The turn that faces a prop on the sidewalk towards the road.

### `function litter`

What collects beside a bin on a side street: one to three black bags and the odd cardboard box,
against the wall.

### `function backStreet`

The side streets' own things: an air-conditioning unit against a wall, and, at a few corners, a pair
of concrete barriers across the no-parking zone by the crossing.

### `function clutter`

What stands on a sidewalk: a hydrant, a bin, a newspaper box (half of them a utility cabinet) or a
parking meter, by seed. The hydrant, the bin and the cabinet are scans where they loaded, and the
bin on a side street gets its litter.

### `function signals`

A traffic light on each corner of a crossing: a pole, a mast arm over the road, a head facing the
oncoming traffic, its lamps tagged with their colour and their direction for the shader.

### `function tag`

The traffic-light attribute on a piece (a shelter's advertising light has none: it stays lit).

### `function shelter`

A bus shelter: posts, a roof, a bench, a glass back, and a lit advertising panel.

### `function loopPath`

The route a car drives: in along an avenue, a quarter of the way round the ring (the plaza on its
left, as on a roundabout where traffic drives on the right), out along the next avenue, and round in
the fog to come back in, through all four avenues. Two lanes, two loops. A third loop circles the
ring on its inside lane.

### `function measure`

A route's length along its points, to place a car by distance.

### `function along`

The point and heading a distance along a route.

### `function build`

The street for a plaza of radius `P`. What is far from where you can walk is left out: furniture past
150 m, trees past 160, lamps past 215 (their light is seen down the avenues), and parked cars past
120 m (none on the `low` path, nor people). One parked car in a dozen is under a cover (a scan). The
scans are instanced, one draw call per prop however many stand in the street, and share their
geometry across rebuilds (`userData.shared`, never disposed). Returns the footprints you walk round,
the lamps' lights, and how many cars, parked cars, people, scanned props and grown trees there are.

With the trees' file (`woods`) the avenues are planted with its trees (`WorldKit.grove`, in a group
named `street-trees`): one species a stretch of avenue between crossings, London planes or lindens,
as a city plants them, either of two trees of it at each spot, each turned and sized a little apart.
A spot that falls at a lamp (every third, where the 16 m of the trees meet the lamps' 24 m) is left
empty: a crown nine metres high would have swallowed the lamp's head and its cone of light.

### `function traffic`

The cars, spaced along their loops, each with its own top speed, and the parked cars along the side
streets' kerbs, each its own colour, their lights off.

With the cars' file (`fleet`, `static/world/cars/cars.glb`) there are four kinds, a sedan, a hatchback,
an SUV and a van, each at two levels of detail: near, a body (painted), glass, trim (tyres, rims,
grille, plates, pillars, seams) and lamps, each through the material of the same name; far, the body
with its glass and trim in it by colour, and the lamps, two draw calls rather than four. A kind's
cars are spread over its two levels every frame (`frame`), so a car keeps its colour wherever it is
drawn: its paint is the car's, not its slot's.

### `function walkers`

With `WorldPeople`'s crowd the walkers are realistic people, each a crowd character with its own skin,
coat, trousers and hair colours, holding an umbrella; without it, the procedural walkers below.


People on the sidewalks, each with a pace, walking up and down their stretch of sidewalk.

### `function frame`

The crowd's pedestrians are placed every frame into their character's mesh, the detailed one within
24 m of the camera (`eye`), and each umbrella is put in its pedestrian's right hand where the baked walk
has it.


Every frame: a car keeps its distance from the one ahead and stops at a red light at the crossing
ahead, then drives on; every car, parked or moving, goes to its kind's near meshes within 36 m of the
eye and to its far ones beyond, with its paint and its lamps lit or dark; its headlights light the road ahead of it and its tail lights the road behind
(the kit's moving lights). The people walk. Under reduced motion nothing moves.

### `function stopLine`

How far ahead is the stop line of the next crossing on an avenue, if there is one within 16 m.
