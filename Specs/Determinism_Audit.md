# Determinism Audit & Replay Format Rationale (Epic 115)

Audit date: 2026-07-19, against `main` @ a0399cf. Scope: every system that influences a play's
outcome (`UPSPlaySimulation`, `APSPlayerPawn`, `APSBall`, `APSGameMode`) plus the in-flight
Epic C1 telemetry bus, examined for the three classic divergence sources: RNG discipline,
tick/order dependence, and float stability.

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
