"""Epic 78: the opponent model's data contract (no Unreal)."""

import copy
import json
import unittest

from tools import validate_data


class OpponentModelContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()
        self.path = validate_data.DATA_DIR / "opponent_model.json"
        self.tuning = json.loads(self.path.read_text(encoding="utf-8"))

    def tearDown(self):
        validate_data.errors.clear()

    def test_shipped_tuning_is_clean(self):
        validate_data.validate_opponent_model(self.path, self.tuning)
        self.assertEqual(validate_data.errors, [])

    def test_mistakes_are_reported(self):
        broken = copy.deepcopy(self.tuning)
        broken["DistanceBuckets"] = [7, 3]
        broken["MinMultiplier"] = 0
        broken["HalftimeQuarter"] = 1
        broken["Counters"][0]["Counter"] = "Run"          # the CPU defense can't call a run
        broken["Counters"][1] = dict(broken["Counters"][2])  # listed twice
        broken["Counters"].append({"bOffense": False, "Observed": "Punt", "Counter": "Run", "Weight": 1})
        validate_data.validate_opponent_model(self.path, broken)
        joined = "\n".join(validate_data.errors)
        for expected in ("DistanceBuckets", "MinMultiplier", "HalftimeQuarter", "Counters[0].Counter: 'Run'",
                         "is listed twice", "Observed: 'Punt'"):
            self.assertIn(expected, joined)


if __name__ == "__main__":
    unittest.main()
