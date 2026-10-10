#!/usr/bin/env python
"""Content validation and import CLI for play-sports (Epic 125).

One command for everything Track L's generators produce: players and rosters, teams, the
league config, the playbook and the route library. Venues have no content type yet (Epic 124).

  validate          Every data contract and every cross-file reference: validate_data.py,
                    which runs tools/content_contracts.py. Exit 1 on any error. This is
                    what CI's "Validate data contracts" step runs.
  report [--json]   Statistical sanity of the league: rating distributions per role, name
                    duplication, roster shape (every personnel package fielded from each
                    team's own roster), body plausibility and playbook coverage.
                    It warns and does not fail unless --strict is given. CI prints it.
  import            validate, then the PSContentReimport commandlet through Unreal (needs
                    UE_ROOT). The commandlet loads every file through UPSDataIngestion and
                    UPSPlaybookIngestion, the path the game uses.
  check             validate, then report (the default).

validate, report and check take --root DIR to work on DIR/Data instead of the repo's: content
laid out like the repo, such as what the generators' automation tests write. CI runs
"check --root ... --strict" on Saved/GeneratedLeague (Epic 122) and Saved/GeneratedPlaybooks
(Epic 121).

Run from the repo root:  python tools/content.py [command]
"""

import argparse
import json
import os
import statistics
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

try:
    from tools import content_contracts, validate_data  # imported as part of the tools package (tests)
except ImportError:
    import content_contracts  # run as a script from tools/
    import validate_data

REPO = Path(__file__).resolve().parent.parent

# The clock plays are called by the situation, not chosen for yardage: no coverage warning.
CLOCK_CATEGORIES = {"Spike", "Kneel"}

# Report heuristics for generated content. They describe a plausible league, not a rule of
# the game, so they warn rather than fail.
INFLATED_MEAN = 90.0        # a role whose mean rating is this high: "a league of 99s"
FLAT_STDEV = 2.0            # ratings this uniform across a role: the generator collapsed
MIN_SAMPLE = 5              # distributions need this many players to judge
TEAM_OUTLIER_POINTS = 15.0  # a team this far from the league's mean overall rating
WEIGHT_RANGE_KG = (60.0, 200.0)
HEIGHT_RANGE_CM = (150.0, 220.0)


def load_league(repo):
    """The league as the game reads it: {"league": payload or None, "teams": [team dicts with a
    "Players" list attached], "plays": [...], "routes": [...], "problems": [...]}. Follows the
    league config's TeamsDataTablePath and each team's RosterDataTablePath."""
    repo = Path(repo)
    data = repo / "Data"
    parsed = {}
    for path in sorted(data.rglob("*.json")):
        try:
            parsed[path] = json.loads(path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
    league = next((p for p in parsed.values() if content_contracts.is_league_config(p)), None)
    problems = []
    teams_payload = None
    if league is not None:
        target = content_contracts.resolve(repo, league.get("TeamsDataTablePath"))
        teams_payload = parsed.get(target) if target else None
    if teams_payload is None:
        teams_payload = next((p for p in parsed.values() if isinstance(p, dict) and isinstance(p.get("Teams"), list)), None)
    teams = []
    for team in (teams_payload or {}).get("Teams", []):
        if not isinstance(team, dict):
            continue
        target = content_contracts.resolve(repo, team.get("RosterDataTablePath"))
        roster = parsed.get(target) if target else None
        players = roster.get("Players") if isinstance(roster, dict) else None
        if not isinstance(players, list):
            problems.append(f"team '{team.get('TeamId')}': roster '{team.get('RosterDataTablePath')}' did not load")
            players = []
        teams.append(dict(team, Players=[p for p in players if isinstance(p, dict)]))
    plays, routes, packages = [], [], []
    for payload in parsed.values():
        if isinstance(payload, dict) and isinstance(payload.get("Plays"), list):
            plays.extend(p for p in payload["Plays"] if isinstance(p, dict))
        if isinstance(payload, dict) and isinstance(payload.get("Routes"), list):
            routes.extend(r for r in payload["Routes"] if isinstance(r, dict))
        if isinstance(payload, dict) and "DefaultOffensePackage" in payload and isinstance(payload.get("Packages"), list):
            packages.extend(p for p in payload["Packages"] if isinstance(p, dict))
    return {"league": league, "teams": teams, "plays": plays, "routes": routes, "packages": packages,
            "problems": problems}


def describe(values):
    values = [v for v in values if content_contracts.is_number(v)]
    if not values:
        return None
    return {
        "n": len(values),
        "mean": round(statistics.fmean(values), 1),
        "stdev": round(statistics.pstdev(values), 1),
        "min": min(values),
        "max": max(values),
    }


def overall(player):
    ratings = [player.get(f) for f in content_contracts.RATING_FIELDS if content_contracts.is_number(player.get(f))]
    return statistics.fmean(ratings) if ratings else None


def build_report(repo):
    """The league's sanity report: {"summary", "ratings", "teams", "playbook", "warnings"}."""
    league = load_league(repo)
    warnings = list(league["problems"])
    teams = league["teams"]
    players = [p for team in teams for p in team["Players"]]

    by_role = defaultdict(list)
    for player in players:
        by_role[player.get("Role")].append(player)
    ratings = {}
    for role in sorted(r for r in by_role if isinstance(r, str)):
        ratings[role] = {}
        inflated, flat = [], []
        for field in content_contracts.RATING_FIELDS + content_contracts.BODY_FIELDS:
            stats = describe([p.get(field) for p in by_role[role]])
            if stats is None:
                continue
            ratings[role][field] = stats
            if field in content_contracts.RATING_FIELDS and stats["n"] >= MIN_SAMPLE:
                if stats["mean"] >= INFLATED_MEAN:
                    inflated.append(f"{field} {stats['mean']}")
                if stats["stdev"] < FLAT_STDEV:
                    flat.append(f"{field} {stats['stdev']}")
        count = len(by_role[role])
        if inflated:
            warnings.append(f"{role} ({count} players): ratings look inflated, mean {INFLATED_MEAN:g} or more - {', '.join(inflated)}")
        if flat:
            warnings.append(f"{role} ({count} players): ratings barely vary, spread under {FLAT_STDEV:g} - {', '.join(flat)}")

    for player in players:
        who = f"{player.get('PlayerId')} ({player.get('DisplayName')})"
        weight, height = player.get("WeightKg"), player.get("HeightCm")
        if content_contracts.is_number(weight) and not WEIGHT_RANGE_KG[0] <= weight <= WEIGHT_RANGE_KG[1]:
            warnings.append(f"{who}: {weight} kg is outside {WEIGHT_RANGE_KG[0]:g}-{WEIGHT_RANGE_KG[1]:g}")
        if content_contracts.is_number(height) and not HEIGHT_RANGE_CM[0] <= height <= HEIGHT_RANGE_CM[1]:
            warnings.append(f"{who}: {height} cm is outside {HEIGHT_RANGE_CM[0]:g}-{HEIGHT_RANGE_CM[1]:g}")

    names = Counter(str(p.get("DisplayName", "")).strip() for p in players)
    for name, count in sorted(names.items()):
        if not name:
            warnings.append(f"{count} player(s) have no DisplayName")
        elif count > 1:
            warnings.append(f"name '{name}' is used by {count} players")

    all_roles = content_contracts.OFFENSE_ROLES | content_contracts.DEFENSE_ROLES
    team_rows = []
    overalls = [o for o in (overall(p) for p in players) if o is not None]
    league_mean = statistics.fmean(overalls) if overalls else None
    for team in teams:
        roles = Counter(p.get("Role") for p in team["Players"])
        team_overall = [o for o in (overall(p) for p in team["Players"]) if o is not None]
        mean = round(statistics.fmean(team_overall), 1) if team_overall else None
        team_rows.append({"TeamId": team.get("TeamId"), "Players": len(team["Players"]), "Overall": mean,
                          "Roles": dict(sorted(roles.items(), key=lambda item: str(item[0])))})
        missing = sorted(all_roles - set(roles))
        if missing:
            warnings.append(f"team '{team.get('TeamId')}' has no {', '.join(missing)} - plays' slots for them go unfilled")
        # A live game fields a personnel package from the team's own roster (UPSPersonnelManager).
        short = []
        for package in league["packages"]:
            counts = package.get("RoleCounts") if isinstance(package.get("RoleCounts"), dict) else {}
            gaps = [f"{need} {role}, has {roles[role]}" for role, need in sorted(counts.items())
                    if content_contracts.is_number(need) and roles[role] < need]
            if gaps:
                short.append(f"{package.get('PackageId')} ({'; '.join(gaps)})")
        if short:
            warnings.append(f"team '{team.get('TeamId')}' can't field personnel package(s) {', '.join(short)} - "
                            "the field plays short")
        if mean is not None and league_mean is not None and abs(mean - league_mean) > TEAM_OUTLIER_POINTS:
            warnings.append(f"team '{team.get('TeamId')}' overall {mean} is far from the league's {league_mean:.1f}")

    plays = league["plays"]
    by_category = Counter((bool(p.get("bIsOffensivePlay")), p.get("PlayCategory")) for p in plays)
    for offense, categories in ((True, content_contracts.OFFENSE_CATEGORIES), (False, content_contracts.DEFENSE_CATEGORIES)):
        # The clock and special-teams plays are called only when due, not weighted.
        for category in sorted(categories - CLOCK_CATEGORIES - content_contracts.OFFENSE_SPECIAL_TEAMS - content_contracts.DEFENSE_SPECIAL_TEAMS):
            if plays and by_category[(offense, category)] == 0:
                side = "offensive" if offense else "defensive"
                warnings.append(f"no {side} {category} play - the coaching AI has nothing to call there")
    used_routes = {a.get("RouteId") for p in plays for a in p.get("Assignments", []) if isinstance(a, dict)}
    # An option route's branches are run through it.
    used_routes |= {r.get(branch) for r in league["routes"] if r.get("RouteId") in used_routes for branch in ("VsManBranch", "VsZoneBranch")}
    unused = sorted(r.get("RouteId") for r in league["routes"] if r.get("RouteId") not in used_routes)

    summary = {
        "league": (league["league"] or {}).get("LeagueName"),
        "teams": len(teams),
        "players": len(players),
        "plays": len(plays),
        "routes": len(league["routes"]),
    }
    playbook = {
        "offense": {c: n for (o, c), n in sorted(by_category.items(), key=str) if o},
        "defense": {c: n for (o, c), n in sorted(by_category.items(), key=str) if not o},
        "unused_routes": unused,
    }
    return {"summary": summary, "ratings": ratings, "teams": team_rows, "playbook": playbook, "warnings": warnings}


def print_report(report):
    s = report["summary"]
    print(f"content report: {s['league'] or '(no league config)'} - {s['teams']} teams, {s['players']} players, "
          f"{s['plays']} plays, {s['routes']} routes")
    print("")
    print("Teams")
    for team in report["teams"]:
        roles = ", ".join(f"{role} {n}" for role, n in team["Roles"].items())
        print(f"  {team['TeamId']:<12} {team['Players']:>3} players, overall {team['Overall']}: {roles}")
    print("")
    print("Ratings by role (mean / spread / min-max)")
    for role, fields in report["ratings"].items():
        cells = []
        for field, stats in fields.items():
            cells.append(f"{field} {stats['mean']}/{stats['stdev']}/{stats['min']:g}-{stats['max']:g}")
        print(f"  {role} (n={next(iter(fields.values()))['n'] if fields else 0}): " + "; ".join(cells))
    print("")
    pb = report["playbook"]
    print(f"Playbook: offense {pb['offense']}, defense {pb['defense']}")
    if pb["unused_routes"]:
        print(f"  routes no play runs: {', '.join(pb['unused_routes'])}")
    print("")
    if report["warnings"]:
        print(f"{len(report['warnings'])} warning(s):")
        for warning in report["warnings"]:
            print("  " + warning)
    else:
        print("No warnings.")


def cmd_validate(root=None):
    validate_data.errors.clear()  # main() collects into a module list; start each run empty
    return validate_data.main(root)


def cmd_report(as_json, strict, root=None):
    report = build_report(Path(root).resolve() if root else REPO)
    if as_json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print_report(report)
    return 1 if strict and report["warnings"] else 0


def unreal_editor_cmd(ue_root):
    binaries = Path(ue_root) / "Engine" / "Binaries"
    if sys.platform.startswith("win"):
        return binaries / "Win64" / "UnrealEditor-Cmd.exe"
    if sys.platform == "darwin":
        return binaries / "Mac" / "UnrealEditor-Cmd"
    return binaries / "Linux" / "UnrealEditor-Cmd"


def cmd_import(runner=subprocess.call):
    if cmd_validate() != 0:
        print("content import: fix the validation errors first")
        return 1
    ue_root = os.environ.get("UE_ROOT")
    if not ue_root:
        print("content import: UE_ROOT is not set. The import runs the PSContentReimport commandlet, "
              "which needs Unreal Engine; the automation test PlaySports.Content.ImportShippedContent "
              "runs the same import in CI.")
        return 2
    command = [str(unreal_editor_cmd(ue_root)), str(REPO / "play-sports.uproject"), "-run=PSContentReimport",
               "-unattended", "-nullrhi", "-nosplash", "-nop4", "-stdout"]
    print("content import: " + " ".join(command))
    return runner(command)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command")
    root_help = "work on this directory's Data/ instead of the repo's"
    validate = sub.add_parser("validate", help="every data contract and cross-file reference")
    validate.add_argument("--root", help=root_help)
    report = sub.add_parser("report", help="statistical sanity of the league")
    report.add_argument("--json", action="store_true", help="machine-readable output")
    report.add_argument("--strict", action="store_true", help="exit 1 when there are warnings")
    report.add_argument("--root", help=root_help)
    sub.add_parser("import", help="validate, then the PSContentReimport commandlet (needs UE_ROOT)")
    check = sub.add_parser("check", help="validate, then report (the default)")
    check.add_argument("--strict", action="store_true", help="exit 1 when the report warns")
    check.add_argument("--root", help=root_help)
    args = parser.parse_args(argv)
    root = getattr(args, "root", None)

    if args.command == "validate":
        return cmd_validate(root)
    if args.command == "report":
        return cmd_report(args.json, args.strict, root)
    if args.command == "import":
        return cmd_import()
    status = cmd_validate(root)
    print("")
    return max(status, cmd_report(False, getattr(args, "strict", False), root))


if __name__ == "__main__":
    sys.exit(main())
