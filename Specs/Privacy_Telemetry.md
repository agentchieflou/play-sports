# Privacy, Crash Reporting & Session Telemetry (Epic 117)

What the game records about a play session or a crash, where it goes, and what it never touches.
Every system that records anything about a player follows this page. Change the page in the same
PR as the system.

## Principles

1. **Opt-in.** Session telemetry is recorded only after the player says yes. Until they answer,
   nothing is kept.
2. **Local first.** Everything stays on the player's machine. The game has no upload code and no
   telemetry endpoint. Crash reports are uploaded by no one automatically: the engine's crash
   client has no data router, and a person routes reports with a tool.
3. **Minimal and anonymous.** Records hold counts, timings and the game's own state. They never
   hold names, accounts, machine or install identifiers, IP addresses, or free text.
4. **Erasable.** Opting out deletes everything collected, the backup copy included.
5. **Public by default.** The repository is public, so a routed crash issue and a CI log are
   world-readable. Only scrubbed, allowlisted fields go into either.

## Session telemetry

`UPSSessionTelemetrySubsystem` (`Source/PlaySports/Public/PSSessionTelemetry.h`) keeps one session
per game world: the front end, a Play Now game or a practice. It listens to `UPSTelemetryBus`
(Architecture rule 5) and reads nothing else.

| Field (`FPSSessionSummary`) | Why |
| --- | --- |
| `SessionId` | Random per session. It links a session's checkpoints together and nothing across sessions. |
| `Mode` | Mode usage: the travel URL's mode (`PlayNow`, `Franchise`, `Practice`) or the front end's `Menu`. |
| `BuildVersion`, `Platform`, `PlatformTier` | Which build and platform tier (`Data/platform_tiers.json`) the numbers came from. |
| `Date` | The UTC day (`YYYY-MM-DD`), never the time of day. |
| `DurationSeconds`, `PlayCount` | Session length (unpaused) and plays snapped. |
| `FrameCount`, `MaxFrameMs`, `FrameTimePercentiles` | Performance: p50/p95/p99 frame times from a fixed-size histogram. Individual frames are not kept. |
| `bEndedCleanly` | Session health. A session saved as open and never closed means the game crashed or was killed. |

**Storage.** If the player opted in, sessions go to the `Telemetry_Sessions` save slot through
`UPSSaveSubsystem` (`UPSSessionTelemetrySave`). A session is saved as open at the world's
BeginPlay, again every `CheckpointEveryPlays` plays, and as ended cleanly at the world's cleanup.
A clean session shorter than `MinSessionSeconds` is dropped. The store keeps the newest
`MaxStoredSessions` (`Data/session_telemetry.json`).

**Report.** `UPSSessionTelemetrySave::BuildReport` gives the anonymous aggregate: sessions,
sessions that never ended cleanly, and plays and time per mode. `ReportToJson` is the payload a
future uploader would send. Adding that uploader needs a backend and an owner decision, and this
page must be updated first.

**Consent.** `UPSSessionTelemetrySave::GetConsent` / `SetConsent` (Blueprint-callable) are the only
switch. The states are:

- **NotAsked:** no slot exists. This is the default, and nothing is recorded.
- **OptedIn:** sessions are recorded as described above.
- **OptedOut:** the slot and its `.bak` backup are deleted, and then the answer alone is saved.

A first-run prompt or a settings screen calls `SetConsent`. That UI belongs to the front-end lane
and is not built yet. Until it exists, every player is NotAsked and no session is stored.

## Crash reporting

**Capture.** The engine's crash handler writes `Saved/Crashes/<id>/` with the crash context
(`CrashContext.runtime-xml`), a minidump and the log. Packaged builds ship the Crash Report
Client and their `.pdb` files (`Config/DefaultGame.ini`: `bIncludeCrashReporter`,
`bIncludeDebugFiles`), so the call stack in the crash context is symbolized on the player's
machine.

**What the game adds.** `FPSCrashContext` (`PSCrashContext.h`) keeps these crash-context game-data
keys current while a session runs. They hold only the game's own state:

- `PS.SessionId`, `PS.Mode`, `PS.PlatformTier`, `PS.Plays`, `PS.SessionSeconds`.
- `PS.RecentEvents`: the last `CrashBreadcrumbCount` telemetry-bus event descriptions. These are
  snaps, throws, tackles and calls, which name fictional players only.

**No automatic upload.** `Config/DefaultEngine.ini` `[CrashReportClient]` sets
`DataRouterUrl=""` (no endpoint), `bAgreeToCrashUpload=false`, `bSendUnattendedBugReports=false`
and `bSendLogFile=false`.

**Routing to issues.** `tools/crash_report.py` is the only path a report takes off the machine:

- `python tools/crash_report.py summarize [DIR]` prints a scrubbed summary. CI runs it after the
  automation tests, so a crash in a CI run shows its stack in the job log.
- `python tools/crash_report.py file [DIR] [--dry-run]` files one GitHub issue per crash
  signature, labelled `crash`. The signature is the crash type plus the first meaningful
  symbolized frames. A repeat crash comments on its issue, and a crash whose issue was closed
  reopens it as a regression. Check `--dry-run` output before filing, because issues are public.
  For a packaged build, the crash folder is `%LOCALAPPDATA%/<project>/Saved/Crashes`.

The tool copies only these:

- An allowlist of crash-context fields: crash type, error message, engine and build version,
  build configuration, platform, engine mode, and the seconds since start.
- The `PS.*` game data.
- The call stack.

It scrubs user home paths (`C:\Users\<user>`, `/home/<user>`, `/Users/<user>`) from all of them.
It never reads or forwards `UserName`, `LoginId`, `EpicAccountId`, `MachineId`, `CommandLine`,
`BaseDir`, `RootDir` or `UserDescription`. Minidumps and logs never leave the machine: they hold
process memory and paths.

**Unsymbolized stacks.** A build without its `.pdb` files reports addresses only. The tool flags
such a stack and signs it by its error message. To read it, rebuild the same commit with debug
files.

## Before a public release

- Stop shipping `.pdb` files (`bIncludeDebugFiles=False`). Archive them per build and symbolize
  server-side instead.
- Any upload, whether of crashes or of the session report, needs these first:
  - an owner decision;
  - an endpoint with a retention policy;
  - a player-facing privacy notice built from this page;
  - consent UI that can be revisited from settings.
