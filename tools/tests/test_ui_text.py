"""Epic 106: the UI string tables' generator and checks (tools/ui_text.py)."""

import csv
import io
import unittest

from tools import ui_text


class UITextTests(unittest.TestCase):
    def test_repo_tables_are_current_and_clean(self):
        self.assertEqual(ui_text.problems(), [])

    def test_keys_follow_the_contract(self):
        rows = {key: source for key, source, _ in ui_text.data_rows()}
        self.assertEqual(rows.get("Menu.MainMenu.Title"), "PLAY SPORTS")
        self.assertIn("Menu.MainMenu.Settings.Label", rows)
        self.assertIn("Setting.Category.Video", rows)
        self.assertIn("Setting.FrameRateLimit.Choice0", rows)
        self.assertTrue(any(key.startswith("Tip.") for key in rows))

    def test_render_round_trips_through_csv(self):
        rows = [("A.Key", 'Say "hi", then\nleave', "comment")]
        parsed = list(csv.reader(io.StringIO(ui_text.render(rows))))
        self.assertEqual(parsed[0], ["Key", "SourceString", "Comment"])
        # A newline is written as \n, which the string table import turns back into one.
        self.assertEqual(parsed[1], ["A.Key", 'Say "hi", then\\nleave', "comment"])

    def test_string_problems(self):
        self.assertEqual(ui_text._string_problems("K", "{Label}: {Value}"), [])
        self.assertTrue(ui_text._string_problems("K", "C:\\path"))
        self.assertTrue(ui_text._string_problems("K", "{Label: oops"))
        self.assertTrue(ui_text._string_problems("K", "oops}"))

    def test_raw_text_gate(self):
        self.assertTrue(ui_text.RAW_TEXT.search('Label->SetText(FText::FromString(Name));'))
        self.assertTrue(ui_text.RAW_TEXT.search('NSLOCTEXT("A", "B", "C")'))
        self.assertFalse(ui_text.RAW_TEXT.search('FText::AsNumber(Count)'))
        self.assertFalse(ui_text.RAW_TEXT.search('UPSLocalization::FromLocalized(Text)'))

    def test_key_uses(self):
        text = ('UPSLocalization::GetText(TEXT("Menu.ResetToDefaults")); '
                'UPSLocalization::Format(TEXT("Menu.Option"), Arguments); '
                'FString::Format(TEXT("{0}"), Args);')
        self.assertEqual(ui_text.KEY_USE.findall(text), ["Menu.ResetToDefaults", "Menu.Option"])


class XboxTermTests(unittest.TestCase):
    """Epic 150: player-facing words follow the Xbox naming standard (XR-022)."""

    def test_non_xbox_words_are_flagged(self):
        for text in ("The controller rumbles on hits.", "Press L3 to sprint.", "Click the right thumbstick.",
                     "Press the Start button.", "Press the Cross button.", "Plug in a DualSense.", "Your Gamer tag"):
            self.assertTrue(ui_text.term_problems("K", text), text)

    def test_xbox_words_pass(self):
        for text in ("The controller vibrates on hits.", "Press LSB to sprint.", "Click the right stick.",
                     "Press the Menu button.", "Press A.", "Your gamertag", "Level 3", "1st and 10"):
            self.assertEqual(ui_text.term_problems("K", text), [], text)

    def test_the_xbox_glyph_labels_are_checked(self):
        labels = dict(ui_text.xbox_glyph_labels())
        self.assertEqual(labels.get("Xbox_A"), "A")
        self.assertEqual(labels.get("Xbox_RS_Press"), "RSB")
        self.assertEqual([p for glyph, label in labels.items() for p in ui_text.term_problems(glyph, label)], [])


if __name__ == "__main__":
    unittest.main()
