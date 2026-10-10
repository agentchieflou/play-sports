"""Epic 84: the difficulty tiers and assist settings data contract (no Unreal)."""

import copy
import json
import unittest

from tools import validate_data


class DifficultyContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()

    def tearDown(self):
        validate_data.errors.clear()

    def load(self, name):
        path = validate_data.DATA_DIR / name
        return path, json.loads(path.read_text(encoding="utf-8"))

    def test_shipped_file_is_clean(self):
        path, payload = self.load("difficulty.json")
        validate_data.validate_difficulty(path, payload)
        self.assertEqual(validate_data.errors, [])

    def test_tiers_are_capability_dials_not_ratings(self):
        _, payload = self.load("difficulty.json")
        for tier in payload["DifficultyTiers"]:
            for scale in tier["Scales"]:
                self.assertIn(scale["Target"], validate_data.DNA_BINDING_TARGETS)
                self.assertNotIn(scale["Field"], validate_data.PLAYER_FIELDS)

    def test_tiers_get_harder_in_order(self):
        _, payload = self.load("difficulty.json")
        tiers = payload["DifficultyTiers"]
        dials = [t["AdaptationDial"] for t in tiers]
        scatter = [t["ThrowScatterScale"] for t in tiers]
        self.assertEqual(dials, sorted(dials), "each tier adapts at least as much as the one before")
        self.assertEqual(scatter, sorted(scatter, reverse=True), "each tier throws at least as true as the one before")

    def test_mistakes_are_reported(self):
        path, payload = self.load("difficulty.json")
        broken = copy.deepcopy(payload)
        tier = broken["DifficultyTiers"][0]
        tier["AdaptationDial"] = 1.5
        tier["ThrowScatterScale"] = 0
        tier["Scales"].append({"Dial": "Strength", "Target": "Attributes", "Field": "Strength", "Scale": 1.2})
        tier["Scales"].append({"Dial": "Speed", "Target": "SkillAI", "Field": "NotAField", "Scale": 1.2})
        tier["Scales"].append(dict(tier["Scales"][0]))
        broken["DifficultyTiers"][1]["TierId"] = tier["TierId"]
        broken["DifficultyTiers"][2]["Label"] = "Hall of Fame"
        broken["AutoSlideSetting"] = "Units"
        broken["SuggestedPlayAccent"] = "gold"
        validate_data.validate_difficulty(path, broken)
        joined = "\n".join(validate_data.errors)
        for expected in ("AdaptationDial", "ThrowScatterScale", "'Attributes' must be one of", "'NotAField' is not a number",
                         "is scaled twice", "an identifier used once", "the tiers' labels in order", "AutoSlideSetting",
                         "SuggestedPlayAccent"):
            self.assertIn(expected, joined)


if __name__ == "__main__":
    unittest.main()
