"""Epic 94: the league history's data contract (no Unreal)."""

import copy
import json
import unittest

from tools import validate_data


class LegacyContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()
        self.path = validate_data.DATA_DIR / "legacy.json"
        self.tuning = json.loads(self.path.read_text(encoding="utf-8"))

    def tearDown(self):
        validate_data.errors.clear()

    def test_shipped_tuning_is_clean(self):
        validate_data.validate_legacy(self.path, self.tuning)
        self.assertEqual(validate_data.errors, [])

    def test_mistakes_are_reported(self):
        broken = copy.deepcopy(self.tuning)
        broken["HallOfFame"]["MaxInducteesPerSeason"] = 0
        broken["HallOfFame"]["InductionScore"] = 0
        broken["HallOfFame"]["Thresholds"][0]["Category"] = "TeamPoints"      # a team category
        broken["HallOfFame"]["Thresholds"][1]["CareerValue"] = 0
        broken["HallOfFame"]["Thresholds"][2]["Category"] = broken["HallOfFame"]["Thresholds"][3]["Category"]
        broken["LeaderCategories"].append("PassingYards")                    # listed twice
        validate_data.validate_legacy(self.path, broken)
        joined = "\n".join(validate_data.errors)
        for expected in ("MaxInducteesPerSeason", "InductionScore", "Thresholds[0].Category: 'TeamPoints'",
                         "Thresholds[1].CareerValue", "Thresholds[3].Category", "LeaderCategories[8]"):
            self.assertIn(expected, joined)


if __name__ == "__main__":
    unittest.main()
