"""Epic 86: the draft's data contract (no Unreal)."""

import copy
import json
import unittest

from tools import validate_data


class DraftContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()
        self.path = validate_data.DATA_DIR / "draft.json"
        self.tuning = json.loads(self.path.read_text(encoding="utf-8"))

    def tearDown(self):
        validate_data.errors.clear()

    def test_shipped_tuning_is_clean(self):
        validate_data.validate_draft(self.path, self.tuning, validate_data.load_contract_max_years())
        self.assertEqual(validate_data.errors, [])

    def test_mistakes_are_reported(self):
        broken = copy.deepcopy(self.tuning)
        broken["ReportCost"] = 0
        broken["ProDayShare"] = 1.5
        broken["NumRounds"] = 0
        broken["RookieYears"] = 9
        broken["CombineDrills"][0]["Attribute"] = "WeightKg"
        broken["CombineDrills"][1]["PerPoint"] = 0
        broken["CombineDrills"][2]["DrillId"] = broken["CombineDrills"][3]["DrillId"]
        validate_data.validate_draft(self.path, broken, 5)
        joined = "\n".join(validate_data.errors)
        for expected in ("ReportCost", "ProDayShare", "NumRounds", "longer than contracts.json's MaxContractYears (5)",
                         "CombineDrills[0].Attribute", "CombineDrills[1].PerPoint", "CombineDrills[3].DrillId"):
            self.assertIn(expected, joined)


if __name__ == "__main__":
    unittest.main()
