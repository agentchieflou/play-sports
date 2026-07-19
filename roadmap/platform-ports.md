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

- [ ] `Specs/Platform_Audit.md`: Win64 baseline vs iOS/Metal constraints — mobile renderer choice (forward shading), memory/thermal budgets, 22-pawn physics cost on mobile CPU, feature cut-lines per tier (crowd, overlays, resolution)
- [ ] Device profiles: `Config/DefaultDeviceProfiles.ini` tiers (DesktopHigh / MobileBaseline / MobileLow) mapped to UE scalability groups
- [ ] Scalability hooks retrofit: audit systems with per-tier cost (crowd density, overlay complexity, camera effects) and route them through a tier flag instead of hardcoding — data-driven per rule 4
- [ ] Headless smoke test: tier config resolves and tier-reading systems return per-tier values

### Epic 130: Touch Input Abstraction

**Size/Mode:** M / code
**Goal:** Touch drives the same action layer as the gamepad — no fork in gameplay input.
**Depends on:** 126, 128, 129

- [ ] Touch layer mapping onto the `UPSInputConfig` action catalog: virtual stick + tap/swipe gestures resolve to the same Move/Confirm/etc. actions via Enhanced Input
- [ ] `Specs/Touch_Controls_Spec.md`: on-screen layout, HUD-safe zones, per-context button sets (pre-snap vs ball-carrier) — the editor/visual half handed off per the Specs pattern
- [ ] Glyph table (128) gains a touch glyph set; active-device events (127) drive automatic UI glyph switching
- [ ] Automation test: injected touch gestures resolve to action values identical to their gamepad equivalents

### Epic 131: iOS Build Pipeline & Signing

**Size/Mode:** M / mixed
**Goal:** A documented, reproducible path from this repo to a signed iOS build — decisions and runbooks, not yet a shipping port.
**Depends on:** 129

- [ ] `Specs/ADR_iOS_Build.md`: Mac-in-the-loop strategy — remote-Mac build from the Windows runner vs cloud Mac CI vs manual packaging; pick one and document tradeoffs (pattern: `Specs/ADR_CI_Environment.md`)
- [ ] iOS target settings in platform configs (bundle ID, orientation, min OS version, Metal) — compile-safe on Win64, validated by existing CI
- [ ] Signing/provisioning runbook for the human operator (certificates, profiles, TestFlight) — agents cannot sign; explicit human-mode deliverable
- [ ] CI packaging job stub: workflow gated on a `mac` runner label, documenting the exact `RunUAT BuildCookRun` invocation for when a Mac runner exists
