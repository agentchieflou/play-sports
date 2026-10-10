"""Epic 152: gameplay code never names a platform implementation or forks on the platform
(tools/lint_conventions.py's platform rule)."""

import unittest
from pathlib import Path

from tools import lint_conventions as lint

GAMEPLAY = Path("Source/PlaySports/Private/PSSomeSystem.cpp")
GAMEPLAY_HEADER = Path("Source/PlaySports/Public/PSSomeSystem.h")


class PlatformRuleTests(unittest.TestCase):
    def test_repo_gameplay_code_is_platform_independent(self):
        found = []
        for path in sorted((lint.REPO / "Source").rglob("*")):
            if path.suffix not in (".h", ".cpp") or "Intermediate" in path.parts:
                continue
            rel = path.relative_to(lint.REPO)
            text = path.read_text(encoding="utf-8", errors="replace")
            found += [f"{rel}:{line}: {message}" for line, message in lint.platform_problems(rel, text)]
        self.assertEqual(found, [])

    def test_naming_an_implementation_in_gameplay_code_is_flagged(self):
        include = '#include "PSPlatformBackendLocal.h"\n'
        self.assertEqual([line for line, _ in lint.platform_problems(GAMEPLAY, include)], [1])
        use = "void F()\n{\n    UPSPlatformBackendLocal* Local = nullptr;\n}\n"
        self.assertEqual([line for line, _ in lint.platform_problems(GAMEPLAY_HEADER, use)], [3])
        # The abstract backend is the platform layer's business too: gameplay asks the services.
        base = "UPSPlatformBackend* Backend = Services->GetBackend();\n"
        self.assertTrue(lint.platform_problems(GAMEPLAY, base))

    def test_a_backend_path_in_a_string_is_still_a_reference(self):
        text = 'const FSoftClassPath Path(TEXT("/Script/PlaySports.PSPlatformBackendLocal"));\n'
        self.assertTrue(lint.platform_problems(GAMEPLAY, text))

    def test_comments_may_mention_implementations(self):
        text = ("// Saves go where UPSPlatformBackendLocal says.\n"
                "/** Not a reference: UPSPlatformBackend is\n"
                " *  named in a doc comment. */\n"
                "int32 Value = 0; // nor PSPlatformBackendLocal here\n"
                'FString Url = TEXT("http://example.com"); // a // in a string is not a comment\n')
        self.assertEqual(lint.platform_problems(GAMEPLAY, text), [])

    def test_platform_forks_in_gameplay_code_are_flagged(self):
        text = ("#if PLATFORM_IOS\n"
                "int32 A = 1;\n"
                "#elif PLATFORM_ANDROID\n"
                "int32 A = 2;\n"
                "#endif\n"
                "#ifdef PLATFORM_XBOXONE\n"
                "#endif\n"
                "#  if !PLATFORM_DESKTOP\n"
                "#endif\n")
        self.assertEqual([line for line, _ in lint.platform_problems(GAMEPLAY, text)], [1, 3, 6, 8])

    def test_other_conditionals_pass(self):
        text = ("#if WITH_EDITOR\n#endif\n"
                "#if WITH_DEV_AUTOMATION_TESTS\n#endif\n"
                "#if !UE_BUILD_SHIPPING\n#endif\n"
                "const int32 Line = PLATFORM_CACHE_LINE_SIZE;\n")
        self.assertEqual(lint.platform_problems(GAMEPLAY, text), [])

    def test_the_platform_layer_and_tests_are_exempt(self):
        text = '#include "PSPlatformBackendLocal.h"\n#if PLATFORM_IOS\n#endif\n'
        for exempt in ("Source/PlaySports/Private/PSPlatformServices.cpp",
                       "Source/PlaySports/Public/PSPlatformBackendLocal.h",
                       "Source/PlaySports/Private/PSPlatformBackendIOS.cpp",
                       "Source/PlaySports/Private/Tests/PSPlatformServicesTests.cpp",
                       "Plugins/AgenticLink/Source/AgenticLink/Private/AgenticLinkModule.cpp"):
            self.assertEqual(lint.platform_problems(Path(exempt), text), [], exempt)

    def test_strip_comments_keeps_line_numbers(self):
        text = "a /* one\ntwo */ b\n// three\nc\n"
        stripped = lint.strip_comments(text)
        self.assertEqual(stripped.count("\n"), text.count("\n"))
        self.assertEqual(stripped.split("\n")[1].strip(), "b")
        self.assertEqual(stripped.split("\n")[3], "c")


class TitleSafeRuleTests(unittest.TestCase):
    """Epic 150: every widget reaches the screen through PSTitleSafeArea."""

    def test_repo_widgets_go_through_the_title_safe_area(self):
        found = []
        for path in sorted((lint.REPO / "Source").rglob("*")):
            if path.suffix not in (".h", ".cpp") or "Intermediate" in path.parts:
                continue
            rel = path.relative_to(lint.REPO)
            text = path.read_text(encoding="utf-8", errors="replace")
            found += [f"{rel}:{line}: {message}" for line, message in lint.title_safe_problems(rel, text)]
        self.assertEqual(found, [])

    def test_the_hud_and_menus_use_the_helper(self):
        hud = (lint.REPO / "Source/PlaySports/Private/PSHUD.cpp").read_text(encoding="utf-8")
        for widget in ("ScoreboardWidget", "PersonnelWidget", "ChyronWidget"):
            self.assertIn(f"PSTitleSafeArea::AddInside({widget})", hud)
        captions = (lint.REPO / "Source/PlaySports/Private/PSUIAccessibilitySubsystem.cpp").read_text(encoding="utf-8")
        self.assertIn("PSTitleSafeArea::AddInside(CaptionWidget", captions)
        menus = (lint.REPO / "Source/PlaySports/Private/PSMenuScreenWidget.cpp").read_text(encoding="utf-8")
        self.assertIn("Backdrop->SetPadding(TitleSafeMargin)", menus)

    def test_a_direct_viewport_add_is_flagged(self):
        text = "void F(UUserWidget* W)\n{\n    W->AddToViewport();\n    W->AddToPlayerScreen(2);\n}\n"
        self.assertEqual([line for line, _ in lint.title_safe_problems(GAMEPLAY, text)], [3, 4])

    def test_the_helper_tests_and_comments_are_exempt(self):
        text = "Widget->AddToViewport(ZOrder);\n"
        self.assertEqual(lint.title_safe_problems(Path("Source/PlaySports/Private/PSTitleSafeArea.cpp"), text), [])
        self.assertEqual(lint.title_safe_problems(Path("Source/PlaySports/Private/Tests/PSWidgetTests.cpp"), text), [])
        self.assertEqual(lint.title_safe_problems(GAMEPLAY, "// never call AddToViewport() directly\n"), [])


if __name__ == "__main__":
    unittest.main()
