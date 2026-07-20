# Track N — Platform Ports (Epics 129–131)

High-level planning, scalability setups, touch input abstraction, and build pipelines for mobile and alternative platforms, with iOS as the primary target. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** The codebase is currently configured for Win64 development and testing only. Packaging and code verification for iOS requires a Mac, which is not present in the local CI runner environment. touch input should translate to the abstract input actions defined in Track M (Enhanced Input) to ensure no gameplay forks.

### Epic 129: Target Platform Audit & Scalability Tiers

**Size/Mode:** M / code
**Goal:** Platform constraints are documented, and device profile scalability overrides are defined for mobile.
**Depends on:** —

- [ ] Create `Specs/Platform_Audit.md` detailing iOS rendering capabilities (Metal, forward shading vs deferred, memory budgets, thermal considerations, and CPU bottlenecks of 22-character physics)
- [ ] Configure scalability tiers in `Config/DefaultDeviceProfiles.ini` for iOS profiles (resolution scaling, mobile post-processing, static lighting overrides)
- [ ] Add runtime target-platform preprocessor checks (`PLATFORM_IOS`) around high-overhead physics and particle systems
- [ ] Implement a headless tier-resolution smoke test asserting that the correct scalability configuration loads based on mock platform identifiers

### Epic 130: Touch Input Abstraction

**Size/Mode:** M / code
**Goal:** Touch gestures and virtual joysticks are mapped to the core Enhanced Input actions.
**Depends on:** 126, 128, 129

- [ ] Create `Specs/Touch_Controls_Spec.md` defining touch layout regions (left-thumb virtual stick, right-thumb action buttons/gestures)
- [ ] Create virtual stick mappings in Enhanced Input that resolve to the standard `IA_Move` input action
- [ ] Bind swipe and tap gestures to player action configurations (Confirm, Cancel, Sprint, SwitchPlayer)
- [ ] Create touch glyph visuals in `Data/touch_glyphs.json` and extend the device change listener to toggle UI graphics when touch input is active
- [ ] Automation test: simulated touch events on virtual stick resolve to identical locomotion action values as gamepad inputs

### Epic 131: iOS Build Pipeline & Signing

**Size/Mode:** M / mixed
**Goal:** iOS packaging settings are configured, and compile/signing procedures are scripted.
**Depends on:** 129

- [ ] Create `Specs/ADR_iOS_Build.md` detailing the remote-Mac compile options, local stub packaging, or cloud Mac runner integration patterns
- [ ] Configure iOS packaging settings in `play-sports.uproject` and `Config/DefaultEngine.ini` (bundle identifier, supported orientations, splash/icon asset slots)
- [ ] Write a provisioning and signing script runbook for human developers packaging via the Unreal Editor
- [ ] Add a stubbed iOS package command job in `.github/workflows/ci.yml` gated on a `mac` runner label for future runner support
