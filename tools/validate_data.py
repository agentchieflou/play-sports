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
catalog binds has a glyph; "CpuSnapDelaySeconds" files against FPlayCallTuningRow.

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


MENU_COMMANDS = {"None", "Resume", "StartPlayNow", "StartFranchise", "StartPractice", "QuitToMainMenu", "QuitGame", "CallPlay"}
MENU_CONTENTS = {"Static", "TeamSelect", "Loading", "PlayCallFormations", "PlayCallPlays", "PlayCallRecent"}
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
        if isinstance(payload, dict) and "CpuSnapDelaySeconds" in payload:
            validate_play_call_tuning(path, payload)
        if isinstance(payload, dict) and "GlyphSets" in payload:
            validate_input_glyphs(path, payload, load_input_catalog())
    if errors:
        print(f"validate_data: {len(errors)} error(s):")
        for e in errors:
            print("  " + e)
        return 1
    print(f"validate_data: OK ({len(files)} JSON file(s) clean)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
