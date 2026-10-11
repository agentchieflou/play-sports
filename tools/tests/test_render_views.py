"""Lane V1: the render capture's views data contract (Data/render_views.json; no Unreal)."""

import copy
import json
import unittest

from tools import validate_data

# The views the render workflow is asked to start with (one per look the owner judges).
REQUIRED_VIEWS = {
    "broadcast_wide", "all22_endzone", "sideline_field_level", "player_closeup", "helmet_closeup",
    "stadium_aerial", "turf_low",
}


class RenderViewsContractTest(unittest.TestCase):
    def setUp(self):
        validate_data.errors.clear()

    def tearDown(self):
        validate_data.errors.clear()

    def load(self):
        path = validate_data.DATA_DIR / "render_views.json"
        return path, json.loads(path.read_text(encoding="utf-8"))

    def test_shipped_file_is_clean(self):
        path, payload = self.load()
        validate_data.validate_render_views(path, payload)
        self.assertEqual(validate_data.errors, [])

    def test_shipped_file_has_the_starting_views(self):
        _, payload = self.load()
        self.assertEqual({view["ViewId"] for view in payload["RenderViews"]}, REQUIRED_VIEWS)

    def test_close_ups_frame_a_player(self):
        _, payload = self.load()
        for view in payload["RenderViews"]:
            if view["ViewId"].endswith("closeup"):
                self.assertEqual(view["Anchor"], "Player", view["ViewId"])

    def test_mistakes_are_reported(self):
        path, payload = self.load()
        broken = copy.deepcopy(payload)
        broken["MeasureFrames"] = broken["SettleFrames"] + 1
        broken["ResolutionX"] = 0
        broken["CompileTimeoutSeconds"] = 0
        broken["Exposure"] = 1
        broken["bCaptureOnlyPreSnap"] = "yes"
        broken["StreamingWaitSeconds"] = -1
        views = broken["RenderViews"]
        views[1]["ViewId"] = views[0]["ViewId"].upper()
        views[2]["ViewId"] = "has space"
        views[3]["PlayerRole"] = "Kicker"
        views[3]["PlayerIndex"] = -1
        views[4]["TargetYards"] = dict(views[4]["CameraYards"])
        views[5]["FieldOfViewDegrees"] = 179
        views[6]["CameraYards"] = {"X": 1, "Y": 2}
        views[6]["Anchor"] = "Ball"
        views[0]["Exposure"] = 1
        validate_data.validate_render_views(path, broken)
        joined = "\n".join(validate_data.errors)
        for expected in ("MeasureFrames", "ResolutionX", "CompileTimeoutSeconds", "names must match FPSRenderCaptureSettings",
                         "bCaptureOnlyPreSnap", "StreamingWaitSeconds",
                         "is used twice", "letters, digits and underscores", "not a valid EPlayerRole", "PlayerIndex",
                         "must differ", "FieldOfViewDegrees", "numbers X, Y and Z", "'Ball' must be one of",
                         "names must match FPSRenderView"):
            self.assertIn(expected, joined)

    def test_views_are_required(self):
        path, payload = self.load()
        broken = copy.deepcopy(payload)
        broken["RenderViews"] = []
        validate_data.validate_render_views(path, broken)
        self.assertIn("non-empty list", "\n".join(validate_data.errors))


if __name__ == "__main__":
    unittest.main()
