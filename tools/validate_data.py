#!/usr/bin/env python
"""Data contract validator for play-sports (Epic 113).

Validates every JSON file under Data/ : parseability always; files carrying a "Players" array
additionally against the FPlayerAttributes contract (Source/PlaySports/Public/PSPlayerAttributes.h)
- exact field names, numeric types, valid EPlayerRole values, unique non-empty PlayerId; files
carrying "Contexts" + "Actions" against the input catalog contract (FPSInputCatalog,
Source/PlaySports/Public/PSInputConfigTypes.h; Specs/Input_Architecture.md); files carrying
"StickDeadZoneLower" against FInputTuningRow's ranges; files carrying "Screens" + "RootScreen"
against the menu catalog rules (FPSMenuCatalog); team identity fields (colors, abbreviation) on
"Teams" files; "Tips" files against FPSLoadingTipCatalog; "Cues" + "MasterIntensity" files against
FPSForceFeedbackTuning; "GlyphSets" files against FPSInputGlyphCatalog, including that every key the
input catalog binds has a glyph; "CpuSnapDelaySeconds" files against FPlayCallTuningRow;
"Adjustments" files against FPSDefensiveAdjustmentCatalog; "OpenSeparation" files against
FSkillPlayerAITuningRow; "ManCushion" files against FDefenderAITuningRow; "SlotActions" files
against FPassingInputTuningRow, including that each named action is a Boolean in the input catalog's
Passing context; "Moves" files against FPSCarrierMoveCatalog, each move's action a Boolean in the
BallCarrier context; "Tiers" files against FPSPlatformTierCatalog, each tier's DeviceProfile defined
by the engine (Windows, IOS, ...) or in Config/DefaultDeviceProfiles.ini; "MaxQueued" files against
FInputBufferTuningRow, each buffered action a Boolean catalog action; "RushMoves" files against
FPSRushMoveCatalog; "HotRouteSets" files against FPreSnapTuningRow, each route in the route library
and each action a Boolean in the PreSnap context; "SituationTempos" files against
FPSSituationalTuning, its route IDs against the route library; "KeyframeEvents" files against
FPSTelemetrySamplingTuning, each event an EPSTelemetryEventType as the bus header declares it;
"FrameTimeBucketMs" files against FPSSessionTelemetryTuning (Epic 117); "Fronts" files against
FPSRunFitCatalog; "PressRadius" files against FRouteRunningTuningRow; "Routes" files against the
FPSRoute library (timing, fakes, option branches); "All22Rigs" files against FPSAll22CameraTuning;
"JumpWindowSeconds" files against FDefensiveTechniqueTuningRow and "PowerFillSeconds" files against
FKickMeterTuningRow, each named action a Boolean in its context; "CutRules" files against
FPSCameraDirectorTuning, each all-22 shot's rig in camera_all22.json; "ReticleStates" files against
FPSOverlayReticleStyle; "CycleWindowSeconds" files against FControlHandoffTuningRow, each pick
action a Boolean in the input catalog's PreSnap context; "ChyronKinds" files against
FPSBroadcastOverlayTheme; "Settings" files against FPSSettingsCatalog (Epic 103.1);
"CatenaryParameterCm" files against FPSSkycamTuning; "UncoveredSeparation" files against
FBlownCoverageTuningRow; "Packages" + "DefaultOffensePackage" files against FPSPersonnelCatalog (11
players per package, roles on the package's side, one package per formation and side);
"CaptionWordsPerSecond" files against FPSUIAccessibilityTuning (Epic 103.2). The UI string tables
(Data/ui_text.csv, Data/ui_text_data.csv) and the UI code's text are checked by tools/ui_text.py
(Epic 106); "KickoffTouchbackChance" files against FPSSpecialTeamsTuning, each return scheme a
KickReturn formation (Epic 75); "UprightWidth" files against FPSBallFlightStyle; "PocketRadius"
files against FPocketTuningRow; "TouchControls" files against FPSTouchLayout (Epic 130): every input
context has a touch button set or is listed as without one, each bound action lives in its context
with the control's value type, every action of a covered context is reachable by touch, and every
touch-bound action has a Touch glyph; "Staffs" files against FPSCoachingLeague (Epic 89): each
scheme's formations in the playbook on its side (an offense keeping a run and a pass, a defense a
base call), coaches' schemes and roles, each staff's team in sample_teams.json and its jobs held by
coaches of that role; "RoleLabels" files against FPSOverlayBadgeStyle; "DimStencil" files against
FPSEmphasisStyle (Epic 36); "Axes" + "Bindings" files against FPSPlayerDNACatalog, each axis an
FPSPlayerDNA field, each binding a numeric field of its target's tuning file and each rush move in
pass_rush_moves.json, and every player's optional "DNA" against its axes and his role (Epic 79);
"PositionMarkets" files against FPSContractTuning (Epic 87): one market per EPlayerRole, ordered
rating and guarantee bounds, offer ratios walk-away <= accept <= instant; "ShellSafeties" files
against FPSDefensivePreSnapTuning (Epic 67), each action a Boolean in the DefensePreSnap context;
"Hints" files against FPSHintCatalog (Epic 105.4); "PlaybackRates" files against FPSReplayTuning
(Epic 41), each camera a named one or a rig in camera_all22.json; "DefenseNameFallback" files
against FPSPersonnelPanelStyle (Epic 29); "MinSamples" files against FPSOpponentModelTuning, each
counter pairing a play category the human calls with one the CPU answers on the other side (Epic
78); "PausesPerHalf" files against FPSVersusRules (Epic 107): control roles on their sides, screen
and overlay audiences, pause and resume etiquette; "bLogDecisions" files against FPSAIDebugTuning
(its overlay cards' sizes, colors and nudges too, Epic 85.2) and "Scenarios" files against FPSAIScenarioCatalog, each expectation and cover target naming a
player of its scenario (Epic 85); "StadiumCapacity" files against FPSEconomyTuning (Epic 95):
ordered prices and fill rates, 0-1 satisfaction, the default budget within MaxBudgetFraction;
"UnownedColor" files against FPSGapOverlayStyle (Epic 81); "TradeRequestWeeks" files against
FPSMoraleTuning (Epic 91): 0-1 thresholds, each chemistry unit's role, games and bonus; "ReelSize"
files against FPSHighlightTuning (Epic 42); "PlayerPickRadius" files against FPSTelestratorTuning
(Epic 44), its drawing layer's colors and sizes too; "LeverageShade" files against FPSCoverageMatchupTuning (Epic 69): its shell rules (each
coverage shell the playbook calls has one) and a press spot inside the route-running PressRadius;
"ScoopClearRadius" files against FPSLooseBallTuning (Epic 17.4); "DifficultyTiers" files against
FPSDifficultyCatalog, each scale a numeric field of its AI tuning file, the tiers the Difficulty
setting's choices in order and each assist a toggle in ui_settings.json (Epic 84);
"MeshRecognizeRadius" files against FPSDeceptionTuning (Epic 72): a discipline rating 0-100, a
tendency window of 1 or more; "HardFailMultiplier" files against
FPSPerfHarnessTuning (Epic 114), and every platform tier's SystemBudgets: one per system, within its
frame; "FocusAreas" files against FPSTrainingTuning (Epic 90): 0-1 fatigue, recovery and AI fields,
the practice injury tuning, each focus area's roles, rating weights and play categories (each one
opponent_model.json tracks on its side); "CaptureResolutionMultiplier" files against
FPSPhotoModeTuning (Epic 45); "RoleProfiles" + "NameCultures" files against FPSLeagueGeneratorTuning
(Epic 122): a profile per EPlayerRole with a curve for every float field of FPlayerAttributes, name
pools and the real-person NameBlocklist, which every roster's DisplayNames are checked against;
"PeakAgeStart" files against FPSProgressionTuning (the age curve; Epic 122); a player's optional
"Age" is a whole number; "ReadColors" files against FPSPlayArtStyle (Epics 27 and 31), each no-art
category one of its side's play categories, and its "Diagram" block against FPSPlayDiagramStyle
(Epic 102.1's play-call previews); "Concepts" + "Coverages" files against
FPSPlaybookGeneratorTuning (Epic 121): its concepts' routes in the route library and formations in
personnel packages, each front in run_fits.json, each coverage shell in coverage_matchups.json and
each flavor's scheme in coaching_staffs.json; "CombineDrills" files against FPSDraftTuning (Epic
86): positive uncertainties and costs, 0-1 shares and guarantees, each drill reading a rating, a
rookie deal no longer than contracts.json's MaxContractYears; "PlayCallTimeoutSeconds" files against
FPSGameIntelligenceTuning (Epic 82), each task one of tools/orchestrator/routing.json's;
"HallOfFame" files against FPSLegacyTuning (Epic 94): the hall of fame's waits and score, each
threshold and archived leader a player stat category, listed once, each award's score an
EPSAwardKind's, once, each role's age curve and the retirement chances; "StorylineKinds" files against FPSNarrativeTuning (Epic 93): one weight per
EPSStorylineKind, award scoring by EPSStatCategory, a falling ballot, the digest's task one of
routing.json's; "FormationClasses" files against FPSPlayRecognitionTuning (Epic 80): its distances,
read scales and weights, the pistol no shallower than under center, 0-1 leans and weights, and each
formation class's unique ID, alignment, backfield and counts; "EventCues" files against
FPSAudioTuning (Epic 23.1): unique cues on known layers, 0-1 volumes, 0-100 priorities, loops in a
group, each rule's trigger an EPSAudioTrigger and its cue in the catalog, each layer's volume a
0-100 slider in ui_settings.json; "CrowdReactions" files against FPSCrowdTuning (Epic 23.2): every
EPSCrowdLevel once with rising thresholds from Hush's 0, every EPSCrowdStimulus once with -1..1
deltas; "ModelMoments" files against FPSCommentaryHookTuning (Epic 23.5), each moment an
EPSCommentaryMoment and the task one of routing.json's, the stakes and novelty weights 0-1 (Epic
96.1); "CentimetresPerYard" files against FPSFieldDimensions (Data/field_dimensions.json, the
field's one frame): every dimension a positive number; "HoldingChancePerPlay" files against
FPSPenaltyTuning (Data/penalties.json): each flag's chance from 0 to 1; "PickRoundValues" files
against FPSTradeTuning (Epic 88): one entry per EPSTradeStance, 0-1 weights, chances and win
percentages (the rebuilder's under the contender's), a counter ratio no more than the accept ratio,
a falling pick chart above its last pick's value; "SkillWindowGrowthPerSecond" files against
FPSSessionMatchmakingTuning (Epic 108.5): a protocol version of 1 or more, skill windows that widen
to a cap no narrower than they start, waits and host scores of 0 or more, each default cross-play
policy an EPSCrossPlayPolicy; "InterruptMargin" files against FPSCommentaryLibrary (Epic 96): the
booth's pacing, every line's voice, moment and conditions, its Commentary.Line.<LineId> text in
Data/ui_text.csv naming only the facts it may and the players and totals it says it needs, a
play-by-play line for every moment, and the voices', storyline's and fouls' rows; "Techniques" files
against FPSFormationCatalog (Data/formations.json): known techniques, sides and roles, every
formation the plays, generator, staffs and packages name with a slot for each of its package's
players and the QBAlignment, Backfield and Strength play_recognition.json reads from them, every
front and shell the plays, run_fits.json and coverage_matchups.json name, each defensive call's
package placed, and each shell's deep safeties defensive_presnap.json's. "HashOffsetYards" files
against FPSFieldMarkingsStyle (Data/field_markings.json, Epic 146.3): the field's meshes and
material named, #RRGGBB colors, positive line sizes and spacings. A player's optional
"JerseyNumber" is 1-99 and unique on his roster. "PlayDemos" files against FPSPlayDemoCatalog
(Data/play_demos.json, the live-play demos): a positive frame rate and limits, unique demo IDs,
two different league teams per demo, and each call an offensive or defensive play the playbook has
and its team's scheme keeps. "PressedOpacity" files
against FPSTouchHudStyle (Data/touch_hud.json, Epic 146.4): opacities and fractions from 0 to 1,
#RRGGBB colors, positive sizes. Teams, the league config,
the playbook, player rating ranges and every reference between files are
tools/content_contracts.py's (Epic 125), run from here.

Exit 0 when clean, exit 1 with actionable errors (file / row / field).
Run from the repo root:  python tools/validate_data.py
With --root DIR, checks DIR/Data instead (a generated league laid out like the repo, e.g. the one
the league generator's automation test writes); the catalogs it is checked against stay the repo's.
"""

import argparse
import json
import math
import re
import sys
from pathlib import Path

try:
    from tools import content_contracts  # imported as part of the tools package (tests)
except ImportError:
    import content_contracts  # run as a script from tools/

REPO = Path(__file__).resolve().parent.parent
DATA_DIR = REPO / "Data"

PLAYER_ROLES = {
    "Quarterback", "RunningBack", "WideReceiver", "TightEnd",
    "OffensiveLineman", "DefensiveLineman", "Linebacker", "DefensiveBack",
}
PLAYER_FIELDS = {
    "PlayerId": str,
    "DisplayName": str,
    "Role": str,
    "WeightKg": (int, float),
    "HeightCm": (int, float),
    "Speed": (int, float),
    "Agility": (int, float),
    "Strength": (int, float),
    "Acceleration": (int, float),
    "Awareness": (int, float),
    "Stamina": (int, float),
}
# Optional FPlayerAttributes fields: Age (Epic 122), 0 or missing meaning unknown; JerseyNumber,
# 0 or missing meaning none, else 1-99 and unique on its roster.
PLAYER_OPTIONAL_FIELDS = {"Age": int, "JerseyNumber": int}

INPUT_VALUE_TYPES = {"Boolean", "Axis1D", "Axis2D", "Axis3D"}
INPUT_CONTEXT_FIELDS = {"ContextId": str, "Priority": int, "Description": str, "bRemappable": bool}
INPUT_ACTION_FIELDS = {"ActionId": str, "ValueType": str, "Description": str, "Contexts": list, "Bindings": list,
                       "bTriggerWhenPaused": bool}
INPUT_BINDING_FIELDS = {"Key": str, "bSwizzleYX": bool, "bNegate": bool}
INPUT_REQUIRED = {"ContextId", "ActionId", "ValueType", "Contexts", "Bindings", "Key"}

errors = []


def err(path, message):
    try:
        shown = Path(path).resolve().relative_to(REPO)
    except ValueError:
        shown = path
    errors.append(f"{shown}: {message}")


def validate_players(path, players, dna_catalog=None):
    seen_ids = set()
    seen_numbers = {}
    for idx, row in enumerate(players):
        where = f"Players[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: not an object")
            continue
        for field, ftype in PLAYER_FIELDS.items():
            if field not in row:
                err(path, f"{where}: missing field '{field}'")
            elif not isinstance(row[field], ftype):
                err(path, f"{where}.{field}: expected {ftype}, got {type(row[field]).__name__}")
        for field, ftype in PLAYER_OPTIONAL_FIELDS.items():
            if field in row and (isinstance(row[field], bool) or not isinstance(row[field], ftype)):
                err(path, f"{where}.{field}: expected a whole number, got {type(row[field]).__name__}")
        extra = set(row) - set(PLAYER_FIELDS) - set(PLAYER_OPTIONAL_FIELDS) - {"DNA"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPlayerAttributes exactly")
        role = row.get("Role")
        if isinstance(role, str) and role not in PLAYER_ROLES:
            err(path, f"{where}.Role: '{role}' is not a valid EPlayerRole")
        if "DNA" in row:
            validate_player_dna(path, where, row, dna_catalog)
        number = row.get("JerseyNumber")
        if isinstance(number, int) and not isinstance(number, bool) and number != 0:
            if not 1 <= number <= 99:
                err(path, f"{where}.JerseyNumber: {number} is not 1-99 (0 or missing means none)")
            elif number in seen_numbers:
                err(path, f"{where}.JerseyNumber: {number} is already worn by {seen_numbers[number]}")
            else:
                seen_numbers[number] = row.get("PlayerId")
        pid = row.get("PlayerId")
        if isinstance(pid, str):
            if not pid:
                err(path, f"{where}.PlayerId: empty")
            elif pid in seen_ids:
                err(path, f"{where}.PlayerId: duplicate '{pid}'")
            seen_ids.add(pid)


def check_fields(path, where, row, fields):
    """Type-check known fields, require INPUT_REQUIRED ones, reject unknown names."""
    if not isinstance(row, dict):
        err(path, f"{where}: not an object")
        return False
    for field, ftype in fields.items():
        if field not in row:
            if field in INPUT_REQUIRED:
                err(path, f"{where}: missing field '{field}'")
        elif ftype is int and isinstance(row[field], bool) or not isinstance(row[field], ftype):
            err(path, f"{where}.{field}: expected {ftype.__name__}, got {type(row[field]).__name__}")
    extra = set(row) - set(fields)
    if extra:
        err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match the input catalog structs exactly")
    return True


def is_gamepad_key(key):
    # Every engine gamepad key (EKeys::Gamepad_*) carries this prefix.
    return key.startswith("Gamepad_")


def validate_input_catalog(path, payload):
    contexts, actions = payload["Contexts"], payload["Actions"]
    if not isinstance(contexts, list) or not isinstance(actions, list):
        err(path, "'Contexts' and 'Actions' must be arrays")
        return

    context_ids = set()
    for idx, row in enumerate(contexts):
        where = f"Contexts[{idx}]"
        if not check_fields(path, where, row, INPUT_CONTEXT_FIELDS):
            continue
        cid = row.get("ContextId")
        if isinstance(cid, str):
            if not cid:
                err(path, f"{where}.ContextId: empty")
            elif cid in context_ids:
                err(path, f"{where}.ContextId: duplicate '{cid}'")
            context_ids.add(cid)

    action_ids = set()
    keys_by_context = {}
    for idx, row in enumerate(actions):
        where = f"Actions[{idx}]"
        if not check_fields(path, where, row, INPUT_ACTION_FIELDS):
            continue
        aid = row.get("ActionId")
        if isinstance(aid, str):
            where = f"Actions[{idx}] '{aid}'"
            if not aid:
                err(path, f"{where}.ActionId: empty")
            elif aid in action_ids:
                err(path, f"{where}.ActionId: duplicate")
            action_ids.add(aid)
        vtype = row.get("ValueType")
        if isinstance(vtype, str) and vtype not in INPUT_VALUE_TYPES:
            err(path, f"{where}.ValueType: '{vtype}' is not an EInputActionValueType ({sorted(INPUT_VALUE_TYPES)})")

        keys = []
        for bidx, binding in enumerate(row.get("Bindings") or []):
            if check_fields(path, f"{where}.Bindings[{bidx}]", binding, INPUT_BINDING_FIELDS):
                key = binding.get("Key")
                if isinstance(key, str) and key:
                    keys.append(key)
                elif "Key" in binding:
                    err(path, f"{where}.Bindings[{bidx}].Key: empty")

        declared = row.get("Contexts") if isinstance(row.get("Contexts"), list) else []
        if not declared:
            err(path, f"{where}.Contexts: declares no context, so nothing can trigger it")
        for cid in declared:
            if cid not in context_ids:
                err(path, f"{where}.Contexts: unknown context '{cid}'")
                continue
            if not any(not is_gamepad_key(k) for k in keys):
                err(path, f"{where}: no keyboard/mouse binding in context '{cid}'")
            if not any(is_gamepad_key(k) for k in keys):
                err(path, f"{where}: no gamepad binding in context '{cid}'")
            bound = keys_by_context.setdefault(cid, {})
            for key in keys:
                owner = bound.setdefault(key, aid)
                if owner != aid:
                    err(path, f"{where}: key '{key}' is already bound to '{owner}' in context '{cid}'")


MENU_COMMANDS = {"None", "Resume", "StartPlayNow", "StartFranchise", "StartPractice", "QuitToMainMenu", "QuitGame", "CallPlay", "ApplyAdjustment",
                 "StepSetting", "ResetSettings", "BeginRemap", "ResetRemaps", "StartVersus"}
MENU_CONTENTS = {"Static", "TeamSelect", "Loading", "PlayCallFormations", "PlayCallPlays", "PlayCallRecent",
                 "PlayCallFavorites", "PlayCallAdjustments", "Settings", "SettingsCategory", "InputRemap"}
TIP_CONTEXTS = {"Any", "PlayNow", "Franchise", "Practice"}
HEX_COLOR = re.compile(r"^#[0-9A-Fa-f]{6}$")


def validate_menu_catalog(path, payload):
    """FPSMenuCatalog (Data/ui_menus.json); mirrors UPSMenuComponent::ValidateCatalog."""
    screens = payload.get("Screens")
    if not isinstance(screens, list):
        err(path, "'Screens' must be an array")
        return
    by_id = {}
    for idx, screen in enumerate(screens):
        if not isinstance(screen, dict) or not isinstance(screen.get("ScreenId"), str) or not screen["ScreenId"]:
            err(path, f"Screens[{idx}]: needs a non-empty ScreenId")
            continue
        sid = screen["ScreenId"]
        if sid in by_id:
            err(path, f"Screens[{idx}]: duplicate ScreenId '{sid}'")
        by_id[sid] = screen
    for key in ("RootScreen", "PauseScreen"):
        if payload.get(key) not in by_id:
            err(path, f"{key} '{payload.get(key)}' is not a screen")
    root = by_id.get(payload.get("RootScreen"))
    if root is not None and root.get("bAllowBack", True):
        err(path, f"RootScreen '{payload['RootScreen']}' must set bAllowBack to false")
    loading = payload.get("LoadingScreen")
    if loading:
        if loading not in by_id:
            err(path, f"LoadingScreen '{loading}' is not a screen")
        elif by_id[loading].get("Content") != "Loading":
            err(path, f"LoadingScreen '{loading}' must have Content Loading")
    play_call = payload.get("PlayCallScreen")
    if play_call:
        if play_call not in by_id:
            err(path, f"PlayCallScreen '{play_call}' is not a screen")
        elif by_id[play_call].get("Content") != "PlayCallFormations":
            err(path, f"PlayCallScreen '{play_call}' must have Content PlayCallFormations")
        if not any(screen.get("Content") == "PlayCallPlays" for screen in by_id.values()):
            err(path, "a PlayCallScreen needs a screen with Content PlayCallPlays to list a formation's plays")
    contents = {screen.get("Content") for screen in by_id.values()}
    if "Settings" in contents and "SettingsCategory" not in contents:
        err(path, "a Settings screen needs a screen with Content SettingsCategory to list a category's settings")
    if payload.get("TransitionSeconds", 0) < 0:
        err(path, "TransitionSeconds must not be negative")
    for sid, screen in by_id.items():
        content = screen.get("Content", "Static")
        if content not in MENU_CONTENTS:
            err(path, f"Screen '{sid}': unknown Content '{content}' ({sorted(MENU_CONTENTS)})")
        options = screen.get("Options", [])
        # Generated screens get their options at runtime; Loading is left by its travel.
        if not options and not screen.get("bAllowBack", True) and content == "Static":
            err(path, f"Screen '{sid}' has no options and blocks Back")
        seen = set()
        for option in options:
            oid = option.get("OptionId")
            where = f"Screen '{sid}', option '{oid}'"
            if not oid or oid in seen:
                err(path, f"{where}: empty or duplicate OptionId")
            seen.add(oid)
            command = option.get("Command", "None")
            target = option.get("TargetScreen")
            if command not in MENU_COMMANDS:
                err(path, f"{where}: unknown Command '{command}' ({sorted(MENU_COMMANDS)})")
            if not target and command == "None":
                err(path, f"{where}: needs a TargetScreen or a Command")
            if target and target not in by_id:
                err(path, f"{where}: unknown TargetScreen '{target}'")


def validate_team_identity(path, teams):
    """FPSTeamInfo identity fields (Epic 101 team select): optional, but well-formed if set."""
    for idx, team in enumerate(teams):
        if not isinstance(team, dict):
            continue
        where = f"Teams[{idx}] '{team.get('TeamId')}'"
        for field in ("PrimaryColor", "SecondaryColor"):
            value = team.get(field)
            if value is not None and not (isinstance(value, str) and HEX_COLOR.match(value)):
                err(path, f"{where}.{field}: '{value}' is not #RRGGBB")
        abbr = team.get("Abbreviation")
        if abbr is not None and not (isinstance(abbr, str) and 2 <= len(abbr) <= 4 and abbr.isalnum()):
            err(path, f"{where}.Abbreviation: '{abbr}' must be 2-4 letters or digits")


def validate_loading_tips(path, payload):
    """FPSLoadingTipCatalog (Data/loading_tips.json); mirrors UPSLoadingTips::Validate."""
    tips = payload.get("Tips")
    if not isinstance(tips, list) or not tips:
        err(path, "'Tips' must be a non-empty array")
        return
    if payload.get("MinimumDisplaySeconds", 0) < 0:
        err(path, "MinimumDisplaySeconds must not be negative")
    seen = set()
    for idx, tip in enumerate(tips):
        tid = tip.get("TipId") if isinstance(tip, dict) else None
        where = f"Tips[{idx}] '{tid}'"
        if not tid or tid in seen:
            err(path, f"{where}: empty or duplicate TipId")
        seen.add(tid)
        if not isinstance(tip, dict):
            continue
        if not str(tip.get("Text", "")).strip():
            err(path, f"{where}: empty Text")
        contexts = tip.get("Contexts") or []
        if not contexts:
            err(path, f"{where}: names no context")
        for context in contexts:
            if context not in TIP_CONTEXTS:
                err(path, f"{where}: unknown context '{context}' ({sorted(TIP_CONTEXTS)})")


INPUT_TUNING_FIELDS = ("StickDeadZoneLower", "StickDeadZoneUpper", "StickResponseExponent", "DeviceSwitchAnalogThreshold")


def validate_input_tuning(path, payload):
    """FInputTuningRow (Data/input_tuning.json): numeric fields in range."""
    for field in INPUT_TUNING_FIELDS:
        value = payload.get(field)
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            err(path, f"{field}: expected a number, got {type(value).__name__}")
            return
    extra = set(payload) - set(INPUT_TUNING_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FInputTuningRow exactly")
    lower, upper = payload["StickDeadZoneLower"], payload["StickDeadZoneUpper"]
    if not 0 <= lower < upper <= 1:
        err(path, f"stick dead zone must satisfy 0 <= StickDeadZoneLower ({lower}) < StickDeadZoneUpper ({upper}) <= 1")
    if payload["StickResponseExponent"] <= 0:
        err(path, "StickResponseExponent must be positive")
    if not 0 < payload["DeviceSwitchAnalogThreshold"] <= 1:
        err(path, "DeviceSwitchAnalogThreshold must be in (0, 1]")


FORCE_FEEDBACK_CUES = ("Hit", "Tackle", "Sack", "Catch", "Interception", "Fumble", "Score")
FORCE_FEEDBACK_MOTORS = ("bLeftLarge", "bLeftSmall", "bRightLarge", "bRightSmall")
FORCE_FEEDBACK_ROW_FIELDS = {"Cue", "Intensity", "Duration", "bOnlyWhenInvolved", *FORCE_FEEDBACK_MOTORS}
MAX_CUE_DURATION_SECONDS = 3.0  # UPSForceFeedbackComponent::MaxCueDurationSeconds


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def validate_force_feedback(path, payload):
    """FPSForceFeedbackTuning (Data/force_feedback.json); mirrors UPSForceFeedbackComponent::ValidateTuning."""
    master = payload.get("MasterIntensity")
    if not is_number(master) or not 0 <= master <= 1:
        err(path, f"MasterIntensity: '{master}' must be a number in 0-1")
    cues = payload.get("Cues")
    if not isinstance(cues, list):
        err(path, "'Cues' must be an array")
        return
    seen = set()
    for idx, row in enumerate(cues):
        if not isinstance(row, dict):
            err(path, f"Cues[{idx}]: not an object")
            continue
        cue = row.get("Cue")
        where = f"Cues[{idx}] '{cue}'"
        extra = set(row) - FORCE_FEEDBACK_ROW_FIELDS
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FForceFeedbackTuningRow exactly")
        if cue not in FORCE_FEEDBACK_CUES:
            err(path, f"{where}: unknown Cue ({list(FORCE_FEEDBACK_CUES)})")
        elif cue in seen:
            err(path, f"{where}: more than one pattern for this cue")
        seen.add(cue)
        intensity, duration = row.get("Intensity"), row.get("Duration")
        if not is_number(intensity) or not 0 <= intensity <= 1:
            err(path, f"{where}.Intensity: '{intensity}' must be a number in 0-1")
        if not is_number(duration) or not 0 < duration <= MAX_CUE_DURATION_SECONDS:
            err(path, f"{where}.Duration: '{duration}' must be above 0 and at most {MAX_CUE_DURATION_SECONDS} seconds")
        for field in (*FORCE_FEEDBACK_MOTORS, "bOnlyWhenInvolved"):
            if field in row and not isinstance(row[field], bool):
                err(path, f"{where}.{field}: expected a bool")
        if is_number(intensity) and intensity > 0 and not any(row.get(m, m in ("bLeftLarge", "bRightLarge")) for m in FORCE_FEEDBACK_MOTORS):
            err(path, f"{where}: rumbles no motor")
    for cue in FORCE_FEEDBACK_CUES:
        if cue not in seen:
            err(path, f"Cue '{cue}' has no pattern")


INPUT_DEVICES = ("KeyboardMouse", "Gamepad", "Touch")
TOUCH_KEY = re.compile(r"^Touch\d+$")


def key_device(key):
    if is_gamepad_key(key):
        return "Gamepad"
    return "Touch" if TOUCH_KEY.match(key) else "KeyboardMouse"


def validate_input_glyphs(path, payload, catalog):
    """FPSInputGlyphCatalog (Data/input_glyphs.json); mirrors UPSInputGlyphs::Validate.
    catalog is the parsed input catalog (Data/input_actions.json) or None."""
    sets = payload.get("GlyphSets")
    if not isinstance(sets, list):
        err(path, "'GlyphSets' must be an array")
        return
    action_ids = {a.get("ActionId") for a in (catalog or {}).get("Actions", []) if isinstance(a, dict)}
    set_ids, defaults = set(), {device: [] for device in INPUT_DEVICES}
    for idx, glyph_set in enumerate(sets):
        if not isinstance(glyph_set, dict):
            err(path, f"GlyphSets[{idx}]: not an object")
            continue
        sid = glyph_set.get("GlyphSetId")
        where = f"GlyphSets[{idx}] '{sid}'"
        if not sid or sid in set_ids:
            err(path, f"{where}: empty or duplicate GlyphSetId")
        set_ids.add(sid)
        device = glyph_set.get("Device")
        if device not in INPUT_DEVICES:
            err(path, f"{where}.Device: '{device}' is not an EPSInputDevice ({list(INPUT_DEVICES)})")
        elif glyph_set.get("bDefaultForDevice", False):
            defaults[device].append(glyph_set)
        keys_seen = set()
        for entry in glyph_set.get("Keys", []):
            key = entry.get("Key") if isinstance(entry, dict) else None
            if not key or key in keys_seen:
                err(path, f"{where}: empty or duplicate key '{key}'")
            keys_seen.add(key)
            if key and device in INPUT_DEVICES and key_device(key) != device:
                err(path, f"{where}: key '{key}' is a {key_device(key)} key, not {device}")
            if isinstance(entry, dict) and (not entry.get("GlyphId") or not str(entry.get("Label", "")).strip()):
                err(path, f"{where}, key '{key}': needs a GlyphId and a Label")
        actions_seen = set()
        for entry in glyph_set.get("Actions", []):
            aid = entry.get("ActionId") if isinstance(entry, dict) else None
            if not aid or aid in actions_seen:
                err(path, f"{where}: empty or duplicate action glyph '{aid}'")
            actions_seen.add(aid)
            if isinstance(entry, dict) and (not entry.get("GlyphId") or not str(entry.get("Label", "")).strip()):
                err(path, f"{where}, action '{aid}': needs a GlyphId and a Label")
            if catalog is not None and aid and aid not in action_ids:
                err(path, f"{where}: action glyph for '{aid}', which is not in the input catalog")
    for device, found in defaults.items():
        if len(found) != 1:
            err(path, f"Device '{device}' needs exactly one default glyph set (has {len(found)})")
    if catalog is None:
        return
    reported = set()
    for action in catalog.get("Actions", []):
        for binding in action.get("Bindings", []) if isinstance(action, dict) else []:
            key = binding.get("Key") if isinstance(binding, dict) else None
            if not key or key in reported:
                continue
            found = defaults.get(key_device(key)) or []
            if len(found) != 1 or found[0].get("bFallbackToKeyName", False):
                continue
            if not any(isinstance(e, dict) and e.get("Key") == key for e in found[0].get("Keys", [])):
                err(path, f"Glyph set '{found[0].get('GlyphSetId')}' has no glyph for '{key}', which the input catalog binds to '{action.get('ActionId')}'")
                reported.add(key)


def validate_play_call_tuning(path, payload):
    """FPlayCallTuningRow (Data/play_call.json)."""
    for field in ("CpuSnapDelaySeconds", "QuickCallPlayClockSeconds"):
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number of seconds, 0 or more")
    shown = payload.get("RecentPlaysShown")
    if not isinstance(shown, int) or isinstance(shown, bool) or shown < 1:
        err(path, f"RecentPlaysShown: '{shown}' must be a whole number, 1 or more")
    extra = set(payload) - {"CpuSnapDelaySeconds", "QuickCallPlayClockSeconds", "RecentPlaysShown"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPlayCallTuningRow exactly")


DEFENSIVE_ROLES = {"DefensiveLineman", "Linebacker", "DefensiveBack"}
DEFENSIVE_KINDS = {"PassRush", "Blitz", "RunFit", "ManCoverage", "ZoneCoverage"}


def validate_defensive_adjustments(path, payload):
    """FPSDefensiveAdjustmentCatalog (Data/defensive_adjustments.json); mirrors
    UPSPlayCallSubsystem::ValidateAdjustments."""
    adjustments = payload.get("Adjustments")
    if not isinstance(adjustments, list):
        err(path, "'Adjustments' must be an array")
        return
    seen = set()
    for idx, adjustment in enumerate(adjustments):
        aid = adjustment.get("AdjustmentId") if isinstance(adjustment, dict) else None
        where = f"Adjustments[{idx}] '{aid}'"
        if not aid or aid in seen:
            err(path, f"{where}: empty or duplicate AdjustmentId")
        seen.add(aid)
        if not isinstance(adjustment, dict):
            continue
        if not str(adjustment.get("Label", "")).strip():
            err(path, f"{where}: no Label")
        if adjustment.get("Role") not in DEFENSIVE_ROLES:
            err(path, f"{where}.Role: '{adjustment.get('Role')}' is not a defender ({sorted(DEFENSIVE_ROLES)})")
        if adjustment.get("Kind") not in DEFENSIVE_KINDS:
            err(path, f"{where}.Kind: '{adjustment.get('Kind')}' is not a defensive assignment ({sorted(DEFENSIVE_KINDS)})")


SKILL_AI_FIELDS = ("WaypointArrivalRadius", "OpenSeparation", "AwarenessMisreadSeparation", "MinReadSeconds",
                   "MaxReadSeconds", "PressureRadius", "PressuredThrowSeparation", "HandoffRadius",
                   "HandoffTimeoutSeconds", "CarrierAvoidRadius", "CarrierAvoidWeight", "ThrowLeadSpeed",
                   "BlockSetDistance", "BlockEngageRadius", "FieldHalfWidth", "SidelineCushion", "SidelineSteerWeight",
                   "ReadWindowSeconds", "MaxAnticipationSeconds", "BlownCoverageSeparation")


def validate_skill_ai_tuning(path, payload):
    """FSkillPlayerAITuningRow (Data/skill_ai_tuning.json, Epic 14)."""
    for field in SKILL_AI_FIELDS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    extra = set(payload) - set(SKILL_AI_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FSkillPlayerAITuningRow exactly")
    low, high = payload.get("MinReadSeconds"), payload.get("MaxReadSeconds")
    if is_number(low) and is_number(high) and low > high:
        err(path, f"MinReadSeconds ({low}) must not exceed MaxReadSeconds ({high})")
    if is_number(payload.get("HandoffRadius")) and payload["HandoffRadius"] > 200:
        err(path, "HandoffRadius must be at most 200 (the hand-off's own reach)")
    if is_number(payload.get("ThrowLeadSpeed")) and payload["ThrowLeadSpeed"] <= 0:
        err(path, "ThrowLeadSpeed must be positive")


DEFENDER_AI_FIELDS = ("ArrivalRadius", "ManCushion", "ManAnticipationSeconds", "ZoneRadius", "ZoneShadeWeight",
                      "ContainWidth", "PassDropDepth", "MaxReactionSeconds", "BallHawkRadius",
                      "PumpFakeFreezeSeconds")


def validate_defender_ai_tuning(path, payload):
    """FDefenderAITuningRow (Data/defense_ai_tuning.json)."""
    for field in DEFENDER_AI_FIELDS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    extra = set(payload) - set(DEFENDER_AI_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FDefenderAITuningRow exactly")
    shade = payload.get("ZoneShadeWeight")
    if is_number(shade) and shade > 1:
        err(path, f"ZoneShadeWeight ({shade}) must be between 0 (hold the spot) and 1 (go to the receiver)")


PASSING_INPUT_NUMBERS = ("BulletHoldSeconds", "TouchSpeedScale", "PlacementDepth", "PlacementWidth", "LeadSpeed")


def validate_passing_input(path, payload, catalog):
    """FPassingInputTuningRow (Data/passing_input.json, Epic 104)."""
    for field in PASSING_INPUT_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    extra = set(payload) - set(PASSING_INPUT_NUMBERS) - {"SlotActions", "PumpFakeAction"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPassingInputTuningRow exactly")
    scale = payload.get("TouchSpeedScale")
    if is_number(scale) and not 0 < scale <= 1:
        err(path, f"TouchSpeedScale ({scale}) must be above 0 and at most 1 (a fraction of the passer's arm)")
    if is_number(payload.get("LeadSpeed")) and payload["LeadSpeed"] <= 0:
        err(path, "LeadSpeed must be positive")
    slots = payload.get("SlotActions")
    if not isinstance(slots, list) or not slots or not all(isinstance(a, str) and a for a in slots):
        err(path, "SlotActions must be a non-empty array of action IDs")
        slots = []
    elif len(set(slots)) != len(slots):
        err(path, "SlotActions repeats an action")
    named = list(slots) + [payload.get("PumpFakeAction")]
    if catalog is None:
        return
    actions = {a.get("ActionId"): a for a in catalog.get("Actions", []) if isinstance(a, dict)}
    for action_id in named:
        action = actions.get(action_id)
        if action is None:
            err(path, f"'{action_id}' is not an action in input_actions.json")
        elif action.get("ValueType") != "Boolean" or "Passing" not in (action.get("Contexts") or []):
            err(path, f"'{action_id}' must be a Boolean action in the Passing context")


ENGINE_DEVICE_PROFILES = {"Windows", "Mac", "IOS", "Android", "Linux"}


def project_device_profiles():
    """Profile names declared in Config/DefaultDeviceProfiles.ini ([Name DeviceProfile])."""
    ini = REPO / "Config" / "DefaultDeviceProfiles.ini"
    try:
        text = ini.read_text(encoding="utf-8")
    except OSError:
        return set()
    return set(re.findall(r"^\[(\S+) DeviceProfile\]", text, flags=re.MULTILINE))


# Epic 26's sampler: its rate and per-frame budget are per tier.
TIER_TELEMETRY_NUMBERS = ("TelemetrySampleRateHz", "TelemetrySampleBudgetMs")
# EPSOverlayDetail: how much broadcast overlay a tier draws (Track A).
OVERLAY_DETAILS = {"Full", "Simplified", "Minimal"}


PERF_SYSTEMS = ("Simulation", "AI", "Telemetry", "Overlays", "UI", "Animation", "Crowd", "Audio")


def validate_system_budgets(path, where, tier):
    """A tier's TargetFrameRate and SystemBudgets (Epic 114); mirrors
    PSPlatformTiers::ValidateSystemBudgets."""
    fps = tier.get("TargetFrameRate")
    if not is_number(fps) or fps <= 0:
        err(path, f"{where}.TargetFrameRate: '{fps}' must be a number above 0")
        return
    budgets = tier.get("SystemBudgets")
    if not isinstance(budgets, list):
        err(path, f"{where}.SystemBudgets: must be an array")
        return
    total = 0.0
    by_system = {}
    for idx, budget in enumerate(budgets):
        if not isinstance(budget, dict) or budget.get("System") not in PERF_SYSTEMS:
            err(path, f"{where}.SystemBudgets[{idx}]: System must be one of {list(PERF_SYSTEMS)}")
            continue
        ms = budget.get("BudgetMs")
        if not is_number(ms) or ms < 0:
            err(path, f"{where}.SystemBudgets[{idx}].BudgetMs: '{ms}' must be a number, 0 or more")
            continue
        by_system.setdefault(budget["System"], []).append(ms)
        total += ms
    for system in PERF_SYSTEMS:
        if len(by_system.get(system, [])) != 1:
            err(path, f"{where}.SystemBudgets: needs exactly one budget for {system}")
    frame_ms = 1000.0 / fps
    if total > frame_ms + 1e-6:
        err(path, f"{where}.SystemBudgets: add up to {total:.2f} ms, more than a {fps} fps frame ({frame_ms:.2f} ms)")
    telemetry = by_system.get("Telemetry", [None])[0]
    sample = tier.get("TelemetrySampleBudgetMs")
    if is_number(telemetry) and is_number(sample) and telemetry + 1e-6 < sample:
        err(path, f"{where}.SystemBudgets: the Telemetry budget is below TelemetrySampleBudgetMs, which it includes")


def validate_platform_tiers(path, payload):
    """FPSPlatformTierCatalog (Data/platform_tiers.json, Epic 129); mirrors
    PSPlatformTiers::ValidateCatalog plus the device-profile cross-check."""
    tiers = payload.get("Tiers")
    if not isinstance(tiers, list) or not tiers:
        err(path, "'Tiers' must be a non-empty array")
        return
    profiles = ENGINE_DEVICE_PROFILES | project_device_profiles()
    ids = set()
    for idx, tier in enumerate(tiers):
        where = f"Tiers[{idx}]"
        if not isinstance(tier, dict):
            err(path, f"{where}: must be an object")
            continue
        tier_id = tier.get("TierId")
        if not isinstance(tier_id, str) or not tier_id or tier_id in ids:
            err(path, f"{where}.TierId: empty or used twice")
        ids.add(tier_id)
        if tier.get("DeviceProfile") not in profiles:
            err(path, f"{where}.DeviceProfile: '{tier.get('DeviceProfile')}' is neither an engine profile nor in Config/DefaultDeviceProfiles.ini")
        interval = tier.get("AIDecisionInterval")
        if not is_number(interval) or interval < 0:
            err(path, f"{where}.AIDecisionInterval: '{interval}' must be a number, 0 or more")
        for field in TIER_TELEMETRY_NUMBERS:
            value = tier.get(field)
            if not is_number(value) or value <= 0:
                err(path, f"{where}.{field}: '{value}' must be a number above 0")
        pose_rate = tier.get("ReplayPoseRateHz")
        if not is_number(pose_rate) or pose_rate < 0:
            err(path, f"{where}.ReplayPoseRateHz: '{pose_rate}' must be a number, 0 (every frame) or more")
        art_rate = tier.get("PlayArtRefreshHz")
        if not is_number(art_rate) or art_rate < 0:
            err(path, f"{where}.PlayArtRefreshHz: '{art_rate}' must be a number, 0 (only on events) or more")
        if tier.get("OverlayDetail") not in OVERLAY_DETAILS:
            err(path, f"{where}.OverlayDetail: '{tier.get('OverlayDetail')}' must be one of {sorted(OVERLAY_DETAILS)}")
        for field in ("AudioUpdateHz", "CrowdUpdateHz"):
            rate = tier.get(field)
            if not is_number(rate) or rate < 0:
                err(path, f"{where}.{field}: '{rate}' must be a number, 0 (every frame) or more")
        voices = tier.get("AudioMaxVoices")
        if not isinstance(voices, int) or isinstance(voices, bool) or voices < 1:
            err(path, f"{where}.AudioMaxVoices: '{voices}' must be a whole number, 1 or more")
        validate_system_budgets(path, where, tier)
        extra = set(tier) - {"TierId", "Description", "DeviceProfile", "AIDecisionInterval", "OverlayDetail",
                             "ReplayPoseRateHz", "TargetFrameRate", "SystemBudgets", "PlayArtRefreshHz",
                             "AudioUpdateHz", "AudioMaxVoices", "CrowdUpdateHz", *TIER_TELEMETRY_NUMBERS}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")
    if payload.get("DefaultTier") not in ids:
        err(path, f"DefaultTier '{payload.get('DefaultTier')}' is not a tier")
    for idx, mapping in enumerate(payload.get("Platforms") or []):
        if not isinstance(mapping, dict) or mapping.get("Tier") not in ids or not mapping.get("Platform"):
            err(path, f"Platforms[{idx}]: needs a Platform and a known Tier")


CARRIER_MOVES = {"Juke", "Spin", "Truck", "StiffArm", "Hurdle", "Slide"}
CARRIER_MOVE_ATTRIBUTES = {"Agility", "Strength", "Speed"}
CARRIER_MOVE_NUMBERS = ("MinAttribute", "WindowSeconds", "CommitSeconds", "CooldownSeconds", "StaminaCost",
                        "TackleChanceScale", "SpeedRetained", "LateralSpeed", "ForwardSpeed")


SESSION_TELEMETRY_FIELDS = {
    "FrameTimeBucketMs": "number", "FrameTimeBucketCount": "int", "Percentiles": "list",
    "MinSessionSeconds": "number", "MaxStoredSessions": "int", "CheckpointEveryPlays": "int",
    "CrashBreadcrumbCount": "int",
}


def validate_session_telemetry(path, payload):
    """FPSSessionTelemetryTuning (Data/session_telemetry.json, Epic 117); mirrors
    UPSSessionTelemetrySubsystem::ValidateTuning."""
    extra = set(payload) - set(SESSION_TELEMETRY_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSSessionTelemetryTuning exactly")
    for field, kind in SESSION_TELEMETRY_FIELDS.items():
        value = payload.get(field)
        if kind == "number" and not is_number(value):
            err(path, f"{field}: '{value}' must be a number")
        elif kind == "int" and not (isinstance(value, int) and not isinstance(value, bool)):
            err(path, f"{field}: '{value}' must be a whole number")
        elif kind == "list" and not isinstance(value, list):
            err(path, f"{field}: must be an array")
    if is_number(payload.get("FrameTimeBucketMs")) and payload["FrameTimeBucketMs"] <= 0:
        err(path, "FrameTimeBucketMs must be above 0")
    for field, least in (("FrameTimeBucketCount", 1), ("MaxStoredSessions", 1), ("CheckpointEveryPlays", 0),
                         ("CrashBreadcrumbCount", 0)):
        value = payload.get(field)
        if isinstance(value, int) and value < least:
            err(path, f"{field} must be {least} or more")
    if is_number(payload.get("MinSessionSeconds")) and payload["MinSessionSeconds"] < 0:
        err(path, "MinSessionSeconds must not be negative")
    percentiles = payload.get("Percentiles")
    if isinstance(percentiles, list):
        if not percentiles:
            err(path, "Percentiles must name at least one percentile")
        for value in percentiles:
            if not is_number(value) or not 0 < value <= 100:
                err(path, f"Percentiles: '{value}' must be a number in (0, 100]")


OFFENSIVE_ROLES = PLAYER_ROLES - {"DefensiveLineman", "Linebacker", "DefensiveBack"}
PLAYERS_PER_SIDE = 11
PERSONNEL_FIELDS = {"PackageId", "DisplayName", "bOffense", "RoleCounts", "Formations"}


def validate_personnel_catalog(path, payload):
    """FPSPersonnelCatalog (Data/personnel_packages.json, Epic 19.5); mirrors
    UPSPersonnelManager::ValidateCatalog."""
    packages = payload.get("Packages")
    if not isinstance(packages, list) or not packages:
        err(path, "'Packages' must be a non-empty array")
        return
    threshold = payload.get("FatigueSubstitutionThreshold", 0.3)
    if not is_number(threshold) or not 0 <= threshold <= 1:
        err(path, f"FatigueSubstitutionThreshold: '{threshold}' must be a number from 0 to 1")
    extra = set(payload) - {"DefaultOffensePackage", "DefaultDefensePackage", "FatigueSubstitutionThreshold", "Packages"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSPersonnelCatalog exactly")

    sides = {}
    owners = {True: {}, False: {}}
    for idx, package in enumerate(packages):
        if not isinstance(package, dict):
            err(path, f"Packages[{idx}]: not an object")
            continue
        pid = package.get("PackageId")
        where = f"Packages[{idx}] '{pid}'"
        if not isinstance(pid, str) or not pid:
            err(path, f"{where}: empty PackageId")
        elif pid in sides:
            err(path, f"{where}: duplicate PackageId")
        offense = package.get("bOffense", True)
        if not isinstance(offense, bool):
            err(path, f"{where}.bOffense: must be true or false")
            offense = True
        sides[pid] = offense
        extra = set(package) - PERSONNEL_FIELDS
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPSPersonnelPackage exactly")
        if not str(package.get("DisplayName", "")).strip():
            err(path, f"{where}: no DisplayName")

        counts = package.get("RoleCounts")
        if not isinstance(counts, dict):
            err(path, f"{where}.RoleCounts: must be an object of role -> count")
            counts = {}
        total = 0
        for role, count in counts.items():
            if role not in PLAYER_ROLES:
                err(path, f"{where}.RoleCounts: '{role}' is not a valid EPlayerRole")
                continue
            if (role in OFFENSIVE_ROLES) != offense:
                err(path, f"{where}.RoleCounts: {role} doesn't play on {'offense' if offense else 'defense'}")
            if isinstance(count, bool) or not isinstance(count, int) or count < 0:
                err(path, f"{where}.RoleCounts.{role}: '{count}' must be a whole number, 0 or more")
                continue
            total += count
        if total != PLAYERS_PER_SIDE:
            err(path, f"{where}: fields {total} players, not {PLAYERS_PER_SIDE}")
        if offense and (counts.get("Quarterback", 0) < 1 or counts.get("OffensiveLineman", 0) < 1):
            err(path, f"{where}: an offense needs a Quarterback and an OffensiveLineman to snap to him")

        formations = package.get("Formations", [])
        if not isinstance(formations, list):
            err(path, f"{where}.Formations: must be an array of formation names")
            continue
        for formation in formations:
            if not isinstance(formation, str) or not formation.strip():
                err(path, f"{where}.Formations: empty formation name")
                continue
            # FString keys compare case-insensitively in the engine.
            key = formation.lower()
            if key in owners[offense]:
                err(path, f"Formation '{formation}' brings on both {owners[offense][key]} and {pid}")
            else:
                owners[offense][key] = pid

    for field, offense in (("DefaultOffensePackage", True), ("DefaultDefensePackage", False)):
        default = payload.get(field)
        if sides.get(default) is not offense:
            err(path, f"{field}: '{default}' is not {'an offensive' if offense else 'a defensive'} package")


def validate_carrier_moves(path, payload, catalog):
    """FPSCarrierMoveCatalog (Data/carrier_moves.json, Epic 104.2); mirrors
    UPSCarrierMoveComponent::ValidateCatalog plus the catalog cross-check."""
    moves = payload.get("Moves")
    if not isinstance(moves, list):
        err(path, "'Moves' must be an array")
        return
    actions = {a.get("ActionId"): a for a in (catalog or {}).get("Actions", []) if isinstance(a, dict)}
    seen_moves, seen_actions = set(), set()
    for idx, row in enumerate(moves):
        where = f"Moves[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        move = row.get("Move")
        if move not in CARRIER_MOVES:
            err(path, f"{where}.Move: '{move}' is not an EPSCarrierMove ({sorted(CARRIER_MOVES)})")
        elif move in seen_moves:
            err(path, f"{where}.Move: '{move}' is defined twice")
        seen_moves.add(move)
        action_id = row.get("ActionId")
        if not isinstance(action_id, str) or not action_id or action_id in seen_actions:
            err(path, f"{where}.ActionId: empty or used twice")
        seen_actions.add(action_id)
        if catalog is not None:
            action = actions.get(action_id)
            if action is None:
                err(path, f"{where}.ActionId: '{action_id}' is not an action in input_actions.json")
            elif action.get("ValueType") != "Boolean" or "BallCarrier" not in (action.get("Contexts") or []):
                err(path, f"{where}.ActionId: '{action_id}' must be a Boolean action in the BallCarrier context")
        if row.get("Attribute") not in CARRIER_MOVE_ATTRIBUTES:
            err(path, f"{where}.Attribute: '{row.get('Attribute')}' must be one of {sorted(CARRIER_MOVE_ATTRIBUTES)}")
        for field in CARRIER_MOVE_NUMBERS:
            value = row.get(field)
            if not is_number(value) or value < 0:
                err(path, f"{where}.{field}: '{value}' must be a number, 0 or more")
        if is_number(row.get("MinAttribute")) and row["MinAttribute"] > 100:
            err(path, f"{where}.MinAttribute: ratings run 0-100")
        if is_number(row.get("SpeedRetained")) and row["SpeedRetained"] > 1:
            err(path, f"{where}.SpeedRetained: at most 1 (a move never adds speed this way)")
        if not isinstance(row.get("bGivesUp"), bool):
            err(path, f"{where}.bGivesUp: must be true or false")
        extra = set(row) - set(CARRIER_MOVE_NUMBERS) - {"Move", "ActionId", "Attribute", "bGivesUp"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")


TELEMETRY_BUS_HEADER = REPO / "Source" / "PlaySports" / "Public" / "PSTelemetryBus.h"
TELEMETRY_SAMPLING_NUMBERS = ("HistorySeconds", "RecoverBelowFraction")
TELEMETRY_SAMPLING_INTS = ("DegradeAfterSamples", "RecoverAfterSamples", "MaxDegradeLevel")
TELEMETRY_MAX_RING_FRAMES = 10000  # PSTelemetrySamplingPrivate::MaxRingFrames
TELEMETRY_MAX_DEGRADE_LEVEL = 8  # PSTelemetrySamplingPrivate::MaxAllowedDegradeLevel


def telemetry_event_types():
    """EPSTelemetryEventType's names, read from the bus header so new events need no edit here;
    None when the header can't be read."""
    try:
        text = TELEMETRY_BUS_HEADER.read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r"enum\s+class\s+EPSTelemetryEventType\s*:\s*uint8\s*\{(.*?)\}", text, re.S)
    if not match:
        return None
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", match.group(1), flags=re.S)
    names = set()
    for entry in body.split(","):
        name = re.sub(r"UMETA\(.*?\)", "", entry).split("=")[0].strip()
        if name:
            names.add(name)
    return names


def fastest_tier_sample_rate():
    """The highest TelemetrySampleRateHz in platform_tiers.json, or None when unreadable (its own
    checks report that)."""
    try:
        tiers = json.loads((DATA_DIR / "platform_tiers.json").read_text(encoding="utf-8")).get("Tiers") or []
    except (OSError, json.JSONDecodeError, UnicodeDecodeError, AttributeError):
        return None
    rates = [t.get("TelemetrySampleRateHz") for t in tiers if isinstance(t, dict)]
    rates = [r for r in rates if is_number(r) and r > 0]
    return max(rates) if rates else None


def validate_telemetry_sampling(path, payload):
    """FPSTelemetrySamplingTuning (Data/telemetry_sampling.json, Epic 26); mirrors
    UPSTelemetrySamplingSubsystem::ValidateTuning."""
    for field in TELEMETRY_SAMPLING_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    for field in TELEMETRY_SAMPLING_INTS:
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool):
            err(path, f"{field}: '{value}' must be a whole number")
    if is_number(payload.get("RecoverBelowFraction")) and payload["RecoverBelowFraction"] > 1:
        err(path, "RecoverBelowFraction must be at most 1 (a fraction of the budget)")
    for field in ("DegradeAfterSamples", "RecoverAfterSamples"):
        value = payload.get(field)
        if isinstance(value, int) and not isinstance(value, bool) and value < 1:
            err(path, f"{field}: must be 1 or more")
    level = payload.get("MaxDegradeLevel")
    if isinstance(level, int) and not isinstance(level, bool) and not 0 <= level <= TELEMETRY_MAX_DEGRADE_LEVEL:
        err(path, f"MaxDegradeLevel: must be 0 to {TELEMETRY_MAX_DEGRADE_LEVEL}")
    rate, history = fastest_tier_sample_rate(), payload.get("HistorySeconds")
    if rate is not None and is_number(history) and history > 0 and rate * history > TELEMETRY_MAX_RING_FRAMES:
        err(path, f"HistorySeconds x the fastest tier's TelemetrySampleRateHz ({rate * history:g}) must be at most "
                  f"{TELEMETRY_MAX_RING_FRAMES} frames")
    events = payload.get("KeyframeEvents")
    if not isinstance(events, list):
        err(path, "'KeyframeEvents' must be an array of event type names")
    else:
        known = telemetry_event_types()
        seen = set()
        for idx, name in enumerate(events):
            if known is not None and name not in known:
                err(path, f"KeyframeEvents[{idx}]: '{name}' is not an EPSTelemetryEventType ({sorted(known)})")
            elif name in seen:
                err(path, f"KeyframeEvents[{idx}]: '{name}' is listed twice")
            seen.add(name)
    for field in ("SampleRateHz", "SampleBudgetMs"):
        if field in payload:
            err(path, f"{field}: set per tier, as Telemetry{field} in platform_tiers.json")
    extra = (set(payload) - set(TELEMETRY_SAMPLING_NUMBERS) - set(TELEMETRY_SAMPLING_INTS) - {"KeyframeEvents"}
             - {"SampleRateHz", "SampleBudgetMs"})
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSTelemetrySamplingTuning exactly")


REPLAY_FIELDS = ("PreRollSeconds", "PostRollSeconds", "PlaybackRates", "ScrubSecondsPerSecond", "SaveFrameRateHz",
                 "Cameras", "FreeCamDistanceCm", "FreeCamMinDistanceCm", "FreeCamMaxDistanceCm", "FreeCamPitchDegrees",
                 "FreeCamOrbitDegreesPerSecond", "FreeCamZoomCmPerSecond", "bAutoReplay", "AutoReplayDelaySeconds",
                 "AutoReplayHoldSeconds", "AutoReplays", "ReducedMotionCamera")
# The replay cameras that aren't all-22 rigs (UPSReplaySubsystem::DirectorCamera, ...).
REPLAY_NAMED_CAMERAS = {"Director", "Skycam", "Free"}
REPLAY_TRIGGERS = {"Score", "Turnover"}


def validate_replay_tuning(path, payload, rig_ids):
    """FPSReplayTuning (Data/replay.json, Epic 41); mirrors UPSReplaySubsystem::ValidateTuning,
    with each camera that isn't Director, Skycam or Free a rig of camera_all22.json."""
    for field in ("PreRollSeconds", "PostRollSeconds", "AutoReplayDelaySeconds", "AutoReplayHoldSeconds"):
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in ("SaveFrameRateHz", "ScrubSecondsPerSecond", "FreeCamOrbitDegreesPerSecond", "FreeCamZoomCmPerSecond"):
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    rates = payload.get("PlaybackRates")
    if not isinstance(rates, list) or not rates:
        err(path, "'PlaybackRates' must be a non-empty array of speeds")
    else:
        for idx, rate in enumerate(rates):
            if not is_number(rate) or not 0 < rate <= 1:
                err(path, f"PlaybackRates[{idx}]: '{rate}' must be above 0 and at most 1")
            elif rates.index(rate) != idx:
                err(path, f"PlaybackRates[{idx}]: {rate} is listed twice")
        if rates[0] != 1:
            err(path, f"PlaybackRates[0]: '{rates[0]}' must be 1, the speed a replay starts at")

    def is_rig(name):
        return name not in REPLAY_NAMED_CAMERAS and (rig_ids is None or name in rig_ids)

    cameras = payload.get("Cameras")
    if not isinstance(cameras, list) or not cameras:
        err(path, "'Cameras' must be a non-empty array of camera names")
        cameras = []
    for idx, name in enumerate(cameras):
        if not isinstance(name, str) or not name:
            err(path, f"Cameras[{idx}]: must be a camera name")
        elif cameras.index(name) != idx:
            err(path, f"Cameras[{idx}]: '{name}' is listed twice")
        elif name not in REPLAY_NAMED_CAMERAS and not is_rig(name):
            err(path, f"Cameras[{idx}]: '{name}' is neither {sorted(REPLAY_NAMED_CAMERAS)} nor a RigId in camera_all22.json")
    reduced = payload.get("ReducedMotionCamera")
    if reduced not in cameras or not is_rig(reduced):
        err(path, f"ReducedMotionCamera: '{reduced}' must be a still all-22 rig listed in Cameras")

    low, mid, high = (payload.get(f) for f in ("FreeCamMinDistanceCm", "FreeCamDistanceCm", "FreeCamMaxDistanceCm"))
    if not all(is_number(v) for v in (low, mid, high)) or not 0 < low <= mid <= high:
        err(path, "free camera distances must be 0 < FreeCamMinDistanceCm <= FreeCamDistanceCm <= FreeCamMaxDistanceCm")
    pitch = payload.get("FreeCamPitchDegrees")
    if not is_number(pitch) or not 0 < pitch < 90:
        err(path, f"FreeCamPitchDegrees: '{pitch}' must be above 0 and below 90")
    if not isinstance(payload.get("bAutoReplay"), bool):
        err(path, "bAutoReplay must be true or false")

    rules = payload.get("AutoReplays")
    if not isinstance(rules, list):
        err(path, "'AutoReplays' must be an array of rules")
        rules = []
    seen = set()
    for idx, rule in enumerate(rules):
        where = f"AutoReplays[{idx}]"
        if not isinstance(rule, dict):
            err(path, f"{where}: must be an object")
            continue
        trigger = rule.get("Trigger")
        if trigger not in REPLAY_TRIGGERS:
            err(path, f"{where}.Trigger: '{trigger}' is not an EPSReplayTrigger ({sorted(REPLAY_TRIGGERS)})")
        elif trigger in seen:
            err(path, f"{where}.Trigger: '{trigger}' already has a rule")
        seen.add(trigger)
        if rule.get("Shot") not in DIRECTOR_SHOTS:
            err(path, f"{where}.Shot: '{rule.get('Shot')}' is not an EPSDirectorShot ({list(DIRECTOR_SHOTS)})")
        rate = rule.get("PlaybackRate")
        if not is_number(rate) or not 0 < rate <= 1:
            err(path, f"{where}.PlaybackRate: '{rate}' must be above 0 and at most 1")
        extra = set(rule) - {"Trigger", "Shot", "PlaybackRate"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")

    if "ReplayPoseRateHz" in payload:
        err(path, "ReplayPoseRateHz: set per tier, in platform_tiers.json")
    extra = set(payload) - set(REPLAY_FIELDS) - {"ReplayPoseRateHz"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSReplayTuning exactly")


HIGHLIGHT_WEIGHTS = ("YardWeight", "PointsWeight", "TurnoverWeight", "BrokenTackleWeight", "WinProbabilityWeight",
                     "MinImportance")
HIGHLIGHT_TIMES = ("BeatLeadSeconds", "BeatSeconds", "ClipGapSeconds", "GameEndReelDelaySeconds")
HIGHLIGHT_KINDS = ("Score", "Turnover", "BigPlay")
WIN_PROBABILITY_FIELDS = ("MarginScale", "PossessionPoints", "TimeFloor", "GameSeconds", "QuarterSeconds")


def validate_highlights(path, payload):
    """FPSHighlightTuning (Data/highlights.json, Epic 42); mirrors UPSHighlightSubsystem::ValidateTuning."""
    for field in HIGHLIGHT_WEIGHTS + HIGHLIGHT_TIMES:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in ("ReelSize", "SeasonHighlightsKept"):
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 1:
            err(path, f"{field}: '{value}' must be a whole number, 1 or more")
    rate = payload.get("BeatPlaybackRate")
    if not is_number(rate) or not 0 < rate <= 1:
        err(path, f"BeatPlaybackRate: '{rate}' must be above 0 and at most 1")
    settle = payload.get("SettleAfterWhistleSeconds")
    if not is_number(settle) or settle <= 0:
        err(path, f"SettleAfterWhistleSeconds: '{settle}' must be above 0")
    if not isinstance(payload.get("bPlayReelAtGameEnd"), bool):
        err(path, "bPlayReelAtGameEnd must be true or false")
    shots = payload.get("KindShots")
    if not isinstance(shots, list):
        err(path, "'KindShots' must be an array")
        shots = []
    kinds = [s.get("Kind") for s in shots if isinstance(s, dict)]
    for kind in HIGHLIGHT_KINDS:
        if kinds.count(kind) != 1:
            err(path, f"KindShots: '{kind}' must have exactly one shot")
    for idx, shot in enumerate(shots):
        if not isinstance(shot, dict) or shot.get("Kind") not in HIGHLIGHT_KINDS:
            err(path, f"KindShots[{idx}]: needs a Kind of {list(HIGHLIGHT_KINDS)}")
        elif shot.get("Shot") not in DIRECTOR_SHOTS:
            err(path, f"KindShots[{idx}].Shot: '{shot.get('Shot')}' is not an EPSDirectorShot ({list(DIRECTOR_SHOTS)})")
    win = payload.get("WinProbability")
    if not isinstance(win, dict):
        err(path, "'WinProbability' must be an object")
    else:
        for field in WIN_PROBABILITY_FIELDS:
            value = win.get(field)
            low_ok = value >= 0 if field == "PossessionPoints" and is_number(value) else (is_number(value) and value > 0)
            if not is_number(value) or not low_ok:
                err(path, f"WinProbability.{field}: '{value}' must be a number above 0" + (" (or 0)" if field == "PossessionPoints" else ""))
        extra = set(win) - set(WIN_PROBABILITY_FIELDS)
        if extra:
            err(path, f"WinProbability: unknown field(s) {sorted(extra)}")
    extra = set(payload) - set(HIGHLIGHT_WEIGHTS) - set(HIGHLIGHT_TIMES) - {
        "ReelSize", "SeasonHighlightsKept", "BeatPlaybackRate", "SettleAfterWhistleSeconds", "bPlayReelAtGameEnd",
        "KindShots", "WinProbability"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSHighlightTuning exactly")



GAME_INTELLIGENCE_COUNTS = ("MaxOpenRequests", "MaxAnswerChars", "MaxKeyPlays")
GAME_INTELLIGENCE_TASKS = ("PlayCallTask", "DriveSummaryTask", "GameAnalysisTask")
GAME_INTELLIGENCE_TEXTS = ("PlayCallInstructions", "DriveSummaryInstructions", "GameAnalysisInstructions")
GAME_STATE_MIN_BUDGET = 1024  # PSGameStateSerializer::MinBudgetChars
ROUTING_TABLE = REPO / "tools" / "orchestrator" / "routing.json"


def validate_game_intelligence(path, payload):
    """FPSGameIntelligenceTuning (Data/game_intelligence.json, Epic 82); mirrors
    UPSGameIntelligenceSubsystem::ValidateTuning, and each task against Epic 119's routing table."""
    budget = payload.get("ContextBudgetChars")
    if not isinstance(budget, int) or isinstance(budget, bool) or budget < GAME_STATE_MIN_BUDGET:
        err(path, f"ContextBudgetChars: '{budget}' must be a whole number, at least {GAME_STATE_MIN_BUDGET}")
    timeout = payload.get("PlayCallTimeoutSeconds")
    if not is_number(timeout) or timeout <= 0:
        err(path, f"PlayCallTimeoutSeconds: '{timeout}' must be above 0")
    for field in GAME_INTELLIGENCE_COUNTS:
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 1:
            err(path, f"{field}: '{value}' must be a whole number, 1 or more")
    leaders = payload.get("LeadersPerCategory")
    if not isinstance(leaders, int) or isinstance(leaders, bool) or leaders < 0:
        err(path, f"LeadersPerCategory: '{leaders}' must be a whole number, 0 or more")
    if not isinstance(payload.get("bPostGameRequests"), bool):
        err(path, "bPostGameRequests must be true or false")
    for field in GAME_INTELLIGENCE_TEXTS:
        if not isinstance(payload.get(field), str) or not payload.get(field).strip():
            err(path, f"{field} must be a non-empty string")
    try:
        routes = json.loads(ROUTING_TABLE.read_text(encoding="utf-8")).get("tasks", {})
    except (OSError, ValueError) as error:
        err(path, f"can't read the model router's tasks from {ROUTING_TABLE.relative_to(REPO)}: {error}")
        routes = None
    for field in GAME_INTELLIGENCE_TASKS:
        task = payload.get(field)
        if not isinstance(task, str) or not task:
            err(path, f"{field} must name a model-router task")
        elif routes is not None and task not in routes:
            err(path, f"{field}: '{task}' is not a task in tools/orchestrator/routing.json ({sorted(routes)})")
    if routes is not None and all(payload.get(f) in routes for f in ("PlayCallTask", "DriveSummaryTask")):
        play_call = routes[payload["PlayCallTask"]].get("min_capability", 0)
        summary = routes[payload["DriveSummaryTask"]].get("min_capability", 0)
        if play_call < summary:
            err(path, f"PlayCallTask '{payload['PlayCallTask']}' routes to weaker models (min_capability {play_call}) "
                      f"than DriveSummaryTask '{payload['DriveSummaryTask']}' ({summary}): strategy needs the better ones")
    extra = set(payload) - {"ContextBudgetChars", "PlayCallTimeoutSeconds", "LeadersPerCategory", "bPostGameRequests"} \
        - set(GAME_INTELLIGENCE_COUNTS) - set(GAME_INTELLIGENCE_TASKS) - set(GAME_INTELLIGENCE_TEXTS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSGameIntelligenceTuning exactly")



STORYLINE_KINDS = ("WinStreak", "LosingStreak", "RookieSurge", "RevengeGame", "RecordBroken", "AwardRace", "Trade")
STAT_CATEGORIES = ("PassingYards", "PassingTouchdowns", "Completions", "InterceptionsThrown", "RushingYards",
                   "RushingTouchdowns", "Receptions", "ReceivingYards", "ReceivingTouchdowns", "Tackles", "Sacks",
                   "Interceptions", "TeamPoints", "TeamTotalYards")
NARRATIVE_COUNTS = ("RookieSurgeTopN", "AwardRaceFromWeek", "MaxDigestItems", "DigestsKept", "VoterCount")


def validate_narrative(path, payload):
    """FPSNarrativeTuning (Data/league_narrative.json, Epic 93); mirrors UPSLeagueNarrative::ValidateTuning."""
    def whole(value):
        return isinstance(value, int) and not isinstance(value, bool)

    if not whole(payload.get("StreakMin")) or payload.get("StreakMin") < 2:
        err(path, f"StreakMin: '{payload.get('StreakMin')}' must be a whole number, 2 or more")
    for field in NARRATIVE_COUNTS:
        if not whole(payload.get(field)) or payload.get(field) < 1:
            err(path, f"{field}: '{payload.get(field)}' must be a whole number, 1 or more")
    if not whole(payload.get("MaxBroadcastStorylines")) or payload.get("MaxBroadcastStorylines") < 0:
        err(path, f"MaxBroadcastStorylines: '{payload.get('MaxBroadcastStorylines')}' must be a whole number, 0 or more")
    margin = payload.get("AwardRaceMargin")
    if not is_number(margin) or not 0 <= margin <= 1:
        err(path, f"AwardRaceMargin: '{margin}' must be 0 to 1")
    noise = payload.get("VoterNoise")
    if not is_number(noise) or not 0 <= noise < 1:
        err(path, f"VoterNoise: '{noise}' must be 0 or more, under 1")
    if not is_number(payload.get("MvpWinWeight")) or payload.get("MvpWinWeight") < 0:
        err(path, f"MvpWinWeight: '{payload.get('MvpWinWeight')}' must be a number, 0 or more")
    if not whole(payload.get("VotingSeed")):
        err(path, "VotingSeed must be a whole number")
    kinds = payload.get("StorylineKinds")
    if not isinstance(kinds, list):
        err(path, "'StorylineKinds' must be an array")
        kinds = []
    for idx, entry in enumerate(kinds):
        if not isinstance(entry, dict) or entry.get("Kind") not in STORYLINE_KINDS:
            err(path, f"StorylineKinds[{idx}].Kind must be one of {list(STORYLINE_KINDS)}")
        elif not is_number(entry.get("Weight")) or entry.get("Weight") < 0:
            err(path, f"StorylineKinds[{idx}].Weight: '{entry.get('Weight')}' must be a number, 0 or more")
    named = [entry.get("Kind") for entry in kinds if isinstance(entry, dict)]
    for kind in STORYLINE_KINDS:
        if named.count(kind) != 1:
            err(path, f"StorylineKinds: '{kind}' must have exactly one entry")
    for field in ("OffenseScoring", "DefenseScoring"):
        weights = payload.get(field)
        if not isinstance(weights, list) or not weights:
            err(path, f"'{field}' must be a non-empty array")
            continue
        for idx, entry in enumerate(weights):
            if not isinstance(entry, dict) or entry.get("Category") not in STAT_CATEGORIES:
                err(path, f"{field}[{idx}].Category must be an EPSStatCategory ({list(STAT_CATEGORIES)})")
            elif not is_number(entry.get("Weight")):
                err(path, f"{field}[{idx}].Weight must be a number")
    ballot = payload.get("BallotPoints")
    if not isinstance(ballot, list) or not ballot or not all(whole(p) and p > 0 for p in ballot):
        err(path, "BallotPoints must be a non-empty array of whole numbers above 0")
    elif any(later > earlier for earlier, later in zip(ballot, ballot[1:])):
        err(path, f"BallotPoints {ballot}: a lower place can't be worth more")
    if not isinstance(payload.get("DigestInstructions"), str) or not payload.get("DigestInstructions").strip():
        err(path, "DigestInstructions must be a non-empty string")
    if not whole(payload.get("DigestContextChars")) or payload.get("DigestContextChars") < 512:
        err(path, f"DigestContextChars: '{payload.get('DigestContextChars')}' must be a whole number, 512 or more")
    task = payload.get("DigestTask")
    try:
        routes = json.loads(ROUTING_TABLE.read_text(encoding="utf-8")).get("tasks", {})
    except (OSError, ValueError):
        routes = None
    if not isinstance(task, str) or not task:
        err(path, "DigestTask must name a model-router task")
    elif routes is not None and task not in routes:
        err(path, f"DigestTask: '{task}' is not a task in tools/orchestrator/routing.json ({sorted(routes)})")
    known = {"StreakMin", "AwardRaceMargin", "StorylineKinds", "MaxBroadcastStorylines", "OffenseScoring", "DefenseScoring",
             "MvpWinWeight", "BallotPoints", "VoterNoise", "VotingSeed", "DigestTask", "DigestInstructions",
             "DigestContextChars"} | set(NARRATIVE_COUNTS)
    extra = set(payload) - known
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSNarrativeTuning exactly")


TRADE_STANCES = ("Contender", "Balanced", "Rebuilder")
TRADE_POSITIVE = ("MaxPlayerValue", "TalentCurveExponent", "CounterRatio", "AcceptRatio", "DeadlineFraction")
TRADE_NON_NEGATIVE = ("RoleWeightExponent", "SurplusValuePerCap", "NeedValueWeight", "LastPickValue", "LopsidedMinGap",
                      "DeadlineBuyerPremium", "MinTargetGain")
TRADE_FRACTIONS = ("UncontrolledYearWeight", "TradeRequestDiscount", "ContenderWinPercentage", "RebuilderWinPercentage",
                   "FuturePickDiscount", "MaxValueImbalance", "DeadlineFraction", "BaseTradeChance", "DeadlineTradeChance",
                   "DeadlineSellerDiscount")
TRADE_COUNTS = {"ValueHorizonYears": 1, "TradablePickYears": 1, "MaxAssetsPerSide": 1, "MaxTradesPerTeamPerSeason": 1,
                "DeadlineRampWeeks": 1, "TargetsPerAttempt": 1, "MinGamesForStance": 0, "RetradeCooldownWeeks": 0,
                "MinPlayersAtRole": 0, "MaxOffersToUserPerWeek": 0, "RandomSeed": None}


def validate_trades(path, payload):
    """FPSTradeTuning (Data/trades.json, Epic 88); mirrors UPSTradeMarket::ValidateTuning."""
    for field in TRADE_POSITIVE:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    for field in TRADE_NON_NEGATIVE:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in TRADE_FRACTIONS:
        value = payload.get(field)
        if not is_number(value) or not 0 <= value <= 1:
            err(path, f"{field}: '{value}' must be a number from 0 to 1")
    for field, floor in TRADE_COUNTS.items():
        value = payload.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or (floor is not None and value < floor):
            err(path, f"{field}: '{value}' must be a whole number" + (f", {floor} or more" if floor is not None else ""))
    contender, rebuilder = payload.get("ContenderWinPercentage"), payload.get("RebuilderWinPercentage")
    if is_number(contender) and is_number(rebuilder) and rebuilder >= contender:
        err(path, f"RebuilderWinPercentage ({rebuilder}) must be under ContenderWinPercentage ({contender})")
    accept, counter = payload.get("AcceptRatio"), payload.get("CounterRatio")
    if is_number(accept) and is_number(counter) and counter > accept:
        err(path, f"CounterRatio ({counter}) must be no more than AcceptRatio ({accept})")

    stances = payload.get("Stances")
    if not isinstance(stances, list):
        err(path, "'Stances' must be an array of { Stance, FutureYearWeight, PickMultiplier }")
        stances = []
    for idx, entry in enumerate(stances):
        where = f"Stances[{idx}]"
        if not isinstance(entry, dict) or entry.get("Stance") not in TRADE_STANCES:
            err(path, f"{where}.Stance must be one of {list(TRADE_STANCES)}")
            continue
        weight, multiplier = entry.get("FutureYearWeight"), entry.get("PickMultiplier")
        if not is_number(weight) or not 0 <= weight <= 1:
            err(path, f"{where}.FutureYearWeight: '{weight}' must be a number from 0 to 1")
        if not is_number(multiplier) or multiplier <= 0:
            err(path, f"{where}.PickMultiplier: '{multiplier}' must be a number above 0")
        if set(entry) - {"Stance", "FutureYearWeight", "PickMultiplier"}:
            err(path, f"{where}: unknown field(s) {sorted(set(entry) - {'Stance', 'FutureYearWeight', 'PickMultiplier'})} - names must match FPSTradeStanceTuning exactly")
    named = [entry.get("Stance") for entry in stances if isinstance(entry, dict)]
    for stance in TRADE_STANCES:
        if named.count(stance) != 1:
            err(path, f"Stances: '{stance}' must have exactly one entry")

    chart = payload.get("PickRoundValues")
    if not isinstance(chart, list) or not chart or not all(is_number(value) and value > 0 for value in chart):
        err(path, "PickRoundValues must be a non-empty array of numbers above 0")
    elif any(later > earlier for earlier, later in zip(chart, chart[1:])):
        err(path, f"PickRoundValues {chart}: a later round's first pick can't be worth more")
    elif is_number(payload.get("LastPickValue")) and payload["LastPickValue"] > chart[-1]:
        err(path, f"LastPickValue ({payload['LastPickValue']}) must be no more than the last round's first pick ({chart[-1]})")

    known = set(TRADE_POSITIVE) | set(TRADE_NON_NEGATIVE) | set(TRADE_FRACTIONS) | set(TRADE_COUNTS) | {"Stances", "PickRoundValues"}
    if set(payload) - known:
        err(path, f"unknown field(s) {sorted(set(payload) - known)} - names must match FPSTradeTuning exactly")


SOURCE_PUBLIC = REPO / "Source" / "PlaySports" / "Public"


def header_enum(header, enum_name):
    """The names of UENUM enum_name in Source/PlaySports/Public/<header>, in order, so a new value
    needs no edit here; None when the header can't be read."""
    try:
        text = (SOURCE_PUBLIC / header).read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r"enum\s+class\s+" + enum_name + r"\s*:\s*uint8\s*\{(.*?)\}", text, re.S)
    if not match:
        return None
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", match.group(1), flags=re.S)
    names = []
    for entry in body.split(","):
        name = re.sub(r"UMETA\(.*?\)", "", entry).split("=")[0].strip()
        if name:
            names.append(name)
    return names


def whole_number(value):
    return isinstance(value, int) and not isinstance(value, bool)


AUDIO_CUE_FIELDS = {"CueId", "Layer", "SoundPath", "Volume", "Priority", "CooldownSeconds", "DurationSeconds", "bLoop",
                    "LoopGroup", "bSpatial", "bScaleByIntensity"}
AUDIO_TUNING_FIELDS = {"Cues", "EventCues", "LayerSettings", "StartupLoops", "BigHitDamage", "FullIntensityDamage",
                       "DeepPassCm", "MaxRequestsKept"}


def percent_sliders():
    """The 0-100 sliders in ui_settings.json, or None when it can't be read (its own checks say why)."""
    try:
        settings = json.loads((DATA_DIR / "ui_settings.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return {s.get("SettingId") for s in settings.get("Settings", []) if isinstance(s, dict)
            and s.get("Kind") == "Slider" and s.get("Min") == 0 and s.get("Max") == 100}


def validate_audio_cues(path, payload):
    """FPSAudioTuning (Data/audio_cues.json, Epic 23.1); mirrors UPSAudioSubsystem::ValidateTuning,
    plus the volume settings' sliders and the enums' names."""
    layers = header_enum("PSAudioTypes.h", "EPSAudioLayer") or []
    triggers = header_enum("PSAudioTypes.h", "EPSAudioTrigger") or []
    for field in ("BigHitDamage", "FullIntensityDamage", "DeepPassCm"):
        if not is_number(payload.get(field)) or payload.get(field) <= 0:
            err(path, f"{field}: '{payload.get(field)}' must be a number above 0")
    if not whole_number(payload.get("MaxRequestsKept")) or payload.get("MaxRequestsKept") < 1:
        err(path, f"MaxRequestsKept: '{payload.get('MaxRequestsKept')}' must be a whole number, 1 or more")
    cues = payload.get("Cues")
    if not isinstance(cues, list) or not cues:
        err(path, "'Cues' must be a non-empty array")
        cues = []
    by_id = {}
    for idx, cue in enumerate(cues):
        where = f"Cues[{idx}]"
        if not isinstance(cue, dict):
            err(path, f"{where}: must be an object")
            continue
        cue_id = cue.get("CueId")
        if not isinstance(cue_id, str) or not cue_id or cue_id == "None" or cue_id in by_id:
            err(path, f"{where}.CueId: empty, None or used twice")
            continue
        by_id[cue_id] = cue
        if layers and cue.get("Layer") not in layers:
            err(path, f"{where} {cue_id}: Layer must be one of {layers}")
        sound = cue.get("SoundPath")
        if not isinstance(sound, str) or (sound and not (sound.startswith("/Game/") and "." in sound)):
            err(path, f"{where} {cue_id}: SoundPath must be empty (not imported yet) or an object path like /Game/Audio/Name.Name")
        if not is_number(cue.get("Volume")) or not 0 <= cue.get("Volume") <= 1:
            err(path, f"{where} {cue_id}: Volume '{cue.get('Volume')}' must be 0-1")
        if not whole_number(cue.get("Priority")) or not 0 <= cue.get("Priority") <= 100:
            err(path, f"{where} {cue_id}: Priority '{cue.get('Priority')}' must be a whole number 0-100")
        if not is_number(cue.get("CooldownSeconds")) or cue.get("CooldownSeconds") < 0:
            err(path, f"{where} {cue_id}: CooldownSeconds must be a number, 0 or more")
        if not is_number(cue.get("DurationSeconds")) or cue.get("DurationSeconds") <= 0:
            err(path, f"{where} {cue_id}: DurationSeconds must be a number above 0")
        for flag in ("bLoop", "bSpatial", "bScaleByIntensity"):
            if not isinstance(cue.get(flag), bool):
                err(path, f"{where} {cue_id}: {flag} must be true or false")
        group = cue.get("LoopGroup")
        if not isinstance(group, str):
            err(path, f"{where} {cue_id}: LoopGroup must be a string ('None' outside a loop)")
        elif cue.get("bLoop") is True and group in ("", "None"):
            err(path, f"{where} {cue_id}: a loop needs a LoopGroup")
        if set(cue) - AUDIO_CUE_FIELDS:
            err(path, f"{where} {cue_id}: unknown field(s) {sorted(set(cue) - AUDIO_CUE_FIELDS)}")
    for idx, rule in enumerate(payload.get("EventCues") or []):
        where = f"EventCues[{idx}]"
        if not isinstance(rule, dict):
            err(path, f"{where}: must be an object")
            continue
        if triggers and (rule.get("Trigger") not in triggers or rule.get("Trigger") == "Manual"):
            err(path, f"{where}: Trigger '{rule.get('Trigger')}' must be an EPSAudioTrigger other than Manual ({triggers[1:]})")
        if not isinstance(rule.get("Detail"), str):
            err(path, f"{where}: Detail must be a string ('None' for any)")
        if rule.get("CueId") not in by_id:
            err(path, f"{where}: CueId '{rule.get('CueId')}' is not in Cues")
    if not isinstance(payload.get("EventCues"), list):
        err(path, "'EventCues' must be an array")
    sliders = percent_sliders()
    seen_layers = set()
    for idx, setting in enumerate(payload.get("LayerSettings") or []):
        where = f"LayerSettings[{idx}]"
        if not isinstance(setting, dict) or (layers and setting.get("Layer") not in layers):
            err(path, f"{where}: Layer must be one of {layers}")
            continue
        if setting.get("Layer") in seen_layers:
            err(path, f"{where}: {setting.get('Layer')} has two settings")
        seen_layers.add(setting.get("Layer"))
        if sliders is not None and setting.get("SettingId") not in sliders:
            err(path, f"{where}: SettingId '{setting.get('SettingId')}' must be a 0-100 slider in ui_settings.json")
    for idx, cue_id in enumerate(payload.get("StartupLoops") or []):
        if cue_id not in by_id or by_id[cue_id].get("bLoop") is not True:
            err(path, f"StartupLoops[{idx}]: '{cue_id}' must be a loop in Cues")
    extra = set(payload) - AUDIO_TUNING_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSAudioTuning exactly")


CROWD_UNIT_FIELDS = ("RestingExcitement", "LateCloseBonus", "DefaultHomeShare", "LevelHysteresis")
CROWD_TUNING_FIELDS = {"RestingExcitement", "LateGameQuarter", "CloseGameMargin", "LateCloseBonus", "HalfLifeSeconds",
                       "DefaultHomeShare", "LevelHysteresis", "BigGainYards", "BigHitDamage", "DeepPassCm", "Levels",
                       "CrowdReactions"}


def validate_crowd(path, payload):
    """FPSCrowdTuning (Data/crowd.json, Epic 23.2); mirrors UPSCrowdExcitementSubsystem::ValidateTuning."""
    levels = header_enum("PSTelemetryBus.h", "EPSCrowdLevel") or []
    reactions = header_enum("PSTelemetryBus.h", "EPSCrowdReaction") or []
    stimuli = header_enum("PSCrowdTypes.h", "EPSCrowdStimulus") or []
    for field in CROWD_UNIT_FIELDS:
        if not is_number(payload.get(field)) or not 0 <= payload.get(field) <= 1:
            err(path, f"{field}: '{payload.get(field)}' must be 0-1")
    for field in ("HalfLifeSeconds", "BigHitDamage", "DeepPassCm"):
        if not is_number(payload.get(field)) or payload.get(field) <= 0:
            err(path, f"{field}: '{payload.get(field)}' must be a number above 0")
    for field, low in (("BigGainYards", 1), ("LateGameQuarter", 1), ("CloseGameMargin", 0)):
        if not whole_number(payload.get(field)) or payload.get(field) < low:
            err(path, f"{field}: '{payload.get(field)}' must be a whole number, {low} or more")
    thresholds = {}
    for idx, entry in enumerate(payload.get("Levels") or []):
        if not isinstance(entry, dict) or entry.get("Level") not in levels or not is_number(entry.get("MinExcitement")):
            err(path, f"Levels[{idx}]: needs a Level ({levels}) and a MinExcitement")
            continue
        if entry["Level"] in thresholds:
            err(path, f"Levels[{idx}]: {entry['Level']} is listed twice")
        thresholds[entry["Level"]] = entry["MinExcitement"]
    previous = None
    for level in levels:
        if level not in thresholds:
            err(path, f"Levels: {level} needs a threshold")
            continue
        value = thresholds[level]
        if previous is None and value != 0:
            err(path, f"Levels: {level}, the quietest, must start at 0")
        elif previous is not None and not previous < value <= 1:
            err(path, f"Levels: {level}'s MinExcitement {value} must be above the quieter level's ({previous}), at most 1")
        previous = value
    named = []
    for idx, entry in enumerate(payload.get("CrowdReactions") or []):
        where = f"CrowdReactions[{idx}]"
        if not isinstance(entry, dict) or entry.get("Stimulus") not in stimuli:
            err(path, f"{where}: Stimulus must be one of {stimuli}")
            continue
        named.append(entry["Stimulus"])
        for field in ("FansDelta", "RivalsDelta"):
            if not is_number(entry.get(field)) or not -1 <= entry.get(field) <= 1:
                err(path, f"{where}.{field}: '{entry.get(field)}' must be -1..1")
        for field in ("FansReaction", "RivalsReaction"):
            if entry.get(field) not in reactions:
                err(path, f"{where}.{field}: '{entry.get(field)}' must be one of {reactions}")
    for stimulus in stimuli:
        if named.count(stimulus) != 1:
            err(path, f"CrowdReactions: '{stimulus}' must have exactly one entry")
    extra = set(payload) - CROWD_TUNING_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSCrowdTuning exactly")


COMMENTARY_STAKES = ("LateQuarterStakes", "CloseGameStakes", "CriticalDownStakes", "RedZoneStakes", "ScoreStakes",
                     "TurnoverStakes", "BigPlayNovelty")
COMMENTARY_HOOK_FIELDS = {"BigHitDamage", "DeepPassCm", "TwoMinuteWarningSeconds", "MaxMomentsKept", "bOfferToModels",
                          "ModelMoments", "ModelTask", "ModelInstructions", "ModelContextChars", "LateGameQuarter",
                          "CloseGameMargin", "RedZoneYardLine", "NoveltyHorizon", "BigPlayYards", *COMMENTARY_STAKES}


def validate_commentary_hooks(path, payload):
    """FPSCommentaryHookTuning (Data/commentary_hooks.json, Epic 23.5); mirrors
    UPSCommentaryEventModel::ValidateTuning, plus the moments' names and the router's tasks."""
    moments = header_enum("PSTelemetryBus.h", "EPSCommentaryMoment") or []
    for field in ("BigHitDamage", "DeepPassCm", "TwoMinuteWarningSeconds"):
        if not is_number(payload.get(field)) or payload.get(field) <= 0:
            err(path, f"{field}: '{payload.get(field)}' must be a number above 0")
    if not whole_number(payload.get("MaxMomentsKept")) or payload.get("MaxMomentsKept") < 1:
        err(path, f"MaxMomentsKept: '{payload.get('MaxMomentsKept')}' must be a whole number, 1 or more")
    for field in COMMENTARY_STAKES:
        if not is_number(payload.get(field)) or not 0 <= payload.get(field) <= 1:
            err(path, f"{field}: '{payload.get(field)}' must be 0-1")
    for field, low, high in (("LateGameQuarter", 1, None), ("CloseGameMargin", 0, None), ("RedZoneYardLine", 1, 99),
                             ("NoveltyHorizon", 1, None), ("BigPlayYards", 1, None)):
        value = payload.get(field)
        if not whole_number(value) or value < low or (high is not None and value > high):
            err(path, f"{field}: '{value}' must be a whole number, {low} or more" + (f", at most {high}" if high else ""))
    if not isinstance(payload.get("bOfferToModels"), bool):
        err(path, "bOfferToModels must be true or false")
    offered = payload.get("ModelMoments")
    if not isinstance(offered, list):
        err(path, "'ModelMoments' must be an array")
        offered = []
    for idx, moment in enumerate(offered):
        if moments and moment not in moments:
            err(path, f"ModelMoments[{idx}]: '{moment}' must be an EPSCommentaryMoment ({moments})")
        elif offered.count(moment) > 1:
            err(path, f"ModelMoments[{idx}]: '{moment}' is listed twice")
    if not isinstance(payload.get("ModelInstructions"), str) or not payload.get("ModelInstructions").strip():
        err(path, "ModelInstructions must be a non-empty string")
    if not whole_number(payload.get("ModelContextChars")) or payload.get("ModelContextChars") < 512:
        err(path, f"ModelContextChars: '{payload.get('ModelContextChars')}' must be a whole number, 512 or more")
    task = payload.get("ModelTask")
    try:
        routes = json.loads(ROUTING_TABLE.read_text(encoding="utf-8")).get("tasks", {})
    except (OSError, ValueError):
        routes = None
    if not isinstance(task, str) or not task:
        err(path, "ModelTask must name a model-router task")
    elif routes is not None and task not in routes:
        err(path, f"ModelTask: '{task}' is not a task in tools/orchestrator/routing.json ({sorted(routes)})")
    extra = set(payload) - COMMENTARY_HOOK_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSCommentaryHookTuning exactly")


COMMENTARY_LINE_PLACEHOLDERS = {"Player", "Other", "Yards", "Points", "Down", "Distance", "Quarter", "Clock", "HomeScore",
                                "AwayScore", "Total", "Stat", "Penalty"}
COMMENTARY_LINE_TYPES = {"LineId": str, "Voice": str, "Moment": str, "Priority": int, "CooldownSeconds": (int, float),
                         "MaxPerGame": int, "MaxPerSeason": int, "Detail": str, "MinDown": int, "MaxDown": int,
                         "MinYards": int, "MaxYards": int, "MinStakes": (int, float), "bRequireFirstDown": bool,
                         "bRequireTurnover": bool, "bNeedsPrimary": bool, "bNeedsSecondary": bool, "bNeedsTotal": bool}
COMMENTARY_LIBRARY_NUMBERS = {"WordsPerSecond": 0, "MinLineSeconds": 0, "MaxDelaySeconds": 0, "ColorMaxDelaySeconds": 0}
COMMENTARY_LIBRARY_FIELDS = {"Seed", "WordsPerSecond", "MinLineSeconds", "MaxLineSeconds", "MaxDelaySeconds",
                             "ColorMaxDelaySeconds", "InterruptMargin", "QueueLength", "ColorWindowDelaySeconds",
                             "StakesWeight", "NoveltyWeight", "RepeatPenalty", "VarietyBand", "MaxTalkingPointsPerGame",
                             "TalkingPointPriority", "TalkingPointGapSeconds", "bUseModelLines", "ModelLinePriority",
                             "MaxSpokenKept", "Lines"}


def placeholders(text):
    return set(re.findall(r"\{(\w+)\}", text))


def validate_commentary_lines(path, payload):
    """FPSCommentaryLibrary (Data/commentary_lines.json, Epic 96); mirrors
    UPSCommentaryEngine::ValidateLibrary, plus each line's text in Data/ui_text.csv."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import ui_text
    table, _ = ui_text.read_table(ui_text.UI_TEXT)
    voices = header_enum("PSCommentaryTypes.h", "EPSCommentaryVoice") or []
    moments = header_enum("PSTelemetryBus.h", "EPSCommentaryMoment") or []
    penalties = [name for name in (header_enum("PSPlaySimulation.h", "EPSPenaltyType") or []) if name != "None"]
    for field, low in COMMENTARY_LIBRARY_NUMBERS.items():
        if not is_number(payload.get(field)) or payload.get(field) <= low:
            err(path, f"{field}: '{payload.get(field)}' must be a number above {low}")
    if is_number(payload.get("MinLineSeconds")) and (not is_number(payload.get("MaxLineSeconds"))
                                                     or payload.get("MaxLineSeconds") < payload.get("MinLineSeconds")):
        err(path, "MaxLineSeconds must be a number, at least MinLineSeconds")
    for field in ("ColorWindowDelaySeconds", "TalkingPointGapSeconds", "StakesWeight", "NoveltyWeight", "RepeatPenalty",
                  "VarietyBand"):
        if not is_number(payload.get(field)) or payload.get(field) < 0:
            err(path, f"{field}: '{payload.get(field)}' must be a number, 0 or more")
    for field, low, high in (("InterruptMargin", 0, 100), ("QueueLength", 1, None), ("MaxSpokenKept", 1, None),
                             ("MaxTalkingPointsPerGame", 0, None), ("TalkingPointPriority", 0, 100),
                             ("ModelLinePriority", 0, 100)):
        value = payload.get(field)
        if not whole_number(value) or value < low or (high is not None and value > high):
            err(path, f"{field}: '{value}' must be a whole number, {low}" + (f"-{high}" if high is not None else " or more"))
    if not whole_number(payload.get("Seed")):
        err(path, "Seed must be a whole number")
    if not isinstance(payload.get("bUseModelLines"), bool):
        err(path, "bUseModelLines must be true or false")
    lines = payload.get("Lines")
    if not isinstance(lines, list) or not lines:
        err(path, "'Lines' must be a non-empty array")
        lines = []
    seen = set()
    called = set()
    for idx, line in enumerate(lines):
        where = f"Lines[{idx}]"
        if not isinstance(line, dict):
            err(path, f"{where}: must be an object")
            continue
        line_id = line.get("LineId")
        if not isinstance(line_id, str) or not line_id or line_id in seen:
            err(path, f"{where}.LineId: empty or used twice")
            continue
        seen.add(line_id)
        where = f"{where} {line_id}"
        for field, kind in COMMENTARY_LINE_TYPES.items():
            if field in line and (not isinstance(line[field], kind) or (kind is int and isinstance(line[field], bool))):
                err(path, f"{where}: {field} has the wrong type")
        for field in ("LineId", "Voice", "Moment", "Priority"):
            if field not in line:
                err(path, f"{where}: needs {field}")
        if set(line) - set(COMMENTARY_LINE_TYPES):
            err(path, f"{where}: unknown field(s) {sorted(set(line) - set(COMMENTARY_LINE_TYPES))}")
        if voices and line.get("Voice") not in voices:
            err(path, f"{where}: Voice must be one of {voices}")
        if moments and line.get("Moment") not in moments:
            err(path, f"{where}: Moment must be one of {moments}")
        if line.get("Voice") == "PlayByPlay":
            called.add(line.get("Moment"))
        priority = line.get("Priority")
        if whole_number(priority) and not 0 <= priority <= 100:
            err(path, f"{where}: Priority {priority} must be 0-100")
        for field in ("CooldownSeconds", "MaxPerGame", "MaxPerSeason"):
            if is_number(line.get(field, 0)) and line.get(field, 0) < 0:
                err(path, f"{where}: {field} must be 0 or more")
        min_down, max_down = line.get("MinDown", 0), line.get("MaxDown", 4)
        min_yards, max_yards = line.get("MinYards", -100), line.get("MaxYards", 100)
        stakes = line.get("MinStakes", 0)
        if (whole_number(min_down) and whole_number(max_down) and not 0 <= min_down <= max_down <= 4) \
                or (whole_number(min_yards) and whole_number(max_yards) and min_yards > max_yards) \
                or (is_number(stakes) and not 0 <= stakes <= 1):
            err(path, f"{where}: its conditions can't hold (downs 0-4, MinYards at most MaxYards, MinStakes 0-1)")
        key = f"Commentary.Line.{line_id}"
        if key not in table:
            err(path, f"{where}: no '{key}' row in Data/ui_text.csv")
            continue
        used = placeholders(table[key])
        if used - COMMENTARY_LINE_PLACEHOLDERS:
            err(path, f"{where}: its text names {sorted(used - COMMENTARY_LINE_PLACEHOLDERS)}, not facts of a moment ({sorted(COMMENTARY_LINE_PLACEHOLDERS)})")
        for needed, flag in ((("Player",), "bNeedsPrimary"), (("Other",), "bNeedsSecondary"), (("Total", "Stat"), "bNeedsTotal")):
            if used & set(needed) and line.get(flag) is not True:
                err(path, f"{where}: its text names {{{needed[0]}}}, so it needs {flag}: true (never said without it)")
    for moment in moments:
        if moment not in called:
            err(path, f"Lines: no PlayByPlay line for the {moment} moment")
    for key in ("Commentary.Voice.PlayByPlay", "Commentary.Voice.Color", "Commentary.Storyline",
                *[f"Commentary.Penalty.{name}" for name in penalties]):
        if key not in table:
            err(path, f"no '{key}' row in Data/ui_text.csv (the booth says it)")
    if "Commentary.Storyline" in table and placeholders(table["Commentary.Storyline"]) - {"Headline", "Body"}:
        err(path, "Commentary.Storyline may name only {Headline} and {Body}")
    extra = set(payload) - COMMENTARY_LIBRARY_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSCommentaryLibrary exactly")


TELESTRATOR_FIELDS = ("FieldHeightCm", "MinPointSpacing", "MaxStrokePoints", "PlayerPickRadius", "MaxMarks",
                      "MarkColor", "AutoMarkColor", "MarkWidth", "MinStrokeWidth", "ArrowheadLength",
                      "ArrowheadAngleDegrees", "PlayerRingRadius", "CircleSegments", "CursorSpeed",
                      "CursorDeadZone", "CursorRadius")
TELESTRATOR_LAYER_POSITIVE = ("MarkWidth", "MinStrokeWidth", "ArrowheadLength", "PlayerRingRadius", "CursorSpeed", "CursorRadius")


def validate_telestrator(path, payload):
    """FPSTelestratorTuning (Data/telestrator.json, Epic 44); mirrors UPSTelestratorSubsystem::ValidateTuning."""
    if not is_number(payload.get("FieldHeightCm")):
        err(path, f"FieldHeightCm: '{payload.get('FieldHeightCm')}' must be a number")
    spacing = payload.get("MinPointSpacing")
    if not is_number(spacing) or spacing < 0:
        err(path, f"MinPointSpacing: '{spacing}' must be a number, 0 or more")
    radius = payload.get("PlayerPickRadius")
    if not is_number(radius) or radius <= 0:
        err(path, f"PlayerPickRadius: '{radius}' must be a number above 0")
    for field, low in (("MaxStrokePoints", 2), ("MaxMarks", 1)):
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < low:
            err(path, f"{field}: '{value}' must be a whole number, {low} or more")
    # The drawing layer (UPSTelestratorWidget).
    for field in ("MarkColor", "AutoMarkColor"):
        value = payload.get(field)
        if not isinstance(value, str) or not HEX_COLOR.match(value):
            err(path, f"{field}: '{value}' must be #RRGGBB")
    for field in TELESTRATOR_LAYER_POSITIVE:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    angle = payload.get("ArrowheadAngleDegrees")
    if not is_number(angle) or not 0 < angle < 90:
        err(path, f"ArrowheadAngleDegrees: '{angle}' must be above 0 and below 90")
    segments = payload.get("CircleSegments")
    if not isinstance(segments, int) or isinstance(segments, bool) or segments < 8:
        err(path, f"CircleSegments: '{segments}' must be a whole number, 8 or more")
    dead_zone = payload.get("CursorDeadZone")
    if not is_number(dead_zone) or not 0 <= dead_zone < 1:
        err(path, f"CursorDeadZone: '{dead_zone}' must be a number from 0 to below 1")
    extra = set(payload) - set(TELESTRATOR_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSTelestratorTuning exactly")


PHOTO_MODE_NUMBERS = ("MoveCmPerSecond", "RiseCmPerSecond", "TurnDegreesPerSecond", "TurnStepDegrees", "MaxPitchDegrees",
                      "MaxDistanceCm", "MinHeightCm", "MinFieldOfView", "MaxFieldOfView", "ZoomDegreesPerSecond",
                      "MaxRollDegrees", "RollDegreesPerSecond", "MinFocusCm", "MaxFocusCm", "FocusDoublingsPerSecond",
                      "DefaultFocusCm", "CaptureResolutionMultiplier")
PHOTO_MODE_FIELDS = PHOTO_MODE_NUMBERS + ("Apertures", "Filters", "Presets", "MaxCaptureDimension")
PHOTO_FILTER_FIELDS = ("FilterId", "Saturation", "Contrast", "Tint", "WhiteTemp", "Vignette")


def validate_photo_mode(path, payload):
    """FPSPhotoModeTuning (Data/photo_mode.json, Epic 45); mirrors UPSPhotoModeSubsystem::ValidateTuning."""
    for field in PHOTO_MODE_NUMBERS:
        if not is_number(payload.get(field)):
            err(path, f"{field}: '{payload.get(field)}' must be a number")
    num = {f: payload[f] for f in PHOTO_MODE_NUMBERS if is_number(payload.get(f))}
    for field in ("MoveCmPerSecond", "RiseCmPerSecond", "TurnDegreesPerSecond", "MaxPitchDegrees", "MaxDistanceCm",
                  "ZoomDegreesPerSecond", "MaxRollDegrees", "RollDegreesPerSecond", "FocusDoublingsPerSecond"):
        if field in num and num[field] <= 0:
            err(path, f"{field}: must be above 0")
    if num.get("TurnStepDegrees", 0) < 0:
        err(path, "TurnStepDegrees: must be 0 or more")
    if num.get("MaxPitchDegrees", 0) >= 90:
        err(path, "MaxPitchDegrees: must be below 90")
    lo, hi = num.get("MinFieldOfView"), num.get("MaxFieldOfView")
    if lo is not None and hi is not None and not (0 < lo < hi < 180):
        err(path, "the field of view needs 0 < MinFieldOfView < MaxFieldOfView < 180")
    lo, hi, start = num.get("MinFocusCm"), num.get("MaxFocusCm"), num.get("DefaultFocusCm")
    if None not in (lo, hi, start) and not (0 < lo < hi and lo <= start <= hi):
        err(path, "the focus needs 0 < MinFocusCm < MaxFocusCm, with DefaultFocusCm between them")
    if num.get("CaptureResolutionMultiplier", 1) < 1:
        err(path, "CaptureResolutionMultiplier: must be 1 or more")
    biggest = payload.get("MaxCaptureDimension")
    if not isinstance(biggest, int) or isinstance(biggest, bool) or biggest < 1:
        err(path, f"MaxCaptureDimension: '{biggest}' must be a whole number, 1 or more")

    apertures = payload.get("Apertures")
    if not isinstance(apertures, list) or not apertures or not all(is_number(a) for a in apertures):
        err(path, "Apertures: must be a non-empty array of f-stops")
    else:
        for i, fstop in enumerate(apertures):
            if fstop < 0 or (i > 0 and fstop <= apertures[i - 1]):
                err(path, f"Apertures[{i}]: {fstop} must be 0 or more and above the one before")

    filter_ids = set()
    for i, row in enumerate(payload.get("Filters") or []):
        where = f"Filters[{i}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        fid = row.get("FilterId")
        if not isinstance(fid, str) or not fid or fid in filter_ids:
            err(path, f"{where}: FilterId '{fid}' is empty or listed twice")
        filter_ids.add(fid)
        extra = set(row) - set(PHOTO_FILTER_FIELDS)
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPSPhotoFilter exactly")
        for field in ("Saturation", "Contrast"):
            if field in row and (not is_number(row[field]) or row[field] < 0):
                err(path, f"{where}.{field}: must be a number, 0 or more")
        if "WhiteTemp" in row and (not is_number(row["WhiteTemp"]) or row["WhiteTemp"] <= 0):
            err(path, f"{where}.WhiteTemp: must be a number above 0")
        if "Vignette" in row and (not is_number(row["Vignette"]) or not 0 <= row["Vignette"] <= 1):
            err(path, f"{where}.Vignette: must be a number from 0 to 1")
        if "Tint" in row and not (isinstance(row["Tint"], str) and HEX_COLOR.match(row["Tint"])):
            err(path, f"{where}.Tint: '{row['Tint']}' must be #RRGGBB")

    presets = payload.get("Presets")
    if not isinstance(presets, list) or not presets:
        err(path, "Presets: must be a non-empty array")
        presets = []
    preset_ids = set()
    for i, row in enumerate(presets):
        where = f"Presets[{i}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        pid = row.get("PresetId")
        if not isinstance(pid, str) or not pid or pid in preset_ids:
            err(path, f"{where}: PresetId '{pid}' is empty or listed twice")
        preset_ids.add(pid)
        for fid in row.get("Filters") or []:
            if fid not in filter_ids:
                err(path, f"{where}: filter '{fid}' isn't in Filters")

    extra = set(payload) - set(PHOTO_MODE_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSPhotoModeTuning exactly")


def validate_input_buffer(path, payload, catalog):
    """FInputBufferTuningRow (Data/input_buffer.json, Epic 104.4); mirrors
    UPSInputBufferComponent::ValidateTuning plus the catalog cross-check."""
    max_queued = payload.get("MaxQueued")
    if isinstance(max_queued, bool) or not isinstance(max_queued, int) or max_queued < 1:
        err(path, f"MaxQueued: '{max_queued}' must be a whole number, 1 or more")
    extra = set(payload) - {"MaxQueued", "Actions"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FInputBufferTuningRow exactly")
    rows = payload.get("Actions")
    if not isinstance(rows, list):
        err(path, "'Actions' must be an array")
        return
    actions = {a.get("ActionId"): a for a in (catalog or {}).get("Actions", []) if isinstance(a, dict)}
    seen = set()
    for idx, row in enumerate(rows):
        where = f"Actions[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        action_id = row.get("ActionId")
        if not isinstance(action_id, str) or not action_id or action_id in seen:
            err(path, f"{where}.ActionId: empty or used twice")
        seen.add(action_id)
        window = row.get("BufferSeconds")
        if not is_number(window) or window < 0:
            err(path, f"{where}.BufferSeconds: '{window}' must be a number, 0 or more")
        extra = set(row) - {"ActionId", "BufferSeconds"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")
        if catalog is not None and isinstance(action_id, str) and action_id:
            action = actions.get(action_id)
            if action is None or action.get("ValueType") != "Boolean":
                err(path, f"{where}.ActionId: '{action_id}' must be a Boolean action in input_actions.json")


RUSH_MOVES = {"Bull", "Swim", "Rip", "Spin", "Club", "Split"}
BLOCK_RESPONSES = {"Anchor", "Punch", "Mirror"}
RUSH_ATTRIBUTES = {"Strength", "Agility", "Speed", "Awareness"}
RUSH_MOVE_NUMBERS = ("MinAttribute", "BaseWinChance", "RatingScalar", "MoveSeconds", "StaminaCost", "WinBurstSpeed",
                     "DoubleTeamWinScale")
RUSH_CATALOG_NUMBERS = ("FirstMoveSeconds", "RecoverySeconds", "CounterBonus", "WinChanceMin", "WinChanceMax",
                        "HistoryPriorWeight", "DoubleTeamRadius")


def validate_rush_moves(path, payload):
    """FPSRushMoveCatalog (Data/pass_rush_moves.json, Epic 70); mirrors
    PSRushMoves::ValidateCatalog."""
    for field in RUSH_CATALOG_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    low, high = payload.get("WinChanceMin"), payload.get("WinChanceMax")
    if is_number(low) and is_number(high) and not low <= high <= 1:
        err(path, f"WinChanceMin ({low}) <= WinChanceMax ({high}) <= 1 must hold")
    if is_number(payload.get("CounterBonus")) and payload["CounterBonus"] > 1:
        err(path, "CounterBonus is a chance: at most 1")
    if is_number(payload.get("HistoryPriorWeight")) and payload["HistoryPriorWeight"] <= 0:
        err(path, "HistoryPriorWeight must be positive")
    extra = set(payload) - set(RUSH_CATALOG_NUMBERS) - {"RushMoves"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSRushMoveCatalog exactly")
    moves = payload.get("RushMoves")
    if not isinstance(moves, list) or not moves:
        err(path, "'RushMoves' must be a non-empty array")
        return
    seen = set()
    for idx, row in enumerate(moves):
        where = f"RushMoves[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        move = row.get("Move")
        if move not in RUSH_MOVES:
            err(path, f"{where}.Move: '{move}' is not an EPSRushMove ({sorted(RUSH_MOVES)})")
        elif move in seen:
            err(path, f"{where}.Move: '{move}' is defined twice")
        seen.add(move)
        for field in ("Attribute", "BlockerAttribute"):
            if row.get(field) not in RUSH_ATTRIBUTES:
                err(path, f"{where}.{field}: '{row.get(field)}' must be one of {sorted(RUSH_ATTRIBUTES)}")
        for field in RUSH_MOVE_NUMBERS:
            value = row.get(field)
            if not is_number(value) or value < 0:
                err(path, f"{where}.{field}: '{value}' must be a number, 0 or more")
        if is_number(row.get("MinAttribute")) and row["MinAttribute"] > 100:
            err(path, f"{where}.MinAttribute: ratings run 0-100")
        for field in ("BaseWinChance", "DoubleTeamWinScale"):
            if is_number(row.get(field)) and row[field] > 1:
                err(path, f"{where}.{field}: at most 1")
        if is_number(row.get("MoveSeconds")) and row["MoveSeconds"] <= 0:
            err(path, f"{where}.MoveSeconds: must be positive")
        response, counters = row.get("Response"), row.get("Counters")
        if response not in BLOCK_RESPONSES:
            err(path, f"{where}.Response: '{response}' must be one of {sorted(BLOCK_RESPONSES)} (what stops the move)")
        if counters not in BLOCK_RESPONSES | {"None"}:
            err(path, f"{where}.Counters: '{counters}' must be None or one of {sorted(BLOCK_RESPONSES)}")
        elif counters == response:
            err(path, f"{where}.Counters: a move can't counter the response that stops it")
        if not isinstance(row.get("bDoubleTeamOnly"), bool):
            err(path, f"{where}.bDoubleTeamOnly: must be true or false")
        extra = set(row) - set(RUSH_MOVE_NUMBERS) - {"Move", "Attribute", "BlockerAttribute", "Response", "Counters",
                                                     "bDoubleTeamOnly"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")


DEFENSIVE_TECHNIQUE_NUMBERS = ("JumpWindowSeconds", "GetOffSpeed", "StripWindowSeconds", "StripCooldownSeconds",
                               "StripTackleScale", "StripFumbleChance")


def check_named_action(path, field, action_id, context, catalog):
    """The catalog action a tuning file names must be a Boolean action in context."""
    if not isinstance(action_id, str) or not action_id:
        err(path, f"{field}: must name a catalog action")
        return
    if catalog is None:
        return
    actions = {a.get("ActionId"): a for a in catalog.get("Actions", []) if isinstance(a, dict)}
    action = actions.get(action_id)
    if action is None:
        err(path, f"{field}: '{action_id}' is not an action in input_actions.json")
    elif action.get("ValueType") != "Boolean" or context not in (action.get("Contexts") or []):
        err(path, f"{field}: '{action_id}' must be a Boolean action in the {context} context")


def validate_defensive_techniques(path, payload, catalog):
    """FDefensiveTechniqueTuningRow (Data/defensive_techniques.json, Epic 104.5); mirrors
    UPSDefenderTechniqueComponent::ValidateTuning plus the catalog cross-check."""
    for field in DEFENSIVE_TECHNIQUE_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in ("StripTackleScale", "StripFumbleChance"):
        if is_number(payload.get(field)) and payload[field] > 1:
            err(path, f"{field}: at most 1")
    extra = set(payload) - set(DEFENSIVE_TECHNIQUE_NUMBERS) - {"JumpSnapAction", "StripAction"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FDefensiveTechniqueTuningRow exactly")
    check_named_action(path, "JumpSnapAction", payload.get("JumpSnapAction"), "DefensePreSnap", catalog)
    check_named_action(path, "StripAction", payload.get("StripAction"), "Defense", catalog)


KICK_METER_SECONDS = ("LineUpSeconds", "PowerFillSeconds", "AccuracySweepSeconds")
KICK_METER_WEIGHTS = ("PowerWeight", "AccuracyWeight")


def validate_kick_meter(path, payload, catalog):
    """FKickMeterTuningRow (Data/kick_meter.json, Epic 104.5); mirrors
    UPSKickMeterComponent::ValidateTuning plus the catalog cross-check."""
    for field in KICK_METER_SECONDS:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a positive number")
    for field in KICK_METER_WEIGHTS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    if all(is_number(payload.get(f)) for f in KICK_METER_WEIGHTS) and sum(payload[f] for f in KICK_METER_WEIGHTS) <= 0:
        err(path, "PowerWeight and AccuracyWeight can't both be 0")
    extra = set(payload) - set(KICK_METER_SECONDS) - set(KICK_METER_WEIGHTS) - {"KickAction"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FKickMeterTuningRow exactly")
    check_named_action(path, "KickAction", payload.get("KickAction"), "Kicking", catalog)


RETICLE_STATES = ("PreSnap", "InPlay", "BallCarrier")
RETICLE_STATE_NUMBERS = ("Radius", "Brightness", "PulseHz", "PulseAmount")
RETICLE_STYLE_FIELDS = {"OffenseColor", "DefenseColor", "bUseTeamColor", "MeshPath", "MaterialPath", "ColorParameter",
                        "MeshDiameter", "Thickness", "GroundClearance", "ReticleStates"}
HEX_COLOR = re.compile(r"^#[0-9A-Fa-f]{6}$")


def validate_overlay_reticle(path, payload):
    """FPSOverlayReticleStyle (Data/overlay_reticle.json, Epic 30); mirrors
    UPSOverlayReticleComponent::ValidateStyle."""
    for field in ("OffenseColor", "DefenseColor"):
        if not isinstance(payload.get(field), str) or not HEX_COLOR.match(payload[field]):
            err(path, f"{field}: '{payload.get(field)}' must be #RRGGBB")
    if not isinstance(payload.get("bUseTeamColor"), bool):
        err(path, "bUseTeamColor: must be true or false")
    for field in ("MeshPath", "MaterialPath", "ColorParameter"):
        if not isinstance(payload.get(field), str):
            err(path, f"{field}: must be a string")
    diameter = payload.get("MeshDiameter")
    if not is_number(diameter) or diameter <= 0:
        err(path, f"MeshDiameter: '{diameter}' must be a number above 0")
    for field in ("Thickness", "GroundClearance"):
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    extra = set(payload) - RETICLE_STYLE_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSOverlayReticleStyle exactly")
    states = payload.get("ReticleStates")
    if not isinstance(states, list):
        err(path, "'ReticleStates' must be an array")
        return
    seen = set()
    for idx, row in enumerate(states):
        where = f"ReticleStates[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        state = row.get("State")
        if state not in RETICLE_STATES:
            err(path, f"{where}.State: '{state}' must be one of {list(RETICLE_STATES)} (Hidden has no look)")
        elif state in seen:
            err(path, f"{where}.State: '{state}' is listed twice")
        seen.add(state)
        for field in RETICLE_STATE_NUMBERS:
            value = row.get(field)
            if not is_number(value) or value < 0:
                err(path, f"{where}.{field}: '{value}' must be a number, 0 or more")
        if is_number(row.get("Radius")) and row["Radius"] <= 0:
            err(path, f"{where}.Radius: must be above 0")
        if is_number(row.get("PulseAmount")) and row["PulseAmount"] > 1:
            err(path, f"{where}.PulseAmount: at most 1 (a fraction of Radius)")
        extra = set(row) - set(RETICLE_STATE_NUMBERS) - {"State"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")
    for state in RETICLE_STATES:
        if state not in seen:
            err(path, f"ReticleStates: no '{state}' entry")


FIELD_DIMENSION_FIELDS = ("CentimetresPerYard", "FieldLengthYards", "EndZoneDepthYards", "FieldWidthYards",
                          "OutOfBoundsDepthYards", "BoundaryHeightCm")


def validate_field_dimensions(path, payload):
    """FPSFieldDimensions (Data/field_dimensions.json): the field's one frame, which PSField maps
    yards to world space with; mirrors PSField::Validate."""
    for field in FIELD_DIMENSION_FIELDS:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    extra = set(payload) - set(FIELD_DIMENSION_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSFieldDimensions exactly")


FIELD_MARKING_PATHS = ("GroundMeshPath", "PlaneMeshPath", "MaterialPath")
FIELD_MARKING_COLORS = ("FieldColor", "SurroundColor", "NearEndZoneColor", "FarEndZoneColor", "LineColor")
FIELD_MARKING_POSITIVE = ("MeshSizeCm", "GroundThicknessCm", "LineWidthYards", "YardLineSpacingYards",
                          "HashSpacingYards", "HashLengthYards")
FIELD_MARKING_NON_NEGATIVE = ("LayerLiftCm", "HashOffsetYards")


def validate_field_markings(path, payload):
    """FPSFieldMarkingsStyle (Data/field_markings.json, Epic 146.3): how APSFieldSurface draws the
    field; mirrors APSFieldSurface::ValidateStyle."""
    for field in FIELD_MARKING_PATHS:
        value = payload.get(field)
        if not isinstance(value, str) or not value.startswith("/"):
            err(path, f"{field}: '{value}' must be an asset path such as /Engine/BasicShapes/Plane.Plane")
    if not isinstance(payload.get("ColorParameter"), str) or not payload["ColorParameter"]:
        err(path, "ColorParameter must name the material's color parameter")
    for field in FIELD_MARKING_COLORS:
        if not isinstance(payload.get(field), str) or not HEX_COLOR.match(payload[field]):
            err(path, f"{field}: '{payload.get(field)}' must be #RRGGBB")
    for field in FIELD_MARKING_POSITIVE:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    for field in FIELD_MARKING_NON_NEGATIVE:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    known = set(FIELD_MARKING_PATHS) | set(FIELD_MARKING_COLORS) | set(FIELD_MARKING_POSITIVE) \
        | set(FIELD_MARKING_NON_NEGATIVE) | {"ColorParameter"}
    extra = set(payload) - known
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSFieldMarkingsStyle exactly")


PLAY_DEMO_POSITIVE = ("FrameRateHz", "MaxPreSnapSeconds", "MaxPlaySeconds", "MaxResultWaitSeconds")
PLAY_DEMO_NON_NEGATIVE = ("PostWhistleSeconds", "MinPlayerMoveCm", "SpeedAllowanceCmPerSec", "GroundToleranceCm")
PLAY_DEMO_FIELDS = {"DemoId", "Intent", "HomeTeamId", "AwayTeamId", "OffensePlayId", "DefensePlayId", "Seed", "SeedTries",
                    "WantedOutcome"}
PLAY_DEMO_OUTCOMES = {"Run", "Completion", "Incompletion", "Sack", "Interception", "Touchdown"}
# Play categories every team keeps whatever its scheme (UPSStaffManager::BuildPlaybook): kicks,
# returns and clock plays.
EVERY_TEAM_CATEGORIES = {"Punt", "FakePunt", "FieldGoal", "FakeFieldGoal", "Kickoff", "OnsideKick", "KickReturn",
                         "KickBlock", "HandsTeam", "ReturnLaterals", "Spike", "Kneel"}


def team_scheme_formations(staffs, team_id, offense):
    """The formations team_id's coordinator's scheme keeps on a side (UPSStaffManager::GetTeamScheme
    and BuildPlaybook), or None when the team has no staff or scheme: then it keeps every play."""
    staff = next((s for s in staffs.get("Staffs", []) if isinstance(s, dict) and s.get("TeamId") == team_id), None)
    if staff is None:
        return None
    coach_id = staff.get("OffensiveCoordinatorId" if offense else "DefensiveCoordinatorId")
    coach = next((c for c in staffs.get("Coaches", []) if isinstance(c, dict) and c.get("CoachId") == coach_id), None)
    scheme = next((s for s in staffs.get("Schemes", []) if isinstance(s, dict) and coach and s.get("SchemeId") == coach.get("SchemeId")), None)
    return None if scheme is None else set(scheme.get("Formations") or [])


def validate_play_demos(path, payload):
    """FPSPlayDemoCatalog (Data/play_demos.json, the live-play demos); mirrors
    UPSPlayDemoRunner::ValidateCatalog, and checks each call against the playbook and its team's
    scheme, as UPSPlayCallSubsystem::CallPlay will."""
    for field in PLAY_DEMO_POSITIVE:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    for field in PLAY_DEMO_NON_NEGATIVE:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    if is_number(payload.get("PostWhistleSeconds")) and is_number(payload.get("MaxResultWaitSeconds")) \
            and payload["PostWhistleSeconds"] >= payload["MaxResultWaitSeconds"]:
        err(path, "PostWhistleSeconds must be shorter than MaxResultWaitSeconds")
    extra = set(payload) - set(PLAY_DEMO_POSITIVE) - set(PLAY_DEMO_NON_NEGATIVE) - {"PlayDemos"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSPlayDemoCatalog exactly")

    demos = payload.get("PlayDemos")
    if not isinstance(demos, list) or not demos:
        err(path, "PlayDemos must list at least one demo")
        return
    teams = {t.get("TeamId") for t in (load_json("sample_teams.json") or {}).get("Teams", []) if isinstance(t, dict)}
    plays = {p.get("PlayId"): p for p in (load_json("sample_playbook.json") or {}).get("Plays", []) if isinstance(p, dict)}
    staffs = load_json("coaching_staffs.json") or {}
    seen = set()
    for idx, demo in enumerate(demos):
        where = f"PlayDemos[{idx}]"
        if not isinstance(demo, dict):
            err(path, f"{where}: not an object")
            continue
        unknown = set(demo) - PLAY_DEMO_FIELDS
        if unknown:
            err(path, f"{where}: unknown field(s) {sorted(unknown)} - names must match FPSPlayDemoDef exactly")
        demo_id = demo.get("DemoId")
        if not isinstance(demo_id, str) or not demo_id:
            err(path, f"{where}.DemoId: must be a non-empty string")
        elif demo_id in seen:
            err(path, f"{where}.DemoId: '{demo_id}' is used twice")
        seen.add(demo_id)
        if not isinstance(demo.get("Intent"), str) or not demo["Intent"]:
            err(path, f"{where}.Intent: must say what the demo sets out to show")
        home, away = demo.get("HomeTeamId"), demo.get("AwayTeamId")
        for field, team in (("HomeTeamId", home), ("AwayTeamId", away)):
            if team not in teams:
                err(path, f"{where}.{field}: '{team}' is not a team in sample_teams.json")
        if home == away:
            err(path, f"{where}: HomeTeamId and AwayTeamId must be two different teams")
        for field, offense, team in (("OffensePlayId", True, home), ("DefensePlayId", False, away)):
            play = plays.get(demo.get(field))
            if play is None:
                err(path, f"{where}.{field}: '{demo.get(field)}' is not a play in sample_playbook.json")
                continue
            if bool(play.get("bIsOffensivePlay", True)) != offense:
                err(path, f"{where}.{field}: '{demo.get(field)}' is not a{'n offensive' if offense else ' defensive'} play")
            kept = team_scheme_formations(staffs, team, offense)
            if kept is not None and play.get("PlayCategory") not in EVERY_TEAM_CATEGORIES and play.get("Formation") not in kept:
                err(path, f"{where}.{field}: {team}'s scheme doesn't keep the '{play.get('Formation')}' formation, "
                          f"so the play-call subsystem would refuse '{demo.get(field)}'")
        seed = demo.get("Seed")
        if not isinstance(seed, int) or isinstance(seed, bool):
            err(path, f"{where}.Seed: '{seed}' must be a whole number")
        tries = demo.get("SeedTries", 1)
        if not isinstance(tries, int) or isinstance(tries, bool) or tries < 1:
            err(path, f"{where}.SeedTries: '{tries}' must be a whole number, 1 or more")
        wanted = demo.get("WantedOutcome", "")
        if not isinstance(wanted, str) or (wanted and wanted not in PLAY_DEMO_OUTCOMES):
            err(path, f"{where}.WantedOutcome: '{wanted}' must be empty or one of {sorted(PLAY_DEMO_OUTCOMES)}")


TOUCH_HUD_SHARES = ("RestOpacity", "PressedOpacity", "StickIdleFade")
TOUCH_HUD_FRACTIONS = ("RingWidth", "KnobRadius", "SwipeArrowWidth", "SwipeArrowHead")
TOUCH_HUD_POSITIVE = ("LabelSize", "SwipeArrowLength")
TOUCH_HUD_COLORS = ("ControlColor", "PressedColor", "LabelColor")


def validate_touch_hud(path, payload):
    """FPSTouchHudStyle (Data/touch_hud.json, Epic 146.4): how UPSTouchHudWidget draws the touch
    controls; mirrors PSTouchHud::ValidateStyle."""
    for field in TOUCH_HUD_SHARES:
        value = payload.get(field)
        if not is_number(value) or value < 0 or value > 1:
            err(path, f"{field}: '{value}' must be a number from 0 to 1")
    for field in TOUCH_HUD_FRACTIONS:
        value = payload.get(field)
        if not is_number(value) or value <= 0 or value > 1:
            err(path, f"{field}: '{value}' must be a number above 0 and at most 1")
    for field in TOUCH_HUD_POSITIVE:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    flash = payload.get("SwipeFlashSeconds")
    if not is_number(flash) or flash < 0:
        err(path, f"SwipeFlashSeconds: '{flash}' must be a number, 0 or more")
    segments = payload.get("CircleSegments")
    if not isinstance(segments, int) or isinstance(segments, bool) or segments < 3:
        err(path, f"CircleSegments: '{segments}' must be a whole number, 3 or more")
    for field in TOUCH_HUD_COLORS:
        if not isinstance(payload.get(field), str) or not HEX_COLOR.match(payload[field]):
            err(path, f"{field}: '{payload.get(field)}' must be #RRGGBB")
    known = set(TOUCH_HUD_SHARES) | set(TOUCH_HUD_FRACTIONS) | set(TOUCH_HUD_POSITIVE) | set(TOUCH_HUD_COLORS) \
        | {"SwipeFlashSeconds", "CircleSegments"}
    extra = set(payload) - known
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSTouchHudStyle exactly")


PENALTY_FIELDS = ("HoldingChancePerPlay", "OffsidesChancePerSnap")


def validate_penalties(path, payload):
    """FPSPenaltyTuning (Data/penalties.json): how often the simulation's own flags fly; mirrors
    UPSPenaltyModel::ValidateTuning."""
    for field in PENALTY_FIELDS:
        value = payload.get(field)
        if not is_number(value) or value < 0 or value > 1:
            err(path, f"{field}: '{value}' must be a number from 0 to 1")
    extra = set(payload) - set(PENALTY_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSPenaltyTuning exactly")


def validate_control_handoff(path, payload, catalog):
    """FControlHandoffTuningRow (Data/control_handoff.json, Epic 30); mirrors
    UPSControlHandoffComponent::ValidateTuning plus the catalog cross-check."""
    window = payload.get("CycleWindowSeconds")
    if not is_number(window) or window < 0:
        err(path, f"CycleWindowSeconds: '{window}' must be a number, 0 or more")
    left, right = payload.get("PickLeftAction"), payload.get("PickRightAction")
    if not isinstance(left, str) or not isinstance(right, str) or not left or not right or left == right:
        err(path, "PickLeftAction and PickRightAction must be two different action names")
    elif catalog is not None:
        actions = {a.get("ActionId"): a for a in catalog.get("Actions", []) if isinstance(a, dict)}
        for field, action_id in (("PickLeftAction", left), ("PickRightAction", right)):
            action = actions.get(action_id)
            if action is None or action.get("ValueType") != "Boolean" or "PreSnap" not in (action.get("Contexts") or []):
                err(path, f"{field}: '{action_id}' must be a Boolean action in input_actions.json's PreSnap context")
    offense_role, defense_role = payload.get("OffenseControlRole"), payload.get("DefenseControlRole")
    if offense_role not in OFFENSIVE_ROLES:
        err(path, f"OffenseControlRole: '{offense_role}' must be an offensive role ({sorted(OFFENSIVE_ROLES)})")
    if defense_role not in DEFENSIVE_ROLES:
        err(path, f"DefenseControlRole: '{defense_role}' must be a defensive role ({sorted(DEFENSIVE_ROLES)})")
    extra = set(payload) - {"CycleWindowSeconds", "PickLeftAction", "PickRightAction", "OffenseControlRole", "DefenseControlRole"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FControlHandoffTuningRow exactly")

RECEIVER_ALIGNMENTS = {"Wide", "Slot", "Tight", "Backfield"}
PRESNAP_NUMBERS = ("SlotMaxSplit", "MotionEndSplit", "MotionArrivalRadius", "ManTravelLateralRadius", "SlideAimOffset",
                   "BoxWidth", "BoxDepth", "CpuReadMinAwareness")
PRESNAP_COUNTS = ("HeavyBoxCount", "LightBoxCount")
PRESNAP_FLAGS = ("bCpuKeepsBackInVsBlitz", "bCpuMotionOnPass")
PRESNAP_ACTIONS = ("AudibleAction", "SelectAction", "HotRouteAction", "MotionAction", "SlideAction", "ProtectionAction")


FORMATION_CATALOG_NUMBERS = ("LinemanSpacingYards", "LineSetbackYards", "ShadeYards", "BoundaryMarginYards")
FORMATION_SLOT_FIELDS = {"Role", "ScrimmageYardOffset", "LateralYardOffset", "Side", "Technique", "OverReceiver"}
FORMATION_QB_ALIGNMENTS = ("UnderCenter", "Pistol", "Shotgun")
FORMATION_BACKFIELDS = ("Empty", "Single", "Offset", "I", "Split", "Full")
FORMATION_DEFENSE_ROLES = {"DefensiveLineman", "Linebacker", "DefensiveBack"}


def load_json(name):
    """Data/<name> as parsed JSON (None when missing or broken; its own checks report that)."""
    try:
        return json.loads((DATA_DIR / name).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None


def formation_read(formation, recognition, cm_per_yard):
    """(QBAlignment, Backfield, strong side +1/-1) that Epic 80's classifier
    (PSPlayRecognition::ClassifyFormation) reads from formation lined up with the ball at 0;
    None without one quarterback slot."""
    strength = -1 if formation.get("Strength") == "Left" else 1
    slots = [s for s in formation.get("Slots") or [] if isinstance(s, dict)]
    passers = [s for s in slots if s.get("Role") == "Quarterback"]
    if len(passers) != 1:
        return None

    def depth(slot):
        return -float(slot.get("ScrimmageYardOffset", 0)) * cm_per_yard

    def across(slot):
        side = -strength if slot.get("Side") == "Weak" else strength
        return side * float(slot.get("LateralYardOffset", 0)) * cm_per_yard

    qb_depth = depth(passers[0])
    alignment = ("UnderCenter" if qb_depth <= recognition.get("UnderCenterMaxDepth", 200)
                 else "Pistol" if qb_depth <= recognition.get("PistolMaxDepth", 450) else "Shotgun")
    backs, right, left, inline_right, inline_left = [], 0, 0, 0, 0
    for slot in slots:
        if slot is passers[0]:
            continue
        a = across(slot)
        if depth(slot) >= recognition.get("BackfieldMinDepth", 250) and abs(a) <= recognition.get("BoxHalfWidth", 450):
            backs.append(a)
            continue
        inline = abs(a) <= recognition.get("InlineWidth", 500) and slot.get("Role") == "TightEnd"
        if a >= 0:
            right += 1
            inline_right += 1 if inline else 0
        else:
            left += 1
            inline_left += 1 if inline else 0
    if not backs:
        backfield = "Empty"
    elif len(backs) == 1:
        backfield = "Offset" if abs(backs[0]) > recognition.get("OffsetWidth", 100) else "Single"
    elif len(backs) == 2:
        backfield = "I" if abs(backs[0] - backs[1]) <= recognition.get("StackWidth", 100) else "Split"
    else:
        backfield = "Full"
    strong = 1 if right > left or (right == left and inline_right >= inline_left) else -1
    return alignment, backfield, strong


def validate_formations(path, payload):
    """FPSFormationCatalog (Data/formations.json); mirrors PSFormations::Validate, plus what only
    other files know: every formation the playbook, the generator, the staffs and the personnel
    packages name exists, with a slot for each of its package's players; every front the plays,
    the generator and run_fits.json name, and every shell they and coverage_matchups.json name,
    exists; each defensive call places its whole package; each formation's QBAlignment, Backfield
    and Strength are what play_recognition.json's classifier reads from its slots; and each shell
    keeps defensive_presnap.json's count of deep safeties."""
    for field in FORMATION_CATALOG_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0 or (field == "LinemanSpacingYards" and value <= 0):
            err(path, f"{field}: '{value}' must be a number, 0 or more (the spacing above 0)")
    extra = set(payload) - set(FORMATION_CATALOG_NUMBERS) - {"Techniques", "OffenseFormations", "FrontAlignments", "ShellAlignments"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSFormationCatalog exactly")

    techniques = set()
    for idx, row in enumerate(payload.get("Techniques") or []):
        where = f"Techniques[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: not an object")
            continue
        name = row.get("Technique")
        if not isinstance(name, str) or not name or name in techniques:
            err(path, f"{where}: Technique empty or listed twice")
        techniques.add(name)
        if row.get("Lineman") not in (0, 1, 2, 3) or row.get("Shade") not in (-1, 0, 1):
            err(path, f"{where} '{name}': Lineman 0-3 and Shade -1, 0 or 1")
        if set(row) - {"Technique", "Lineman", "Shade"}:
            err(path, f"{where}: unknown field(s) {sorted(set(row) - {'Technique', 'Lineman', 'Shade'})}")

    def check_slots(where, slots, offense, backs_only=False):
        counts = {}
        for sidx, slot in enumerate(slots or []):
            swhere = f"{where}.Slots[{sidx}]"
            if not isinstance(slot, dict):
                err(path, f"{swhere}: not an object")
                continue
            if set(slot) - FORMATION_SLOT_FIELDS:
                err(path, f"{swhere}: unknown field(s) {sorted(set(slot) - FORMATION_SLOT_FIELDS)} - names must match FPSFormationSpawnPoint exactly")
            role = slot.get("Role")
            counts[role] = counts.get(role, 0) + 1
            if offense and (role not in content_contracts.OFFENSE_ROLES or role == "OffensiveLineman"):
                err(path, f"{swhere}: {role} takes no offensive slot (the line is the catalog's)")
            if not offense and role not in FORMATION_DEFENSE_ROLES:
                err(path, f"{swhere}: {role} is not a defender")
            if backs_only and role != "DefensiveBack":
                err(path, f"{swhere}: a shell places the defensive backs")
            if slot.get("Side", "Strong") not in ("Strong", "Weak"):
                err(path, f"{swhere}.Side: Strong or Weak")
            for field in ("ScrimmageYardOffset", "LateralYardOffset"):
                if field in slot and not is_number(slot[field]):
                    err(path, f"{swhere}.{field}: must be a number")
            offset = slot.get("ScrimmageYardOffset", 0)
            if is_number(offset) and (offset > 0 if offense else offset < 0):
                err(path, f"{swhere}.ScrimmageYardOffset: the {'offense lines up behind' if offense else 'defense beyond'} the line")
            technique, over = slot.get("Technique"), slot.get("OverReceiver", 0)
            if offense and (technique or over):
                err(path, f"{swhere}: the offense keys on no technique or receiver")
            if technique is not None and technique not in techniques:
                err(path, f"{swhere}.Technique: '{technique}' is not in Techniques")
            if not isinstance(over, int) or isinstance(over, bool) or over < 0 or (over and technique):
                err(path, f"{swhere}.OverReceiver: a whole number, 0 or more, and not with a technique")
        return counts

    def named(rows, key, where):
        names = {}
        for idx, row in enumerate(rows or []):
            if not isinstance(row, dict):
                err(path, f"{where}[{idx}]: not an object")
                continue
            name = row.get(key)
            if not isinstance(name, str) or not name.strip() or name.lower() in names or name.lower() == "none":
                err(path, f"{where}[{idx}].{key}: empty, None or listed twice")
                continue
            names[name.lower()] = row
        return names

    formations = named(payload.get("OffenseFormations"), "Formation", "OffenseFormations")
    fronts = named(payload.get("FrontAlignments"), "Front", "FrontAlignments")
    shells = named(payload.get("ShellAlignments"), "Shell", "ShellAlignments")

    recognition = load_json("play_recognition.json") or {}
    cm_per_yard = (load_json("field_dimensions.json") or {}).get("CentimetresPerYard", 100.0)
    packages = load_data("personnel_packages.json", "Packages")
    slot_counts = {}
    for name, row in formations.items():
        where = f"OffenseFormations '{row['Formation']}'"
        slot_counts[name] = check_slots(where, row.get("Slots"), True)
        if row.get("Strength") not in ("Right", "Left"):
            err(path, f"{where}.Strength: Right or Left")
        if row.get("QBAlignment") not in FORMATION_QB_ALIGNMENTS or row.get("Backfield") not in FORMATION_BACKFIELDS:
            err(path, f"{where}: QBAlignment one of {FORMATION_QB_ALIGNMENTS}, Backfield one of {FORMATION_BACKFIELDS}")
        if set(row) - {"Formation", "Strength", "QBAlignment", "Backfield", "Slots"}:
            err(path, f"{where}: unknown field(s) - names must match FPSOffenseFormationDef exactly")
        read = formation_read(row, recognition, cm_per_yard)
        if read is None:
            err(path, f"{where}: one Quarterback slot")
        elif read != (row.get("QBAlignment"), row.get("Backfield"), -1 if row.get("Strength") == "Left" else 1):
            err(path, f"{where}: play_recognition.json reads its slots as {read[0]}, {read[1]} backfield, strong "
                      f"{'right' if read[2] > 0 else 'left'}; it says {row.get('QBAlignment')}, {row.get('Backfield')}, {row.get('Strength')}")
        roles = formation_roles(packages, row["Formation"], True) if packages is not None else None
        if roles is not None:
            wanted = {role: n for role, n in roles.items() if role != "OffensiveLineman"}
            if wanted != slot_counts[name]:
                err(path, f"{where}: its slots {slot_counts[name]} aren't its personnel package's {wanted}")
    front_counts = {name: check_slots(f"FrontAlignments '{row['Front']}'", row.get("Slots"), False) for name, row in fronts.items()}
    shell_counts = {name: check_slots(f"ShellAlignments '{row['Shell']}'", row.get("Slots"), False, True) for name, row in shells.items()}

    # Every name the plays, the generator, the staffs and the packages use.
    plays = load_data("sample_playbook.json", "Plays") or []
    generator = load_json("playbook_generator.json") or {}
    used_formations = {(p.get("Formation"), "sample_playbook.json") for p in plays if isinstance(p, dict) and p.get("bIsOffensivePlay")}
    used_formations |= {(f, "playbook_generator.json") for f in generator.get("OffenseFormations") or []}
    used_formations |= {(f, "coaching_staffs.json") for s in load_data("coaching_staffs.json", "Schemes") or []
                        if isinstance(s, dict) and s.get("bOffense") for f in s.get("Formations") or []}
    used_formations |= {(f, "personnel_packages.json") for p in packages or [] if isinstance(p, dict) and p.get("bOffense")
                        for f in p.get("Formations") or []}
    for formation, source in sorted(used_formations, key=str):
        if isinstance(formation, str) and formation.lower() not in formations:
            err(path, f"OffenseFormations: '{formation}' ({source}) has no alignment")
    used_fronts = {(p.get("Front"), "sample_playbook.json") for p in plays if isinstance(p, dict) and not p.get("bIsOffensivePlay") and p.get("Front")}
    used_fronts |= {(f.get("Front"), "playbook_generator.json") for f in generator.get("DefensiveFronts") or [] if isinstance(f, dict)}
    used_fronts |= {(f.get("Front"), "run_fits.json") for f in load_data("run_fits.json", "Fronts") or [] if isinstance(f, dict)}
    for front, source in sorted(used_fronts, key=str):
        if isinstance(front, str) and front.lower() not in fronts:
            err(path, f"FrontAlignments: '{front}' ({source}) has no alignment")
    used_shells = {(p.get("CoverageShell"), "sample_playbook.json") for p in plays if isinstance(p, dict) and not p.get("bIsOffensivePlay")}
    used_shells |= {(c.get("Shell"), "playbook_generator.json") for c in generator.get("Coverages") or [] if isinstance(c, dict)}
    used_shells |= {(s.get("Shell"), "coverage_matchups.json") for s in load_data("coverage_matchups.json", "Shells") or [] if isinstance(s, dict)}
    for shell, source in sorted(used_shells, key=str):
        if isinstance(shell, str) and shell.strip() and shell.lower() != "none" and shell.lower() not in shells:
            err(path, f"ShellAlignments: '{shell}' ({source}) has no alignment")

    # Each defensive call places its whole package: the shell the backs it has slots for, the
    # front everyone else.
    def check_call(label, formation, front, shell):
        roles = formation_roles(packages, formation, False) if packages is not None else None
        front_slots = front_counts.get((front or "").lower())
        if roles is None or front_slots is None:
            return
        shell_backs = shell_counts.get((shell or "").lower(), {}).get("DefensiveBack", 0)
        for role, count in roles.items():
            have = max(front_slots.get(role, 0), shell_backs) if role == "DefensiveBack" else front_slots.get(role, 0)
            if have < count:
                err(path, f"{label}: front '{front}' and shell '{shell}' place {have} of its {count} {role}s")
    for play in plays:
        if isinstance(play, dict) and not play.get("bIsOffensivePlay"):
            check_call(f"sample_playbook.json '{play.get('PlayId')}'", play.get("Formation"), play.get("Front"), play.get("CoverageShell"))
    for front in generator.get("DefensiveFronts") or []:
        for coverage in generator.get("Coverages") or []:
            if isinstance(front, dict) and isinstance(coverage, dict):
                check_call(f"playbook_generator.json '{front.get('Formation')}' in {coverage.get('Shell')}",
                           front.get("Formation"), front.get("Front"), coverage.get("Shell"))

    # The deep safeties each shell lines up: Epic 67's count for it.
    presnap = load_json("defensive_presnap.json") or {}
    deep_depth = presnap.get("DeepSafetyDepth", 1000)
    for rule in presnap.get("ShellSafeties") or []:
        shell = shells.get(str(rule.get("Shell", "")).lower()) if isinstance(rule, dict) else None
        if shell is None:
            continue
        deep = sum(1 for s in shell.get("Slots") or [] if isinstance(s, dict) and not s.get("OverReceiver")
                   and is_number(s.get("ScrimmageYardOffset", 0)) and s.get("ScrimmageYardOffset", 0) * cm_per_yard >= deep_depth)
        if deep != rule.get("DeepSafeties"):
            err(path, f"ShellAlignments '{shell['Shell']}': {deep} deep safeties; defensive_presnap.json plays {rule.get('DeepSafeties')}")


def load_route_ids():
    """Route IDs in Data/sample_routes.json, or None when it is missing or broken."""
    try:
        routes = json.loads((DATA_DIR / "sample_routes.json").read_text(encoding="utf-8")).get("Routes")
    except (OSError, json.JSONDecodeError, UnicodeDecodeError, AttributeError):
        return None
    if not isinstance(routes, list):
        return None
    return {r.get("RouteId") for r in routes if isinstance(r, dict)}


def validate_presnap_tuning(path, payload, catalog, route_ids):
    """FPreSnapTuningRow (Data/presnap_tuning.json, Epic 66)."""
    sets = payload.get("HotRouteSets")
    if not isinstance(sets, list):
        err(path, "'HotRouteSets' must be an array")
        sets = []
    seen = set()
    for idx, row in enumerate(sets):
        where = f"HotRouteSets[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        alignment = row.get("Alignment")
        if alignment not in RECEIVER_ALIGNMENTS:
            err(path, f"{where}.Alignment: '{alignment}' is not an EPSReceiverAlignment ({sorted(RECEIVER_ALIGNMENTS)})")
        elif alignment in seen:
            err(path, f"{where}.Alignment: '{alignment}' has two sets")
        seen.add(alignment)
        routes = row.get("Routes")
        if not isinstance(routes, list) or not routes or not all(isinstance(r, str) and r for r in routes):
            err(path, f"{where}.Routes: must be a non-empty array of route IDs")
            routes = []
        elif len(set(routes)) != len(routes):
            err(path, f"{where}.Routes: repeats a route")
        named = list(routes) + [row.get("ReleaseRoute")]
        if not isinstance(row.get("ReleaseRoute"), str) or not row.get("ReleaseRoute"):
            err(path, f"{where}.ReleaseRoute: must name a route")
        elif route_ids is not None:
            for route in named:
                if route not in route_ids:
                    err(path, f"{where}: '{route}' is not a route in sample_routes.json")
        extra = set(row) - {"Alignment", "Routes", "ReleaseRoute"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")
    missing = RECEIVER_ALIGNMENTS - seen
    if missing:
        err(path, f"HotRouteSets: no set for {sorted(missing)}")
    for field in PRESNAP_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in PRESNAP_COUNTS:
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            err(path, f"{field}: '{value}' must be a whole number, 0 or more")
    if isinstance(payload.get("LightBoxCount"), int) and isinstance(payload.get("HeavyBoxCount"), int) \
            and payload["LightBoxCount"] >= payload["HeavyBoxCount"]:
        err(path, "LightBoxCount must be below HeavyBoxCount")
    if is_number(payload.get("CpuReadMinAwareness")) and payload["CpuReadMinAwareness"] > 100:
        err(path, "CpuReadMinAwareness: ratings run 0-100")
    for field in PRESNAP_FLAGS:
        if not isinstance(payload.get(field), bool):
            err(path, f"{field}: must be true or false")
    blitz_route = payload.get("BlitzHotRoute")
    if not isinstance(blitz_route, str) or (route_ids is not None and blitz_route and blitz_route not in route_ids):
        err(path, f"BlitzHotRoute: '{blitz_route}' is not a route in sample_routes.json")
    actions = {a.get("ActionId"): a for a in (catalog or {}).get("Actions", []) if isinstance(a, dict)}
    named_actions = [payload.get(field) for field in PRESNAP_ACTIONS]
    if len(set(named_actions)) != len(named_actions):
        err(path, "the pre-snap actions must all differ")
    for field in PRESNAP_ACTIONS:
        action_id = payload.get(field)
        if not isinstance(action_id, str) or not action_id:
            err(path, f"{field}: must name an action")
        elif catalog is not None:
            action = actions.get(action_id)
            if action is None:
                err(path, f"{field}: '{action_id}' is not an action in input_actions.json")
            elif action.get("ValueType") != "Boolean" or "PreSnap" not in (action.get("Contexts") or []):
                err(path, f"{field}: '{action_id}' must be a Boolean action in the PreSnap context")
    extra = set(payload) - set(PRESNAP_NUMBERS) - set(PRESNAP_COUNTS) - set(PRESNAP_FLAGS) - set(PRESNAP_ACTIONS) \
        - {"HotRouteSets", "BlitzHotRoute"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPreSnapTuningRow exactly")


SETTING_KINDS = {"Toggle", "Choice", "Slider"}


def validate_settings_catalog(path, payload):
    """FPSSettingsCatalog (Data/ui_settings.json, Epic 103); mirrors
    UPSSettingsSubsystem::ValidateCatalog."""
    categories = payload.get("Categories")
    settings = payload.get("Settings")
    if not isinstance(categories, list) or not isinstance(settings, list):
        err(path, "'Categories' and 'Settings' must be arrays")
        return
    category_ids = set()
    for idx, row in enumerate(categories):
        cid = row.get("CategoryId") if isinstance(row, dict) else None
        if not isinstance(cid, str) or not cid or cid in category_ids:
            err(path, f"Categories[{idx}].CategoryId: empty or used twice")
        category_ids.add(cid)
        if isinstance(row, dict) and not isinstance(row.get("Label"), str):
            err(path, f"Categories[{idx}].Label: must be text")
    setting_ids = set()
    allowed = {"SettingId", "Category", "Label", "Kind", "Choices", "Values", "Min", "Max", "Step", "Unit", "Default", "Description"}
    for idx, row in enumerate(settings):
        where = f"Settings[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        sid = row.get("SettingId")
        if not isinstance(sid, str) or not sid or sid in setting_ids:
            err(path, f"{where}.SettingId: empty or used twice")
        setting_ids.add(sid)
        if row.get("Category") not in category_ids:
            err(path, f"{where}.Category: '{row.get('Category')}' is not a category")
        kind = row.get("Kind", "Toggle")
        default = row.get("Default", 0)
        extra = set(row) - allowed
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")
        if kind not in SETTING_KINDS:
            err(path, f"{where}.Kind: '{kind}' is not one of {sorted(SETTING_KINDS)}")
        elif not is_number(default):
            err(path, f"{where}.Default: must be a number")
        elif kind == "Toggle" and default not in (0, 1):
            err(path, f"{where}.Default: a toggle's default is 0 or 1")
        elif kind == "Choice":
            choices = row.get("Choices", [])
            values = row.get("Values", [])
            if not isinstance(choices, list) or len(choices) < 2:
                err(path, f"{where}.Choices: a choice needs at least two")
                choices = []
            if values and (not isinstance(values, list) or len(values) != len(choices) or not all(is_number(v) for v in values)):
                err(path, f"{where}.Values: empty, or one number per choice")
            if default != int(default) or not 0 <= default < max(len(choices), 1):
                err(path, f"{where}.Default: the index of one of its Choices")
        elif kind == "Slider":
            low, high, step = row.get("Min", 0), row.get("Max", 1), row.get("Step", 1)
            if not all(is_number(v) for v in (low, high, step)) or step <= 0 or high <= low:
                err(path, f"{where}: a slider needs Min below Max and a positive Step")
            elif not low <= default <= high:
                err(path, f"{where}.Default: outside Min..Max")


TEMPOS = ("Huddle", "NoHuddle", "HurryUp", "MilkClock")
GAME_SITUATIONS = ("Normal", "TwoMinuteDrill", "FourMinuteOffense", "VictoryFormation")
SITUATIONAL_SECONDS = ("TwoMinuteWindowSeconds", "TwoScoreWindowSeconds", "ClockUrgencySeconds", "SpikeMinSeconds",
                       "FourMinuteWindowSeconds", "DefenseTimeoutWindowSeconds", "KneelPlaySeconds",
                       "KneelPreSnapSeconds", "EndOfHalfKneelSeconds", "ClockPlayWeight")
SITUATIONAL_COUNTS = ("OneScorePoints", "MaxSpikeDown", "MaxDeficitToChase", "EndOfHalfKneelMaxYardLine")
SITUATIONAL_FIELDS = set(SITUATIONAL_SECONDS) | set(SITUATIONAL_COUNTS) | {
    "Tempos", "SituationTempos", "HumanTempoCycle", "SpikeTempo", "KneelTempo", "SidelineRouteIds",
    "MiddleRouteIds", "SidelinePlayDelta", "MiddlePlayDelta", "CategoryWeights"}


def validate_situational_tuning(path, payload, route_ids):
    """FPSSituationalTuning (Data/situational_tuning.json, Epic 76)."""
    extra = set(payload) - SITUATIONAL_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSSituationalTuning exactly")
    for field in SITUATIONAL_SECONDS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in SITUATIONAL_COUNTS:
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            err(path, f"{field}: '{value}' must be a whole number, 0 or more")
    if isinstance(payload.get("MaxSpikeDown"), int) and not 1 <= payload["MaxSpikeDown"] <= 4:
        err(path, "MaxSpikeDown must be a down, 1-4")
    if isinstance(payload.get("EndOfHalfKneelMaxYardLine"), int) and payload["EndOfHalfKneelMaxYardLine"] > 100:
        err(path, "EndOfHalfKneelMaxYardLine must be a yard line, 0-100")
    for field in ("SidelinePlayDelta", "MiddlePlayDelta"):
        if not is_number(payload.get(field)):
            err(path, f"{field}: '{payload.get(field)}' must be a number")

    defined = set()
    tempos = payload.get("Tempos")
    if not isinstance(tempos, list):
        err(path, "'Tempos' must be an array")
        tempos = []
    for idx, tempo in enumerate(tempos):
        where = f"Tempos[{idx}]"
        if not isinstance(tempo, dict):
            err(path, f"{where}: must be an object")
            continue
        name = tempo.get("Tempo")
        if name not in TEMPOS:
            err(path, f"{where}.Tempo: '{name}' is not an EPSTempo ({list(TEMPOS)})")
        elif name in defined:
            err(path, f"{where}.Tempo: '{name}' is defined twice")
        defined.add(name)
        if not str(tempo.get("Label", "")).strip():
            err(path, f"{where}: no Label")
        mark = tempo.get("SnapAtPlayClockSeconds")
        if not is_number(mark) or not 0 <= mark <= 40:
            err(path, f"{where}.SnapAtPlayClockSeconds: '{mark}' must be a play-clock reading, 0-40")
        if not isinstance(tempo.get("bRerunLastCall"), bool):
            err(path, f"{where}.bRerunLastCall: must be true or false")

    def check_tempo(where, name):
        if name not in TEMPOS:
            err(path, f"{where}: '{name}' is not an EPSTempo ({list(TEMPOS)})")
        elif name not in defined:
            err(path, f"{where}: '{name}' has no entry in Tempos")

    situation_tempos = payload.get("SituationTempos")
    if not isinstance(situation_tempos, list):
        err(path, "'SituationTempos' must be an array")
        situation_tempos = []
    seen = set()
    for idx, row in enumerate(situation_tempos):
        where = f"SituationTempos[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        situation = row.get("Situation")
        if situation not in GAME_SITUATIONS:
            err(path, f"{where}.Situation: '{situation}' is not an EPSGameSituation ({list(GAME_SITUATIONS)})")
        elif situation in seen:
            err(path, f"{where}.Situation: '{situation}' is listed twice")
        seen.add(situation)
        check_tempo(f"{where}.ClockRunningTempo", row.get("ClockRunningTempo"))
        check_tempo(f"{where}.ClockStoppedTempo", row.get("ClockStoppedTempo"))

    cycle = payload.get("HumanTempoCycle")
    if not isinstance(cycle, list) or not cycle:
        err(path, "HumanTempoCycle: must list at least one tempo")
    else:
        for idx, name in enumerate(cycle):
            check_tempo(f"HumanTempoCycle[{idx}]", name)
        if len(set(map(str, cycle))) != len(cycle):
            err(path, "HumanTempoCycle: lists a tempo twice")
    check_tempo("SpikeTempo", payload.get("SpikeTempo"))
    check_tempo("KneelTempo", payload.get("KneelTempo"))

    route_sets = {}
    for field in ("SidelineRouteIds", "MiddleRouteIds"):
        ids = payload.get(field)
        if not isinstance(ids, list) or not all(isinstance(r, str) and r for r in ids):
            err(path, f"{field}: must be an array of route IDs")
            continue
        route_sets[field] = set(ids)
        for rid in ids:
            if route_ids is not None and rid not in route_ids:
                err(path, f"{field}: '{rid}' is not a route in sample_routes.json")
    both = route_sets.get("SidelineRouteIds", set()) & route_sets.get("MiddleRouteIds", set())
    if both:
        err(path, f"route(s) {sorted(both)} are both sideline and middle routes")

    weights = payload.get("CategoryWeights")
    if not isinstance(weights, list):
        err(path, "'CategoryWeights' must be an array")
        weights = []
    for idx, row in enumerate(weights):
        where = f"CategoryWeights[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        if row.get("Situation") not in GAME_SITUATIONS:
            err(path, f"{where}.Situation: '{row.get('Situation')}' is not an EPSGameSituation ({list(GAME_SITUATIONS)})")
        if not isinstance(row.get("bOffense"), bool):
            err(path, f"{where}.bOffense: must be true or false")
        if not str(row.get("Category", "")).strip():
            err(path, f"{where}: no Category")
        if not is_number(row.get("Delta")):
            err(path, f"{where}.Delta: '{row.get('Delta')}' must be a number")
        if not str(row.get("Reason", "")).strip():
            err(path, f"{where}: no Reason (the play-call screen shows it)")


UI_ACCESSIBILITY_NUMBERS = ("CaptionMinSeconds", "CaptionMaxSeconds", "CaptionWordsPerSecond", "MinMatchupColorDistance")


def validate_ui_accessibility(path, payload):
    """FPSUIAccessibilityTuning (Data/ui_accessibility.json, Epic 103); mirrors
    UPSUIAccessibilitySubsystem::ValidateTuning."""
    for field in UI_ACCESSIBILITY_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    low, high = payload.get("CaptionMinSeconds"), payload.get("CaptionMaxSeconds")
    if is_number(low) and is_number(high) and not 0 < low <= high:
        err(path, "CaptionMinSeconds must be positive and CaptionMaxSeconds no less")
    if is_number(payload.get("CaptionWordsPerSecond")) and payload["CaptionWordsPerSecond"] <= 0:
        err(path, "CaptionWordsPerSecond must be positive")
    lines = payload.get("CaptionMaxLines")
    if isinstance(lines, bool) or not isinstance(lines, int) or lines < 1:
        err(path, f"CaptionMaxLines: '{lines}' must be a whole number, 1 or more")
    extra = set(payload) - set(UI_ACCESSIBILITY_NUMBERS) - {"CaptionMaxLines"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSUIAccessibilityTuning exactly")


SPECIAL_TEAMS_CHANCES = ("KickoffTouchbackChance", "OnsideRecoveryChance", "OnsideRecoveryVsHandsTeamChance",
                         "LateralTouchdownChance", "LateralFumbleLostChance", "PuntBlockChance", "FieldGoalBlockChance",
                         "MaxBlockChance", "BlockedKickTouchdownChance", "DefaultBigReturnChance",
                         "LaneDisciplineBigReturnScale", "FakePuntSuccessChance", "FakeFieldGoalSuccessChance",
                         "FakeVsBlockUnitDelta", "FakeMinAggression", "FakeCallChance", "SurpriseOnsideChance",
                         "BaseBlockCallChance")
SPECIAL_TEAMS_YARD_LINES = ("KickoffYardLine", "SafetyKickYardLine", "TouchbackYardLine", "KickoffReturnMinYardLine",
                            "KickoffReturnMaxYardLine", "PuntTouchbackYardLine", "MissedFieldGoalMinYardLine",
                            "MissedFieldGoalMaxYardLine")
SPECIAL_TEAMS_COUNTS = ("OnsideKickYards", "PuntGrossYardsMin", "PuntGrossYardsMax", "PuntReturnYardsMin", "PuntReturnYardsMax",
                        "BlockedPuntRecoilYards", "BigReturnYards", "FakeExtraYardsMax", "FakeMaxDistance",
                        "OnsideMaxDeficit", "LateralsMaxDeficit")
SPECIAL_TEAMS_NUMBERS = ("HandsTeamReturnPenaltyYards", "FieldGoalSnapYards", "BlockUnitMultiplier", "EdgeSpeedFactor",
                         "InteriorStrengthFactor", "BlockUnitReturnPenaltyYards", "CoverageAwarenessSpan",
                         "LaneDisciplineYards", "MaxFieldGoalAttemptYards", "LastPlaySeconds", "NoPuntTrailingSeconds",
                         "OnsideWindowSeconds", "LateralsWindowSeconds", "BlockWindowSeconds", "SpecialTeamsPlayWeight")
SPECIAL_TEAMS_FIELDS = (set(SPECIAL_TEAMS_CHANCES) | set(SPECIAL_TEAMS_YARD_LINES) | set(SPECIAL_TEAMS_COUNTS)
                        | set(SPECIAL_TEAMS_NUMBERS) | {"FieldGoalRanges", "ReturnSchemes"})


def load_return_formations():
    """The formations of the playbook's KickReturn plays, or None when the playbook is missing or
    broken (its own checks report that)."""
    try:
        playbook = json.loads((DATA_DIR / "sample_playbook.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    if not isinstance(playbook, dict) or not isinstance(playbook.get("Plays"), list):
        return None
    return {p.get("Formation") for p in playbook["Plays"] if isinstance(p, dict) and p.get("PlayCategory") == "KickReturn"}


def validate_special_teams(path, payload, return_formations):
    """FPSSpecialTeamsTuning (Data/special_teams.json, Epic 75)."""
    extra = set(payload) - SPECIAL_TEAMS_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSSpecialTeamsTuning exactly")
    for field in SPECIAL_TEAMS_CHANCES:
        value = payload.get(field)
        if not is_number(value) or not 0 <= value <= 1:
            err(path, f"{field}: '{value}' must be a chance, 0-1")
    for field in SPECIAL_TEAMS_YARD_LINES:
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or not 1 <= value <= 99:
            err(path, f"{field}: '{value}' must be a yard line, 1-99")
    for field in SPECIAL_TEAMS_COUNTS:
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            err(path, f"{field}: '{value}' must be a whole number, 0 or more")
    for field in SPECIAL_TEAMS_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for low, high in (("KickoffReturnMinYardLine", "KickoffReturnMaxYardLine"), ("PuntGrossYardsMin", "PuntGrossYardsMax"),
                      ("PuntReturnYardsMin", "PuntReturnYardsMax"), ("MissedFieldGoalMinYardLine", "MissedFieldGoalMaxYardLine")):
        if isinstance(payload.get(low), int) and isinstance(payload.get(high), int) and payload[low] > payload[high]:
            err(path, f"{low} ({payload[low]}) must not exceed {high} ({payload[high]})")

    ranges = payload.get("FieldGoalRanges")
    if not isinstance(ranges, list) or not ranges:
        err(path, "FieldGoalRanges: must list at least one range")
        ranges = []
    previous = None
    for idx, row in enumerate(ranges):
        where = f"FieldGoalRanges[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        yards, chance = row.get("MaxYards"), row.get("MakeChance")
        if not is_number(yards) or yards <= 0:
            err(path, f"{where}.MaxYards: '{yards}' must be a distance above 0")
        elif previous is not None and yards <= previous:
            err(path, f"{where}.MaxYards: ranges must run shortest first")
        else:
            previous = yards
        if not is_number(chance) or not 0 <= chance <= 1:
            err(path, f"{where}.MakeChance: '{chance}' must be a chance, 0-1")

    schemes = payload.get("ReturnSchemes")
    if not isinstance(schemes, list):
        err(path, "'ReturnSchemes' must be an array")
        schemes = []
    seen = set()
    for idx, row in enumerate(schemes):
        where = f"ReturnSchemes[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        formation = row.get("Formation")
        if not isinstance(formation, str) or not formation or formation in seen:
            err(path, f"{where}.Formation: '{formation}' is empty or listed twice")
        seen.add(formation)
        if return_formations is not None and formation not in return_formations:
            err(path, f"{where}.Formation: '{formation}' is not the formation of a KickReturn play in sample_playbook.json")
        if not is_number(row.get("ReturnYardsBonus")):
            err(path, f"{where}.ReturnYardsBonus: '{row.get('ReturnYardsBonus')}' must be a number")
        chance = row.get("BigReturnChance")
        if not is_number(chance) or not 0 <= chance <= 1:
            err(path, f"{where}.BigReturnChance: '{chance}' must be a chance, 0-1")
RUN_GAPS = {"DLeft", "CLeft", "BLeft", "ALeft", "ARight", "BRight", "CRight", "DRight"}
RUN_FIT_NUMBERS = ("GapWidth", "InlineTightEndWidth", "FitDepth", "SecondLevelDepth", "LeverageOffset", "FlowWeight",
                   "AttackRadius", "FillRadius")


def validate_run_fits(path, payload):
    """FPSRunFitCatalog (Data/run_fits.json, Epic 81); mirrors PSDefenderGaps::ValidateCatalog."""
    for field in RUN_FIT_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    if is_number(payload.get("GapWidth")) and payload["GapWidth"] <= 0:
        err(path, "GapWidth must be positive")
    if is_number(payload.get("FlowWeight")) and payload["FlowWeight"] > 1:
        err(path, "FlowWeight must be between 0 (hold the gap) and 1 (follow the carrier)")
    extra = set(payload) - set(RUN_FIT_NUMBERS) - {"Fronts", "DefaultFront"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSRunFitCatalog exactly")
    fronts = payload.get("Fronts")
    if not isinstance(fronts, list) or not fronts:
        err(path, "'Fronts' must be a non-empty array")
        return
    names = set()
    for idx, front in enumerate(fronts):
        where = f"Fronts[{idx}]"
        if not isinstance(front, dict):
            err(path, f"{where}: must be an object")
            continue
        name = front.get("Front")
        if not isinstance(name, str) or not name or name in names:
            err(path, f"{where}.Front: empty or listed twice")
        names.add(name)
        fits = front.get("Fits")
        if not isinstance(fits, list):
            err(path, f"{where}.Fits: must be an array")
            continue
        roles, gaps = set(), set()
        for fit_idx, fit in enumerate(fits):
            fit_where = f"{where}.Fits[{fit_idx}]"
            if not isinstance(fit, dict):
                err(path, f"{fit_where}: must be an object")
                continue
            role = fit.get("Role")
            if role not in PLAYER_ROLES:
                err(path, f"{fit_where}.Role: '{role}' is not an EPlayerRole")
            elif role in roles:
                err(path, f"{fit_where}.Role: '{role}' is listed twice in this front")
            roles.add(role)
            fit_gaps = fit.get("Gaps")
            if not isinstance(fit_gaps, list):
                err(path, f"{fit_where}.Gaps: must be an array")
                continue
            for gap in fit_gaps:
                if gap not in RUN_GAPS:
                    err(path, f"{fit_where}.Gaps: '{gap}' is not an EPSRunGap ({sorted(RUN_GAPS)})")
                elif gap in gaps:
                    err(path, f"{fit_where}.Gaps: '{gap}' is given twice in this front")
                gaps.add(gap)
            extra = set(fit) - {"Role", "Gaps"}
            if extra:
                err(path, f"{fit_where}: unknown field(s) {sorted(extra)}")
    if payload.get("DefaultFront") not in names:
        err(path, f"DefaultFront: '{payload.get('DefaultFront')}' must name a listed front")


ROUTE_RUNNING_FIELDS = ("PressRadius", "ReleaseBaseWinChance", "ReleaseRatingWeight", "ReleaseMinWinChance",
                        "ReleaseMaxWinChance", "DelayShare", "DelaySeconds", "RerouteOffset", "RerouteDelaySeconds",
                        "BreakMinAngleDegrees", "MaxBreakRounding", "BreakSeparationBase", "BreakSeparationPerAgility",
                        "FakeSellSeconds", "BiteRadius", "BiteBaseChance", "BiteAgilityWeight", "BiteAwarenessWeight",
                        "BiteMinChance", "BiteMaxChance", "BiteFreezeSeconds", "ManReadRadius")
ROUTE_RUNNING_CHANCES = ("ReleaseBaseWinChance", "ReleaseMinWinChance", "ReleaseMaxWinChance", "DelayShare",
                         "BiteBaseChance", "BiteMinChance", "BiteMaxChance")


def validate_route_running(path, payload):
    """FRouteRunningTuningRow (Data/route_running.json, Epic 68)."""
    for field in ROUTE_RUNNING_FIELDS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in ROUTE_RUNNING_CHANCES:
        value = payload.get(field)
        if is_number(value) and value > 1:
            err(path, f"{field}: {value} is a chance, at most 1")
    for low, high in (("ReleaseMinWinChance", "ReleaseMaxWinChance"), ("BiteMinChance", "BiteMaxChance")):
        if is_number(payload.get(low)) and is_number(payload.get(high)) and payload[low] > payload[high]:
            err(path, f"{low} must not exceed {high}")
    if is_number(payload.get("BreakMinAngleDegrees")) and payload["BreakMinAngleDegrees"] > 180:
        err(path, "BreakMinAngleDegrees: at most 180")
    extra = set(payload) - set(ROUTE_RUNNING_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FRouteRunningTuningRow exactly")


BLOWN_COVERAGE_FIELDS = ("CheckIntervalSeconds", "UncoveredSeparation", "MinDepthPastLine", "HelpRadius")


def validate_blown_coverage(path, payload):
    """FBlownCoverageTuningRow (Data/blown_coverage.json, Epic 17.4)."""
    for field in BLOWN_COVERAGE_FIELDS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    if is_number(payload.get("CheckIntervalSeconds")) and payload["CheckIntervalSeconds"] <= 0:
        err(path, "CheckIntervalSeconds: must be above 0")
    extra = set(payload) - set(BLOWN_COVERAGE_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FBlownCoverageTuningRow exactly")


ROUTE_FIELDS = {"RouteId", "Waypoints", "OptionReadWaypoint", "VsManBranch", "VsZoneBranch"}
ROUTE_WAYPOINT_FIELDS = {"Offset", "TimingSeconds", "bFake"}


def validate_routes(path, payload):
    """FPSRoute library (Data/sample_routes.json): waypoints, timing, fakes, option branches (Epic 68)."""
    routes = payload.get("Routes")
    if not isinstance(routes, list):
        err(path, "'Routes' must be an array")
        return
    by_id = {}
    for idx, route in enumerate(routes):
        where = f"Routes[{idx}]"
        if not isinstance(route, dict):
            err(path, f"{where}: must be an object")
            continue
        route_id = route.get("RouteId")
        if not isinstance(route_id, str) or not route_id or route_id in by_id:
            err(path, f"{where}.RouteId: empty or used twice")
        by_id[route_id] = route
        extra = set(route) - ROUTE_FIELDS
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPSRoute exactly")
        waypoints = route.get("Waypoints")
        if not isinstance(waypoints, list) or not waypoints:
            err(path, f"{where}.Waypoints: must be a non-empty array")
            continue
        last_time = None
        for widx, waypoint in enumerate(waypoints):
            wwhere = f"{where}.Waypoints[{widx}]"
            if not isinstance(waypoint, dict):
                err(path, f"{wwhere}: must be an object")
                continue
            extra = set(waypoint) - ROUTE_WAYPOINT_FIELDS
            if extra:
                err(path, f"{wwhere}: unknown field(s) {sorted(extra)}")
            offset = waypoint.get("Offset")
            if not isinstance(offset, dict) or not all(is_number(offset.get(axis)) for axis in ("X", "Y", "Z")):
                err(path, f"{wwhere}.Offset: needs numeric X, Y and Z")
            timing = waypoint.get("TimingSeconds")
            if not is_number(timing) or timing < 0:
                err(path, f"{wwhere}.TimingSeconds: '{timing}' must be a number, 0 or more")
            elif last_time is not None and timing < last_time:
                err(path, f"{wwhere}.TimingSeconds: earlier than the waypoint before")
            else:
                last_time = timing
            fake = waypoint.get("bFake", False)
            if not isinstance(fake, bool):
                err(path, f"{wwhere}.bFake: must be true or false")
            elif fake and widx == len(waypoints) - 1:
                err(path, f"{wwhere}.bFake: the last waypoint can't be a fake")
        read = route.get("OptionReadWaypoint", -1)
        if not isinstance(read, int) or isinstance(read, bool) or read < -1 or read >= len(waypoints):
            err(path, f"{where}.OptionReadWaypoint: '{read}' must be -1 or a waypoint index")
    for idx, route in enumerate(routes):
        if not isinstance(route, dict) or route.get("OptionReadWaypoint", -1) in (-1, None):
            continue
        for field in ("VsManBranch", "VsZoneBranch"):
            branch = by_id.get(route.get(field))
            if branch is None:
                err(path, f"Routes[{idx}].{field}: '{route.get(field)}' is not a route here")
            elif branch.get("OptionReadWaypoint", -1) not in (-1, None):
                err(path, f"Routes[{idx}].{field}: '{route.get(field)}' is itself an option route")


ALL22_PLACEMENTS = {"Sideline", "EndZone"}
ALL22_RIG_NUMBERS = ("HeightCm", "StandoffCm", "RailHalfLengthCm", "MinFieldOfView", "MaxFieldOfView")
ALL22_TUNING_NUMBERS = ("FramingMarginCm", "PlayerHeightCm", "AspectRatio", "ReframeSpeed")


def validate_all22_camera(path, payload):
    """FPSAll22CameraTuning (Data/camera_all22.json, Epic 40); mirrors
    UPSCameraFraming::ValidateTuning."""
    rigs = payload.get("All22Rigs")
    if not isinstance(rigs, list) or not rigs:
        err(path, "'All22Rigs' must be a non-empty array: the film view needs at least one rig")
        rigs = []
    seen = set()
    for idx, rig in enumerate(rigs):
        where = f"All22Rigs[{idx}]"
        if not isinstance(rig, dict):
            err(path, f"{where}: must be an object")
            continue
        rig_id = rig.get("RigId")
        if not isinstance(rig_id, str) or not rig_id or rig_id in seen:
            err(path, f"{where}.RigId: empty or used twice")
        seen.add(rig_id)
        if rig.get("Placement") not in ALL22_PLACEMENTS:
            err(path, f"{where}.Placement: '{rig.get('Placement')}' is not an EPSAll22RigPlacement ({sorted(ALL22_PLACEMENTS)})")
        for field in ALL22_RIG_NUMBERS:
            value = rig.get(field)
            if not is_number(value) or value < 0:
                err(path, f"{where}.{field}: '{value}' must be a number, 0 or more")
        for field in ("HeightCm", "StandoffCm"):
            if is_number(rig.get(field)) and rig[field] <= 0:
                err(path, f"{where}.{field}: must be positive")
        low, high = rig.get("MinFieldOfView"), rig.get("MaxFieldOfView")
        if is_number(low) and is_number(high) and not 0 < low <= high < 170:
            err(path, f"{where}: the zoom range must satisfy 0 < MinFieldOfView ({low}) <= MaxFieldOfView ({high}) < 170")
        if not isinstance(rig.get("bTrackPlay"), bool):
            err(path, f"{where}.bTrackPlay: must be true or false")
        extra = set(rig) - set(ALL22_RIG_NUMBERS) - {"RigId", "Placement", "bTrackPlay"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPSAll22RigDef exactly")
    for field in ALL22_TUNING_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    if is_number(payload.get("AspectRatio")) and payload["AspectRatio"] <= 0:
        err(path, "AspectRatio must be positive")
    extra = set(payload) - set(ALL22_TUNING_NUMBERS) - {"All22Rigs"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSAll22CameraTuning exactly")


DIRECTOR_SHOTS = ("LosWide", "All22High", "TightFollow", "EndZone", "SidelineReaction", "Skycam")
DIRECTOR_RIG_SHOTS = {"All22High", "EndZone"}
DIRECTOR_TRIGGERS = {"PreSnap", "Snap", "Throw", "Catch", "Tackle", "Fumble", "Score", "PlayEnd", "Breakaway"}
DIRECTOR_SHOT_NUMBERS = ("HeightCm", "DistanceCm", "FieldOfView", "AimHeightCm", "LeadSeconds")
DIRECTOR_INTEREST_NUMBERS = ("BallWeight", "ProximityWeight", "ProximityRadiusCm", "BreakawayWeight", "BreakawaySpeedCms",
                             "BreakawayClearanceCm", "BigHitWeight", "BigHitSeconds", "BigHitDamage", "SwitchMargin")
DIRECTOR_NUMBERS = ("MinShotSeconds", "FollowInterpSpeed", "NeutralBandCm")


def load_all22_rig_ids():
    """Rig IDs in Data/camera_all22.json, or None when it is missing or broken."""
    try:
        rigs = json.loads((DATA_DIR / "camera_all22.json").read_text(encoding="utf-8")).get("All22Rigs")
    except (OSError, json.JSONDecodeError, UnicodeDecodeError, AttributeError):
        return None
    if not isinstance(rigs, list):
        return None
    return {r.get("RigId") for r in rigs if isinstance(r, dict)}


def validate_camera_director(path, payload, rig_ids):
    """FPSCameraDirectorTuning (Data/camera_director.json, Epic 38); mirrors
    UPSCameraDirectorComponent::ValidateTuning, plus the rigs cross-checked against
    camera_all22.json."""
    if not isinstance(payload.get("bDirectorEnabled"), bool):
        err(path, "bDirectorEnabled: must be true or false")
    shots = payload.get("Shots")
    if not isinstance(shots, list):
        err(path, "'Shots' must be an array")
        shots = []
    defined = set()
    for idx, row in enumerate(shots):
        where = f"Shots[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        shot = row.get("Shot")
        if shot not in DIRECTOR_SHOTS:
            err(path, f"{where}.Shot: '{shot}' is not an EPSDirectorShot ({list(DIRECTOR_SHOTS)})")
        elif shot in defined:
            err(path, f"{where}.Shot: '{shot}' is defined twice")
        defined.add(shot)
        rig = row.get("RigId", "")
        if not isinstance(rig, str):
            err(path, f"{where}.RigId: must be a string")
        elif shot in DIRECTOR_RIG_SHOTS:
            if not rig:
                err(path, f"{where}.RigId: an all-22 shot needs a rig")
            elif rig_ids is not None and rig not in rig_ids:
                err(path, f"{where}.RigId: '{rig}' is not a rig in camera_all22.json")
        for field in DIRECTOR_SHOT_NUMBERS:
            value = row.get(field)
            if not is_number(value) or value < 0:
                err(path, f"{where}.{field}: '{value}' must be a number, 0 or more")
        if shot not in DIRECTOR_RIG_SHOTS:
            for field in ("HeightCm", "DistanceCm"):
                if is_number(row.get(field)) and row[field] <= 0:
                    err(path, f"{where}.{field}: must be positive")
            fov = row.get("FieldOfView")
            if is_number(fov) and not 0 < fov < 170:
                err(path, f"{where}.FieldOfView: {fov} must be in (0, 170)")
        extra = set(row) - set(DIRECTOR_SHOT_NUMBERS) - {"Shot", "RigId"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPSDirectorShotDef exactly")
    missing = [shot for shot in DIRECTOR_SHOTS if shot not in defined]
    if missing:
        err(path, f"Shots: {missing} not defined")
    rules = payload.get("CutRules")
    if not isinstance(rules, list):
        err(path, "'CutRules' must be an array")
        rules = []
    ruled = set()
    for idx, row in enumerate(rules):
        where = f"CutRules[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        trigger = row.get("Trigger")
        if trigger not in DIRECTOR_TRIGGERS:
            err(path, f"{where}.Trigger: '{trigger}' is not an EPSDirectorTrigger ({sorted(DIRECTOR_TRIGGERS)})")
        elif trigger in ruled:
            err(path, f"{where}.Trigger: '{trigger}' has two rules")
        ruled.add(trigger)
        if row.get("Shot") not in defined:
            err(path, f"{where}.Shot: '{row.get('Shot')}' is not a defined shot")
        extra = set(row) - {"Trigger", "Shot"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")
    if "PreSnap" not in ruled:
        err(path, "CutRules: no rule for PreSnap, the director's opening shot")
    interest = payload.get("Interest")
    if not isinstance(interest, dict):
        err(path, "'Interest' must be an object")
        interest = {}
    for field in DIRECTOR_INTEREST_NUMBERS:
        value = interest.get(field)
        if not is_number(value) or value < 0:
            err(path, f"Interest.{field}: '{value}' must be a number, 0 or more")
    for field in ("ProximityRadiusCm", "BreakawaySpeedCms", "BigHitSeconds"):
        if is_number(interest.get(field)) and interest[field] <= 0:
            err(path, f"Interest.{field}: must be positive")
    extra = set(interest) - set(DIRECTOR_INTEREST_NUMBERS)
    if extra:
        err(path, f"Interest: unknown field(s) {sorted(extra)} - names must match FPSDirectorInterestTuning exactly")
    for field in DIRECTOR_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    if payload.get("CameraSide") not in (-1, 1) or isinstance(payload.get("CameraSide"), bool):
        err(path, f"CameraSide: '{payload.get('CameraSide')}' must be -1 (the -Y sideline) or 1")
    extra = set(payload) - set(DIRECTOR_NUMBERS) - {"bDirectorEnabled", "Shots", "CutRules", "Interest", "CameraSide"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSCameraDirectorTuning exactly")


BROADCAST_COLORS = ("HomeColor", "AwayColor", "BarColor", "TextColor", "RedZoneColor", "TwoMinuteColor", "TimeoutColor",
                    "TimeoutUsedColor", "ChyronColor")
BROADCAST_ANCHORS = {"BottomCenter", "TopCenter", "TopLeft"}
CHYRON_KINDS = ("ScoreAlert", "DriveSummary", "PlayStat", "StatLine", "Custom")
BROADCAST_FIELDS = {"HomeLabel", "AwayLabel", "bUseTeamColors", "Anchor", "ScoreFontSize", "TextFontSize", "RedZoneYardLine",
                    "TwoMinuteSeconds", "ChyronMaxQueued", "ChyronMinShowSeconds", "ChyronGapSeconds", "ChyronKinds",
                    *BROADCAST_COLORS}


def validate_broadcast_overlay(path, payload):
    """FPSBroadcastOverlayTheme (Data/broadcast_overlay.json, Epic 33); mirrors
    UPSOverlayBroadcastSubsystem::ValidateTheme."""
    for field in BROADCAST_COLORS:
        if not isinstance(payload.get(field), str) or not HEX_COLOR.match(payload[field]):
            err(path, f"{field}: '{payload.get(field)}' must be #RRGGBB")
    for field in ("HomeLabel", "AwayLabel"):
        if not isinstance(payload.get(field), str) or not payload[field]:
            err(path, f"{field}: must be a non-empty string")
    if not isinstance(payload.get("bUseTeamColors"), bool):
        err(path, "bUseTeamColors: must be true or false")
    if payload.get("Anchor") not in BROADCAST_ANCHORS:
        err(path, f"Anchor: '{payload.get('Anchor')}' must be one of {sorted(BROADCAST_ANCHORS)}")
    for field in ("ScoreFontSize", "TextFontSize", "ChyronMaxQueued"):
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 1:
            err(path, f"{field}: '{value}' must be a whole number, 1 or more")
    red_zone = payload.get("RedZoneYardLine")
    if not isinstance(red_zone, int) or isinstance(red_zone, bool) or not 1 <= red_zone <= 99:
        err(path, f"RedZoneYardLine: '{red_zone}' must be a whole number from 1 to 99")
    for field in ("TwoMinuteSeconds", "ChyronMinShowSeconds", "ChyronGapSeconds"):
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    extra = set(payload) - BROADCAST_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSBroadcastOverlayTheme exactly")
    kinds = payload.get("ChyronKinds")
    if not isinstance(kinds, list):
        err(path, "'ChyronKinds' must be an array")
        return
    seen = set()
    for idx, row in enumerate(kinds):
        where = f"ChyronKinds[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        kind = row.get("Kind")
        if kind not in CHYRON_KINDS:
            err(path, f"{where}.Kind: '{kind}' must be one of {list(CHYRON_KINDS)}")
        elif kind in seen:
            err(path, f"{where}.Kind: '{kind}' is listed twice")
        seen.add(kind)
        priority = row.get("Priority")
        if not isinstance(priority, int) or isinstance(priority, bool):
            err(path, f"{where}.Priority: '{priority}' must be a whole number")
        seconds = row.get("Seconds")
        if not is_number(seconds) or seconds <= 0:
            err(path, f"{where}.Seconds: '{seconds}' must be a number above 0")
        extra = set(row) - {"Kind", "Priority", "Seconds"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")
    for kind in CHYRON_KINDS:
        if kind not in seen:
            err(path, f"ChyronKinds: no '{kind}' entry")


SKYCAM_POSITIVE = ("AnchorHalfLengthCm", "AnchorHalfWidthCm", "AnchorHeightCm", "CatenaryParameterCm", "StiffnessPerSecSq",
                   "MaxSpeedCms", "MaxAccelerationCms2", "FieldOfView")
SKYCAM_NON_NEGATIVE = ("EdgeMarginCm", "MinHeightCm", "DampingPerSec", "BehindQuarterbackDistanceCm",
                       "BehindQuarterbackHeightCm", "ChaseDistanceCm", "ChaseHeightCm", "LookAheadCm")


def validate_skycam(path, payload):
    """FPSSkycamTuning (Data/camera_skycam.json, Epic 39); mirrors
    UPSCameraSkycamComponent::ValidateTuning, including the catenary ceiling over midfield."""
    for field in SKYCAM_POSITIVE:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a positive number")
    for field in SKYCAM_NON_NEGATIVE:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    extra = set(payload) - set(SKYCAM_POSITIVE) - set(SKYCAM_NON_NEGATIVE)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSSkycamTuning exactly")
    if not all(is_number(payload.get(f)) and payload[f] > 0 for f in SKYCAM_POSITIVE) \
            or not all(is_number(payload.get(f)) for f in SKYCAM_NON_NEGATIVE):
        return
    if payload["FieldOfView"] >= 170:
        err(path, "FieldOfView must be below 170")
    if payload["EdgeMarginCm"] >= min(payload["AnchorHalfLengthCm"], payload["AnchorHalfWidthCm"]):
        err(path, "EdgeMarginCm must leave room inside the towers")
    a = payload["CatenaryParameterCm"]
    ceiling = payload["AnchorHeightCm"] - sum(a * (math.cosh(span / a) - 1.0)
                                              for span in (payload["AnchorHalfLengthCm"], payload["AnchorHalfWidthCm"]))
    if payload["MinHeightCm"] >= ceiling:
        err(path, f"MinHeightCm ({payload['MinHeightCm']}) must be below the cables' ceiling over midfield ({ceiling:.0f})")


HINT_TRIGGERS = {"OffenseCall", "DefenseCall", "FourthDown", "TwoMinuteDrill", "Kickoff"}


def validate_ui_hints(path, payload):
    """FPSHintCatalog (Data/ui_hints.json, Epic 105.4); mirrors UPSUIHintSubsystem::ValidateCatalog."""
    hints = payload.get("Hints")
    if not isinstance(hints, list):
        err(path, "'Hints' must be an array")
        return
    seen = set()
    for idx, hint in enumerate(hints):
        where = f"Hints[{idx}]"
        if not isinstance(hint, dict):
            err(path, f"{where}: must be an object")
            continue
        hint_id = hint.get("HintId")
        if not isinstance(hint_id, str) or not hint_id or hint_id in seen:
            err(path, f"{where}.HintId: empty or used twice")
        seen.add(hint_id)
        if hint.get("Trigger") not in HINT_TRIGGERS:
            err(path, f"{where}.Trigger: '{hint.get('Trigger')}' is not an EPSHintTrigger ({sorted(HINT_TRIGGERS)})")
        if not isinstance(hint.get("Text"), str) or not hint["Text"].strip():
            err(path, f"{where}.Text: the hint needs text")
        extra = set(hint) - {"HintId", "Trigger", "Text"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPSHintDef exactly")
    extra = set(payload) - {"Hints"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSHintCatalog exactly")


def validate_ui_text():
    """Data/ui_text.csv, Data/ui_text_data.csv and the UI code's text (Epic 106); the checks
    live in tools/ui_text.py, which also regenerates ui_text_data.csv."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import ui_text
    for path, message in ui_text.problems():
        err(path, message)


BALL_FLIGHT_COLORS = ("ArcColor", "LandingColor", "LeadOnTargetColor", "LeadOffTargetColor", "GoodColor", "NoGoodColor")
BALL_FLIGHT_TEXTS = ("DotMeshPath", "RingMeshPath", "MaterialPath", "ColorParameter", "GoodLabel", "WideLeftLabel",
                     "WideRightLabel", "ShortLabel")
BALL_FLIGHT_POSITIVE = ("MeshDiameter", "ArcDotDiameter", "LandingRadiusFallback", "LeadRadius", "DeviationTolerance",
                        "MaxFlightSeconds", "UprightWidth", "ReadoutTextSize")
BALL_FLIGHT_NON_NEGATIVE = ("RingThickness", "GroundClearance", "LingerSeconds", "ReadoutSeconds", "CrossbarHeight",
                            "ReadoutHeight")
BALL_FLIGHT_FIELDS = {"ArcPoints", "GroundZ", "GoalPostX", "GoalPostY", *BALL_FLIGHT_COLORS, *BALL_FLIGHT_TEXTS,
                      *BALL_FLIGHT_POSITIVE, *BALL_FLIGHT_NON_NEGATIVE}


def validate_ball_flight_overlay(path, payload):
    """FPSBallFlightStyle (Data/ball_flight_overlay.json, Epic 32); mirrors
    UPSOverlayBallFlightSubsystem::ValidateStyle."""
    for field in BALL_FLIGHT_COLORS:
        if not isinstance(payload.get(field), str) or not HEX_COLOR.match(payload[field]):
            err(path, f"{field}: '{payload.get(field)}' must be #RRGGBB")
    for field in BALL_FLIGHT_TEXTS:
        if not isinstance(payload.get(field), str) or not payload[field]:
            err(path, f"{field}: must be a non-empty string")
    for field in BALL_FLIGHT_POSITIVE:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    for field in BALL_FLIGHT_NON_NEGATIVE:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in ("GroundZ", "GoalPostY"):
        if not is_number(payload.get(field)):
            err(path, f"{field}: '{payload.get(field)}' must be a number")
    points = payload.get("ArcPoints")
    if not isinstance(points, int) or isinstance(points, bool) or points < 2:
        err(path, f"ArcPoints: '{points}' must be a whole number, 2 or more (release and landing)")
    posts = payload.get("GoalPostX")
    if not isinstance(posts, list) or not posts or not all(is_number(x) for x in posts):
        err(path, "GoalPostX: must be a non-empty array of numbers (each end line's X)")
    extra = set(payload) - BALL_FLIGHT_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSBallFlightStyle exactly")


POCKET_FIELDS = ("PocketRadius", "EngagedPressureWeight", "EdgeWidth", "MinPressure", "CollapsePressure",
                 "EscapeRadius", "ClimbStopDistance", "SackImminentRadius", "StripBaseChance", "StripStrengthWeight",
                 "ThrowawayMinAwareness", "GroundingAvoidAwareness", "TackleBoxHalfWidth", "ThrowawayReceiverRange",
                 "ThrowawayShort", "ThrowawayDepth", "ThrowawayWidth", "ScrambleForwardBias", "ScrambleMaxSeconds",
                 "RunLaneClearance", "RunLaneWidth", "SlideTriggerRadius", "SlideMinGain", "ScrambleDrillDepth",
                 "ScrambleDrillWidth", "ScrambleDrillJitter", "ScrambleDeepDepth", "ScrambleDeepRunOn")


def validate_pocket_tuning(path, payload):
    """FPocketTuningRow (Data/pocket_tuning.json, Epic 71)."""
    for field in POCKET_FIELDS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in ("EngagedPressureWeight", "StripBaseChance"):
        if is_number(payload.get(field)) and payload[field] > 1:
            err(path, f"{field}: at most 1")
    for field in ("ThrowawayMinAwareness", "GroundingAvoidAwareness"):
        if is_number(payload.get(field)) and payload[field] > 100:
            err(path, f"{field}: ratings run 0-100")
    for low, high in (("MinPressure", "CollapsePressure"), ("ThrowawayMinAwareness", "GroundingAvoidAwareness")):
        if is_number(payload.get(low)) and is_number(payload.get(high)) and payload[low] > payload[high]:
            err(path, f"{low} must not exceed {high}")
    extra = set(payload) - set(POCKET_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPocketTuningRow exactly")


COVERAGE_MATCHUP_FIELDS = ("PressDepth", "PressShade", "PressAlignWidth", "PressMinJamChance", "PreSnapArrivalRadius",
                           "PressCushion", "PressBeatenSeconds", "LeverageShade", "LeverageLostMargin",
                           "LeverageRegainMargin", "LeverageBiteBonus", "BreakMinLateral", "IntoLeverageSeparationScale",
                           "AwayFromLeverageBonus", "SeparationRecoverySpeed", "MaxOutOfPhaseSeconds", "CarryMargin",
                           "ZoneCarryCushion", "VerticalCarryDepth", "DeepZoneDepth", "OverTopCushion", "DeepHelpWidth",
                           "DeepShadeWeight", "FieldWidth", "FreeDeepDepth", "RobberDepth", "RobberRadius",
                           "RobberJumpWeight", "ContactRadius", "TrailMargin", "FlagChance")
COVERAGE_MATCHUP_SHARES = ("PressMinJamChance", "LeverageBiteBonus", "BreakMinLateral", "IntoLeverageSeparationScale",
                           "DeepShadeWeight", "RobberJumpWeight", "FlagChance")
COVERAGE_LEVERAGES = {"Inside", "Outside"}
COVERAGE_FREE_ROLES = {"DeepMiddle", "Robber"}
COVERAGE_SHELL_FIELDS = {"Shell", "Leverage", "bPress", "FreeRoles"}


def validate_coverage_shell(path, where, rule):
    if not isinstance(rule, dict):
        err(path, f"{where}: must be an object")
        return
    if not isinstance(rule.get("Shell"), str):
        err(path, f"{where}.Shell: must be a string")
    if rule.get("Leverage") not in COVERAGE_LEVERAGES:
        err(path, f"{where}.Leverage: '{rule.get('Leverage')}' is not an EPSLeverage ({sorted(COVERAGE_LEVERAGES)})")
    if not isinstance(rule.get("bPress"), bool):
        err(path, f"{where}.bPress: must be true or false")
    roles = rule.get("FreeRoles")
    if not isinstance(roles, list) or any(role not in COVERAGE_FREE_ROLES for role in roles):
        err(path, f"{where}.FreeRoles: must be an array of {sorted(COVERAGE_FREE_ROLES)}")
    extra = set(rule) - COVERAGE_SHELL_FIELDS
    if extra:
        err(path, f"{where}: unknown field(s) {sorted(extra)}")


def validate_coverage_matchups(path, payload):
    """FPSCoverageMatchupTuning (Data/coverage_matchups.json, Epic 69); mirrors
    UPSCoverageMatchupSubsystem::ValidateTuning, plus its shells against the playbook and its press spot
    against the route-running model's PressRadius."""
    for field in COVERAGE_MATCHUP_FIELDS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in COVERAGE_MATCHUP_SHARES:
        if is_number(payload.get(field)) and payload[field] > 1:
            err(path, f"{field}: {payload[field]} is a share or chance, at most 1")
    for field in ("SeparationRecoverySpeed", "FieldWidth"):
        if is_number(payload.get(field)) and payload[field] <= 0:
            err(path, f"{field}: must be above 0")
    shells = payload.get("Shells")
    names = set()
    if not isinstance(shells, list):
        err(path, "'Shells' must be an array")
        shells = []
    for idx, rule in enumerate(shells):
        validate_coverage_shell(path, f"Shells[{idx}]", rule)
        name = rule.get("Shell") if isinstance(rule, dict) else None
        if isinstance(name, str):
            if not name:
                err(path, f"Shells[{idx}].Shell: must not be empty")
            elif name in names:
                err(path, f"Shells[{idx}].Shell: '{name}' is listed twice")
            names.add(name)
    validate_coverage_shell(path, "DefaultShell", payload.get("DefaultShell"))
    extra = set(payload) - set(COVERAGE_MATCHUP_FIELDS) - {"Shells", "DefaultShell"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSCoverageMatchupTuning exactly")

    # Every coverage shell a scrimmage defense (Base, Blitz, Prevent) calls has its rule; the kicking
    # game's returns and blocks play the default.
    try:
        plays = json.loads((DATA_DIR / "sample_playbook.json").read_text(encoding="utf-8")).get("Plays", [])
    except (OSError, json.JSONDecodeError, UnicodeDecodeError, AttributeError):
        plays = []
    for play in plays if isinstance(plays, list) else []:
        if not isinstance(play, dict) or play.get("bIsOffensivePlay", True) or play.get("PlayCategory") not in ("Base", "Blitz", "Prevent"):
            continue
        shell = play.get("CoverageShell")
        if shell and shell not in names:
            err(path, f"ShellAlignments: no rule for '{shell}', the coverage of {play.get('PlayId')} in sample_playbook.json")

    # A pressing defender must stand inside the release contest's PressRadius (Epic 68).
    try:
        press_radius = json.loads((DATA_DIR / "route_running.json").read_text(encoding="utf-8")).get("PressRadius")
    except (OSError, json.JSONDecodeError, UnicodeDecodeError, AttributeError):
        press_radius = None
    depth, shade = payload.get("PressDepth"), payload.get("PressShade")
    if is_number(press_radius) and is_number(depth) and is_number(shade) and math.hypot(depth, shade) > press_radius:
        err(path, f"PressDepth/PressShade: the press spot ({math.hypot(depth, shade):.0f} cm off the receiver) is outside "
                  f"route_running.json's PressRadius ({press_radius}): a pressing defender would not contest the release")


LOOSE_BALL_FIELDS = ("BlockedFieldGoalYards", "ChaseRadius", "RecoverRadius", "ScoopClearRadius", "SquirtDistance",
                     "RetrySeconds", "MaxLooseSeconds", "MaxReturnSeconds")


def validate_loose_ball(path, payload):
    """FPSLooseBallTuning (Data/loose_ball.json, Epic 17.4); mirrors UPSLooseBallSubsystem::ValidateTuning."""
    for field in LOOSE_BALL_FIELDS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    yards = payload.get("BlockedFieldGoalYards")
    if not isinstance(yards, int) or isinstance(yards, bool):
        err(path, "BlockedFieldGoalYards: must be a whole number of yards")
    for field in ("MaxLooseSeconds", "MaxReturnSeconds"):
        if is_number(payload.get(field)) and payload[field] <= 0:
            err(path, f"{field}: must be above 0")
    recover, chase = payload.get("RecoverRadius"), payload.get("ChaseRadius")
    if is_number(recover) and is_number(chase) and recover > chase:
        err(path, "RecoverRadius must not exceed ChaseRadius: a player close enough to take the ball must be chasing it")
    extra = set(payload) - set(LOOSE_BALL_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSLooseBallTuning exactly")


DECEPTION_FIELDS = ("FakeSeconds", "TendencyWindow", "MeshRideSeconds", "ReadMinSpeed", "KeyLineDepth",
                    "PitchReadRadius", "PitchWindowDepth", "MeshRecognizeRadius", "DisciplineAwareness",
                    "ScrapeRadius")


def validate_deception(path, payload):
    """FPSDeceptionTuning (Data/deception.json, Epic 72); mirrors UPSDeceptionSubsystem::ValidateTuning."""
    for field in DECEPTION_FIELDS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    discipline = payload.get("DisciplineAwareness")
    if is_number(discipline) and discipline > 100:
        err(path, f"DisciplineAwareness: {discipline} is a rating, 0-100")
    window = payload.get("TendencyWindow")
    if not isinstance(window, int) or isinstance(window, bool) or window < 1:
        err(path, f"TendencyWindow: '{window}' must be a whole number of calls, 1 or more")
    extra = set(payload) - set(DECEPTION_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSDeceptionTuning exactly")


PLAY_RECOGNITION_NUMBERS = ("UnderCenterMaxDepth", "PistolMaxDepth", "BackfieldMinDepth", "BoxHalfWidth", "StackWidth",
                            "OffsetWidth", "InlineWidth", "DefaultRunLean", "DropKeyDepth", "DropKeyRetreat",
                            "FlowMinSpeed", "LineKeyDistance", "PassReadScale", "RunReadScale", "ThrowReadScale",
                            "FakeReadScale", "ExpectationWeight", "TendencyWeight", "LatencyJitter", "MaxBiteSeconds")
FORMATION_CLASS_FIELDS = {"ClassId", "QBAlignment", "Backfield", "MinStrongSide", "MaxWeakSide", "MinTightEnds",
                          "MinSplitReceivers", "RunLean"}
QB_ALIGNMENTS = {"UnderCenter", "Pistol", "Shotgun"}
BACKFIELD_SETS = {"Empty", "Single", "Offset", "I", "Split", "Full"}


def is_count(value, floor=0):
    return isinstance(value, int) and not isinstance(value, bool) and value >= floor


def validate_play_recognition(path, payload):
    """FPSPlayRecognitionTuning (Data/play_recognition.json, Epic 80); mirrors
    PSPlayRecognition::ValidateTuning."""
    for field in PLAY_RECOGNITION_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in ("FlowMinSpeed", "LineKeyDistance"):
        value = payload.get(field)
        if is_number(value) and value <= 0:
            err(path, f"{field}: must be above 0")
    for field in ("DefaultRunLean", "TendencyWeight"):
        value = payload.get(field)
        if is_number(value) and value > 1:
            err(path, f"{field}: {value} runs from 0 to 1")
    jitter = payload.get("LatencyJitter")
    if is_number(jitter) and jitter >= 1:
        err(path, f"LatencyJitter: {jitter} is a fraction, 0 to below 1")
    under, pistol = payload.get("UnderCenterMaxDepth"), payload.get("PistolMaxDepth")
    if is_number(under) and is_number(pistol) and under > pistol:
        err(path, "PistolMaxDepth must not be inside UnderCenterMaxDepth")
    classes = payload.get("FormationClasses")
    if not isinstance(classes, list) or not classes:
        err(path, "FormationClasses: must be a non-empty array (with none, every look is Unknown)")
        classes = []
    seen = set()
    for idx, entry in enumerate(classes):
        where = f"FormationClasses[{idx}]"
        if not isinstance(entry, dict):
            err(path, f"{where}: must be an object")
            continue
        class_id = entry.get("ClassId")
        if not isinstance(class_id, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", class_id) or class_id == "Unknown" or class_id in seen:
            err(path, f"{where}.ClassId: '{class_id}' must be an identifier used once (not Unknown)")
        seen.add(class_id)
        if "QBAlignment" in entry and entry["QBAlignment"] not in QB_ALIGNMENTS:
            err(path, f"{where}.QBAlignment: '{entry['QBAlignment']}' must be one of {sorted(QB_ALIGNMENTS)} (leave it out for any)")
        if "Backfield" in entry and entry["Backfield"] not in BACKFIELD_SETS:
            err(path, f"{where}.Backfield: '{entry['Backfield']}' must be one of {sorted(BACKFIELD_SETS)} (leave it out for any)")
        for field in ("MinStrongSide", "MinTightEnds", "MinSplitReceivers"):
            if field in entry and not is_count(entry[field]):
                err(path, f"{where}.{field}: '{entry[field]}' must be a whole number, 0 or more")
        if "MaxWeakSide" in entry and not is_count(entry["MaxWeakSide"], -1):
            err(path, f"{where}.MaxWeakSide: '{entry['MaxWeakSide']}' must be a whole number, -1 (any) or more")
        lean = entry.get("RunLean")
        if not is_number(lean) or not 0 <= lean <= 1:
            err(path, f"{where}.RunLean: '{lean}' must be a number from 0 to 1")
        extra = set(entry) - FORMATION_CLASS_FIELDS
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPSFormationClassDef exactly")
    extra = set(payload) - set(PLAY_RECOGNITION_NUMBERS) - {"FormationClasses"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSPlayRecognitionTuning exactly")


TOUCH_KINDS = {"Stick", "Button", "Swipe"}
TOUCH_DIRECTIONS = {"Left", "Right", "Up", "Down"}
TOUCH_LAYOUT_FIELDS = {"SafeZone", "LayoutAspect", "bFloatingStick", "StickZone", "GestureZone",
                       "SwipeMinDistance", "SwipeMaxSeconds", "TouchControls", "TouchContexts",
                       "ContextsWithoutTouch"}


def zone_ok(zone):
    try:
        lo, hi = zone["Min"], zone["Max"]
        values = [lo["X"], lo["Y"], hi["X"], hi["Y"]]
    except (KeyError, TypeError):
        return False
    return all(is_number(v) and 0 <= v <= 1 for v in values) and lo["X"] < hi["X"] and lo["Y"] < hi["Y"]


def validate_touch_controls(path, payload, catalog, glyphs):
    """FPSTouchLayout (Data/touch_controls.json, Epic 130); mirrors
    PSTouchControls::ValidateLayout. catalog / glyphs are the parsed input catalog and glyph
    table, or None."""
    extra = set(payload) - TOUCH_LAYOUT_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSTouchLayout exactly")
    safe = payload.get("SafeZone")
    margins = [safe.get(k) for k in ("Left", "Top", "Right", "Bottom")] if isinstance(safe, dict) else []
    if (len(margins) != 4 or not all(is_number(m) and m >= 0 for m in margins)
            or margins[0] + margins[2] >= 1 or margins[1] + margins[3] >= 1):
        err(path, "SafeZone: margins must be numbers, 0 or more, leaving part of the screen on each axis")
    aspect = payload.get("LayoutAspect")
    if not is_number(aspect) or aspect <= 0:
        err(path, "LayoutAspect must be a positive number")
        aspect = 1.0
    for name in ("StickZone", "GestureZone"):
        if not zone_ok(payload.get(name)):
            err(path, f"{name} must satisfy 0 <= Min < Max <= 1 on both axes")
    for name in ("SwipeMinDistance", "SwipeMaxSeconds"):
        if not is_number(payload.get(name)) or payload[name] <= 0:
            err(path, f"{name} must be a positive number")

    controls = {}
    swipe_directions = set()
    sticks = 0
    for idx, control in enumerate(payload.get("TouchControls") or []):
        where = f"TouchControls[{idx}]"
        if not isinstance(control, dict):
            err(path, f"{where}: must be an object")
            continue
        cid = control.get("ControlId")
        if not isinstance(cid, str) or not cid or cid in controls:
            err(path, f"{where}.ControlId: empty or used twice")
        controls[cid] = control
        kind = control.get("Kind")
        if kind not in TOUCH_KINDS:
            err(path, f"{where}.Kind: '{kind}' is not an EPSTouchControlKind ({sorted(TOUCH_KINDS)})")
            continue
        if kind == "Swipe":
            direction = control.get("Direction")
            if direction not in TOUCH_DIRECTIONS or direction in swipe_directions:
                err(path, f"{where}: a swipe needs a Direction ({sorted(TOUCH_DIRECTIONS)}) no other swipe uses")
            swipe_directions.add(direction)
            continue
        if kind == "Stick":
            sticks += 1
        pos, radius = control.get("Position"), control.get("Radius")
        x, y = (pos.get("X"), pos.get("Y")) if isinstance(pos, dict) else (None, None)
        if not (is_number(x) and is_number(y) and 0 <= x <= 1 and 0 <= y <= 1 and is_number(radius) and radius > 0):
            err(path, f"{where} '{cid}': needs a Position inside the safe area and a positive Radius")
            continue
        if kind == "Button" and (y - radius < 0 or y + radius > 1 or x - radius / aspect < 0 or x + radius / aspect > 1):
            err(path, f"{where} '{cid}': the button reaches outside the safe area")
    if sticks > 1:
        err(path, f"the layout has {sticks} sticks; the touch layer drives one")
    buttons = [c for c in controls.values() if isinstance(c, dict) and c.get("Kind") == "Button"
               and isinstance(c.get("Position"), dict) and is_number(c.get("Radius"))
               and is_number(c["Position"].get("X")) and is_number(c["Position"].get("Y"))]
    for i, a in enumerate(buttons):
        for b in buttons[i + 1:]:
            dx = (a["Position"]["X"] - b["Position"]["X"]) * aspect
            dy = a["Position"]["Y"] - b["Position"]["Y"]
            if (dx * dx + dy * dy) ** 0.5 < a["Radius"] + b["Radius"]:
                err(path, f"buttons '{a.get('ControlId')}' and '{b.get('ControlId')}' overlap")

    catalog_contexts = {c.get("ContextId") for c in (catalog or {}).get("Contexts", []) if isinstance(c, dict)}
    actions = {a.get("ActionId"): a for a in (catalog or {}).get("Actions", []) if isinstance(a, dict)}
    bound_actions = set()
    seen_contexts = set()
    for idx, entry in enumerate(payload.get("TouchContexts") or []):
        if not isinstance(entry, dict):
            err(path, f"TouchContexts[{idx}]: must be an object")
            continue
        ctx = entry.get("ContextId")
        where = f"TouchContexts[{idx}] '{ctx}'"
        if not isinstance(ctx, str) or not ctx or ctx in seen_contexts:
            err(path, f"{where}: empty or listed twice")
        seen_contexts.add(ctx)
        known_context = catalog is None or ctx in catalog_contexts
        if not known_context:
            err(path, f"{where}: not a context in input_actions.json")
        bound_here, reached = set(), set()
        for bidx, binding in enumerate(entry.get("Bindings") or []):
            bwhere = f"{where}.Bindings[{bidx}]"
            if not isinstance(binding, dict):
                err(path, f"{bwhere}: must be an object")
                continue
            cid, aid = binding.get("ControlId"), binding.get("ActionId")
            control = controls.get(cid)
            if control is None:
                err(path, f"{bwhere}: no control '{cid}' in TouchControls")
            if cid in bound_here:
                err(path, f"{bwhere}: control '{cid}' bound twice in one context")
            bound_here.add(cid)
            if not isinstance(aid, str) or not aid:
                err(path, f"{bwhere}: no ActionId")
                continue
            bound_actions.add(aid)
            reached.add(aid)
            if catalog is None:
                continue
            action = actions.get(aid)
            if action is None:
                err(path, f"{bwhere}: '{aid}' is not an action in input_actions.json")
                continue
            if known_context and ctx not in (action.get("Contexts") or []):
                err(path, f"{bwhere}: '{aid}' does not live in context '{ctx}'")
            if isinstance(control, dict) and control.get("Kind") in TOUCH_KINDS:
                expected = "Axis2D" if control["Kind"] == "Stick" else "Boolean"
                if action.get("ValueType") != expected:
                    err(path, f"{bwhere}: a {control['Kind']} control needs an action of type {expected}, and '{aid}' is {action.get('ValueType')}")
            keys = [b.get("Key") for b in action.get("Bindings", []) if isinstance(b, dict) and isinstance(b.get("Key"), str)]
            if not any(is_gamepad_key(k) for k in keys):
                err(path, f"{bwhere}: '{aid}' has no gamepad binding for the touch value to go through")
        if catalog is not None and known_context:
            for aid, action in actions.items():
                if ctx in (action.get("Contexts") or []) and aid not in reached:
                    err(path, f"{where}: action '{aid}' has no touch control")

    # Every input context either has a touch button set or is listed as having none, so a new
    # context can't slip past touch unnoticed.
    without = payload.get("ContextsWithoutTouch") or []
    if not isinstance(without, list):
        err(path, "ContextsWithoutTouch must be an array of context IDs")
        without = []
    for cid in without:
        if cid in seen_contexts:
            err(path, f"ContextsWithoutTouch: '{cid}' also has a touch button set")
        elif catalog is not None and cid not in catalog_contexts:
            err(path, f"ContextsWithoutTouch: '{cid}' is not a context in input_actions.json")
    if catalog is not None:
        for cid in sorted(c for c in catalog_contexts if isinstance(c, str)):
            if cid not in seen_contexts and cid not in without:
                err(path, f"context '{cid}' has no touch button set: add it to TouchContexts (or, deliberately, to ContextsWithoutTouch)")

    if glyphs is None:
        return
    touch_sets = [s for s in glyphs.get("GlyphSets", [])
                  if isinstance(s, dict) and s.get("Device") == "Touch" and s.get("bDefaultForDevice")]
    if len(touch_sets) != 1:
        err(path, "input_glyphs.json needs exactly one default Touch glyph set")
        return
    drawn = {a.get("ActionId") for a in touch_sets[0].get("Actions", []) if isinstance(a, dict)}
    for aid in sorted(bound_actions - drawn):
        err(path, f"glyph set '{touch_sets[0].get('GlyphSetId')}' has no glyph for '{aid}', which a touch control drives")


def load_input_glyphs():
    """The glyph table touch controls must be drawn from, or None when missing or broken."""
    try:
        glyphs = json.loads((DATA_DIR / "input_glyphs.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    return glyphs if isinstance(glyphs, dict) else None


COACH_ROLES = ("HeadCoach", "OffensiveCoordinator", "DefensiveCoordinator")
OFFENSE_ROLES = {"Quarterback", "RunningBack", "WideReceiver", "TightEnd", "OffensiveLineman"}
FIT_ATTRIBUTES = {"Speed", "Agility", "Strength", "Acceleration", "Awareness", "Stamina"}
PASS_CATEGORIES = {"ShortPass", "DeepPass", "PlayAction", "Screen"}
SCHEME_FIELDS = {"SchemeId", "Label", "bOffense", "Formations", "CategoryWeights", "FitWeights", "Description"}
COACH_FIELDS = {"CoachId", "DisplayName", "Role", "SchemeId", "PlayCalling", "Development", "Aggression"}
STAFF_JOBS = {"HeadCoachId": "HeadCoach", "OffensiveCoordinatorId": "OffensiveCoordinator",
              "DefensiveCoordinatorId": "DefensiveCoordinator"}
STAFF_FIELDS = set(STAFF_JOBS) | {"TeamId", "HeadCoachSeasons"}
STAFF_TUNING_NUMBERS = ("MinSchemeAdherence", "MaxSchemeAdherence", "FitSpan", "BestFitMultiplier", "WorstFitMultiplier",
                        "PromotionBonus", "SchemeMatchBonus")
STAFF_TUNING_CHANCES = ("DevelopmentMisfitRelief", "FitLabelThreshold", "FireWinPercentage", "CoordinatorSafeWinPercentage",
                        "PromoteWinPercentage")
STAFF_TUNING_COUNTS = ("GraceSeasons", "CoordinatorFiresPerSide")


def load_playbook_plays():
    """The playbook's plays, or None when it is missing or broken (its own checks report that)."""
    try:
        playbook = json.loads((DATA_DIR / "sample_playbook.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    if not isinstance(playbook, dict) or not isinstance(playbook.get("Plays"), list):
        return None
    return [p for p in playbook["Plays"] if isinstance(p, dict)]


def load_team_ids():
    """The league's team IDs, or None when the teams file is missing or broken."""
    try:
        teams = json.loads((DATA_DIR / "sample_teams.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    if not isinstance(teams, dict) or not isinstance(teams.get("Teams"), list):
        return None
    return {t.get("TeamId") for t in teams["Teams"] if isinstance(t, dict)}


def validate_coaching_staffs(path, payload, plays, team_ids):
    """FPSCoachingLeague (Data/coaching_staffs.json, Epic 89); mirrors UPSStaffManager::Validate."""
    extra = set(payload) - {"Schemes", "Coaches", "Staffs", "Tuning"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSCoachingLeague exactly")

    schemes = {}
    for idx, row in enumerate(payload.get("Schemes") if isinstance(payload.get("Schemes"), list) else []):
        where = f"Schemes[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        unknown = set(row) - SCHEME_FIELDS
        if unknown:
            err(path, f"{where}: unknown field(s) {sorted(unknown)}")
        scheme_id = row.get("SchemeId")
        if not isinstance(scheme_id, str) or not scheme_id or scheme_id in schemes:
            err(path, f"{where}.SchemeId: '{scheme_id}' is empty or listed twice")
            continue
        schemes[scheme_id] = row
        where = f"Schemes '{scheme_id}'"
        offense = row.get("bOffense")
        if not isinstance(offense, bool):
            err(path, f"{where}.bOffense: must be true or false")
            continue
        if not isinstance(row.get("Label"), str) or not row["Label"]:
            err(path, f"{where}.Label: must be a non-empty string")
        formations = row.get("Formations")
        if not isinstance(formations, list) or not formations:
            err(path, f"{where}.Formations: must list at least one formation")
            formations = []
        weights = row.get("CategoryWeights")
        if not isinstance(weights, dict):
            err(path, f"{where}.CategoryWeights: must be an object")
            weights = {}
        if plays is not None:
            side = [p for p in plays if p.get("bIsOffensivePlay") is offense]
            for formation in formations:
                if not any(p.get("Formation") == formation for p in side):
                    err(path, f"{where}.Formations: '{formation}' is no {'offensive' if offense else 'defensive'} formation in sample_playbook.json")
            kept = {p.get("PlayCategory") for p in side if p.get("Formation") in formations}
            if offense and not ("Run" in kept and kept & PASS_CATEGORIES):
                err(path, f"{where}.Formations: the scheme's plays need a run and a pass")
            if not offense and "Base" not in kept:
                err(path, f"{where}.Formations: the scheme's plays need a Base defense")
            categories = {p.get("PlayCategory") for p in side}
            for category in weights:
                if category not in categories:
                    err(path, f"{where}.CategoryWeights: '{category}' is no {'offensive' if offense else 'defensive'} PlayCategory in sample_playbook.json")
        for category, value in weights.items():
            if not is_number(value) or value < 0:
                err(path, f"{where}.CategoryWeights.{category}: '{value}' must be a number, 0 or more")
        fit_weights = row.get("FitWeights")
        if not isinstance(fit_weights, list):
            err(path, f"{where}.FitWeights: must be an array")
            fit_weights = []
        for fidx, entry in enumerate(fit_weights):
            fwhere = f"{where}.FitWeights[{fidx}]"
            if not isinstance(entry, dict) or set(entry) != {"Role", "Attribute", "Weight"}:
                err(path, f"{fwhere}: must have exactly Role, Attribute and Weight")
                continue
            if entry["Role"] not in PLAYER_ROLES:
                err(path, f"{fwhere}.Role: '{entry['Role']}' is not an EPlayerRole")
            elif (entry["Role"] in OFFENSE_ROLES) != offense:
                err(path, f"{fwhere}.Role: {entry['Role']} plays the other side")
            if entry["Attribute"] not in FIT_ATTRIBUTES:
                err(path, f"{fwhere}.Attribute: '{entry['Attribute']}' must be one of {sorted(FIT_ATTRIBUTES)}")
            if not is_number(entry["Weight"]) or entry["Weight"] <= 0:
                err(path, f"{fwhere}.Weight: '{entry['Weight']}' must be above 0")

    coaches = {}
    for idx, row in enumerate(payload.get("Coaches") if isinstance(payload.get("Coaches"), list) else []):
        where = f"Coaches[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        if set(row) != COACH_FIELDS:
            err(path, f"{where}: fields must be exactly {sorted(COACH_FIELDS)}")
        coach_id = row.get("CoachId")
        if not isinstance(coach_id, str) or not coach_id or coach_id in coaches:
            err(path, f"{where}.CoachId: '{coach_id}' is empty or listed twice")
            continue
        coaches[coach_id] = row
        where = f"Coaches '{coach_id}'"
        role = row.get("Role")
        if role not in COACH_ROLES:
            err(path, f"{where}.Role: '{role}' must be one of {list(COACH_ROLES)}")
        scheme = schemes.get(row.get("SchemeId"))
        if scheme is None:
            err(path, f"{where}.SchemeId: '{row.get('SchemeId')}' is not a scheme in this file")
        elif role in ("OffensiveCoordinator", "DefensiveCoordinator") and scheme.get("bOffense") != (role == "OffensiveCoordinator"):
            err(path, f"{where}.SchemeId: a {role} cannot run the {scheme.get('Label')}")
        for field in ("PlayCalling", "Development"):
            value = row.get(field)
            if not is_number(value) or not 0 <= value <= 100:
                err(path, f"{where}.{field}: '{value}' must be 0-100")
        value = row.get("Aggression")
        if not is_number(value) or not 0 <= value <= 1:
            err(path, f"{where}.Aggression: '{value}' must be 0-1")

    teams = set()
    employed = set()
    for idx, row in enumerate(payload.get("Staffs") if isinstance(payload.get("Staffs"), list) else []):
        where = f"Staffs[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        if set(row) != STAFF_FIELDS:
            err(path, f"{where}: fields must be exactly {sorted(STAFF_FIELDS)}")
        team = row.get("TeamId")
        if not isinstance(team, str) or not team or team in teams:
            err(path, f"{where}.TeamId: '{team}' is empty or listed twice")
        elif team_ids is not None and team not in team_ids:
            err(path, f"{where}.TeamId: '{team}' is not a team in sample_teams.json")
        teams.add(team)
        for field, role in STAFF_JOBS.items():
            coach_id = row.get(field)
            if coach_id in (None, "", "None"):
                continue
            coach = coaches.get(coach_id)
            if coach is None:
                err(path, f"{where}.{field}: '{coach_id}' is not a coach in this file")
            elif coach.get("Role") != role:
                err(path, f"{where}.{field}: '{coach_id}' is not a {role}")
            if coach_id in employed:
                err(path, f"{where}.{field}: '{coach_id}' is on two staffs")
            employed.add(coach_id)
        seasons = row.get("HeadCoachSeasons")
        if not isinstance(seasons, int) or isinstance(seasons, bool) or seasons < 0:
            err(path, f"{where}.HeadCoachSeasons: '{seasons}' must be a whole number, 0 or more")

    tuning = payload.get("Tuning")
    if not isinstance(tuning, dict):
        err(path, "'Tuning' must be an object")
        return
    known = set(STAFF_TUNING_NUMBERS) | set(STAFF_TUNING_CHANCES) | set(STAFF_TUNING_COUNTS)
    if set(tuning) - known:
        err(path, f"Tuning: unknown field(s) {sorted(set(tuning) - known)} - names must match FPSStaffTuning exactly")
    for field in STAFF_TUNING_NUMBERS:
        if not is_number(tuning.get(field)) or tuning[field] < 0:
            err(path, f"Tuning.{field}: '{tuning.get(field)}' must be a number, 0 or more")
    for field in STAFF_TUNING_CHANCES:
        if not is_number(tuning.get(field)) or not 0 <= tuning[field] <= 1:
            err(path, f"Tuning.{field}: '{tuning.get(field)}' must be 0-1")
    for field in STAFF_TUNING_COUNTS:
        value = tuning.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            err(path, f"Tuning.{field}: '{value}' must be a whole number, 0 or more")
    if is_number(tuning.get("FitSpan")) and tuning["FitSpan"] <= 0:
        err(path, "Tuning.FitSpan: must be above 0")
    if is_number(tuning.get("BestFitMultiplier")) and tuning["BestFitMultiplier"] < 1:
        err(path, "Tuning.BestFitMultiplier: a fit never plays below his ratings (1 or more)")
    if is_number(tuning.get("WorstFitMultiplier")) and not 0 < tuning["WorstFitMultiplier"] <= 1:
        err(path, "Tuning.WorstFitMultiplier: must be above 0 and at most 1")


BADGE_GROUPS = ("Receiver", "Back", "Quarterback", "Line", "Defense")
BADGE_IN_PLAY = {"Hidden", "WhilePassing", "Always"}
BADGE_POSITIVE = ("BadgeWidth", "BadgeHeight", "ReferenceDistance", "MinScale", "MaxScale", "NudgeStep")
BADGE_NON_NEGATIVE = ("HeadClearance", "BallClearance", "FadeInSeconds")
BADGE_FIELDS = {"Groups", "RoleLabels", "FontSize", "MaxNudges", "bBadgeControlledPlayer", *BADGE_POSITIVE,
                *BADGE_NON_NEGATIVE}
BADGE_GROUP_FIELDS = {"Group", "Color", "TextColor", "bPreSnap", "InPlay", "bEssential"}


def validate_overlay_badges(path, payload):
    """FPSOverlayBadgeStyle (Data/overlay_badges.json, Epic 28); mirrors
    UPSOverlayBadgeComponent::ValidateStyle."""
    groups = payload.get("Groups")
    if not isinstance(groups, list):
        err(path, "'Groups' must be an array")
        groups = []
    seen = {}
    for idx, row in enumerate(groups):
        where = f"Groups[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        group = row.get("Group")
        if group not in BADGE_GROUPS:
            err(path, f"{where}.Group: '{group}' must be one of {list(BADGE_GROUPS)}")
        seen[group] = seen.get(group, 0) + 1
        for field in ("Color", "TextColor"):
            if not isinstance(row.get(field), str) or not HEX_COLOR.match(row[field]):
                err(path, f"{where}.{field}: '{row.get(field)}' must be #RRGGBB")
        for field in ("bPreSnap", "bEssential"):
            if not isinstance(row.get(field), bool):
                err(path, f"{where}.{field}: must be true or false")
        in_play = row.get("InPlay")
        if in_play not in BADGE_IN_PLAY:
            err(path, f"{where}.InPlay: '{in_play}' must be one of {sorted(BADGE_IN_PLAY)}")
        extra = set(row) - BADGE_GROUP_FIELDS
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")
    for group in BADGE_GROUPS:
        count = seen.get(group, 0)
        if count != 1:
            err(path, f"Groups: '{group}' is listed {count} times; it needs exactly one entry")
    labels = payload.get("RoleLabels")
    if not isinstance(labels, list):
        err(path, "'RoleLabels' must be an array")
        labels = []
    labelled = set()
    for idx, row in enumerate(labels):
        where = f"RoleLabels[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        role = row.get("Role")
        if role not in PLAYER_ROLES:
            err(path, f"{where}.Role: '{role}' is not an EPlayerRole")
        elif role in labelled:
            err(path, f"{where}.Role: '{role}' is listed twice")
        labelled.add(role)
        if not isinstance(row.get("Label"), str) or not row["Label"]:
            err(path, f"{where}.Label: must be a non-empty string")
        extra = set(row) - {"Role", "Label"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")
    for role in sorted(set(PLAYER_ROLES) - labelled):
        err(path, f"RoleLabels: no label for '{role}'")
    for field in BADGE_POSITIVE:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    for field in BADGE_NON_NEGATIVE:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    low, high = payload.get("MinScale"), payload.get("MaxScale")
    if is_number(low) and is_number(high) and high < low:
        err(path, "MaxScale must not be below MinScale")
    for field, floor in (("FontSize", 1), ("MaxNudges", 0)):
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < floor:
            err(path, f"{field}: '{value}' must be a whole number, {floor} or more")
    if not isinstance(payload.get("bBadgeControlledPlayer"), bool):
        err(path, "bBadgeControlledPlayer: must be true or false")
    extra = set(payload) - BADGE_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSOverlayBadgeStyle exactly")


EMPHASIS_KINDS = ("Highlight", "Mismatch", "Focus")


def validate_player_emphasis(path, payload):
    """FPSEmphasisStyle (Data/player_emphasis.json, Epic 36); mirrors
    UPSOverlayEmphasisSubsystem::ValidateStyle."""
    kinds = payload.get("Kinds")
    if not isinstance(kinds, list):
        err(path, "'Kinds' must be an array")
        kinds = []
    seen = {}
    stencils = []
    for idx, row in enumerate(kinds):
        where = f"Kinds[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        kind = row.get("Kind")
        if kind not in EMPHASIS_KINDS:
            err(path, f"{where}.Kind: '{kind}' must be one of {list(EMPHASIS_KINDS)}")
        seen[kind] = seen.get(kind, 0) + 1
        stencil = row.get("Stencil")
        if not isinstance(stencil, int) or isinstance(stencil, bool) or not 1 <= stencil <= 255:
            err(path, f"{where}.Stencil: '{stencil}' must be a whole number from 1 to 255")
        stencils.append(stencil)
        priority = row.get("Priority")
        if not isinstance(priority, int) or isinstance(priority, bool):
            err(path, f"{where}.Priority: '{priority}' must be a whole number")
        extra = set(row) - {"Kind", "Stencil", "Priority"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")
    for kind in EMPHASIS_KINDS:
        count = seen.get(kind, 0)
        if count != 1:
            err(path, f"Kinds: '{kind}' is listed {count} times; it needs exactly one entry")
    dim = payload.get("DimStencil")
    if not isinstance(dim, int) or isinstance(dim, bool) or not 1 <= dim <= 255:
        err(path, f"DimStencil: '{dim}' must be a whole number from 1 to 255")
    stencils.append(dim)
    if len(set(map(str, stencils))) != len(stencils):
        err(path, "Stencil values must differ: the emphasis material tells the looks apart by them")
    most = payload.get("MaxEmphasized")
    if not isinstance(most, int) or isinstance(most, bool) or most < 1:
        err(path, f"MaxEmphasized: '{most}' must be a whole number, 1 or more")
    if not isinstance(payload.get("bSpotlightDimsEmphasized"), bool):
        err(path, "bSpotlightDimsEmphasized: must be true or false")
    extra = set(payload) - {"Kinds", "DimStencil", "MaxEmphasized", "bSpotlightDimsEmphasized"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSEmphasisStyle exactly")


PLAYER_ATTRIBUTES_HEADER = REPO / "Source" / "PlaySports" / "Public" / "PSPlayerAttributes.h"
# What a DNA binding's Target names: its tuning file, whose numeric fields are the struct's.
DNA_BINDING_TARGETS = {
    "SkillAI": "skill_ai_tuning.json",
    "Pocket": "pocket_tuning.json",
    "DefenderAI": "defense_ai_tuning.json",
    "RouteRunning": "route_running.json",
    "Recognition": "play_recognition.json",
}
DNA_AXIS_FIELDS = ("Axis", "Roles", "LowTrait", "LowLabel", "LowDescription", "HighTrait", "HighLabel",
                   "HighDescription", "Generator")
DNA_GENERATOR_FIELDS = ("HighAttribute", "LowAttribute", "RatingLean", "Spread")
DNA_BINDING_FIELDS = ("Axis", "Target", "Field", "AtLow", "AtHigh")
DNA_CATALOG_FIELDS = ("Axes", "Bindings", "RushMoveLeans", "RushStyleWeight", "TraitThreshold")


def dna_axes():
    """FPSPlayerDNA's float fields, read from PSPlayerAttributes.h so a new axis needs no edit
    here; None when the header can't be read."""
    try:
        text = PLAYER_ATTRIBUTES_HEADER.read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r"struct\s+FPSPlayerDNA\s*\{(.*?)\n\};", text, re.S)
    if not match:
        return None
    return set(re.findall(r"^\s*float\s+(\w+)\s*=", match.group(1), re.M))


def load_dna_catalog():
    """Data/player_dna.json, or None when it is missing or broken (its own checks report that)."""
    try:
        catalog = json.loads((DATA_DIR / "player_dna.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    return catalog if isinstance(catalog, dict) else None


def validate_player_dna(path, where, row, catalog):
    """A player's optional "DNA" (FPSPlayerDNA, Epic 79): an object of axes the catalog lists
    for his role, each a number from -1 to 1."""
    dna = row.get("DNA")
    if not isinstance(dna, dict):
        err(path, f"{where}.DNA: must be an object of style axes")
        return
    axes = {a.get("Axis"): a for a in (catalog or {}).get("Axes", []) if isinstance(a, dict)}
    for axis, value in dna.items():
        if not is_number(value) or not -1 <= value <= 1:
            err(path, f"{where}.DNA.{axis}: '{value}' must be a number from -1 to 1")
        if catalog is None:
            continue
        if axis not in axes:
            err(path, f"{where}.DNA.{axis}: not an axis in player_dna.json ({sorted(axes)})")
        elif row.get("Role") not in (axes[axis].get("Roles") or []):
            err(path, f"{where}.DNA.{axis}: doesn't apply to a {row.get('Role')} (player_dna.json lists it for {axes[axis].get('Roles')})")


def validate_player_dna_catalog(path, payload):
    """FPSPlayerDNACatalog (Data/player_dna.json, Epic 79); mirrors PSPlayerDNA::ValidateCatalog."""
    extra = set(payload) - set(DNA_CATALOG_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSPlayerDNACatalog exactly")
    weight, threshold = payload.get("RushStyleWeight"), payload.get("TraitThreshold")
    if not is_number(weight) or not 0 <= weight < 1:
        err(path, f"RushStyleWeight: '{weight}' must be a number from 0 to below 1")
    if not is_number(threshold) or not 0 < threshold <= 1:
        err(path, f"TraitThreshold: '{threshold}' must be a number above 0, at most 1")
    known_axes = dna_axes()
    axes, traits = set(), set()
    for idx, axis in enumerate(payload.get("Axes") if isinstance(payload.get("Axes"), list) else []):
        where = f"Axes[{idx}]"
        if not isinstance(axis, dict):
            err(path, f"{where}: must be an object")
            continue
        name = axis.get("Axis")
        if known_axes is not None and name not in known_axes:
            err(path, f"{where}.Axis: '{name}' is not an FPSPlayerDNA field ({sorted(known_axes)})")
        elif name in axes:
            err(path, f"{where}.Axis: '{name}' is listed twice")
        axes.add(name)
        roles = axis.get("Roles")
        if not isinstance(roles, list) or not roles or any(r not in PLAYER_ROLES for r in roles):
            err(path, f"{where}.Roles: '{roles}' must be a non-empty list of EPlayerRole names")
        for end in ("Low", "High"):
            trait = axis.get(f"{end}Trait")
            if not isinstance(trait, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", trait):
                err(path, f"{where}.{end}Trait: '{trait}' must be an identifier (it names the trait's text rows)")
            elif trait in traits:
                err(path, f"{where}.{end}Trait: '{trait}' is used twice")
            traits.add(trait)
            for field in (f"{end}Label", f"{end}Description"):
                if not isinstance(axis.get(field), str) or not axis.get(field).strip():
                    err(path, f"{where}.{field}: must be a non-empty string")
        generator = axis.get("Generator")
        if not isinstance(generator, dict):
            err(path, f"{where}.Generator: must be an object")
        else:
            for field in ("HighAttribute", "LowAttribute"):
                if generator.get(field) not in content_contracts.RATING_FIELDS:
                    err(path, f"{where}.Generator.{field}: '{generator.get(field)}' must be one of {list(content_contracts.RATING_FIELDS)}")
            for field in ("RatingLean", "Spread"):
                if not is_number(generator.get(field)) or generator[field] < 0:
                    err(path, f"{where}.Generator.{field}: '{generator.get(field)}' must be a number, 0 or more")
            if set(generator) - set(DNA_GENERATOR_FIELDS):
                err(path, f"{where}.Generator: unknown field(s) {sorted(set(generator) - set(DNA_GENERATOR_FIELDS))}")
        if set(axis) - set(DNA_AXIS_FIELDS):
            err(path, f"{where}: unknown field(s) {sorted(set(axis) - set(DNA_AXIS_FIELDS))}")
    target_fields = {}
    for target, filename in DNA_BINDING_TARGETS.items():
        try:
            tuning = json.loads((DATA_DIR / filename).read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError, UnicodeDecodeError):
            continue
        if isinstance(tuning, dict):
            target_fields[target] = {k for k, v in tuning.items() if is_number(v)}
    for idx, binding in enumerate(payload.get("Bindings") if isinstance(payload.get("Bindings"), list) else []):
        where = f"Bindings[{idx}]"
        if not isinstance(binding, dict):
            err(path, f"{where}: must be an object")
            continue
        if binding.get("Axis") not in axes:
            err(path, f"{where}.Axis: '{binding.get('Axis')}' is not in Axes")
        target, field = binding.get("Target"), binding.get("Field")
        if target not in DNA_BINDING_TARGETS:
            err(path, f"{where}.Target: '{target}' must be one of {sorted(DNA_BINDING_TARGETS)}")
        elif target in target_fields and field not in target_fields[target]:
            err(path, f"{where}.Field: '{field}' is not a number in {DNA_BINDING_TARGETS[target]}")
        for end in ("AtLow", "AtHigh"):
            if not is_number(binding.get(end)) or binding[end] <= 0:
                err(path, f"{where}.{end}: '{binding.get(end)}' must be a multiplier above 0")
        if set(binding) - set(DNA_BINDING_FIELDS):
            err(path, f"{where}: unknown field(s) {sorted(set(binding) - set(DNA_BINDING_FIELDS))}")
    try:
        rush = json.loads((DATA_DIR / "pass_rush_moves.json").read_text(encoding="utf-8"))
        rush_moves = {m.get("Move") for m in rush.get("RushMoves", []) if isinstance(m, dict)}
    except (OSError, json.JSONDecodeError, UnicodeDecodeError, AttributeError):
        rush_moves = RUSH_MOVES
    seen = set()
    for idx, lean in enumerate(payload.get("RushMoveLeans") if isinstance(payload.get("RushMoveLeans"), list) else []):
        where = f"RushMoveLeans[{idx}]"
        if not isinstance(lean, dict):
            err(path, f"{where}: must be an object")
            continue
        move = lean.get("Move")
        if move not in rush_moves:
            err(path, f"{where}.Move: '{move}' is not a move in pass_rush_moves.json")
        elif move in seen:
            err(path, f"{where}.Move: '{move}' is listed twice")
        seen.add(move)
        if not is_number(lean.get("Lean")) or not -1 <= lean["Lean"] <= 1:
            err(path, f"{where}.Lean: '{lean.get('Lean')}' must be a number from -1 to 1")
        if set(lean) - {"Move", "Lean"}:
            err(path, f"{where}: unknown field(s) {sorted(set(lean) - {'Move', 'Lean'})}")


CONTRACT_INT_FIELDS = {
    "FirstLeagueYear", "SalaryCap", "MinimumSalary", "MaxContractYears", "MaxProrationYears",
    "PrimeAge", "DeclineAge", "FreeAgencyDays", "DecisionDays", "AIOffersPerDay", "DefaultPlayerAge",
}
CONTRACT_FLOAT_FIELDS = {
    "CapGrowthRate", "MaxCarryoverFraction", "ReplacementRating", "EliteRating", "DemandCurveExponent",
    "YearsLostPerYearPastPrime", "AgeDiscountPerYear", "MinAgeMultiplier", "MinGuaranteeFraction",
    "MaxGuaranteeFraction", "MarketSpaceWeight", "NeutralCapSpaceFraction", "MaxMarketAdjustment",
    "MoraleLoyaltyWeight", "GuaranteeValueWeight", "YearsMismatchPenalty", "AcceptRatio", "WalkAwayRatio",
    "InstantAcceptRatio", "DemandDecayPerDay", "DemandFloorFraction", "AIBidRatio", "AINeedPremium",
    "AICapCushionFraction",
}
CONTRACT_FRACTIONS = {
    "MaxCarryoverFraction", "MinAgeMultiplier", "MinGuaranteeFraction", "MaxGuaranteeFraction",
    "NeutralCapSpaceFraction", "MaxMarketAdjustment", "MoraleLoyaltyWeight", "AICapCushionFraction",
    "DemandFloorFraction",
}


def validate_contract_tuning(path, payload):
    """FPSContractTuning (Data/contracts.json, Epic 87); mirrors UPSContractManager::ValidateTuning."""
    for field in sorted(CONTRACT_INT_FIELDS):
        value = payload.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            err(path, f"{field}: '{value}' must be a whole number, 0 or more")
    for field in sorted(CONTRACT_FLOAT_FIELDS):
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
        elif field in CONTRACT_FRACTIONS and value > 1:
            err(path, f"{field}: a fraction, at most 1")
    extra = set(payload) - CONTRACT_INT_FIELDS - CONTRACT_FLOAT_FIELDS - {"PositionMarkets"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSContractTuning exactly")

    def num(field):
        value = payload.get(field)
        return value if is_number(value) else None

    def ordered(low, high, strict=False):
        a, b = num(low), num(high)
        if a is not None and b is not None and (a >= b if strict else a > b):
            err(path, f"{low} must be {'below' if strict else 'at most'} {high}")

    for field in ("SalaryCap", "MinimumSalary", "MaxContractYears", "MaxProrationYears", "FreeAgencyDays",
                  "DecisionDays", "DemandCurveExponent", "WalkAwayRatio", "DemandFloorFraction"):
        if num(field) == 0:
            err(path, f"{field}: must be above 0")
    ordered("MinimumSalary", "SalaryCap", strict=True)
    ordered("ReplacementRating", "EliteRating", strict=True)
    ordered("MinGuaranteeFraction", "MaxGuaranteeFraction")
    ordered("WalkAwayRatio", "AcceptRatio")
    ordered("AcceptRatio", "InstantAcceptRatio")
    ordered("PrimeAge", "DeclineAge")
    if num("DemandDecayPerDay") is not None and num("DemandDecayPerDay") >= 1:
        err(path, "DemandDecayPerDay: must be below 1")
    for field in ("ReplacementRating", "EliteRating"):
        if num(field) is not None and num(field) > 100:
            err(path, f"{field}: ratings run 0-100")

    markets = payload.get("PositionMarkets")
    if not isinstance(markets, list):
        err(path, "'PositionMarkets' must be an array")
        return
    seen = set()
    for idx, market in enumerate(markets):
        where = f"PositionMarkets[{idx}]"
        if not isinstance(market, dict):
            err(path, f"{where}: not an object")
            continue
        role = market.get("Role")
        if role not in PLAYER_ROLES:
            err(path, f"{where}.Role: '{role}' is not a valid EPlayerRole")
        elif role in seen:
            err(path, f"{where}.Role: duplicate '{role}'")
        seen.add(role)
        top = market.get("TopCapFraction")
        if not is_number(top) or not 0 < top <= 1:
            err(path, f"{where}.TopCapFraction: '{top}' must be a fraction above 0, at most 1")
        target = market.get("RosterTarget")
        if isinstance(target, bool) or not isinstance(target, int) or target < 0:
            err(path, f"{where}.RosterTarget: '{target}' must be a whole number, 0 or more")
        extra = set(market) - {"Role", "TopCapFraction", "RosterTarget"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPSPositionMarket exactly")
    missing = PLAYER_ROLES - seen
    if missing:
        err(path, f"PositionMarkets: no market for {sorted(missing)}")


DEFENSIVE_PRESNAP_NUMBERS = ("TwoHighDepth", "TwoHighWidth", "SingleHighDepth", "RobberDepth", "RobberWidth",
                             "DeepSafetyDepth", "ShowBlitzDepth", "BlitzLookDepth", "BlitzLookWidth", "CreepDelaySeconds")
DEFENSIVE_PRESNAP_FRACTIONS = ("CreepSpeedScale", "MaxDisguiseLeak", "DisguiseChanceConservative", "DisguiseChanceAggressive",
                               "ShowBlitzChanceConservative", "ShowBlitzChanceAggressive", "CreepChanceConservative",
                               "CreepChanceAggressive")
DEFENSIVE_PRESNAP_ACTIONS = ("AudibleAction", "SelectAction", "ShadowAction", "ShowBlitzAction", "DisguiseAction",
                             "CreepAction")


def validate_defensive_presnap(path, payload, catalog):
    """FPSDefensivePreSnapTuning (Data/defensive_presnap.json, Epic 67); mirrors
    UPSDefenderPreSnapSubsystem::ValidateTuning plus the catalog cross-check."""
    for field in DEFENSIVE_PRESNAP_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in DEFENSIVE_PRESNAP_FRACTIONS:
        value = payload.get(field)
        if not is_number(value) or not 0 <= value <= 1:
            err(path, f"{field}: '{value}' must be a number from 0 to 1")
    count = payload.get("ShowBlitzCount")
    if not isinstance(count, int) or isinstance(count, bool) or count < 0:
        err(path, f"ShowBlitzCount: '{count}' must be a whole number, 0 or more")
    if not isinstance(payload.get("bCpuShadowsTopReceiver"), bool):
        err(path, "bCpuShadowsTopReceiver: must be true or false")
    deep, robber = payload.get("DeepSafetyDepth"), payload.get("RobberDepth")
    high = [payload.get(f) for f in ("TwoHighDepth", "SingleHighDepth")]
    if is_number(deep) and is_number(robber) and all(is_number(h) for h in high) and not robber < deep <= min(high):
        err(path, "DeepSafetyDepth must lie past RobberDepth and no deeper than the deep safeties' spots")
    show, look = payload.get("ShowBlitzDepth"), payload.get("BlitzLookDepth")
    if is_number(show) and is_number(look) and show > look:
        err(path, "ShowBlitzDepth must be within BlitzLookDepth, or a shown blitz can't be seen")
    shells = payload.get("ShellSafeties")
    if not isinstance(shells, list):
        err(path, "'ShellSafeties' must be an array")
        shells = []
    seen = set()
    for idx, row in enumerate(shells):
        where = f"ShellSafeties[{idx}]"
        shell = row.get("Shell") if isinstance(row, dict) else None
        deep_count = row.get("DeepSafeties") if isinstance(row, dict) else None
        if not isinstance(shell, str) or not shell or shell in seen:
            err(path, f"{where}.Shell: empty or listed twice")
        seen.add(shell)
        if deep_count not in (0, 1, 2) or isinstance(deep_count, bool):
            err(path, f"{where}.DeepSafeties: '{deep_count}' must be 0, 1 or 2")
    extra = set(payload) - set(DEFENSIVE_PRESNAP_NUMBERS) - set(DEFENSIVE_PRESNAP_FRACTIONS) - set(DEFENSIVE_PRESNAP_ACTIONS) \
        - {"ShellSafeties", "ShowBlitzCount", "bCpuShadowsTopReceiver"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSDefensivePreSnapTuning exactly")
    if catalog is None:
        return
    actions = {a.get("ActionId"): a for a in catalog.get("Actions", []) if isinstance(a, dict)}
    used = [payload.get(field) for field in DEFENSIVE_PRESNAP_ACTIONS]
    if len(set(used)) != len(used):
        err(path, "each defensive pre-snap button must be a different action")
    for field in DEFENSIVE_PRESNAP_ACTIONS:
        action_id = payload.get(field)
        action = actions.get(action_id)
        if action is None:
            err(path, f"{field}: '{action_id}' is not an action in input_actions.json")
        elif action.get("ValueType") != "Boolean" or "DefensePreSnap" not in (action.get("Contexts") or []):
            err(path, f"{field}: '{action_id}' must be a Boolean action in the DefensePreSnap context")


OFFENSE_ROLES = {"Quarterback", "RunningBack", "WideReceiver", "TightEnd", "OffensiveLineman"}
DEFENSE_ROLES = {"DefensiveLineman", "Linebacker", "DefensiveBack"}
PERSONNEL_PANEL_FIELDS = {"OffenseRoles", "DefenseRoles", "OffenseNameFormat", "DefenseNames", "DefenseNameFallback", "PanelColor",
                          "TextColor", "FlashColor", "FontSize", "TitleFontSize", "ChangeFlashSeconds", "bShowInPlay"}


def validate_personnel_panel(path, payload):
    """FPSPersonnelPanelStyle (Data/personnel_panel.json, Epic 29); mirrors
    UPSOverlayPersonnelSubsystem::ValidateStyle."""
    for field, side_roles in (("OffenseRoles", OFFENSE_ROLES), ("DefenseRoles", DEFENSE_ROLES)):
        rows = payload.get(field)
        if not isinstance(rows, list) or not rows:
            err(path, f"'{field}' must be a non-empty array")
            continue
        seen = set()
        for idx, row in enumerate(rows):
            where = f"{field}[{idx}]"
            if not isinstance(row, dict):
                err(path, f"{where}: must be an object")
                continue
            role = row.get("Role")
            if role not in side_roles:
                err(path, f"{where}.Role: '{role}' must be one of {sorted(side_roles)}")
            elif role in seen:
                err(path, f"{where}.Role: '{role}' is listed twice")
            seen.add(role)
            if not isinstance(row.get("Label"), str) or not row["Label"]:
                err(path, f"{where}.Label: must be a non-empty string")
            extra = set(row) - {"Role", "Label"}
            if extra:
                err(path, f"{where}: unknown field(s) {sorted(extra)}")
    backs = set()
    names = payload.get("DefenseNames")
    if not isinstance(names, list):
        err(path, "'DefenseNames' must be an array")
        names = []
    for idx, row in enumerate(names):
        where = f"DefenseNames[{idx}]"
        if not isinstance(row, dict):
            err(path, f"{where}: must be an object")
            continue
        count = row.get("DefensiveBacks")
        if not isinstance(count, int) or isinstance(count, bool) or count < 0:
            err(path, f"{where}.DefensiveBacks: '{count}' must be a whole number, 0 or more")
        elif count in backs:
            err(path, f"{where}.DefensiveBacks: {count} is named twice")
        backs.add(count)
        if not isinstance(row.get("Name"), str) or not row["Name"]:
            err(path, f"{where}.Name: must be a non-empty string")
        extra = set(row) - {"DefensiveBacks", "Name"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)}")
    for field in ("OffenseNameFormat", "DefenseNameFallback"):
        if not isinstance(payload.get(field), str) or not payload[field]:
            err(path, f"{field}: must be a non-empty string")
    for field in ("PanelColor", "TextColor", "FlashColor"):
        if not isinstance(payload.get(field), str) or not HEX_COLOR.match(payload[field]):
            err(path, f"{field}: '{payload.get(field)}' must be #RRGGBB")
    for field in ("FontSize", "TitleFontSize"):
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 1:
            err(path, f"{field}: '{value}' must be a whole number, 1 or more")
    flash = payload.get("ChangeFlashSeconds")
    if not is_number(flash) or flash < 0:
        err(path, f"ChangeFlashSeconds: '{flash}' must be a number, 0 or more")
    if not isinstance(payload.get("bShowInPlay"), bool):
        err(path, "bShowInPlay: must be true or false")
    extra = set(payload) - PERSONNEL_PANEL_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSPersonnelPanelStyle exactly")


OPPONENT_MODEL_FIELDS = ("DistanceBuckets", "MinSamples", "PriorGameWeight", "FirstHalfStrength", "SecondHalfStrength",
                         "HalftimeQuarter", "DefaultAdaptationDial", "MinMultiplier", "MaxMultiplier", "Counters")
# The categories the coaching AI weights (not the clock's or special teams' calls, which the
# situation calls for): the ones a tendency can be read in and countered with.
WEIGHTED_OFFENSE_CATEGORIES = {"Run", "ShortPass", "DeepPass", "PlayAction", "Screen"}
WEIGHTED_DEFENSE_CATEGORIES = {"Base", "Blitz", "Prevent"}


def validate_opponent_model(path, payload):
    """FPSOpponentModelTuning (Data/opponent_model.json, Epic 78); mirrors
    PSOpponentModel::ValidateTuning."""
    extra = set(payload) - set(OPPONENT_MODEL_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSOpponentModelTuning exactly")
    buckets = payload.get("DistanceBuckets")
    if (not isinstance(buckets, list) or any(not isinstance(b, int) or isinstance(b, bool) or b < 1 for b in buckets)
            or any(b <= a for a, b in zip(buckets, buckets[1:]))):
        err(path, f"DistanceBuckets: '{buckets}' must be rising whole numbers of yards, 1 or more")
    if not is_number(payload.get("MinSamples")) or payload["MinSamples"] < 1:
        err(path, "MinSamples: a number, 1 or more")
    for field in ("PriorGameWeight", "FirstHalfStrength", "SecondHalfStrength", "DefaultAdaptationDial"):
        if not is_number(payload.get(field)) or not 0 <= payload[field] <= 1:
            err(path, f"{field}: '{payload.get(field)}' must be a number from 0 to 1")
    quarter = payload.get("HalftimeQuarter")
    if not isinstance(quarter, int) or isinstance(quarter, bool) or quarter < 2:
        err(path, f"HalftimeQuarter: '{quarter}' must be a whole number, 2 or more")
    low, high = payload.get("MinMultiplier"), payload.get("MaxMultiplier")
    if not is_number(low) or not 0 < low <= 1 or not is_number(high) or high < 1:
        err(path, "MinMultiplier must be above 0 and at most 1, MaxMultiplier at least 1")
    counters = payload.get("Counters")
    if not isinstance(counters, list):
        err(path, "'Counters' must be an array")
        return
    seen = set()
    for idx, counter in enumerate(counters):
        where = f"Counters[{idx}]"
        if not isinstance(counter, dict):
            err(path, f"{where}: must be an object")
            continue
        offense = counter.get("bOffense")
        if not isinstance(offense, bool):
            err(path, f"{where}.bOffense: must be true or false (the human's side)")
            continue
        observed_set = WEIGHTED_OFFENSE_CATEGORIES if offense else WEIGHTED_DEFENSE_CATEGORIES
        counter_set = WEIGHTED_DEFENSE_CATEGORIES if offense else WEIGHTED_OFFENSE_CATEGORIES
        if counter.get("Observed") not in observed_set:
            err(path, f"{where}.Observed: '{counter.get('Observed')}' must be one of {sorted(observed_set)}")
        if counter.get("Counter") not in counter_set:
            err(path, f"{where}.Counter: '{counter.get('Counter')}' must be one of {sorted(counter_set)} (the CPU's side)")
        if not is_number(counter.get("Weight")):
            err(path, f"{where}.Weight: must be a number")
        key = (offense, counter.get("Observed"), counter.get("Counter"))
        if key in seen:
            err(path, f"{where}: {key[1]} -> {key[2]} is listed twice")
        seen.add(key)
        if set(counter) - {"bOffense", "Observed", "Counter", "Weight"}:
            err(path, f"{where}: unknown field(s) {sorted(set(counter) - {'bOffense', 'Observed', 'Counter', 'Weight'})}")


VERSUS_SCREENS = {"Shared", "Split"}
VERSUS_AUDIENCES = {"Everyone", "OwnerOnly", "Nobody"}
VERSUS_FLAGS = ("bResetControlEachDown", "bDefenseSwitchDuringPlay", "bDefensePreSnapPicks", "bPauseOnlyBetweenPlays",
                "bResumeNeedsBoth", "bPauseOnDisconnect", "bQuitForfeits")
VERSUS_FIELDS = {"OffenseControlRole", "DefenseControlRole", "Screen", "RouteArtAudience", "DefensiveIconsAudience",
                 "PausesPerHalf", "ResumeCountdownSeconds"} | set(VERSUS_FLAGS)


def validate_versus_rules(path, payload):
    """FPSVersusRules (Data/versus_rules.json, Epic 107); mirrors UPSVersusSubsystem::ValidateRules."""
    offense_role, defense_role = payload.get("OffenseControlRole"), payload.get("DefenseControlRole")
    if offense_role not in OFFENSIVE_ROLES:
        err(path, f"OffenseControlRole: '{offense_role}' must be an offensive role ({sorted(OFFENSIVE_ROLES)})")
    if defense_role not in DEFENSIVE_ROLES:
        err(path, f"DefenseControlRole: '{defense_role}' must be a defensive role ({sorted(DEFENSIVE_ROLES)})")
    for flag in VERSUS_FLAGS:
        if not isinstance(payload.get(flag), bool):
            err(path, f"{flag}: must be true or false")
    if payload.get("Screen") not in VERSUS_SCREENS:
        err(path, f"Screen: '{payload.get('Screen')}' must be one of {sorted(VERSUS_SCREENS)}")
    for field in ("RouteArtAudience", "DefensiveIconsAudience"):
        if payload.get(field) not in VERSUS_AUDIENCES:
            err(path, f"{field}: '{payload.get(field)}' must be one of {sorted(VERSUS_AUDIENCES)}")
    pauses = payload.get("PausesPerHalf")
    if not isinstance(pauses, int) or isinstance(pauses, bool) or pauses < -1:
        err(path, f"PausesPerHalf: '{pauses}' must be a whole number, -1 (no limit) or 0 or more")
    countdown = payload.get("ResumeCountdownSeconds")
    if not is_number(countdown) or countdown < 0:
        err(path, f"ResumeCountdownSeconds: '{countdown}' must be a number, 0 or more")
    extra = set(payload) - VERSUS_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSVersusRules exactly")


CROSS_PLAY_POLICIES = {"Anyone", "SameInput", "SamePlatform"}
SESSION_MATCHMAKING_NUMBERS = ("InitialSkillWindow", "SkillWindowGrowthPerSecond", "MaxSkillWindow", "RegionRelaxSeconds",
                               "MaxWaitSeconds", "HostScoreDesktop", "HostScoreOnPower", "HostScoreUnmetered")
SESSION_MATCHMAKING_FIELDS = {"ProtocolVersion", "DefaultCrossPlay", "TouchDefaultCrossPlay"} | set(SESSION_MATCHMAKING_NUMBERS)


def validate_session_matchmaking(path, payload):
    """FPSSessionMatchmakingTuning (Data/session_matchmaking.json, Epic 108.5); mirrors
    UPSSessionService::ValidateTuning."""
    protocol = payload.get("ProtocolVersion")
    if not isinstance(protocol, int) or isinstance(protocol, bool) or protocol < 1:
        err(path, f"ProtocolVersion: '{protocol}' must be a whole number, 1 or more")
    for field in SESSION_MATCHMAKING_NUMBERS:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    initial, widest = payload.get("InitialSkillWindow"), payload.get("MaxSkillWindow")
    if is_number(initial) and is_number(widest) and widest < initial:
        err(path, f"MaxSkillWindow: {widest} must be at least InitialSkillWindow ({initial})")
    wait = payload.get("MaxWaitSeconds")
    if is_number(wait) and wait <= 0:
        err(path, "MaxWaitSeconds: must be above 0")
    for field in ("DefaultCrossPlay", "TouchDefaultCrossPlay"):
        if payload.get(field) not in CROSS_PLAY_POLICIES:
            err(path, f"{field}: '{payload.get(field)}' must be one of {sorted(CROSS_PLAY_POLICIES)} (EPSCrossPlayPolicy)")
    extra = set(payload) - SESSION_MATCHMAKING_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSSessionMatchmakingTuning exactly")


AI_DEBUG_FIELDS = {"bLogDecisions": bool, "bWritePostMortems": bool, "PostMortemDirectory": str, "MaxPostMortemFiles": int,
                   "MaxRecordsPerPlay": int, "OverlayHeightCm": (int, float), "OverlayFontScale": (int, float),
                   "OverlayFontSize": int, "OverlayCharWidth": (int, float), "OverlayLineHeight": (int, float),
                   "OverlayPadding": (int, float), "OverlayMaxLineChars": int, "OverlayOffenseColor": str,
                   "OverlayDefenseColor": str, "OverlayTextColor": str, "OverlayOpacity": (int, float),
                   "OverlayCrowdedOpacity": (int, float), "OverlayNudgeStep": (int, float), "OverlayMaxNudges": int,
                   "OverlayTargetLineWidth": (int, float)}
DEFENSIVE_ASSIGNMENTS = {"PassRush", "Contain", "ManCoverage", "ZoneCoverage", "RunFit", "Block"}
SCENARIO_FIELDS = {"ScenarioId", "Description", "OffenseCategory", "Down", "Distance", "StepSeconds", "Steps", "Players", "Expectations"}
SCENARIO_PLAYER_FIELDS = {"PlayerId", "Role", "Location", "Rating", "DNA", "bHasBall", "Route", "Assignment", "CoverTarget", "ZoneOffset"}
SCENARIO_EXPECTATION_FIELDS = {"PlayerId", "Action", "Target", "Heading", "MaxAngleDegrees"}


def validate_ai_debug(path, payload):
    """FPSAIDebugTuning (Data/ai_debug.json, Epic 85)."""
    for field, ftype in AI_DEBUG_FIELDS.items():
        value = payload.get(field)
        if field not in payload or (ftype is int and isinstance(value, bool)) or not isinstance(value, ftype):
            err(path, f"{field}: '{value}' has the wrong type")
    for field in ("MaxPostMortemFiles", "MaxRecordsPerPlay", "OverlayFontScale"):
        if is_number(payload.get(field)) and payload[field] <= 0:
            err(path, f"{field}: must be above 0")
    directory = payload.get("PostMortemDirectory")
    if isinstance(directory, str) and (not directory.strip() or ".." in directory or directory.startswith(("/", "\\"))):
        err(path, "PostMortemDirectory: a folder under Saved/, without '..'")
    # The overlay's cards (Epic 85.2); mirrors PSAIDebugOverlay::ValidateTuning.
    for field in ("OverlayFontSize", "OverlayCharWidth", "OverlayLineHeight", "OverlayTargetLineWidth"):
        if is_number(payload.get(field)) and payload[field] <= 0:
            err(path, f"{field}: must be above 0")
    for field in ("OverlayPadding", "OverlayNudgeStep", "OverlayMaxNudges"):
        if is_number(payload.get(field)) and payload[field] < 0:
            err(path, f"{field}: must be 0 or more")
    if isinstance(payload.get("OverlayMaxLineChars"), int) and payload["OverlayMaxLineChars"] < 8:
        err(path, "OverlayMaxLineChars: must be 8 or more")
    for field in ("OverlayOpacity", "OverlayCrowdedOpacity"):
        if is_number(payload.get(field)) and not 0 <= payload[field] <= 1:
            err(path, f"{field}: must be from 0 to 1")
    for field in ("OverlayOffenseColor", "OverlayDefenseColor", "OverlayTextColor"):
        if isinstance(payload.get(field), str) and not HEX_COLOR.match(payload[field]):
            err(path, f"{field}: '{payload[field]}' must be #RRGGBB")
    extra = set(payload) - set(AI_DEBUG_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSAIDebugTuning exactly")


def is_vector(value):
    return isinstance(value, dict) and set(value) <= {"X", "Y", "Z"} and all(is_number(v) for v in value.values())


def validate_ai_scenarios(path, payload, dna_catalog):
    """FPSAIScenarioCatalog (Data/ai_scenarios.json, Epic 85); mirrors
    UPSAIScenarioRunner::ValidateScenario."""
    scenarios = payload.get("Scenarios")
    if not isinstance(scenarios, list) or not scenarios:
        err(path, "'Scenarios' must be a non-empty array")
        return
    ids = set()
    for idx, scenario in enumerate(scenarios):
        where = f"Scenarios[{idx}]"
        if not isinstance(scenario, dict):
            err(path, f"{where}: must be an object")
            continue
        sid = scenario.get("ScenarioId")
        if not isinstance(sid, str) or not sid or sid in ids:
            err(path, f"{where}.ScenarioId: '{sid}' must be a unique, non-empty name")
        ids.add(sid)
        where = f"Scenarios[{idx}] '{sid}'"
        if set(scenario) - SCENARIO_FIELDS:
            err(path, f"{where}: unknown field(s) {sorted(set(scenario) - SCENARIO_FIELDS)}")
        if scenario.get("OffenseCategory", "ShortPass") not in content_contracts.OFFENSE_CATEGORIES:
            err(path, f"{where}.OffenseCategory: '{scenario.get('OffenseCategory')}' is not an offensive play category")
        step_seconds = scenario.get("StepSeconds", 0.1)
        if not is_number(step_seconds) or step_seconds <= 0:
            err(path, f"{where}.StepSeconds: must be above 0")
        steps = scenario.get("Steps", 1)
        if not isinstance(steps, int) or isinstance(steps, bool) or steps < 1:
            err(path, f"{where}.Steps: a whole number, 1 or more")
        players = scenario.get("Players") if isinstance(scenario.get("Players"), list) else []
        if not players:
            err(path, f"{where}.Players: must place at least one player")
        names = set()
        for pidx, player in enumerate(players):
            pwhere = f"{where}.Players[{pidx}]"
            if not isinstance(player, dict):
                err(path, f"{pwhere}: must be an object")
                continue
            if set(player) - SCENARIO_PLAYER_FIELDS:
                err(path, f"{pwhere}: unknown field(s) {sorted(set(player) - SCENARIO_PLAYER_FIELDS)}")
            pid = player.get("PlayerId")
            if not isinstance(pid, str) or not pid or pid in names:
                err(path, f"{pwhere}.PlayerId: '{pid}' must be a unique, non-empty name")
            names.add(pid)
            if player.get("Role") not in PLAYER_ROLES:
                err(path, f"{pwhere}.Role: '{player.get('Role')}' is not an EPlayerRole")
            if not is_vector(player.get("Location")):
                err(path, f"{pwhere}.Location: must be an X/Y/Z object")
            if "Rating" in player and (not is_number(player["Rating"]) or not 0 <= player["Rating"] <= 100):
                err(path, f"{pwhere}.Rating: ratings run 0-100")
            if "DNA" in player:
                validate_player_dna(path, pwhere, player, dna_catalog)
            if "Assignment" in player and player["Assignment"] not in DEFENSIVE_ASSIGNMENTS:
                err(path, f"{pwhere}.Assignment: '{player['Assignment']}' must be one of {sorted(DEFENSIVE_ASSIGNMENTS)}")
            if "Route" in player and (not isinstance(player["Route"], list) or not all(is_vector(v) for v in player["Route"])):
                err(path, f"{pwhere}.Route: must be a list of X/Y/Z offsets")
            if "ZoneOffset" in player and not is_vector(player["ZoneOffset"]):
                err(path, f"{pwhere}.ZoneOffset: must be an X/Y/Z object")
        for pidx, player in enumerate(players):
            if isinstance(player, dict) and "CoverTarget" in player and player["CoverTarget"] not in names:
                err(path, f"{where}.Players[{pidx}].CoverTarget: '{player['CoverTarget']}' isn't a player in the scenario")
        expectations = scenario.get("Expectations") if isinstance(scenario.get("Expectations"), list) else []
        if not expectations:
            err(path, f"{where}.Expectations: must expect something")
        for eidx, expectation in enumerate(expectations):
            ewhere = f"{where}.Expectations[{eidx}]"
            if not isinstance(expectation, dict):
                err(path, f"{ewhere}: must be an object")
                continue
            if set(expectation) - SCENARIO_EXPECTATION_FIELDS:
                err(path, f"{ewhere}: unknown field(s) {sorted(set(expectation) - SCENARIO_EXPECTATION_FIELDS)}")
            if expectation.get("PlayerId") not in names:
                err(path, f"{ewhere}.PlayerId: '{expectation.get('PlayerId')}' isn't a player in the scenario")
            if not isinstance(expectation.get("Action"), str) or not expectation.get("Action"):
                err(path, f"{ewhere}.Action: must name the decision expected")
            if "Heading" in expectation and not is_vector(expectation["Heading"]):
                err(path, f"{ewhere}.Heading: must be an X/Y/Z object")


ECONOMY_FIELDS = {
    "StadiumCapacity": int, "BaseTicketPrice": float, "MinTicketPrice": float, "MaxTicketPrice": float,
    "BaseFillRate": float, "WinFillWeight": float, "SatisfactionFillWeight": float, "PriceElasticity": float,
    "MinFillRate": float, "ConcessionsPerFan": float, "MediaRevenuePerTeam": int, "StartingSatisfaction": float,
    "WinSatisfactionGain": float, "LossSatisfactionLoss": float, "PriceSatisfactionLoss": float,
    "WinningSeasonSatisfactionGain": float, "LosingSeasonSatisfactionLoss": float,
    "RelocationSatisfactionThreshold": float, "RelocationLosingSeasons": int, "MaxBudgetFraction": float,
}
ECONOMY_FRACTIONS = {
    "BaseFillRate", "MinFillRate", "StartingSatisfaction", "WinSatisfactionGain", "LossSatisfactionLoss",
    "PriceSatisfactionLoss", "WinningSeasonSatisfactionGain", "LosingSeasonSatisfactionLoss",
    "RelocationSatisfactionThreshold", "MaxBudgetFraction",
}
BUDGET_FIELDS = ("ScoutingFraction", "TrainingFraction", "StaffFraction")


def validate_owner_economics(path, payload):
    """FPSEconomyTuning (Data/owner_economics.json, Epic 95); mirrors UPSOwnerEconomy::ValidateTuning."""
    for field, ftype in ECONOMY_FIELDS.items():
        value = payload.get(field)
        if ftype is int:
            if isinstance(value, bool) or not isinstance(value, int) or value < 0:
                err(path, f"{field}: '{value}' must be a whole number, 0 or more")
        elif not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
        elif field in ECONOMY_FRACTIONS and value > 1:
            err(path, f"{field}: a fraction, at most 1")
    extra = set(payload) - set(ECONOMY_FIELDS) - {"DefaultBudget"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSEconomyTuning exactly")

    def num(field):
        value = payload.get(field)
        return value if is_number(value) else None

    for field in ("StadiumCapacity", "MinTicketPrice", "RelocationLosingSeasons"):
        if num(field) == 0:
            err(path, f"{field}: must be above 0")
    for low, high in (("MinTicketPrice", "BaseTicketPrice"), ("BaseTicketPrice", "MaxTicketPrice"), ("MinFillRate", "BaseFillRate")):
        if num(low) is not None and num(high) is not None and num(low) > num(high):
            err(path, f"{low} must not exceed {high}")

    budget = payload.get("DefaultBudget")
    if not isinstance(budget, dict):
        err(path, "'DefaultBudget' must be an object of ScoutingFraction, TrainingFraction, StaffFraction")
        return
    total = 0
    for field in BUDGET_FIELDS:
        value = budget.get(field)
        if not is_number(value) or value < 0:
            err(path, f"DefaultBudget.{field}: '{value}' must be a number, 0 or more")
        else:
            total += value
    extra = set(budget) - set(BUDGET_FIELDS)
    if extra:
        err(path, f"DefaultBudget: unknown field(s) {sorted(extra)} - names must match FPSTeamBudget exactly")
    if num("MaxBudgetFraction") is not None and total > num("MaxBudgetFraction") + 1e-6:
        err(path, f"DefaultBudget: its shares total {total:g}, over MaxBudgetFraction")


GAP_OVERLAY_COLORS = ("FilledColor", "BlockedColor", "OpenColor", "UnownedColor")
GAP_OVERLAY_FLAGS = ("bEnabledByDefault", "bEmphasizeOpenOwners", "bDrawDebug")


def validate_gap_overlay(path, payload):
    """FPSGapOverlayStyle (Data/gap_overlay.json, Epic 81); mirrors
    UPSDefenderGapOverlaySubsystem::ValidateStyle."""
    refresh = payload.get("RefreshSeconds")
    if not is_number(refresh) or refresh <= 0:
        err(path, f"RefreshSeconds: '{refresh}' must be a number above 0")
    height, radius = payload.get("MarkerHeight"), payload.get("MarkerRadius")
    if not is_number(height) or height < 0:
        err(path, f"MarkerHeight: '{height}' must be a number, 0 or more")
    if not is_number(radius) or radius <= 0:
        err(path, f"MarkerRadius: '{radius}' must be a number above 0")
    for field in GAP_OVERLAY_COLORS:
        value = payload.get(field)
        if not isinstance(value, str) or not HEX_COLOR.match(value):
            err(path, f"{field}: '{value}' must be #RRGGBB")
    for field in GAP_OVERLAY_FLAGS:
        if not isinstance(payload.get(field), bool):
            err(path, f"{field}: must be true or false")
    if payload.get("OpenOwnerEmphasis") not in EMPHASIS_KINDS:
        err(path, f"OpenOwnerEmphasis: '{payload.get('OpenOwnerEmphasis')}' must be one of {list(EMPHASIS_KINDS)}")
    extra = set(payload) - set(GAP_OVERLAY_COLORS) - set(GAP_OVERLAY_FLAGS) \
        - {"RefreshSeconds", "MarkerHeight", "MarkerRadius", "OpenOwnerEmphasis"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSGapOverlayStyle exactly")


MORALE_FLOAT_FIELDS = {
    "StarterBonus", "BackupPenalty", "BetterThanStarterPenalty", "TeamSuccessWeight", "UnderpaidRatio",
    "UnderpaidPenalty", "WellPaidBonus", "ContractYearPenalty", "LeaderBoost", "MoraleInertia",
    "PerformanceSwing", "TradeRequestMorale", "StarRating", "HoldoutPayRatio", "HoldoutMorale",
    "LeaderAwareness", "LeaderWinPercentage", "LeaderMorale",
}
MORALE_INT_FIELDS = {"MaxLeaders", "TradeRequestWeeks"}
MORALE_FRACTIONS = {
    "UnderpaidRatio", "TradeRequestMorale", "HoldoutPayRatio", "HoldoutMorale", "LeaderWinPercentage",
    "LeaderMorale",
}
MORALE_UNIT_FIELDS = {"Unit", "Role", "FullCohesionGames", "MaxBonus"}


def validate_morale(path, payload):
    """FPSMoraleTuning (Data/morale.json, Epic 91); mirrors UPSLockerRoom::ValidateTuning."""
    for field in sorted(MORALE_FLOAT_FIELDS):
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
        elif field in MORALE_FRACTIONS and value > 1:
            err(path, f"{field}: a fraction, at most 1")
    for field in sorted(MORALE_INT_FIELDS):
        value = payload.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            err(path, f"{field}: '{value}' must be a whole number, 0 or more")
    if payload.get("TradeRequestWeeks") == 0:
        err(path, "TradeRequestWeeks: must be at least 1")
    for field in ("MoraleInertia", "PerformanceSwing"):
        if is_number(payload.get(field)) and payload[field] >= 1:
            err(path, f"{field}: must be below 1")
    for field in ("StarRating", "LeaderAwareness"):
        if is_number(payload.get(field)) and payload[field] > 100:
            err(path, f"{field}: ratings run 0-100")
    extra = set(payload) - MORALE_FLOAT_FIELDS - MORALE_INT_FIELDS - {"Units"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSMoraleTuning exactly")

    units = payload.get("Units")
    if not isinstance(units, list):
        err(path, "'Units' must be an array")
        return
    seen = set()
    for idx, unit in enumerate(units):
        where = f"Units[{idx}]"
        if not isinstance(unit, dict):
            err(path, f"{where}: not an object")
            continue
        name = unit.get("Unit")
        if not isinstance(name, str) or not name or name in seen:
            err(path, f"{where}.Unit: empty or duplicate '{name}'")
        seen.add(name)
        if unit.get("Role") not in PLAYER_ROLES:
            err(path, f"{where}.Role: '{unit.get('Role')}' is not a valid EPlayerRole")
        games = unit.get("FullCohesionGames")
        if isinstance(games, bool) or not isinstance(games, int) or games < 1:
            err(path, f"{where}.FullCohesionGames: '{games}' must be a whole number, 1 or more")
        bonus = unit.get("MaxBonus")
        if not is_number(bonus) or not 0 <= bonus < 1:
            err(path, f"{where}.MaxBonus: '{bonus}' must be a number in [0, 1)")
        extra = set(unit) - MORALE_UNIT_FIELDS
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPSChemistryUnit exactly")


LEAGUE_GENERATOR_FIELDS = {
    "LeagueName": str, "NumTeams": int, "NumWeeks": int, "ByeWeekNumbers": list, "NumPlayoffTeams": int,
    "Divisions": list, "PlaceholderTeamName": str, "PlaceholderColors": list, "TeamTalentSpread": (int, float),
    "TalentPerExperienceYear": (int, float), "MaxExperienceTalent": (int, float), "EntryAgeMin": int,
    "EntryAgeMax": int, "RoleProfiles": list, "NameCultures": list, "NameBlocklist": list, "MaxNameAttempts": int,
    "DraftClass": dict,
}
ROLE_PROFILE_FIELDS = {"Role": str, "RosterCount": int, "IdCode": str, "Attrition": (int, float), "MaxAge": int,
                       "Attributes": list}
ATTRIBUTE_CURVE_FIELDS = {"Attribute": str, "Mean": (int, float), "StdDev": (int, float), "Min": (int, float),
                          "Max": (int, float), "TalentWeight": (int, float)}
NAME_CULTURE_FIELDS = {"Culture": str, "Weight": (int, float), "FirstNames": list, "LastNames": list}
DRAFT_CLASS_FIELDS = {"ProspectsPerTeam": int, "TalentShift": (int, float), "TalentSpread": (int, float)}
PROGRESSION_FIELDS = {"PeakAgeStart": int, "PeakAgeEnd": int, "GrowthPerYear": (int, float),
                      "DeclinePerYear": (int, float), "LowSnapShareThreshold": (int, float)}


def check_typed(path, where, row, fields, struct):
    """Every field present with its type, and no unknown names: a generator's tuning has no
    defaults to fall back on, so a missing field is a mistake. False when row is not an object."""
    if not isinstance(row, dict):
        err(path, f"{where}: not an object")
        return False
    for field, ftype in fields.items():
        if field not in row:
            err(path, f"{where}: missing field '{field}'")
        elif isinstance(row[field], bool) or not isinstance(row[field], ftype):
            err(path, f"{where}.{field}: expected {getattr(ftype, '__name__', 'number')}, got {type(row[field]).__name__}")
    extra = set(row) - set(fields)
    if extra:
        err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match {struct} exactly")
    return True


def validate_league_generator(path, payload):
    """FPSLeagueGeneratorTuning (Data/league_generator.json, Epic 122); mirrors
    PSLeagueGenerator::ValidateTuning, and also rejects a name listed twice in one pool."""
    if not check_typed(path, "generator tuning", payload, LEAGUE_GENERATOR_FIELDS, "FPSLeagueGeneratorTuning"):
        return

    def num(field):
        value = payload.get(field)
        return value if is_number(value) else None

    teams, weeks, playoff = num("NumTeams"), num("NumWeeks"), num("NumPlayoffTeams")
    if teams is not None and teams < 2:
        err(path, "NumTeams: a league needs 2 or more")
    if weeks is not None and weeks < 1:
        err(path, "NumWeeks: 1 or more")
    byes = payload.get("ByeWeekNumbers") if isinstance(payload.get("ByeWeekNumbers"), list) else []
    for week in byes:
        whole = isinstance(week, int) and not isinstance(week, bool)
        if not whole or (weeks is not None and not 1 <= week <= weeks) or byes.count(week) > 1:
            err(path, f"ByeWeekNumbers: week {week!r} is outside the season or listed twice")
    if playoff is not None and teams is not None and not 2 <= playoff <= teams:
        err(path, f"NumPlayoffTeams: {playoff} must be from 2 to NumTeams ({teams})")
    divisions = payload.get("Divisions") if isinstance(payload.get("Divisions"), list) else []
    if not divisions:
        err(path, "Divisions: a league needs at least one")
    for division in divisions:
        if not isinstance(division, str) or not division.strip() or divisions.count(division) > 1:
            err(path, f"Divisions: {division!r} is empty or listed twice")
    if isinstance(payload.get("PlaceholderTeamName"), str) and not payload["PlaceholderTeamName"].strip():
        err(path, "PlaceholderTeamName: empty")
    for color in payload.get("PlaceholderColors") or []:
        if not (isinstance(color, str) and HEX_COLOR.match(color)):
            err(path, f"PlaceholderColors: {color!r} is not #RRGGBB")
    for field in ("TeamTalentSpread", "TalentPerExperienceYear", "MaxExperienceTalent"):
        if num(field) is not None and num(field) < 0:
            err(path, f"{field}: 0 or more")
    entry_min, entry_max = num("EntryAgeMin"), num("EntryAgeMax")
    if entry_min is not None and entry_max is not None and (entry_min < 18 or entry_max < entry_min):
        err(path, "EntryAgeMin, EntryAgeMax: 18 or more, the minimum first")
    if num("MaxNameAttempts") is not None and num("MaxNameAttempts") < 1:
        err(path, "MaxNameAttempts: 1 or more")

    attributes = set(content_contracts.RATING_FIELDS) | set(content_contracts.BODY_FIELDS)
    roles_seen = set()
    for idx, profile in enumerate(payload.get("RoleProfiles") or []):
        where = f"RoleProfiles[{idx}]"
        if not check_typed(path, where, profile, ROLE_PROFILE_FIELDS, "FPSRoleProfile"):
            continue
        role = profile.get("Role")
        where = f"RoleProfiles[{idx}] '{role}'"
        if role not in PLAYER_ROLES:
            err(path, f"{where}.Role: not an EPlayerRole")
        elif role in roles_seen:
            err(path, f"{where}: listed twice")
        roles_seen.add(role)
        if isinstance(profile.get("RosterCount"), int) and profile["RosterCount"] < 1:
            err(path, f"{where}.RosterCount: 1 or more")
        code = profile.get("IdCode")
        if isinstance(code, str) and not (code and code.isascii() and code.isalnum()):
            err(path, f"{where}.IdCode: '{code}' must be letters and digits")
        if is_number(profile.get("Attrition")) and not 0 < profile["Attrition"] < 1:
            err(path, f"{where}.Attrition: above 0, below 1")
        if isinstance(profile.get("MaxAge"), int) and entry_max is not None and not entry_max <= profile["MaxAge"] <= 50:
            err(path, f"{where}.MaxAge: from EntryAgeMax to 50")
        curves = set()
        for cidx, curve in enumerate(profile.get("Attributes") or []):
            cwhere = f"{where}.Attributes[{cidx}]"
            if not check_typed(path, cwhere, curve, ATTRIBUTE_CURVE_FIELDS, "FPSAttributeCurve"):
                continue
            name = curve.get("Attribute")
            cwhere = f"{where}.Attributes '{name}'"
            if name not in attributes:
                err(path, f"{cwhere}: not a float field of FPlayerAttributes ({sorted(attributes)})")
            elif name in curves:
                err(path, f"{cwhere}: listed twice")
            curves.add(name)
            low, mean, high, spread, weight = (curve.get(f) for f in ("Min", "Mean", "Max", "StdDev", "TalentWeight"))
            if all(is_number(v) for v in (low, mean, high, spread)) and (spread < 0 or not low <= mean <= high):
                err(path, f"{cwhere}: StdDev 0 or more, and Min <= Mean <= Max")
            if is_number(low) and is_number(high):
                if name in content_contracts.BODY_FIELDS and low <= 0:
                    err(path, f"{cwhere}: a body measure is above 0")
                if name in content_contracts.RATING_FIELDS and (low < 0 or high > 100):
                    err(path, f"{cwhere}: a rating runs 0-100")
            if is_number(weight) and not 0 <= weight <= 1:
                err(path, f"{cwhere}.TalentWeight: 0 to 1")
        for name in sorted(attributes - curves):
            err(path, f"{where}: no curve for {name}")
    for role in sorted(PLAYER_ROLES - roles_seen):
        err(path, f"RoleProfiles: no profile for {role}, so rosters would have none")

    cultures = payload.get("NameCultures") or []
    if not cultures:
        err(path, "NameCultures: at least one")
    for idx, culture in enumerate(cultures):
        where = f"NameCultures[{idx}]"
        if not check_typed(path, where, culture, NAME_CULTURE_FIELDS, "FPSNameCulture"):
            continue
        where = f"NameCultures[{idx}] '{culture.get('Culture')}'"
        if is_number(culture.get("Weight")) and culture["Weight"] <= 0:
            err(path, f"{where}.Weight: above 0")
        for field in ("FirstNames", "LastNames"):
            names = culture.get(field) if isinstance(culture.get(field), list) else []
            if not names:
                err(path, f"{where}.{field}: empty")
            for name in names:
                if not isinstance(name, str) or not content_contracts.normalize_name(name):
                    err(path, f"{where}.{field}: {name!r} has no letters")
                elif names.count(name) > 1:
                    err(path, f"{where}.{field}: '{name}' is listed twice")
    for entry in payload.get("NameBlocklist") or []:
        if not isinstance(entry, str) or not content_contracts.normalize_name(entry):
            err(path, f"NameBlocklist: {entry!r} has no letters")
    draft = payload.get("DraftClass")
    if check_typed(path, "DraftClass", draft, DRAFT_CLASS_FIELDS, "FPSDraftClassTuning"):
        if isinstance(draft.get("ProspectsPerTeam"), int) and draft["ProspectsPerTeam"] < 1:
            err(path, "DraftClass.ProspectsPerTeam: 1 or more")
        if is_number(draft.get("TalentSpread")) and draft["TalentSpread"] <= 0:
            err(path, "DraftClass.TalentSpread: above 0")


def validate_progression(path, payload):
    """FPSProgressionTuning (Data/player_progression.json): the age curve players grow and decline
    along (UPSPlayerProgression), which the league generator walks generated players along."""
    if not check_typed(path, "progression tuning", payload, PROGRESSION_FIELDS, "FPSProgressionTuning"):
        return
    start, end = payload.get("PeakAgeStart"), payload.get("PeakAgeEnd")
    if isinstance(start, int) and isinstance(end, int) and not 18 <= start <= end <= 50:
        err(path, "PeakAgeStart, PeakAgeEnd: 18 to 50, the start first")
    for field in ("GrowthPerYear", "DeclinePerYear"):
        if is_number(payload.get(field)) and payload[field] < 0:
            err(path, f"{field}: 0 or more")
    threshold = payload.get("LowSnapShareThreshold")
    if is_number(threshold) and not 0 <= threshold <= 1:
        err(path, "LowSnapShareThreshold: 0 to 1")


PLAYBOOK_GENERATOR_FIELDS = {
    "OffenseFormations": list, "PlayActionDrop": (int, float), "OffensePlaybookSize": int, "DefensePlaybookSize": int,
    "CategoryEmphasis": (int, float), "Concepts": list, "DefensiveFronts": list, "Coverages": list, "Pressures": list,
    "SchemeFlavors": list,
}
CONCEPT_FIELDS = {"ConceptId": str, "Label": str, "Family": str, "Category": str, "Formations": list, "QBDrop": (int, float),
                  "BackSpot": dict, "Slots": list, "BacksideRoute": str, "LineKind": str, "Deceptions": list}
CONCEPT_SLOT_FIELDS = {"Roles": list, "Routes": list}
FRONT_FIELDS = {"Formation": str, "Front": str, "LineKind": str}
COVERAGE_FIELDS = {"Shell": str, "Label": str, "Category": str, "MaxBlitzers": int, "Slots": list}
COVERAGE_SLOT_FIELDS = {"Role": str, "Kind": str, "Zone": dict}
PRESSURE_FIELDS = {"PressureId": str, "Label": str, "Blitzers": dict}
FLAVOR_FIELDS = {"SchemeId": str, "ConceptWeights": dict, "ShellWeights": dict, "PressureWeights": dict}
RECEIVER_ROLES = {"RunningBack", "WideReceiver", "TightEnd"}


def load_data(name, key):
    """Data/<name>'s <key> (None when the file is missing or broken; its own checks report that)."""
    try:
        payload = json.loads((DATA_DIR / name).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    return payload.get(key) if isinstance(payload, dict) else None


def formation_roles(packages, formation, offense):
    """{role: count} of the personnel package that lists formation on its side, or None."""
    for package in packages or []:
        if isinstance(package, dict) and package.get("bOffense") == offense and formation in (package.get("Formations") or []):
            counts = package.get("RoleCounts") or {}
            return {role: n for role, n in counts.items() if isinstance(n, int) and n > 0}
    return None


def concept_fits(concept, roles):
    """Whether every slot of concept finds a receiver in roles (PSPlaybookGenerator::BuildConceptPlays)."""
    if not roles or roles.get("Quarterback", 0) < 1:
        return False
    open_roles = dict(roles)
    if concept.get("Category") == "Run":
        if open_roles.get("RunningBack", 0) < 1:
            return False
        open_roles["RunningBack"] -= 1
    for slot in concept.get("Slots") or []:
        taken = next((r for r in (slot.get("Roles") or []) if r in RECEIVER_ROLES and open_roles.get(r, 0) > 0), None)
        if taken is None or not slot.get("Routes"):
            return False
        open_roles[taken] -= 1
    return True


def validate_playbook_generator(path, payload):
    """FPSPlaybookGeneratorTuning (Data/playbook_generator.json, Epic 121); mirrors
    PSPlaybookGenerator::ValidateTuning, plus what only other files know: each coverage's shell in
    coverage_matchups.json, each front in run_fits.json, each flavor's scheme in
    coaching_staffs.json with weights for its side."""
    if not check_typed(path, "playbook generator", payload, PLAYBOOK_GENERATOR_FIELDS, "FPSPlaybookGeneratorTuning"):
        return
    packages = load_data("personnel_packages.json", "Packages")
    route_ids = load_route_ids()
    shells_known = {s.get("Shell") for s in (load_data("coverage_matchups.json", "Shells") or []) if isinstance(s, dict)}
    fronts_known = {f.get("Front") for f in (load_data("run_fits.json", "Fronts") or []) if isinstance(f, dict)}
    schemes = {s.get("SchemeId"): s for s in (load_data("coaching_staffs.json", "Schemes") or []) if isinstance(s, dict)}

    def check_route(where, route):
        if route_ids is not None and route not in route_ids:
            err(path, f"{where}: route '{route}' is not in sample_routes.json")

    def is_id(value):
        return isinstance(value, str) and value.isascii() and value.isalnum()

    formations = payload.get("OffenseFormations") or []
    if not formations:
        err(path, "OffenseFormations: at least one")
    for formation in formations:
        if packages is not None and formation_roles(packages, formation, True) is None:
            err(path, f"OffenseFormations: '{formation}' is in no offensive personnel package")
    for field in ("OffensePlaybookSize", "DefensePlaybookSize"):
        if isinstance(payload.get(field), int) and payload[field] < 1:
            err(path, f"{field}: 1 or more")
    if is_number(payload.get("CategoryEmphasis")) and payload["CategoryEmphasis"] < 0:
        err(path, "CategoryEmphasis: 0 or more")
    if is_number(payload.get("PlayActionDrop")) and payload["PlayActionDrop"] >= 0:
        err(path, "PlayActionDrop: behind the line (below 0)")

    concept_ids = set()
    for idx, concept in enumerate(payload.get("Concepts") or []):
        where = f"Concepts[{idx}]"
        if not check_typed(path, where, concept, CONCEPT_FIELDS, "FPSPlayConcept"):
            continue
        cid = concept.get("ConceptId")
        where = f"Concepts[{idx}] '{cid}'"
        if not is_id(cid) or cid in concept_ids:
            err(path, f"{where}.ConceptId: empty, not letters and digits, or used twice")
        concept_ids.add(cid)
        if not str(concept.get("Label", "")).strip():
            err(path, f"{where}.Label: empty")
        category = concept.get("Category")
        if category not in ("Run", "ShortPass", "DeepPass", "Screen"):
            err(path, f"{where}.Category: '{category}' is not Run, ShortPass, DeepPass or Screen")
        if is_number(concept.get("QBDrop")) and concept["QBDrop"] >= 0:
            err(path, f"{where}.QBDrop: behind the line (below 0)")
        content_contracts.check_vector(path, f"{where}.BackSpot", concept.get("BackSpot"), err)
        if concept.get("LineKind") not in ("PassBlock", "RunBlock"):
            err(path, f"{where}.LineKind: PassBlock or RunBlock")
        for formation in concept.get("Formations") or []:
            if formation not in formations:
                err(path, f"{where}.Formations: '{formation}' is not in OffenseFormations")
        variants = 1
        for sidx, slot in enumerate(concept.get("Slots") or []):
            swhere = f"{where}.Slots[{sidx}]"
            if not check_typed(path, swhere, slot, CONCEPT_SLOT_FIELDS, "FPSConceptSlot"):
                continue
            if not slot.get("Roles") or not slot.get("Routes"):
                err(path, f"{swhere}: needs Roles and Routes")
            for role in slot.get("Roles") or []:
                if role not in RECEIVER_ROLES:
                    err(path, f"{swhere}: {role} is not a receiver")
            for route in slot.get("Routes") or []:
                check_route(swhere, route)
            variants *= max(1, len(slot.get("Routes") or []))
        if variants > 64:
            err(path, f"{where}: {variants} route variants; at most 64")
        if concept.get("BacksideRoute"):
            check_route(f"{where}.BacksideRoute", concept["BacksideRoute"])
        for deception in concept.get("Deceptions") or []:
            fits = (deception == "None" or (deception == "PlayAction" and category in ("ShortPass", "DeepPass"))
                    or (deception in ("ZoneRead", "RPO") and category == "Run"))
            if not fits:
                err(path, f"{where}.Deceptions: {deception} doesn't go with a {category} concept (play-action on a pass, ZoneRead or RPO on a run)")
            if deception == "RPO" and not concept.get("BacksideRoute"):
                err(path, f"{where}: an RPO needs a BacksideRoute for its pass option")
        if packages is not None:
            usable = [f for f in formations if not concept.get("Formations") or f in concept["Formations"]]
            if not any(concept_fits(concept, formation_roles(packages, f, True)) for f in usable):
                err(path, f"{where}: its slots fit none of its formations")

    for idx, front in enumerate(payload.get("DefensiveFronts") or []):
        where = f"DefensiveFronts[{idx}]"
        if not check_typed(path, where, front, FRONT_FIELDS, "FPSDefensiveFrontDef"):
            continue
        where = f"DefensiveFronts[{idx}] '{front.get('Formation')}'"
        if packages is not None and formation_roles(packages, front.get("Formation"), False) is None:
            err(path, f"{where}: in no defensive personnel package")
        if fronts_known and front.get("Front") not in fronts_known:
            err(path, f"{where}.Front: '{front.get('Front')}' has no run fits in run_fits.json ({sorted(fronts_known)})")
        if front.get("LineKind") not in ("PassRush", "RunFit"):
            err(path, f"{where}.LineKind: PassRush or RunFit")

    shells = set()
    for idx, coverage in enumerate(payload.get("Coverages") or []):
        where = f"Coverages[{idx}]"
        if not check_typed(path, where, coverage, COVERAGE_FIELDS, "FPSCoverageTemplate"):
            continue
        shell = coverage.get("Shell")
        where = f"Coverages[{idx}] '{shell}'"
        if not is_id(shell) or shell in shells:
            err(path, f"{where}.Shell: empty, not letters and digits, or used twice")
        shells.add(shell)
        if shells_known and shell not in shells_known:
            err(path, f"{where}.Shell: has no rules in coverage_matchups.json ({sorted(shells_known)})")
        if coverage.get("Category") not in ("Base", "Prevent"):
            err(path, f"{where}.Category: Base or Prevent")
        if not str(coverage.get("Label", "")).strip() or (isinstance(coverage.get("MaxBlitzers"), int) and coverage["MaxBlitzers"] < 0):
            err(path, f"{where}: needs a Label, and MaxBlitzers 0 or more")
        jobs = [j for j in (coverage.get("Slots") or []) if isinstance(j, dict)]
        for role in ("Linebacker", "DefensiveBack"):
            if not any(j.get("Role") == role for j in jobs):
                err(path, f"{where}: no job for a {role}")
        for jidx, job in enumerate(coverage.get("Slots") or []):
            jwhere = f"{where}.Slots[{jidx}]"
            if not check_typed(path, jwhere, job, COVERAGE_SLOT_FIELDS, "FPSCoverageSlotDef"):
                continue
            if job.get("Role") not in content_contracts.DEFENSE_ROLES or job.get("Kind") not in ("ZoneCoverage", "ManCoverage"):
                err(path, f"{jwhere}: a defender's ZoneCoverage or ManCoverage")
            content_contracts.check_vector(path, f"{jwhere}.Zone", job.get("Zone"), err)

    pressure_ids = set()
    any_base = False
    for idx, pressure in enumerate(payload.get("Pressures") or []):
        where = f"Pressures[{idx}]"
        if not check_typed(path, where, pressure, PRESSURE_FIELDS, "FPSPressureDef"):
            continue
        pid = pressure.get("PressureId")
        where = f"Pressures[{idx}] '{pid}'"
        if not is_id(pid) or pid in pressure_ids:
            err(path, f"{where}.PressureId: empty, not letters and digits, or used twice")
        pressure_ids.add(pid)
        blitzers = pressure.get("Blitzers") or {}
        for role, count in blitzers.items():
            if role not in content_contracts.DEFENSE_ROLES or not isinstance(count, int) or isinstance(count, bool) or count < 0:
                err(path, f"{where}.Blitzers: '{role}' is not a defensive role, or its count isn't a whole number from 0")
        any_base |= not any(isinstance(n, int) and n > 0 for n in blitzers.values())
    if not any_base:
        err(path, "Pressures: one must send nobody, so coverages have a base call")

    flavors = set()
    for idx, flavor in enumerate(payload.get("SchemeFlavors") or []):
        where = f"SchemeFlavors[{idx}]"
        if not check_typed(path, where, flavor, FLAVOR_FIELDS, "FPSSchemeFlavor"):
            continue
        sid = flavor.get("SchemeId")
        where = f"SchemeFlavors[{idx}] '{sid}'"
        if not sid or sid in flavors:
            err(path, f"{where}.SchemeId: empty or used twice")
        flavors.add(sid)
        scheme = schemes.get(sid)
        if schemes and scheme is None:
            err(path, f"{where}.SchemeId: no such scheme in coaching_staffs.json ({sorted(schemes)})")
        for field, known in (("ConceptWeights", concept_ids), ("ShellWeights", shells), ("PressureWeights", pressure_ids)):
            weights = flavor.get(field) or {}
            for key, weight in weights.items():
                if key not in known or not is_number(weight) or weight < 0:
                    err(path, f"{where}.{field}: '{key}' is not one of the tuning's, or its weight is below 0")
            if scheme is not None and weights and (field == "ConceptWeights") != bool(scheme.get("bOffense")):
                err(path, f"{where}.{field}: {sid} is a{'n offensive' if scheme.get('bOffense') else ' defensive'} scheme; it weighs the other side")


def load_name_forms():
    """The blocked forms of Data/league_generator.json's NameBlocklist (empty when the file is
    missing or broken; its own checks report that)."""
    try:
        tuning = json.loads((DATA_DIR / "league_generator.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return set()
    blocklist = tuning.get("NameBlocklist") if isinstance(tuning, dict) else None
    return content_contracts.blocked_name_forms(blocklist if isinstance(blocklist, list) else [])


PERF_HARNESS_POSITIVE = ("FrameSeconds", "HistogramBucketMs", "HardFailMultiplier")
PERF_HARNESS_NON_NEGATIVE = ("RegressionTolerance", "MinRegressionMs")
PERF_HARNESS_COUNTS = {"WarmupFrames": 0, "PassFrames": 1, "PursuitFrames": 1, "PreSnapFrames": 0,
                       "HistogramBucketCount": 1, "MaxBusEventsPerPlay": 1, "TrendWindow": 1}


def validate_perf_harness(path, payload):
    """FPSPerfHarnessTuning (Data/perf_harness.json, Epic 114); mirrors
    UPSPerfHarness::ValidateTuning."""
    for field in PERF_HARNESS_POSITIVE:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    for field in PERF_HARNESS_NON_NEGATIVE:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field, floor in PERF_HARNESS_COUNTS.items():
        value = payload.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < floor:
            err(path, f"{field}: '{value}' must be a whole number, {floor} or more")
    if is_number(payload.get("HardFailMultiplier")) and payload["HardFailMultiplier"] < 1:
        err(path, "HardFailMultiplier: must be 1 or more (it multiplies the budget)")
    extra = set(payload) - set(PERF_HARNESS_POSITIVE) - set(PERF_HARNESS_NON_NEGATIVE) - set(PERF_HARNESS_COUNTS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSPerfHarnessTuning exactly")


TRAINING_NUMBER_FIELDS = {
    "DevelopIntensity", "GameplanIntensity", "DevelopPointsPerWeek", "DevelopHeadroom", "MinCoachDevelopment",
    "MaxCoachDevelopment", "FundingFloor", "MaxFundingMultiplier", "GameplanBonusPerShare", "MaxGameplanBonus",
    "MaxRelevance", "UnscoutedRelevance", "GameFatigue", "PracticeFatigue", "WeeklyRecovery", "RestRecovery",
    "StaminaFatigueRelief", "FatiguePerformanceSwing", "AIRestFreshness", "AIRestShift", "AILateSeasonProgress",
    "AILateGameplanShift",
}
TRAINING_INT_FIELDS = {"MaxFocusAreas", "RandomSeed", "AIFocusAreas"}
TRAINING_FRACTIONS = {
    "FundingFloor", "GameFatigue", "PracticeFatigue", "WeeklyRecovery", "RestRecovery", "StaminaFatigueRelief",
    "AIRestFreshness", "AIRestShift", "AILateSeasonProgress", "AILateGameplanShift",
}
TRAINING_OBJECT_FIELDS = {"DefaultAllocation", "DevelopRatings", "FocusAreas", "PracticeInjury"}
TRAINING_ALLOCATION_FIELDS = ("Develop", "Gameplan", "Rest")
RATING_WEIGHT_FIELDS = ("Speed", "Agility", "Strength", "Acceleration", "Awareness")
TRAINING_FOCUS_FIELDS = {"FocusId", "Label", "bVersusOffense", "Categories", "Roles", "Ratings"}
INJURY_TUNING_FIELDS = {"BaseInjuryChance", "MaxFatigueMultiplier", "MinRecoveryWeeks", "MaxRecoveryWeeks"}


def load_opponent_model_tracked():
    """The play categories Data/opponent_model.json tracks, by the human's side (True = offense),
    or None when it is missing or broken (its own checks report that)."""
    try:
        tuning = json.loads((DATA_DIR / "opponent_model.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    if not isinstance(tuning, dict) or not isinstance(tuning.get("Counters"), list):
        return None
    tracked = {True: set(), False: set()}
    for counter in tuning["Counters"]:
        if isinstance(counter, dict) and isinstance(counter.get("bOffense"), bool):
            tracked[counter["bOffense"]].add(counter.get("Observed"))
    return tracked


def validate_rating_weights(path, where, weights, need_some):
    """An FPSRatingWeights object: each skill rating's weight, 0 or more."""
    if not isinstance(weights, dict):
        err(path, f"{where}: must be an object of {', '.join(RATING_WEIGHT_FIELDS)}")
        return
    for field in RATING_WEIGHT_FIELDS:
        if field in weights and (not is_number(weights[field]) or weights[field] < 0):
            err(path, f"{where}.{field}: '{weights[field]}' must be a number, 0 or more")
    extra = set(weights) - set(RATING_WEIGHT_FIELDS)
    if extra:
        err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPSRatingWeights exactly")
    if need_some and not any(is_number(weights.get(field)) and weights[field] > 0 for field in RATING_WEIGHT_FIELDS):
        err(path, f"{where}: needs a rating weighted above 0")


def validate_training(path, payload, tracked):
    """FPSTrainingTuning (Data/training.json, Epic 90); mirrors UPSWeeklyPreparation::ValidateTuning,
    each focus area's categories ones Data/opponent_model.json tracks on its side."""
    for field in sorted(TRAINING_NUMBER_FIELDS):
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
        elif field in TRAINING_FRACTIONS and value > 1:
            err(path, f"{field}: a fraction, at most 1")
    for field in sorted(TRAINING_INT_FIELDS):
        value = payload.get(field)
        if isinstance(value, bool) or not isinstance(value, int):
            err(path, f"{field}: '{value}' must be a whole number")
        elif field != "RandomSeed" and value < 0:
            err(path, f"{field}: '{value}' must be 0 or more")
    extra = set(payload) - TRAINING_NUMBER_FIELDS - TRAINING_INT_FIELDS - TRAINING_OBJECT_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSTrainingTuning exactly")

    def num(field):
        value = payload.get(field)
        return value if is_number(value) else None

    if num("DevelopHeadroom") is not None and not 0 < num("DevelopHeadroom") <= 100:
        err(path, "DevelopHeadroom: must be above 0 and at most 100")
    if None not in (num("MinCoachDevelopment"), num("MaxCoachDevelopment")) and num("MaxCoachDevelopment") < num("MinCoachDevelopment"):
        err(path, "MaxCoachDevelopment must not be below MinCoachDevelopment")
    if num("MaxFundingMultiplier") is not None and num("MaxFundingMultiplier") < 1:
        err(path, "MaxFundingMultiplier: must be at least 1")
    for field in ("MaxGameplanBonus", "FatiguePerformanceSwing"):
        if num(field) is not None and num(field) >= 1:
            err(path, f"{field}: must be below 1")
    if None not in (num("UnscoutedRelevance"), num("MaxRelevance")) and num("UnscoutedRelevance") > num("MaxRelevance"):
        err(path, "UnscoutedRelevance must not exceed MaxRelevance")
    focus_max, ai_focus = payload.get("MaxFocusAreas"), payload.get("AIFocusAreas")
    if isinstance(focus_max, int) and focus_max < 1:
        err(path, "MaxFocusAreas: must be at least 1")
    if isinstance(focus_max, int) and isinstance(ai_focus, int) and ai_focus > focus_max:
        err(path, "AIFocusAreas must not exceed MaxFocusAreas")

    allocation = payload.get("DefaultAllocation")
    if not isinstance(allocation, dict):
        err(path, "'DefaultAllocation' must be an object of Develop, Gameplan, Rest")
    else:
        shares = [allocation.get(field) for field in TRAINING_ALLOCATION_FIELDS]
        if any(not is_number(share) or share < 0 for share in shares) or sum(s for s in shares if is_number(s)) <= 0:
            err(path, "DefaultAllocation: Develop, Gameplan and Rest must be numbers, 0 or more, and not all 0")
        if set(allocation) - set(TRAINING_ALLOCATION_FIELDS):
            err(path, f"DefaultAllocation: unknown field(s) {sorted(set(allocation) - set(TRAINING_ALLOCATION_FIELDS))}")
    validate_rating_weights(path, "DevelopRatings", payload.get("DevelopRatings"), False)

    injury = payload.get("PracticeInjury")
    if not isinstance(injury, dict):
        err(path, "'PracticeInjury' must be an FPSInjuryTuning object")
    else:
        chance, multiplier = injury.get("BaseInjuryChance"), injury.get("MaxFatigueMultiplier")
        low, high = injury.get("MinRecoveryWeeks"), injury.get("MaxRecoveryWeeks")
        if not is_number(chance) or not 0 <= chance <= 1:
            err(path, f"PracticeInjury.BaseInjuryChance: '{chance}' must be a number from 0 to 1")
        if not is_number(multiplier) or multiplier < 1:
            err(path, f"PracticeInjury.MaxFatigueMultiplier: '{multiplier}' must be a number, 1 or more")
        if (isinstance(low, bool) or not isinstance(low, int) or low < 1 or isinstance(high, bool)
                or not isinstance(high, int) or high < low):
            err(path, "PracticeInjury: MinRecoveryWeeks a whole number, 1 or more, and MaxRecoveryWeeks not below it")
        if set(injury) - INJURY_TUNING_FIELDS:
            err(path, f"PracticeInjury: unknown field(s) {sorted(set(injury) - INJURY_TUNING_FIELDS)}")

    areas = payload.get("FocusAreas")
    if not isinstance(areas, list):
        err(path, "'FocusAreas' must be an array")
        return
    seen = set()
    for idx, focus in enumerate(areas):
        where = f"FocusAreas[{idx}]"
        if not isinstance(focus, dict):
            err(path, f"{where}: not an object")
            continue
        focus_id = focus.get("FocusId")
        if not isinstance(focus_id, str) or not focus_id or focus_id in seen:
            err(path, f"{where}.FocusId: empty or duplicate '{focus_id}'")
        seen.add(focus_id)
        if not isinstance(focus.get("Label", ""), str):
            err(path, f"{where}.Label: must be a string")
        versus_offense = focus.get("bVersusOffense")
        if not isinstance(versus_offense, bool):
            err(path, f"{where}.bVersusOffense: must be true or false")
        categories = focus.get("Categories")
        if not isinstance(categories, list) or not categories or len(set(map(str, categories))) != len(categories):
            err(path, f"{where}.Categories: a non-empty array of distinct play categories")
        elif isinstance(versus_offense, bool):
            side = WEIGHTED_OFFENSE_CATEGORIES if versus_offense else WEIGHTED_DEFENSE_CATEGORIES
            for category in categories:
                if category not in side:
                    err(path, f"{where}.Categories: '{category}' must be one of {sorted(side)}")
                elif tracked is not None and category not in tracked[versus_offense]:
                    err(path, f"{where}.Categories: opponent_model.json doesn't track '{category}' on that side")
        roles = focus.get("Roles")
        if not isinstance(roles, list) or not roles or any(role not in PLAYER_ROLES for role in roles):
            err(path, f"{where}.Roles: a non-empty array of EPlayerRole values")
        validate_rating_weights(path, f"{where}.Ratings", focus.get("Ratings"), True)
        if set(focus) - TRAINING_FOCUS_FIELDS:
            err(path, f"{where}: unknown field(s) {sorted(set(focus) - TRAINING_FOCUS_FIELDS)} - names must match FPSGameplanFocusDef exactly")


PLAY_ART_NUMBERS = {
    "RibbonWidth": "above 0", "PrimaryWidthScale": "above 0", "GroundOffset": "0 or more",
    "RingRadius": "above 0", "BreakMarkerRadius": "0 or more", "SnapFadeSeconds": "0 or more",
    "EmphasisScale": "above 0", "ZoneStarRadius": "above 0", "ManLineWidth": "above 0", "RushArrowWidth": "above 0",
    "RushArrowDepth": "0 or more",
}
PLAY_ART_COLORS = ("UnrankedColor", "ZoneStarColor", "ManLineColor", "BlitzArrowColor", "RushArrowColor")
PLAY_ART_FIELDS = set(PLAY_ART_NUMBERS) | set(PLAY_ART_COLORS) | {
    "ReadColors", "BranchOpacity", "NoRouteArtCategories", "NoDefenseArtCategories", "bDrawDebug", "Diagram"}
PLAY_DIAGRAM_NUMBERS = {
    "MinFieldWidth": "above 0", "MinFieldDepth": "above 0", "FieldMargin": "0 or more", "WidthScale": "above 0",
    "MinStrokeWidth": "above 0", "PlayerRadius": "above 0", "MarkWidth": "above 0", "ArrowheadLength": "above 0",
    "RunBlockStemLength": "0 or more", "PassBlockStemLength": "0 or more", "BlockBarWidth": "above 0",
    "PreviewWidth": "above 0", "PreviewHeight": "above 0",
}
PLAY_DIAGRAM_FRACTIONS = ("OpponentOpacity", "GuideOpacity", "BackgroundOpacity")
PLAY_DIAGRAM_COLORS = ("OffenseColor", "DefenseColor", "BlockColor", "LineOfScrimmageColor", "BackgroundColor")
PLAY_DIAGRAM_FIELDS = set(PLAY_DIAGRAM_NUMBERS) | set(PLAY_DIAGRAM_FRACTIONS) | set(PLAY_DIAGRAM_COLORS) | {
    "ArrowheadAngleDegrees", "CircleSegments"}


def validate_play_diagram(path, diagram):
    """FPSPlayDiagramStyle (play_art.json's Diagram block, Epic 102.1); mirrors
    PSPlayDiagram::ValidateStyle."""
    if not isinstance(diagram, dict):
        err(path, "Diagram: must be an object (FPSPlayDiagramStyle)")
        return
    for field, rule in PLAY_DIAGRAM_NUMBERS.items():
        value = diagram.get(field)
        bad = not is_number(value) or (value <= 0 if rule == "above 0" else value < 0)
        if bad:
            err(path, f"Diagram.{field}: '{value}' must be a number {rule}")
    for field in PLAY_DIAGRAM_FRACTIONS:
        value = diagram.get(field)
        if not is_number(value) or not 0 <= value <= 1:
            err(path, f"Diagram.{field}: '{value}' must be a number from 0 to 1")
    for field in PLAY_DIAGRAM_COLORS:
        value = diagram.get(field)
        if not isinstance(value, str) or not HEX_COLOR.match(value):
            err(path, f"Diagram.{field}: '{value}' must be #RRGGBB")
    angle = diagram.get("ArrowheadAngleDegrees")
    if not is_number(angle) or not 0 < angle < 90:
        err(path, f"Diagram.ArrowheadAngleDegrees: '{angle}' must be above 0 and below 90")
    segments = diagram.get("CircleSegments")
    if not isinstance(segments, int) or isinstance(segments, bool) or segments < 6:
        err(path, f"Diagram.CircleSegments: '{segments}' must be a whole number, 6 or more")
    extra = set(diagram) - PLAY_DIAGRAM_FIELDS
    if extra:
        err(path, f"Diagram: unknown field(s) {sorted(extra)} - names must match FPSPlayDiagramStyle exactly")


def validate_play_art(path, payload):
    """FPSPlayArtStyle (Data/play_art.json, Epics 27 and 31); mirrors PSPlayArt::ValidateStyle,
    plus each no-art category one of its side's play categories."""
    for field, rule in PLAY_ART_NUMBERS.items():
        value = payload.get(field)
        bad = not is_number(value) or (value <= 0 if rule == "above 0" else value < 0)
        if bad:
            err(path, f"{field}: '{value}' must be a number {rule}")
    colors = payload.get("ReadColors")
    if not isinstance(colors, list) or not colors:
        err(path, "ReadColors: needs a color for the primary read at least")
    else:
        for idx, value in enumerate(colors):
            if not isinstance(value, str) or not HEX_COLOR.match(value):
                err(path, f"ReadColors[{idx}]: '{value}' must be #RRGGBB")
    for field in PLAY_ART_COLORS:
        value = payload.get(field)
        if not isinstance(value, str) or not HEX_COLOR.match(value):
            err(path, f"{field}: '{value}' must be #RRGGBB")
    opacity = payload.get("BranchOpacity")
    if not is_number(opacity) or not 0 <= opacity <= 1:
        err(path, f"BranchOpacity: '{opacity}' must be a number from 0 to 1")
    for field, side, known in (("NoRouteArtCategories", "an offensive", content_contracts.OFFENSE_CATEGORIES),
                               ("NoDefenseArtCategories", "a defensive", content_contracts.DEFENSE_CATEGORIES)):
        categories = payload.get(field)
        if not isinstance(categories, list):
            err(path, f"{field}: must be an array of play categories")
            continue
        for idx, category in enumerate(categories):
            if category not in known:
                err(path, f"{field}[{idx}]: '{category}' is not {side} play category")
    if not isinstance(payload.get("bDrawDebug"), bool):
        err(path, "bDrawDebug: must be true or false")
    if "Diagram" in payload:
        validate_play_diagram(path, payload["Diagram"])
    extra = set(payload) - PLAY_ART_FIELDS
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSPlayArtStyle exactly")


DRAFT_POSITIVE = ("PublicUncertainty", "ReportNoise", "ReportCost", "RangeSigmas", "RookieScaleExponent")
DRAFT_NON_NEGATIVE = ("BoomBustSwing", "MisleadSwing", "PointsPerSeason", "NeedWeight", "MaxFundingMultiplier")
DRAFT_FRACTIONS = ("CombineCertainty", "ProDayShare", "BoomBustChance", "MisleadChance", "FundingFloor",
                   "FirstPickGuarantee", "LastPickGuarantee")
DRAFT_COUNTS = {"NumRounds": 1, "AIScoutTargets": 1, "RookieYears": 1, "FirstPickSalary": 0}
DRAFT_DRILL_FIELDS = {"DrillId", "Label", "Attribute", "Base", "PerPoint", "Noise"}
DRAFT_DRILL_RATINGS = {"Speed", "Agility", "Strength", "Acceleration", "Awareness", "Stamina"}


def load_contract_max_years():
    """contracts.json's MaxContractYears, or None when it is missing or broken (its own checks
    report that)."""
    try:
        tuning = json.loads((DATA_DIR / "contracts.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    years = tuning.get("MaxContractYears") if isinstance(tuning, dict) else None
    return years if isinstance(years, int) and not isinstance(years, bool) else None


def validate_draft(path, payload, max_contract_years):
    """FPSDraftTuning (Data/draft.json, Epic 86); mirrors UPSDraft::ValidateTuning, and holds the
    rookie deal to contracts.json's MaxContractYears."""
    for field in DRAFT_POSITIVE:
        value = payload.get(field)
        if not is_number(value) or value <= 0:
            err(path, f"{field}: '{value}' must be a number above 0")
    for field in DRAFT_NON_NEGATIVE:
        value = payload.get(field)
        if not is_number(value) or value < 0:
            err(path, f"{field}: '{value}' must be a number, 0 or more")
    for field in DRAFT_FRACTIONS:
        value = payload.get(field)
        if not is_number(value) or not 0 <= value <= 1:
            err(path, f"{field}: '{value}' must be a number from 0 to 1")
    if payload.get("CombineCertainty") == 0:
        err(path, "CombineCertainty: must be above 0")
    if is_number(payload.get("MaxFundingMultiplier")) and payload["MaxFundingMultiplier"] < 1:
        err(path, "MaxFundingMultiplier: must be at least 1")
    for field, floor in DRAFT_COUNTS.items():
        value = payload.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < floor:
            err(path, f"{field}: '{value}' must be a whole number, {floor} or more")
    years = payload.get("RookieYears")
    if isinstance(years, int) and max_contract_years is not None and years > max_contract_years:
        err(path, f"RookieYears: {years} is longer than contracts.json's MaxContractYears ({max_contract_years})")
    known = set(DRAFT_POSITIVE) | set(DRAFT_NON_NEGATIVE) | set(DRAFT_FRACTIONS) | set(DRAFT_COUNTS) | {"CombineDrills"}
    if set(payload) - known:
        err(path, f"unknown field(s) {sorted(set(payload) - known)} - names must match FPSDraftTuning exactly")

    drills = payload.get("CombineDrills")
    if not isinstance(drills, list):
        err(path, "'CombineDrills' must be an array")
        return
    seen = set()
    for idx, drill in enumerate(drills):
        where = f"CombineDrills[{idx}]"
        if not isinstance(drill, dict):
            err(path, f"{where}: not an object")
            continue
        drill_id = drill.get("DrillId")
        if not isinstance(drill_id, str) or not drill_id or drill_id in seen:
            err(path, f"{where}.DrillId: empty or duplicate '{drill_id}'")
        seen.add(drill_id)
        if drill.get("Attribute") not in DRAFT_DRILL_RATINGS:
            err(path, f"{where}.Attribute: '{drill.get('Attribute')}' must be one of {sorted(DRAFT_DRILL_RATINGS)}")
        for field in ("Base", "PerPoint", "Noise"):
            if not is_number(drill.get(field)):
                err(path, f"{where}.{field}: must be a number")
        if is_number(drill.get("PerPoint")) and drill["PerPoint"] == 0:
            err(path, f"{where}.PerPoint: must not be 0 (the drill would read nothing)")
        if is_number(drill.get("Noise")) and drill["Noise"] < 0:
            err(path, f"{where}.Noise: must be 0 or more")
        if not isinstance(drill.get("Label", ""), str):
            err(path, f"{where}.Label: must be a string")
        if set(drill) - DRAFT_DRILL_FIELDS:
            err(path, f"{where}: unknown field(s) {sorted(set(drill) - DRAFT_DRILL_FIELDS)} - names must match FPSCombineDrill exactly")


PLAYER_STAT_CATEGORIES = {
    "PassingYards", "PassingTouchdowns", "Completions", "InterceptionsThrown", "RushingYards", "RushingTouchdowns",
    "Receptions", "ReceivingYards", "ReceivingTouchdowns", "Tackles", "Sacks", "Interceptions",
}
HALL_OF_FAME_INT_FIELDS = ("WaitSeasons", "MinSeasons", "MaxInducteesPerSeason")
AWARD_KINDS = ("OffensivePlayerOfWeek", "DefensivePlayerOfWeek", "MostValuablePlayer", "OffensivePlayerOfYear",
               "DefensivePlayerOfYear", "RookieOfYear")


AGING_CURVE_FIELDS = {"PeakAgeStart", "PeakAgeEnd", "GrowthPerYear", "DeclinePerYear", "LowSnapShareThreshold"}
RETIREMENT_FRACTIONS = ("BaseChance", "ChancePerYear", "LowRatingChance", "InjuredChance", "LowMorale", "LowMoraleChance",
                        "MaxRetirementShare")
RETIREMENT_INTS = ("MinAge", "ForcedAge", "RandomSeed")


def validate_legacy_aging(path, payload):
    """FPSLegacyTuning's RoleCurves (each an FPSProgressionTuning) and Retirement (Epic 94)."""
    curves = payload.get("RoleCurves")
    if not isinstance(curves, list):
        err(path, "'RoleCurves' must be an array of { Role, Curve }")
        curves = []
    seen = set()
    for idx, entry in enumerate(curves):
        where = f"RoleCurves[{idx}]"
        if not isinstance(entry, dict) or not isinstance(entry.get("Curve"), dict):
            err(path, f"{where}: must be an object with a Role and a Curve object")
            continue
        role = entry.get("Role")
        if role not in PLAYER_ROLES or role in seen:
            err(path, f"{where}.Role: '{role}' must be an EPlayerRole, listed once")
        seen.add(role)
        curve = entry["Curve"]
        start, end = curve.get("PeakAgeStart"), curve.get("PeakAgeEnd")
        if not isinstance(start, int) or not isinstance(end, int) or isinstance(start, bool) or isinstance(end, bool) or start > end:
            err(path, f"{where}.Curve: PeakAgeStart and PeakAgeEnd must be whole numbers, the start no later than the end")
        for field in ("GrowthPerYear", "DeclinePerYear"):
            if not is_number(curve.get(field)) or curve[field] < 0:
                err(path, f"{where}.Curve.{field}: must be a number, 0 or more")
        threshold = curve.get("LowSnapShareThreshold")
        if not is_number(threshold) or not 0 <= threshold <= 1:
            err(path, f"{where}.Curve.LowSnapShareThreshold: must be a number from 0 to 1")
        if set(curve) - AGING_CURVE_FIELDS or set(entry) - {"Role", "Curve"}:
            err(path, f"{where}: unknown field(s) - names must match FPSRoleAgingCurve and FPSProgressionTuning exactly")

    rules = payload.get("Retirement")
    if not isinstance(rules, dict):
        err(path, "'Retirement' must be an FPSRetirementTuning object")
        return
    for field in RETIREMENT_FRACTIONS:
        value = rules.get(field)
        if not is_number(value) or not 0 <= value <= 1:
            err(path, f"Retirement.{field}: '{value}' must be a number from 0 to 1")
    for field in RETIREMENT_INTS:
        value = rules.get(field)
        if isinstance(value, bool) or not isinstance(value, int):
            err(path, f"Retirement.{field}: '{value}' must be a whole number")
    if isinstance(rules.get("MinAge"), int) and isinstance(rules.get("ForcedAge"), int) and not 0 <= rules["MinAge"] < rules["ForcedAge"]:
        err(path, "Retirement: MinAge must be 0 or more and ForcedAge above it")
    rating = rules.get("LowRating")
    if not is_number(rating) or not 0 <= rating <= 100:
        err(path, f"Retirement.LowRating: '{rating}' must be a 0-100 rating")
    known = set(RETIREMENT_FRACTIONS) | set(RETIREMENT_INTS) | {"LowRating"}
    if set(rules) - known:
        err(path, f"Retirement: unknown field(s) {sorted(set(rules) - known)} - names must match FPSRetirementTuning exactly")


def validate_legacy(path, payload):
    """FPSLegacyTuning (Data/legacy.json, Epic 94); mirrors UPSLeagueHistory::ValidateTuning."""
    extra = set(payload) - {"HallOfFame", "LeaderCategories", "RoleCurves", "Retirement"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSLegacyTuning exactly")
    validate_legacy_aging(path, payload)
    hall = payload.get("HallOfFame")
    if not isinstance(hall, dict):
        err(path, "'HallOfFame' must be an FPSHallOfFameTuning object")
    else:
        for field in HALL_OF_FAME_INT_FIELDS:
            value = hall.get(field)
            if isinstance(value, bool) or not isinstance(value, int) or value < 0:
                err(path, f"HallOfFame.{field}: '{value}' must be a whole number, 0 or more")
        if hall.get("MaxInducteesPerSeason") == 0:
            err(path, "HallOfFame.MaxInducteesPerSeason: must be at least 1")
        score = hall.get("InductionScore")
        if not is_number(score) or score <= 0:
            err(path, f"HallOfFame.InductionScore: '{score}' must be a number above 0")
        thresholds = hall.get("Thresholds")
        if not isinstance(thresholds, list) or not thresholds:
            err(path, "HallOfFame.Thresholds: a non-empty array, or nobody is ever voted in")
            thresholds = []
        seen = set()
        for idx, threshold in enumerate(thresholds):
            where = f"HallOfFame.Thresholds[{idx}]"
            if not isinstance(threshold, dict):
                err(path, f"{where}: not an object")
                continue
            category = threshold.get("Category")
            if category not in PLAYER_STAT_CATEGORIES or category in seen:
                err(path, f"{where}.Category: '{category}' must be a player EPSStatCategory, listed once")
            seen.add(category)
            value = threshold.get("CareerValue")
            if isinstance(value, bool) or not isinstance(value, int) or value < 1:
                err(path, f"{where}.CareerValue: '{value}' must be a whole number, 1 or more")
            if set(threshold) - {"Category", "CareerValue"}:
                err(path, f"{where}: unknown field(s) {sorted(set(threshold) - {'Category', 'CareerValue'})}")
        awards = hall.get("AwardScores", [])
        if not isinstance(awards, list):
            err(path, "HallOfFame.AwardScores: must be an array of { Award, Score }")
            awards = []
        named = set()
        for idx, entry in enumerate(awards):
            where = f"HallOfFame.AwardScores[{idx}]"
            if not isinstance(entry, dict) or entry.get("Award") not in AWARD_KINDS or entry.get("Award") in named:
                err(path, f"{where}.Award: must be an EPSAwardKind ({list(AWARD_KINDS)}), listed once")
                continue
            named.add(entry["Award"])
            if not is_number(entry.get("Score")) or entry["Score"] < 0:
                err(path, f"{where}.Score: '{entry.get('Score')}' must be a number, 0 or more")
            if set(entry) - {"Award", "Score"}:
                err(path, f"{where}: unknown field(s) {sorted(set(entry) - {'Award', 'Score'})}")
        known = set(HALL_OF_FAME_INT_FIELDS) | {"InductionScore", "Thresholds", "AwardScores"}
        if set(hall) - known:
            err(path, f"HallOfFame: unknown field(s) {sorted(set(hall) - known)} - names must match FPSHallOfFameTuning exactly")
    leaders = payload.get("LeaderCategories")
    if not isinstance(leaders, list):
        err(path, "'LeaderCategories' must be an array of player EPSStatCategory names")
    else:
        for idx, category in enumerate(leaders):
            if category not in PLAYER_STAT_CATEGORIES or category in leaders[:idx]:
                err(path, f"LeaderCategories[{idx}]: '{category}' must be a player EPSStatCategory, listed once")


def load_input_catalog():
    """The input catalog the glyph table must cover, or None when it is missing or broken
    (its own checks report that)."""
    try:
        catalog = json.loads((DATA_DIR / "input_actions.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    return catalog if isinstance(catalog, dict) else None


DIFFICULTY_FIELDS = ("DifficultyTiers", "DifficultySetting", "PassLeadSetting", "AutoSlideSetting",
                     "SuggestedPlaySetting", "SuggestedPlayAccent")
DIFFICULTY_TIER_FIELDS = {"TierId", "Label", "AdaptationDial", "ThrowScatterScale", "Scales"}
DIFFICULTY_SCALE_FIELDS = {"Dial", "Target", "Field", "Scale"}


def validate_difficulty(path, payload):
    """FPSDifficultyCatalog (Data/difficulty.json, Epic 84); mirrors PSDifficulty::ValidateCatalog,
    plus the settings it names in ui_settings.json."""
    extra = set(payload) - set(DIFFICULTY_FIELDS)
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FPSDifficultyCatalog exactly")
    target_fields = {}
    for target, filename in DNA_BINDING_TARGETS.items():
        try:
            tuning = json.loads((DATA_DIR / filename).read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError, UnicodeDecodeError):
            continue
        if isinstance(tuning, dict):
            target_fields[target] = {k for k, v in tuning.items() if is_number(v)}
    tiers = payload.get("DifficultyTiers")
    if not isinstance(tiers, list) or not tiers:
        err(path, "'DifficultyTiers' must be a non-empty array")
        tiers = []
    ids, labels = set(), []
    for idx, tier in enumerate(tiers):
        where = f"DifficultyTiers[{idx}]"
        if not isinstance(tier, dict):
            err(path, f"{where}: must be an object")
            continue
        tier_id, label = tier.get("TierId"), tier.get("Label")
        if not isinstance(tier_id, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", tier_id) or tier_id in ids:
            err(path, f"{where}.TierId: '{tier_id}' must be an identifier used once")
        ids.add(tier_id)
        if not isinstance(label, str) or not label.strip():
            err(path, f"{where}.Label: must be a non-empty string")
        labels.append(label)
        dial = tier.get("AdaptationDial")
        if not is_number(dial) or not 0 <= dial <= 1:
            err(path, f"{where}.AdaptationDial: '{dial}' must be a number from 0 to 1")
        scatter = tier.get("ThrowScatterScale")
        if not is_number(scatter) or scatter <= 0:
            err(path, f"{where}.ThrowScatterScale: '{scatter}' must be a multiplier above 0")
        scaled = set()
        scales = tier.get("Scales")
        if not isinstance(scales, list):
            err(path, f"{where}.Scales: must be an array")
            scales = []
        for sidx, scale in enumerate(scales):
            swhere = f"{where}.Scales[{sidx}]"
            if not isinstance(scale, dict):
                err(path, f"{swhere}: must be an object")
                continue
            target, field = scale.get("Target"), scale.get("Field")
            if not isinstance(scale.get("Dial"), str) or not scale["Dial"].strip():
                err(path, f"{swhere}.Dial: must name the capability it turns")
            if target not in DNA_BINDING_TARGETS:
                err(path, f"{swhere}.Target: '{target}' must be one of {sorted(DNA_BINDING_TARGETS)} (an AI tuning, never a rating)")
            elif target in target_fields and field not in target_fields[target]:
                err(path, f"{swhere}.Field: '{field}' is not a number in {DNA_BINDING_TARGETS[target]}")
            if (target, field) in scaled:
                err(path, f"{swhere}: {target}.{field} is scaled twice")
            scaled.add((target, field))
            if not is_number(scale.get("Scale")) or scale["Scale"] <= 0:
                err(path, f"{swhere}.Scale: '{scale.get('Scale')}' must be a multiplier above 0")
            if set(scale) - DIFFICULTY_SCALE_FIELDS:
                err(path, f"{swhere}: unknown field(s) {sorted(set(scale) - DIFFICULTY_SCALE_FIELDS)}")
        if set(tier) - DIFFICULTY_TIER_FIELDS:
            err(path, f"{where}: unknown field(s) {sorted(set(tier) - DIFFICULTY_TIER_FIELDS)}")
    accent = payload.get("SuggestedPlayAccent")
    if not isinstance(accent, str) or not re.fullmatch(r"#[0-9A-Fa-f]{6}", accent):
        err(path, f"SuggestedPlayAccent: '{accent}' must be \"#RRGGBB\"")
    try:
        settings = json.loads((DATA_DIR / "ui_settings.json").read_text(encoding="utf-8"))
        rows = {s.get("SettingId"): s for s in settings.get("Settings", []) if isinstance(s, dict)}
    except (OSError, json.JSONDecodeError, UnicodeDecodeError, AttributeError):
        return
    difficulty = rows.get(payload.get("DifficultySetting"))
    if not difficulty or difficulty.get("Kind") != "Choice":
        err(path, f"DifficultySetting: '{payload.get('DifficultySetting')}' must be a Choice setting in ui_settings.json")
    elif difficulty.get("Choices") != labels:
        err(path, f"DifficultySetting: ui_settings.json's choices {difficulty.get('Choices')} must be the tiers' labels in order {labels}")
    for field in ("PassLeadSetting", "AutoSlideSetting", "SuggestedPlaySetting"):
        row = rows.get(payload.get(field))
        if not row or row.get("Kind", "Toggle") != "Toggle":
            err(path, f"{field}: '{payload.get(field)}' must be a Toggle setting in ui_settings.json")


def main(root=None):
    """Checks root's Data/ (the repo's by default) and returns the exit code."""
    repo = Path(root).resolve() if root else REPO
    data_dir = repo / "Data"
    if not data_dir.is_dir():
        print(f"validate_data: no Data/ directory under {repo} - nothing to check")
        return 0
    files = sorted(data_dir.rglob("*.json"))
    name_forms = load_name_forms()
    parsed = {}
    for path in files:
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            err(path, f"invalid JSON: {exc}")
            continue
        parsed[path] = payload
        content_contracts.check_file(path, payload, err)
        if isinstance(payload, dict) and "Players" in payload:
            if not isinstance(payload["Players"], list):
                err(path, "'Players' must be an array")
            else:
                validate_players(path, payload["Players"], load_dna_catalog())
                content_contracts.validate_name_policy(path, payload["Players"], name_forms, err)
        if isinstance(payload, dict) and "Contexts" in payload and "Actions" in payload:
            validate_input_catalog(path, payload)
        if isinstance(payload, dict) and "StickDeadZoneLower" in payload:
            validate_input_tuning(path, payload)
        if isinstance(payload, dict) and "Screens" in payload and "RootScreen" in payload:
            validate_menu_catalog(path, payload)
        if isinstance(payload, dict) and isinstance(payload.get("Teams"), list):
            validate_team_identity(path, payload["Teams"])
        if isinstance(payload, dict) and "Tips" in payload:
            validate_loading_tips(path, payload)
        if isinstance(payload, dict) and "Cues" in payload and "MasterIntensity" in payload:
            validate_force_feedback(path, payload)
        if isinstance(payload, dict) and "OpenSeparation" in payload:
            validate_skill_ai_tuning(path, payload)
        if isinstance(payload, dict) and "ManCushion" in payload:
            validate_defender_ai_tuning(path, payload)
        if isinstance(payload, dict) and "Adjustments" in payload:
            validate_defensive_adjustments(path, payload)
        if isinstance(payload, dict) and "CpuSnapDelaySeconds" in payload:
            validate_play_call_tuning(path, payload)
        if isinstance(payload, dict) and "GlyphSets" in payload:
            validate_input_glyphs(path, payload, load_input_catalog())
        if isinstance(payload, dict) and "Tiers" in payload:
            validate_platform_tiers(path, payload)
        if isinstance(payload, dict) and "SlotActions" in payload:
            validate_passing_input(path, payload, load_input_catalog())
        if isinstance(payload, dict) and "FrameTimeBucketMs" in payload:
            validate_session_telemetry(path, payload)
        if isinstance(payload, dict) and "Moves" in payload:
            validate_carrier_moves(path, payload, load_input_catalog())
        if isinstance(payload, dict) and "PocketRadius" in payload:
            validate_pocket_tuning(path, payload)
        if isinstance(payload, dict) and "KeyframeEvents" in payload:
            validate_telemetry_sampling(path, payload)
        if isinstance(payload, dict) and "ReticleStates" in payload:
            validate_overlay_reticle(path, payload)
        if isinstance(payload, dict) and "CycleWindowSeconds" in payload:
            validate_control_handoff(path, payload, load_input_catalog())
        if isinstance(payload, dict) and "ChyronKinds" in payload:
            validate_broadcast_overlay(path, payload)
        if isinstance(payload, dict) and "DefenseNameFallback" in payload:
            validate_personnel_panel(path, payload)
        if isinstance(payload, dict) and "UprightWidth" in payload:
            validate_ball_flight_overlay(path, payload)
        if isinstance(payload, dict) and "RoleLabels" in payload:
            validate_overlay_badges(path, payload)
        if isinstance(payload, dict) and "DimStencil" in payload:
            validate_player_emphasis(path, payload)
        if isinstance(payload, dict) and "SituationTempos" in payload:
            validate_situational_tuning(path, payload, load_route_ids())
        if isinstance(payload, dict) and "KickoffTouchbackChance" in payload:
            validate_special_teams(path, payload, load_return_formations())
        if isinstance(payload, dict) and "Schemes" in payload and "Staffs" in payload:
            validate_coaching_staffs(path, payload, load_playbook_plays(), load_team_ids())
        if isinstance(payload, dict) and "PressRadius" in payload:
            validate_route_running(path, payload)
        if isinstance(payload, dict) and "UncoveredSeparation" in payload:
            validate_blown_coverage(path, payload)
        if isinstance(payload, dict) and "Routes" in payload:
            validate_routes(path, payload)
        if isinstance(payload, dict) and "HotRouteSets" in payload:
            validate_presnap_tuning(path, payload, load_input_catalog(), load_route_ids())
        if isinstance(payload, dict) and "MaxQueued" in payload:
            validate_input_buffer(path, payload, load_input_catalog())
        if isinstance(payload, dict) and "JumpWindowSeconds" in payload:
            validate_defensive_techniques(path, payload, load_input_catalog())
        if isinstance(payload, dict) and "PowerFillSeconds" in payload:
            validate_kick_meter(path, payload, load_input_catalog())
        if isinstance(payload, dict) and "RushMoves" in payload:
            validate_rush_moves(path, payload)
        if isinstance(payload, dict) and "CaptionWordsPerSecond" in payload:
            validate_ui_accessibility(path, payload)
        if isinstance(payload, dict) and "Hints" in payload:
            validate_ui_hints(path, payload)
        if isinstance(payload, dict) and "Fronts" in payload:
            validate_run_fits(path, payload)
        if isinstance(payload, dict) and "All22Rigs" in payload:
            validate_all22_camera(path, payload)
        if isinstance(payload, dict) and "Settings" in payload and "Categories" in payload:
            validate_settings_catalog(path, payload)
        if isinstance(payload, dict) and "CutRules" in payload:
            validate_camera_director(path, payload, load_all22_rig_ids())
        if isinstance(payload, dict) and "CatenaryParameterCm" in payload:
            validate_skycam(path, payload)
        if isinstance(payload, dict) and "Packages" in payload and "DefaultOffensePackage" in payload:
            validate_personnel_catalog(path, payload)
        if isinstance(payload, dict) and "LeverageShade" in payload:
            validate_coverage_matchups(path, payload)
        if isinstance(payload, dict) and "ScoopClearRadius" in payload:
            validate_loose_ball(path, payload)
        if isinstance(payload, dict) and "MeshRecognizeRadius" in payload:
            validate_deception(path, payload)
        if isinstance(payload, dict) and "TouchControls" in payload:
            validate_touch_controls(path, payload, load_input_catalog(), load_input_glyphs())
        if isinstance(payload, dict) and "Axes" in payload and "Bindings" in payload:
            validate_player_dna_catalog(path, payload)
        if isinstance(payload, dict) and "SalaryCap" in payload and "PositionMarkets" in payload:
            validate_contract_tuning(path, payload)
        if isinstance(payload, dict) and "ShellSafeties" in payload:
            validate_defensive_presnap(path, payload, load_input_catalog())
        if isinstance(payload, dict) and "PlaybackRates" in payload:
            validate_replay_tuning(path, payload, load_all22_rig_ids())
        if isinstance(payload, dict) and "Counters" in payload and "MinSamples" in payload:
            validate_opponent_model(path, payload)
        if isinstance(payload, dict) and "PausesPerHalf" in payload:
            validate_versus_rules(path, payload)
        if isinstance(payload, dict) and "bLogDecisions" in payload:
            validate_ai_debug(path, payload)
        if isinstance(payload, dict) and "Scenarios" in payload:
            validate_ai_scenarios(path, payload, load_dna_catalog())
        if isinstance(payload, dict) and "StadiumCapacity" in payload:
            validate_owner_economics(path, payload)
        if isinstance(payload, dict) and "UnownedColor" in payload:
            validate_gap_overlay(path, payload)
        if isinstance(payload, dict) and "TradeRequestWeeks" in payload:
            validate_morale(path, payload)
        if isinstance(payload, dict) and "RoleProfiles" in payload and "NameCultures" in payload:
            validate_league_generator(path, payload)
        if isinstance(payload, dict) and "PeakAgeStart" in payload and "GrowthPerYear" in payload:
            validate_progression(path, payload)
        if isinstance(payload, dict) and "Concepts" in payload and "Coverages" in payload:
            validate_playbook_generator(path, payload)
        if isinstance(payload, dict) and "ReelSize" in payload:
            validate_highlights(path, payload)
        if isinstance(payload, dict) and "PlayerPickRadius" in payload:
            validate_telestrator(path, payload)
        if isinstance(payload, dict) and "DifficultyTiers" in payload:
            validate_difficulty(path, payload)
        if isinstance(payload, dict) and "HardFailMultiplier" in payload:
            validate_perf_harness(path, payload)
        if isinstance(payload, dict) and "FocusAreas" in payload:
            validate_training(path, payload, load_opponent_model_tracked())
        if isinstance(payload, dict) and "CaptureResolutionMultiplier" in payload:
            validate_photo_mode(path, payload)
        if isinstance(payload, dict) and "ReadColors" in payload:
            validate_play_art(path, payload)
        if isinstance(payload, dict) and "CombineDrills" in payload:
            validate_draft(path, payload, load_contract_max_years())
        if isinstance(payload, dict) and "PlayCallTimeoutSeconds" in payload:
            validate_game_intelligence(path, payload)
        if isinstance(payload, dict) and "HallOfFame" in payload:
            validate_legacy(path, payload)
        if isinstance(payload, dict) and "PickRoundValues" in payload:
            validate_trades(path, payload)
        if isinstance(payload, dict) and "StorylineKinds" in payload:
            validate_narrative(path, payload)
        if isinstance(payload, dict) and "FormationClasses" in payload:
            validate_play_recognition(path, payload)
        if isinstance(payload, dict) and "HoldingChancePerPlay" in payload:
            validate_penalties(path, payload)
        if isinstance(payload, dict) and "EventCues" in payload:
            validate_audio_cues(path, payload)
        if isinstance(payload, dict) and "CrowdReactions" in payload:
            validate_crowd(path, payload)
        if isinstance(payload, dict) and "ModelMoments" in payload:
            validate_commentary_hooks(path, payload)
        if isinstance(payload, dict) and "CentimetresPerYard" in payload:
            validate_field_dimensions(path, payload)
        if isinstance(payload, dict) and "HashOffsetYards" in payload:
            validate_field_markings(path, payload)
        if isinstance(payload, dict) and "PlayDemos" in payload:
            validate_play_demos(path, payload)
        if isinstance(payload, dict) and "PressedOpacity" in payload:
            validate_touch_hud(path, payload)
        if isinstance(payload, dict) and "SkillWindowGrowthPerSecond" in payload:
            validate_session_matchmaking(path, payload)
        if isinstance(payload, dict) and "InterruptMargin" in payload:
            validate_commentary_lines(path, payload)
        if isinstance(payload, dict) and "Techniques" in payload:
            validate_formations(path, payload)
    content_contracts.check_references(repo, parsed, err)
    if root is None:
        validate_ui_text()
    if errors:
        print(f"validate_data: {len(errors)} error(s):")
        for e in errors:
            print("  " + e)
        return 1
    print(f"validate_data: OK ({len(files)} JSON file(s) clean)")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Validate Data/ against the content contracts.")
    parser.add_argument("--root", help="check this directory's Data/ instead of the repo's")
    sys.exit(main(parser.parse_args().root))
