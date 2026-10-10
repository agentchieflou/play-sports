# Track B — Camera & Cinematics (Epics 38–45)

Deepens core Epic 4's basic broadcast camera into a full presentation layer: an auto-directing
camera brain, physical rig simulations, replay, auto-highlights, and analysis tooling.
Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review, updated 2026-10-10):** `APSBroadcastCamera` (follow/bounds/
framing/free-cam from Epic 4) is wired: Phase 1.5 C3 made `SetTargetActor` the way in, and the
game mode points `TargetActor` at the receiver on each catch on the bus. Since Epic 38 the camera
director picks its own subject from Epic 26's snapshots, so the plain follow of `TargetActor` is
the fallback while the director is off. Epic 41's ring buffer is C1's event history (don't build a
second one). Architecture rules apply: new camera behaviors are components/classes, each epic
ships tests.

### Epic 38: Camera Director AI

**Size/Mode:** L / code
**Goal:** An automated director cuts between camera rigs based on play context — no manual camera work needed to watch a full game.
**Depends on:** Core 4, 26

- [x] Shot vocabulary: LOS wide, all-22 high, tight follow, end-zone, sideline reaction *(`UPSCameraDirectorComponent` on `APSBroadcastCamera`, shots in `Data/camera_director.json`. All-22 high and end zone are Epic 40's rigs, framed by `UPSCameraFraming`. The other three stand their distance toward the camera side of their target: the ball for LOS wide, and the live subject for tight follow (led along his run) and sideline reaction (low and close))*
- [x] Cut rules driven by phase/events (pre-snap wide → snap follow → post-play tight) *(each `CutRules` row maps a bus trigger to a shot: `PhaseChange` to PreSnap → LOS wide, `Snap` → tight follow, `Throw` → all-22 high, `Catch` → follow, `Fumble` → end zone, `Tackle`/`Score`/the whistle → sideline reaction. The director is the broadcast camera's normal presentation; the film view (Epic 40) and the free cam override it, and with `bDirectorEnabled` off the plain follow returns)*
- [x] Interest scoring (ball, big hits, breakaways) to pick the live subject *(read from Epic 26's latest snapshot plus the bus's tackles and hits. A player's interest is the sum of weights for holding the ball, closeness to it, a breakaway (fast, nobody within the clearance) and a big hit fading over `BigHitSeconds`. A new subject must beat the current one by `SwitchMargin`)*
- [x] Smoothing/constraint layer so cuts never disorient (180° rule, minimum shot length) *(no shot is cut away from before `MinShotSeconds`: an earlier ask waits and the latest wins, and an ask for the live shot does nothing. Every shot stays on `CameraSide` of the line of action through the ball: one across it is mirrored back and re-aimed, while end-zone angles on the line are allowed. Cuts are instant; within a shot the camera eases at `FollowInterpSpeed`. Tests: `PlaySports.Camera.Director*`)*

### Epic 39: Skycam / Cable-Cam Rig Simulation

**Size/Mode:** M / code
**Goal:** A physically plausible suspended camera flies behind the offense — the modern broadcast signature angle.
**Depends on:** 38

- [x] Catenary-constrained rig: camera moves within a simulated cable envelope above the field *(`UPSCameraSkycamComponent` on `APSBroadcastCamera`, rig in `Data/camera_skycam.json`. The camera stays inside the four towers' rectangle, above its floor and below the cables. The cables hang in catenaries, so the ceiling is the anchor height less both cable families' sag: highest by the towers, lowest over midfield)*
- [x] Follow behaviors (behind-QB pre-snap, chase on breakaways) with mass/lag for realism *(it parks behind the quarterback looking downfield until the snap, then chases the ball carrier from behind along his run (`Snap`/`PhaseChange` on the bus, field from Epic 26's snapshots). It flies as a critically damped spring within the winches' speed and acceleration, so it lags and settles rather than jumping)*
- [x] Handoff integration so the director (38) can cut to/from it *(the director's vocabulary gains `Skycam` and its triggers `Breakaway`, which fires when the live subject breaks into the clear; the rule cuts to the skycam. The rig flies every tick on air or not, so it is in position when cut to; while it is live, the camera is the rig's own shot, exempt from the 180° rule as it flies over the line of action. Tests: `PlaySports.Camera.Skycam*`)*

### Epic 40: All-22 Coaches Film Camera

**Size/Mode:** S / code
**Goal:** A locked high wide angle showing all 22 players, recordable for analysis.
**Depends on:** Core 4

- [x] Fixed elevated sideline and end-zone all-22 rigs *(rows in `Data/camera_all22.json`: height, standoff, rail and zoom range per rig. `UPSCameraFraming` aims at the players' padded bounding box and zooms to the narrowest angle that holds its eight corners, backing the rig away along its line of sight when even the widest zoom can't. The sideline rig sits on the broadcast camera's side (180° rule); the end-zone rig stands behind the offense, whose direction is read from the formation at each cut and each snap)*
- [x] Toggle path from normal presentation into film view *(`UPSCameraAll22Component` on `APSBroadcastCamera`, so film is a mode of the one game camera, not a second camera. A new catalog action, `FilmView` (R3 / F, in `OnField`), steps broadcast → sideline → end zone → broadcast. It is not on Y, which is the tempo, a pass and a move button on the field: a toggle there would cut the camera on a Y pressed around the snap, and the input buffer (104.4) could no longer carry that press into the throw. The camera hears it from whichever `APSPlayerController` views through it (`BecomeViewTarget`). Leaving film view restores the broadcast view. Film view has no camera effects and turns motion blur off, so the per-tier cut-line in `Specs/Platform_Audit.md` doesn't apply)*
- [x] Frame export hook for the analysis/telestrator Epic (44) *(`CaptureFrame` records the live shot and each player's normalized place in it (`FPSFilmFrame`) and fires `OnFrameCaptured`; `ExportFrame` writes the frame as JSON to `Saved/Film` and requests a still when a game viewport exists. Frames carry bus time, so they line up with the event history and replays. Tests: `PlaySports.Camera.All22*`)*

### Epic 41: Replay System

**Size/Mode:** XL / code
**Goal:** Any recent play can be re-rendered from any camera with scrubbing and slow motion.
**Depends on:** C1, 26, 38, Core 17 (determinism hooks)

- [x] Replay recording joins C1's event ring buffer with Epic 26's snapshot history (no third buffer) *(as built: `UPSReplaySubsystem::CaptureClip` / `CaptureLastPlay` cut a clip when one is wanted. It takes the sampler's frames from `PreRollSeconds` before the snap to `PostRollSeconds` after the whistle, and the bus's events in that span, timed and ticked on those frames. The clip is Epic 115's `FPSReplayRecording`, which gains `Frames`. The replay keeps no buffer of its own: a clip is a one-off copy, so the history rolls on while it plays. It opens on the last `GameState` announced before the snap (`PSGameStateEvents::ToPlayState`). Tuning: `Data/replay.json`)*
- [x] Deterministic re-simulation or state-playback of the buffered play *(as built: state playback, since the physical game isn't re-simulable (`Specs/Determinism_Audit.md`). At the playhead, the frame blended from the clip's frames either side poses every pawn (by `PlayerId`) and the ball, with their collision off.
  - The sampler shows that frame as the present (`SetReplayFrame`), so the director, the skycam and the overlays follow the replay and record none of it.
  - The game is paused under the replay, and re-paused if something else unpauses it. The broadcast camera ticks through the pause.
  - `StopReplay` puts every pawn and the ball back, with their velocity and collision.
  - The tier's `ReplayPoseRateHz` (`Data/platform_tiers.json`) caps how often the field is re-posed.
  - Tests: `PlaySports.Replay.System.*`.
  - Not seen yet: the replay on screen, and whether the players' animation runs while the game is paused. That is an editor check.)*
- [x] Scrub/pause/slow-mo/frame-step transport controls *(as built: pause, slow motion through `PlaybackRates`, steps between captured frames, held scrubbing at `ScrubSecondsPerSecond`, and the playhead's limits. The buttons are a new `Replay` input context (priority 4) that the replay pushes on every player controller (`APSPlayerController::SetModeContextActive`): A play/pause, X slow motion, D-pad left/right frame steps, LB/RB scrub, Y camera, B exit, with keys, glyphs, text rows and touch twins. Pause still opens the pause menu, and the replay holds while it is open. Tests: `PlaySports.Replay.System.TransportControls`, `PlaySports.Replay.Controls.*`)*
- [x] Free camera + all rig cameras available inside replay *(as built: Y steps through `Cameras` in `Data/replay.json`. The order is the camera director (which follows the replay through the sampler's replay frame), the all-22 sideline and end-zone rigs, the skycam (cut to at once with the director's new `CutNow`), and a free camera that circles the ball and closes in on the Move stick. The broadcast camera ticks through the pause. When the replay ends it gets back its transform, field of view, film view and director state. Not seen yet: how the angles look on screen)*
- [x] Auto-replay trigger after scores/turnovers with director-chosen angle *(as built: the replay system watches the bus. A score is the game state's score going up (the play simulation is the authority) or a `Score` event. A turnover is an interception, a lost fumble, or the ball changing hands on a play that wasn't a kick. Either one replays the play `AutoReplayDelaySeconds` after its whistle, at its rule's speed, opening on its rule's director shot: the end-zone rig for a score, the skycam for a turnover. `AutoReplayHoldSeconds` after the clip's end the replay gives the game back, unless the viewer took the controls. The next snap cancels a replay that hasn't started. With Reduced motion on (Epic 103.5), every replay opens on `ReducedMotionCamera`, the still sideline rig)*
- [x] Persistence: save a play's replay data to disk for later viewing *(as built: `SaveClip` writes a clip as Epic 115's JSON to `Saved/Replays`. Its scheduled frames are thinned to `SaveFrameRateHz`, and its keyframes, first frame and last frame are kept. A snapshot's live pawn is `Transient` and isn't written; a loaded clip finds its pawns by `PlayerId`. `LoadClip` goes through the format's version gate. `ListSavedClips` lists what is saved)*

### Epic 42: Auto-Highlight Generation

**Size/Mode:** L / code
**Goal:** The game detects its own big moments and assembles a highlight reel per game.
**Depends on:** 41

- [x] Play-importance scoring (yardage, score change, turnover, broken tackles, win probability swing) *(as built: `UPSHighlightSubsystem` follows every play on the bus from its snap until it settles: the first game state after the whistle, `SettleAfterWhistleSeconds` of running game after it, or the next snap. It reads five things.
  - Yards: the longest tackle or catch, else the yard line's move.
  - Points: the game state's score, which is the authority, or `Score` events.
  - Turnovers: an interception, a lost fumble, or the ball changing hands on a play that wasn't a kick.
  - Broken tackles: a hit the carrier survives (Epic 139's hitpoints).
  - The swing in the home team's chance of winning: `HomeWinProbability`, a logistic of the margin plus the ball's worth where it is, sharpened as time runs out.
  Each is weighed by `Data/highlights.json`)*
- [x] Clip assembly: top-N plays with director-selected angles and slow-mo beats *(as built: a play of at least `MinImportance`, among the `ReelSize` best so far, is cut into a clip as it settles (Epic 41's `CaptureClip`); only the reel keeps clips. Each kind has its director shot: end zone for a score, skycam for a turnover, tight follow for a big play. The slow-motion beat starts `BeatLeadSeconds` before the play's key moment (the score, the turnover, the catch, the broken tackle, the tackle, by that rank) and plays `BeatSeconds` at `BeatPlaybackRate`)*
- [x] End-of-game highlight package playback *(as built: `PlayReel` plays the reel in game order through the replay system, each clip from its angle (`UPSReplaySubsystem::CutToDirectorShot`), slowing through its beat, with `ClipGapSeconds` between clips. A game state past the fourth quarter starts it by itself `GameEndReelDelaySeconds` later. With Reduced motion on it opens on the still rig like every replay, and the viewer leaving the replay ends it. Tests: `PlaySports.Highlights.*`. Not seen yet: the package on screen)*
- [x] Franchise hook: highlights persist per season (Track G consumes) *(as built: `ArchiveForSeason` saves the reel's clips (under `Saved/Replays/Highlights`) and adds the game's highlights to `UPSFranchiseSaveGame::SeasonHighlights` (`FPSSeasonHighlight`: week, teams, kind, importance, situation, clip file). It keeps the season's `SeasonHighlightsKept` most important and deletes the clips of those it drops. Calling it when a franchise game ends, and showing the season's highlights, is Track G's)*

### Epic 43: Cinematic Play Framing Sequences

**Size/Mode:** L / editor
**Goal:** Pre/post-play presentation moments — huddle break, lineup walk, celebrations, dejection — staged as short cinematics.
**Depends on:** 38, Core 22

- [ ] Sequence catalog and trigger matrix (score, turnover, game-winner, injury)
- [ ] Huddle-break and pre-snap approach staging
- [ ] Celebration staging with camera coverage (mixed: staging code, animation content in editor)
- [ ] Skip/auto-skip rules so cinematics never slow repeat viewers

### Epic 44: Telestrator & Analysis Mode

**Size/Mode:** M / code
**Goal:** Draw-on-screen analysis (circles, arrows, freehand) over paused replay or film view.
**Depends on:** 40, 41

- [x] Draw layer (freehand, arrow, circle, highlight-player via Epic 36) over paused frames *(`UPSTelestratorSubsystem` draws on a held replay (Epic 41, paused for the drawing and played on after) or the film view (Epic 40), on the frame from `UPSCameraAll22Component::CaptureFrame`. Freehand, arrow and circle strokes keep their screen points and are pinned to the field through the frame's shot (`UPSCameraFraming::DeprojectToField`, new). A player tap picks the nearest player and lights him up through `UPSOverlayEmphasisSubsystem`. Undo, clear and a mark limit; tuning in `Data/telestrator.json`. The way in is the new `Telestrator` action (Y; D-pad Up, or a right-stick flick up before the snap; touch D-pad Up or a swipe up), bound for whoever looks through the broadcast camera, as photo mode is. The drawing layer is `UPSTelestratorWidget`, made by `APSHUD`. While analysis is on it takes the input (UI input mode; the touch layer stands down) and shows a toolbar: the four tools, undo, clear, save, done. A mouse drag or a finger becomes `BeginStroke`/`ExtendStroke`/`EndStroke`. A gamepad or the keys move a cursor (`TelestratorCursor`), and A or Space draws at it. The layer's buttons are the new `Telestrator` input context, read through Slate like `Menu`'s (`RunLayerAction`). It paints the marks and the stroke in progress with Slate lines (`PSTelestratorLayer`, `PSWidgetDrawing`), sized from the screen's shorter side so a phone shows the same drawing; the look is in `telestrator.json`. Tests: `PlaySports.Telestrator.*` (`LayerGeometry`, `LayerCursor`, `AnalysisMode`, `LayerTuning` new). Not seen on a screen yet: the PIE and iPhone check of the layer, its toolbar and the cursor, `Specs/HUD_Spec.md` (Telestrator Drawing Layer))*
- [x] Save/share annotated stills to disk *(`ExportStill` writes the frame, its marks and notes as JSON (`Saved/Telestrator` by default), read back by `LoadStill`. With a game viewport it also requests a screenshot with the UI, so the drawing layer is in it; the frame's `ImageFile` names it. Sharing is the file on disk. The screenshot is not exercised headless)*
- [x] LLM hook (bridge-gated per Epic 25): auto-annotate a play with coaching notes *(`RequestAutoAnnotation` hands the frame, the replay clip's events and its situation to a bound listener, or keeps it for an agent that polls `GetPendingAnnotationRequests` through AgenticLink's `call_function` once `SetAutoAnnotationBridgeOnline` is on. With neither it is refused. `SubmitAutoAnnotation` takes notes and marks in field terms, put on the screen through the frame's shot. An answer for a frame no longer drawn on is refused. No live model has answered one yet)*

### Epic 45: Photo Mode

**Size/Mode:** S / code
**Goal:** Free-camera still capture with framing aids and filters.
**Depends on:** 41

- [x] Pause-anywhere free cam with FOV/DOF/roll controls *(as built: `UPSPhotoModeSubsystem`. The `PhotoMode` button (B; View, or a right-stick flick down before the snap, where View is the timeout; touch View or a swipe down) enters it from the field or a replay. It pauses the game, holds a replay (`UPSReplaySubsystem::SetHeld`) and flies the broadcast camera free. Controls: the Move stick flies it level; turn buttons on the right stick step and then turn; bumpers raise and lower; triggers zoom; the D-pad rolls and focuses; X steps the apertures (depth of field). It stays on a leash from where it started and above the turf. A new `PhotoMode` input context (priority 5) takes `Replay` off while on. Leaving puts the camera, its view and post-process settings, the replay and the pause back. Tuning: `Data/photo_mode.json`. Tests: `PlaySports.PhotoMode.*`. Not seen yet: how the depth of field and roll look on screen. That is an editor check)*
- [x] Filter/preset stack and UI-hide toggle *(filters (saturation, contrast, tint, white balance, vignette) stack into one grade (`ComposeLook`), laid on the camera's post-process settings. View steps the presets (named stacks); `PushFilter`/`PopFilter` and a strength are there for a UI. Y hides every viewport widget, the HUD and the on-field overlays (reticle, ball-flight arc), and shows exactly those again. Framing guides (thirds, centre) are lines for a widget. Not built: that widget, which draws the guides and the controls)*
- [x] High-res screenshot export *(A asks the engine for a high-resolution screenshot, `FHighResScreenshotConfig`, of the camera's view: the viewport times `CaptureResolutionMultiplier`, at most `MaxCaptureDimension`, into the screenshots folder's `Photo`. Headless runs have no viewport, so only the request's size and name are tested; a real capture is an editor check)*
