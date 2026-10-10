#!/usr/bin/env python
"""Data contract validator for play-sports (Epic 113).

Validates every JSON file under Data/ : parseability always; files carrying a
"Players" array additionally against the FPlayerAttributes contract
(Source/PlaySports/Public/PSPlayerAttributes.h) - exact field names, numeric
types, valid EPlayerRole values, unique non-empty PlayerId; files carrying
"Contexts" + "Actions" against the input catalog contract (FPSInputCatalog,
Source/PlaySports/Public/PSInputConfigTypes.h; Specs/Input_Architecture.md); files
carrying "StickDeadZoneLower" against FInputTuningRow's ranges; files carrying
"Screens" + "RootScreen" against the menu catalog rules (FPSMenuCatalog); team
identity fields (colors, abbreviation) on "Teams" files; "Tips" files against
FPSLoadingTipCatalog; "Cues" + "MasterIntensity" files against FPSForceFeedbackTuning;
"GlyphSets" files against FPSInputGlyphCatalog, including that every key the input
catalog binds has a glyph; "CpuSnapDelaySeconds" files against FPlayCallTuningRow;
"Adjustments" files against FPSDefensiveAdjustmentCatalog; "OpenSeparation" files against
FSkillPlayerAITuningRow; "ManCushion" files against FDefenderAITuningRow; "SlotActions" files
against FPassingInputTuningRow, including that each named action is a Boolean in the input
catalog's Passing context; "Moves" files against FPSCarrierMoveCatalog, each move's action a
Boolean in the BallCarrier context; "Tiers" files against FPSPlatformTierCatalog, each tier's
DeviceProfile defined by the engine (Windows, IOS, ...) or in Config/DefaultDeviceProfiles.ini;
"MaxQueued" files against FInputBufferTuningRow, each buffered action a Boolean catalog action;
"RushMoves" files against FPSRushMoveCatalog; "HotRouteSets" files against FPreSnapTuningRow, each
route in the route library and each action a Boolean in the PreSnap context; "SituationTempos"
files against FPSSituationalTuning, its route IDs against the route library; "KeyframeEvents"
files against FPSTelemetrySamplingTuning, each event an EPSTelemetryEventType as the bus header
declares it; "FrameTimeBucketMs" files against FPSSessionTelemetryTuning (Epic 117); "Fronts"
files against FPSRunFitCatalog; "PressRadius" files against FRouteRunningTuningRow;
"Routes" files against the FPSRoute library (timing, fakes, option branches); "ReticleStates" files
against FPSOverlayReticleStyle; "CycleWindowSeconds" files against FControlHandoffTuningRow, each
pick action a Boolean in the input catalog's PreSnap context.

Exit 0 when clean, exit 1 with actionable errors (file / row / field).
Run from the repo root:  python tools/validate_data.py
"""

import json
import re
import sys
from pathlib import Path

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
INPUT_CONTEXT_FIELDS = {"ContextId": str, "Priority": int, "Description": str}
INPUT_ACTION_FIELDS = {"ActionId": str, "ValueType": str, "Description": str, "Contexts": list, "Bindings": list}
INPUT_BINDING_FIELDS = {"Key": str, "bSwizzleYX": bool, "bNegate": bool}
INPUT_REQUIRED = {"ContextId", "ActionId", "ValueType", "Contexts", "Bindings", "Key"}

errors = []


def err(path, message):
    errors.append(f"{path.relative_to(REPO)}: {message}")


def validate_players(path, players):
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
        extra = set(row) - set(PLAYER_FIELDS)
        if extra:
            err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match FPlayerAttributes exactly")
        role = row.get("Role")
        if isinstance(role, str) and role not in PLAYER_ROLES:
            err(path, f"{where}.Role: '{role}' is not a valid EPlayerRole")
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


MENU_COMMANDS = {"None", "Resume", "StartPlayNow", "StartFranchise", "StartPractice", "QuitToMainMenu", "QuitGame", "CallPlay", "ApplyAdjustment"}
MENU_CONTENTS = {"Static", "TeamSelect", "Loading", "PlayCallFormations", "PlayCallPlays", "PlayCallRecent",
                 "PlayCallFavorites", "PlayCallAdjustments"}
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


INPUT_DEVICES = ("KeyboardMouse", "Gamepad")


def key_device(key):
    return "Gamepad" if is_gamepad_key(key) else "KeyboardMouse"


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
                   "ReadWindowSeconds", "MaxAnticipationSeconds")


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
        if tier.get("OverlayDetail") not in OVERLAY_DETAILS:
            err(path, f"{where}.OverlayDetail: '{tier.get('OverlayDetail')}' must be one of {sorted(OVERLAY_DETAILS)}")
        extra = set(tier) - {"TierId", "Description", "DeviceProfile", "AIDecisionInterval", "OverlayDetail",
                             *TIER_TELEMETRY_NUMBERS}
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


TEMPOS = ("Huddle", "NoHuddle", "HurryUp", "MilkClock")
GAME_SITUATIONS = ("Normal", "TwoMinuteDrill", "FourMinuteOffense", "VictoryFormation")
SITUATIONAL_SECONDS = ("TwoMinuteWindowSeconds", "TwoScoreWindowSeconds", "ClockUrgencySeconds", "SpikeMinSeconds",
                       "FourMinuteWindowSeconds", "DefenseTimeoutWindowSeconds", "KneelPlaySeconds",
                       "KneelPreSnapSeconds", "EndOfHalfKneelSeconds", "ClockPlayWeight")
SITUATIONAL_COUNTS = ("OneScorePoints", "MaxSpikeDown", "MaxDeficitToChase", "EndOfHalfKneelMaxYardLine")
SITUATIONAL_FIELDS = set(SITUATIONAL_SECONDS) | set(SITUATIONAL_COUNTS) | {
    "Tempos", "SituationTempos", "HumanTempoCycle", "SpikeTempo", "KneelTempo", "SidelineRouteIds",
    "MiddleRouteIds", "SidelinePlayDelta", "MiddlePlayDelta", "CategoryWeights"}


def load_route_ids():
    """The route library's IDs, or None when it is missing or broken (its own checks report that)."""
    try:
        routes = json.loads((DATA_DIR / "sample_routes.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    if not isinstance(routes, dict) or not isinstance(routes.get("Routes"), list):
        return None
    return {r.get("RouteId") for r in routes["Routes"] if isinstance(r, dict)}


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
    for path in files:
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            err(path, f"invalid JSON: {exc}")
            continue
        if isinstance(payload, dict) and "Players" in payload:
            if not isinstance(payload["Players"], list):
                err(path, "'Players' must be an array")
            else:
                validate_players(path, payload["Players"])
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
        if isinstance(payload, dict) and "KeyframeEvents" in payload:
            validate_telemetry_sampling(path, payload)
        if isinstance(payload, dict) and "ReticleStates" in payload:
            validate_overlay_reticle(path, payload)
        if isinstance(payload, dict) and "CycleWindowSeconds" in payload:
            validate_control_handoff(path, payload, load_input_catalog())
        if isinstance(payload, dict) and "SituationTempos" in payload:
            validate_situational_tuning(path, payload, load_route_ids())
        if isinstance(payload, dict) and "PressRadius" in payload:
            validate_route_running(path, payload)
        if isinstance(payload, dict) and "Routes" in payload:
            validate_routes(path, payload)
        if isinstance(payload, dict) and "HotRouteSets" in payload:
            validate_presnap_tuning(path, payload, load_input_catalog(), load_route_ids())
        if isinstance(payload, dict) and "MaxQueued" in payload:
            validate_input_buffer(path, payload, load_input_catalog())
        if isinstance(payload, dict) and "RushMoves" in payload:
            validate_rush_moves(path, payload)
        if isinstance(payload, dict) and "Fronts" in payload:
            validate_run_fits(path, payload)
    if errors:
        print(f"validate_data: {len(errors)} error(s):")
        for e in errors:
            print("  " + e)
        return 1
    print(f"validate_data: OK ({len(files)} JSON file(s) clean)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
