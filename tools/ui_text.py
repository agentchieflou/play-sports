#!/usr/bin/env python
"""UI text tables for localization (Epic 106).

Two CSV string tables hold everything the UI shows. UPSLocalization registers both and UE's
localization gather collects them:

  Data/ui_text.csv       "PSUI": the code's own text, written by hand (Key, SourceString,
                         Comment). Code names its keys: UPSLocalization::GetText(TEXT("Key"))
                         and UPSLocalization::Format(TEXT("Key"), ...).
  Data/ui_text_data.csv  "PSUIData": generated from the UI data files' strings
                         (ui_menus.json, ui_settings.json, loading_tips.json,
                         defensive_adjustments.json, ui_hints.json, and the scouting traits
                         in player_dna.json). Don't edit it; run this script with --write
                         after changing one of those files.

  python tools/ui_text.py           check (exit 1 with the problems)
  python tools/ui_text.py --write   regenerate Data/ui_text_data.csv

tools/validate_data.py runs the same check through problems(). Besides the tables it checks
the code:
  - every key the code names exists in Data/ui_text.csv;
  - every remappable input action has an Input.Action.<ActionId> name, and each of its
    contexts an Input.Context.<ContextId> name (the key remapping screen shows them);
  - UI code (Private/PSUI*, PSMenu*, PSHUD*, PSLoading*, PSSettings*, PSPlayCall*, and the
    broadcast overlays' PSOverlay* and PSGameStateEvents*) builds no FText from raw strings:
    its text comes through UPSLocalization (GetText, Format, GetDataText, Verbatim,
    FromLocalized).

Keys of generated rows (UPSLocalization::MenuKey and friends build the same ones):
  Menu.<ScreenId>.Title | Body
  Menu.<ScreenId>.<OptionId>.Label | Detail
  Setting.Category.<CategoryId>
  Setting.<SettingId>.Label | Description | Unit
  Setting.<SettingId>.Choice<Index>
  Tip.<TipId>
  Trait.<TraitId>.Label | Description     (PSPlayerDNA::TraitKey)
  Adjustment.<AdjustmentId>.Label | Description
  Hint.<HintId>
  Broadcast.HomeLabel | AwayLabel                        (UPSOverlayBroadcastSubsystem)
  BallFlight.GoodLabel | WideLeftLabel | WideRightLabel | ShortLabel
  Badge.Role.<Role>                                      (UPSOverlayBadgeComponent)
  Personnel.Role.<Role>, Personnel.OffenseNameFormat, Personnel.DefenseName.<Backs>,
  Personnel.DefenseNameFallback, Personnel.Package.<PackageId>  (UPSOverlayPersonnelSubsystem)
"""

import csv
import io
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DATA_DIR = REPO / "Data"
UI_TEXT = DATA_DIR / "ui_text.csv"
UI_TEXT_DATA = DATA_DIR / "ui_text_data.csv"
SOURCE_DIR = REPO / "Source"
GATED_PREFIXES = ("PSUI", "PSMenu", "PSHUD", "PSLoading", "PSSettings", "PSPlayCall", "PSOverlay", "PSGameStateEvents")
RAW_TEXT = re.compile(r"\bFText::FromString\(|\bFText::AsCultureInvariant\(|\bN?S?LOCTEXT\(|\bINVTEXT\(")
KEY_USE = re.compile(r"(?<![\w:])(?:UPSLocalization::)?(?:GetText|Format)\(\s*TEXT\(\"([^\"]+)\"\)")


def _load(name):
    path = DATA_DIR / name
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None


def data_rows():
    """(key, source, comment) for every user-facing string in the UI data files, in file order."""
    rows = []

    def add(key, source, comment):
        if isinstance(source, str) and source:
            rows.append((key, source, comment))

    menus = _load("ui_menus.json") or {}
    for screen in menus.get("Screens", []) if isinstance(menus, dict) else []:
        if not isinstance(screen, dict):
            continue
        screen_id = screen.get("ScreenId", "")
        add(f"Menu.{screen_id}.Title", screen.get("Title"), f"ui_menus.json: screen {screen_id}, title")
        add(f"Menu.{screen_id}.Body", screen.get("Body"), f"ui_menus.json: screen {screen_id}, text")
        for option in screen.get("Options", []) or []:
            if not isinstance(option, dict):
                continue
            option_id = option.get("OptionId", "")
            add(f"Menu.{screen_id}.{option_id}.Label", option.get("Label"), f"ui_menus.json: screen {screen_id}, option")
            add(f"Menu.{screen_id}.{option_id}.Detail", option.get("Detail"), f"ui_menus.json: screen {screen_id}, option detail")

    settings = _load("ui_settings.json") or {}
    if isinstance(settings, dict):
        for category in settings.get("Categories", []) or []:
            if isinstance(category, dict):
                category_id = category.get("CategoryId", "")
                add(f"Setting.Category.{category_id}", category.get("Label"), "ui_settings.json: settings category")
        for setting in settings.get("Settings", []) or []:
            if not isinstance(setting, dict):
                continue
            setting_id = setting.get("SettingId", "")
            add(f"Setting.{setting_id}.Label", setting.get("Label"), "ui_settings.json: setting name")
            add(f"Setting.{setting_id}.Description", setting.get("Description"), "ui_settings.json: setting description")
            add(f"Setting.{setting_id}.Unit", setting.get("Unit"), "ui_settings.json: unit shown after the value")
            for index, choice in enumerate(setting.get("Choices", []) or []):
                add(f"Setting.{setting_id}.Choice{index}", choice, f"ui_settings.json: a choice of {setting_id}")

    tips = _load("loading_tips.json") or {}
    for tip in tips.get("Tips", []) if isinstance(tips, dict) else []:
        if isinstance(tip, dict):
            add(f"Tip.{tip.get('TipId', '')}", tip.get("Text"), "loading_tips.json: loading screen tip")

    dna = _load("player_dna.json") or {}
    for axis in dna.get("Axes", []) if isinstance(dna, dict) else []:
        if not isinstance(axis, dict):
            continue
        for end in ("Low", "High"):
            trait = axis.get(f"{end}Trait", "")
            add(f"Trait.{trait}.Label", axis.get(f"{end}Label"), f"player_dna.json: the scouting trait at the {end.lower()} end of {axis.get('Axis', '')}")
            add(f"Trait.{trait}.Description", axis.get(f"{end}Description"), f"player_dna.json: what the {trait} trait means")

    adjustments = _load("defensive_adjustments.json") or {}
    for adjustment in adjustments.get("Adjustments", []) if isinstance(adjustments, dict) else []:
        if isinstance(adjustment, dict):
            adjustment_id = adjustment.get("AdjustmentId", "")
            add(f"Adjustment.{adjustment_id}.Label", adjustment.get("Label"), "defensive_adjustments.json: play-call adjustment")
            add(f"Adjustment.{adjustment_id}.Description", adjustment.get("Description"), "defensive_adjustments.json: adjustment detail")

    hints = _load("ui_hints.json") or {}
    for hint in hints.get("Hints", []) if isinstance(hints, dict) else []:
        if isinstance(hint, dict):
            add(f"Hint.{hint.get('HintId', '')}", hint.get("Text"), f"ui_hints.json: first-time hint ({hint.get('Trigger', '')})")

    # The Track A broadcast overlays' words (Epic 106): their styles' labels and names.
    broadcast = _load("broadcast_overlay.json") or {}
    if isinstance(broadcast, dict):
        add("Broadcast.HomeLabel", broadcast.get("HomeLabel"), "broadcast_overlay.json: the score bug's home side when its team isn't known")
        add("Broadcast.AwayLabel", broadcast.get("AwayLabel"), "broadcast_overlay.json: the score bug's away side when its team isn't known")

    ball_flight = _load("ball_flight_overlay.json") or {}
    if isinstance(ball_flight, dict):
        for field in ("GoodLabel", "WideLeftLabel", "WideRightLabel", "ShortLabel"):
            add(f"BallFlight.{field}", ball_flight.get(field), "ball_flight_overlay.json: the kick readout over the uprights")

    badges = _load("overlay_badges.json") or {}
    for row in badges.get("RoleLabels", []) if isinstance(badges, dict) else []:
        if isinstance(row, dict):
            add(f"Badge.Role.{row.get('Role', '')}", row.get("Label"), "overlay_badges.json: the position badge of a player without a pass button")

    panel = _load("personnel_panel.json") or {}
    if isinstance(panel, dict):
        for field in ("OffenseRoles", "DefenseRoles"):
            for row in panel.get(field, []) or []:
                if isinstance(row, dict):
                    add(f"Personnel.Role.{row.get('Role', '')}", row.get("Label"), "personnel_panel.json: a position on the personnel panel")
        add("Personnel.OffenseNameFormat", panel.get("OffenseNameFormat"),
            "personnel_panel.json: an offensive package's name; each {Label} is a position's count (keep them)")
        for row in panel.get("DefenseNames", []) or []:
            if isinstance(row, dict):
                add(f"Personnel.DefenseName.{row.get('DefensiveBacks', '')}", row.get("Name"),
                    f"personnel_panel.json: a defensive package with {row.get('DefensiveBacks', '')} defensive backs")
        add("Personnel.DefenseNameFallback", panel.get("DefenseNameFallback"),
            "personnel_panel.json: any other defensive package; each {Label} is a position's count (keep them)")

    packages = _load("personnel_packages.json") or {}
    for package in packages.get("Packages", []) if isinstance(packages, dict) else []:
        if isinstance(package, dict):
            add(f"Personnel.Package.{package.get('PackageId', '')}", package.get("DisplayName"), "personnel_packages.json: a personnel package's name")
    return rows


def _escape(source):
    # The string table import un-escapes \n; a real newline is written that way.
    return source.replace("\n", "\\n")


def render(rows):
    out = io.StringIO()
    writer = csv.writer(out, quoting=csv.QUOTE_ALL, lineterminator="\n")
    writer.writerow(["Key", "SourceString", "Comment"])
    for key, source, comment in rows:
        writer.writerow([key, _escape(source), comment])
    return out.getvalue()


def read_table(path):
    """{key: source} of a string table CSV, plus its problems."""
    problems = []
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        return {}, [f"can't be read: {exc}"]
    reader = csv.reader(io.StringIO(text))
    header = next(reader, [])
    if "Key" not in header or "SourceString" not in header:
        return {}, ["the first row must name the Key and SourceString columns"]
    key_col, source_col = header.index("Key"), header.index("SourceString")
    table = {}
    for number, row in enumerate(reader, start=2):
        if not row:
            continue
        if len(row) <= max(key_col, source_col):
            problems.append(f"row {number}: needs a Key and a SourceString")
            continue
        key, source = row[key_col], row[source_col]
        if not key.strip():
            problems.append(f"row {number}: empty Key")
        elif key in table:
            problems.append(f"row {number}: '{key}' is used twice")
        elif not source:
            problems.append(f"row {number}: '{key}' has no SourceString")
        else:
            table[key] = source
    return table, problems


def _string_problems(key, source):
    found = []
    if "\\" in source.replace("\\n", ""):
        found.append(f"'{key}': no backslashes (the string table import reads them as escapes)")
    depth = 0
    for char in source:
        depth += 1 if char == "{" else -1 if char == "}" else 0
        if depth < 0 or depth > 1:
            break
    if depth != 0:
        found.append(f"'{key}': unbalanced {{placeholder}} braces")
    return found


def remappable_actions():
    """[(ActionId, [ContextId])] of the input catalog's remappable actions (UPSInputConfig::IsRemappable)."""
    catalog = _load("input_actions.json")
    if not isinstance(catalog, dict):
        return []
    fixed = {c.get("ContextId") for c in catalog.get("Contexts", []) if isinstance(c, dict) and c.get("bRemappable") is False}
    result = []
    for action in catalog.get("Actions", []):
        if not isinstance(action, dict) or action.get("ValueType") != "Boolean":
            continue
        contexts = action.get("Contexts") or []
        if not any(context in fixed for context in contexts):
            result.append((action.get("ActionId", ""), contexts))
    return result


def problems():
    """[(path, message)] for the two tables and the UI code."""
    found = []
    ui_table, ui_problems = read_table(UI_TEXT)
    found += [(UI_TEXT, p) for p in ui_problems]
    for key, source in ui_table.items():
        found += [(UI_TEXT, p) for p in _string_problems(key, source)]

    rows = data_rows()
    seen = {}
    for key, source, _ in rows:
        if key in seen and seen[key] != source:
            found.append((UI_TEXT_DATA, f"'{key}' would hold two different strings: give the screen, option, setting or tip a unique ID"))
        seen[key] = source
        found += [(UI_TEXT_DATA, p) for p in _string_problems(key, source)]
    try:
        current = UI_TEXT_DATA.read_text(encoding="utf-8").replace("\r\n", "\n")
    except (OSError, UnicodeDecodeError):
        current = None
    if current != render(rows):
        found.append((UI_TEXT_DATA, "out of date with the UI data files - run: python tools/ui_text.py --write"))

    for action_id, contexts in remappable_actions():
        if f"Input.Action.{action_id}" not in ui_table:
            found.append((UI_TEXT, f"no 'Input.Action.{action_id}' row: the key remapping screen names every remappable action"))
        for context in contexts:
            if f"Input.Context.{context}" not in ui_table:
                found.append((UI_TEXT, f"no 'Input.Context.{context}' row: the key remapping screen says where {action_id} works"))

    for path in sorted(SOURCE_DIR.rglob("*")):
        if path.suffix not in (".h", ".cpp") or "Intermediate" in path.parts:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for key in KEY_USE.findall(text):
            if key not in ui_table:
                found.append((path, f"names text '{key}', which is not in Data/ui_text.csv"))
        if "Tests" not in path.parts and path.name.startswith(GATED_PREFIXES):
            for number, line in enumerate(text.splitlines(), start=1):
                if RAW_TEXT.search(line):
                    found.append((path, f"line {number}: FText from a raw string - user-facing text comes through "
                                        "UPSLocalization (Data/ui_text.csv), or UPSLocalization::Verbatim for names"))
    return found


def main(argv):
    if "--write" in argv:
        UI_TEXT_DATA.write_text(render(data_rows()), encoding="utf-8", newline="\n")
        print(f"ui_text: wrote {UI_TEXT_DATA.relative_to(REPO)}")
    found = problems()
    for path, message in found:
        print(f"  {path.relative_to(REPO)}: {message}")
    if found:
        print(f"ui_text: {len(found)} problem(s)")
        return 1
    print("ui_text: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
