"""Epic 122: the league generator's data contracts: its tuning, the age curve, a player's age, the
no-real-person name policy and validating a generated league under --root (no Unreal; the
generator itself is C++, tested by PlaySports.Content.LeagueGenerator.*)."""

import copy
import json
import tempfile
import unittest
from pathlib import Path

from tools import content, content_contracts, validate_data


def player(pid, role, name=None, **fields):
    row = {"PlayerId": pid, "DisplayName": name or pid, "Role": role, "WeightKg": 100, "HeightCm": 190}
    row.update({field: 75 for field in content_contracts.RATING_FIELDS})
    row.update(fields)
    return row


def shipped(name):
    return json.loads((validate_data.DATA_DIR / name).read_text(encoding="utf-8"))


class ContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()
        self.path = validate_data.DATA_DIR / "test_content.json"

    def tearDown(self):
        validate_data.errors.clear()

    def errors(self):
        found = list(validate_data.errors)
        validate_data.errors.clear()
        return found

    def test_shipped_generator_tuning_and_age_curve_are_clean(self):
        validate_data.validate_league_generator(self.path, shipped("league_generator.json"))
        validate_data.validate_progression(self.path, shipped("player_progression.json"))
        self.assertEqual(self.errors(), [])

    def test_generator_tuning_mistakes(self):
        tuning = copy.deepcopy(shipped("league_generator.json"))
        tuning["NumPlayoffTeams"] = tuning["NumTeams"] + 1
        tuning["RoleProfiles"][1]["Role"] = tuning["RoleProfiles"][0]["Role"]
        tuning["RoleProfiles"][2]["Attrition"] = 1.5
        del tuning["RoleProfiles"][3]["Attributes"][0]
        tuning["RoleProfiles"][4]["Attributes"][0]["Attribute"] = "Charisma"
        tuning["RoleProfiles"][5]["Attributes"][0]["Max"] = 120
        tuning["PlaceholderColors"].append("blue")
        tuning["NameCultures"][0]["LastNames"] = []
        tuning["NameCultures"][1]["FirstNames"].append(tuning["NameCultures"][1]["FirstNames"][0])
        tuning["Mascots"] = []
        validate_data.validate_league_generator(self.path, tuning)
        errors = self.errors()
        for expected in ("NumPlayoffTeams", "listed twice", "no profile for", "Attrition: above 0, below 1",
                         "no curve for", "'Charisma': not a float field", "a rating runs 0-100", "'blue' is not #RRGGBB",
                         "LastNames: empty", "unknown field(s) ['Mascots']"):
            self.assertTrue(any(expected in e for e in errors), (expected, errors))

    def test_age_curve_mistakes(self):
        validate_data.validate_progression(self.path, {"PeakAgeStart": 30, "PeakAgeEnd": 26, "GrowthPerYear": -1,
                                                       "DeclinePerYear": 2.0, "LowSnapShareThreshold": 2})
        errors = self.errors()
        self.assertEqual(len(errors), 3, errors)

    def test_age_is_an_optional_whole_number(self):
        validate_data.validate_players(self.path, [player("A", "Quarterback", Age=24), player("B", "Quarterback")])
        self.assertEqual(self.errors(), [])
        validate_data.validate_players(self.path, [player("C", "Quarterback", Age=24.5), player("D", "Quarterback", Age="old")])
        self.assertEqual(len(self.errors()), 2)
        sink = []
        content_contracts.validate_player_ranges(self.path, [player("E", "Quarterback", Age=0), player("F", "Quarterback", Age=12),
                                                             player("G", "Quarterback", Age=61)], lambda p, m: sink.append(m))
        self.assertEqual(len(sink), 2, sink)
        self.assertTrue(all("0 means unknown" in m for m in sink))

    def test_no_real_person_policy(self):
        forms = content_contracts.blocked_name_forms(["Josh Allen", "T.J. Watt"])
        self.assertEqual(content_contracts.normalize_name("Ja'Marr  CHASE"), "jamarr chase")
        self.assertEqual(content_contracts.normalize_name("Smith-Jones"), "smith jones")
        rows = [player("A", "Quarterback", "Josh Allen"), player("B", "Quarterback", "J. Allen"),
                player("C", "Linebacker", "TJ Watt"), player("D", "Quarterback", "Josh Allender"),
                player("E", "Quarterback", "Joshua Allen")]
        sink = []
        content_contracts.validate_name_policy(self.path, rows, forms, lambda p, m: sink.append(m))
        self.assertEqual(len(sink), 3, sink)
        self.assertTrue(all("real person's name" in m for m in sink))

    def test_shipped_blocklist_reaches_every_roster(self):
        forms = validate_data.load_name_forms()
        self.assertIn("josh allen", forms)
        self.assertIn("j allen", forms)


class GeneratedLeagueTest(unittest.TestCase):
    """validate_data and content.py --root on a league laid out as the generator writes one."""

    def write_league(self, root, names):
        data = Path(root) / "Data"
        (data / "rosters").mkdir(parents=True)
        (data / "sample_league_config.json").write_text(json.dumps(
            {"LeagueName": "Generated", "NumWeeks": 4, "ByeWeekNumbers": [], "NumPlayoffTeams": 2,
             "TeamsDataTablePath": "Data/league_teams.json"}), encoding="utf-8")
        teams = []
        for idx, team_id in enumerate(("T01", "T02")):
            teams.append({"TeamId": team_id, "DisplayName": f"League Team {idx + 1:02d}", "Division": "Atlantic",
                          "RosterDataTablePath": f"Data/rosters/team_{team_id}.json", "Abbreviation": team_id,
                          "PrimaryColor": "#1F4E79", "SecondaryColor": "#F2F2F2", "LogoPath": ""})
            roster = {"Players": [player(f"{team_id}_QB_001", "Quarterback", names[idx], Age=24, DNA={"Mobility": 0.3})]}
            (data / "rosters" / f"team_{team_id}.json").write_text(json.dumps(roster), encoding="utf-8")
        (data / "league_teams.json").write_text(json.dumps({"Teams": teams}), encoding="utf-8")

    def run_validate(self, root):
        validate_data.errors.clear()
        status = validate_data.main(root)
        found = list(validate_data.errors)
        validate_data.errors.clear()
        return status, found

    def test_a_generated_league_validates_under_its_root(self):
        with tempfile.TemporaryDirectory() as root:
            self.write_league(root, ["Marcus Okafor", "Colt Pruett"])
            self.assertEqual(self.run_validate(root), (0, []))
            self.assertEqual(content.build_report(Path(root))["summary"]["teams"], 2)

    def test_a_real_persons_name_fails_it(self):
        with tempfile.TemporaryDirectory() as root:
            self.write_league(root, ["Josh Allen", "Colt Pruett"])
            status, errors = self.run_validate(root)
            self.assertEqual(status, 1)
            self.assertTrue(any("'Josh Allen' is a real person's name" in e for e in errors), errors)


if __name__ == "__main__":
    unittest.main()
