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
FPSLoadingTipCatalog.

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


MENU_COMMANDS = {"None", "Resume", "StartPlayNow", "StartFranchise", "StartPractice", "QuitToMainMenu", "QuitGame"}
MENU_CONTENTS = {"Static", "TeamSelect", "Loading"}
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
    if payload.get("TransitionSeconds", 0) < 0:
        err(path, "TransitionSeconds must not be negative")
    for sid, screen in by_id.items():
        content = screen.get("Content", "Static")
        if content not in MENU_CONTENTS:
            err(path, f"Screen '{sid}': unknown Content '{content}' ({sorted(MENU_CONTENTS)})")
        options = screen.get("Options", [])
        if not options and not screen.get("bAllowBack", True) and content != "Loading":
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
    if errors:
        print(f"validate_data: {len(errors)} error(s):")
        for e in errors:
            print("  " + e)
        return 1
    print(f"validate_data: OK ({len(files)} JSON file(s) clean)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
