# Platform Audit: Win64 Baseline vs iOS

**Epic:** 129 (`roadmap/platform-ports.md`, Track N). **Status:** first pass, 2026-10-10.
**Reference device:** iPhone 17 Pro (the owner's phone).

This is the budget every system plans against. It separates what is known from the engine and
the code from what can only be measured on a phone. Nothing here has run on iOS yet: an iOS
build needs a Mac (Epic 131), and the CI runner is Windows (`Specs/ADR_CI_Environment.md`).

## 1. What the game costs today (Win64)

| Cost | Where | Scale |
|---|---|---|
| AI decisions | `UPSSkillPlayerAIComponent`, `UPSDefenderAIComponent`, `UPSRushMoveComponent`, `UPSDefenderGapSubsystem` | Up to 22 players. The field is scanned once a frame (`UPSAIFieldSnapshot`, Epic 17.5) and shared; each decision then walks that list of 22 pawns a few times in memory. See section 6. |
| Movement | `UFloatingPawnMovement` on every `APSPlayerPawn` | Kinematic, with no rigid-body simulation. Capsule overlaps drive tackles and blocks. |
| Ball flight | `UProjectileMovementComponent` on `APSBall` | One ballistic projectile. |
| UI | UMG HUD and menus built in code (Epics 5, 101, 102) | A few widgets that update each frame. |
| Rendering | Engine desktop defaults | `Content/` is still empty: no imported meshes, crowd, weather or post effects yet. |

The CPU load is modest and almost all of it is the AI. Rendering cost will arrive with the
world kit (Track R, Epics 142–144), so its budgets have to be set **before** that import, not
retrofitted after it.

## 2. iOS constraints to plan around

These follow from Unreal's iOS support and Apple's platform. The numbers in section 5 still
have to be measured on the phone.

- **Renderer.** The phone runs Unreal's mobile renderer on Metal, and the project plans for
  forward shading. Do not depend on desktop-renderer features (Lumen global illumination,
  Nanite geometry, virtual shadow maps). Plan lighting as baked or static, plus one dynamic sun
  for the day/night cycle (Epic 144), and keep materials simple.
- **Memory.** iOS ends an app that passes its memory limit, and that limit sits well below the
  phone's RAM. Texture resolution and streaming are the main levers (`sg.TextureQuality` per
  tier). Import the world kit at mobile texture sizes, with desktop as the up-scale, not the
  other way round.
- **Heat.** A phone throttles under sustained load, and a football game is a long, sustained
  load. Cap the frame rate (60 on the baseline tier, 30 on the low tier). A steady 30 beats an
  unsteady 60.
- **CPU.** The AI's decision rate is the main code-side knob. Phones decide 30 times a second
  (baseline) or 15 (low), and steer every frame in between, so movement stays smooth. Players
  stay kinematic: no physics-simulated pawns.
- **Input.** Two routes, both through the action catalog (`Specs/Input_Architecture.md`):
  - A paired Bluetooth controller (Xbox or PlayStation) works through the existing gamepad
    path today.
  - Touch is Epic 130's layer (`Specs/Touch_Controls_Spec.md`): a virtual stick, buttons and
    swipes that drive the same catalog actions through their gamepad mappings. A touch-only
    gameplay path is a review-rejection.
- **Build and signing.** Building and signing for iOS needs a Mac with Xcode, and an Apple
  account to install on the phone: a free Apple ID for 7-day test builds, or the Developer
  Program for TestFlight. The how is Epic 131's decision record (`Specs/ADR_iOS_Build.md`)
  and the human signing steps (`Specs/iOS_Signing_Runbook.md`).

## 3. Tiers

Each tier has two halves:

- the **code budgets**, in `Data/platform_tiers.json`, read through
  `PSPlatformTiers::GetActiveTier()`;
- the **rendering settings**, in a device profile in `Config/DefaultDeviceProfiles.ini`.

A run picks its tier from its platform, or from `-PSTier=<TierId>` on the command line, which
lets you test a phone tier on the desktop.

| Tier | Runs on | AI decides | Frame cap | Rendering (scalability groups; 0 low – 3 epic) |
|---|---|---|---|---|
| `DesktopHigh` | Windows, Mac | every frame | none | Engine defaults (`Windows` profile, untouched), on the desktop renderer's look-dev baseline: D3D12 at SM6, Lumen GI and reflections, virtual shadow maps, TSR (`Config/DefaultEngine.ini`) |
| `MobileBaseline` | iOS | every 33 ms (30 Hz) | 60 fps | `IOS` profile: view distance 2, shadows 1, post 1, textures 2, effects 1, foliage 1 |
| `MobileLow` | Android, older or hot iPhones | every 66 ms (15 Hz) | 30 fps | `PSMobileLow` profile: resolution 75%, view distance 1, shadows/post/effects/foliage 0, textures 1 |

The desktop baseline's Lumen, virtual shadow maps and SM6 are desktop-renderer settings: the
phone's mobile renderer reads none of them, so the mobile tiers are unchanged. Their cost on the
desktop is measured per view by the render capture (`.github/workflows/render.yml`, `index.json`'s
`GpuMs`).

`MobileBaseline`'s settings sit on the engine's own `IOS` profile, so every iPhone inherits them
through the engine's per-model profiles. To put a phone on `MobileLow`, make `PSMobileLow` the
`BaseProfileName` of that model's profile. Which models need it is a measurement (section 5).

## 4. Cut-lines for systems that don't exist yet

When one of these lands, it adds its per-tier budget as a field in `Data/platform_tiers.json`
and reads it from `GetActiveTier()`. It never hardcodes a mobile special case.

| System (epic) | DesktopHigh | MobileBaseline | MobileLow |
|---|---|---|---|
| Stadium crowd (Track R crowd assets; Track D) | Animated crowd | Flat cards or impostors | Static bowl, no crowd |
| Broadcast overlays (Track C) | Full | Simplified, no animated transitions | Score bug only |
| Rain and weather (Epic 144) | Particles plus a wet field | Reduced particles, wet field | Wet field, no particles |
| Day/night (Epic 144) | Dynamic sun plus sky | Dynamic sun, baked sky | Fixed time of day |
| Camera effects (Epic 40 and onwards) | Depth of field, motion blur | None | None |
| Player detail (Track A) | Full skeletal LODs | LOD 1+ | LOD 2+ |

Landed so far:

| System (epic) | Tier fields | DesktopHigh | MobileBaseline | MobileLow |
|---|---|---|---|---|
| Telemetry sampling, which overlays, trails and replay read (Epic 26) | `TelemetrySampleRateHz`, `TelemetrySampleBudgetMs` | 30 Hz, 0.25 ms a frame | 15 Hz, 0.15 ms | 10 Hz, 0.1 ms |
| Broadcast overlays (Track A; first user: the Epic 30 reticle) | `OverlayDetail` | `Full`: animated | `Simplified`: no pulses or animated transitions | `Minimal`: score bug and the control reticle, static |
| Personnel panels (Epic 29) | `OverlayDetail` | Shown, flashing on substitutions | Shown, no flash | Not shown |
| Ball-flight indicators (Epic 32), on the same field | `OverlayDetail` | Arc that shortens behind the ball, landing spot, receiver lead, kick readout | Whole arc, static; landing spot, lead, readout | Landing spot and kick readout only |
| Position badges (Epic 28), on the same field | `OverlayDetail` | Every group, fading in | Every group, no fade | The pass buttons only |
| Player emphasis (Epic 36): custom-depth marks for outline, glow and dimming | `OverlayDetail` | Drawn, up to `MaxEmphasized` players | Drawn, up to `MaxEmphasized` players | Not drawn (requests kept) |
| Replay playback (Epic 41): posing every pawn and the ball at the playhead | `ReplayPoseRateHz` | Every frame (0) | 30 a second | 15 a second |
| Pre-snap route art (Epic 27): resolving every player's route to follow shifts and motion | `PlayArtRefreshHz`, `OverlayDetail` | 30 a second; fades at the snap | 15 a second; gone at the snap | Not drawn (0: events only) |
| Audio (Epic 23): voices sounding at once, and releasing them and following the volume settings; the crowd's excitement settling | `AudioMaxVoices`, `AudioUpdateHz`, `CrowdUpdateHz` | 32 voices, every frame; crowd 30 a second | 16 voices, 30 a second; crowd 15 a second | 12 voices, 15 a second; crowd 10 a second |

The sampler also halves its own rate when its frames run over the tier's budget
(`PlaySports.TelemetrySampling.BudgetDegradesAndRecovers`); `stat PSTelemetrySampling` shows its
cost on a device.

## 5. Verification

| What | How it's verified today | Still needed |
|---|---|---|
| Tier data is sound, and each tier's profile exists | `tools/validate_data.py`, `PlaySports.Platform.TierCatalogValidates` | — |
| The right tier is chosen per platform, and the override works | `PlaySports.Platform.TierResolution` | Confirm on the phone with the `PSPlatformTiers: running tier ...` log line |
| The AI decides at its tier's rate and steers in between | `PlaySports.Platform.AIDecidesAtTierRate` | — |
| The profile's settings reach an iPhone | Nothing: Win64 CI never loads the IOS profile | A Mac build on the phone. Read `sg.*` and `t.MaxFPS` in the console, and check the engine's per-model profile doesn't override them. |
| Frame time, memory and heat over a full game | Nothing | Xcode Instruments on the phone during a CPU-vs-CPU game: tune the tier numbers from those measurements |
| The AI scans the field once a frame, however many players decide | `PlaySports.AI.Performance.OneFieldScanPerFrame` | — |
| The AI's time per frame with 22 players | Nothing measured on a device. `stat PSAI` shows it (section 6); CI times it headless (section 7) | The device run in section 6 |
| Every system holds to its tier's frame-time budget | `PlaySports.Perf.StandardPlayProfile` profiles the standard play headless; `tools/perf_budget.py` holds it to the budgets and the trend in CI (section 7) | `PS.Perf.Capture` on the phone (section 7) |

## 6. AI performance pass (Epic 17.5)

**What changed (code, verified headless).** Before this pass, each AI player's decision
scanned every actor in the world several times. It looked for the ball carrier, the passer, the
receivers and the nearest defender, and it copied each pawn's attribute row (strings included)
to read its role. At 22 players that is about a hundred actor scans and thousands of row copies
a frame.

`UPSAIFieldSnapshot` (a world subsystem) now scans once a frame and keeps the pawn list and each
pawn's role. Every AI system reads the field through it:
- `UPSDefenderAIComponent`
- `UPSSkillPlayerAIComponent`
- `UPSRushMoveComponent`
- `UPSDefenderGapSubsystem`

Positions, velocities and possession are still read live, so nothing a decision sees is a frame
old. A new frame, a pawn spawning, or a listed pawn being destroyed starts a new scan.
`PlaySports.AI.Performance.OneFieldScanPerFrame` checks the contract: 22 players deciding twice
in a frame scan once between them.

**Players stay kinematic.** "Physics" in the roadmap story is `UFloatingPawnMovement` plus capsule
overlaps; nothing is simulated (section 2). The gap accounting runs at the tier's
`AIDecisionInterval`, like the players' decisions.

**What is still to measure (needs a device, or at least a packaged Win64 build).** Nobody has
measured the frame rate yet. Measure it with this procedure:
1. Run a CPU-vs-CPU game: both sides called by the CPU, no human input, the default map.
   - On Win64, run it in a Development build, not the editor.
   - On the iPhone, run the Epic 131 build.
2. Open the console and turn on `stat unit`, `stat game`, `stat PSAI` and `stat PlaySports`, and
   run `PS.Perf.Capture 120` (section 7). Play at least one full drive, so every play type runs: a
   run, a pass and a sack.
3. Record the game-thread time and the `PSAI` counters at the snap, during the rush, and in
   pursuit. The counters are:
   - `Defender AI decision`
   - `Skill player AI decision`
   - `Field scan`
4. Do it at each tier: the platform default on the phone, and `-PSTier=MobileBaseline` and
   `-PSTier=MobileLow` on Win64 as a proxy.

**Budget to check against.** These are proposed figures; nothing has measured them yet.
- The whole AI should stay under 2 ms of game-thread time per frame on `MobileBaseline`. That is
  an eighth of a 60 fps frame, which leaves room for movement, UI and the world kit.
- If it doesn't, raise that tier's `AIDecisionInterval` before changing code.

Record the measured numbers here, replacing this paragraph.

## 7. Frame-time budgets and the profiling harness (Epic 114)

**Budgets.** Each tier in `Data/platform_tiers.json` has a `TargetFrameRate` and a
`SystemBudgets` entry for every system the game runs on the game thread. Each entry is that
system's share of the frame, in ms. The shares must fit in the frame. The rest of the frame goes
to the engine: movement, physics queries and UMG's own layout. Rendering runs on the render
thread.

| System | What it covers | DesktopHigh (60 fps, 16.7 ms) | MobileBaseline (60 fps, 16.7 ms) | MobileLow (30 fps, 33.3 ms) |
|---|---|---|---|---|
| Simulation | `UPSPlaySimulation` | 1.0 | 0.75 | 1.0 |
| AI | decisions, the field scan, the pass rush, gap fits | 2.0 | 2.0 (section 6) | 3.0 |
| Telemetry | the bus recording events, the sampler (includes `TelemetrySampleBudgetMs`) | 0.5 | 0.3 | 0.3 |
| Overlays | ball flight, broadcast package, reticle, badges | 1.0 | 0.75 | 0.5 |
| UI | HUD and menus | 1.0 | 0.75 | 1.0 |
| Animation | not built yet (Track A) | 3.0 | 2.5 | 4.0 |
| Crowd | the crowd's excitement (Epic 23.2); the rendered crowd (Track R) not built yet | 2.0 | 1.0 | 0.5 |
| Audio | the cue mapping and the commentary (Epics 23, 96); no sounds imported yet | 1.0 | 0.75 | 1.0 |

These are proposed figures, like section 6's, set before the world kit arrives. A device
measurement replaces them. A system that lands later keeps to its row, or argues for a new
one in the same change.

**Instrumentation.** Each system's entry points open a `PS_PERF_SCOPE(<System>)`
(`PSPerfBudget.h`):
- AI: the AI decisions;
- Simulation: `UPSPlaySimulation::AdvancePlay`;
- Telemetry: `UPSTelemetryBus::RecordHistory` and the sampler's frame;
- Overlays: each overlay's step and the score bug and chyron widgets.

Time is exclusive. A bus event published from an AI decision counts as Telemetry, not as AI too.
The same scopes feed `stat PlaySports`: one counter per system, plus bus events, AI decisions and
field scans per frame. The finer AI counters stay in `stat PSAI`.

**The harness (CI).** `UPSPerfHarness` plays the standard play under full load in a headless
world:
- two full elevens under AI, and the ball;
- the snap, with the CPU calling both sides;
- the rush, the routes and the coverage;
- a throw and a catch, pursuit, the tackle and the whistle;
- the next down's pre-snap.

Each frame it steps what the game ticks, at the running tier's AI decision interval.
`PlaySports.Perf.StandardPlayProfile` runs it in CI. It asserts the counts, which hold on any
machine: at most one field scan a frame, and the bus within `MaxBusEventsPerPlay` events. It then
writes `Saved/Profiling/StandardPlay_<tier>.json`.

CI's Performance budgets step (`tools/perf_budget.py`) checks that report:
- A system over its budget at the 95th percentile is a warning.
- A system over `HardFailMultiplier` times its budget fails the build.
- A system that regressed against the median of its last runs on main is a warning. Pushes to
  main record each run in the runner's history (`%USERPROFILE%\play-sports-ci\perf_history.jsonl`,
  or `PS_PERF_HISTORY`).

The report is uploaded as the `perf-report` artifact, and the step summary shows the table.
`python tools/perf_budget.py trend --history <file>` prints the history.

The CI runner is the editor with no renderer, on a laptop shared with other work. Its times
check the budgets' order of magnitude and catch regressions. They are not device numbers.

**On a device.** Two console commands write the same report, which `tools/perf_budget.py check
<file>` reads:
- `PS.Perf.Capture [seconds]` profiles real play for that long (30 s by default) and writes
  `Saved/Profiling/Live_<tier>.json`.
- `PS.Perf.RunHarness` plays the standard play in a scratch world and writes
  `StandardPlay_<tier>.json`.

Run both on the phone (Epic 131's build) and on a packaged Win64 build at each tier. Record the
numbers here, replacing the proposed budgets. Nothing in this section has been measured on a
device yet.
