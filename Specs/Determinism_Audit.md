# Determinism Audit & Replay Format Rationale (Epics 115 and 108)

Audit date: 2026-07-19, against `main` @ a0399cf. Scope: every system that influences a play's
outcome (`UPSPlaySimulation`, `APSPlayerPawn`, `APSBall`, `APSGameMode`) plus the in-flight
Epic C1 telemetry bus, examined for the three classic divergence sources: RNG discipline,
tick/order dependence, and float stability.

**Epic 108 update (2026-10-10).** The online feasibility study at the end of this document
("Epic 108: determinism across machines") re-audits these findings, fixes the ones the code
controls, adds automated checks, and lists what can't be made deterministic across machines.
In short: the outcome rolls are now seeded (A1 resolved), a live play run twice headless at a
fixed step is the same play bit for bit, and cross-platform bit-exact simulation (PC against
iPhone) stays out of reach. The sections between here and there are the July record; their
status is in the study's first table.

## Verdict

The physical simulation is **not deterministically re-simulable today**, and cannot be made so
by seeding alone. The blockers are structural (variable timestep, unordered actor iteration,
overlap-callback ordering), so Epic 115 adopts a **two-mode replay strategy**:

- **Mode 1 — event playback (achievable now):** record initial state + the ordered gameplay
  event stream (C1's bus history) and re-enact recorded outcomes. This mode is immune to every
  finding below and is what the v1 format (`PSReplayFormat.h`) targets.
- **Mode 2 — deterministic re-simulation (after remediation):** replay = initial state + seeds
  + input stream, outcomes recomputed. Requires R1–R5 below. The v1 header already reserves
  `RandomSeed` and `FixedDeltaSeconds` so Mode 2 will not need a format break.

Cross-platform / cross-binary bit-exact replay is a **non-goal** for v1 (float codegen variance
— FMA, SIMD paths — makes it a research project, not a story). Same-binary, same-machine is the
supported envelope.

## Findings

### A. RNG discipline — BLOCKER

**A1. All gameplay randomness uses the process-global, unseeded RNG.** 18 call sites of
`FMath::FRand` / `FRandRange` / `RandRange` / `VRand`; zero uses of `FRandomStream`. The global
stream is shared with the engine and any other code, so identical inputs do not reproduce
identical rolls even in principle.

| Roll | Site |
|---|---|
| Offsides 5% at snap | `PSPlaySimulation.cpp:59` |
| In-play holding | `PSPlaySimulation.cpp:126` |
| Kickoff touchback / return yards | `PSPlaySimulation.cpp:164,171` |
| Punt net yards | `PSPlaySimulation.cpp:182` |
| FG success | `PSPlaySimulation.cpp:197` |
| Completion / yards noise / TD chance | `PSPlaySimulation.cpp:271,281,290` |
| PAT 94% | `PSPlaySimulation.cpp:395` |
| Block shed | `PSPlayerPawn.cpp:125` |
| Throw accuracy scatter | `PSPlayerPawn.cpp:442` |
| Fumble velocity jitter | `PSPlayerPawn.cpp:617` |
| Tackle roll / fumble chance | `PSPlayerPawn.cpp:663,672` |
| Fumble recovery / catch / interception | `PSBall.cpp:162,188,211` |

**A2. No roll is recorded anywhere.** C1's history captures outcomes, not the rolls that
produced them, so a recording cannot be validated against a re-run.

**A3. Roll *consumption order* is itself nondeterministic** (see B3/B4): even a seeded single
stream would diverge because which actor rolls first varies run to run. Remediation needs
per-domain streams (or recorded rolls), not just a seed.

### B. Tick order & scheduling — BLOCKER for re-simulation

**B1. No fixed timestep.** `APSGameMode::Tick` advances the phase machine with render-frame
`DeltaSeconds`; `UFloatingPawnMovement` (22 pawns) and `UProjectileMovementComponent` (ball)
integrate per-frame variable dt. Different frame timing ⇒ different trajectories and different
phase-boundary crossings.

**B2. Frame-rate-coupled probability formulas.** `PSPlayerPawn.cpp:125`
(`FRand() <= ShedChance * DeltaSeconds * 10.f`) and `PSPlaySimulation.cpp:126`
(`FRand() < 0.03f * DeltaSeconds`) roll once per frame with p ∝ dt. The per-second event rate
varies with frame rate (correct conversion is `1 - pow(1 - p, dt)`), and one roll per frame
means the RNG stream position depends on total frame count.

**B3. Actor tick order is unspecified.** Engaged linemen mutate *both* pawns' velocities in
their own `Tick` (`PSPlayerPawn.cpp:95-113`); whichever pawn ticks first that frame changes the
push contest. No tick prerequisites or tick groups are configured anywhere.

**B4. Physics overlap callbacks resolve in engine-internal order.** `OnPawnOverlap`
(`PSPlayerPawn.cpp:626`) and `OnBallOverlap` (`PSBall.cpp:133`) both roll RNG and can force play
phase; when two pawns reach the ball the same frame, catch-vs-interception priority is whatever
order the broadphase reports.

**B5. `GetAllActorsOfClass` iteration order dependence** in `APSGameMode`:
`FindPlayerPawnByRole` returns the *first* match (`PSGameMode.cpp:319`), `PairLinemen` greedily
pairs in iteration order (`PSGameMode.cpp:255`), and `ResetPawnPositions` assigns formation
Y-slots in iteration order (`PSGameMode.cpp:398`) — the post-play formation itself is
order-dependent. Actor-array order is not guaranteed stable across level loads.

**B6. Dual outcome authority (cross-ref Epic C2).** `APSGameMode::Tick` adds `HomeScore += 6`
on any touchdown regardless of possession (`PSGameMode.cpp:220-228`) while the sim keeps its own
score. Until C2 lands, "record the score" is ambiguous — the recorder must read only
`UPSPlaySimulation` state.

### C. Float stability — moderate

**C1. Accumulated float clocks.** `GameTimeSeconds += DeltaSeconds`, decremented game/play
clocks, and `PhaseTimer >= 0.5f`-style threshold tests: different dt sequences accumulate
different error and can flip a threshold frame. The replay format therefore keys events on an
integer `TickIndex`; float timestamps are diagnostic only.

**C2. Knife-edge rounding on derived yards.** `RoundToInt((X - StartingLocation.X) / 100.f)`
(`PSPlayerPawn.cpp:702`) turns sub-centimeter position divergence into a ±1-yard state
difference that then feeds down/distance logic — a divergence amplifier.

**C3.** No wall-clock (`FPlatformTime`/`FDateTime`) dependence in gameplay logic — good.
`FDateTime` appears only in save metadata.

### D. C1 telemetry bus (in-flight, `epic-c1/telemetry-bus`) — consumer notes

**D1.** `FPSTelemetryEvent` carries a float `Timestamp` but no tick index, and the ring buffer
caps at 100 events (`MaxHistorySize`) — a long drive can overflow. The replay recorder must
drain the buffer during recording (or the cap must be configurable); add an integer tick counter
when the C1 migration stories land (R5).

**D2.** The history is an *outcome* stream, sufficient for Mode 1 playback but not Mode 2
validation. Seeds + inputs remain format-header concerns, not bus concerns.

## Remediations (for Mode 2, mapped to roadmap)

- **R1 — Seeded per-domain streams.** `FRandomStream` instances (sim outcomes, physical-contest
  rolls, cosmetic) seeded from one recorded master seed, routed through a single service.
  Satisfies Core 17's "seedable decisions" checkbox; prerequisite for Mode 2.
- **R2 — Frame-rate-independent probabilities.** Replace `p * dt` with `1 - pow(1 - rate, dt)`
  or fixed-interval decision rolls; formulas move to DataTables per architecture rule 4.
- **R3 — Stable ordering.** Sort pawn collections by `PlayerId` before any order-dependent
  resolution (pairing, role lookup, formation slots). Epic C3's cached-roster work is the vehicle.
- **R4 — Fixed decision timestep** decoupled from render tick. Largest change; only needed for
  full Mode 2.
- **R5 — Integer `TickIndex`** published with every bus event.

## Serialization format v1 (`Source/PlaySports/Public/PSReplayFormat.h`)

One JSON document per recorded play: `FPSReplayRecording = Header + InitialState + Events[]`.

- **Header** (`FPSReplayHeader`): `FormatVersion`, `GameBuildVersion`, `RecordedAtUtc`,
  `RandomSeed` (0 = playback-only recording), `FixedDeltaSeconds` (0 = variable-dt recording).
- **InitialState** (`FPSReplayInitialState`): embeds the live `FPlayState` plus offense/defense
  `FPlayerAttributes` rosters — reuses gameplay structs rather than duplicating their shape
  (architecture rules 3/6).
- **Events** (`FPSReplayEventRecord[]`): `TickIndex` (int), `TimestampSeconds` (diagnostic),
  `EventType` (by **name**, matching C1's `EPSTelemetryEventType` entries once merged — never by
  integer value), `PayloadJson` (the typed payload object, same convention as the bus history).
  C1 types are deliberately not referenced so the format compiles on `main` before C1 merges.

### Versioning & migration policy

1. `FormatVersion` is a single integer, bumped only on breaking shape changes. Purely additive
   optional fields do **not** bump it (JSON readers tolerate unknown/missing fields).
2. A default-constructed header carries version 0 = "unversioned/invalid"; only
   `UPSReplayFormat::MakeRecording` (and future recorders) stamp `CurrentFormatVersion`. Readers
   reject version ≤ 0 — an empty or truncated document can never masquerade as a recording.
3. Readers migrate old recordings **step-wise** (v_n → v_n+1 chains, mirroring
   `UPSSaveGame::MigrateFrom`) so any historical version loads forever. Writers only ever write
   the current version.
4. Documents with a version **newer** than the reader are rejected, never guessed at.
5. Enums are serialized by name; unknown `EventType` strings are skipped by players, not fatal.

Version history: **1** (Epic 115) is the shape above. **2** (Epic 108) keeps the shape and
changes what `RandomSeed` means: it seeds the simulation's own stream, not the engine's global
one. A version 1 seed can't be reproduced by a version 2 build (finding E4), so the v1 → v2
step sets it to 0. The recording keeps its events for playback.

Optional fields added under rule 1, with no bump: `Frames` (Epic 41, state playback), and `Teams`
and `Participants` (the live-play demos): the teams on the field and every player the frames name,
with his name, team, side, role and jersey number, so a viewer can label him. A recording without
them loads with them empty.

## Record/playback round trip (story 4)

`UPSReplayRecorder` is the recorder D1 asks for: it copies every event off a `UPSTelemetryBus`
as it is published (so a run longer than the bus's 100-event history is recorded whole), stamps
each with the driving step's integer tick (R5 at the recorder, not the bus), and writes the event
type by name. It starts from a `MakeRecording` header and initial state.

`UPSQuickSimRunner` uses it for the one simulation that is Mode 2 today, the quick sim:

- `RecordGame` plays the game `SimulateGame` would for the seed, and records the `GameState`
  events the simulation publishes. The header carries the seed and the step. Until Epic 108 the
  seed was the global stream's (A1 was open); since format version 2 it seeds the simulation's
  own stream (`UPSPlaySimulation::SeedRolls`, finding E4 below).
- `ReplayGame` re-simulates a loaded recording from what it holds alone (rosters, initial state,
  seed, step) and records the rerun the same way. It refuses, with the reason, a recording with
  no seed or no fixed step (Mode 1 only), one that starts mid-game (the simulation has no way to
  be put into an arbitrary `FPlayState`), and a world with a game mode (a live game: the run
  would publish on its bus and reset its pawns).

`PlaySports.Replay.RoundTrip.*` records whole games, saves them to JSON, loads them and plays
them back: `UPSDeterminism::FindFirstDivergence` finds no difference, event for event, and the
final score matches the unrecorded quick sim's for the same seed. A recording edited after the
fact, or played back with a changed roster, is reported at its first differing event, by index,
tick and field.

The live, physical game is still not re-simulable (findings B1 to B5 stand). Its replay is state
playback of Epic 26's snapshots (Epic 41), not re-simulation.

## Epic 108: determinism across machines (feasibility study, 2026-10-10)

Audited against `claude/integration-10` @ d16e47c (integration batch 10, PR #173). The question
for Epic 108 is narrower than Epic 115's: can two machines, the owner's Windows PC and their
iPhone 17 Pro, run the same match and stay in step from inputs alone (deterministic lockstep),
or must one machine be the authority (replication)? The answer feeds
`Specs/ADR_Online_Architecture.md`.

Method: read every source of chance and every order dependence in the game code; fix what the
code controls; prove the fixes with headless tests that run the same seeded game twice and
compare it event for event and frame for frame; and list, with evidence, what can't be made
the same on two different machines. No device or second machine was available to this study.
Everything below about the iPhone is reasoned from the compilers, the C libraries and the code,
and is marked as such.

### Status of the July findings

| Finding | Status (2026-10-10) |
|---|---|
| A1 global RNG | **Resolved for the outcome rolls.** The live contests (throw scatter, tackle, strip, fumble bounce, hit damage, block shed, catch, interception, fumble recovery) roll on `UPSNetRandomStreams`, seeded per match, play, kind of roll and player. The play simulation and its special-teams model roll on their own stream once seeded (`UPSPlaySimulation::SeedRolls`): the game mode seeds it from the match seed, the quick sim's recorder from the recording's seed. Only an *unseeded* simulation (franchise quick sims, which their callers seed with `FMath::RandInit`) and code outside any game world still use the global stream. |
| A2 rolls not recorded | Stands, by design: re-simulation records the seed, not the rolls. |
| A3 consumption order | **Resolved for the contests.** A stream per roll kind and per player means one player's draws never move another's, whatever order actors tick or overlap in. Streams that several players share (coverage, loose ball, deception) are seeded per snap but still drawn in iteration order (E12). |
| B1 variable timestep | Stands for the live game: `APSGameMode::Tick` advances the simulation by the frame's `DeltaSeconds`. The checks below step at a fixed 1/30 s. |
| B2 frame-coupled chances | Stands: the block shed and offensive holding roll once a frame with `p * dt`. Seeded, they replay only at the same step. |
| B3 actor tick order | Stands for the live game. The checks below step every system in one fixed order, players by `PlayerId`. |
| B4 overlap callback order | Stands for the live game: overlaps fire in the physics engine's order. Per-player streams make each contest's roll independent of the order, but which contest happens first still follows it. |
| B5 actor iteration order | Narrowed: the AI reads the field through `UPSAIFieldSnapshot`, which lists actors in spawn order (`TActorIterator`). The same spawn order gives the same list on one machine, but not between machines (E12). |
| B6 dual outcome authority | Resolved by Epic C2: `UPSPlaySimulation` is the one authority on the play's outcome. |
| C1 float clocks | Stands; the replay format keys events on integer ticks. |
| C2 rounded yards | Stands. Between machines it amplifies differences (E8). |
| C3 wall clock | Still clean in gameplay. The two new uses don't decide outcomes: the default match seed (E11), and the telemetry sampler's self-throttling, which changes what is sampled, not what happens. |
| D1, D2 bus | Resolved by Epic 115's `UPSReplayRecorder`, which drains the bus and stamps integer ticks. |

### New findings and fixes

**E1. Throw scatter on the global stream (fixed in its own commit).**
`UPSBallActionComponent::ThrowPass` drew `FMath::VRand() * FMath::FRandRange(0, AccuracyError)`.
It now draws from the passer's `ThrowScatter` stream: direction, then distance, in two
statements. Test: `PlaySports.Net.RandomStreams.ThrowScatterIsSeeded`.

**E2. A hit's damage spread never varied (fixed).** `ResolveTackle` made a fresh
`UPSCombatRulesModel` for every hit and never seeded it. A default `FRandomStream` starts at
seed 0, so every hit drew the same first number. That was deterministic by accident, and not
random at all. The model is now seeded from the carrier's `TackleDamage` stream. Test:
`PlaySports.Net.RandomStreams.TackleContestIsSeeded` checks that each hit's spread differs and
that the same seed repeats them.

**E3. Two draws as one call's arguments (fixed).** `UPSPlayOrchestrator::TriggerScrambleDrill`
built its jitter as `FVector2D(Stream.FRandRange(...), Stream.FRandRange(...))`. C++ leaves the
order in which a call's arguments are evaluated unspecified. One compiler can draw X first and
another Y first, so the PC build and the iPhone build would send each receiver to a different
spot from the same seed. The jitter is now drawn in two statements. A search for two draws from
one stream in one expression finds no other case in the game code. The rule for new code is one
draw per statement.

**E4. The global stream is the C runtime's `rand()`, which differs by platform (fixed for
seeded games).** `FMath::RandInit` and `FMath::FRand` call the C library's `srand` and `rand`.
The C standard leaves `rand()`'s algorithm to the implementation, and the two platforms differ
even in range. Microsoft's `RAND_MAX` is 32767 (15 bits); Apple's is 2^31 - 1. `FRand` keeps at
most 24 bits of it. So a Windows `FRand` has 32768 possible values where an iPhone's has
16 million, and the same seed gives two different sequences. Microsoft's `rand()` also keeps its
state per thread. A quick-sim recording seeded through the global stream (replay format
version 1) therefore re-simulates only on the platform, and the thread, that recorded it.

Recordings now seed the simulation's own `FRandomStream`. Its core is integer arithmetic
(`Seed = Seed * 196314165 + 907633515`, with fractions built from its bits), the same on every
platform. The format went to version 2, and version 1 recordings load without their seed
(event playback only). Tests: `PlaySports.Net.Determinism.QuickSimIgnoresGlobalStream` and
`PlaySports.Net.Determinism.VersionOneSeedsMigrateToPlaybackOnly`.

**E5. Seeded streams nobody seeds (not fixed).** Four systems each own an `FRandomStream` that
the live game never seeds: the coaching AI (`UPSCoachingAI`), the play orchestrator (route seeds
and scramble jitter), the pass-rush moves (`UPSRushMoveComponent`) and the defensive pre-snap
decisions (`UPSDefenderPreSnapSubsystem`). Each starts at seed 0 when its object is made. That
is deterministic, and the checks below depend on it, but it has two costs. Every match calls the
same CPU plays in the same situations. And a play can only be re-simulated by replaying the
match from its start, not from its snap. This isn't fixed here because the files belong to other
tracks. When their owners next touch them, they should seed each stream from
`UPSNetRandomStreams`: `MakeMatchSeed` for match-long streams, and the snap's streams for
per-play ones.

**E6. FName hashes are not portable.** `GetTypeHash(FName)` returns the name's index in the
process's name table. That depends on the order names were first used, so it differs on every
machine and every run, and a seed derived from it would differ between the two machines.
Today's seeds avoid it. The franchise code hashes text with `FCrc::StrCrc32` (`PSDraft`,
`PSPlayerAging`, `PSWeeklyPreparation`, `PSPlaybookGenerator`), and the snap-seeded subsystems
hash integers and the clock's float bits. `UPSNetRandomStreams` hashes keys as lower-cased text
with `FCrc`; the case is folded because an FName's spelling is that of its first use. Its mixer
is pinned to values computed outside the engine
(`PlaySports.Net.RandomStreams.SameSeedSameRolls`), so the test checks the integer core on any
platform it runs on.

**E7. The platform tiers change the simulation itself.** `Data/platform_tiers.json` gives the
AI a different decision interval per tier: every frame on `DesktopHigh` (the PC), every 0.033 s
on `MobileBaseline` (the iPhone 17 Pro), and every 0.066 s on `MobileLow`. Five systems read it:
`UPSDefenderAIComponent`, `UPSCoverageMatchupSubsystem`, `UPSDeceptionSubsystem`,
`UPSDefenderGapSubsystem` and `UPSLooseBallSubsystem`. The same play on the PC and on the phone
decides at different moments, so the players run different paths. If both machines simulate the
play, the match needs one simulation tier, whatever each device renders at.

**E8. Floats differ between the PC and the iPhone build (the game can't fix this).** The PC build
is MSVC on x64, and the iPhone build is Clang on arm64. Three effects are outside the game's
control:

- *Fused multiply-add.* Clang may contract `a * b + c` within an expression into one fused
  instruction (its default is `-ffp-contract=on`), and arm64 always has one. The fused result
  is rounded once instead of twice, so it can differ in the last bit. MSVC's default
  `/fp:precise` does not contract. This study didn't check whether the 5.8 toolchain passes
  `-ffp-contract=off` for Apple targets, because there is no engine source here. The next item
  stands either way.
- *The C math library.* `FMath::Sin`, `Cos`, `Tan`, `Acos`, `Pow`, `Exp` and `Loge` call the
  platform's `sinf`, `cosf` and so on. IEEE 754 requires correct rounding only for the basic
  operations and the square root, not for these functions, so Microsoft's and Apple's libraries
  can differ in the last bits for some inputs. The game uses them in 17 source files. Gameplay
  uses include route break angles (`PSRouteRunning`, `Acos`), a loose ball's squirt
  (`PSLooseBallSubsystem`, `Cos`/`Sin`), turning (`APSPlayerPawn::Tick`, `RInterpTo`), kicks
  (`ExecuteKick`, `Sin`) and the league generator's normal draws (`Loge`, `Cos`).
- *Amplifiers.* Rounding a position to whole yards (C2), comparisons against thresholds
  (`Roll <= Chance`, arrival radii) and the AI's discrete choices turn a last-bit difference
  into a different yard, roll outcome or decision, and the difference grows from there.

Making these two builds bit-exact would take a dedicated project. Every float expression on the
simulation path would need a portable formulation: either fixed point, or compiler flags pinned
on both platforms plus replacements for every library function the simulation calls. A
cross-device test would then have to verify it. That is a research project, as the July audit
said, not a story.

**E9. The physics engine is only a query engine here.** Nothing in the game simulates rigid
bodies: there is no `SetSimulatePhysics(true)`, impulse or force. Players move with
`UFloatingPawnMovement` (kinematic sweeps) and the ball with `UProjectileMovementComponent`. The
contests start from overlap events: `APSPlayerPawn::OnPawnOverlap`, `APSBall::OnBallOverlap`,
and the boundary and end zone volumes. So Chaos rigid-body determinism, often the first worry
for a networked physics game, isn't the obstacle. The obstacles are E7, E8, the order of overlap
events (B4), and movement components that keep no history and can't be rewound. A rollback
model would have to own and restore every moving piece of state itself.

**E10. A fixed order exists only in two copies.** The game's per-frame order (field read,
simulation, each AI, rush, gap fits) is spelled out in `UPSPerfHarness::StepFrame` and in this
study's test. The live game relies on the engine's tick order instead (B3). A lockstep model
would need one stepping function that the game itself runs. Replication wouldn't, because only
one machine simulates.

**E11. The default match seed comes from the clock.** `UPSNetRandomStreams` draws it from
`FPlatformTime::Cycles64()` when the world starts. It doesn't use `FMath::Rand`, which would
shift any seeded quick sim. A session that needs the match reproduced must set the seed before
kickoff and send it to the other machine; 108.5's session carries it.

**E12. Iteration order between machines.** On a client, replicated actors are spawned in the
order their packets arrive. So `TActorIterator` and `UPSAIFieldSnapshot::GetFieldPawns` list
them in a different order on each machine. The subsystems that draw from one shared stream per
play (coverage, loose ball, deception) draw in that order too. A lockstep model needs every
order-sensitive loop sorted by `PlayerId` (the July audit's R3). Under replication only the
server simulates, and the order doesn't matter.

### The checks

| Check | What it proves |
|---|---|
| `PlaySports.Net.RandomStreams.SameSeedSameRolls` | The same match seed and snaps roll the same, whatever the global stream holds and whichever player or domain drew first. A new snap starts new streams. The seed mixer matches values computed outside the engine. |
| `PlaySports.Net.RandomStreams.ThrowScatterIsSeeded` | A pass lands on the same spot for the same seed and snap, and elsewhere for another seed, within the passer's inaccuracy (E1). |
| `PlaySports.Net.RandomStreams.TackleContestIsSeeded` | Ten tackle attempts repeat hit for hit, damage included, for the same seed, and each hit's spread differs (E2). |
| `PlaySports.Net.Determinism.QuickSimIgnoresGlobalStream` | A seeded quick-sim game is the same game when the global stream is seeded and drawn differently in between. Another seed plays another game, and the unrecorded game agrees (E4). The test logs the recording's fingerprint: comparing that number with a run on the phone is the cross-platform check this study couldn't run. |
| `PlaySports.Net.Determinism.VersionOneSeedsMigrateToPlaybackOnly` | Version 1 recordings load without their seed and are refused for re-simulation (E4). |
| `PlaySports.Net.Determinism.LivePlaySameSeedSamePlay` | A headless live play runs twice in fresh worlds with the same match seed and differently seeded global streams: 22 players under their AI, the CPU's calls, the snap, routes, rush, coverage and gap fits, for five seconds at 1/30 s, with systems in a fixed order and players by `PlayerId`. Both runs give the same bus events and the same position and velocity for every player and the ball, bit for bit, every frame. A fixed-step integrator stands in for `UFloatingPawnMovement`, which a world that never ticks doesn't drive, so engine movement and overlaps are outside this check. |
| `PlaySports.Replay.RoundTrip.*`, `PlaySports.Gym.SameSeedSameGame` (Epics 115, 24) | A quick-sim game is recorded, saved, loaded and re-simulated, and the first divergence is reported by event, tick and field. |

### What can't be made deterministic across machines, and why

1. **Floats on the simulation path between MSVC/x64 and Clang/arm64** (E8): fused
   multiply-add, the C math library, and the amplifiers. This is out of the game's control
   without rewriting the simulation's arithmetic.
2. **Engine order**: tick order (B3), overlap-event order (B4) and, on a client, the order in
   which replicated actors appear (E12). The game can impose its own order on what it steps,
   but it can't change the engine's.
3. **Engine movement state**: `UFloatingPawnMovement` and `UProjectileMovementComponent` keep no
   history, so a rollback model can't rewind them (E9).
4. **Per-device tiers** (E7). A policy could fix this (one simulation tier per match), at the
   cost of the per-device budgets Epic 129 introduced.
5. **The frame-coupled chances** (B2), which are deterministic only at one fixed step.

What *is* deterministic, and tested:

- on one machine and build, a seeded live play stepped at a fixed step in a fixed order;
- a seeded quick-sim game, whatever state the process is in;
- every outcome roll's integer seed, on any platform. The floats made from it are subject to
  item 1.

### Verdict for 108.2

Deterministic lockstep between the PC and the iPhone isn't feasible for the live, physical play
without rewriting its arithmetic, its stepping and its movement state (items 1 to 3).
Same-platform lockstep is feasible in principle, since the live-play check shows the logic is
deterministic at a fixed step, but it would rule out the cross-play the owner wants. What the
study does make possible is one machine simulating with seeded, reproducible rolls, and the
other trusting its results. `Specs/ADR_Online_Architecture.md` weighs the options on that basis.
