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

- [x] Shot vocabulary: LOS wide, all-22 high, tight follow, end-zone, sideline reaction *(`UPSCameraDirectorComponent` on `APSBroadcastCamera`, shots in `Data/camera_director.json`. All-22 high and end zone are Epic 40's rigs, framed by `UPSCameraFraming`. The other three stand their distance toward the camera side of their target: the ball for LOS wide, and the live subject for tight follow (led along his run) and sideline reaction (low and close))*
- [x] Cut rules driven by phase/events (pre-snap wide → snap follow → post-play tight) *(each `CutRules` row maps a bus trigger to a shot: `PhaseChange` to PreSnap → LOS wide, `Snap` → tight follow, `Throw` → all-22 high, `Catch` → follow, `Fumble` → end zone, `Tackle`/`Score`/the whistle → sideline reaction. The director is the broadcast camera's normal presentation; the film view (Epic 40) and the free cam override it, and with `bDirectorEnabled` off the plain follow returns)*
- [x] Interest scoring (ball, big hits, breakaways) to pick the live subject *(read from Epic 26's latest snapshot plus the bus's tackles and hits. A player's interest is the sum of weights for holding the ball, closeness to it, a breakaway (fast, nobody within the clearance) and a big hit fading over `BigHitSeconds`. A new subject must beat the current one by `SwitchMargin`)*
- [x] Smoothing/constraint layer so cuts never disorient (180° rule, minimum shot length) *(no shot is cut away from before `MinShotSeconds`: an earlier ask waits and the latest wins, and an ask for the live shot does nothing. Every shot stays on `CameraSide` of the line of action through the ball: one across it is mirrored back and re-aimed, while end-zone angles on the line are allowed. Cuts are instant; within a shot the camera eases at `FollowInterpSpeed`. Tests: `PlaySports.Camera.Director*`)*

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

- [x] Fixed elevated sideline and end-zone all-22 rigs *(rows in `Data/camera_all22.json`: height, standoff, rail and zoom range per rig. `UPSCameraFraming` aims at the players' padded bounding box and zooms to the narrowest angle that holds its eight corners, backing the rig away along its line of sight when even the widest zoom can't. The sideline rig sits on the broadcast camera's side (180° rule); the end-zone rig stands behind the offense, whose direction is read from the formation at each cut and each snap)*
- [x] Toggle path from normal presentation into film view *(`UPSCameraAll22Component` on `APSBroadcastCamera`, so film is a mode of the one game camera, not a second camera. A new catalog action, `FilmView` (R3 / F, in `OnField`), steps broadcast → sideline → end zone → broadcast. It is not on Y, which is the tempo, a pass and a move button on the field: a toggle there would cut the camera on a Y pressed around the snap, and the input buffer (104.4) could no longer carry that press into the throw. The camera hears it from whichever `APSPlayerController` views through it (`BecomeViewTarget`). Leaving film view restores the broadcast view. Film view has no camera effects and turns motion blur off, so the per-tier cut-line in `Specs/Platform_Audit.md` doesn't apply)*
- [x] Frame export hook for the analysis/telestrator Epic (44) *(`CaptureFrame` records the live shot and each player's normalized place in it (`FPSFilmFrame`) and fires `OnFrameCaptured`; `ExportFrame` writes the frame as JSON to `Saved/Film` and requests a still when a game viewport exists. Frames carry bus time, so they line up with the event history and replays. Tests: `PlaySports.Camera.All22*`)*

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
