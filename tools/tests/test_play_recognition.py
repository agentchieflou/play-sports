"""Epic 80: formation and play recognition's data contract (no Unreal)."""

import copy
import json
import unittest

from tools import validate_data


class PlayRecognitionContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()
        self.path = validate_data.DATA_DIR / "play_recognition.json"
        self.tuning = json.loads(self.path.read_text(encoding="utf-8"))

    def tearDown(self):
        validate_data.errors.clear()

    def test_shipped_tuning_is_clean(self):
        validate_data.validate_play_recognition(self.path, self.tuning)
        self.assertEqual(validate_data.errors, [])

    def test_mistakes_are_reported(self):
        broken = copy.deepcopy(self.tuning)
        broken["PistolMaxDepth"] = broken["UnderCenterMaxDepth"] - 1
        broken["FlowMinSpeed"] = 0
        broken["LatencyJitter"] = 1.0
        broken["FormationClasses"].append(dict(broken["FormationClasses"][0]))  # listed twice
        broken["FormationClasses"].append({"ClassId": "Wildcat", "QBAlignment": "Direct", "Backfield": "Wing",
                                           "MaxWeakSide": -2, "RunLean": 1.5, "Motion": True})
        validate_data.validate_play_recognition(self.path, broken)
        joined = "\n".join(validate_data.errors)
        for expected in ("PistolMaxDepth", "FlowMinSpeed: must be above 0", "LatencyJitter", "used once",
                         "QBAlignment: 'Direct'", "Backfield: 'Wing'", "MaxWeakSide", "RunLean", "['Motion']"):
            self.assertIn(expected, joined)

    def test_no_classes_is_reported(self):
        broken = copy.deepcopy(self.tuning)
        broken["FormationClasses"] = []
        validate_data.validate_play_recognition(self.path, broken)
        self.assertTrue(any("FormationClasses" in error for error in validate_data.errors))

    def test_dna_and_difficulty_can_bind_the_reads(self):
        self.assertEqual(validate_data.DNA_BINDING_TARGETS.get("Recognition"), "play_recognition.json")
        catalog = json.loads((validate_data.DATA_DIR / "player_dna.json").read_text(encoding="utf-8"))
        validate_data.validate_player_dna_catalog(validate_data.DATA_DIR / "player_dna.json", catalog)
        self.assertEqual(validate_data.errors, [])
        bound = {b["Field"] for b in catalog["Bindings"] if b["Target"] == "Recognition"}
        self.assertTrue(bound and bound <= {k for k, v in self.tuning.items() if isinstance(v, (int, float))})


if __name__ == "__main__":
    unittest.main()
