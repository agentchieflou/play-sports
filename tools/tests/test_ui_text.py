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


if __name__ == "__main__":
    unittest.main()
