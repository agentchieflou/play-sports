# `world/city.js`

The reasoning for this script, kept out of the source (decisions 18 and 19 on #429). The source keeps
its code and the JSDoc types `tsc` reads; the server strips every comment from what it serves
(`agentdata/fleet/strip.py`).

A `###` heading is the declaration or statement the notes sit in or above, in source order.

### The file

The streets and the buildings round the plaza. The operator, 2026-10-02: *"We're looking for a far
cry 3 / rdr2 / gta 6 / cyberpunk quality world with rich assets and quality features that can run in
browser."* A ring of towers in the fog was a backdrop; this is a district you can walk into.

A classic script after `world/kit.js` and `world/bake.js`. It defines one global, `WorldCity`. Every
building is made here from quads and boxes, in arrays, merged by material: a dozen draw calls for the
whole city.

### `var K`

The grid: streets every 64 m, three blocks out each way from the plaza (two on the `low` path). The
avenues through the plaza are 16 m of road with 5 m sidewalks, the side streets 11 m and 4 m. A ring
road 12 m wide goes round the plaza, which grows with the fleet; blocks inside the ring's reach are
left out.

### `var LIMIT`

You walk up to 150 m from the plaza: far enough to stand in a side street, near enough that the
detail is where you are.

### `var FACADE`

Each style's colours, which tint its baked material: brick (red, buff, painted), stucco (pastels),
concrete, stone and glass.

### `function quad`

Every surface is two triangles with its normal, uv in metres (so the baked textures are at their real
scale) and a colour; the glass and the signs carry extra attributes.

### `function box`

An axis-aligned box whose faces can be left out by a mask: the face against a wall, the top and
bottom of a thin bar.

### `function face`

A wall's own frame: along the wall, up, and out of it, so a facade is written once whatever way it
faces.

### `function fbox`

A box on a facade (a sill, a cornice, a railing). Its flags say what not to draw: the face against
the wall, the top and bottom, the ends.

### `function reveal`

The four sides of a window's opening: the window is set into the wall, so it has depth and its
sides catch the light.

### `function glass`

A window: a quad whose shader draws the room behind it. `aWin` is the point on the glass, the window's
seed, and its kind (a window, a shopfront, a curtain wall, or a flat facade's grid of windows) with the
window's height packed in; `aDim` is the room's size round it.

### `function sign`

A sign's panel: `aSign` counts its characters, so the shader writes that many glyphs.

### `function blade`

A projecting sign, the kind that hangs over a sidewalk: an iron box with neon on both faces, and a
coloured light on the street.

### `function facade`

A wall of a building, by its style:
- windows set into the wall in bays, with stone sills, brick lintels, balconies on stucco, a floor
  band on concrete, and a cornice;
- glass towers' curtain walls, between metal columns;
- a shopfront on the street side;
- a plain party wall where the next building stands against it, as real party walls are;
- the side away from the street and buildings past 105 m have flat windows with no reveals, which no
  one walks close enough to see.

On the `low` path (`lod` 0) every upper facade is one quad whose shader draws the windows, for a
software renderer.

### `function balcony`

A slab, a top rail, a bottom rail and balusters.

### `function bars`

Thin bars as two quads each, front and back, rather than boxes: a railing is many bars, and a box is
twelve triangles.

### `function escape`

A fire escape on the front of a brick walk-up: a platform and railing on every floor, and a stair
between them.

### `function shopfront`

The ground floor, in bays: bulkhead, glass deep in its frame (`glass` kind 1: a bright shop
interior), a sign over each, sometimes an awning, a light on the sidewalk in the sign's colour, and
on many a blade sign above.

### `function awning`

Striped canvas over a shop window: sloping, two-sided, with a valance.

### `function roof`

A gravel roof inside a parapet, with a stair bulkhead, air-conditioning units, sometimes an antenna,
and on brick buildings a wooden water tank on legs.

### `function tank`

The water tank: staves in wood, a cone roof, iron legs.

### `function building`

The four facades (their uv runs on round the corner, so the brick does not jump), the roof, and
sometimes a billboard.

### `function billboard`

A lit board on a steel frame on the roof, facing the street, its picture moving slowly, lighting the
street below in its colour.

### `function plan`

The lots of every block.

### `function lots`

A block's edges divided into lots 8 to 20 m wide and up to 19 m deep, facing their street. A lot's
style and height follow a seed: brick and stucco low, concrete and stone higher, glass towers tallest
and never next to the plaza; buildings rise further out.

### `function party`

Which walls stand against a neighbour at least as tall.

### `function curbs`

Granite kerbs along every road edge, with gaps at the crossings, and round both edges of the ring
road.

### `function clipRing`

A straight kerb stops where the ring road begins.

### `var GLASS_F`

The window shader: interior mapping. The view ray is followed into a room behind the glass (its width
the bay, its height the floor, its depth 3.6 to 6.8 m, by seed), and whichever wall, floor or ceiling
it meets is shaded: wall paint by seed, wood or carpet, a ceiling, a picture or a door on the back
wall, and a piece of furniture (a sofa, a desk, shelves in a shop). By night a room is lit or not,
switching every few minutes, warm or cool, at its own brightness, the ceiling light visible from
below; some rooms have only a television's flicker. By day the rooms are dim, darker further in. Some
windows have blinds part down. Frames and glazing bars are drawn in the frame's colour. Curtain walls
show a room per bay and floor, with the spandrel opaque. The glass reflects the sky and the lights
through three.js's own specular (curtain walls as a metal coating), and Fresnel decides how much of
the room shows.

### `function glassMat`

A `MeshStandardMaterial` with the window shader inside it, so the glass is lit, fogged and reflected
like everything else. A lit window adds back part of its light after the fog, so the city glows
through the rain at night.

### `var SIGN_F`

Signs: glyphs from a seed on a 3 by 5 stroke grid, so a sign reads as lettering at a distance without
a font or a 2D canvas: neon tubes on a dark panel, a lit box with dark letters, or a billboard's
moving picture. A few flicker. Brighter at night.

### `function signMat`

Signs are unlit (they are lights), fogged, and seen in the mirror.

### `function geom`

A bag of arrays into a `BufferGeometry`, its extra attributes split out.

### `function materials`

The baked materials (brick, stucco, concrete, stone, roof, metal, wood, kerb granite), canvas, glass
and signs, each given the kit's lights and wet.

### `function build`

The city for a plaza of radius `P`: every lot, the party walls, the kerbs, merged by material; the
footprints (so you cannot walk into a building) and the lights of the shopfronts, signs and
billboards are returned. Built again only when the plaza's size changes.

### `var GROUND_F`

The ground: one plane whose shader knows the plan, so the roads, sidewalks and plaza need no geometry
of their own:
- the plaza: granite in rings, each stone its own shade, a brass compass at the centre;
- roads: asphalt, double yellow centre lines and dashed lanes on the avenues, a dashed line on the
  side streets, zebra crossings and stop lines at every crossing, lanes round the ring, worn paint,
  manhole covers, and gutters along the kerbs where the water gathers;
- sidewalks: concrete slabs.

Puddles gather by noise and in the gutters. They are near mirrors, and the rain rings them: drops
land at random in a grid of cells and each sends out a ring that fades (`GROUND_SIMPLE` leaves the
rings out on the `low` path).

Everything on it is shaded ±14% by a noise some 20 m across: newer and older asphalt, cleaner and
dirtier slabs. The photographs repeat every 1.8 to 3 m, and without variation larger than that, the
repeat showed as a grid down every street.

### `function ground`

The ground is in layer 1, so the mirror does not draw it into itself, and it reads the mirror
(`reflect`).
The asphalt and the sidewalk tile at the size their textures cover (`AS_SIZE`, `SW_SIZE`, from the
material library): a photo of three metres of road is laid three metres wide. Their colours take
the photos' gains (`AS_GAIN`, `SW_GAIN`), and the sidewalk's photo, a warm stone, is half
desaturated (`SW_SAT`) to the grey of wet concrete.

### `function onRoad`

Whether a point is on a road.
