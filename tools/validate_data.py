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
and "Scenarios" files against FPSAIScenarioCatalog, each expectation and cover target naming a
player of its scenario (Epic 85); "StadiumCapacity" files against FPSEconomyTuning (Epic 95):
ordered prices and fill rates, 0-1 satisfaction, the default budget within MaxBudgetFraction;
"UnownedColor" files against FPSGapOverlayStyle (Epic 81); "TradeRequestWeeks" files against
FPSMoraleTuning (Epic 91): 0-1 thresholds, each chemistry unit's role, games and bonus. Teams, the
league config, the playbook, player rating ranges and every reference between files are
tools/content_contracts.py's (Epic 125), run from here.

Exit 0 when clean, exit 1 with actionable errors (file / row / field).
Run from the repo root:  python tools/validate_data.py
"""

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

INPUT_VALUE_TYPES = {"Boolean", "Axis1D", "Axis2D", "Axis3D"}
INPUT_CONTEXT_FIELDS = {"ContextId": str, "Priority": int, "Description": str, "bRemappable": bool}
INPUT_ACTION_FIELDS = {"ActionId": str, "ValueType": str, "Description": str, "Contexts": list, "Bindings": list}
INPUT_BINDING_FIELDS = {"Key": str, "bSwizzleYX": bool, "bNegate": bool}
INPUT_REQUIRED = {"ContextId", "ActionId", "ValueType", "Contexts", "Bindings", "Key"}

errors = []


def err(path, message):
    errors.append(f"{path.relative_to(REPO)}: {message}")


def validate_players(path, players, dna_catalog=None):
    seen_ids = set()
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
        extra = set(row) - set(PLAYER_FIELDS) - {"DNA"}
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPlayerAttributes exactly")
        role = row.get("Role")
        if isinstance(role, str) and role not in PLAYER_ROLES:
            err(path, f"{where}.Role: '{role}' is not a valid EPlayerRole")
        if "DNA" in row:
            validate_player_dna(path, where, row, dna_catalog)
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
                      "ContainWidth", "PassReadDepth", "PassDropDepth", "MaxReactionSeconds", "BallHawkRadius",
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
        if tier.get("OverlayDetail") not in OVERLAY_DETAILS:
            err(path, f"{where}.OverlayDetail: '{tier.get('OverlayDetail')}' must be one of {sorted(OVERLAY_DETAILS)}")
        extra = set(tier) - {"TierId", "Description", "DeviceProfile", "AIDecisionInterval", "OverlayDetail",
                             "ReplayPoseRateHz", *TIER_TELEMETRY_NUMBERS}
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
    extra = set(payload) - {"CycleWindowSeconds", "PickLeftAction", "PickRightAction"}
    if extra:
        err(path, f"unknown field(s) {sorted(extra)} - names must match FControlHandoffTuningRow exactly")

RECEIVER_ALIGNMENTS = {"Wide", "Slot", "Tight", "Backfield"}
PRESNAP_NUMBERS = ("SlotMaxSplit", "MotionEndSplit", "MotionArrivalRadius", "ManTravelLateralRadius", "SlideAimOffset",
                   "BoxWidth", "BoxDepth", "CpuReadMinAwareness")
PRESNAP_COUNTS = ("HeavyBoxCount", "LightBoxCount")
PRESNAP_FLAGS = ("bCpuKeepsBackInVsBlitz", "bCpuMotionOnPass")
PRESNAP_ACTIONS = ("AudibleAction", "SelectAction", "HotRouteAction", "MotionAction", "SlideAction", "ProtectionAction")


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


AI_DEBUG_FIELDS = {"bLogDecisions": bool, "bWritePostMortems": bool, "PostMortemDirectory": str, "MaxPostMortemFiles": int,
                   "MaxRecordsPerPlay": int, "OverlayHeightCm": (int, float), "OverlayFontScale": (int, float)}
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


def load_input_catalog():
    """The input catalog the glyph table must cover, or None when it is missing or broken
    (its own checks report that)."""
    try:
        catalog = json.loads((DATA_DIR / "input_actions.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    return catalog if isinstance(catalog, dict) else None


def main():
    if not DATA_DIR.is_dir():
        print("validate_data: no Data/ directory - nothing to check")
        return 0
    files = sorted(DATA_DIR.rglob("*.json"))
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
    content_contracts.check_references(REPO, parsed, err)
    validate_ui_text()
    if errors:
        print(f"validate_data: {len(errors)} error(s):")
        for e in errors:
            print("  " + e)
        return 1
    print(f"validate_data: OK ({len(files)} JSON file(s) clean)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
