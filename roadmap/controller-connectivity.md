# Track M — Controller Connectivity (Epics 126–128)

The plumbing that puts a human on the field: Enhanced Input bring-up, a real player
controller, Xbox gamepad support, and human↔AI possession handoff. This track owns
*connectivity*; Track I's Epic 104 owns *feel* (the move vocabulary, buffering, and passing
model) and consumes what lands here. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** Input today is two legacy `BindAxis` calls
(`MoveForward`/`MoveRight`) on `APSPlayerPawn`; the `EnhancedInput` module is not in
`PlaySports.Build.cs`, and no project player controller class exists. Epic 126 creates the
substrate; Epic 104's first story was re-scoped to extend it rather than create it. Device
and possession state flow over the C1 `UPSTelemetryBus` — HUD/camera never cast to the
controller. Note the file-scope conflict recorded in `roadmap/PARALLEL.md`: Epic C3's
ball-action-component fast-follow touches `APSPlayerPawn` and must land before Epic 127.

### Epic 126: Enhanced Input Foundation & PSPlayerController

**Size/Mode:** M / code
**Goal:** Enhanced Input replaces legacy axis bindings behind a real player controller — the substrate every human-input epic builds on.
**Depends on:** Core 3

- [ ] Enable Enhanced Input: add `EnhancedInput` to `PlaySports.Build.cs`, enable the plugin in `play-sports.uproject`, set `UEnhancedPlayerInput`/`UEnhancedInputComponent` as the default input classes in `Config/DefaultInput.ini`
- [ ] `APSPlayerController`: project player controller registered in `APSGameMode`; owns mapping-context application on possession so `APSPlayerPawn` stays input-free (rule 1: new system = new class)
- [ ] `UPSInputConfig` code-defined data asset declaring the action catalog (Move, Sprint, Confirm, Cancel, SwitchPlayer) and context priorities — no magic bindings in gameplay code (rule 4)
- [ ] Migrate `APSPlayerPawn`'s legacy `BindAxis` MoveForward/MoveRight onto Enhanced Input actions bound in the controller; delete the legacy axis entries from `Config/DefaultInput.ini`
- [ ] Automation test: possessing a pawn applies the gameplay mapping context; an injected Move action value reaches the pawn's movement input path headlessly

### Epic 127: Xbox Gamepad Bring-Up & Human Possession

**Size/Mode:** M / code
**Goal:** A human on an Xbox controller actually controls one pawn on the field, with device awareness.
**Depends on:** 126 (and Epic C3's ball-action fast-follow — shared `APSPlayerPawn` scope, see `roadmap/PARALLEL.md`)

- [ ] Gamepad mapping context: left stick → Move, face buttons/triggers/bumpers → catalog actions; dead-zone and response curves via Enhanced Input modifiers with values from a tuning DataTable row (`FInputTuningRow`)
- [ ] Device detection: active-device tracking (gamepad vs keyboard) via `IPlatformInputDeviceMapper` connect/disconnect plus a last-input heuristic; device-change events published on `UPSTelemetryBus` (rule 5)
- [ ] Human possession flow: `APSPlayerController` takes control of a designated pawn (QB by default on offense) through a `UPSPossessionComponent`-aware handoff; the displaced `AIController` resumes on release; user-controlled flag queryable by HUD/camera
- [ ] Player-switch action (defense / post-turnover): switch control to the nearest eligible pawn to the ball, respecting single-authority possession rules (rule 6)
- [ ] Automation tests: device-change event round-trip on the bus; human↔AI possession handoff in both directions with no orphaned controllers

### Epic 128: Rumble, Glyphs & Feel Handoff

**Size/Mode:** S / code
**Goal:** Controller output (rumble), device-correct button glyphs, and the documented contract Epic 104 builds feel on.
**Depends on:** 127

- [ ] Force-feedback layer: a `UPSTelemetryBus` subscriber mapping gameplay events (tackle, catch, score, sack) to `PlayDynamicForceFeedback` patterns, with intensities in a tuning DataTable
- [ ] Glyph mapping table (JSON via the `UPSDataIngestion` pattern): action → per-device glyph ID (Xbox set first); consumed by Epic 5/101 HUD, Epic 103's remap surface, and Track N's touch layer
- [ ] `Specs/Input_Architecture.md`: context stack, action catalog, and extension points — the written contract Epic 104's move vocabulary/buffering and Epic 107's two-controller split build on
- [ ] Automation tests: telemetry event → force-feedback dispatch mapping; glyph table ingestion + validation
