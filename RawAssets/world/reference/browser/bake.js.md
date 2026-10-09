# `world/bake.js`

The reasoning for this script, kept out of the source (decisions 18 and 19 on #429). The source keeps
its code and the JSDoc types `tsc` reads; the server strips every comment from what it serves
(`agentdata/fleet/strip.py`).

A `###` heading is the declaration or statement the notes sit in or above, in source order.

### The file

The world's surfaces, baked on the GPU when the page loads. A real material needs textures: colour,
how rough, how lit its creases are, and a normal map for its relief. Those are usually image files;
here they are drawn by shaders into render targets once, in a few milliseconds, and then sampled like
any texture, with mipmaps and anisotropic filtering, so they cost what a texture costs per frame and
nothing on the wire. No image is fetched and none is drawn on a 2D canvas (#257).

A classic script after `world/kit.js`. It defines one global, `WorldBake`.

### `var LIB`

The materials and how many metres one tile covers: brick (8 bricks by 16 courses in 1.8 by 1.2 m,
the bricks' real size), board-formed concrete, stucco, stone ashlar, asphalt, sidewalk slabs, granite,
corrugated metal, roofing gravel, wood, and leaves.

### `var NOISE`

Hash, value noise, fbm and cellular noise that repeat with the tile (every lattice is taken `mod` the
period), so a texture tiles without a seam.

### `var MATS`

Each material as a function of the tile's uv that gives its colour, roughness, ambient occlusion,
metalness and height:
- brick: running bond, each brick its own red, some burnt dark, grime, recessed mortar;
- concrete: formwork panels with seams and tie holes, and rain streaks down it;
- stucco: plaster with fine relief, dirt streaks and the odd crack;
- stone: ashlar blocks with bevelled, chipped edges;
- asphalt: aggregate, patches, cracks;
- sidewalk: 1.5 m slabs, joints, cracks, gum spots;
- granite: salt and pepper grain, for the plaza;
- metal: ribbed, painted, rusting in streaks;
- roof: tar and gravel with standing water;
- wood: planks with grain;
- leaf: a cluster of leaves with veins, its alpha the leaves' outline.

### `function shader`

One material in one mode: colour (written to an sRGB target, so 8 bits are spent where the eye sees
them), the ORM texture three.js reads (occlusion in red, roughness in green, metalness in blue), or
the normal map, from the height's slope.

### `function make`

Bakes every material at the resolution the tier asks for (512, or 256 on the lowest), with mipmaps,
anisotropic filtering and repeat wrapping, and sets each texture's repeat to its tile's size, so
geometry can carry its uv in metres.
A material Poly Haven photographed (`photos`, from `WorldAssets.load`) is not baked: its photo is
used in its place, at the size it really covers (`size`), and marked `photo`. The baked one is what is
left when the file does not load.

### `function std`

A `MeshStandardMaterial` wearing a baked set, its vertex colour tinting it, so one brick texture is
many buildings' brick.
