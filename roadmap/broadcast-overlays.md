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

- [ ] Per-tick snapshot channel (position, velocity, acceleration, facing per pawn) with sampling-rate control
- [ ] Snapshot history windows aligned to C1's event ring buffer (trail/replay queries join both)
- [ ] Snapshot-vs-event correlation API (e.g. "positions of all 22 at the moment of the catch event")
- [ ] Performance budget: sampling cost measured under Epic 114's counters, degradable rate
- [ ] Automation test: scripted movement produces expected snapshot stream, correlation query correctness

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

- [ ] Screen-space badge widget anchored to pawn head position with distance-based scaling
- [ ] Badge assignment from play data (receiver designations) and role fallback (RB, QB)
- [ ] Occlusion/overlap handling so badges never collide or block the ball
- [ ] Color semantics (eligible receivers vs. backs vs. defense) as a data-driven style table

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

- [ ] Ground-projected reticle decal under the controlled pawn, team-colored
- [ ] Control switching (nearest-to-ball cycling, direct pick pre-snap) moving possession of input
- [ ] Reticle state variants: pre-snap, in-play, ball-carrier emphasis
- [ ] AI takeover of the previously controlled pawn without behavior pops

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

- [ ] Predicted-arc spline from the ball physics state at release
- [ ] Landing-spot marker with catchable-radius ring
- [ ] Receiver lead indicator (where the target will be at arrival)
- [ ] Kick variant: field-goal arc with upright-relative good/wide readout

### Epic 33: Score Bug & Broadcast Chyron Framework

**Size/Mode:** L / code
**Goal:** Persistent broadcast-grade score bug (teams, score, quarter, clocks, timeouts, down/distance) plus a lower-third chyron system.
**Depends on:** Core 5, Core 10, Core 12

- [ ] Score bug widget consolidating game state (replaces/absorbs the Epic 5 debug HUD)
- [ ] Possession + timeout pips, red-zone and two-minute state styling
- [ ] Lower-third chyron queue (player stat lines, drive summaries) with priority/timing rules
- [ ] Data-driven layout theme so Track C branding can reskin it per team/broadcast package

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

- [ ] Outline/glow post-process pass togglable per pawn
- [ ] Emphasis API consumed by commentary (Track H), replay (Track B), and coaching tips
- [ ] Spotlight/dim-others mode for isolation replays
- [ ] Editor pass: tune against night lighting so emphasis reads without blowing out

### Epic 37: Overlay Theming & Branding System

**Size/Mode:** M / code
**Goal:** All Track A visuals pull colors, logos, fonts, and layout variants from one theme asset — team-flavored broadcasts without code changes.
**Depends on:** 28, 29, 33, 34

- [ ] Broadcast theme data asset (palette, typography, logo slots, badge styles)
- [ ] Team-color resolution rules (home/away contrast, colorblind-safe fallbacks feeding Epic 103)
- [ ] Sponsor/branding placeholder slots (score bug corner, chyron tail)
- [ ] Theme hot-swap for testing multiple broadcast looks
