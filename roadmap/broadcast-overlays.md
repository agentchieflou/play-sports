# Track A — Broadcast Overlay & Telemetry (Epics 26–37)

Everything the broadcast reference frame shows layered *over* the game: pre-snap route ribbons
with endpoint rings, floating position badges (X/Y/A/B, RB), personnel-package panels
(RB 1 | TE 3 | WR 1 vs DL 3 | LB 4 | DB 4), zone-assignment stars, the selection reticle, and
the telemetry plumbing all of it feeds on. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** the event/telemetry foundation this track assumed was
promoted into core **Phase 1.5 as Epic C1** (`UPSTelemetryBus`) — Epic 26 below is re-scoped to
the overlay-grade *sampling* layer on top of it. `APSHUD` exists on `main` but is a bare widget
host (no game-state bindings); Epics 29/33 build its real content. Per `AGENTS.md`
"Architecture rules", every epic here ships automation tests and communicates via the bus —
`Cast<APSGameMode>` reach-through is rejected in review.

### Epic 26: Telemetry Sampling Layer (re-scoped 2026-07-19)

**Size/Mode:** M / code
**Goal:** Overlay-grade continuous sampling on top of Phase 1.5 C1's event bus — per-tick spatial snapshots the discrete event stream doesn't carry.
**Builds on:** `UPSTelemetryBus` (Phase 1.5 C1 — the bus itself, event stream, ring buffer, and subscription API live there)
**Depends on:** C1, Core 3, 6

- [x] Per-tick snapshot channel (position, velocity, acceleration, facing per pawn) with sampling-rate control *(`UPSTelemetrySamplingSubsystem`, a tickable world subsystem: every `APSPlayerPawn` plus the ball, offense first, at the tier's `TelemetrySampleRateHz`; `SetSampleRateHz`/`SetSamplingEnabled`, headless step `AdvanceTime`)*
- [x] Snapshot history windows aligned to C1's event ring buffer (trail/replay queries join both) *(bus events now carry a `Sequence`; scheduled frames sit in a `HistorySeconds` ring, and event keyframes and event times live exactly as long as their event stays in the bus's history. `GetFramesBetween`, `GetPawnTrail`, `SampleAt`, `GetFramesBetweenEvents`, `GetEventsBetween`)*
- [x] Snapshot-vs-event correlation API (e.g. "positions of all 22 at the moment of the catch event") *(`GetFrameAtLatestEvent(Catch)`: the keyframe captured as the event is published, before any subscriber reacts; other events get a frame blended at their time)*
- [x] Performance budget: sampling cost measured under Epic 114's counters, degradable rate *(each frame's cost is timed and shown by `stat PSTelemetrySampling`, ready for 114's harness, which doesn't exist yet; runs over the tier's `TelemetrySampleBudgetMs` halve the rate, runs well under double it back)*
- [x] Automation test: scripted movement produces expected snapshot stream, correlation query correctness *(`PlaySports.TelemetrySampling.*`, `PlaySports.TelemetryBus.EventSequenceAndLookup`)*

### Epic 27: Pre-Snap Route Visualization Overlay

**Size/Mode:** L / mixed
**Goal:** Offensive routes render as ground-projected ribbons with endpoint rings before the snap, exactly like the reference frame.
**Depends on:** 26, Core 16

- [ ] Route-ribbon renderer: spline decal/mesh projected on the field following route waypoints
- [ ] Endpoint ring marker at each route terminus; break-point articulation on cuts
- [ ] Show/hide policy tied to play phase (visible pre-snap, fade at snap) and to user settings
- [ ] Per-route color/emphasis coding (primary read vs. check-down)
- [ ] Editor pass: material/glow polish so ribbons read on grass at broadcast camera distance

### Epic 28: Player Position Badge System

**Size/Mode:** M / mixed
**Goal:** Floating letter badges (X, Y, A, B, RB, …) track above assigned players in world space.
**Depends on:** 26

- [x] Screen-space badge widget anchored to pawn head position with distance-based scaling *(`UPSOverlayBadgeWidget`, shown by `APSHUD`, draws what `UPSOverlayBadgeComponent` on the player controller lays out each frame for the player's camera: above each head, scaled by distance between `MinScale` and `MaxScale`. Look polish and glyph icons are an editor pass, `Specs/Position_Badges_Spec.md`)*
- [x] Badge assignment from play data (receiver designations) and role fallback (RB, QB) *(the designations are the human QB's receiver slots, left to right as the play's formation lines them up (`UPSPassingComponent`); each wears its slot button's glyph on the device in use, "X", "Y", "B", "RB", "A" on a gamepad, so a remapped key shows. Everyone else wears his role's label. A per-play letter field is Epic 35's annotation schema)*
- [x] Occlusion/overlap handling so badges never collide or block the ball *(pass buttons placed first, then nearer before farther; a badge in the way of another or of the ball moves up a step at a time, and one with no room isn't drawn)*
- [x] Color semantics (eligible receivers vs. backs vs. defense) as a data-driven style table *(`Data/overlay_badges.json`: per group (receivers, backs, QB, line, defense) colors, when they show before the snap and in play, and whether a Minimal tier keeps them)*

### Epic 29: Personnel Package HUD Panels

**Size/Mode:** M / code
**Goal:** Offense/defense panels summarize on-field personnel (e.g. RB 1 | TE 3 | WR 1 / DL 3 | LB 4 | DB 4) and update on substitutions.
**Depends on:** Core 5, Core 19

- [ ] Personnel counter derived live from the 22 on-field `EPlayerRole`s
- [ ] UMG panel pair (offense left, defense right) with team logo/color slots
- [ ] Package naming layer (11 personnel, nickel, dime) from the counter
- [ ] Update animation on substitution events

### Epic 30: Selected-Player Indicator & Control Handoff

**Size/Mode:** M / code
**Goal:** A reticle (the hexagon under the QB in the reference) marks the controlled player, with clean control switching.
**Depends on:** Core 3

- [x] Ground-projected reticle decal under the controlled pawn, team-colored *(`APSOverlayReticle`, driven by `UPSOverlayReticleComponent` on the controller: a flat ring attached at the controlled pawn's feet, in the human's team color (team select) or the side's. The look is `Data/overlay_reticle.json`; until an editor session authors the hexagon it is an engine disc (`Specs/Overlay_Reticle_Spec.md`))*
- [x] Control switching (nearest-to-ball cycling, direct pick pre-snap) moving possession of input *(`UPSControlHandoffComponent`: SwitchPlayer cycles through the nearest-to-the-ball order within `CycleWindowSeconds` (the side's carrier always first); pre-snap `PickPlayerLeft`/`PickPlayerRight` (Q/E, a right-stick flick) and `PickPlayer` by ID)*
- [x] Reticle state variants: pre-snap, in-play, ball-carrier emphasis *(per-state radius, brightness and pulse from data; the pulse runs only where the tier's new `OverlayDetail` is `Full`)*
- [x] AI takeover of the previously controlled pawn without behavior pops *(a handoff keeps the pawn's velocity both ways (possession used to stop it dead); the AI that takes a player back holds his heading until it decides, takes up the opening action if the human had him from before the snap, and skips route waypoints he is already past; a defender notes a throw made while a human had him)*

### Epic 31: Defensive Assignment Iconography

**Size/Mode:** M / mixed
**Goal:** Zone stars, man-coverage lines, and blitz arrows visualize the defensive call pre-snap.
**Depends on:** 26, 27, Core 16

- [ ] Zone-drop star markers at assignment landmarks (as in the reference frame's white stars)
- [ ] Man-coverage connector lines defender→receiver
- [ ] Blitz arrows from rushing defenders toward the LOS
- [ ] Toggle policy: user setting + "show defense" study mode (hidden in competitive contexts)

### Epic 32: Live Ball-Trajectory & Pass Indicators

**Size/Mode:** M / code
**Goal:** In-flight ball arc, landing marker, and receiver lead indicators render during passes and kicks.
**Depends on:** 26, Core 7

- [x] Predicted-arc spline from the ball physics state at release *(`UPSOverlayBallFlightSubsystem`: the ball's position, projectile velocity and gravity at the `Throw` event give the exact drag-free arc; `APSOverlayBallFlight` holds it in a spline and dots it, the dots behind the ball going on a `Full` tier. A pass now leaves from the hand height its velocity was aimed from, so it comes down where it was aimed. A ribbon look is an editor pass, `Specs/Ball_Flight_Overlay_Spec.md`)*
- [x] Landing-spot marker with catchable-radius ring *(where the arc comes down to the throw's catch height; the ring is the receiver's capsule plus the ball)*
- [x] Receiver lead indicator (where the target will be at arrival) *(his position plus his velocity until the ball comes down, green when that is inside the catch radius)*
- [x] Kick variant: field-goal arc with upright-relative good/wide readout *(a ball leaving the kicker in a kick phase is judged at the first posts ahead: good, wide left or right, short; the readout floats above the crossbar. Posts are data in the game mode's field frame. The simulation still resolves kicks with a roll and no ball in the air, so in a game the readout waits for the kicking game to launch the ball, as `ExecuteKick` does)*

### Epic 33: Score Bug & Broadcast Chyron Framework

**Size/Mode:** L / code
**Goal:** Persistent broadcast-grade score bug (teams, score, quarter, clocks, timeouts, down/distance) plus a lower-third chyron system.
**Depends on:** Core 5, Core 10, Core 12

- [x] Score bug widget consolidating game state (replaces/absorbs the Epic 5 debug HUD) *(`UPSOverlayScoreBugWidget`, built in code and shown by `APSHUD` by default, draws `UPSOverlayBroadcastSubsystem`'s model of `UPSPlaySimulation`'s new `GameState` bus event: teams, score, quarter, game and play clocks, down and distance. The sim announces only discrete changes; clocks run on in between. Look polish is an editor pass)*
- [x] Possession + timeout pips, red-zone and two-minute state styling *(red zone and two-minute thresholds are theme data)*
- [x] Lower-third chyron queue (player stat lines, drive summaries) with priority/timing rules *(`UPSOverlayChyronWidget`; per-kind priority and time on screen, cut-ins after a minimum time up, a queue limit and a gap. Fed by score alerts, drive summaries and play lines from the bus; `PushStatLine` is the door for Epic 92's box score, which doesn't exist yet)*
- [x] Data-driven layout theme so Track C branding can reskin it per team/broadcast package *(`Data/broadcast_overlay.json`: colors, sizes, anchor, thresholds, chyron rules; known teams show their own abbreviation and color)*

### Epic 34: On-Field AR Paint

**Size/Mode:** L / mixed
**Goal:** Virtual first-down line, LOS marker, and situational field tinting drawn on the field, correctly occluded by players.
**Depends on:** 26, Core 2, Core 10

- [ ] First-down line and LOS decals bound to drive state
- [ ] Occlusion so paint renders under players/ball (the classic broadcast trick)
- [ ] Red-zone / goal-to-go tint zones
- [ ] Distance-to-gain arrow cluster (the on-grass "40 →" style art from the reference)
- [ ] Editor pass: material calibration against field textures and night lighting

### Epic 35: Play-Art Authoring Pipeline

**Size/Mode:** L / code
**Goal:** One data format drives both AI route execution (Epic 16) and overlay rendering (27/31) — art is never hand-drawn twice.
**Depends on:** 27, 31, Core 16

- [ ] Overlay-annotation schema layered onto play definitions (colors, emphasis, badge letters)
- [ ] Compiler from play data → renderable art primitives (ribbons, rings, stars, arrows)
- [ ] Validation: every eligible player in a play has consistent art + AI assignment
- [ ] Round-trip test: authored play renders identically to what the AI runs

### Epic 36: Player Highlight & Emphasis Rendering

**Size/Mode:** M / mixed
**Goal:** Individual players can be visually emphasized — glow, outline, spotlight — for key-player callouts, mismatch alerts, and replay focus.
**Depends on:** 26

- [x] Outline/glow post-process pass togglable per pawn *(`UPSOverlayEmphasisSubsystem` marks a player's meshes for the custom-depth pass with his look's stencil value, and unmarks them, per pawn; `Config/DefaultEngine.ini` turns on the custom depth-stencil pass. The post-process material that draws outline and glow by stencil is the editor pass below, `Specs/Player_Emphasis_Spec.md`)*
- [x] Emphasis API consumed by commentary (Track H), replay (Track B), and coaching tips *(`Emphasize(Pawn, Highlight / Mismatch / Focus, Source, Seconds)`, `ClearEmphasis`, `ClearSource`: the highest priority request on a player wins, timed ones run out, and at most `MaxEmphasized` are drawn. Commentary and a replay player don't exist yet; this is the door they call)*
- [x] Spotlight/dim-others mode for isolation replays *(`Spotlight(Pawn, ...)`: a Focus that marks every other player with the dim stencil, unless he has a drawn look of his own; lifting it restores them)*
- [ ] Editor pass: tune against night lighting so emphasis reads without blowing out

### Epic 37: Overlay Theming & Branding System

**Size/Mode:** M / code
**Goal:** All Track A visuals pull colors, logos, fonts, and layout variants from one theme asset — team-flavored broadcasts without code changes.
**Depends on:** 28, 29, 33, 34

- [ ] Broadcast theme data asset (palette, typography, logo slots, badge styles)
- [ ] Team-color resolution rules (home/away contrast, colorblind-safe fallbacks feeding Epic 103)
- [ ] Sponsor/branding placeholder slots (score bug corner, chyron tail)
- [ ] Theme hot-swap for testing multiple broadcast looks
