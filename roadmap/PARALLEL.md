# PARALLEL.md — Parallel Dispatch Matrix

Which epics can be worked concurrently by independent agents, derived from every
`Depends on:` line in `ROADMAP.md` and the track files, plus file-scope analysis. Human
tables below are a rendered view; **the single fenced code block tagged
`json parallel-matrix` is the machine-readable payload** consumed by the Track P supervisor
(`tools/orchestrator/supervisor/board.py`, Epic 138). Any PR that completes an epic, adds
one, or changes a dependency must update both views. `python -m tools.orchestrator
check-parallel` (Epic 138) validates this file against a fresh roadmap crawl.

## Rules

1. An epic is **unblocked** when every ID in its `depends_on` has `status: "done"`.
2. A **group** is dispatchable when all of its members' external dependencies are done;
   `serialize_within: true` means one story at a time inside the group (one phase-runner /
   one worker), members in listed order.
3. Two groups (or two epics) may run **concurrently** iff the union of their `scope` globs
   is disjoint. An epic's scope is its own `scope` field if present, else its track's entry
   in `track_scopes`.
4. `conflicts` entries always override group math — they record known file collisions the
   glob analysis can't see, with the required ordering.
5. Statuses: `done` (all stories checked), `partial` (some checked; remaining stories are
   the open work), `open` (not started). Pseudo-IDs `C3-ff-A`/`C3-ff-B` are Epic C3's two
   unticked fast-follows (ball-action component extraction; roster single source of truth).

## Dispatchable today (2026-07-19)

These groups are mutually disjoint and can run simultaneously:

| Group | Label | Epics (order) | Owner suggestion |
|---|---|---|---|
| G1 | Phase 0/1.5 cleanup | 12.5 → C3-ff-A → C3-ff-B → 1.5 → 2.4 → 5.1–5.4 → 2.1–2.2 (specs/escalation) | Antigravity phase-runner (`.agents/prompts/phase0-cleanup.md`) |
| G2 | Orchestrator (Track P) | 135 → 136 → 137 → 138 | Claude Code |
| G3 | Playbook extraction (Track O) | 132 → 133 → 134 | Any agent (Python-only) |
| G4 | Platform audit | 129 | Any agent (docs/config-only) |
| G5 | Bridge track | 25 (then 118/119) | Any strong agent |

After G1 completes: Track M (126 → 127 → 128) unblocks, and Phase 2 (14 → 15 → 16 → 17 →
18) unblocks in parallel with it — see `roadmap/MILESTONE_FIRST_GAME.md` M4/M5.

```json parallel-matrix
{
  "version": 1,
  "generated": "2026-07-19",
  "track_scopes": {
    "core": ["Source/PlaySports/**", "Data/**", "Config/**"],
    "A": ["Source/PlaySports/**/PSOverlay*", "Source/PlaySports/**/PSTelemetrySampling*", "Content/UI/Overlays/**"],
    "B": ["Source/PlaySports/**/PSCamera*", "Source/PlaySports/**/PSReplay*", "Source/PlaySports/**/PSBroadcastCamera*"],
    "C": ["Content/Stadium/**", "Source/PlaySports/**/PSCrowd*", "Source/PlaySports/**/PSWeather*"],
    "D": ["Content/Characters/**", "Source/PlaySports/**/PSAnim*", "Source/PlaySports/**/PSContact*"],
    "E": ["Source/PlaySports/**/PSRoute*", "Source/PlaySports/**/PSCoverage*", "Source/PlaySports/**/PSPreSnap*", "Source/PlaySports/**/PSRush*"],
    "F": ["Source/PlaySports/**/PSAI*", "Source/PlaySports/**/PSTendency*", "Source/PlaySports/**/PSCalibration*"],
    "G": ["Source/PlaySports/**/PSFranchise*", "Source/PlaySports/**/PSDraft*", "Source/PlaySports/**/PSContract*", "Source/PlaySports/**/PSStats*"],
    "H": ["Content/Audio/**", "Source/PlaySports/**/PSAudio*", "Source/PlaySports/**/PSCommentary*"],
    "I": ["Source/PlaySports/**/PSUI*", "Source/PlaySports/**/PSMenu*", "Source/PlaySports/**/PSPlayCall*", "Content/UI/**"],
    "J": ["Source/PlaySports/**/PSNet*", "Source/PlaySports/**/PSSession*"],
    "K": [".github/workflows/**", "tools/**", "eval/**", "Source/PlaySports/**/PSSave*", "Source/PlaySports/**/PSPerf*", "Source/PlaySports/**/PSDeterminism*", "Plugins/**"],
    "L": ["tools/generators/**", "Data/generated/**", "Source/PlaySports/**/PSGenerator*"],
    "M": ["Source/PlaySports/**/PSPlayerController*", "Source/PlaySports/**/PSInputConfig*", "Source/PlaySports/**/PSForceFeedback*", "Source/PlaySports/PlaySports.Build.cs", "Config/DefaultInput.ini", "play-sports.uproject", "Specs/Input_Architecture.md", "Data/input_glyphs*"],
    "N": ["Specs/Platform_Audit.md", "Specs/Touch_Controls_Spec.md", "Specs/ADR_iOS_Build.md", "Config/DefaultDeviceProfiles.ini", "Source/PlaySports/**/PSTouch*", "Source/PlaySports/**/PSScalability*"],
    "O": ["tools/playbook_scraper/**", "Data/playbooks/**"],
    "P": ["tools/orchestrator/**", "tools/score_lib.py", "eval/duels/**", "eval/runs/**", "roadmap/PARALLEL.md"],
    "Q": ["Source/PlaySports/**/PSArchetype*", "Source/PlaySports/**/PSHealth*", "Source/PlaySports/**/PSCombat*", "Source/PlaySports/**/PSLeveling*", "Source/PlaySports/**/PSPlayerLeveling*"]
  },
  "epics": {
    "1":   {"track": "core", "mode": "mixed", "status": "partial", "depends_on": [], "open_stories": ["1.5 end-to-end PIE test"]},
    "2":   {"track": "core", "mode": "mixed", "status": "partial", "depends_on": [], "open_stories": ["2.1 field geometry (editor)", "2.2 markings (editor)", "2.4 OOB/end-zone volumes"]},
    "3":   {"track": "core", "mode": "code", "status": "done", "depends_on": ["2"]},
    "4":   {"track": "core", "mode": "code", "status": "done", "depends_on": ["2", "3"]},
    "5":   {"track": "core", "mode": "mixed", "status": "open", "depends_on": ["1"], "scope": ["Source/PlaySports/**/PSHUD*", "Source/PlaySports/**/PSScoreboard*", "Content/UI/HUD/**"]},
    "6":   {"track": "core", "mode": "code", "status": "done", "depends_on": ["3"]},
    "7":   {"track": "core", "mode": "code", "status": "done", "depends_on": ["3", "6"]},
    "8":   {"track": "core", "mode": "code", "status": "done", "depends_on": ["6", "7"]},
    "9":   {"track": "core", "mode": "code", "status": "done", "depends_on": ["6", "8"]},
    "10":  {"track": "core", "mode": "code", "status": "done", "depends_on": ["1"]},
    "11":  {"track": "core", "mode": "code", "status": "done", "depends_on": ["2", "10"]},
    "12":  {"track": "core", "mode": "code", "status": "partial", "depends_on": ["10"], "open_stories": ["12.5 timeout budget per team"]},
    "13":  {"track": "core", "mode": "code", "status": "done", "depends_on": ["7", "10", "11"]},
    "C1":  {"track": "core", "mode": "code", "status": "done", "depends_on": []},
    "C2":  {"track": "core", "mode": "code", "status": "done", "depends_on": ["C1"]},
    "C3":  {"track": "core", "mode": "code", "status": "partial", "depends_on": ["C1"], "open_stories": ["C3-ff-A", "C3-ff-B"]},
    "C3-ff-A": {"track": "core", "mode": "code", "status": "open", "depends_on": ["C3"], "scope": ["Source/PlaySports/**/PSPlayerPawn*", "Source/PlaySports/**/PSBallAction*"], "note": "extract ball-action component from APSPlayerPawn"},
    "C3-ff-B": {"track": "core", "mode": "code", "status": "open", "depends_on": ["C3-ff-A"], "scope": ["Source/PlaySports/**/PSGameMode*", "Source/PlaySports/**/PSPlayerPawn*", "Source/PlaySports/**/PSPlaySimulation*"], "note": "single roster source of truth"},
    "C4":  {"track": "core", "mode": "code", "status": "done", "depends_on": ["C2", "C3"]},
    "14":  {"track": "core", "mode": "code", "status": "open", "depends_on": ["6", "7", "9", "C1", "C2", "C3-ff-B", "C4"]},
    "15":  {"track": "core", "mode": "code", "status": "open", "depends_on": ["9", "14"]},
    "16":  {"track": "core", "mode": "code", "status": "open", "depends_on": ["14"]},
    "17":  {"track": "core", "mode": "code", "status": "open", "depends_on": ["14", "15", "16"]},
    "18":  {"track": "core", "mode": "code", "status": "open", "depends_on": ["16", "17"]},
    "19":  {"track": "core", "mode": "code", "status": "open", "depends_on": ["1"]},
    "20":  {"track": "core", "mode": "code", "status": "open", "depends_on": ["12", "19"]},
    "21":  {"track": "core", "mode": "code", "status": "open", "depends_on": ["16", "19"]},
    "22":  {"track": "core", "mode": "editor", "status": "open", "depends_on": ["6", "7", "8"]},
    "23":  {"track": "core", "mode": "mixed", "status": "open", "depends_on": ["8", "11"]},
    "24":  {"track": "core", "mode": "code", "status": "open", "depends_on": []},
    "25":  {"track": "core", "mode": "code", "status": "open", "depends_on": [], "scope": ["Plugins/Autonomix/**", "Plugins/AgenticLink/**", ".mcp.json", ".vscode/mcp.json"]},
    "26":  {"track": "A", "mode": "code", "status": "open", "depends_on": ["C1", "3", "6"]},
    "27":  {"track": "A", "mode": "mixed", "status": "open", "depends_on": ["26", "16"]},
    "28":  {"track": "A", "mode": "mixed", "status": "open", "depends_on": ["26"]},
    "29":  {"track": "A", "mode": "code", "status": "open", "depends_on": ["5", "19"]},
    "30":  {"track": "A", "mode": "code", "status": "open", "depends_on": ["3"]},
    "31":  {"track": "A", "mode": "mixed", "status": "open", "depends_on": ["26", "27", "16"]},
    "32":  {"track": "A", "mode": "code", "status": "open", "depends_on": ["26", "7"]},
    "33":  {"track": "A", "mode": "code", "status": "open", "depends_on": ["5", "10", "12"]},
    "34":  {"track": "A", "mode": "mixed", "status": "open", "depends_on": ["26", "2", "10"]},
    "35":  {"track": "A", "mode": "code", "status": "open", "depends_on": ["27", "31", "16"]},
    "36":  {"track": "A", "mode": "mixed", "status": "open", "depends_on": ["26"]},
    "37":  {"track": "A", "mode": "code", "status": "open", "depends_on": ["28", "29", "33", "34"]},
    "38":  {"track": "B", "mode": "code", "status": "open", "depends_on": ["4", "26"]},
    "39":  {"track": "B", "mode": "code", "status": "open", "depends_on": ["38"]},
    "40":  {"track": "B", "mode": "code", "status": "open", "depends_on": ["4"]},
    "41":  {"track": "B", "mode": "code", "status": "open", "depends_on": ["C1", "26", "38", "17"]},
    "42":  {"track": "B", "mode": "code", "status": "open", "depends_on": ["41"]},
    "43":  {"track": "B", "mode": "editor", "status": "open", "depends_on": ["38", "22"]},
    "44":  {"track": "B", "mode": "code", "status": "open", "depends_on": ["40", "41"]},
    "45":  {"track": "B", "mode": "code", "status": "open", "depends_on": ["41"]},
    "46":  {"track": "C", "mode": "editor", "status": "open", "depends_on": ["2"]},
    "47":  {"track": "C", "mode": "mixed", "status": "open", "depends_on": ["46", "6", "7"]},
    "48":  {"track": "C", "mode": "mixed", "status": "open", "depends_on": ["2"]},
    "49":  {"track": "C", "mode": "code", "status": "open", "depends_on": ["48", "26"]},
    "50":  {"track": "C", "mode": "editor", "status": "open", "depends_on": ["48", "2"]},
    "51":  {"track": "C", "mode": "mixed", "status": "open", "depends_on": ["47", "2"]},
    "52":  {"track": "C", "mode": "editor", "status": "open", "depends_on": ["2", "46"]},
    "53":  {"track": "C", "mode": "code", "status": "open", "depends_on": ["2", "37"]},
    "54":  {"track": "C", "mode": "code", "status": "open", "depends_on": ["41", "52"]},
    "55":  {"track": "C", "mode": "editor", "status": "open", "depends_on": ["46", "49"]},
    "56":  {"track": "D", "mode": "mixed", "status": "open", "depends_on": ["22"]},
    "57":  {"track": "D", "mode": "editor", "status": "open", "depends_on": ["22"]},
    "58":  {"track": "D", "mode": "mixed", "status": "open", "depends_on": ["22", "56"]},
    "59":  {"track": "D", "mode": "editor", "status": "open", "depends_on": ["56"]},
    "60":  {"track": "D", "mode": "mixed", "status": "open", "depends_on": ["22", "14"]},
    "61":  {"track": "D", "mode": "mixed", "status": "open", "depends_on": ["C3", "8", "22"]},
    "62":  {"track": "D", "mode": "editor", "status": "open", "depends_on": ["7", "22"]},
    "63":  {"track": "D", "mode": "editor", "status": "open", "depends_on": ["22", "14"]},
    "64":  {"track": "D", "mode": "editor", "status": "open", "depends_on": ["9", "22"]},
    "65":  {"track": "D", "mode": "mixed", "status": "open", "depends_on": ["11", "2"]},
    "66":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["14", "16"]},
    "67":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["15", "16", "66"]},
    "68":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["C3", "C4", "14", "16"]},
    "69":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["C3", "C4", "15", "68"]},
    "70":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["C3", "C4", "9"]},
    "71":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["14", "9"]},
    "72":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["66", "68", "14", "15"]},
    "73":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["11", "69", "61"]},
    "74":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["7", "8", "61"]},
    "75":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["13"]},
    "76":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["12", "18"]},
    "77":  {"track": "E", "mode": "code", "status": "open", "depends_on": ["6", "49"]},
    "78":  {"track": "F", "mode": "code", "status": "open", "depends_on": ["18", "26"]},
    "79":  {"track": "F", "mode": "code", "status": "open", "depends_on": ["14", "15", "19"]},
    "80":  {"track": "F", "mode": "code", "status": "open", "depends_on": ["15", "68", "72"]},
    "81":  {"track": "F", "mode": "code", "status": "open", "depends_on": ["15", "9"]},
    "82":  {"track": "F", "mode": "code", "status": "open", "depends_on": ["25", "26", "18"]},
    "83":  {"track": "F", "mode": "code", "status": "open", "depends_on": ["20", "24"]},
    "84":  {"track": "F", "mode": "code", "status": "open", "depends_on": ["78", "79"]},
    "85":  {"track": "F", "mode": "code", "status": "open", "depends_on": ["14", "15", "26"]},
    "86":  {"track": "G", "mode": "code", "status": "open", "depends_on": ["19", "20", "121", "122"]},
    "87":  {"track": "G", "mode": "code", "status": "open", "depends_on": ["19", "20"]},
    "88":  {"track": "G", "mode": "code", "status": "open", "depends_on": ["86", "87"]},
    "89":  {"track": "G", "mode": "code", "status": "open", "depends_on": ["18", "16"]},
    "90":  {"track": "G", "mode": "code", "status": "open", "depends_on": ["19", "78"]},
    "91":  {"track": "G", "mode": "code", "status": "open", "depends_on": ["19", "87"]},
    "92":  {"track": "G", "mode": "code", "status": "open", "depends_on": ["26", "20"]},
    "93":  {"track": "G", "mode": "code", "status": "open", "depends_on": ["92", "82"]},
    "94":  {"track": "G", "mode": "code", "status": "open", "depends_on": ["86", "92"]},
    "95":  {"track": "G", "mode": "code", "status": "open", "depends_on": ["87", "20"]},
    "96":  {"track": "H", "mode": "mixed", "status": "open", "depends_on": ["23", "26", "92", "93"]},
    "97":  {"track": "H", "mode": "mixed", "status": "open", "depends_on": ["23", "49"]},
    "98":  {"track": "H", "mode": "editor", "status": "open", "depends_on": ["23"]},
    "99":  {"track": "H", "mode": "code", "status": "open", "depends_on": ["97", "52"]},
    "100": {"track": "H", "mode": "mixed", "status": "open", "depends_on": ["96", "97", "98"]},
    "101": {"track": "I", "mode": "code", "status": "open", "depends_on": ["5"]},
    "102": {"track": "I", "mode": "code", "status": "open", "depends_on": ["16", "101"]},
    "103": {"track": "I", "mode": "code", "status": "open", "depends_on": ["101"]},
    "104": {"track": "I", "mode": "code", "status": "open", "depends_on": ["3", "6", "126", "127"]},
    "105": {"track": "I", "mode": "code", "status": "open", "depends_on": ["101", "104", "24"]},
    "106": {"track": "I", "mode": "code", "status": "open", "depends_on": ["101"]},
    "107": {"track": "J", "mode": "code", "status": "open", "depends_on": ["104", "102"]},
    "108": {"track": "J", "mode": "code", "status": "open", "depends_on": ["17", "115"]},
    "109": {"track": "J", "mode": "code", "status": "open", "depends_on": ["108", "107"]},
    "110": {"track": "J", "mode": "code", "status": "open", "depends_on": ["109", "38"]},
    "111": {"track": "J", "mode": "code", "status": "open", "depends_on": ["20", "109", "116"]},
    "112": {"track": "K", "mode": "code", "status": "done", "depends_on": []},
    "113": {"track": "K", "mode": "code", "status": "done", "depends_on": []},
    "114": {"track": "K", "mode": "code", "status": "open", "depends_on": ["17"]},
    "115": {"track": "K", "mode": "code", "status": "open", "depends_on": ["17", "26"]},
    "116": {"track": "K", "mode": "code", "status": "done", "depends_on": []},
    "117": {"track": "K", "mode": "code", "status": "open", "depends_on": []},
    "118": {"track": "K", "mode": "code", "status": "open", "depends_on": ["25"]},
    "119": {"track": "K", "mode": "code", "status": "open", "depends_on": ["25", "135"]},
    "120": {"track": "K", "mode": "code", "status": "done", "depends_on": ["112", "113"]},
    "121": {"track": "L", "mode": "code", "status": "open", "depends_on": ["16", "89"]},
    "122": {"track": "L", "mode": "code", "status": "open", "depends_on": ["19", "79"]},
    "123": {"track": "L", "mode": "mixed", "status": "open", "depends_on": ["37", "56"]},
    "124": {"track": "L", "mode": "mixed", "status": "open", "depends_on": ["52", "123"]},
    "125": {"track": "L", "mode": "code", "status": "open", "depends_on": ["21", "113"]},
    "126": {"track": "M", "mode": "code", "status": "open", "depends_on": ["3"]},
    "127": {"track": "M", "mode": "code", "status": "open", "depends_on": ["126", "C3-ff-A"]},
    "128": {"track": "M", "mode": "code", "status": "open", "depends_on": ["127"]},
    "129": {"track": "N", "mode": "code", "status": "open", "depends_on": []},
    "130": {"track": "N", "mode": "code", "status": "open", "depends_on": ["126", "128", "129"]},
    "131": {"track": "N", "mode": "mixed", "status": "open", "depends_on": ["129"]},
    "132": {"track": "O", "mode": "code", "status": "open", "depends_on": []},
    "133": {"track": "O", "mode": "code", "status": "open", "depends_on": ["132"]},
    "134": {"track": "O", "mode": "code", "status": "open", "depends_on": ["133"]},
    "135": {"track": "P", "mode": "code", "status": "open", "depends_on": []},
    "136": {"track": "P", "mode": "code", "status": "open", "depends_on": ["135"]},
    "137": {"track": "P", "mode": "code", "status": "open", "depends_on": ["136"]},
    "138": {"track": "P", "mode": "code", "status": "open", "depends_on": ["136", "137"]},
    "139": {"track": "Q", "mode": "code", "status": "done", "depends_on": ["8", "19", "C1"]},
    "140": {"track": "Q", "mode": "code", "status": "done", "depends_on": ["139", "18", "17"]},
    "141": {"track": "Q", "mode": "code", "status": "done", "depends_on": ["139", "19"]}
  },
  "groups": [
    {"id": "G1", "label": "Phase 0/1.5 cleanup", "epics": ["12", "C3-ff-A", "C3-ff-B", "1", "2", "5"], "serialize_within": true},
    {"id": "G2", "label": "Orchestrator (Track P)", "epics": ["135", "136", "137", "138"], "serialize_within": true},
    {"id": "G3", "label": "Playbook extraction (Track O)", "epics": ["132", "133", "134"], "serialize_within": true},
    {"id": "G4", "label": "Platform audit", "epics": ["129"], "serialize_within": true},
    {"id": "G5", "label": "Agentic engine bridge", "epics": ["25"], "serialize_within": true},
    {"id": "G6", "label": "Controller connectivity (Track M)", "epics": ["126", "127", "128"], "serialize_within": true},
    {"id": "G7", "label": "Phase 2 AI & playbook", "epics": ["14", "15", "16", "17", "18"], "serialize_within": true}
  ],
  "conflicts": [
    {"epics": ["127", "C3-ff-A"], "reason": "both touch APSPlayerPawn; C3 fast-follow A must merge first"},
    {"epics": ["C3-ff-B", "14"], "reason": "Phase 2 AI must consume the single roster source of truth; C3-ff-B first"},
    {"epics": ["126", "C3-ff-A"], "reason": "126's input migration edits APSPlayerPawn's SetupPlayerInputComponent; do not run concurrently with the ball-action extraction"}
  ]
}
```
