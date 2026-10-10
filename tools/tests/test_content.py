"""Epic 125: content contracts, cross-content references and the sanity report (no Unreal)."""

import copy
import json
import tempfile
import unittest
from pathlib import Path

from tools import content, content_contracts


def player(pid, role, name=None, rating=80, **overrides):
    row = {"PlayerId": pid, "DisplayName": name or pid, "Role": role, "WeightKg": 100, "HeightCm": 190}
    row.update({field: rating for field in content_contracts.RATING_FIELDS})
    row.update(overrides)
    return row


LEAGUE = {"LeagueName": "Test League", "NumWeeks": 6, "ByeWeekNumbers": [3], "NumPlayoffTeams": 2,
          "TeamsDataTablePath": "Data/teams.json"}
TEAMS = {"Teams": [
    {"TeamId": "Alpha", "DisplayName": "Alpha", "Division": "East", "RosterDataTablePath": "Data/rosters/alpha.json"},
    {"TeamId": "Beta", "DisplayName": "Beta", "Division": "West", "RosterDataTablePath": "Data/rosters/beta.json"},
]}
ALPHA = {"Players": [player("ALP_QB", "Quarterback"), player("ALP_WR", "WideReceiver")]}
BETA = {"Players": [player("BET_QB", "Quarterback"), player("BET_DB", "DefensiveBack")]}
ROUTES = {"Routes": [{"RouteId": "Go", "Waypoints": [
    {"Offset": {"X": 600, "Y": 0, "Z": 0}, "TimingSeconds": 1.0},
    {"Offset": {"X": 1500, "Y": 0, "Z": 0}, "TimingSeconds": 2.5}]}]}
PLAYBOOK = {"Plays": [
    {"PlayId": "Offense_Go", "DisplayName": "Go", "Formation": "Spread", "bIsOffensivePlay": True, "PlayCategory": "DeepPass",
     "Assignments": [{"Role": "Quarterback", "Kind": "Route", "FormationOffset": {"X": -200, "Y": 0, "Z": 0}},
                     {"Role": "WideReceiver", "Kind": "Route", "RouteId": "Go"}]},
    {"PlayId": "Defense_Base", "DisplayName": "Base", "Formation": "4-3", "bIsOffensivePlay": False, "PlayCategory": "Base",
     "Front": "4-3", "CoverageShell": "Cover2", "Assignments": [{"Role": "DefensiveBack", "Kind": "ZoneCoverage"}]},
]}


def check(files):
    """Errors from every content contract and reference over files {relative path: payload}."""
    errors = []
    with tempfile.TemporaryDirectory() as tmp:
        repo = Path(tmp)
        parsed = {}
        for relative, payload in files.items():
            path = repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(payload), encoding="utf-8")
            parsed[path] = payload
        sink = lambda path, message: errors.append(f"{Path(path).relative_to(repo).as_posix()}: {message}")
        for path, payload in parsed.items():
            content_contracts.check_file(path, payload, sink)
        content_contracts.check_references(repo, parsed, sink)
    return errors


def league(**changes):
    files = {"Data/league.json": LEAGUE, "Data/teams.json": TEAMS, "Data/rosters/alpha.json": ALPHA,
             "Data/rosters/beta.json": BETA, "Data/routes.json": ROUTES, "Data/playbook.json": PLAYBOOK}
    files = copy.deepcopy(files)
    files.update(changes)
    return {k: v for k, v in files.items() if v is not None}


class ContractTests(unittest.TestCase):
    def test_a_sound_league_is_clean(self):
        self.assertEqual(check(league()), [])

    def test_shipped_data_is_clean(self):
        errors = []
        repo = content.REPO
        parsed = {p: json.loads(p.read_text(encoding="utf-8")) for p in sorted((repo / "Data").rglob("*.json"))}
        for path, payload in parsed.items():
            content_contracts.check_file(path, payload, lambda p, m: errors.append(f"{p}: {m}"))
        content_contracts.check_references(repo, parsed, lambda p, m: errors.append(f"{p}: {m}"))
        self.assertEqual(errors, [])

    def test_player_ranges(self):
        roster = {"Players": [player("ALP_QB", "Quarterback", Speed=104), player("ALP_WR", "WideReceiver", WeightKg=0)]}
        errors = check(league(**{"Data/rosters/alpha.json": roster}))
        self.assertTrue(any("Speed: 104 is outside the 0-100" in e for e in errors), errors)
        self.assertTrue(any("WeightKg: 0 must be above 0" in e for e in errors), errors)

    def test_teams_contract(self):
        teams = copy.deepcopy(TEAMS)
        teams["Teams"][1]["TeamId"] = "Alpha"
        teams["Teams"][1]["Mascot"] = "Owl"
        del teams["Teams"][0]["Division"]
        errors = check(league(**{"Data/teams.json": teams}))
        self.assertTrue(any("TeamId: 'Alpha' is used by another team" in e for e in errors), errors)
        self.assertTrue(any("unknown field(s) ['Mascot']" in e for e in errors), errors)
        self.assertTrue(any("missing field 'Division'" in e for e in errors), errors)

    def test_league_contract(self):
        bad = dict(LEAGUE, NumWeeks=4, ByeWeekNumbers=[2, 2, 9], NumPlayoffTeams=1)
        errors = check(league(**{"Data/league.json": bad}))
        self.assertTrue(any("a week is listed twice" in e for e in errors), errors)
        self.assertTrue(any("week 9 is outside the season (1-4)" in e for e in errors), errors)
        self.assertTrue(any("NumPlayoffTeams: 1 must be 2 or more" in e for e in errors), errors)

    def test_playbook_sides(self):
        plays = copy.deepcopy(PLAYBOOK)
        plays["Plays"][0]["Assignments"].append({"Role": "Linebacker", "Kind": "Blitz"})
        plays["Plays"][0]["Assignments"].append({"Role": "TightEnd", "Kind": "PassBlock", "RouteId": "Go"})
        plays["Plays"][1]["PlayCategory"] = "DeepPass"
        plays["Plays"].append(copy.deepcopy(plays["Plays"][0]))
        errors = check(league(**{"Data/playbook.json": plays}))
        self.assertTrue(any("Role: 'Linebacker' is not an offensive role" in e for e in errors), errors)
        self.assertTrue(any("Kind: 'Blitz' is not an offensive assignment" in e for e in errors), errors)
        self.assertTrue(any("only a Route assignment runs one" in e for e in errors), errors)
        self.assertTrue(any("'DeepPass' is not a defensive category" in e for e in errors), errors)
        self.assertTrue(any("PlayId: duplicate" in e for e in errors), errors)

    def test_playbook_deception(self):
        plays = copy.deepcopy(PLAYBOOK)
        plays["Plays"][0]["Deception"] = {"Type": "PlayAction"}
        self.assertEqual(check(league(**{"Data/playbook.json": plays})), [])
        # A run option on a pass play without a back at the mesh; a pitch to the QB; a bad side.
        plays["Plays"][0]["Deception"] = {"Type": "TripleOption", "PlaySide": 2, "PitchRole": "Quarterback"}
        plays["Plays"][1]["Deception"] = {"Type": "PlayAction"}
        plays["Plays"].append({"PlayId": "Offense_RPO", "DisplayName": "RPO", "Formation": "Shotgun", "bIsOffensivePlay": True,
                               "PlayCategory": "Run", "Deception": {"Type": "RPO", "PassRole": "TightEnd"},
                               "Assignments": [{"Role": "RunningBack", "Kind": "Route"}, {"Role": "TightEnd", "Kind": "RunBlock"}]})
        plays["Plays"].append({"PlayId": "Offense_Screen", "DisplayName": "Screen", "Formation": "Shotgun", "bIsOffensivePlay": True,
                               "PlayCategory": "Screen", "Deception": {"Type": "Bootleg"}, "Assignments": []})
        errors = check(league(**{"Data/playbook.json": plays}))
        self.assertTrue(any("TripleOption is a run option, so the play is a Run" in e for e in errors), errors)
        self.assertTrue(any("a run option needs a RunningBack on a Route" in e for e in errors), errors)
        self.assertTrue(any("PlaySide: 2 must be 1 (right) or -1 (left)" in e for e in errors), errors)
        self.assertTrue(any("the pitch man is not the Quarterback" in e for e in errors), errors)
        self.assertTrue(any("'Defense_Base'.Deception: only offensive plays deceive" in e for e in errors), errors)
        self.assertTrue(any("no TightEnd runs a route to be the pass option" in e for e in errors), errors)
        self.assertTrue(any("'Bootleg' is not an EPSDeception" in e for e in errors), errors)

    def test_read_order(self):
        # Epic 27: the play art's primary read and check-downs.
        plays = copy.deepcopy(PLAYBOOK)
        plays["Plays"][0]["Assignments"][1]["ReadOrder"] = 1
        self.assertEqual(check(league(**{"Data/playbook.json": plays})), [])

        plays["Plays"][0]["Assignments"][0]["ReadOrder"] = 2
        plays["Plays"][0]["Assignments"].append({"Role": "TightEnd", "Kind": "Route", "RouteId": "Go", "ReadOrder": 3})
        plays["Plays"][0]["Assignments"].append({"Role": "RunningBack", "Kind": "Route", "RouteId": "Go", "ReadOrder": 0})
        plays["Plays"][1]["Assignments"][0]["ReadOrder"] = "1"
        errors = check(league(**{"Data/playbook.json": plays}))
        self.assertTrue(any("ReadOrder: only a route with a RouteId is read" in e for e in errors), errors)
        self.assertTrue(any("ReadOrder: 0 - 1 is the primary read" in e for e in errors), errors)
        self.assertTrue(any("ReadOrder values [1, 3] must run 1, 2, 3" in e for e in errors), errors)
        self.assertTrue(any("ReadOrder: expected int" in e for e in errors), errors)


class ArtAnnotationTests(unittest.TestCase):
    def test_art_annotation(self):
        # Epic 35: the play art's annotation layer.
        plays = copy.deepcopy(PLAYBOOK)
        plays["Plays"][0]["Assignments"][1]["Art"] = {"Color": "#3FB950", "bEmphasis": True, "BadgeLetter": "X"}
        plays["Plays"][1]["Assignments"][0]["Art"] = {"BadgeLetter": "M"}
        self.assertEqual(check(league(**{"Data/playbook.json": plays})), [])

        plays["Plays"][0]["Assignments"][1]["Art"] = {"Color": "green", "bEmphasis": "yes", "BadgeLetter": "xyz", "Glow": 2}
        errors = check(league(**{"Data/playbook.json": plays}))
        self.assertTrue(any("Art.Color: 'green' must be #RRGGBB" in e for e in errors), errors)
        self.assertTrue(any("Art.BadgeLetter: 'xyz' must be one or two capitals" in e for e in errors), errors)
        self.assertTrue(any("Art.bEmphasis: expected bool" in e for e in errors), errors)
        self.assertTrue(any("unknown field(s) ['Glow']" in e for e in errors), errors)


class ReferenceTests(unittest.TestCase):
    def test_missing_roster_and_orphan_roster(self):
        teams = copy.deepcopy(TEAMS)
        teams["Teams"][1]["RosterDataTablePath"] = "Data/rosters/gamma.json"
        errors = check(league(**{"Data/teams.json": teams}))
        self.assertIn("Data/teams.json: Teams[1] 'Beta'.RosterDataTablePath: 'Data/rosters/gamma.json' does not exist", errors)
        self.assertIn("Data/rosters/beta.json: roster belongs to no team - add a team whose RosterDataTablePath names it, or delete it", errors)

    def test_roster_path_must_stay_in_the_project(self):
        teams = copy.deepcopy(TEAMS)
        teams["Teams"][0]["RosterDataTablePath"] = "../outside.json"
        errors = check(league(**{"Data/teams.json": teams}))
        self.assertTrue(any("must be a path inside the project" in e for e in errors), errors)

    def test_player_ids_unique_across_the_league(self):
        beta = {"Players": [player("BET_QB", "Quarterback"), player("ALP_WR", "WideReceiver")]}
        errors = check(league(**{"Data/rosters/beta.json": beta}))
        self.assertIn("Data/teams.json: PlayerId 'ALP_WR' is on both 'Alpha' and 'Beta' - ids must be unique across the league", errors)

    def test_routes_resolve(self):
        plays = copy.deepcopy(PLAYBOOK)
        plays["Plays"][0]["Assignments"][1]["RouteId"] = "Wheel"
        errors = check(league(**{"Data/playbook.json": plays}))
        self.assertIn("Data/playbook.json: Plays[0] 'Offense_Go'.Assignments[1].RouteId: 'Wheel' is in no route library", errors)

    def test_league_names_a_teams_file_big_enough(self):
        errors = check(league(**{"Data/league.json": dict(LEAGUE, NumPlayoffTeams=4)}))
        self.assertIn("Data/league.json: NumPlayoffTeams: 4 is more than the league's 2 teams", errors)
        errors = check(league(**{"Data/league.json": dict(LEAGUE, TeamsDataTablePath="Data/routes.json")}))
        self.assertTrue(any("is not a teams file" in e for e in errors), errors)


class ReportTests(unittest.TestCase):
    def build(self, files):
        with tempfile.TemporaryDirectory() as tmp:
            for relative, payload in files.items():
                path = Path(tmp) / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(payload), encoding="utf-8")
            return content.build_report(tmp)

    def test_summary_follows_the_league(self):
        report = self.build(league())
        self.assertEqual(report["summary"], {"league": "Test League", "teams": 2, "players": 4, "plays": 2, "routes": 1})
        self.assertEqual([t["TeamId"] for t in report["teams"]], ["Alpha", "Beta"])

    def test_inflated_flat_ratings_and_duplicate_names(self):
        stars = {"Players": [player(f"ALP_WR{i}", "WideReceiver", name="J. Smith", rating=97) for i in range(6)]}
        report = self.build(league(**{"Data/rosters/alpha.json": stars}))
        warnings = report["warnings"]
        self.assertTrue(any(w.startswith("WideReceiver (6 players): ratings look inflated") for w in warnings), warnings)
        self.assertTrue(any(w.startswith("WideReceiver (6 players): ratings barely vary") for w in warnings), warnings)
        self.assertIn("name 'J. Smith' is used by 6 players", warnings)
        self.assertEqual(report["ratings"]["WideReceiver"]["Speed"]["mean"], 97)

    def test_missing_roles_and_playbook_gaps(self):
        report = self.build(league())
        warnings = report["warnings"]
        self.assertTrue(any(w.startswith("team 'Alpha' has no ") and "DefensiveBack" in w for w in warnings), warnings)
        self.assertIn("no offensive Run play - the coaching AI has nothing to call there", warnings)

    def test_a_roster_short_of_a_personnel_package(self):
        personnel = {"DefaultOffensePackage": "Spread", "DefaultDefensePackage": "Dime", "Packages": [
            {"PackageId": "Spread", "bOffense": True, "RoleCounts": {"Quarterback": 1, "WideReceiver": 2}},
            {"PackageId": "Dime", "bOffense": False, "RoleCounts": {"DefensiveBack": 1}}]}
        warnings = self.build(league(**{"Data/personnel_packages.json": personnel}))["warnings"]
        self.assertIn("team 'Alpha' can't field personnel package(s) Spread (2 WideReceiver, has 1), "
                      "Dime (1 DefensiveBack, has 0) - the field plays short", warnings)
        self.assertIn("team 'Beta' can't field personnel package(s) Spread (2 WideReceiver, has 0) - the field plays short",
                      warnings)

    def test_every_shipped_team_fields_every_personnel_package(self):
        report = content.build_report(content.REPO)
        self.assertEqual([w for w in report["warnings"] if "personnel package" in w], [])

    def test_body_plausibility(self):
        roster = {"Players": [player("ALP_QB", "Quarterback", WeightKg=40), player("ALP_WR", "WideReceiver", HeightCm=250)]}
        warnings = self.build(league(**{"Data/rosters/alpha.json": roster}))["warnings"]
        self.assertTrue(any("40 kg is outside" in w for w in warnings), warnings)
        self.assertTrue(any("250 cm is outside" in w for w in warnings), warnings)


class CliTests(unittest.TestCase):
    def test_import_without_unreal_explains_and_does_not_run(self):
        calls = []
        saved = content.os.environ.pop("UE_ROOT", None)
        try:
            self.assertEqual(content.cmd_import(runner=calls.append), 2)
        finally:
            if saved is not None:
                content.os.environ["UE_ROOT"] = saved
        self.assertEqual(calls, [])

    def test_import_runs_the_commandlet(self):
        calls = []
        saved = content.os.environ.get("UE_ROOT")
        content.os.environ["UE_ROOT"] = "/opt/UE_5.8"
        try:
            content.cmd_import(runner=lambda command: calls.append(command) or 0)
        finally:
            if saved is None:
                del content.os.environ["UE_ROOT"]
            else:
                content.os.environ["UE_ROOT"] = saved
        self.assertEqual(len(calls), 1)
        self.assertIn("-run=PSContentReimport", calls[0])
        self.assertTrue(calls[0][1].endswith("play-sports.uproject"))


if __name__ == "__main__":
    unittest.main()
