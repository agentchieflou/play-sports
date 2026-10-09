# `world.css`

The reasoning for this stylesheet, kept out of the source (decisions 18 and 19 on #429). The source
keeps its rules, and the server strips any comment from what it serves (`agentdata/fleet/strip.py`).

A `###` heading is the rule or at-rule the notes sit in or above, by its selector or prelude, in
source order. A builder who changes a rule changes its note here.

### The file

/world (#626): the HUD over the 3D scene, over `app.css` (its tokens, its toolbar, `.chip`,
`.linkbtn`, `.ask-choice`). The scene has its own rainy look; the HUD wears the page's palette, in
tokens only, so it reads on every palette. No word is in `--muted`.

### `#world`

The canvas fills the window behind everything; the HUD sits over it at a higher `z-index`.

### `#wlabels`

The agents' name labels, over the canvas and under the HUD, taking no pointer events. Each `.wtag` is
placed by its `transform` alone (`drawLabels`), from the top left, scaled from its bottom centre so it
stands on the point over the agent's head; `will-change` keeps moving it a compositor job.

### `.wtag.running, .wtag.starting`

The state is the label's bottom edge in its token, and its words; never the label's fill.

### `.wtag.near`

The agent within reach, the one E or A would talk to, is ringed in the focus colour.

### `.whud`

The toolbar is translucent and blurred, so the rain shows through without the words losing their
contrast.

### `.wcompass, .wprompt, .whelp`

Pills centred over the scene that take no pointer events, so a click on them still reaches the
canvas (which takes the pointer lock). The compass sits under the toolbar, the prompt low in the
view, near where the agent stands, and the controls along the bottom.

### `.wpanel`

A conversation is a panel on the right, so the agent you are talking to stays in view on the left.

### `.wpanel :focus-visible`

A heavy ring: with a controller the focus ring is the only cursor there is.

### `.wwho`

The character picker is a panel on the left, so the character it dresses stays in view on the right,
where the camera turns to face it (`vCamera`). It scrolls inside itself when the options outgrow the
window.

### `.ww-choice[aria-checked="true"]`

The chosen option is ringed in the focus colour and set in bold, so it reads without colour.

### `.ww-swatch`

A colour choice is a swatch of that colour (`--swatch`, set per button); chosen, it gets a double
ring, panel then focus, so the ring shows whatever the colour under it.

### `.wwho :focus-visible`

The same heavy ring as in a conversation: with a controller it is the only cursor.

### `.wnogl`

Said in words when the browser cannot draw the world, with the way to the same agents elsewhere.

### `@media (pointer: coarse)`

44 px targets and 16 px fields on a touch screen, as on /m; the picker's swatches are 44 px square.
