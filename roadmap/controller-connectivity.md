# Track M — Controller Connectivity (Epics 126–128)

Bring-up plumbing for gamepads and enhanced input contexts. Installs EnhancedInput modules, exposes PSPlayerController, maps controls, handles device events, and manages possession handoffs between AI and human. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** Input today is legacy `BindAxis` MoveForward/MoveRight on `APSPlayerPawn` (PSPlayerPawn.cpp). `EnhancedInput` is NOT in `PlaySports.Build.cs` yet, and no custom PlayerController subclass exists. Track M owns the input plumbing/connectivity; Epic 104 (Track I) owns gameplay input feel and move vocabulary (jukes, placement passing).

### Epic 126: Enhanced Input Foundation & PSPlayerController

**Size/Mode:** M / code
**Goal:** Enhanced Input is integrated into the build, replacing legacy input bindings with a default Action mapping context.
**Depends on:** Core 3

- [ ] Add `EnhancedInput` dependency to `PlaySports.Build.cs` and verify module loading
- [ ] Create `APSPlayerController` (subclass of `APlayerController`) and configure in `APSGameMode` as default controller class
- [ ] Create `UPSInputConfig` (data asset wrapping input actions: Move, Sprint, Confirm, Cancel, SwitchPlayer)
- [ ] Create `IMC_Default` Input Mapping Context matching actions to keyboard defaults (WASD, Space, Shift, Tab)
- [ ] Bind actions in `APSPlayerPawn::SetupPlayerInputComponent` via `UEnhancedInputComponent`, removing legacy `BindAxis`/`BindAction`
- [ ] Automation test: context applied to `APSPlayerController` on possession, keyboard Move action resolves to pawn locomotion vectors

### Epic 127: Xbox Gamepad Bring-Up & Human Possession

**Size/Mode:** M / code
**Goal:** Xbox controllers are mapped via DataTables with possession handoff between human controller and AI.
**Depends on:** 126

- [ ] Create `IMC_Gamepad` mapping actions to Xbox thumbsticks and buttons (Left Stick -> Move, A -> Sprint, B -> SwitchPlayer)
- [ ] Add dead-zone and sensitivity tuning to gamepad mappings, loaded from a new `FInputTuningRow` DataTable structure
- [ ] Listen for controller connection events via `IPlatformInputDeviceMapper` and publish connect/disconnect events to `UPSTelemetryBus` (C1)
- [ ] Create `UPSPossessionComponent` on `APSPlayerPawn` to track possession state (Human vs AI controller) and execute clean controller swap
- [ ] Bind "SwitchPlayer" action to possess the defensive pawn nearest to the ball, returning the previous pawn to AI control
- [ ] Automation test: possess/release pawn dynamically shifts driver between `AIController` and `APSPlayerController` with zero movement interruption

### Epic 128: Rumble, Glyphs & Feel Handoff

**Size/Mode:** S / code
**Goal:** Force feedback and UI glyph mappings are exposed, establishing the contract for gameplay input depth.
**Depends on:** 127

- [ ] Implement telemetry-bus (C1) subscriptions mapping game events (tackle, catch, score, sack) to gamepad force feedback/rumble effects
- [ ] Create a gamepad glyph lookup table JSON in `Data/input_glyphs.json` mapping input actions to UI texture assets (Xbox A/B/X/Y, sticks)
- [ ] Create `Specs/Input_Architecture.md` defining the input event APIs, action mappings, and data flow to guide Epic 104's implementation
- [ ] Automation test: event bus notification for tackle triggers rumble intensity corresponding to momentum values
