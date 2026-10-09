# `world/bots.js`

The reasoning for this script, kept out of the source (decisions 18 and 19 on #429). The source keeps
its code and the JSDoc types `tsc` reads; the server strips every comment from what it serves
(`agentdata/fleet/strip.py`).

A `###` heading is the declaration or statement the notes sit in or above, in source order.

### The file

The agents as robots (#626, *"the agents become objects"*). The operator, 2026-10-02: *"We need to
improve the asset quality of our 3d world."* A capsule on a plinth read as a placeholder; a small
robot with a face reads as someone you walk up to and talk to.

A classic script loaded after `world/kit.js` (its `piece` and `merge`) and before
`world/world.js`. It defines one global, `WorldBots`. Every agent shares four instanced meshes, so
the fleet costs four draw calls whatever its size, as before.

### `var SHELL`

A white shell, grey trim, a dark visor: neutral, so a robot's tint and its state's colour are what
tell one from another.

### `var fill`

The shell's own light, which `vWeather` turns down at night, as `WorldHero.fill` does for your
character: a robot stays readable in the dark without looking lit from inside.

### `var beat`

The beacon's time and opacity, shared with its material.

### `function build`

The robot, merged into two geometries:
- the shell: a lathed body, a trim belt, a neck, a round head with a dark visor set into its face,
  ear discs, arms, hands, an antenna and a hover base;
- the glow: two eyes and a smile on the visor, the antenna tip, a chest light and a ring of light
  under the base.

The shell is lit (`MeshStandardMaterial`, with the fill and the kit's lights, so a robot under a
lamp or by a neon sign takes its colour); the glow is unlit, so it shines in the
dark, and it takes the agent's state colour. The ring floats at the waist in the state's colour.
The beacon, for an agent that needs a person, is a tall soft beam added to what is behind it,
banded and fading up and down, ignoring the fog so it is seen across the plaza, and fading out near
the camera so it never fills the view when you stand at it.

Four `InstancedMesh`es up to `cap`, each drawn whole (`frustumCulled` off: their bounds are the
first robot's, not the fleet's).

### `function tint`

Each agent's shell a pale colour from its name: the same agent is the same colour on every machine,
and two side by side differ.

### `function place`

Each frame: a robot bobs on its hover base and turns a little, faster when it is working. Its glow
takes its state's colour, pulsing when it needs you; its ring turns while it works. Under reduced
motion all of it holds still. A beacon is placed only for an agent that needs you, so the count of
beacons is the count of agents waiting.

With `people` (the agents drawn as people, `WorldPeople.placeAgents`) the robot's shell and glow are not
drawn; its ring lies flat at the person's feet, still in its state's colour, and the beacon stays.
