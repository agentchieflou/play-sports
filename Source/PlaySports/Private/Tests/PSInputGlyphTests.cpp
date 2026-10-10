// PSInputGlyphTests.cpp -- Epic 128 (button glyphs)
//
// Tests covered:
//   1. Data/input_glyphs.json loads through UPSDataIngestion, validates against the input
//      catalog (every bound key is drawable), and answers action lookups per device: the
//      Xbox set on a gamepad, keycaps on keyboard, action-wide glyphs (WASD), keyboard
//      fallback for unlisted keys, and a remapped binding showing its new button with no
//      table edit. Every validation rule fires on a broken table.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSInputGlyphs.h"
#include "InputCoreTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSInputGlyphTests
{
    static bool HasProblem(const TArray<FString>& Problems, const TCHAR* Fragment)
    {
        return Problems.ContainsByPredicate([Fragment](const FString& Problem) { return Problem.Contains(Fragment); });
    }

    static FPSInputGlyphSetDef* FindSet(FPSInputGlyphCatalog& Table, const TCHAR* GlyphSetId)
    {
        const FName Id(GlyphSetId);
        return Table.GlyphSets.FindByPredicate([Id](const FPSInputGlyphSetDef& Set) { return Set.GlyphSetId == Id; });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSInputGlyphTableTest,
    "PlaySports.Input.GlyphTableCoversCatalog",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSInputGlyphTableTest::RunTest(const FString& Parameters)
{
    using namespace PSInputGlyphTests;

    UPSInputConfig* Config = NewObject<UPSInputConfig>();
    TestTrue(TEXT("Catalog, tuning and glyphs load from Data/"), Config->LoadDefaults());
    UPSInputGlyphs* Glyphs = Config->GetGlyphs();
    if (!TestNotNull(TEXT("The input config owns a glyph table"), Glyphs))
    {
        return false;
    }

    // The table in use is the file's, read through UPSDataIngestion.
    FPSInputGlyphCatalog FromFile;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    TestTrue(TEXT("input_glyphs.json loads through UPSDataIngestion"), Ingestion->LoadInputGlyphsFromJson(UPSInputGlyphs::GetDefaultGlyphsPath(), FromFile));
    TestEqual(TEXT("Same glyph sets as the file"), Glyphs->Table.GlyphSets.Num(), FromFile.GlyphSets.Num());

    for (const FString& Problem : Glyphs->Validate(&Config->Catalog))
    {
        AddError(FString::Printf(TEXT("input_glyphs.json: %s"), *Problem));
    }

    const FPSInputGlyphSetDef* GamepadSet = Glyphs->GetDefaultSet(EPSInputDevice::Gamepad);
    const FPSInputGlyphSetDef* KeyboardSet = Glyphs->GetDefaultSet(EPSInputDevice::KeyboardMouse);
    if (!TestNotNull(TEXT("A default gamepad set"), GamepadSet) || !TestNotNull(TEXT("A default keyboard/mouse set"), KeyboardSet))
    {
        return false;
    }
    TestEqual(TEXT("The Xbox set comes first for gamepads"), GamepadSet->GlyphSetId, FName(TEXT("Xbox")));

    // Action -> glyph, per device, through the catalog's bindings.
    FPSInputGlyph Glyph;
    TestTrue(TEXT("Confirm on a gamepad has a glyph"), Config->GetGlyphForAction(TEXT("Confirm"), TEXT("OnField"), EPSInputDevice::Gamepad, Glyph));
    TestEqual(TEXT("Confirm on a gamepad is Xbox A"), Glyph.GlyphId, FName(TEXT("Xbox_A")));
    TestEqual(TEXT("...labelled A"), Glyph.Label, FString(TEXT("A")));
    TestTrue(TEXT("...for the A button"), Glyph.Key == EKeys::Gamepad_FaceButton_Bottom);

    TestTrue(TEXT("Confirm on keyboard has a glyph"), Config->GetGlyphForAction(TEXT("Confirm"), TEXT("OnField"), EPSInputDevice::KeyboardMouse, Glyph));
    TestEqual(TEXT("Confirm on keyboard is Enter"), Glyph.GlyphId, FName(TEXT("Key_Enter")));

    TestTrue(TEXT("Back in menus on a gamepad has a glyph"), Config->GetGlyphForAction(TEXT("Cancel"), TEXT("Menu"), EPSInputDevice::Gamepad, Glyph));
    TestEqual(TEXT("Back in menus is Xbox B"), Glyph.GlyphId, FName(TEXT("Xbox_B")));

    TestTrue(TEXT("Pause on a gamepad has a glyph"), Config->GetGlyphForAction(TEXT("Pause"), TEXT("OnField"), EPSInputDevice::Gamepad, Glyph));
    TestEqual(TEXT("Pause is the Xbox Menu button"), Glyph.GlyphId, FName(TEXT("Xbox_Menu")));

    TestTrue(TEXT("Move on keyboard has a glyph"), Config->GetGlyphForAction(TEXT("Move"), TEXT("OnField"), EPSInputDevice::KeyboardMouse, Glyph));
    TestEqual(TEXT("Move on keyboard uses the action-wide WASD glyph"), Glyph.Label, FString(TEXT("WASD")));
    TestFalse(TEXT("...which stands for no single key"), Glyph.Key.IsValid());

    TestTrue(TEXT("Move on a gamepad has a glyph"), Config->GetGlyphForAction(TEXT("Move"), TEXT("OnField"), EPSInputDevice::Gamepad, Glyph));
    TestEqual(TEXT("Move on a gamepad is the left stick"), Glyph.GlyphId, FName(TEXT("Xbox_LS")));

    TestFalse(TEXT("Look has no glyph on the field (it lives in World only)"), Config->GetGlyphForAction(TEXT("Look"), TEXT("OnField"), EPSInputDevice::Gamepad, Glyph));
    TestFalse(TEXT("An unknown action has no glyph"), Config->GetGlyphForAction(TEXT("Juke"), TEXT("OnField"), EPSInputDevice::Gamepad, Glyph));

    // Keys outside the table: keyboard falls back to a keycap with the key's name,
    // gamepads don't guess, and a key never draws with the other device's set.
    TestTrue(TEXT("An unlisted keyboard key still gets a keycap"), Glyphs->GetGlyphForKey(EKeys::F5, EPSInputDevice::KeyboardMouse, Glyph));
    TestEqual(TEXT("...named after the key"), Glyph.GlyphId, FName(TEXT("Key_F5")));
    TestFalse(TEXT("...with a label"), Glyph.Label.IsEmpty());
    TestFalse(TEXT("A gamepad key does not draw with the keyboard set"), Glyphs->GetGlyphForKey(EKeys::Gamepad_FaceButton_Bottom, EPSInputDevice::KeyboardMouse, Glyph));

    // A remap (Epic 103) changes the catalog only; the glyph follows with no table edit.
    FPSInputCatalog Remapped = Config->Catalog;
    FPSInputActionDef* Confirm = Remapped.Actions.FindByPredicate([](const FPSInputActionDef& Action) { return Action.ActionId == FName(TEXT("Confirm")); });
    if (TestNotNull(TEXT("Confirm is in the catalog"), Confirm))
    {
        for (FPSInputKeyBinding& Binding : Confirm->Bindings)
        {
            if (Binding.Key == EKeys::Gamepad_FaceButton_Bottom.GetFName())
            {
                Binding.Key = EKeys::Gamepad_FaceButton_Top.GetFName();
            }
        }
        TestTrue(TEXT("Remapped Confirm has a glyph"), Glyphs->GetGlyphForAction(Remapped, TEXT("Confirm"), TEXT("OnField"), EPSInputDevice::Gamepad, Glyph));
        TestEqual(TEXT("Remapped Confirm shows Xbox Y"), Glyph.GlyphId, FName(TEXT("Xbox_Y")));
    }

    // Broken tables report each mistake.
    UPSInputGlyphs* Broken = NewObject<UPSInputGlyphs>();
    {
        Broken->Table = Glyphs->Table;
        FindSet(Broken->Table, TEXT("Xbox"))->Keys.RemoveAll([](const FPSKeyGlyphDef& Def) { return Def.Key == EKeys::Gamepad_FaceButton_Bottom.GetFName(); });
        TestTrue(TEXT("A bound gamepad key without a glyph is reported"),
            HasProblem(Broken->Validate(&Config->Catalog), TEXT("no glyph for 'Gamepad_FaceButton_Bottom'")));
        TestFalse(TEXT("...but only when checking against the catalog"),
            HasProblem(Broken->Validate(), TEXT("no glyph for")));
    }
    {
        Broken->Table = Glyphs->Table;
        FPSKeyGlyphDef Stray;
        Stray.Key = EKeys::Gamepad_FaceButton_Top.GetFName();
        Stray.GlyphId = TEXT("Xbox_Y");
        Stray.Label = TEXT("Y");
        FindSet(Broken->Table, TEXT("KeyboardMouse"))->Keys.Add(Stray);
        TestTrue(TEXT("A gamepad key in the keyboard set is reported"), HasProblem(Broken->Validate(), TEXT("is a Gamepad key, not KeyboardMouse")));
    }
    {
        Broken->Table = Glyphs->Table;
        FPSKeyGlyphDef Unknown;
        Unknown.Key = TEXT("NotAKey");
        Unknown.GlyphId = TEXT("Key_Nope");
        Unknown.Label = TEXT("?");
        FindSet(Broken->Table, TEXT("KeyboardMouse"))->Keys.Add(Unknown);
        TestTrue(TEXT("A name that is not an engine key is reported"), HasProblem(Broken->Validate(), TEXT("'NotAKey' is not an engine key")));
    }
    {
        Broken->Table = Glyphs->Table;
        FPSInputGlyphSetDef Second = *FindSet(Broken->Table, TEXT("Xbox"));
        Second.GlyphSetId = TEXT("PlayStation");
        Broken->Table.GlyphSets.Add(Second);
        TestTrue(TEXT("Two default sets for one device are reported"), HasProblem(Broken->Validate(), TEXT("'Gamepad' needs exactly one default glyph set (has 2)")));
    }
    {
        Broken->Table = Glyphs->Table;
        FPSActionGlyphDef Unknown;
        Unknown.ActionId = TEXT("Juke");
        Unknown.GlyphId = TEXT("Key_Juke");
        Unknown.Label = TEXT("J");
        FindSet(Broken->Table, TEXT("KeyboardMouse"))->Actions.Add(Unknown);
        TestTrue(TEXT("An action glyph for an action the catalog lacks is reported"),
            HasProblem(Broken->Validate(&Config->Catalog), TEXT("'Juke', which is not in the input catalog")));
    }
    {
        Broken->Table = Glyphs->Table;
        FindSet(Broken->Table, TEXT("Xbox"))->Keys[0].Label.Empty();
        TestTrue(TEXT("A glyph without a label is reported"), HasProblem(Broken->Validate(), TEXT("needs a GlyphId and a Label")));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
