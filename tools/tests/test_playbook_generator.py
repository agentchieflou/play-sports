"""Epic 121: the playbook generator's data contract (no Unreal; the generator itself is C++, tested
by PlaySports.Content.PlaybookGenerator.*)."""

import copy
import json
import unittest

from tools import validate_data


def shipped():
    return json.loads((validate_data.DATA_DIR / "playbook_generator.json").read_text(encoding="utf-8"))


class PlaybookGeneratorContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()
        self.path = validate_data.DATA_DIR / "test_playbook_generator.json"

    def tearDown(self):
        validate_data.errors.clear()

    def errors(self, payload):
        validate_data.validate_playbook_generator(self.path, payload)
        found = list(validate_data.errors)
        validate_data.errors.clear()
        return found

    def concept(self, tuning, concept_id):
        return next(c for c in tuning["Concepts"] if c["ConceptId"] == concept_id)

    def test_shipped_grammar_is_clean(self):
        self.assertEqual(self.errors(shipped()), [])

    def test_shipped_grammar_has_the_roadmaps_families(self):
        ids = {c["ConceptId"] for c in shipped()["Concepts"]}
        self.assertTrue({"Flood", "Mesh", "Dagger"} <= ids)

    def test_mistakes_are_reported(self):
        tuning = copy.deepcopy(shipped())
        self.concept(tuning, "Flood")["Slots"][0]["Routes"].append("Wheel")
        self.concept(tuning, "Mesh")["Slots"][0]["Roles"].append("Quarterback")
        self.concept(tuning, "Smash")["Deceptions"] = ["ZoneRead"]
        self.concept(tuning, "Dagger")["Formations"] = ["Wishbone"]
        tuning["Coverages"][0]["Shell"] = "Tampa2"
        tuning["DefensiveFronts"][0]["Front"] = "5-2"
        tuning["Pressures"] = [p for p in tuning["Pressures"] if p["Blitzers"]]
        tuning["SchemeFlavors"][0]["SchemeId"] = "Wildcat"
        tuning["SchemeFlavors"][1]["ShellWeights"] = {"Cover2": 2.0}
        tuning["Typo"] = 1
        errors = self.errors(tuning)
        for expected in ("route 'Wheel' is not in sample_routes.json", "Quarterback is not a receiver",
                         "ZoneRead doesn't go with a ShortPass concept", "'Wishbone' is not in OffenseFormations",
                         "has no rules in coverage_matchups.json", "has no run fits in run_fits.json",
                         "one must send nobody", "no such scheme in coaching_staffs.json",
                         "is an offensive scheme; it weighs the other side", "unknown field(s) ['Typo']"):
            self.assertTrue(any(expected in e for e in errors), (expected, errors))

    def test_a_concept_that_fits_no_formation(self):
        tuning = copy.deepcopy(shipped())
        flood = self.concept(tuning, "Flood")
        flood["Slots"] = [{"Roles": ["TightEnd"], "Routes": ["Seam"]}] * 4
        errors = self.errors(tuning)
        self.assertTrue(any("'Flood': its slots fit none of its formations" in e for e in errors), errors)


if __name__ == "__main__":
    unittest.main()
