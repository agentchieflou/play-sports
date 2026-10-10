# ADR: Online Architecture (Lockstep, Replication or Hybrid)

**Status:** Proposed (2026-10-10). The owner decides; nothing here is accepted until they say so.
**Epic:** 108 (`roadmap/multiplayer.md`, Track J), story 108.2. Stories 108.3 (the two-machine
prototype) and 108.4 (the latency strategy) wait on this decision.
**Related:**
- `Specs/Determinism_Audit.md`: the 108.1 feasibility study this ADR rests on (findings E1 to
  E12).
- `Specs/Platform_Audit.md` and `Data/platform_tiers.json`: the iPhone 17 Pro's budgets.
- `Specs/ADR_iOS_Build.md`: how the iPhone build gets made.
- 108.5's session and matchmaking interface (`PSSessionService.h`, in its own PR).
- Epic 107's `UPSVersusSubsystem`: the two-seat game every online mode builds on.

## Context

**The game.** American football: discrete plays. Each play has a pre-snap phase (play calls,
with each side's pick hidden from the other, then audibles and motion) followed by a live phase
of a few seconds. In a head-to-head game each human controls one player at a time; the other
21 players on the field are AI. The live phase runs the AI for 20 or more players, kinematic
movement (`UFloatingPawnMovement`), the ball (`UProjectileMovementComponent`), and contests
started by overlaps and resolved with random rolls. `UPSPlaySimulation` is the one authority on
the outcome (architecture rule 6), and systems talk through the telemetry bus (rule 5).

**The owner's target.** They want to play on their iPhone 17 Pro, including against someone on a
PC (cross-play). So the design has to work over mobile networks, with the iPhone's battery and
heat limits, and between two different builds: MSVC on x64 for the PC, Clang on arm64 for the
iPhone.

**What the 108.1 audit found** (`Specs/Determinism_Audit.md`, "Epic 108"):

- On one machine, the logic is deterministic once it is stepped at a fixed step in a fixed
  order. A headless live play run twice is the same play, bit for bit
  (`PlaySports.Net.Determinism.LivePlaySameSeedSamePlay`). The outcome rolls are seeded per
  match, play, kind of roll and player, and their integer seeds are the same on every platform.
- Between the PC and the iPhone, the live play can't be made bit-identical without a rewrite:
  - float results differ, from fused multiply-add and from each platform's math library (E8),
    and the game's thresholds and rounding amplify them;
  - the engine's tick and overlap order isn't the game's to fix (B3, B4), and on a client,
    replicated actors arrive in a different order (E12);
  - the movement components keep no history, so they can't be rewound (E9);
  - the platform tiers give the iPhone a different AI decision rate than the PC (E7).
- The physics engine is used for collision queries and overlaps only; nothing in the game
  simulates rigid bodies (E9).

**Mobile facts to design for.** These are planning assumptions, not measurements: no device was
available to this study.

- *Cellular latency* is higher and more variable than Wi-Fi. Plan for round trips of about
  50 to 150 ms with spikes beyond, and some packet loss.
- *Networks change under the game.* A phone moving between Wi-Fi and cellular gets a new
  address, which drops the connection. When the player switches apps or locks the screen, iOS
  suspends the game within seconds.
- *Peer-to-peer is unreliable.* Carrier-grade NAT often blocks direct connections between two
  phones, so a relay or a server is needed.
- *Radio energy.* The radio stays in its high-power state while packets flow, so a steady
  stream at 30 to 60 Hz costs battery for the whole live phase.
- *Heat.* Sustained CPU load throttles the phone. The `MobileBaseline` tier budgets 2.0 ms of AI
  and 0.75 ms of simulation per frame at 60 fps (`Data/platform_tiers.json`).

## Options considered

### A. Deterministic lockstep with rollback

Each machine runs the whole simulation and they exchange only inputs. When a late input arrives,
a machine rewinds to the frame it belongs to and re-simulates forward (the GGPO model).
Lockstep without rollback (each input delayed until both machines have it) is the variant below.

- **For:**
  - Tiny bandwidth: a few bytes of input per player per frame.
  - No server, and no corrections: both screens show the same game.
  - A match's replay is its inputs plus its seed.
- **Against:**
  - **It needs bit-exact determinism between the two machines, and the audit shows the PC and
    the iPhone can't have it** (E8, items 1 to 3 of the audit's list). Same-platform lockstep
    would work in principle, but it rules out the cross-play the owner wants.
  - A desync, the first bit that differs, ends the match. It needs state checksums and a
    resynchronization path (109.2).
  - **Rollback costs the phone the most.** Re-simulating one step costs about the tier's
    simulation and AI budgets, 2.75 ms on `MobileBaseline` (an estimate from the budgets, not a
    measurement). Hiding 100 ms of one-way latency at a 30 Hz simulation means re-simulating
    3 steps, about 8 ms extra in a frame whose whole budget at 60 fps is 16.7 ms, during every
    live play. That is the sustained load that throttles a phone and drains its battery.
  - Both machines run all the AI all the time. There is no saving for the weaker device.
  - The game would have to own and restore every piece of moving state itself, because the
    engine's movement components can't be rewound (E9). It would also need one fixed simulation
    tier for the match (E7) and a single stepping function in a fixed order (E10).
  - *Without rollback*, every input is delayed by the round trip plus a jitter buffer. On
    cellular that is 60 to 150 ms or more, which makes the live phase feel sluggish. It suits
    the pre-snap phase, not the live play.
  - Unreal has no built-in lockstep: all of it would be custom code.

### B. Unreal replication, one authoritative server

One machine, the server, simulates everything: AI, movement, contests and rolls. It replicates
the 22 players and the ball to the client. The client sends inputs, predicts its own controlled
player, and smooths everyone else.

- **For:**
  - **No cross-machine determinism is needed.** One machine decides every fact, which is
    architecture rule 6 applied to the network. Desyncs can't happen: the client is corrected.
  - It is the engine's own model (actor replication, RPCs, relevancy), so the networking layer
    isn't custom work.
  - It tolerates loss and jitter. Positions are sent as frequent unreliable updates, so a lost
    packet is replaced by the next one, and outcomes go as reliable events.
  - A client phone does less work than an offline game: it runs no AI, so it stays cooler and
    lasts longer. A hosting phone does roughly what it does offline against the CPU, which
    already simulates all 22 players, plus the cost of sending state.
  - Spectators (Epic 110) and reconnects are natural: a newcomer receives the current state.
- **Against:**
  - Bandwidth is higher than lockstep, but modest. 23 moving actors at 30 Hz with quantized
    position, velocity and facing come to roughly 100 to 200 kbps downstream during the live
    phase, and almost nothing between plays. That is fine on Wi-Fi, LTE and 5G. Upstream is a
    few kbps of input.
  - The client's view of contests lags. A tackle, catch or interception involving the client's
    own player is decided on the server and arrives a round trip later, so the client sometimes
    sees a correction.
  - A player on a listen server (one of the players hosting) has no latency. That is an
    advantage, which lag compensation (rewinding positions for contest checks) or a dedicated
    server can address later.
  - `UFloatingPawnMovement` has no built-in client prediction. The engine's predicted movement
    is `UCharacterMovementComponent`, and Epic has newer prediction frameworks (the Network
    Prediction plugin, and Mover since 5.4). Their 5.8 state needs checking before choosing one.
    A simpler start is for the client to move its own player and the server to accept the
    position within speed limits. That is good enough between friends, not for ranked play.
  - A phone that hosts pays for it in heat and battery, and the match ends if it backgrounds.
    Host migration isn't supported.

### C. Hybrid: an authoritative live play, a turn-like pre-snap, seeded rolls (recommended)

Option B for the live play, plus three things the game's structure and the 108.1 work make
cheap:

1. **The pre-snap phase is a turn exchange, not a stream.** Play calls, audibles, hot routes and
   timeouts go to the host as reliable messages. The host keeps each side's call hidden until
   the snap: Epic 107's hidden picks, enforced by the one machine that knows both.
   Nothing time-critical happens before the snap, so latency there costs only a short wait.
2. **The host rolls on seeded streams** (`UPSNetRandomStreams`, `UPSPlaySimulation::SeedRolls`).
   Its outcomes are reproducible from the match seed on the same build, so a disputed play or a
   bug report can be re-simulated on the host's platform. The live-play check proves this today
   at a fixed step; the host must step at a fixed step to keep it (see the decision below).
3. **The bus's outcome events are the replicated record.** The host's `PlayResult`, `GameState`,
   `Score` and similar events go to the client as reliable messages, the same stream Epic 115
   records and Mode 1 replay plays back. The client's HUD, stats and commentary read the same
   facts as the host's, so nothing is re-derived on the client.

Asynchronous league play (Epic 111) needs no live link. One device simulates a game with the
seeded quick sim and uploads the result. The other side can re-simulate it to audit it, which
is exact on the same platform; across platforms it holds to the extent the logged fingerprint
check shows (audit E4, E8).

## Recommendation

**Option C.** Concretely, as the starting point for 108.3 and 108.4:

- **Authority.** One host per match: a listen server. The PC hosts when one of the players is on
  a PC; between two phones, the one on Wi-Fi and power hosts. A dedicated server is a later
  option for ranked play or fairness, not a prerequisite. The session (108.5) decides who hosts
  and carries the match seed, the build's protocol version and both players' platforms and input
  types.
- **Simulation.** The host steps the live play at a fixed rate, proposed 30 Hz (the
  `MobileBaseline` AI rate), decoupled from rendering. That covers audit finding B1 for online
  play, and the July audit's R4. The simulation tier is the match's, not the host device's, so
  hosting on the PC doesn't change how the AI plays (E7).
- **Replication.** The host sends player and ball state at 20 to 30 Hz, and the client renders
  remote players about 100 ms in the past, interpolated. The client sends input at 30 Hz.
- **Prediction scope.** Only the client's own controlled player is predicted, starting with the
  simple client-moves, host-validates scheme. The host resolves every contest. A thrown ball can
  be flown on the client from its launch event (a deterministic arc) and corrected at the catch.
  108.4 sets the budgets.
- **Latency targets** (for 108.4 to refine): up to 120 ms round trip plays normally; up to
  200 ms plays with visible corrections; beyond 250 ms the HUD warns (Epic 109.4's indicators).
- **Disconnects.** Extend Epic 107's etiquette, in which a disconnected seat pauses the game
  until it returns. Allow a reconnect window (proposed 60 s) while the host keeps the state. On
  iOS, switching apps, locking the screen or a Wi-Fi-to-cellular handoff count as a disconnect,
  followed by a reconnect. A host that drops ends the match: no migration.
- **Battery and heat.** A client phone runs no AI. Between plays, traffic drops to a keep-alive
  so the radio can idle. A hosting phone is capped at the 30 Hz simulation. Measure both on the
  device (Epic 114's harness and `PS.Perf.Capture`) before promising match lengths.
- **Cross-play.** The interface in 108.5 keeps the platforms' services behind one abstraction.
  Matching an iPhone with a PC needs a backend both can reach: Game Center alone won't match
  them, and Epic Online Services is the obvious candidate for an Unreal game (to be confirmed
  when 109 starts). Matchmaking takes each player's input type, so that touch players can be
  matched with touch players by default, and with gamepad and keyboard players when they opt in.
- **Not pursued:** bit-exact cross-platform simulation, and rollback of the live play.

## Consequences

- **What 108.3 builds:** a listen server and one client, one play. That means replicating the 22
  players and the ball, the pre-snap calls as reliable messages, the host rolling on its seeded
  streams, the host stepping the live play at a fixed step, and the client predicting its own
  player. It needs two machines on a network, which no agent has.
- **The determinism work stays useful.** Seeded rolls make the host's plays reproducible for
  debugging and audits, and the quick sim's portable seeding serves asynchronous league play.
  The fixes so far (E1 to E4) were needed for any option.
- **Work that is no longer required:** making the live play bit-exact across platforms,
  rollback, and canonical actor ordering on clients (E12). Only the host simulates.
- **New risks:**
  - The host's advantage on a listen server.
  - Choosing a prediction framework for the controlled player.
  - Choosing an online backend and its account setup, including Apple's rules on sign-in
    services if third-party login is offered.
  - A hosting phone's heat.
- **What would reopen this decision:** dropping cross-play to PC against PC only, with a strong
  wish for an offline-like feel at any latency. Lockstep with rollback would then be feasible,
  at the cost of findings E7, E9, E10 and E12 becoming required work.

## Questions for the owner

> 1. **Accept Option C** (an authoritative host, a turn-like pre-snap, seeded rolls), or prefer
>    another option?
> 2. **Who hosts?** A player's machine (free; the host has an edge), or a paid dedicated server
>    later for ranked play?
> 3. **Cross-play with touch:** should iPhone touch players meet PC gamepad and keyboard players
>    by default, or only when both opt in?
> 4. **Online backend:** is Epic Online Services acceptable for accounts and matchmaking between
>    the iPhone and the PC, or is there another service you want?
