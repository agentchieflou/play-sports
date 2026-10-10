# Track S — Three-Platform Release (Epics 145–154)

The north star (re-pointed 2026-10-10): **one game, playable on iOS, Xbox and PC.** It is the same
game on all three: one code base, one data set, one rule set. Only packaging, input defaults,
scalability tier and platform services differ. `roadmap/MILESTONES_PLATFORMS.md` is the ladder
from here to each platform's dream state; this track holds the Epics that ladder needs and no
other track covers. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-10-10):** the code base is far ahead of the game you can hold. CI compiles
Win64 and runs about 500 headless tests, but `Content/` is empty, so nothing runs outside the editor
and nobody has played a full game on any device. Every platform goal therefore starts with the
same shared gate: get a cooked, packaged game with real content out of the editor (145–147).
Editor-mode stories have stalled all project long because agent sessions have no editor. Epic 146
turns most of them into code that runs the editor headlessly on the CI runner, which already has
Unreal Engine 5.8 installed.

Platform facts this track relies on (verify before acting; each owner gate is a decision for the
owner, not an agent):

- **PC (Win64):** the dev machine and the self-hosted runner are Windows with UE 5.8. Nothing
  gates it but the work.
- **iOS:** UE packages and signs iOS only on a Mac (`Specs/ADR_iOS_Build.md`, accepted
  2026-10-10: a Mac as a second CI runner; `Specs/iOS_Signing_Runbook.md`). The owner chose a free
  Apple ID, which sideloads 7-day builds. The iOS goal is playing on the owner's iPhone; TestFlight
  and the App Store (which need the Developer Program) come later, in Epic 154.
- **Xbox Series X|S:** from UE 5.8 the Microsoft GDK plug-ins and Xbox platform extensions ship
  publicly with the engine, with no gated download. Deploying to a console (and possibly building
  for it; Epic 150 checks) still needs Xbox developer onboarding through ID@Xbox (NDA, concept
  approval, Partner Center registration), the GDK on a dev PC, and console hardware enabled for
  development. The owner will apply later (decided 2026-10-10). Sources:
  [Building Xbox games with Unreal Engine's new GDK plug-ins](https://developer.microsoft.com/en-us/games/articles/2026/06/building-xbox-games-with-unreal-engines-new-gdk-plug-ins/),
  [Unreal on Xbox (devdocs)](https://devdocs.xbox.com/build/gdk-and-engines/unreal/unreal),
  [Xbox Developer Mode activation](https://learn.microsoft.com/windows/uwp/xbox-apps/devkit-activation).

Rules for this track:

- **No platform forks in gameplay code.** A platform difference lives in data (`Data/platform_tiers.json`,
  device profiles, input catalogs) or behind one interface with per-platform implementations (152).
  An `#if PLATFORM_IOS` in a gameplay system is a review rejection.
- **The packaged build is the truth.** A platform rung counts as climbed only on a packaged
  build on that platform's hardware, never on PIE.
- **Owner gates come first.** An Epic whose first story is an owner gate doesn't start until the
  owner has answered.

### Epic 145: Packaged Win64 Build That Boots Into a Game

**Size/Mode:** M / code
**Goal:** CI produces a cooked, packaged Win64 build on every main push, and proves headlessly that it boots and plays a full game.
**Depends on:** 112, 101

- [ ] CI packaging job: `RunUAT BuildCookRun` for Win64 Development (and Shipping on demand) on the self-hosted runner, on pushes to main and by manual dispatch, with the packaged build uploaded as an artifact. This is the first build of the `PlaySports` game target on any platform (CI builds only the editor target today): fix editor-only code outside `WITH_EDITOR`, and keep `Plugins/Autonomix` (editor-only dependencies) out of game targets (`Specs/ADR_iOS_Build.md`, prerequisites 1–2)
- [x] Data staging audit: every loader that reads `Data/*.json` finds it in a packaged build (`Data/` staged as non-UFS or cooked into a known path); a test lists every default data path and asserts it resolves under the packaged layout *(`PlaySports.Build.cs` stages all of `Data/` loose (non-UFS runtime dependency on `$(ProjectDir)/Data/...`) at the same place under the build's project directory, so every `FPaths::ProjectDir() / "Data/..."` path resolves unchanged and testers can still edit the tuning. `PSDataPaths` lists the 89 default data files plus the rosters `Data/sample_teams.json` names; `PlaySports.Packaging.DataStaging.*` checks each is staged and resolves, including under the packaged layout (a project directory holding only `Data/`); `tools/tests/test_data_staging.py` fails when a loader's default path is missing from the list; the packaged smoke test checks the real build carries every one)*
- [x] Packaged-build smoke test: the packaged executable runs headless (`-nullrhi`), plays a scripted full game (`PlaySports.Gym.ScriptedFullGame`'s path) and exits with the final score; CI fails if it doesn't *(`UPSPackagedSmokeTest`, `-PSSmokeTest`: runs at `OnPostEngineInit`, before any map loads, so it needs no level; checks the data, loads the league's first two teams through `UPSMatchSetup`, plays a seeded full game on `UPSQuickSimRunner`, logs `PSSmokeTest: PASS seed=145 home=... away=...`, writes `-PSSmokeTestReport=` JSON and exits 0 or 1. `.github/workflows/package.yml` runs it on the packaged build and fails on a non-zero exit or a failed report; headless tests `PlaySports.Packaging.SmokeTest.*`)*
- [ ] Boot flow: a packaged build opens the front end (Epic 101), and Play Now reaches the field with no editor-only dependency (no `WITH_EDITOR` path on the way)

### Epic 146: Content as Code — Headless Editor Content Pipeline

**Size/Mode:** L / mixed
**Goal:** The content the specs describe is built by scripts that drive the editor headlessly on the CI runner, so "editor" stories become reviewable code.
**Depends on:** 112

- [ ] Decision record (`Specs/ADR_Content_Pipeline.md`), **owner decided 2026-10-10: Git LFS.** Every `.uasset`/`.umap` (imported art and generated levels and widgets alike) is committed through Git LFS; scripts regenerate the generated ones, and CI's drift check keeps them in step with their sources. The ADR covers `.gitattributes`, LFS on the runner (checkout and cache), the storage and bandwidth quota, how pipeline output reaches a branch, and how the owner's hand edits coexist with generated assets
- [ ] Pipeline runner: a CI step runs `UnrealEditor-Cmd -run=pythonscript` scripts, the same Python path as Autonomix's `run_python` (25.2), building content idempotently from specs and data, with a drift check that fails when content no longer matches its sources
- [ ] Field level from `Specs/Field_Geometry_Spec.md` and `Specs/Field_Markings_Spec.md`, set as the default map (`Specs/Default_Map_Spec.md`): closes Epic 2.1–2.2 as code
- [ ] Widget Blueprints generated from the C++ widget classes: HUD and score bug (`Specs/HUD_Spec.md`), front end (`Specs/Front_End_Shell.md`), play-call screen (`Specs/Play_Call_Interface.md`), touch controls (`Specs/Touch_Controls_Spec.md`). Each is functional rather than final art
- [ ] World kit import driven by the same pipeline: Track R's `RawAssets/world/` meshes, textures and materials into `Content/` per `RawAssets/world/README.md` (closes Epic 142's editor stories)

### Epic 147: Minimum Playable Content Set

**Size/Mode:** L / mixed
**Goal:** A newcomer can launch the packaged game and play a full game that looks and reads like football: field, players, ball, HUD, menus.
**Depends on:** 145, 146, 142

- [ ] Field and stadium shell: the field level with markings, goal posts and end-zone colors, a simple bowl or sideline set, and one daylight lighting setup
- [ ] Player characters: the world kit's character on UE's skeleton for all 22 players, team colors from team identity data (one material parameter set per team), and readable numbers or position badges
- [ ] Core animation slice (Epic 22's minimum): locomotion (idle, run, backpedal, strafe) driven by Epic 6's movement, plus throw, catch, handoff, tackle and fall. Use retargeted CC0 or engine-sample animations; Track D's depth comes later
- [ ] Ball mesh, field audio placeholders (whistle, pads, crowd bed from CC0 sources through Epic 23's cue map) and the assembled HUD, menus and play-call screen from 146
- [ ] Newcomer playtest (human): someone who has never seen the game starts, plays and finishes a full game on the packaged PC build without help; issues are filed against the Epics that own them

### Epic 148: PC Release — Settings, Input Parity, Performance

**Size/Mode:** M / mixed
**Goal:** The PC build is a complete, comfortable PC game: settings, keyboard/mouse and pad everywhere, a steady frame rate.
**Depends on:** 145, 147

- [ ] Settings screen: graphics (scalability tier from `Data/platform_tiers.json`, resolution, window mode, vsync, frame cap), audio volumes (Epic 23's layers) and controls (Epic 104's remapping), saved per user
- [ ] Input parity on every screen: menus navigable by mouse, keyboard and gamepad focus; glyphs follow the last device (Epic 128)
- [ ] Performance: 60 fps at `DesktopHigh` on the owner's PC in a packaged Shipping build, measured with the perf harness (Epic 114) on the device; closes 17.5's PC measurement
- [ ] Shipping configuration: Shipping build with crash reporting (Epic 117), saves under the user profile, a version stamp on the title screen, a README for installing and running
- [ ] Distribution, **owner decided 2026-10-10: a private zip first.** A versioned zip of the Shipping build for the owner and invited testers, produced by CI and kept as a release artifact. Steam comes later as its own Epic (the Steamworks app fee and its integration)

### Epic 149: iOS — Playable on the Owner's iPhone

**Size/Mode:** L / mixed
**Goal:** The same game, installed on the owner's iPhone 17 Pro from the Mac and played start to finish with touch. App Store readiness is not part of this Epic; it comes later (Epic 154).
**Depends on:** 131, 145, 147

- [ ] Owner gates, **decided 2026-10-10:** a Mac as a second self-hosted CI runner (`Specs/ADR_iOS_Build.md` Option B, accepted) and a free Apple ID (7-day sideloads). Left for the owner: the Mac itself (model, disk, always on), registered as the `mac` runner with `IOS_MAC_RUNNER=true`, and the iPhone paired with it once
- [ ] A signed Development build installed on the iPhone from the Mac runner with the free Apple ID; `.github/workflows/ios-package.yml` made real, and the weekly reinstall (free-account builds stop launching after 7 days) a short documented routine in `Specs/iOS_Signing_Runbook.md`
- [ ] Touch HUD assembled (`Specs/Touch_Controls_Spec.md` section 6, from 146) with safe areas and a landscape lock; every screen usable by touch, and a paired Xbox pad works too
- [ ] Smooth enough to play: the content set on the mobile renderer, targeting 60 fps at `MobileBaseline` and never below 30 fps in play, measured on the device with Epic 114's harness (closes 17.5 for iOS), through a full game without thermal throttling below 30 fps
- [ ] App lifecycle through 152's iOS implementation (local storage and lifecycle events only): backgrounding mid-play pauses the game and saves, and returning resumes it; audio interruptions behave

### Epic 150: Xbox Readiness Without a Console

**Size/Mode:** M / code
**Goal:** Everything Xbox needs that can be built and tested on Win64 is in place before the owner applies to ID@Xbox, so console bring-up is packaging, not new systems.
**Depends on:** 152, 129

The owner decided on 2026-10-10 to build the guts now and apply to ID@Xbox later. Nothing in this
Epic needs an NDA, a dev kit or Partner Center.

- [ ] An `XboxSeries` tier in `Data/platform_tiers.json` and a matching device profile, with Series S as the performance floor, read through the existing tier system (no hardcoded console checks)
- [ ] Users and controllers through 152: a controller disconnect pauses a live game and offers reassignment, and each local player is paired with a user and a controller (local versus, Epic 107), tested headlessly on Win64 against the null implementation
- [ ] Lifecycle through 152: suspend, resume and constrained events pause a live game and flush saves, so Quick Resume needs no new gameplay code; tested headlessly
- [ ] Presentation: a title-safe area every HUD and menu widget respects (one setting, per platform in data), and Epic 128's Xbox glyphs and terminology checked on every screen
- [ ] `Specs/ADR_Xbox_Access.md`: what the ID@Xbox application, Partner Center registration and dev hardware involve, ready for the owner to apply; and whether UE 5.8's public GDK plug-ins can compile the console target without onboarding (if they can, CI compiles it on every PR from here; if not, the compile moves to 151.1)

### Epic 151: Xbox Console Bring-Up and Certification Readiness

**Size/Mode:** L / mixed
**Goal:** The same game plays a full game on an Xbox Series console with a controller, and behaves the way Xbox requires of a title.
**Depends on:** 150, 147

- [ ] Owner gate (when the owner chooses to apply): ID@Xbox onboarding (NDA, concept approval, Partner Center registration) and console hardware enabled for development, per `Specs/ADR_Xbox_Access.md`; the GDK on the Windows runner and the console target compiling in CI if 150 could not
- [ ] 152's Xbox implementation: the GDK's user, storage and lifecycle services behind the same interface, with sign-in and the account picker at boot
- [ ] Deploy and play: a packaged build on the console plays a full game with an Xbox controller at 60 fps on Series S (the floor) and Series X
- [ ] On-console checks of 150's work: controller disconnect and reassignment, suspend/resume and Quick Resume mid-game, constrained mode, title-safe area, and boot within the platform's time limits
- [ ] Store readiness (owner decides when): age rating (IARC), store listing assets and a pre-certification pass against the current Xbox Requirements

### Epic 152: Platform Services Layer

**Size/Mode:** M / code
**Goal:** One interface gives the game user identity, storage, achievements/presence and lifecycle events, with one implementation per platform, so gameplay code never branches on platform.
**Depends on:** 115, 117

- [x] `UPSPlatformServices` (game-instance subsystem): the signed-in user, the save storage root, achievements/presence and lifecycle events (suspend, resume, constrained) published on the bus. This Epic ships the null/local implementation (Win64 and every test); the iOS (149) and Xbox (151) implementations plug in behind the same interface, chosen by platform config *(As built: `UPSPlatformServices` fronts one `UPSPlatformBackend`, named by `BackendClass` in `[/Script/PlaySports.PSPlatformServices]` of `Config/DefaultGame.ini`, which a platform's own Game.ini overrides; `UPSPlatformBackendLocal` is the null/local one: a user per local slot ("Player N"), `Saved/SaveGames`, achievements and presence kept in memory. Lifecycle is `EPSPlatformLifecycle` (Suspend, Resume, Constrained, Unconstrained), published as the bus's `Lifecycle` event and on the native `OnLifecycleMC`; `PS.Platform.Lifecycle <event>` plays one on a PC.)*
- [x] Epic 115's save system stores through it unchanged above that layer; saves written on one platform load on another *(As built: `UPSSaveSubsystem` keeps its slots under the services' save storage root; its API and file format are unchanged, and a subsystem without services (every headless test) uses the configured platform's default root, the old `Saved/SaveGames`. A save written under one platform's root loads under another's: `PlaySports.Platform.SavesStoreThroughServices`.)*
- [x] Lifecycle events pause a live game and flush saves; 149 and 151 consume them *(As built: `UPSAutoPauseSubsystem` hears a suspend or a constrain on the bus and has every human's menu `PauseForInterruption`: the pause screen opens over whatever is up (a play-call screen comes back on resume), the front end is left alone, and a head-to-head session pauses by its etiquette without costing a pause. A resume leaves the game paused for the player. `UPSSaveSubsystem::FlushPendingWrites` finishes every async write before the handler returns.)*
- [x] Headless tests against the null implementation, plus a test that no gameplay module references a platform implementation directly *(As built: five `PlaySports.Platform.*` tests; `tools/lint_conventions.py` fails gameplay code (`Source/PlaySports` apart from the platform layer and tests) that names a `PSPlatformBackend*` or has an `#if PLATFORM_*`, with `tools/tests/test_lint_conventions.py`. `UPSSessionService::GetCurrentPlatform` lost its `#if` chain to the engine's platform name.)*

### Epic 153: Cross-Platform Parity and Release QA

**Size/Mode:** M / mixed
**Goal:** Every change is proven on all three platforms' builds, and the game plays the same everywhere.
**Depends on:** 145

- [ ] One build matrix in CI: the Win64 package (145), the Xbox compile (150 or 151) and the iOS package (149, once a Mac path exists), each a separate PR check
- [ ] `Specs/Release_Checklist.md`: a per-platform smoke runbook (install, boot, full game, suspend/resume, save/load, controller or touch) run by a human before each tagged build
- [ ] Same game everywhere: one data set and rule set on every platform, the determinism fingerprint from Epic 108's audit compared across Win64, iOS and Xbox builds, and saves portable between them
- [ ] A per-platform performance budget table (one row per tier) tracked in CI's perf history (Epic 114), with regressions failing the build

### Epic 154: iOS App Store Release (Later)

**Size/Mode:** M / mixed
**Goal:** The iOS build is ready for TestFlight and the App Store. **Deferred by the owner (2026-10-10):** the goal for now is playing on the owner's iPhone (149); this Epic waits until the owner chooses to start it.
**Depends on:** 149

- [ ] Owner gate (later): join the Apple Developer Program ($99/year), create the App Store Connect app record, and replace the placeholder bundle ID with the owner's own
- [ ] Distribution signing on the Mac runner, and a Shipping build uploaded to TestFlight for testers
- [ ] App Store technical requirements, checked against Apple's current rules at the time: app icons and launch screen, the privacy manifest and required-reason APIs, export compliance, supported devices and orientations
- [ ] Game Center (sign-in, achievements) through 152's iOS implementation
- [ ] Store submission: age rating, the listing (screenshots, description) and App Review against the current guidelines
