# Specification: Rain, Wet Surfaces and Day/Night (input to Epic 47)

Imported 2026-10-08 from the browser world ("Just assume it is a rainy day. And that it can be day
or night, depending on what the local time is." — the operator, 2026-10-02) in
`agentchieflou/this-next-please`. Epic 47 (Time-of-Day & Weather, Track C) owns the Unreal
implementation; this is what the browser version did, what it measured, and what to keep.

## 1. Rain

- Always raining (the brief). 6,000 streaks in a box around the camera, lit by the lights they fall
  past; rings where drops land; splashes skip surfaces under a roof.
- Everything is wet: surfaces darker and glossier (a `WET` term on every material: roughness down,
  colour darkened); puddles gather in gutters and low spots and drops ring them; the ground is a
  mirror of the scene above it, sharp in a puddle and smeared on asphalt (planar reflection, the
  roughness picking a blurrier mip).
- Reduced motion: rain slows; time runs at 0.35×.
- In UE: Niagara rain lit by the scene, a wetness material parameter collection, puddle masks from a
  height/flow map, and screen-space or planar reflections on the field and concourse. The physics
  coupling (wet-ball fumble/catch modifiers, traction) is Epic 47's own story and has no browser
  precedent.

## 2. Day and night from the local clock

- Overcast day 07:30–18:30, dawn from 06:00, dusk until 20:00; the light blends between a
  photographed overcast city sky (`RawAssets/world/cc0/potsdamer_platz.hdr`) by day and a lamp-lit one
  (`hansaplatz.hdr`) by night, with the hour. The visible sky was still the scene's own, raining.
- By night: lamps (sodium orange or LED white, every 24 m, each with a cone of light in the rain),
  shop signs, neon, billboards and headlights light their surroundings in their own colours;
  windows light up (some warm, some cool, switching over the evening, some a television's flicker);
  the city's glow shows on the low cloud.
- Hundreds of lights, a pixel adds the nearest few (8/16/24/32 by quality tier). No shadow maps:
  under overcast and rain, ambient occlusion is the shadow. For a stadium at night Epic 46's rig will
  want real shadows from the floodlights; keep the "nearest N" budget idea for the thousands of small
  concourse/crowd lights.
- `?hour=0..23` pinned the clock for tests and screenshots. Keep that: a console variable or a
  `UPSTimeOfDaySubsystem::SetFixedHour` so a functional test or a screenshot never depends on the
  wall clock (and Track K's determinism audit stays honest).
- Cars stop at red lights and keep their distance; traffic lights cycle red/amber/green. Decorative
  here; Epic 52's approaches and parking could reuse `RawAssets/world/cars/cars.glb`.

## 3. Budget shape that applied

The whole weather (rain, wet ground mirror, night lights) fit in a 10 ms frame with the city on a
laptop GPU because the mirror ran at ½–⅓ resolution, lights per pixel were capped per tier, and the
rain was one instanced draw. `Specs/Browser_World_Lessons.md` §1–2 has the numbers and the adaptive
policy; Epic 47's "performance budget validation" story should set equivalent caps per scalability
group rather than one global quality.
