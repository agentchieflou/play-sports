"""The live-play demo set's contract, the rosters' jersey numbers and the CI summary (no Unreal)."""

import copy
import json
import tempfile
import unittest
from pathlib import Path

from tools import play_demos, validate_data


def checked(fn, *args):
    """The errors fn reports, without touching the validator's run-wide list."""
    saved = list(validate_data.errors)
    validate_data.errors.clear()
    try:
        fn(*args)
        return list(validate_data.errors)
    finally:
        validate_data.errors[:] = saved


def roster(*numbers):
    return [{"PlayerId": f"P{i}", "DisplayName": f"P{i}", "Role": "WideReceiver", "WeightKg": 90.0, "HeightCm": 185.0,
             "Speed": 80.0, "Agility": 80.0, "Strength": 70.0, "Acceleration": 80.0, "Awareness": 75.0, "Stamina": 90.0,
             **({"JerseyNumber": n} if n is not None else {})} for i, n in enumerate(numbers)]


class PlayDemoCatalogTest(unittest.TestCase):
    def setUp(self):
        self.catalog = json.loads((validate_data.DATA_DIR / "play_demos.json").read_text(encoding="utf-8"))

    def test_shipped_catalog_is_clean(self):
        self.assertEqual(checked(validate_data.validate_play_demos, "play_demos.json", self.catalog), [])

    def test_shipped_set_covers_the_brief(self):
        intents = " ".join(demo["Intent"].lower() for demo in self.catalog["PlayDemos"])
        for wanted in ("inside run", "outside run", "quick pass", "deep pass"):
            self.assertIn(wanted, intents)

    def test_call_outside_the_teams_scheme_is_refused(self):
        bad = copy.deepcopy(self.catalog)
        demo = bad["PlayDemos"][0]
        demo["HomeTeamId"], demo["OffensePlayId"] = "Wolves", "Offense_FourVerts"
        errors = checked(validate_data.validate_play_demos, "play_demos.json", bad)
        self.assertTrue(any("doesn't keep the 'Spread' formation" in e for e in errors), errors)

    def test_bad_fields_are_reported(self):
        bad = copy.deepcopy(self.catalog)
        bad["FrameRateHz"] = 0
        bad["PostWhistleSeconds"] = bad["MaxResultWaitSeconds"]
        bad["PlayDemos"][1]["DefensePlayId"] = "Offense_SlantFlat"
        bad["PlayDemos"][2]["WantedOutcome"] = "Safety"
        bad["PlayDemos"][3]["DemoId"] = bad["PlayDemos"][0]["DemoId"]
        bad["PlayDemos"][4]["AwayTeamId"] = bad["PlayDemos"][4]["HomeTeamId"]
        bad["PlayDemos"][5]["SeedTries"] = 0
        bad["PlayDemos"][6]["bAllowAudibles"] = "no"
        bad["MinLinemanMoveCm"] = -1
        errors = " | ".join(checked(validate_data.validate_play_demos, "play_demos.json", bad))
        for needle in ("FrameRateHz", "PostWhistleSeconds must be shorter", "is not a defensive play", "WantedOutcome",
                       "used twice", "two different teams", "SeedTries", "bAllowAudibles", "MinLinemanMoveCm"):
            self.assertIn(needle, errors)


class JerseyNumberTest(unittest.TestCase):
    def test_numbers_one_to_ninety_nine_unique_per_roster(self):
        self.assertEqual(checked(validate_data.validate_players, "r.json", roster(1, 99, None, 0)), [])
        errors = " | ".join(checked(validate_data.validate_players, "r.json", roster(12, 12, 100)))
        self.assertIn("already worn", errors)
        self.assertIn("is not 1-99", errors)

    def test_every_shipped_roster_numbers_everyone(self):
        for path in [validate_data.DATA_DIR / "sample_players.json", *sorted((validate_data.DATA_DIR / "rosters").glob("*.json"))]:
            players = json.loads(path.read_text(encoding="utf-8"))["Players"]
            numbers = [p.get("JerseyNumber", 0) for p in players]
            self.assertTrue(all(1 <= n <= 99 for n in numbers), path.name)
            self.assertEqual(len(set(numbers)), len(numbers), path.name)


class SummaryTest(unittest.TestCase):
    PLAY = {"demoId": "InsideRun", "title": "Inside Zone Run: K. Trample runs for 6 yards", "outcome": "Run",
            "result": "Tackle", "yardsGained": 6, "endedBy": "Tackle", "playSeconds": 3.2, "frameCount": 290,
            "frameRateHz": 30.0, "problems": []}

    def test_summary_lists_every_play(self):
        with tempfile.TemporaryDirectory() as folder:
            bad = dict(self.PLAY, demoId="DeepPass", problems=["The ball moved only 10 cm | from the snap."])
            (Path(folder) / "index.json").write_text(json.dumps({"plays": [self.PLAY, bad]}), encoding="utf-8")
            out = Path(folder) / "summary.md"
            self.assertEqual(play_demos.main(["summary", folder, "--summary", str(out)]), 0)
            text = out.read_text(encoding="utf-8")
            self.assertIn("| InsideRun | Inside Zone Run: K. Trample runs for 6 yards | Run (Tackle, 6 yd) | Tackle |", text)
            self.assertIn("The ball moved only 10 cm / from the snap.", text)
            self.assertEqual(len([line for line in text.splitlines() if line.startswith("| ")]), 3)

    def test_no_index_is_reported_not_fatal(self):
        with tempfile.TemporaryDirectory() as folder:
            self.assertIsNone(play_demos.load_index(folder))
            self.assertEqual(play_demos.main(["summary", folder]), 0)


if __name__ == "__main__":
    unittest.main()
