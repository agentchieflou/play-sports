# Track S — Three-Platform Release (Epics 145–153)

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
- **iOS:** UE packages and signs iOS only on a Mac (`Specs/ADR_iOS_Build.md`, Proposed;
  `Specs/iOS_Signing_Runbook.md`). A free Apple ID sideloads 7-day builds. TestFlight needs the
  Apple Developer Program.
- **Xbox Series X|S:** from UE 5.8 the Microsoft GDK plug-ins and Xbox platform extensions ship
  publicly with the engine, with no gated download. Building for and deploying to a console still
  needs Xbox developer onboarding through ID@Xbox (NDA, concept approval, Partner Center
  registration), the GDK on a dev PC, and console hardware enabled for development. Sources:
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

- [ ] CI packaging job: `RunUAT BuildCookRun` for Win64 Development (and Shipping on demand) on the self-hosted runner, on pushes to main and by manual dispatch, with the packaged build uploaded as an artifact
- [ ] Data staging audit: every loader that reads `Data/*.json` finds it in a packaged build (`Data/` staged as non-UFS or cooked into a known path); a test lists every default data path and asserts it resolves under the packaged layout
- [ ] Packaged-build smoke test: the packaged executable runs headless (`-nullrhi`), plays a scripted full game (`PlaySports.Gym.ScriptedFullGame`'s path) and exits with the final score; CI fails if it doesn't
- [ ] Boot flow: a packaged build opens the front end (Epic 101), and Play Now reaches the field with no editor-only dependency (no `WITH_EDITOR` path on the way)

### Epic 146: Content as Code — Headless Editor Content Pipeline

**Size/Mode:** L / mixed
**Goal:** The content the specs describe is built by scripts that drive the editor headlessly on the CI runner, so "editor" stories become reviewable code.
**Depends on:** 112

- [ ] Decision record (`Specs/ADR_Content_Pipeline.md`): generated `.uasset` content committed through Git LFS vs regenerated in CI on every run; how the owner's hand edits coexist with generated assets (owner decides)
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
- [ ] Distribution (owner decides): a zip or installer for the owner and testers, itch.io, or Steam (the Steamworks app fee and its integration become their own Epic)

### Epic 149: iOS Device Bring-Up

**Size/Mode:** L / mixed
**Goal:** The same game, signed and running on the owner's iPhone 17 Pro, played with touch at a steady frame rate.
**Depends on:** 131, 145, 147

- [ ] Owner gates: accept a Mac strategy in `Specs/ADR_iOS_Build.md` (131.1), and pick the Apple account (free Apple ID for 7-day sideloads, or the Developer Program for TestFlight)
- [ ] Signed Development build installed on the iPhone through the chosen Mac path; `.github/workflows/ios-package.yml` made real on a `mac` runner or a documented manual route
- [ ] Touch HUD assembled (`Specs/Touch_Controls_Spec.md` section 6, from 146) with safe areas and a landscape lock; every screen usable by touch
- [ ] Mobile rendering and performance: the content set validated on the mobile renderer, 60 fps at `MobileBaseline` measured on the device with Epic 114's harness (closes 17.5 for iOS), and a 30-minute thermal session without throttling below 30 fps
- [ ] App lifecycle: backgrounding mid-play pauses the game and saves through 152; audio interruptions and resume behave
- [ ] TestFlight build (needs the Developer Program)

### Epic 150: Xbox Access and Platform Enablement

**Size/Mode:** M / mixed
**Goal:** The project builds for Xbox Series X|S in CI, with the owner's developer access and hardware in place.
**Depends on:** 112, 129

- [ ] Owner gates: ID@Xbox onboarding (NDA, concept approval, Partner Center registration) and console hardware enabled for GDK development (a dev kit, or whatever Microsoft provisions for the account), recorded in `Specs/ADR_Xbox_Access.md`, including whether compiling the console target needs the onboarding too or only deploying does
- [ ] The Microsoft GDK installed on the Windows runner; UE 5.8's public GDK plug-ins and the Xbox Series platform enabled in the project, without disturbing the Win64 build
- [ ] An `XboxSeries` tier in `Data/platform_tiers.json` and a matching device profile, with Series S as the performance floor, read through the existing tier system (no hardcoded console checks)
- [ ] CI compiles the Xbox Series target on the Windows runner on every PR (no console needed to compile), reported next to the Win64 checks

### Epic 151: Xbox Device Bring-Up and Certification Readiness

**Size/Mode:** L / mixed
**Goal:** The same game plays a full game on an Xbox Series console with a controller, and behaves the way Xbox requires of a title.
**Depends on:** 150, 147, 152

- [ ] Deploy and play: a packaged build on the console plays a full game with an Xbox controller at 60 fps on Series S (the floor) and Series X
- [ ] User and controller handling: sign-in and account picker at boot, controller disconnect pausing the game and reassignment, a second user for local versus (Epic 107)
- [ ] Lifecycle: suspend/resume and Quick Resume mid-game, constrained mode, and saves through the GDK's storage via 152
- [ ] Presentation requirements: title-safe UI area, the platform's button glyphs and terminology (Epic 128's Xbox set), and boot within the platform's time limits
- [ ] Store readiness (owner decides when): age rating (IARC), store listing assets and a pre-certification pass against the current Xbox Requirements

### Epic 152: Platform Services Layer

**Size/Mode:** M / code
**Goal:** One interface gives the game user identity, storage, achievements/presence and lifecycle events, with one implementation per platform, so gameplay code never branches on platform.
**Depends on:** 115, 117

- [ ] `UPSPlatformServices` (game-instance subsystem): the signed-in user, the save storage root, achievements/presence and lifecycle events (suspend, resume, constrained) published on the bus. Implementations are null/local (Win64 and tests), iOS (Game Center) and Xbox (the GDK through UE's online services), chosen by platform config
- [ ] Epic 115's save system stores through it unchanged above that layer; saves written on one platform load on another
- [ ] Lifecycle events pause a live game and flush saves; 149 and 151 consume them
- [ ] Headless tests against the null implementation, plus a test that no gameplay module references a platform implementation directly

### Epic 153: Cross-Platform Parity and Release QA

**Size/Mode:** M / mixed
**Goal:** Every change is proven on all three platforms' builds, and the game plays the same everywhere.
**Depends on:** 145

- [ ] One build matrix in CI: the Win64 package (145), the Xbox compile (150) and the iOS package (149, once a Mac path exists), each a separate PR check
- [ ] `Specs/Release_Checklist.md`: a per-platform smoke runbook (install, boot, full game, suspend/resume, save/load, controller or touch) run by a human before each tagged build
- [ ] Same game everywhere: one data set and rule set on every platform, the determinism fingerprint from Epic 108's audit compared across Win64, iOS and Xbox builds, and saves portable between them
- [ ] A per-platform performance budget table (one row per tier) tracked in CI's perf history (Epic 114), with regressions failing the build
