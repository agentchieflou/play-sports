"""Epic 90: the practice week's data contract (no Unreal)."""

import copy
import json
import unittest

from tools import validate_data


class TrainingContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()
        self.path = validate_data.DATA_DIR / "training.json"
        self.tuning = json.loads(self.path.read_text(encoding="utf-8"))
        self.tracked = validate_data.load_opponent_model_tracked()

    def tearDown(self):
        validate_data.errors.clear()

    def test_shipped_tuning_is_clean(self):
        self.assertIsNotNone(self.tracked)
        validate_data.validate_training(self.path, self.tuning, self.tracked)
        self.assertEqual(validate_data.errors, [])

    def test_mistakes_are_reported(self):
        broken = copy.deepcopy(self.tuning)
        broken["GameFatigue"] = 1.5
        broken["MaxGameplanBonus"] = 1.0
        broken["AIFocusAreas"] = 3
        broken["DefaultAllocation"] = {"Develop": 0, "Gameplan": 0, "Rest": 0}
        broken["PracticeInjury"]["MaxRecoveryWeeks"] = 0
        broken["FocusAreas"][0]["Categories"] = ["Blitz"]       # a defensive call, on the offense's side
        broken["FocusAreas"][1]["Roles"] = ["Kicker"]
        broken["FocusAreas"][2]["Ratings"] = {"Speed": 0}
        broken["FocusAreas"][3]["FocusId"] = broken["FocusAreas"][4]["FocusId"]
        validate_data.validate_training(self.path, broken, self.tracked)
        joined = "\n".join(validate_data.errors)
        for expected in ("GameFatigue", "MaxGameplanBonus", "AIFocusAreas", "DefaultAllocation", "PracticeInjury",
                         "FocusAreas[0].Categories: 'Blitz'", "FocusAreas[1].Roles", "FocusAreas[2].Ratings",
                         "FocusAreas[4].FocusId"):
            self.assertIn(expected, joined)

    def test_untracked_category_is_reported(self):
        tracked = {True: {"Run"}, False: {"Base", "Blitz", "Prevent"}}
        validate_data.validate_training(self.path, self.tuning, tracked)
        self.assertIn("doesn't track 'ShortPass'", "\n".join(validate_data.errors))


if __name__ == "__main__":
    unittest.main()
