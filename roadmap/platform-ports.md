# Track N — Platform Ports (Epics 129–131)

Getting the game onto platforms beyond Win64, iOS first. Deliberately high-level at this
stage: decisions, scalability tiers, and input abstractions now; the heavy port labor comes
after the core game exists. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** Everything to date is Win64-only with desktop
assumptions (deferred shading defaults, unbounded overlay/crowd budgets). UE iOS packaging
requires a Mac in the loop — the self-hosted CI runner is Windows
(`Specs/ADR_CI_Environment.md`), so Epic 131's ADR must pick a Mac strategy before any
packaging story is real. All touch input consumes Track M's action layer (`UPSInputConfig`)
— a forked touch-only gameplay input path is a review-rejection.

### Epic 129: Target Platform Audit & Scalability Tiers

**Size/Mode:** M / code
**Goal:** A written audit and a working scalability-tier config so every later system knows its mobile budget.
**Depends on:** —

- [x] `Specs/Platform_Audit.md`: Win64 baseline vs iOS/Metal constraints — mobile renderer choice (forward shading), memory/thermal budgets, 22-pawn physics cost on mobile CPU, feature cut-lines per tier (crowd, overlays, resolution) *(reference device: iPhone 17 Pro; measurements wait on a Mac build, section 5)*
- [x] Device profiles: `Config/DefaultDeviceProfiles.ini` tiers (DesktopHigh / MobileBaseline / MobileLow) mapped to UE scalability groups *(MobileBaseline on the engine's `IOS` profile so every iPhone inherits it; `PSMobileLow` for phones that need less; DesktopHigh keeps the engine's Windows defaults)*
- [x] Scalability hooks retrofit: audit systems with per-tier cost (crowd density, overlay complexity, camera effects) and route them through a tier flag instead of hardcoding — data-driven per rule 4 *(today's only per-frame cost centre is the AI: it decides at `AIDecisionInterval` from `Data/platform_tiers.json` via `PSPlatformTiers::GetActiveTier()`, steering every frame in between. Crowd, overlays, weather and camera effects don't exist yet; their per-tier cut-lines are in the audit, section 4)*
- [x] Headless smoke test: tier config resolves and tier-reading systems return per-tier values *(`PlaySports.Platform.*`)*

### Epic 130: Touch Input Abstraction

**Size/Mode:** M / code
**Goal:** Touch drives the same action layer as the gamepad — no fork in gameplay input.
**Depends on:** 126, 128, 129

- [x] Touch layer mapping onto the `UPSInputConfig` action catalog: virtual stick + tap/swipe gestures resolve to the same Move/Confirm/etc. actions via Enhanced Input *(`UPSTouchInputComponent` on the controller: a floating stick, on-screen buttons and swipes from `Data/touch_controls.json`, each naming a catalog action per context and stacking by context priority; values go through the action's gamepad mapping and `APSPlayerController::InjectCatalogInput` into Enhanced Input. Runs on a device only once an iOS build exists)*
- [x] `Specs/Touch_Controls_Spec.md`: on-screen layout, HUD-safe zones, per-context button sets (pre-snap vs ball-carrier) — the editor/visual half handed off per the Specs pattern *(the touch HUD widget that draws the controls is section 6's handoff, not built)*
- [x] Glyph table (128) gains a touch glyph set; active-device events (127) drive automatic UI glyph switching *(`EPSInputDevice::Touch`; a default `Touch` set of action glyphs; a finger switches the device on the bus, and a phone starts on, and falls back to, Touch. No HUD prompt consumes glyphs yet; the test shows a prompt's glyph following the event)*
- [x] Automation test: injected touch gestures resolve to action values identical to their gamepad equivalents *(`PlaySports.Input.TouchGesturesMatchGamepad`, `PlaySports.Input.TouchLayoutValidates`)*

### Epic 131: iOS Build Pipeline & Signing

**Size/Mode:** M / mixed
**Goal:** A documented, reproducible path from this repo to a signed iOS build — decisions and runbooks, not yet a shipping port.
**Depends on:** 129

- [ ] `Specs/ADR_iOS_Build.md`: Mac-in-the-loop strategy — remote-Mac build from the Windows runner vs cloud Mac CI vs manual packaging; pick one and document tradeoffs (pattern: `Specs/ADR_CI_Environment.md`) *(written as **Proposed**: four options weighed, recommends a Mac as a second self-hosted runner after one manual build on it; ticks when the owner answers the ADR's closing question about a Mac and an Apple account)*
- [x] iOS target settings in platform configs (bundle ID, orientation, min OS version, Metal) — compile-safe on Win64, validated by existing CI *(`[/Script/IOSRuntimeSettings.IOSRuntimeSettings]` in `Config/DefaultEngine.ini`: placeholder bundle ID `com.example.playsports`, iPhone only, landscape only, Metal with the mobile renderer; the minimum iOS version stays at the engine default, its intended line commented. Effect unverified until a Mac build)*
- [x] Signing/provisioning runbook for the human operator (certificates, profiles, TestFlight) — agents cannot sign; explicit human-mode deliverable *(`Specs/iOS_Signing_Runbook.md`: free Apple ID 7-day path, Developer Program TestFlight path, Mac-runner setup, failure table. Written, not yet run: running it is the owner's)*
- [x] CI packaging job stub: workflow gated on a `mac` runner label, documenting the exact `RunUAT BuildCookRun` invocation for when a Mac runner exists *(`.github/workflows/ios-package.yml`: manual dispatch only, labels `self-hosted, macOS, mac`, skipped unless the repo variable `IOS_MAC_RUNNER` is `true`; never run yet)*
