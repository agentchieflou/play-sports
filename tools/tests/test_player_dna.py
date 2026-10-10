"""Epic 79: the player DNA generator and the DNA data contracts (no Unreal)."""

import copy
import json
import unittest

from tools import player_dna, validate_data


def player(pid, role, **ratings):
    row = {"PlayerId": pid, "DisplayName": pid, "Role": role, "WeightKg": 100.0, "HeightCm": 190.0,
           "Speed": 80.0, "Agility": 80.0, "Strength": 80.0, "Acceleration": 80.0, "Awareness": 80.0, "Stamina": 90.0}
    row.update(ratings)
    return row


CATALOG = {"Axes": [
    {"Axis": "Mobility", "Roles": ["Quarterback"],
     "Generator": {"HighAttribute": "Speed", "LowAttribute": "Awareness", "RatingLean": 0.1, "Spread": 0.0}},
    {"Axis": "BallHawk", "Roles": ["DefensiveBack", "Linebacker"],
     "Generator": {"HighAttribute": "Awareness", "LowAttribute": "Speed", "RatingLean": 0.05, "Spread": 0.3}},
]}


class GeneratorTest(unittest.TestCase):
    def test_profile_has_only_the_roles_axes(self):
        self.assertEqual(set(player_dna.generate_profile(player("QB", "Quarterback"), CATALOG)), {"Mobility"})
        self.assertEqual(player_dna.generate_profile(player("OL", "OffensiveLineman"), CATALOG), {})

    def test_ratings_lean_the_axis_from_the_roles_average(self):
        quick = player("QB_FAST", "Quarterback", Speed=90.0, Awareness=80.0)
        slow = player("QB_SLOW", "Quarterback", Speed=70.0, Awareness=80.0)
        centers = player_dna.role_centers([quick, slow], CATALOG)
        self.assertEqual(centers[("Quarterback", "Mobility")], 0.0)
        self.assertEqual(player_dna.generate_profile(quick, CATALOG, centers)["Mobility"], 1.0)
        self.assertEqual(player_dna.generate_profile(slow, CATALOG, centers)["Mobility"], -1.0)

    def test_players_rated_alike_differ_and_each_is_stable(self):
        first = player_dna.generate_profile(player("DB_A", "DefensiveBack"), CATALOG)
        second = player_dna.generate_profile(player("DB_B", "DefensiveBack"), CATALOG)
        self.assertNotEqual(first, second)
        self.assertEqual(first, player_dna.generate_profile(player("DB_A", "DefensiveBack"), CATALOG))
        for value in list(first.values()) + list(second.values()):
            self.assertTrue(-1.0 <= value <= 1.0)

    def test_fill_keeps_authored_dna_unless_forced(self):
        authored = player("DB_A", "DefensiveBack", DNA={"BallHawk": -0.9})
        rosters = {"a": {"Players": [authored, player("DB_B", "DefensiveBack"), player("OL", "OffensiveLineman")]}}
        self.assertEqual(player_dna.fill(rosters, CATALOG), ["DB_B"])
        self.assertEqual(authored["DNA"], {"BallHawk": -0.9})
        self.assertNotIn("DNA", rosters["a"]["Players"][2])
        self.assertEqual(player_dna.fill(rosters, CATALOG, force=True), ["DB_A", "DB_B"])
        self.assertNotEqual(authored["DNA"], {"BallHawk": -0.9})

    def test_compact_layout_round_trips(self):
        payload = {"Players": [player("QB", "Quarterback", DNA={"Mobility": 0.5}), player("WR", "WideReceiver")]}
        text = player_dna.render(payload, compact=True)
        self.assertIn('\n    { "PlayerId": "QB"', text)
        self.assertIn('"DNA": { "Mobility": 0.5 } }', text)
        self.assertEqual(json.loads(text), payload)

    def test_shipped_rosters_round_trip_and_carry_dna(self):
        rosters, skipped = player_dna.load_rosters()
        self.assertEqual(skipped, [])
        catalog = player_dna.load_catalog()
        missing = [p["PlayerId"] for payload, _ in rosters.values() for p in payload["Players"]
                   if "DNA" not in p and player_dna.role_axes(catalog, p["Role"])]
        self.assertEqual(missing, [], "run: python tools/player_dna.py --write")


class ContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()
        self.path = validate_data.DATA_DIR / "test_roster.json"
        self.catalog = validate_data.load_dna_catalog()

    def tearDown(self):
        validate_data.errors.clear()

    def players_errors(self, *rows):
        validate_data.validate_players(self.path, list(rows), self.catalog)
        return list(validate_data.errors)

    def test_dna_on_the_roles_axes_is_clean(self):
        self.assertEqual(self.players_errors(player("QB", "Quarterback", DNA={"Mobility": 0.6, "Gunslinger": -1})), [])

    def test_bad_dna_is_reported(self):
        errors = self.players_errors(
            player("QB", "Quarterback", DNA={"Mobility": 1.5}),
            player("DB", "DefensiveBack", DNA={"Mobility": 0.2}),
            player("WR", "WideReceiver", DNA={"Hands": 0.2}),
            player("RB", "RunningBack", DNA=[0.2]))
        self.assertEqual(len(errors), 4, errors)
        self.assertTrue(any("DNA.Mobility: '1.5' must be a number from -1 to 1" in e for e in errors))
        self.assertTrue(any("doesn't apply to a DefensiveBack" in e for e in errors))
        self.assertTrue(any("DNA.Hands: not an axis" in e for e in errors))
        self.assertTrue(any("DNA: must be an object" in e for e in errors))

    def test_shipped_catalog_is_clean_and_its_mistakes_are_caught(self):
        validate_data.validate_player_dna_catalog(self.path, self.catalog)
        self.assertEqual(validate_data.errors, [])
        broken = copy.deepcopy(self.catalog)
        broken["Axes"][0]["Axis"] = "Clutch"
        broken["Bindings"][0]["Field"] = "NotAField"
        broken["Bindings"][1]["AtHigh"] = 0
        broken["RushMoveLeans"][0]["Move"] = "Hurdle"
        broken["RushStyleWeight"] = 1.0
        validate_data.validate_player_dna_catalog(self.path, broken)
        joined = "\n".join(validate_data.errors)
        for expected in ("'Clutch' is not an FPSPlayerDNA field", "'NotAField' is not a number in",
                         "AtHigh: '0' must be a multiplier above 0", "'Hurdle' is not a move", "RushStyleWeight"):
            self.assertIn(expected, joined)


if __name__ == "__main__":
    unittest.main()
