# The world

`/world` (#626) is the fleet as a place: a wet plaza in the rain, one robot per agent, and you, a
character you choose, walked with a controller or the keyboard. The operator chose it as the next thing to explore
when asked which parked concepts came next (#400, 2026-10-02):

> the next thing we're going to explore is the 3d space. We have users that want the traditional setup
> that I requested earlier in this session, but we also have users that envision a future where they're
> able to interact with their agents via a controller. And in that instance, we need a option that
> allows a user to move around in the browser, preferably at a minimum of 100 frames per second, given
> that we know full games can be played in the browser at 120 frames per second. With this, right, the
> agents become objects. And when they need a user, a user has to move to the agent to interact with
> it. Given that this is a 3D space, don't focus too much on themes. Just assume it is a rainy day. And
> that it can be day or night, depending on what the local time is.

It is one more page beside the desk (`/`), the chat view (`/chat`) and the map (`/map`), never a
replacement for any of them. The desk's toolbar links it (*world*), and `/open?page=world` lands
there. On a phone-width desk (640 px and under) the link gives its room to the others: the world is
walked with keys or a controller, and `/world` still opens there.

## Moving

| | Keyboard and mouse | Controller (standard mapping) |
| --- | --- | --- |
| walk | W A S D, or the arrows | left stick |
| look | the mouse, after a click on the scene (pointer lock); Q and ← → turn | right stick |
| run | Shift | RT, or press the left stick |
| talk to the agent in front of you | E or Enter | A |
| in a conversation | Tab | the D-pad (or the left stick) between buttons, A to press |
| step back | Esc | B |
| third or first person | V | Y |
| choose your character | C (Esc closes) | Start (B or Start closes; the D-pad moves, A picks) |
| show fps and frame time | F3 or \` | — |

## Your character

The operator, with a picture of a friendly cartoon figure in a light-grey shirt and navy trousers, one
arm out, presenting: *"Use some one like this as the main character. Create diverse character options
so everyone feels included."*

Your character is a realistic human: a skinned, rigged body with its face, hair, clothes and shoes,
animated on the Unreal Engine body skeleton (`world/people.js`). It idles (breathing, a slow weight
shift), walks into a jog and a run with your speed, steps round when you turn on the spot, presents
when you talk, and sits in its wheelchair pushing the rims. Today's character is a stand-in built from
MakeHuman's CC0 assets; the realistic people are to be MetaHumans the operator exports from Unreal
Engine, dropped into `static/world/people/` with its manifest, `people.json` (see *The people* below).
If those files are missing or a browser cannot skin a mesh, the page's own cartoon character stands in.

You see your character from behind and over its shoulder (third person, the default), or through its
eyes (first person, V or Y). It walks with you: legs swinging with the distance walked, so the feet
do not slide, arms against the legs. When you talk to an agent you turn to face it, the camera swings
to the side, and your character presents to it, one arm out, palm up, as in the picture.

The first visit opens **Who are you in the world?** (C or Start opens it again). Start from one of ten
looks, then change anything:

| | |
| --- | --- |
| skin | eight tones, deep to light |
| hair | a side part, curls, coils, long, a bun, locs, a buzz cut, none, a headscarf, a wrap |
| hair colour | black, browns, auburn, blond, silver, and blue |
| scarf or wrap | its own colour, when the hair is covered |
| face | none, a beard, a moustache |
| glasses | none, round, square |
| shirt, trousers | eight and five colours |
| figure | angular, between, curved (the realistic character's body shape) |
| build | slim, regular, broad |
| age | young, middle, older (the realistic character) |
| moves by | walking, or a wheelchair whose wheels turn as it rolls and whose rims the arms push |

No option or look is named for a gender: they are looks a person chooses, not a box they are put in.
The ten looks (*side part*, the operator's picture, then *curls*, *headscarf*, *locs*, *silver bun*,
*beard*, *wheelchair*, *long hair*, *wrap*, *bald*) between them span every skin tone, every hair
texture, both coverings, both kinds of glasses, all three builds and a wheelchair user. A colour is a
swatch named by its place in the row (*skin 3 of 8*), never by a word for a person's colour.

Your choice is kept in this browser (`localStorage`, `fleet.world.look`), not on the server: it is
how you appear to yourself on this machine. `?who=0..9` starts from a look, `?view=first` in first
person.

## The agents

Every `/api/fleet` row has a desk in the office, sorted by name round its ring; where people cannot be
drawn, the agent is a small robot at its desk. Each robot has a white shell tinted a pale colour from its name, a dark visor with eyes and a
smile, a chest light, an antenna and a hover base, and a ring at its waist. The eyes, the lights and
the ring are in its state's colour and glow at night. It bobs on its base, faster while it works,
when its ring turns too; under reduced motion it holds still. Over its head is a label with its name
and state in words. The label is page text, not a texture, so it stays sharp.

An agent that needs a person raises a red beam that shows through the rain and the fog (and fades
when you stand at it, so it never fills the view), its eyes pulse, and the HUD's
compass points at the nearest one with its distance. **You talk to an agent only within 3.2 m of it,
facing it.** That is the operator's rule ("a user has to move to the agent"): nothing on this page
answers, approves or sends from a distance. A conversation opens beside the agent, with:

- its state, its sentence, and its last words;
- the approval it is waiting on, if any (Approve, or Deny with a reason);
- its open questions, each with its choices as buttons, so a controller can answer without a keyboard;
- a message box (free text needs a keyboard; there is no on-screen keyboard yet).

Everything posts the desk's own verbs (`answer`, `approve`, `deny`, `send`, or `say` to a console), so
every refusal is the server's, in its words. The page adds no route.

## The place

The operator, 2026-10-02: *"We're looking for a far cry 3 / rdr2 / gta 6 / cyberpunk quality world
with rich assets and quality features that can run in browser."* The office is the middle of a
district you can walk into.

- **The office** (the operator, 2026-10-06; see *The office* below) is a round glass pavilion on the
  plaza's granite, its brass compass in the middle of the floor: a desk for every agent round a ring,
  each with a screen showing what that agent is doing, a door to each avenue, planters with young
  lindens outside and inside. The ring, the office and everything round it grow with the fleet.
- **The streets.** A ring road goes round the plaza and four avenues lead out of it, crossed by side
  streets every 64 m. They have:
  - lane markings, zebra crossings, stop lines and manhole covers;
  - granite kerbs, and gutters where the water gathers;
  - street lamps every 24 m (sodium orange or LED white), each with a cone of light in the rain;
  - traffic lights at every crossing, cycling red, amber and green;
  - trees, hydrants, bins, newspaper boxes, utility cabinets, parking meters and bus shelters;
  - on the side streets, bags and boxes by the bins, air-conditioning units against the walls,
    concrete barriers at a few corners, and the odd car under a cover.
- **The buildings.** About 150 of them, in five styles: brick walk-ups with fire escapes and water
  tanks, stucco with balconies, concrete, stone with cornices, and glass towers further out. They
  have:
  - windows set into the walls;
  - shopfronts with awnings, lit signs and neon;
  - rooftop air-conditioning units, antennas and billboards.

  Behind every window there is a room: its walls, floor, ceiling and furniture are drawn by the glass
  itself (interior mapping). By night some rooms are lit, warm or cool, switching on and off over the
  evening; some show only a television's flicker.
- **Life.** Cars drive the avenues and the ring road. They keep their distance and stop at red
  lights, with their headlights on the wet road at night. Cars are parked along the side streets,
  and people with umbrellas walk the sidewalks.
- **Walking.** You walk up to 150 m from the office. Buildings, desks, planters, trees, lamp posts and
  street furniture are solid, and so is the office's glass: you go in and out by its doors.

The shapes are built in the page from code: three.js's own shapes, merged by material, and
shaders. The surfaces and the small things are real:

- **Photo textures.** Brick, stucco, concrete, asphalt and sidewalk are Poly Haven's photographed
  materials, at the size each covers in reality (a 3 m photo of brick is laid 3 m wide). The rest
  (stone, granite, metal, roofing, wood, leaves) are baked on the GPU when the page loads
  (`world/bake.js`). Every surface has colour, roughness, occlusion and relief.
- **Scanned props.** The hydrants, bins, bags, boxes, cabinets, air-conditioning units, barriers and
  covered cars are Poly Haven's photo-scanned models, decimated to a few thousand triangles and
  drawn instanced: one draw call per kind of prop, however many stand in the street.
- **Real skies for light.** The light everything is lit and reflects by comes from two photographed
  city skies, an overcast square by day and a lamp-lit one by night, blended with the hour. The sky
  you see is still the page's own, raining.

The operator allowed them on 2026-10-03: *"If it is open source and safe, you may use Poly Haven
assets."* Every one is CC0 (public domain), credited by name and author in
`agentdata/fleet/static/world/cc0/LICENSE`. They were made small for a browser before they were
committed (WebP textures, decimated models, 512 x 256 skies): 4 MiB in all, in the package, served by
the fleet's own server like its scripts. Nothing comes from the internet. If a file is missing, the
world falls back to its own version of that thing (a baked material, the sky shader, a built
hydrant). The world's script is 74.2 KiB.

### The trees

The operator, 2026-10-05: *"Work through everything you need to work through to enhance the people
and the environment. We also have UE's TwinMotion installed"*, and then, for the environment,
*"Building environment assets in UE/TwinMotion"*. Twinmotion was not installed (the launcher lists
Unreal Engine 5.8, its Fab plugin and Quixel Bridge), and what its library holds could not be used
here if it were: the Twinmotion EULA (section 1.2) lets its content be used only to visualise inside
Unreal Engine, Twinmotion, RealityCapture and UEFN, and to export images and videos. That covers its
plants, people, vehicles and props and the Megascans inside it; Unreal Engine's own "UE-only" content
is bound the same way. Neither may be drawn by this page, even from the operator's own disk. What may
is what is made here, so the trees are grown here, and the old ones (a stick, two branches and a few
dozen oversized leaf cards) were the environment's weakest thing.

They are one file, `agentdata/fleet/static/world/trees/trees.glb` (1.2 MiB), grown by
`tools/world/trees/` from nothing but its own numbers and under the repository's licence (the folder's
`LICENSE`): Blender, headless, draws each species' leaf (outline, lobes, teeth, veins, colour), lays
leaves along twigs and renders clusters of them into one atlas (colour with occlusion, normals,
roughness), makes the barks, and grows each tree (a trunk with its root flare, scaffold limbs from the
crown's base, branches and twigs to an irregular crown), then spreads the twig cards evenly through
the crown's outer shell and bakes how much sky each one sees into its colour. Node packs it (WebP,
glTF's Y up).

- **On the avenues** London planes (the mottled bark that flakes in patches, broad crowns) and
  lindens (heart-shaped leaves, oval crowns), one species a stretch of avenue between crossings, as a
  city plants them, about 10 m tall, in their pits. A spot that falls at a lamp is left empty: a crown
  that size would swallow the lamp's head.
- **Round the office** young lindens in planters, 6.5 m, eight outside and four inside behind the
  desks (smaller, as indoor trees).
- **Near and far.** Within 42 m of the eye a tree is drawn whole (3,000 to 5,000 triangles), further
  away with fewer and larger twigs (about a third of that). They are instanced: a few draw calls for
  each kind of tree, however many stand in the street. Their leaves let light through, sway, and fade
  where a card turns edge-on rather than showing as a streak.
- **Cost.** On the operator's laptop (RTX 3050 Ti, Chromium, 1600 x 900, `high`, held at full
  resolution) a frame went from 2.5 ms to 2.7 to 2.8 ms with the trees, and from 107 draw calls to 131
  to 135 (the mirror of the wet street draws them twice) and from 1.32 to 1.43 million triangles.
- The `low` quality keeps the page's own trees: two draw calls for all of them.

### The cars

The cars were the next weakest thing: boxes extruded from a side profile, a sedan and an SUV. They are
grown here too, for the same reason as the trees, one file, `agentdata/fleet/static/world/cars/cars.glb`
(0.4 MiB), lofted by `tools/world/cars/` and under the repository's licence (the folder's `LICENSE`),
after no maker's design.

- **Four kinds:** a sedan, a hatchback, an SUV and a van, each body lofted from its own profiles (the
  top line from the bumper over the bonnet, windscreen, roof and rear window to the boot, the
  beltline, the widths, the underside raised over the wheel arches) and closed at the ends with rounded
  noses. Glass, head and tail lamps in their housings, grille, intakes, plates, pillars and door seams
  are laid on the body from outlines seen from the side, above or the ends; tyres and five-spoke rims
  are built for each wheel.
- **Painted by the page:** the body takes each car's colour through the clearcoat paint, as before;
  the glass, trim and lamps keep their own materials, the lamps lit on cars that drive.
- **Near and far.** Within 36 m of the eye a car is drawn whole (about 3,900 triangles); further away
  as about 850 triangles in two draw calls, the glass and trim in the body by colour.
- **Cost.** On the operator's laptop (RTX 3050 Ti, Chromium, 1600 x 900, `high`, held at full
  resolution) a frame went from 2.7 to 2.8 ms (with the trees) to 3.1 ms, at 147 to 163 draw calls
  from 131.
- The `low` quality keeps the page's own two cars.

### The office

The operator, 2026-10-06: *"Let's create an office building with the humans as agents and when we walk
up to them we have the opportunity to 'take over their screen' which would bring us back to the
Desk/Chat screen."* Asked where, they chose the office in place of the plaza, and the chat as the screen
taken over.

- **The building** is a round pavilion of glass between dark mullions under a flat roof that overhangs
  it, with a door to each avenue and a lamp each side of every door. Inside: a polished floor round the
  plaza's brass compass, a light grey ceiling with two rings of light, a warm light over every desk.
  No rain falls under the roof (the rain and the splashes skip it), the floor is dry, and by night the
  office glows through its glass. It is built in the page (`WorldScenery.plaza`) and grows with the
  fleet: the desks' ring is 7 m across at the least and wider as agents come.
- **The desks** are a file grown here like the trees and the cars,
  `agentdata/fleet/static/world/office/office.glb` (0.12 MiB, `tools/world/office/`): a bench desk
  with an oak top, a monitor, keyboard, mouse and a mug, an office chair, and a planter. They are drawn
  instanced, one draw call a material whatever the number of agents; the desks face out to the glass,
  so from the middle you see every agent from behind, and its screen.
- **The screens** show their agent: its name and state in its colour, what it says it is doing, and
  the last thing it said, a terminal's look on one shared texture (an 8 x 8 atlas), redrawn only when
  an agent's row changes. One that needs you is framed in amber.
- **Taking over a screen.** Walk up to an agent: the prompt offers to talk (E, or A on a pad) and to
  take over its screen (T, or X); the agent's panel has the button too. Taking over opens the chat on
  that agent (`/chat#<agent>`), its conversation and its session's controls.
- **Inside**, the third-person camera stays under the roof and within the glass.
- **Cost.** About 3,600 triangles a desk; the office adds a few draw calls in all. The world's scripts
  grew by 2.9 KiB gzipped (the budget moved to 92 KiB, `tests/test_fleet_serve.py`).

### The people

Poly Haven has no people, so they have their own folder, `agentdata/fleet/static/world/people/`, its
own `LICENSE` and its own bound (5 MiB, `tests/test_fleet_serve.py`). The stand-in is CC0 throughout:
MakeHuman's base mesh, skins, clothes, hair, beards and glasses (credited one by one), made in Blender
with MPFB, rigged with its game-engine rig (Unreal Engine bone names), and put through the world's
people pipeline (an offline Node script, gltf-transform and meshoptimizer): skinned to the rest pose,
turned to face -Z in metres, bones outside the Unreal body set folded into their parents, outfits split
into top and bottom, decimated, quantised, colour maps turned into detail maps so one texture takes
every skin tone and colour the picker offers, WebP textures. `standin.glb` is the player's character
(35,000 triangles across its parts, six hair styles, beards, glasses and five body morphs: angular,
curved, older, slim, broad); `standin_crowd.glb` is five pedestrians at two levels of detail in one
atlas. `people.json` says which file is the hero and which the crowd, which hair, beard and glasses
file each look names, and which morphs each figure, build and age sets: swapping in MetaHumans is a
change to that file and the files beside it, not to the code. A hair style no file has (locs, the
headscarf, the wrap) is drawn for the realistic head and skinned to it.

The pedestrians are that crowd: skinned in the vertex shader from a texture of baked bone matrices,
instanced, a mesh per character and level of detail (detailed within 24 m), each with its own skin,
coat, trousers and hair colours, an umbrella in the right hand. The low quality has no pedestrians,
as before.

## Rain, day and night

It is always raining. There are 6,000 streaks in a box around you and rings where drops land, and
the streaks are lit by the lights they fall past. Everything is wet:

- surfaces are darker and glossier;
- puddles gather in the gutters and the low spots, and drops ring them;
- the street is a mirror of the city above it, sharp in a puddle and smeared on wet asphalt.

The local clock decides the light. Overcast day runs from about 07:30 to 18:30, with dawn from 06:00
and dusk until 20:00. By night:

- the lamps, the shop signs, the neon, the billboards and the cars' headlights light the street
  round them in their own colours;
- the windows light up;
- the city's glow shows on the low cloud.

Hundreds of lights, and a pixel adds up the nearest few. `?hour=0..23` pins the clock, for tests and
screenshots. The scene is the world's own look: a palette or skin you chose dresses the HUD, never
the rain.

## Frames

The frame budget is **10 ms**: 100 frames a second.

- **The display sets the ceiling.** The browser draws at most once per display refresh. A 120 or
  144 Hz display runs the world at its own rate; a 60 Hz display caps it at 60, whatever the GPU.
- **The frame is drawn the way open-world games draw theirs** (`world/render.js`): light in high
  dynamic range, a reflection of the scene for the wet street, ambient occlusion, bloom round every
  light, the ACES tone curve and a grade. There are no shadow maps: under overcast cloud and in the
  rain, occlusion is the shadow.
- **The quality fits the GPU.** `?quality=low|medium|high|ultra` chooses; otherwise the GPU decides:
  - **high** for a discrete GPU: 4x MSAA, the mirror at half resolution, 24 lights a pixel;
  - **medium** for an integrated one: FXAA, the mirror at a third, 16 lights;
  - **low** for WebGL1 or a software renderer: straight to the screen, no mirror, plainer facades,
    8 lights;
  - **ultra** only when asked for.

  Each tier draws the same scene, at a different cost.
- **The page keeps every frame within the budget.**
  - The draw calls do not grow with the fleet. The city is merged by material (a dozen draw calls
    for every building), and the robots, cars and people are instanced (four draw calls for every
    agent, every car and every walker). The names are page text.
  - Your character adds ten draw calls (twelve in a wheelchair). The parts that move together are
    one mesh each, all sharing one material, so a change of look compiles no shader.
  - When frames run long it lowers its render resolution. Once a second it compares the median frame
    with the display's rate (or the 10 ms budget on a display faster than 100 Hz), and steps down to
    half resolution at worst. If that is not enough, it drops a quality tier, but not in the first 15 s
    after the warm-up, when late shader compiles make frames long. It steps the resolution back up when
    there is room, and a dropped tier back up once, when frames run under half the target.
  - The surfaces are baked or uploaded once at load, and the sky's environment made once a minute,
    never per frame.
- **It stops drawing when the tab is hidden.**

`FleetWorld.inspect()` reports:

- `fps`, `frameMs` (the median interval) and `workMs` (the script's own time per frame);
- `scale` and `quality`;
- `calls` and `triangles` (the scene), and `passes` (the whole frame's draw calls);
- `town` (buildings, lights, cars, parked cars, people, scanned props, crowd characters);
- `hero` (where it is, its `kind`, `skinned` or `doll`, and its pose: `legL`, `armR`, `hips`) and
  `people` (whether the realistic character loaded, how many crowd characters, and `parts`: each
  drawn part of your character with its role, whether the look dyed it, its roughness and occlusion
  maps, whether light shows through it (`thin`) and its colour; and `agents`, how many agents are drawn
  as people);
- `cc0`, how many of the CC0 textures, skies and props loaded.

F3 shows the frame figures in the toolbar.

**On a software renderer** (SwiftShader in CI, or a virtual desktop without a GPU), the page draws
its lightest path: the `low` quality, plainer facades, no parked cars or walkers, props only within
75 m, half resolution, and the scene at most about ten times a second. The walk keeps the display's pace. The tests therefore
measure what does not depend on the machine:

- the draw calls (at most 64) and triangles (under 400,000) on that path;
- that the full pipeline compiles and draws (`?quality=high`), and that what reaches the screen is
  the frame, not a cleared canvas;
- that every CC0 file loads;
- the walk (`FleetWorld.hold` and `FleetWorld.step` advance it without waiting on frames), reach, and
  the verbs a conversation posts.

**The frame rate is the laptop's to measure:** open `/world`, press F3, and read the fps line in Edge
or Chrome on the operator's GPU.

## Loading

The operator, 2026-10-06: *"work on optimizing the load time when clicking between chat, desk, and
world. We're aiming for ~200ms loads. Right now we're at several seconds."* Measured on the office's
demo fleet (ten agents) in Chromium on the laptop's GPU, a click on the world to its first real frame:

| | before | after |
| --- | --- | --- |
| the pointer rested on the link for about 2 s | 2.6 s | about 0.1 s (prerendered) |
| a click straight away | 2.6 s | 1.2 to 1.4 s |
| a browser profile's very first load | 7.1 s | about 4 s (the GPU's shader cache is cold) |

What took the time, and what changed:

- **Every file was fetched again.** The server answered everything `no-store`, so each load fetched
  the world's ten megabytes again and kept no compiled script. Static files now revalidate (`ETag`,
  a 304 when unchanged): a second load transfers about 20 KB.
- **The shaders were compiled twice.** `WorldRender.prepare` compiled them in the background for
  the screen, while the scene is drawn into a high-dynamic-range target, which needs other programs:
  those ~50 were compiled again, one at a time, at their first draw. It now compiles for the target
  the scene is drawn into.
- **The warm-up drew one object a frame**, 110 frames for the office's world; it now draws as many as
  fit in 8 ms a frame. Shader logs are read only under test automation (`?shaders=check` asks too):
  each read waited on the GPU.
- **The world is prerendered from the desk and the chat** when the pointer rests on its link
  (Chrome's speculation rules, a 200 ms hover): it builds, compiles and warms up behind the page,
  and the click shows its first frame. Built behind another page it takes about 2 s, so a shorter
  hover saves what it lasted.

The desk opens in about 0.1 s and the chat in about 0.2 s (the server's `/api/fleet` for ten active
agents went from about 0.4 s to 0.1 s: Copilot's session store is read once per change, and a poll
that finds nothing new writes nothing).

What is left on a click straight away is the build (about 0.7 s: the city's geometry, three.js's
scene graph, parsing the models) and the uploads of the textures at the first frames (about 0.4 s).

## What it is not (yet)

- No multiplayer or physics; nobody else sees your character. The cars do not hit you, nor you them.
- You cannot enter the buildings: their rooms are drawn by the glass.
- No on-screen keyboard for free text with a controller.
- The scene is not themed.
- The agents do not speak, and their faces do not move: what they say is text in the conversation
  (decided below).
- It is not the map's scene (#409–#414): that is a different page that draws zero frames at rest.

## Future considerations

### Realistic people (MetaHuman)

Poly Haven has no people. Your character and the people on the sidewalks are MakeHuman's CC0 people
(`static/world/people/`), on a loading and animation path built so that MetaHuman exports drop in
(above). The operator has Unreal Engine installed and named MetaHuman as the source of lifelike people.
#### Decided (operator, 2026-10-05)

- **Source.** MetaHumans for your character and for the agents' people. MakeHuman's CC0 stand-in stays
  as what ships whenever no MetaHuman file is present, so the page never depends on them.
- **Tools.** Unreal Engine **5.8.3** for MetaHuman Creator and the export, Blender **5.2 LTS** for
  cleanup and inspection. Blender MCP may automate the Blender side; it does not replace the people
  pipeline, and it is optional.
- **Files.** MetaHuman exports stay out of git and out of the wheel until the redistribution and seat
  questions below are answered; `people.json` points at a local folder. That folder is
  `~/.agentdata/world/people/` (or `$AGENTDATA_WORLD_PEOPLE_DIR`): with its own `people.json`, the
  server answers `/static/world/people/` from it, and falls back to the stand-in for anything it
  lacks or when it has no manifest (`tools/world/people/README.md`).
- **Size is not the budget; the frame is.** A larger file is not by itself a slower page. Every slice
  optimises both: the people pipeline adds meshopt geometry compression and KTX2 textures, and the
  5 MiB bound on the people folder gives way to frame budgets (frame time, draw calls, triangles) and
  a load-time budget. The bound stays until the slice that brings those budgets in.
- **Agents become people.** Each `/api/fleet` row is a person instead of a robot; the robot stays as
  the fallback. Where the person is comes from the agent's state: idle, it walks the plaza; working,
  it is at a desk or bench; needing a person, it stops, faces you and raises its beam; done, it
  leaves. People walk waypoints on the sidewalks that exist, with no navigation mesh.
  Built so far (agents as people): where the crowd is drawn, each agent is one of its characters in the
  robot's place, standing and breathing, facing you within 6 m and presenting while you talk, its shirt
  the robot's hue, its state ring at its feet and its beacon over it; the robot remains on the `low`
  path. Placement by state: the one that needs you stays at its place on the circle; a working one
  walks to a plaza bench and sits (the first eight, by name); a done one walks out past the end of the
  plaza's paving and is gone; an idle one strolls round inside the kerb; any walking one stops and faces
  you within 6 m. They keep to the plaza's own paving rather than the street's sidewalks, which is
  where the agents are; walking the avenues is left for when there is a reason to go there.
  Since the office (2026-10-06) the places are its desks: a working agent types at its desk; the one
  that needs you stands up beside its desk, facing the middle, under its beacon; an idle one sits back
  at its desk; a done one walks round inside the ring to the nearest door and out, and is gone.
- **Talking still means walking up.** A person stops moving once your character is within about 6 m,
  so the 3.2 m rule above still holds.
- **No voice, no lip-sync, no facial animation.** What matters is what the agents decide and how
  they write it down, not how they sound: that work belongs to the agents' skills, not this page. The
  pipeline keeps folding the face, and idle and busy poses stay procedural (no animation files).

Order of the slices, one issue each: a MetaHuman hero exported on the operator's machine and put
through the pipeline locally, with its cost measured; the pipeline's meshopt and KTX2 with the new
budgets; agents as people; state to place on sidewalk waypoints.

The first slice's cost, measured 2026-10-05 on the operator's laptop (NVIDIA GeForce RTX 3050 Ti Laptop
GPU, 4 GB; Chromium with vsync off, 1600x900, `hour=13` and `22`): with the MetaHuman hero (56,000
triangles, 6.3 MB, nine parts) and the five-character crowd, `ultra` at full resolution takes 2.8-2.9 ms
a frame, `high` 2.7-2.8 ms and `medium` 2.6 ms, with 107-111 draw calls and 1.46 million triangles; about
2.5 ms of it is the page's own work on the CPU. The 10 ms budget has room on that machine.

#### Still open

Putting MetaHumans in is deferred, and these questions stay open until someone takes them up:

- **Licence.** MetaHuman is not open source; Epic's EULA governs it. Since the June 2025 licence
  change, MetaHuman characters and animation may be used in other engines and creative software,
  including at runtime and commercially, without royalties. Two points still need an answer from
  whoever owns licensing:
  - **Seat licences.** Free below $1M a year of revenue. Organisations above it that use MetaHumans
    need Unreal Engine seat licences, about $1,850 per seat per year in 2025. Check whether this
    deployment counts.
  - **Redistribution.** Every other asset here is CC0, committed to the repository and shipped in
    the wheel. MetaHuman files in a public repository would be downloadable on their own, outside any
    product. Check whether the EULA allows that, or whether they must stay out of the repository
    (fetched at install, or kept private).
- **AI.** MetaHumans may be used in workflows that involve AI, but not to train or improve AI models.
  The agents' later persona work (agents as people) must stay on the right side of that.
- **Export, on the operator's machine.** The cloud sessions cannot reach Unreal. Someone with Unreal
  Engine 5.8.3 (the version decided above) would:
  1. create a diverse set of MetaHumans (skin tones, body types, ages, hair, and a wheelchair user),
     none based on a real person;
  2. export a higher level of detail for your character and a low one for the crowd, with hair as
     cards (strand grooms do not export);
  3. hand the files over outside git (a shared Drive folder, for example), so large raw exports never
     enter the repository's history.

  Here they would be decimated, given 512 to 1024 px WebP textures, and fitted to the page's budgets,
  as the Poly Haven files were.
- **Animation.** It has to come from somewhere licensed for this page:
  - procedural animation on the MetaHuman (Unreal 5) skeleton, which needs no files;
  - Epic's animations, under the same EULA questions;
  - or CC0 libraries.

  The crowd needs it to be cheap: baked vertex-animation textures or instanced skinning, within the
  `low` path's 64 draw calls and 400,000 triangles.
