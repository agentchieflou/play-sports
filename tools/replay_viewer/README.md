# Film Room: the 3D replay viewer

A static web page that plays recordings of real plays from the simulation back in 3D: the field
from `Data/field_dimensions.json`, all 22 players on the CC0 stand-in character
(`RawAssets/world/people/standin.glb`) with a procedural run cycle, and the ball. Trails, velocity
vectors, contacts and the bus's moments (snap, throw, catch, tackle, whistle) show the physics.
It is built for a phone first (touch orbit, pinch zoom, tap a player to follow him) and works on
a desktop. No build step: three.js r170 (MIT) loads from cdnjs, its addons (GLTFLoader,
OrbitControls, SkeletonUtils, Line2) from jsDelivr at the same version, because cdnjs doesn't host
the addons.

Nothing here runs at game time or in the engine. It reads the files lane S4's demo run writes.

## Point it at real recordings

The demos come from CI as the `play-demos` artifact: one replay JSON per play
(`UPSReplayFormat::SerializeToJson`) and an `index.json` listing them.

```
gh run download <run-id> -R agentchieflou/play-sports -n play-demos -D Saved/PlayDemosDownload
python tools/replay_viewer/stage.py --recordings Saved/PlayDemosDownload --out Saved/FilmRoom --print-files
```

`--recordings` is the folder that holds `index.json`; if the artifact nests it, point at the
nested folder. Staging checks every listed recording against `replay_schema.json` and stops on one
the viewer can't play; it refuses any `SYNTHETIC_*` file, so the synthetic sample can never be
staged as the game. The staged folder is the whole page:

```
Saved/FilmRoom/
  index.html                 the page (no doctype: the Artifact host adds the skeleton)
  app.js loader.js gait.js field.js replay_schema.json
  data/field_dimensions.json data/sample_teams.json     copies of Data/
  assets/standin.glb assets/LICENSE                     RawAssets/world/people/ (CC0)
  recordings/index.json                                 the index, as downloaded
  recordings/<play>.json ...                            each play it lists
```

To publish it as an Artifact, publish `index.html` with `files` set to the map `--print-files`
prints (published path to staged file). To serve it anywhere else, stage with `--standalone` (the
page then carries its own doctype and phone viewport) and serve the folder, for example
`python -m http.server -d Saved/FilmRoom 8000`.

During development, serve the repository root (`python -m http.server 8000`) and open
`http://localhost:8000/tools/replay_viewer/`. The page then reads `Data/` and `RawAssets/` in
place, and `?recordings=/Saved/PlayDemosDownload/` points it at a downloaded artifact. The About
panel (the i button) can also open replay files from the device.

## What it reads

**A recording** is `FPSReplayRecording` as `FJsonObjectConverter` writes it: C++ field names with
the first letter lower-cased (`formatVersion`, `frames`, `ballLocation`), enums by name, FVectors
as `{x, y, z}` in cm, event payloads as JSON strings. `replay_schema.json` lists every field the
viewer reads; the page's loader (`loader.js`) and the Python check (`stage.py`) both read it, and
`tools/tests/test_replay_viewer.py` checks each field is a UPROPERTY of its C++ struct. Keys are
matched in any case. The viewer needs a header with a `FormatVersion` of 1 or more and `Frames`
with `Time` and `Pawns` (`PlayerId`, `Location`); everything else is optional. A version newer
than 2 plays with a note that unknown fields are ignored.

Who each player is comes from, in order: the recording's optional top-level `participants` (lane
S4's additive extension, PR #205: `playerId, displayName, teamId, teamSide, role, jerseyNumber`,
0 meaning none), the rosters in `initialState`, and the pawn snapshots. Without a jersey number a
player is labelled by role and index (`OL3`, `WR2`, `QB`). Kits come from the recording's optional
`teams` (`teamId, displayName, abbreviation, primaryColor, secondaryColor, bHome`; a team's side is
read off its participants), else the team's entry in `Data/sample_teams.json`, else offense blue
and defense red.

Players stand on the ground at the height the recording has them standing (the median of their
capsule centres), so a ground or capsule that sits a little off zero still puts feet on the turf.
The ball is drawn over the index's `groundZ` (or a capsule's half-height below the players).

**The index** (`index.json`) is lane S4's: `{ generatedAtUtc, gameBuildVersion, frameRateHz,
method, plays[] }`. From each play the page uses:

- `file`, and `title` for the play selector with `outcome` and `yardsGained` (`Run +6 yd`);
- `endedBy`: `PhaseClock` (no tackle, landing or boundary) is said plainly, "ended by clock", on
  the selector, the HUD and the whistle;
- `problems[]` as a sanity badge (`Sanity OK`, or the count, listed in About);
- `offensePlayName`, `defensePlayName`, `down`, `distance`, `yardLine`, `offenseTeamId`,
  `defenseTeamId`, `groundZ`;
- for the About panel: `intent`, `wantedOutcome`, `calledBy`, `seed`, `seedsTried`,
  `maxPlayerSpeedCmPerSec` with `fastestPlayerId`, `maxBallSpeedCmPerSec`, `ballTravelCm`, the
  speed limits, and the index's `method`.

Only `file` is needed; the aliases each field accepts are in `replay_schema.json` (`index`).
Anything missing is read from the recording: the calls from its `PlayCall` events, the result
from its `PlayResult`, the duration from its frames.

## What is recorded and what is drawn

- **Recorded** (by the game's telemetry sampler, Epic 26): each player's position, velocity,
  acceleration, facing and possession, and the ball's position and velocity, frame by frame.
  Between two frames the viewer blends them the way the game's own replay does
  (`UPSReplaySubsystem::SampleClip`).
- **The bus's moments**: every event in the recording, on the frames' clock. Events stamped on
  the world's clock are moved onto it by matching keyframes to their events. "Ball to ..." is the
  one moment read off the frames (possession changing hands with no catch or snap there).
- **Contacts** are opponents whose collision capsules (44 cm radius, from `PSPlayerPawn.cpp`)
  touch, measured from the recorded positions, coloured by how fast they were closing.
- **Drawn by the viewer**: the bodies. The run cycle is the world kit's procedural locomotion
  (`RawAssets/world/reference/browser/people.js`: frames posed by aim and pole on Unreal bone
  names), its phase laid against the ground each player covered so feet don't slide, its stride
  against his recorded speed. Height and weight from the roster scale the body and set the
  broad or slim morph. A carried ball sits in the carrier's arm; a loose or thrown ball is
  drawn where it was recorded. Skin tones, hair and kit colours are presentation only.

## The synthetic sample

`sample/SYNTHETIC_*.json` are two hand-made plays (a handoff run and a short pass) for building
the viewer before real recordings existed. They are shaped like the C++ output and S4's
extensions (made-up "Synthetic" teams with jersey numbers) but were drawn from waypoints by
`sample/make_synthetic_sample.py`; nothing in them comes from the game. Their index has no
`problems[]`, so no sanity badge: the recorder's checks never ran on them. Every file is
named `SYNTHETIC_*`, says so in its `GameBuildVersion`, and is marked in its index, and the page
shows a red banner over them. The page falls back to them only when it finds no
`recordings/index.json`.

## Tests

`python -m unittest tools.tests.test_replay_viewer` (part of CI's tool tests):

- every field the viewer reads is a UPROPERTY of its C++ struct; the units and the format version
  match the engine's code;
- `tools/tests/fixtures/replay_viewer_recording.json` and the synthetic sample are shaped exactly
  as `FJsonObjectConverter` writes the current headers, and both play;
- staging builds the folder above, and refuses synthetic or unplayable recordings;
- where Node is installed, the page's own `loader.js` reads the fixture, the sample, and a
  recording with `Participants`, `Teams` and events on another clock (`check_loader.mjs`).
