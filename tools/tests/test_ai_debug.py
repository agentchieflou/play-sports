"""Epic 85: the AI debug settings and scenario data contracts (no Unreal)."""

import copy
import json
import unittest

from tools import validate_data


class AIDebugContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()

    def tearDown(self):
        validate_data.errors.clear()

    def load(self, name):
        path = validate_data.DATA_DIR / name
        return path, json.loads(path.read_text(encoding="utf-8"))

    def test_shipped_files_are_clean(self):
        path, payload = self.load("ai_debug.json")
        validate_data.validate_ai_debug(path, payload)
        path, payload = self.load("ai_scenarios.json")
        validate_data.validate_ai_scenarios(path, payload, validate_data.load_dna_catalog())
        self.assertEqual(validate_data.errors, [])

    def test_debug_mistakes_are_reported(self):
        path, payload = self.load("ai_debug.json")
        broken = dict(payload, PostMortemDirectory="../outside", MaxPostMortemFiles=0, bLogDecisions="yes")
        validate_data.validate_ai_debug(path, broken)
        joined = "\n".join(validate_data.errors)
        for expected in ("PostMortemDirectory", "MaxPostMortemFiles", "bLogDecisions"):
            self.assertIn(expected, joined)

    def test_scenario_mistakes_are_reported(self):
        path, payload = self.load("ai_scenarios.json")
        broken = copy.deepcopy(payload)
        scenario = broken["Scenarios"][0]
        scenario["Players"][0]["Role"] = "Kicker"
        scenario["Players"].append(dict(scenario["Players"][1]))  # the same PlayerId twice
        scenario["Players"][-1]["CoverTarget"] = "Nobody"
        scenario["Expectations"].append({"PlayerId": "Ghost", "Action": "Cover"})
        broken["Scenarios"][1]["ScenarioId"] = scenario["ScenarioId"]
        validate_data.validate_ai_scenarios(path, broken, validate_data.load_dna_catalog())
        joined = "\n".join(validate_data.errors)
        for expected in ("'Kicker' is not an EPlayerRole", "must be a unique, non-empty name", "CoverTarget: 'Nobody'",
                         "PlayerId: 'Ghost'"):
            self.assertIn(expected, joined)


if __name__ == "__main__":
    unittest.main()
