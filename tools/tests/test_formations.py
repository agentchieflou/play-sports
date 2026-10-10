"""Formation alignments' data contract (Data/formations.json; no Unreal)."""

import copy
import json
import unittest

from tools import validate_data


class FormationsContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()
        self.path = validate_data.DATA_DIR / "formations.json"
        self.catalog = json.loads(self.path.read_text(encoding="utf-8"))

    def tearDown(self):
        validate_data.errors.clear()

    def formation(self, catalog, name):
        return next(f for f in catalog["OffenseFormations"] if f["Formation"] == name)

    def test_shipped_catalog_is_clean(self):
        validate_data.validate_formations(self.path, self.catalog)
        self.assertEqual(validate_data.errors, [])

    def test_recognition_reads_the_declared_look(self):
        recognition = json.loads((validate_data.DATA_DIR / "play_recognition.json").read_text(encoding="utf-8"))
        reads = {f["Formation"]: validate_data.formation_read(f, recognition, 100.0) for f in self.catalog["OffenseFormations"]}
        self.assertEqual(reads["Trips Right"], ("UnderCenter", "Single", 1))
        self.assertEqual(reads["Trips Left"], ("UnderCenter", "Single", -1))
        self.assertEqual(reads["Shotgun"][0], "Shotgun")
        self.assertEqual(reads["I-Form"][1], "I")

    def test_mistakes_are_reported(self):
        broken = copy.deepcopy(self.catalog)
        trips = self.formation(broken, "Trips Right")
        trips["Slots"][0]["ScrimmageYardOffset"] = -5.0          # a shotgun depth it doesn't declare
        trips["Slots"].append({"Role": "WideReceiver", "ScrimmageYardOffset": 1.0, "Technique": "5"})
        self.formation(broken, "Ace")["Formation"] = "Ace Wing"   # the playbook's Ace has no alignment now
        broken["FrontAlignments"][0]["Slots"][0]["Technique"] = "8"
        broken["ShellAlignments"][0]["Slots"].append({"Role": "Linebacker", "ScrimmageYardOffset": 5.0})
        cover2 = next(s for s in broken["ShellAlignments"] if s["Shell"] == "Cover2")
        cover2["Slots"][1]["ScrimmageYardOffset"] = 5.0           # one deep safety, not two
        validate_data.validate_formations(self.path, broken)
        joined = "\n".join(validate_data.errors)
        for expected in ("reads its slots as Shotgun", "the offense lines up behind", "the offense keys on no technique",
                         "aren't its personnel package's", "'Ace' (sample_playbook.json) has no alignment",
                         "'8' is not in Techniques", "a shell places the defensive backs",
                         "'Cover2': 1 deep safeties; defensive_presnap.json plays 2"):
            self.assertIn(expected, joined)

    def test_a_front_short_of_its_package_is_reported(self):
        broken = copy.deepcopy(self.catalog)
        nickel = next(f for f in broken["FrontAlignments"] if f["Front"] == "Nickel")
        nickel["Slots"] = [s for s in nickel["Slots"] if s["Role"] != "Linebacker"]
        validate_data.validate_formations(self.path, broken)
        self.assertTrue(any("place 0 of its 2 Linebackers" in error for error in validate_data.errors))


if __name__ == "__main__":
    unittest.main()
