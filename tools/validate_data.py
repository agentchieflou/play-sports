#!/usr/bin/env python
"""Data contract validator for play-sports (Epic 113).

Validates every JSON file under Data/ : parseability always; files carrying a
"Players" array additionally against the FPlayerAttributes contract
(Source/PlaySports/Public/PSPlayerAttributes.h) - exact field names, numeric
types, valid EPlayerRole values, unique non-empty PlayerId.

Exit 0 when clean, exit 1 with actionable errors (file / row / field).
Run from the repo root:  python tools/validate_data.py
"""

import json
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
    if errors:
        print(f"validate_data: {len(errors)} error(s):")
        for e in errors:
            print("  " + e)
        return 1
    print(f"validate_data: OK ({len(files)} JSON file(s) clean)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
