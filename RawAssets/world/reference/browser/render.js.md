# `world/render.js`

The reasoning for this script, kept out of the source (decisions 18 and 19 on #429). The source keeps
its code and the JSDoc types `tsc` reads; the server strips every comment from what it serves
(`agentdata/fleet/strip.py`).

A `###` heading is the declaration or statement the notes sit in or above, in source order.

### The file

How a frame is drawn. The operator asked for the look of the large open-world games, in a browser,
and kept the 100 frames a second asked for on #400. What separates those games from a plain WebGL
scene is mostly not polygons but the frame's pipeline: light in high dynamic range, a filmic tone
curve, the scene reflected in the wet street, ambient occlusion in the creases, bloom round every
light, and a grade. This file is that pipeline, written against three.js r160's own render targets
and a full-screen triangle: no post-processing package, nothing fetched.

A classic script after `world/kit.js`. It defines one global, `WorldRender`.

### `var TIERS`

Four qualities, each a set of costs:
- `low`: drawn straight to the screen with three.js's ACES tone curve, nothing after it; the city's
  plainer facades (`WorldCity`, `lod` 0); 8 lights a pixel;
- `medium`: the full pipeline at lower cost (FXAA instead of MSAA, half-resolution occlusion, the
  mirror at a third), 16 lights;
- `high`: 4x MSAA, the mirror at half resolution, 24 lights;
- `ultra`: full-resolution occlusion, a sharper mirror, 32 lights, up to twice the CSS pixel ratio.

### `function choose`

`?quality=` decides when it is given. Otherwise the GPU's name does: a software renderer
(SwiftShader, llvmpipe, Windows' Basic Render Driver: CI, and virtual desktops without a GPU) gets
`low` and the `soft` path; an integrated GPU `medium`; anything else `high`. WebGL1 is `low`, as
the pipeline needs WebGL2's half-float targets. `vTune` steps a tier down when even half resolution
cannot hold the frame rate.

### `function materials`

The passes, each a fragment shader on the full-screen triangle:
- `ao`: scalable ambient obscurance from the depth buffer, 14 samples on a spiral turned per pixel,
  normals rebuilt from depth, at half or full resolution;
- `blur`: a bilateral blur, which keeps the occlusion from bleeding across depth edges;
- `pre`, `down`, `up`: bloom as a mip chain, a soft knee over the bright pixels, a stable four-tap
  average (bright single pixels are weighted down, so a spark does not flicker) and a tent filter
  back up;
- `comp`: occlusion (less on bright pixels, which light themselves), bloom, exposure, the ACES curve,
  a cool lift and warm highlights at night, a little vignette, faint lens fringing at the edges, and
  grain, which hides banding in the fog;
- `fxaa`: edge smoothing where MSAA is off.

### `function alloc`

The targets for the tier, sized to the drawing buffer: the scene in half float with a depth texture
(multisampled where the tier asks, and three.js resolves its depth too), the occlusion pair, the
bloom chain, the mirror with mipmaps (its roughness picks a mip), and an 8-bit target before FXAA.
They are made again only when the size or the tier changes.

### `function create`

The camera also sees layer 1, where the ground and the rain live; the mirror's camera does not, so
the street is never in its own reflection and the rain does not double.

### `function setTier`

`low` hands tone mapping to three.js; the others tone-map in `comp`, so the scene stays linear and
unclamped until the end.

### `function mirror`

The camera reflected in the street (y = 0), with an oblique near plane on the street itself, so
nothing below it is drawn into the reflection. The matrix the street samples it with is written to
the kit's uniforms.

### `function warmPasses`

Draws each pass once into a 1 by 1 target, so their shaders are compiled before the first real frame
needs them (`vWarm`).

### `function prepare`

Compiles every material in the scene, and the passes, with `compileAsync`: where the browser has
`KHR_parallel_shader_compile` (Chrome on Windows does) the GPU compiles them in the background and the
page never freezes for it. Where it does not (SwiftShader), the promise resolves at once and `vWarm`
spreads the compiling over frames instead.

The scene is compiled for where it is drawn: into `R.hdr`, or the screen on the `low` path. three.js
compiles a material once for the screen (with the tone mapping and the sRGB output) and once for a
render target (linear, no tone mapping), and `compileAsync` compiles for whichever target is set. It
ran with the screen's, so on every tier but `low` it compiled programs the scene never used, and the
ones it did use, some fifty for the office's world, were compiled one by one in `vWarm`, blocking,
at 15 to 140 ms each (2026-10-06, page loads).

### `function render`

One frame: the mirror, the scene, occlusion, bloom, then the composite to the screen (or through
FXAA). Draw calls and triangles are counted for the scene's passes (`calls`, `triangles`), and for
the whole frame (`frameCalls`), which `FleetWorld.inspect()` reports.
