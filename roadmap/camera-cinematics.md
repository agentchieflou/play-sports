# Track B — Camera & Cinematics (Epics 38–45)

Deepens core Epic 4's basic broadcast camera into a full presentation layer: an auto-directing
camera brain, physical rig simulations, replay, auto-highlights, and analysis tooling.
Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** `APSBroadcastCamera` exists on `main` (follow/bounds/
framing/free-cam from Epic 4) but its `TargetActor` is never assigned — **Phase 1.5 C3 wires it
via possession events**; Epic 38 starts from that wired camera, not from scratch. Epic 41's
ring buffer is C1's event history (don't build a second one). Architecture rules apply: new
camera behaviors are components/classes, each epic ships tests.

### Epic 38: Camera Director AI

**Size/Mode:** L / code
**Goal:** An automated director cuts between camera rigs based on play context — no manual camera work needed to watch a full game.
**Depends on:** Core 4, 26

- [ ] Shot vocabulary: LOS wide, all-22 high, tight follow, end-zone, sideline reaction
- [ ] Cut rules driven by phase/events (pre-snap wide → snap follow → post-play tight)
- [ ] Interest scoring (ball, big hits, breakaways) to pick the live subject
- [ ] Smoothing/constraint layer so cuts never disorient (180° rule, minimum shot length)

### Epic 39: Skycam / Cable-Cam Rig Simulation

**Size/Mode:** M / code
**Goal:** A physically plausible suspended camera flies behind the offense — the modern broadcast signature angle.
**Depends on:** 38

- [ ] Catenary-constrained rig: camera moves within a simulated cable envelope above the field
- [ ] Follow behaviors (behind-QB pre-snap, chase on breakaways) with mass/lag for realism
- [ ] Handoff integration so the director (38) can cut to/from it

### Epic 40: All-22 Coaches Film Camera

**Size/Mode:** S / code
**Goal:** A locked high wide angle showing all 22 players, recordable for analysis.
**Depends on:** Core 4

- [ ] Fixed elevated sideline and end-zone all-22 rigs
- [ ] Toggle path from normal presentation into film view
- [ ] Frame export hook for the analysis/telestrator Epic (44)

### Epic 41: Replay System

**Size/Mode:** XL / code
**Goal:** Any recent play can be re-rendered from any camera with scrubbing and slow motion.
**Depends on:** C1, 26, 38, Core 17 (determinism hooks)

- [ ] Replay recording joins C1's event ring buffer with Epic 26's snapshot history (no third buffer)
- [ ] Deterministic re-simulation or state-playback of the buffered play
- [ ] Scrub/pause/slow-mo/frame-step transport controls
- [ ] Free camera + all rig cameras available inside replay
- [ ] Auto-replay trigger after scores/turnovers with director-chosen angle
- [ ] Persistence: save a play's replay data to disk for later viewing

### Epic 42: Auto-Highlight Generation

**Size/Mode:** L / code
**Goal:** The game detects its own big moments and assembles a highlight reel per game.
**Depends on:** 41

- [ ] Play-importance scoring (yardage, score change, turnover, broken tackles, win probability swing)
- [ ] Clip assembly: top-N plays with director-selected angles and slow-mo beats
- [ ] End-of-game highlight package playback
- [ ] Franchise hook: highlights persist per season (Track G consumes)

### Epic 43: Cinematic Play Framing Sequences

**Size/Mode:** L / editor
**Goal:** Pre/post-play presentation moments — huddle break, lineup walk, celebrations, dejection — staged as short cinematics.
**Depends on:** 38, Core 22

- [ ] Sequence catalog and trigger matrix (score, turnover, game-winner, injury)
- [ ] Huddle-break and pre-snap approach staging
- [ ] Celebration staging with camera coverage (mixed: staging code, animation content in editor)
- [ ] Skip/auto-skip rules so cinematics never slow repeat viewers

### Epic 44: Telestrator & Analysis Mode

**Size/Mode:** M / code
**Goal:** Draw-on-screen analysis (circles, arrows, freehand) over paused replay or film view.
**Depends on:** 40, 41

- [ ] Draw layer (freehand, arrow, circle, highlight-player via Epic 36) over paused frames
- [ ] Save/share annotated stills to disk
- [ ] LLM hook (bridge-gated per Epic 25): auto-annotate a play with coaching notes

### Epic 45: Photo Mode

**Size/Mode:** S / code
**Goal:** Free-camera still capture with framing aids and filters.
**Depends on:** 41

- [ ] Pause-anywhere free cam with FOV/DOF/roll controls
- [ ] Filter/preset stack and UI-hide toggle
- [ ] High-res screenshot export
