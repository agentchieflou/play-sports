# Track C — Stadium, Lighting & Atmosphere (Epics 46–55)

The world around the play: night floodlights (the reference frame's look), weather, crowds,
field surface, and venue identity. Heavier on editor work than most tracks — agents deliver
specs, data, and runtime code; visual authoring needs editor sessions. Sizing/mode legend: see
`ROADMAP.md`.

### Epic 46: Night Lighting Rig

**Size/Mode:** L / editor
**Goal:** Stadium floodlight lighting that reads like a prime-time broadcast — multi-source shadows, grass sheen, lit-bowl falloff.
**Depends on:** Core 2

- [ ] Floodlight bank placement spec (positions, intensities, color temperature) as data
- [ ] Multi-shadow tuning on players (the four-shadow prime-time look)
- [ ] Field material response pass (dewy grass sheen under lights)
- [ ] Exposure/bloom calibration against overlay legibility (Track A must stay readable)

### Epic 47: Time-of-Day & Weather System

**Size/Mode:** XL / mixed
**Goal:** Games run at any hour in clear/rain/snow/fog conditions, with weather affecting gameplay physics, not just visuals.
**Depends on:** 46, Core 6, Core 7

- [ ] Time-of-day framework (sun position, dusk transitions, lighting preset blending)
- [ ] Precipitation rendering (rain/snow particles, wet-surface materials) — editor-heavy
- [ ] Physics coupling: wet-ball fumble/catch modifiers, footing traction loss, wind on kicks/passes
- [ ] Forecast data model so franchise (Track G) schedules weather per game/venue
- [ ] Fog/visibility effects with camera-distance tuning

### Epic 48: Crowd Rendering System

**Size/Mode:** L / mixed
**Goal:** Tens of thousands of visible spectators at acceptable cost — instanced, LOD'd, animated.
**Depends on:** Core 2

- [ ] Instanced crowd placement across seating geometry with density control
- [ ] LOD ladder: animated near-field, impostor mid, texture far
- [ ] Team-color outfitting distribution (home majority, away pockets)
- [ ] Performance budget validation at full occupancy

### Epic 49: Crowd Behavior & Emotion Model

**Size/Mode:** M / code
**Goal:** The crowd is a simulation participant — it reacts, and its noise affects play.
**Depends on:** 48, 26

- [ ] Excitement state machine driven by game events (score, big play, controversy, blowout exodus)
- [ ] Reaction animations/waves keyed to state (rise on long plays, towel waves, stunned silence)
- [ ] Gameplay coupling: crowd-noise pressure on visiting-team audibles/false starts (feeds Epic 66)
- [ ] Home/away and rivalry intensity parameters

### Epic 50: Sideline Ecosystem

**Size/Mode:** M / editor
**Goal:** Sidelines are populated — coaches, chain gang, photographers, medical staff, mascots — as in the reference frame's periphery.
**Depends on:** 48, Core 2

- [ ] Sideline population spec: roles, counts, placement zones, team dress
- [ ] Idle/reaction behavior sets per role (coach gesticulating, photographers tracking the ball)
- [ ] Chain gang functionality tied to drive state (they move with the sticks)
- [ ] Bench-area substitution flow (players enter/exit with Track G rotations)

### Epic 51: Field Surface Simulation

**Size/Mode:** M / mixed
**Goal:** The playing surface degrades and varies — turf vs. grass, wear between the hashes, divots, weather interaction.
**Depends on:** 47, Core 2

- [ ] Surface-type data (natural/artificial per venue) with traction/bounce parameters
- [ ] Progressive wear rendering (mid-field chew-up over a game/season)
- [ ] Paint degradation (logos and numbers scuff as the game wears on)
- [ ] Divot/debris FX on cuts and tackles

### Epic 52: Stadium Architecture Kit

**Size/Mode:** XL / editor
**Goal:** A modular kit builds distinct venues — bowl shapes, deck configurations, open/closed corners, roofs — instead of one stadium.
**Depends on:** Core 2, 46

- [ ] Modular seating/deck/concourse component spec
- [ ] Three distinct assembled venues proving the kit's range (open-air, dome, historic bowl)
- [ ] Venue data asset: capacity, orientation, surface, lighting rig, acoustics profile (Track H)
- [ ] Skybox/surroundings treatment per venue (city skyline, parking sea, waterfront)
- [ ] Dome handling: closed lighting/acoustics, no weather

### Epic 53: End-Zone & Field Branding Pipeline

**Size/Mode:** M / code
**Goal:** End-zone lettering, midfield logos, and yard numerals render from team/venue data — the reference frame's painted-field identity, data-driven.
**Depends on:** Core 2, 37

- [ ] End-zone text/wordmark renderer from team identity data (font, arc, outline styles)
- [ ] Midfield logo projection with wear interaction (51)
- [ ] Yard numeral + directional arrow stencil set matching broadcast-perspective legibility
- [ ] League/event branding variants (playoff patches, championship overlays)

### Epic 54: Videoboard & In-Stadium Media

**Size/Mode:** M / code
**Goal:** Jumbotrons and ribbon boards show live, plausible content — score, replays, hype prompts.
**Depends on:** 41, 52

- [ ] Render-target pipeline putting live game feed/replays on stadium screens
- [ ] Content scheduler (score panels, stat graphics, "make noise" prompts on defense downs)
- [ ] Ribbon-board ambient loops with team branding
- [ ] Performance guard: board content at reduced rate/resolution

### Epic 55: Celebration & Event FX

**Size/Mode:** S / editor
**Goal:** Pyro, fireworks, confetti, and smoke moments fire on the right triggers.
**Depends on:** 46, 49

- [ ] FX catalog spec (intro pyro, TD fireworks, championship confetti) with trigger matrix
- [ ] Niagara implementations tuned under night lighting
- [ ] Crowd/audio sync hooks (49, Track H)
