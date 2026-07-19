# Track D — Player Visuals & Animation Depth (Epics 56–65)

Deepens core Epic 22 (base animation) into full player presentation: uniforms, likenesses, body
diversity, cloth, contact animation, and the officiating crew. Sizing/mode legend: see
`ROADMAP.md`.

**Reality note (2026-07-19 review):** tackle/pursuit/blocking logic from Epics 8–9 currently
lives inside `APSPlayerPawn`/`UPSPlaySimulation` and is **extracted into components by Phase 1.5
C3/C4** — Epic 61 (gang tackling) and the animation epics extend those extracted systems, not
the pawn. Rule 1 applies hard here: every new visual/contact mechanic is its own component with
tests.

### Epic 56: Uniform & Equipment System

**Size/Mode:** L / mixed
**Goal:** Players wear data-driven uniforms — jerseys, pants, helmets, facemasks, accessories — with team home/away/alternate sets.
**Depends on:** Core 22

- [ ] Uniform data schema (jersey/pant/sock/helmet sets, number fonts, sleeve styles)
- [ ] Number/nameplate rendering on jerseys from roster data
- [ ] Equipment variety: facemask styles, visors, gloves, arm sleeves, towels (the reference frame's RB towel)
- [ ] Home/away/alternate/color-rush selection logic with clash prevention
- [ ] Editor pass: material work (mesh jersey sheen under night lights)

### Epic 57: Player Likeness & Face Pipeline

**Size/Mode:** L / editor
**Goal:** Distinct, plausible player faces at roster scale — parameterized generation, not hand-sculpting 1,700 heads.
**Depends on:** Core 22

- [ ] Parameterized head system (feature morphs, skin tones, hair/facial-hair library)
- [ ] Roster binding: appearance parameters stored per player in the data pipeline (Track L generates)
- [ ] Helmet-on/off handling (sideline vs. on-field)
- [ ] Close-up camera validation pass (cinematics and replays hold up)

### Epic 58: Position Body-Type Morph System

**Size/Mode:** M / mixed
**Goal:** A lineman and a cornerback are visibly different athletes — body build derives from `WeightKg`/`HeightCm` and role.
**Depends on:** Core 22, 56

- [ ] Body morph axes (mass distribution, height scaling) driven by attribute data
- [ ] Per-role silhouette targets (OL bulk, WR lean, TE hybrid) as tuning data
- [ ] Uniform fit adaptation so gear follows morphs without clipping
- [ ] Animation retarget validation across the size range

### Epic 59: Cloth & Secondary Motion

**Size/Mode:** S / editor
**Goal:** Jerseys ripple, towels swing, chains bounce — the secondary motion that sells speed.
**Depends on:** 56

- [ ] Cloth sim setup on jersey/towel assets with performance budget
- [ ] Wind coupling from the weather system (47)
- [ ] LOD strategy: sim near camera, baked/skipped far

### Epic 60: Contextual Idle & Communication Behaviors

**Size/Mode:** M / mixed
**Goal:** Pre- and post-play, players behave like people — pointing out coverage (the reference frame's X receiver), QB gestures, huddle chatter, line calls.
**Depends on:** Core 22, Core 14

- [ ] Pre-snap behavior library: pointing, audible gestures, stance shifts, DB press posture
- [ ] Behavior selection driven by actual play context (pointing at the actual threat, not random)
- [ ] Post-play idles: first-down signal, frustration, helping opponents up
- [ ] Huddle formation/break choreography feeding Epic 43's cinematics

### Epic 61: Gang Tackling & Multi-Actor Contact

**Size/Mode:** XL / mixed
**Goal:** Two-plus defenders resolve simultaneous contact on one carrier — pile physics, drag-downs, goal-line surges.
**Builds on:** the contact/tackle component extracted in Phase 1.5 C3 (extends it — never `APSPlayerPawn` directly)
**Depends on:** C3, Core 8, Core 22

- [ ] Multi-participant contact resolution extending Epic 8's one-on-one model
- [ ] Assist mechanics: second tackler adds force, changes fall direction
- [ ] Pile formation and safe untangling (no ragdoll explosions)
- [ ] Goal-line/short-yardage surge scrums with push-forward resolution
- [ ] Carry-the-pile moments for elite `Strength` backs
- [ ] Editor pass: contact animation blending over the physics result

### Epic 62: Catch Animation Library

**Size/Mode:** L / editor
**Goal:** Catches read as distinct athletic events — sideline toe-taps, one-handers, contested high-points, diving grabs.
**Depends on:** Core 7, Core 22

- [ ] Catch-type taxonomy mapped to ball position/velocity/defender proximity inputs
- [ ] Sideline awareness: toe-tap/drag selection near boundaries (feeds rules Epic 11)
- [ ] Contested-catch animation pairs (receiver + defender simultaneous play on the ball)
- [ ] Transition guarantees: every catch animation exits cleanly into run-after-catch

### Epic 63: QB Mechanics Animation Depth

**Size/Mode:** M / editor
**Goal:** Quarterback motion is the most-watched animation in the game — throw variety, pressure adjustments, fakes.
**Depends on:** Core 22, Core 14

- [ ] Throw-style matrix: platform, on-run, sidearm, shovel, desperation heave
- [ ] Pressure-adjusted mechanics (feet compress, release quickens as the pocket collapses)
- [ ] Play-action and pump-fake animation with defender-visible believability (ties to Epic 72)
- [ ] Snap-to-drop footwork sets (3/5/7-step, shotgun, pistol)

### Epic 64: Line-Play Animation (Hand Fighting)

**Size/Mode:** M / editor
**Goal:** OL/DL engagements show real technique — punches, swipes, leverage battles — layered on the Epic 9 physics.
**Depends on:** Core 9, Core 22

- [ ] Engagement animation set synchronized between paired combatants
- [ ] Move/counter visuals matching Epic 70's rush-move outcomes
- [ ] Pancake and shed finishing animations
- [ ] Interior pile believability at 60fps with 10 simultaneous engagements

### Epic 65: Officiating Crew

**Size/Mode:** M / mixed
**Goal:** Referees exist on-field: spotting the ball, signaling, throwing flags, announcing penalties.
**Depends on:** Core 11, Core 2

- [ ] Officiating crew placement and movement (positions per play type, stay out of the way)
- [ ] Signal animation library mapped to Epic 11's penalty/scoring events
- [ ] Flag-throw moment with broadcast presentation hook (Track A chyron, Track H announcement)
- [ ] Ball-spotting choreography between plays (feeds Epic 10's reset flow)
