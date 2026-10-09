# `world/world.js`

The reasoning for this script, kept out of the source (decisions 18 and 19 on #429). The source keeps
its code and the JSDoc types `tsc` reads; the server strips every comment from what it serves
(`agentdata/fleet/strip.py`).

A `###` heading is the declaration or statement the notes sit in or above, in source order.

### The file

/world (#626), the operator's verdict on #400 (2026-10-02): *"we need a option that allows a user to
move around in the browser, preferably at a minimum of 100 frames per second ... the agents become
objects. And when they need a user, a user has to move to the agent to interact with it ... don't
focus too much on themes. Just assume it is a rainy day. And that it can be day or night, depending
on what the local time is."*

A classic script after `common.js` (its `q`, `post`, `pageUrl`, `patchList` and setters), which
imports the vendored three.js r160 itself with `import(q(...))`, as the ink layer does: nothing from a
CDN, and the token on the URL. It reads `/api/fleet` and the stream like the phone page and the chat
view, and posts the desk's own verbs, so it adds no route and no rule. The player's character is
`world/hero.js` (`WorldHero`) and the agents are `world/bots.js` (`WorldBots`). The place is
`world/city.js` (the streets and buildings), `world/street.js` (what is on them) and
`world/scenery.js` (the sky and the plaza's furniture); the frame is drawn by `world/render.js`,
the surfaces are baked by `world/bake.js`, and `world/kit.js` holds what they share, the lights
above all. All are loaded before this script.

Unlike the desk's ink and the map's scene, which draw zero frames at rest, the world draws every
display frame: moving through a place is the point of it. It stops when the tab is hidden, because
`requestAnimationFrame` does.

### `var V_STATE`

The states' colours in the scene. The scene is the world's own look (the operator: don't focus on
themes), so these are fixed: a palette reaches the HUD, which is drawn in the page's tokens, never
the rain.

### `var V_DAY`

Rainy daylight is overcast: the zenith brighter than the horizon, and the fog the horizon's colour,
so the towers fade into the rain instead of standing out against it. Night is near black with a
blue-grey haze, lit by the lamps.

### `var V_REACH`

You talk to an agent only within 3.2 m of it and facing it (`V_FACING`, about 57° either side). That
is the operator's rule, *a user has to move to the agent to interact with it*: nothing on this page
answers, approves or sends from a distance.

### `var V_BUDGET_MS`

The frame budget, 10 ms: 100 frames a second. The page cannot draw faster than the display refreshes
(`requestAnimationFrame` is the display's clock), so a 120 or 144 Hz display runs at its own rate
and a 60 Hz one at 60; what the page controls is that a frame never needs more than the budget,
which `vTune` holds by lowering the render resolution.

### `var V_WARM_MS`

How long a frame may spend warming up (`vWarm`): objects are drawn alone until it is spent, so a
frame stays short while the first real frame comes as soon as the shaders allow.

### `var V_RAIN`

6,000 streaks in one draw call. The rain is a box of streaks around the camera, animated entirely in
the vertex shader from `uTime`, so no drop is touched on the CPU after it is made.

### `var V_FACE`

How near you are, in metres, when an agent's person stops walking and turns to face you: the 6 m the
decision gives for a person stopping as you come up (docs/fleet-world.md), so talking still means
walking up, and the person you walk to holds still for you.

### `var V_STROLL`

An agent's walking pace, in metres a second: a stroll, slower than the street's walkers (1.1 to 1.6).

### `var V_DESK`

The arc a desk takes round the office's ring, in metres: its 1.5 m and a little room. With the four
doors it sets the ring's size for the fleet (`vLayout`), 7 m across at the least.

### `var V_SEAT`

How far a sitting agent is raised onto its chair: the seated pose holds the pelvis at the
wheelchair's height, an office chair's seat is a little higher.

### `var V_SKIN`

An agent's person's skin tones: the street's walkers' palette, so the agents look like the city's people.

### `var V_LEGS`

An agent's person's trousers: the walkers' palette.

### `var V_HAIR`

An agent's person's hair colours: the walkers' palette.

### `var V_CAP`

The robots are instanced: every agent's shell, glow, ring and beacon is one draw call each, however
many agents there are, up to 64 (`WorldBots`). The scene's draw calls do not depend on the fleet's
size: the city is merged by material, the cars and people are instanced, and the names are words on
the page, not in the scene (`drawLabels`). About fifty on the `low` path; the mirror draws the scene
a second time on the others. Your character adds its own ten (twelve
in a wheelchair, `WorldHero`), whatever the fleet's size too.

### `var V_LOOK_KEY`

Your character's look is kept in the browser (`localStorage`), as the desk keeps a skin: it is how you
appear to yourself on this machine, not a fact about the fleet, so the server never stores it.

### `var V_STRIDE`

Radians of leg swing per metre walked: about a 0.9 m stride at the character's height, so the feet
do not slide on the ground at a walk or a run.

### `function vHour`

`?hour=` pins the clock, for tests and screenshots; otherwise the local time decides, read again
every minute.

### `function vDaylight`

Dawn from 06:00 to 07:30 and dusk from 18:30 to 20:00, eased: 1 is overcast day, 0 is night.

### `function vContext`

WebGL2, else WebGL (three.js r160 draws on either), else nothing: a browser without either gets
`#wnogl`, which says why and links the chat view and the desk, where the same agents are.

### `function vRain`

Each streak is two vertices sharing a drop's random `xyz`; `w` says which end. The box (44 m wide,
22 m high) follows the camera through `fract`, so a drop stays where it is in the world until it
leaves the box and comes in on the other side: walking through the rain, not carrying it. Streaks
fade with distance in the shader rather than through the scene's fog, which a `ShaderMaterial`
would have to include. A streak is lit by the same lights as everything else (the kit's nearest
lights, `uWkP` and `uWkC`), in their colour, so the rain shows in the lamplight, the neon and the
headlights, as it does on a street.

No streak falls under the office's roof: a streak inside its radius and below its height is not drawn (`uRoof`, set by `vPlaza`).

### `function vSplash`

Rings where drops land: one instanced ring drawn 160 times, each growing and fading on its own phase
and moving to a new spot each cycle, in a 16 m square around the player. Thin and faint, fading as
the square of its age: a splash is a ripple on a wet street, not a white disc.

None lands under the office's roof (`uRoof`).

### `function vProps`

The ground (`WorldCity.ground`: plaza, roads and sidewalks in one shader) and the plaza lamps' halos,
made once. The plaza, the city and the street wait for the circle's size (`vPlaza`). There are no
three.js lights for the lamps: every lamp, sign and headlight is one of the kit's lights, and a pixel
adds up the nearest few (`WorldKit.pick`).

### `function vFigures`

The agents as robots, four instanced meshes (`WorldBots.build`): the shell, the glow (eyes, smile,
lights) in its state's colour, the ring and, for an agent that needs a person, the beacon.

### `function vPlaza`

The office for its edge (`WorldScenery.plaza`), made again only when the edge moves by a tenth of a
metre or more: its lamps' glass places the halos and the lamps' lights; its planters, trees and lamp
posts become `vState.solids` with the desks', which you walk around (`vStep`); its wall is
`vState.wall`, and the rain and the splashes learn its roof (`vRain`). The plaza's paving ends 6 m
further out, where the ring road begins: when that changes (by half a metre or more) the city and the
street are built again round it (`WorldCity.build`, `WorldStreet.build`), their lights replace the old
ones, and the buildings' footprints become `vState.boxes`. On the `low` path both are built plainer
(`lod` 0). With the street come its crowd and so the agents' people (`WorldPeople.agents`).

### `function vLayout`

A desk for every agent round the office's ring, sorted by name and dealt round its four quarters,
each quarter's desks spread between its doors and kept half a metre clear of them; the ring grows with
the fleet (`V_DESK`). Each agent's places follow from its desk: its chair (the home, 0.64 m in from the
desk, facing out) and a spot beside the desk to stand at. A row that leaves takes its figure and its
label with it.

### `function vPlace`

The agents where their state puts them (`vWalk`), the robots or people (`vBots`), the office round
them (`vPlaza`), their desks and screens' cells (`WorldScenery.placeDesks`) and footprints, and a light
over every desk.

### `function vBots`

The robots where the agents are, at time `t` (`WorldBots.place`): on every layout, and every frame
unless motion is reduced, when they hold still. Where the crowd is drawn, the agents are people instead
(decided 2026-10-05, `vPersons`): the robots' rings stay, at their feet, and their beacons; the robot
itself is the fallback where there is no crowd (the `low` path). A person who has left (`vWalk`) is
neither drawn nor ringed.

### `function vWalk`

Where each agent's person is, from its state (decided 2026-10-05, "Agents become people", and the
office, 2026-10-06):
- working (`running`, `starting`): at its desk, typing;
- needing you, or talking to you: standing beside its desk, facing the middle, under its beacon;
- idle and the rest: at its desk, sitting back;
- done: walks round inside the ring to the nearest door, out through it, and is gone (no label, no ring).

It walks at `V_STROLL`, straight (a step from the chair to the desk's side, a chord inside the ring to
the door) and takes the pose its goal asks for on arriving; standing, it faces you within `V_FACE`.
Under reduced motion, and for an agent seen for the first time, it is placed at its goal at once (a
done one already gone).

### `function vPersons`

Each agent as a person: one of the crowd's characters (`WorldPeople.cast`), where `vWalk` put it:
walking (the crowd's free-armed walk, at its pace), typing or sitting back on its chair (raised by
`V_SEAT`), presenting while you talk to it, standing otherwise, facing where `vWalk` turned it. Its
shirt is its own colour, the hue its robot was tinted, darker, so a person and the robot it replaces
read as the same agent; skin, trousers and hair come from the walkers' palettes by a hash of its name.

### `function vWeather`

Day and night move together: the sky (its clouds, and the city's glow on them by night), the fog,
the light, the lamps (their glass, halos and point lights), the city's lit windows, the beacon, the
rain's colour and lamplight, and the fill of your character (`WorldHero.fill`) and the robots
(`WorldBots.fill`), the share of windows lit, and the exposure, which opens up at night as an eye
does. By night the sun becomes a dim blue moonlight rather than going out, so the robots and the
buildings keep their shape.
The fog thins by day (0.014) so the towers read as shapes in the rain; it thickens at night.

### `function vReflect`

The environment every material is lit and reflects by (`PMREMGenerator`): Poly Haven's two
photographed city skies (`WorldAssets.dome`), an overcast Potsdamer Platz by day and lamp-lit
Hansaplatz by night, blended by the daylight, so a car's paint, a wet bin and the shop glass carry a
real city's light. Without them (a file missing) it is the procedural sky, as before. It is made again
only when the weather is (once a minute without `?hour=`), never per frame.

### `function vBuild`

The materials take Poly Haven's photo textures where they loaded (`vState.assets.tex`, handed to
`WorldBake.make`), the environment its skies, and the street its scanned props (`WorldAssets.scans`,
uploaded once and handed to every `WorldStreet.build`). The trees' file is made into the trees' kinds
the same way (`vState.woods`), for the street and the plaza, and the cars' file into the cars' shapes
(`vState.fleet`), except on the `low` path, which keeps the page's own trees and cars: they cost fewer
draw calls than the files' kinds and levels of detail.

The office's furniture is made once too: its file's shapes (`vState.kit`) and the desks' instanced meshes and screens (`WorldScenery.desks`).

The renderer checks each shader's compile and link logs (`debug.checkShaderErrors`) only under test
automation (`navigator.webdriver`, which is how a broken shader fails a test: three.js says so in
the console) or with `?shaders=check`. Reading a log is a round trip to the GPU process that waits for
everything queued before it, the textures being uploaded too, and the first frames made one for every
program: the check cost the first real frame about half a second (2026-10-06, page loads). three.js's
own advice is to switch it off in production.

### `function vPad`

The first connected gamepad, in the Gamepad API's standard mapping: axes 0 and 1 the left stick,
2 and 3 the right; buttons 0 A, 1 B, 3 Y, 7 RT, 9 Start, 10 L3, 12 to 15 the D-pad.

### `function vStep`

One step of the walk: the left stick or WASD moves (Shift, RT or L3 runs), the right stick, the
mouse (with pointer lock) or Q and the arrows turn. The agents are solid (you stop a metre from
one), as are the plaza's benches, trees and lamp posts and the street's furniture (`vState.solids`)
and the buildings (`vState.boxes`, pushed out to the nearest side), and you walk up to 150 m from
the plaza (`WorldCity.LIMIT`). A new press of A beside an agent opens it. With a
conversation open, the pad drives the panel instead (`vPanelPad`); with the character picker open it
drives the picker (`vWhoPad`). Start opens and closes the picker, Y changes the view.

The distance walked is what the character's legs swing by (`vState.walk`, backwards when you back
up) and what a wheelchair's wheels turn by (`vState.rolled`); the speed, as a share of a walk, is how
far they swing.

The office's glass is a wall, but for its doors: you stay 0.35 m off it, on the side you were, wherever you are not in a door's opening (`WorldScenery.opening`).

### `function vCamera`

Third person by default, as the operator's picture is a character you see: the camera 3.4 m behind
and 2 m up, looking past the character's shoulder to a point ahead of it, and tilted by the pitch.
In a conversation it swings 1.3 m to the side, so you see your character presenting to the agent and
the agent beside it. First person (V, or Y) is the eye at 1.6 m, as before. While you choose who you
are, the camera stands in front of the character and looks at it.

Inside the office the third-person camera is drawn in toward you until it is within the glass, and kept under the ceiling.

### `function vHeroFrame`

The character stands where you stand and faces where you face. Standing still, the stride eases to
the nearest step where both feet are down, so it never stops mid-stride. `talk` blends into the
presenting pose over about an eighth of a second, and at once under reduced motion. In first person
the character is hidden: you are behind its eyes.

### `function vLoadLook`

`?who=N` starts from the Nth preset (for tests and screenshots); otherwise the look kept in this
browser, read through `WorldHero.normal` so a stale or hand-edited one cannot break the build. None
means a first visit.

### `function vLook`

A choice in the picker builds the character again and keeps the look at once: there is no "save",
what you see is who you are.

### `function vView`

The view is also written on `body[data-view]`, where a test or a stylesheet can read it.

### `function vWho`

The picker: the keys and pointer lock let go and a conversation closes, so nothing walks or answers
while you choose, and the keyboard lands on the preset you are wearing. Closing it hands the keyboard
back to the scene. A first visit opens it (`vStart`): you choose who you are before you walk.

### `function vLookKey`

Two looks are the same when their normal forms are; a preset reads as chosen only when every option
matches it.

### `function drawWho`

The presets, then one row of choices per option, each row a radio group with its label. Colours are
swatches named by their place in the row ("skin 3 of 8"), never by a word for a person's colour.
The scarf colour row shows only when the hair is a headscarf or a wrap.

### `function vWhoRows`

The picker as a grid for the pad: Done, the presets, then each visible option row.

### `function vWhoPad`

Up and down move between rows, left and right along one (wrapping), A chooses, B or Start closes.
The left stick moves too, with a repeat delay, as in a conversation.

### `function vNearest`

The agent within reach and in front of you, the nearest if several: the one E or A talks to.

### `function vFrame`

The simulation steps with the frame, capped at 50 ms so a stall does not throw you across the plaza.
Until the shaders are compiled (`WorldRender.prepare`, then `vWarm`) the frame warms them instead of
drawing.
The robots move and the ground's ripples and the beacon's bands run with the frame's time; under
reduced motion they hold still. The traffic and the people move (`WorldStreet.frame`), the nearest
lights are picked, and `WorldRender` draws the frame. On a software renderer (`WorldRender.soft`)
the scene is drawn at most about ten times a second, while the walk and the labels keep the
display's pace: SwiftShader on a CPU cannot draw a city faster, and a page that tried would starve
everything else on the machine, the tests included.
`FleetWorld.hold(true)` stops the steps (the frame still draws) so a test can drive `step()` itself.
The resolution is tuned (`vTune`) before the frame is drawn, never after: a resize clears the canvas,
and one made after the draw, in the same task, put the cleared canvas on the screen instead of the
frame, black for as long as the scale kept moving. A resize forces this frame to draw. Nothing is tuned
while the shaders compile, and the frame times start again when they are done: the frames that compile
are slow for a reason that is over, and counted, they stepped the quality down before the first real
frame.

### `var vTask`

The channel `vPrewarm` posts to for its next step: a message is a task the page runs as soon as it
can, where a timer in a hidden or prerendered page is held back (to once a second, or not at all).

### `function vPrewarm`

The warm-up (`vWarm`) while the page is being prerendered (`common.js`'s `prerender`: the pointer
rests on a link to the world) or opened in a tab behind the one in front. Such a page draws no
frames, so the warm-up that runs in `vFrame` would wait for the click; run in steps here, it is done
before, and the click shows the first real frame. The background compile `WorldRender.prepare`
starts reports itself on timers, which a hidden page holds back, so it is asked here instead, each
step, whether its programs are ready (`isReady`, which does not wait). Each step flushes the context
first: a page that draws no frames never sends its queued commands to the GPU, the compile included,
and the programs were still not ready when the page was shown. Once the page is shown,
`vFrame` carries on from wherever it got to; a `compiled` that arrives after the warm-up is ignored.

### `function vWarm`

Before the first real frame, each object is drawn alone (into a 1 by 1 target, or a single
scissored pixel of the screen on the `low` path, whose shaders are compiled for the screen), and then
the passes. The world has some forty shaders; compiled all at once in the first frame they froze the
page for seconds on a software renderer (and noticeably on Windows, where shaders compile slowly),
long enough that nothing else on the page could run. So the objects are spread over frames: as many
a frame as fit in `V_WARM_MS` (8 ms), and an object whose shader is slow to compile has a frame to
itself, so the page answers between them. The scene is drawn when the last is compiled.

It was one object a frame until 2026-10-06 (the operator: "We're aiming for ~200ms loads. Right now
we're at several seconds"): with the office's 110 objects that held the first real frame back by 110
frames, about 1.3 s on a 120 Hz display, although `WorldRender.prepare` had compiled nearly all of them
in the background already and each draw took a fraction of a millisecond.

### `function vTune`

Once a second. The display's own frame interval is the fastest the page has seen (the 5th
percentile). On a display at 100 Hz or slower the target is that interval: hold the display's rate.
On a faster one the target is the 10 ms budget: a 144 Hz display may run at 100 fps before the
resolution drops. A median frame 15% over the target lowers the render scale by 15%, down to half
resolution; one under it raises it again, up to the device's pixel ratio or the tier's cap. When
half resolution is still too slow, the quality steps down a tier (`WorldRender.step`). Says whether it
resized, so `vFrame` draws straight after.

Not in the first 15 s after the warm-up (`calmFrom`): the shaders compiled late (the people, the
crowd, whatever the first views bring in) make the first frames long, and on a laptop with an RTX 3050 Ti
that alone dropped a page asked for `medium` to `low` (2026-10-05), where it stayed: nothing ever
stepped a tier back up. Now, once per page (`rose`), a tier below the one the page started at steps
back up when, settled, the median frame is under half the target at full resolution.

### `function vMaxScale`

The most the render scale may be: the device's pixel ratio or the tier's cap, and on a software
renderer half, always. There the frames that skip drawing (`vFrame`) are quick and the ones that draw
are slow, so the median swung the scale up and down every second, and each change was a resize.

### `function drawLabels`

Each agent's name and state are a label on the page (`#wlabels`), placed over its head by projecting
that point through the camera every frame, and scaled by distance. Words in the DOM rather than
textures in the scene: nothing under `static/` asks for a 2D context (#257, held by
`tests/test_fleet_trace.py`), a label is as sharp as the page's own text at any resolution scale, and
the scene's draw calls do not grow with the fleet. A label behind you, beyond 48 m, off the
screen, under an open conversation or over an agent who has left (`vWalk`) is hidden. The transform is rounded to the pixel and the scale to
a twentieth, so a label is written only when it moves visibly: standing still writes nothing.

### `function drawHud`

The prompt names the agent within reach; the compass points at the nearest agent that needs you and
says how far. The arrow turns in 5° steps, so turning writes the DOM only when it moves a step.
`F3` (or the backquote) shows fps, frame time, the render scale and the draw calls (`?hud=1` at load).

Near an agent it offers both: talking to it (E, A) and taking over its screen (T, X).

### `function drawList`

The agents and their states in words, for a screen reader (`#wlist`), and how many need you in the
header.

And the agents' screens are drawn from their rows (`WorldScenery.screens`), when a row changes.

### `function vTalk`

A conversation: the panel opens, the keys and pointer lock let go, and the keyboard lands on the
first choice, else Approve, else the message box, so a controller can answer at once. You turn to face
the agent, so your character presents to it.

### `function vTakeOver`

Takes over an agent's screen: the chat, on that agent (`/chat#<agent>`), where its conversation and
its session's controls are. The operator chose the chat as the screen taken over (2026-10-06).

### `function vPanelPad`

In a conversation the D-pad (or the left stick, with a repeat delay) moves between the panel's
buttons and fields, A presses the button that has the keyboard, and B steps back. A choice fills the
answer, so a question with choices is answered without a keyboard; free text needs one.

### `function vRefresh`

Each agent's stream cursor starts at its `last_seq`, as the chat view's does: the stream sends what
happens next, never every agent's history again.

### `function vStart`

three.js, the fleet and the CC0 files (`WorldAssets.load`) are fetched together; the world is built
when all three are in. A file that does not load is left out and its procedural stand-in is used.

### `window.FleetWorld`

What the tests and the laptop read: `inspect()` (the agents, the player, who is near, day or night,
the lamps, the quality and whether the renderer is software, the town (buildings, lights, cars,
parked cars, people, scanned props), how many of the CC0 textures, skies and props loaded (`cc0`), the scene's draw calls and triangles and the whole frame's draw calls
(`passes`), fps, frame and work time, the render scale, the view, the picker, the look, and the
character's place, turn and pose), `hold()` and
`step()` to walk without depending on the frame rate (CI draws in SwiftShader; it moves the agents'
people too, `vWalk`), and `teleport(repo)` to stand within reach of an agent. Each agent in `inspect()`
says where `vWalk` has it (`mode`: `walk`, `stand`, `sit` or `gone`).

### People (2026-10-03)

`vStart` hands `WorldPeople` the people `WorldAssets` loaded, unless vertex textures cannot hold floats
(then the procedural character stays). `vStep` keeps the turn rate (`turn`), which the character steps
on the spot to. `inspect().hero` reports `kind` (`skinned` or `doll`) and the pose measured from the
character either way (`WorldHero.measure`); `inspect().people` says whether the realistic character (and, in `parts`, what each of its parts draws with)
and how many crowd characters are in.

