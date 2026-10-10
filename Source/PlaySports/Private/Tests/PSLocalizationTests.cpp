// PSLocalizationTests.cpp -- Epic 106 (localization and text infrastructure)
//
// Tests covered:
//   1. String tables: Data/ui_text.csv and Data/ui_text_data.csv register as PSUI and
//      PSUIData. The C++ keys for the menus, settings and tips match the rows tools/ui_text.py
//      generated from the data files. Patterns format. A string changed since the table was
//      generated shows as written.
//   2. Units and numbers: weights and heights in either unit system, the Units setting
//      picking one, team select showing a roster's size in it, and numbers and dates
//      following the culture.
//   3. Pseudo-localization: with ps.Loc.Pseudo on, every menu screen, every play-call
//      screen's text (formations, plays, the suggestion, adjustments, the situation readout),
//      every narration, setting value, caption, HUD banner and loading tip, and the broadcast
//      overlays (the score bug and its chyrons, the kick readout, the position badges, the
//      personnel panels) show no plain letter outside Verbatim's marks. A string that
//      bypassed the tables would.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSGameStateEvents.h"
#include "PSHUDWidget.h"
#include "PSLoadingTips.h"
#include "PSLocalization.h"
#include "PSMenuComponent.h"
#include "PSOverlayBadgeComponent.h"
#include "PSOverlayBallFlightSubsystem.h"
#include "PSOverlayBroadcastSubsystem.h"
#include "PSOverlayPersonnelSubsystem.h"
#include "PSOverlayScoreBugWidget.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerController.h"
#include "PSSettingsSubsystem.h"
#include "PSTelemetryBus.h"
#include "PSUIAccessibilitySubsystem.h"
#include "PSUITeamCatalog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSLocalizationTests
{
    static UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    static void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    static APSPlayerController* SpawnController(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World ? World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams) : nullptr;
    }

    static UPSSettingsSubsystem* MakeSettings()
    {
        return NewObject<UPSSettingsSubsystem>(NewObject<UGameInstance>());
    }

    static FString DataPath(const TCHAR* FileName)
    {
        FString Path = FPaths::ProjectDir() / TEXT("Data") / FileName;
        FPaths::CollapseRelativeDirectories(Path);
        return Path;
    }

    /** Whether the PSUIData table holds Source at Key. */
    static bool TableHolds(const FString& Key, const FString& Source)
    {
        const FStringTableConstPtr Table = FStringTableRegistry::Get().FindStringTable(UPSLocalization::DataTableId);
        FString Held;
        return Source.IsEmpty() || (Table.IsValid() && Table->GetSourceString(Key, Held) && Held == Source);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- String tables
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStringTablesTest,
    "PlaySports.Localization.StringTables",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStringTablesTest::RunTest(const FString& Parameters)
{
    using namespace PSLocalizationTests;

    UPSLocalization::SetPseudoLocalization(false);
    UPSLocalization::RegisterStringTables();
    TestTrue(TEXT("Data/ui_text.csv is the PSUI string table"), FStringTableRegistry::Get().FindStringTable(UPSLocalization::UITableId).IsValid());
    TestTrue(TEXT("Data/ui_text_data.csv is the PSUIData string table"), FStringTableRegistry::Get().FindStringTable(UPSLocalization::DataTableId).IsValid());
    TestEqual(TEXT("GetText reads PSUI"), UPSLocalization::GetText(TEXT("Menu.ResetToDefaults")).ToString(), FString(TEXT("Reset to defaults")));
    const FString Missing = FString(TEXT("Test.")) + TEXT("NoSuchKey");
    TestFalse(TEXT("A key that isn't there is reported missing"), UPSLocalization::HasText(Missing));

    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Label"), UPSLocalization::Verbatim(TEXT("Vibration")));
    Arguments.Add(TEXT("Value"), UPSLocalization::GetText(TEXT("Common.On")));
    TestEqual(TEXT("A pattern formats its arguments"), UPSLocalization::Format(TEXT("Menu.Option"), Arguments).ToString(), FString(TEXT("Vibration: On")));

    // The keys the C++ builds are the ones tools/ui_text.py generated from the same files.
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSMenuCatalog Menus;
    FPSSettingsCatalog SettingsCatalog;
    FPSLoadingTipCatalog Tips;
    TestTrue(TEXT("ui_menus.json loads"), Ingestion->LoadMenuCatalogFromJson(DataPath(TEXT("ui_menus.json")), Menus));
    TestTrue(TEXT("ui_settings.json loads"), Ingestion->LoadSettingsCatalogFromJson(DataPath(TEXT("ui_settings.json")), SettingsCatalog));
    TestTrue(TEXT("loading_tips.json loads"), Ingestion->LoadLoadingTipsFromJson(DataPath(TEXT("loading_tips.json")), Tips));
    TArray<FString> Unmatched;
    for (const FPSMenuScreenDef& Screen : Menus.Screens)
    {
        if (!TableHolds(UPSLocalization::MenuKey(Screen.ScreenId, TEXT("Title")), Screen.Title)
            || !TableHolds(UPSLocalization::MenuKey(Screen.ScreenId, TEXT("Body")), Screen.Body))
        {
            Unmatched.Add(Screen.ScreenId.ToString());
        }
        for (const FPSMenuOptionDef& Option : Screen.Options)
        {
            if (!TableHolds(UPSLocalization::MenuOptionKey(Screen.ScreenId, Option.OptionId, TEXT("Label")), Option.Label)
                || !TableHolds(UPSLocalization::MenuOptionKey(Screen.ScreenId, Option.OptionId, TEXT("Detail")), Option.Detail))
            {
                Unmatched.Add(Screen.ScreenId.ToString() + TEXT(".") + Option.OptionId.ToString());
            }
        }
    }
    for (const FPSSettingCategoryDef& Category : SettingsCatalog.Categories)
    {
        if (!TableHolds(UPSLocalization::SettingCategoryKey(Category.CategoryId), Category.Label))
        {
            Unmatched.Add(Category.CategoryId.ToString());
        }
    }
    for (const FPSSettingDef& Def : SettingsCatalog.Settings)
    {
        bool bHeld = TableHolds(UPSLocalization::SettingKey(Def.SettingId, TEXT("Label")), Def.Label)
            && TableHolds(UPSLocalization::SettingKey(Def.SettingId, TEXT("Description")), Def.Description)
            && TableHolds(UPSLocalization::SettingKey(Def.SettingId, TEXT("Unit")), Def.Unit);
        for (int32 Index = 0; Index < Def.Choices.Num(); ++Index)
        {
            bHeld &= TableHolds(UPSLocalization::SettingChoiceKey(Def.SettingId, Index), Def.Choices[Index]);
        }
        if (!bHeld)
        {
            Unmatched.Add(Def.SettingId.ToString());
        }
    }
    for (const FPSLoadingTipDef& Tip : Tips.Tips)
    {
        if (!TableHolds(UPSLocalization::TipKey(Tip.TipId), Tip.Text))
        {
            Unmatched.Add(Tip.TipId.ToString());
        }
    }
    TestEqual(TEXT("Every UI data string is in PSUIData under the key the code asks for"), Unmatched.Num(), 0);
    for (const FString& Entry : Unmatched)
    {
        AddInfo(FString::Printf(TEXT("Not in Data/ui_text_data.csv as loaded: %s"), *Entry));
    }

    if (Menus.Screens.Num() > 0)
    {
        const FPSMenuScreenDef& First = Menus.Screens[0];
        TestEqual(TEXT("A data string reads from the table"),
            UPSLocalization::GetDataText(UPSLocalization::MenuKey(First.ScreenId, TEXT("Title")), First.Title).ToString(), First.Title);
        TestEqual(TEXT("...and one changed since the table was generated shows as written"),
            UPSLocalization::GetDataText(UPSLocalization::MenuKey(First.ScreenId, TEXT("Title")), TEXT("A newer title")).ToString(), FString(TEXT("A newer title")));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Units and numbers
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSUnitsTest,
    "PlaySports.Localization.UnitsAndNumbers",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSUnitsTest::RunTest(const FString& Parameters)
{
    using namespace PSLocalizationTests;

    UPSLocalization::SetPseudoLocalization(false);
    TestEqual(TEXT("100 kg in kilograms"), UPSLocalization::FormatWeight(100.f, EPSUnitSystem::Metric).ToString(), FString(TEXT("100 kg")));
    TestEqual(TEXT("...and in pounds"), UPSLocalization::FormatWeight(100.f, EPSUnitSystem::Imperial).ToString(), FString(TEXT("220 lb")));
    TestEqual(TEXT("188 cm in centimeters"), UPSLocalization::FormatHeight(188.f, EPSUnitSystem::Metric).ToString(), FString(TEXT("188 cm")));
    TestEqual(TEXT("...and in feet and inches"), UPSLocalization::FormatHeight(188.f, EPSUnitSystem::Imperial).ToString(), FString(TEXT("6' 2\"")));
    TestEqual(TEXT("A height rounds to the inch, carrying into feet"), UPSLocalization::FormatHeight(182.6f, EPSUnitSystem::Imperial).ToString(), FString(TEXT("6' 0\"")));

    // Numbers and dates follow the culture.
    const FCulturePtr English = FInternationalization::Get().GetCulture(TEXT("en"));
    const FCulturePtr German = FInternationalization::Get().GetCulture(TEXT("de"));
    if (English.IsValid() && German.IsValid())
    {
        TestEqual(TEXT("English groups with commas"), UPSLocalization::FormatNumberIn(1234567.5f, 1, English).ToString(), FString(TEXT("1,234,567.5")));
        TestEqual(TEXT("German with points, and a decimal comma"), UPSLocalization::FormatNumberIn(1234567.5f, 1, German).ToString(), FString(TEXT("1.234.567,5")));
    }
    else
    {
        AddInfo(TEXT("The en and de cultures aren't available here; culture formatting not checked."));
    }
    TestTrue(TEXT("A date carries its year"), UPSLocalization::FormatDate(FDateTime(2026, 10, 10)).ToString().Contains(TEXT("2026")));
    TestEqual(TEXT("A percentage is the culture's"), UPSLocalization::FormatPercent(0.5f).ToString(), FText::AsPercent(0.5f).ToString());

    // The Units setting picks the system; team select shows a roster's size in it.
    UWorld* World = CreateTestWorld();
    APSPlayerController* Controller = SpawnController(World);
    if (!TestNotNull(TEXT("Controller"), Controller))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSettingsSubsystem* Settings = MakeSettings();
    TestNotNull(TEXT("Units is a setting"), Settings->GetCatalog().FindSetting(UPSLocalization::UnitsSettingId));
    TestEqual(TEXT("Units start imperial"), UPSLocalization::GetUnitSystem(Settings), EPSUnitSystem::Imperial);
    TestEqual(TEXT("...and without settings"), UPSLocalization::GetUnitSystem(nullptr), EPSUnitSystem::Imperial);
    UPSMenuComponent* Menu = Controller->GetMenuComponent();
    Menu->SetSettings(Settings);
    const FPSTeamSummary* Team = Menu->GetTeamSummaries().FindByPredicate([](const FPSTeamSummary& Candidate) { return Candidate.PlayerCount > 0; });
    const FPSMenuScreenDef* TeamScreen = Menu->GetCatalog().FindScreenWithContent(EPSMenuScreenContent::TeamSelect);
    if (TestNotNull(TEXT("A team has a roster"), Team) && TestNotNull(TEXT("Team select exists"), TeamScreen))
    {
        TestTrue(TEXT("The roster's average size is a player's"), Team->AverageWeightKg > 50.f && Team->AverageHeightCm > 150.f);
        const FName TeamId = Team->TeamId;
        const FName TeamScreenId = TeamScreen->ScreenId;
        auto TeamDetail = [Menu, TeamId, TeamScreenId]()
        {
            const FPSMenuScreenDef Presented = Menu->GetPresentedScreen(TeamScreenId);
            const FPSMenuOptionDef* Option = Presented.Options.FindByPredicate([TeamId](const FPSMenuOptionDef& Candidate) { return Candidate.OptionId == TeamId; });
            return Option ? Option->Detail : FString();
        };
        const FString Imperial = TeamDetail();
        TestTrue(TEXT("Team select shows the roster in pounds"), Imperial.Contains(UPSLocalization::FormatWeight(Team->AverageWeightKg, EPSUnitSystem::Imperial).ToString()));
        TestTrue(TEXT("...and feet and inches"), Imperial.Contains(UPSLocalization::FormatHeight(Team->AverageHeightCm, EPSUnitSystem::Imperial).ToString()));
        Settings->SetValue(UPSLocalization::UnitsSettingId, static_cast<float>(static_cast<uint8>(EPSUnitSystem::Metric)));
        TestEqual(TEXT("The setting switches to metric"), UPSLocalization::GetUnitSystem(Settings), EPSUnitSystem::Metric);
        const FString Metric = TeamDetail();
        TestTrue(TEXT("...and team select follows, in kilograms"), Metric.Contains(UPSLocalization::FormatWeight(Team->AverageWeightKg, EPSUnitSystem::Metric).ToString()));
        TestTrue(TEXT("...and centimeters"), Metric.Contains(UPSLocalization::FormatHeight(Team->AverageHeightCm, EPSUnitSystem::Metric).ToString()));
    }

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Pseudo-localization
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPseudoLocalizationTest,
    "PlaySports.Localization.PseudoLocalizedUI",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPseudoLocalizationTest::RunTest(const FString& Parameters)
{
    using namespace PSLocalizationTests;

    // The transform: letters accented, placeholders kept, a third longer, bracketed.
    const FString Pattern = TEXT("Press {Action} now");
    const FString Pseudo = UPSLocalization::PseudoLocalize(Pattern);
    TestTrue(TEXT("Pseudo text is bracketed"), Pseudo.StartsWith(TEXT("[")) && Pseudo.EndsWith(TEXT("]")));
    TestTrue(TEXT("...keeps its placeholders"), Pseudo.Contains(TEXT("{Action}")));
    TestTrue(TEXT("...grows by a third"), Pseudo.Len() >= Pattern.Len() + 2 + Pattern.Len() / 3);
    TestTrue(TEXT("...and has no plain letter outside them"), UPSLocalization::IsFullyLocalized(Pseudo.Replace(TEXT("{Action}"), TEXT(""))));
    TestFalse(TEXT("Plain text is caught"), UPSLocalization::IsFullyLocalized(TEXT("Reset to defaults")));

    UWorld* World = CreateTestWorld();
    APSPlayerController* Controller = SpawnController(World);
    UPSUIAccessibilitySubsystem* Accessibility = World ? World->GetSubsystem<UPSUIAccessibilitySubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("Accessibility subsystem"), Accessibility))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSettingsSubsystem* Settings = MakeSettings();
    UPSMenuComponent* Menu = Controller->GetMenuComponent();
    Menu->SetSettings(Settings);
    Accessibility->SetSettings(Settings);
    Settings->SetValue(Accessibility->NarrationSettingId, 1.f);
    TArray<FString> Said;
    const FDelegateHandle Handle = Accessibility->OnNarrationMC.AddLambda([&Said](const FString& Text) { Said.Add(Text); });

    UPSLocalization::SetPseudoLocalization(true);
    TestTrue(TEXT("Pseudo-localization is on"), UPSLocalization::IsPseudoLocalization());
    TestTrue(TEXT("Verbatim text is marked, not translated"), UPSLocalization::IsFullyLocalized(UPSLocalization::Verbatim(TEXT("Falcons")).ToString())
        && UPSLocalization::Verbatim(TEXT("Falcons")).ToString().Contains(TEXT("Falcons")));

    TArray<FString> Plain;
    auto Check = [&Plain](const FString& Where, const FString& Shown)
    {
        if (!Shown.IsEmpty() && !UPSLocalization::IsFullyLocalized(Shown))
        {
            Plain.Add(FString::Printf(TEXT("%s: \"%s\""), *Where, *Shown));
        }
    };
    auto CheckScreen = [&Check](const FPSMenuScreenDef& Shown)
    {
        const FString Where = Shown.ScreenId.ToString();
        Check(Where + TEXT(" title"), Shown.Title);
        Check(Where + TEXT(" body"), Shown.Body);
        for (const FPSMenuOptionDef& ShownOption : Shown.Options)
        {
            Check(Where + TEXT(" ") + ShownOption.OptionId.ToString(), ShownOption.Label);
            Check(Where + TEXT(" ") + ShownOption.OptionId.ToString() + TEXT(" detail"), ShownOption.Detail);
        }
    };

    // Play calling (Epic 102) on a first down, so its screens have a situation to show.
    UPSPlayCallSubsystem* PlayCall = World->GetSubsystem<UPSPlayCallSubsystem>();
    FPSSituationContext FirstAndTen;
    FirstAndTen.Down = 1;
    FirstAndTen.Distance = 10;
    FirstAndTen.YardLine = 20;
    if (TestNotNull(TEXT("Play-call subsystem"), PlayCall))
    {
        PlayCall->OpenPlayCall(FirstAndTen);
    }

    // Every screen as presented.
    for (const FPSMenuScreenDef& Authored : Menu->GetCatalog().Screens)
    {
        CheckScreen(Menu->GetPresentedScreen(Authored.ScreenId));
    }

    // Each formation's plays, both sides' readouts, the adjustments and every kind of spot.
    if (PlayCall)
    {
        for (const bool bOffense : { true, false })
        {
            Check(TEXT("call screen body"), PlayCall->BuildCallScreenBody(bOffense));
            TArray<FPSMenuOptionDef> PlayCallOptions = PlayCall->BuildSuggestionOptions(bOffense);
            PlayCallOptions.Append(PlayCall->BuildFormationOptions(bOffense, NAME_None));
            for (const FString& Formation : PlayCall->GetFormations(bOffense))
            {
                PlayCallOptions.Append(PlayCall->BuildPlayOptions(Formation, bOffense));
            }
            PlayCallOptions.Append(PlayCall->BuildAdjustmentOptions());
            for (const FPSMenuOptionDef& PlayCallOption : PlayCallOptions)
            {
                Check(PlayCallOption.OptionId.ToString(), PlayCallOption.Label);
                Check(PlayCallOption.OptionId.ToString() + TEXT(" detail"), PlayCallOption.Detail);
            }
        }
        Check(TEXT("adjustment body"), PlayCall->BuildAdjustmentScreenBody());
        for (const int32 YardLine : { 20, 50, 80 })
        {
            FPSSituationContext Spot = FirstAndTen;
            Spot.YardLine = YardLine;
            Check(TEXT("situation"), UPSPlayCallSubsystem::DescribeSituation(Spot));
        }
        FPSSituationContext Kickoff = FirstAndTen;
        Kickoff.bKickoff = true;
        Check(TEXT("kickoff"), UPSPlayCallSubsystem::DescribeSituation(Kickoff));
    }

    // Each settings category, the remap prompt, and what narration says on the way.
    Menu->OpenRootScreen();
    Menu->ChooseOption(TEXT("Settings"));
    for (const FPSSettingCategoryDef& Category : Settings->GetCatalog().Categories)
    {
        Menu->ChooseOption(Category.CategoryId);
        const FPSMenuScreenDef CategoryScreen = Menu->GetPresentedScreen(Menu->GetTopScreenId());
        CheckScreen(CategoryScreen);
        if (CategoryScreen.Options.Num() > 0)
        {
            Menu->NarrateOption(CategoryScreen.Options[0].OptionId);
        }
        Menu->HandleBack();
    }
    const FPSMenuScreenDef* RemapScreen = Menu->GetCatalog().FindScreenWithContent(EPSMenuScreenContent::InputRemap);
    if (TestNotNull(TEXT("The remap screen exists"), RemapScreen))
    {
        Menu->BeginRemap(TEXT("Juke"));
        CheckScreen(Menu->GetPresentedScreen(RemapScreen->ScreenId));
    }
    TestTrue(TEXT("Narration spoke"), Said.Num() > 0);
    for (const FString& Line : Said)
    {
        Check(TEXT("narration"), Line);
    }

    // Setting values, captions, HUD banners, units and a loading tip.
    for (const FPSSettingDef& Def : Settings->GetCatalog().Settings)
    {
        Check(Def.SettingId.ToString(), Settings->FormatValue(Def.SettingId).ToString());
    }
    FPSCaptionLine Caption;
    Caption.Speaker = UPSLocalization::PseudoLocalize(TEXT("Referee"));
    Caption.Text = UPSLocalization::PseudoLocalize(TEXT("Holding, offense."));
    Check(TEXT("caption"), UPSUIAccessibilitySubsystem::FormatCaption(Caption));
    Check(TEXT("yards gained"), UPSPlayResultWidget::MakeYardsBanner(7).ToString());
    Check(TEXT("yards lost"), UPSPlayResultWidget::MakeYardsBanner(-3).ToString());
    Check(TEXT("touchdown"), UPSPlayResultWidget::MakeScoreBanner(TEXT("Touchdown")).ToString());
    Check(TEXT("incomplete"), UPSPlayResultWidget::MakeIncompletePassBanner().ToString());
    Check(TEXT("game clock"), UPSScoreboardWidget::MakeGameClockText(905.f).ToString());
    for (const TCHAR* Phase : { TEXT("PreSnap"), TEXT("Snap"), TEXT("PassRush"), TEXT("BallCarrierMovement"), TEXT("Scoring"), TEXT("Kickoff"), TEXT("Punt"), TEXT("FieldGoal") })
    {
        Check(Phase, UPSScoreboardWidget::MakePhaseText(Phase).ToString());
    }
    for (const EPSUnitSystem Units : { EPSUnitSystem::Imperial, EPSUnitSystem::Metric })
    {
        Check(TEXT("weight"), UPSLocalization::FormatWeight(110.f, Units).ToString());
        Check(TEXT("height"), UPSLocalization::FormatHeight(190.f, Units).ToString());
    }
    UPSLoadingTips* Tips = NewObject<UPSLoadingTips>();
    Tips->EnsureLoaded();
    Check(TEXT("loading tip"), Tips->NextTip(UPSLoadingTips::AnyContext));

    // The broadcast overlays (Track A): the score bug and its chyrons, the kick readout, the
    // position badges and the personnel panels.
    UPSOverlayBroadcastSubsystem* Broadcast = World->GetSubsystem<UPSOverlayBroadcastSubsystem>();
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    if (TestNotNull(TEXT("Broadcast overlay"), Broadcast) && TestNotNull(TEXT("Bus"), Bus))
    {
        auto CheckBug = [&Check](const FPSScoreBugState& Bug)
        {
            Check(TEXT("score bug home"), Bug.HomeLabel);
            Check(TEXT("score bug away"), Bug.AwayLabel);
            Check(TEXT("score bug quarter"), Bug.QuarterText);
            Check(TEXT("score bug clock"), Bug.GameClockText);
            Check(TEXT("score bug play clock"), UPSOverlayScoreBugWidget::MakePlayClockText(Bug.PlayClockText).ToString());
            Check(TEXT("score bug down and distance"), Bug.SituationText);
            Check(TEXT("score bug side"), UPSOverlayScoreBugWidget::MakeTeamScoreText(Bug.HomeLabel, Bug.HomeScore, true).ToString());
            Check(TEXT("score bug timeouts"), UPSOverlayScoreBugWidget::MakeTimeoutPips(Bug.HomeTimeouts).ToString());
        };
        auto CheckChyrons = [&Check, Broadcast]()
        {
            FPSChyron Shown;
            if (Broadcast->GetCurrentChyron(Shown))
            {
                Check(TEXT("chyron headline"), Shown.Headline);
                Check(TEXT("chyron detail"), Shown.Detail);
            }
            for (const FPSChyron& Waiting : Broadcast->GetQueuedChyrons())
            {
                Check(TEXT("waiting chyron headline"), Waiting.Headline);
                Check(TEXT("waiting chyron detail"), Waiting.Detail);
            }
        };

        // The theme's side labels, then known teams' abbreviations (names).
        Broadcast->SetOverlayDetail(EPSOverlayDetail::Full);
        Broadcast->SetTheme(Broadcast->GetTheme());
        FPSTelemetryGameStateEvent State;
        State.Phase = TEXT("PreSnap");
        State.GameClockSeconds = 905.f;
        State.bPlayClockRunning = true;
        State.PlayClockSeconds = 25.f;
        Bus->PublishGameState(State);
        CheckBug(Broadcast->GetScoreBug());
        TArray<FPSTeamSummary> Teams;
        TArray<FString> TeamErrors;
        UPSUITeamCatalog::BuildSummaries(UPSUITeamCatalog::GetDefaultTeamsPath(), Teams, TeamErrors);
        if (Teams.Num() >= 2)
        {
            Broadcast->SetTeams(Teams[0].TeamId, Teams[1].TeamId);
            CheckBug(Broadcast->GetScoreBug());
        }
        for (int32 Quarter = 1; Quarter <= 5; ++Quarter)
        {
            Check(TEXT("quarter"), PSGameStateEvents::QuarterLabel(Quarter));
        }
        State.Phase = TEXT("Kickoff");
        Bus->PublishGameState(State);
        CheckBug(Broadcast->GetScoreBug());

        // Points with a finished drive, then a field goal, a sack, a run and a pick.
        State.Phase = TEXT("PreSnap");
        State.HomeScore = 7;
        State.CompletedDrives = 1;
        State.LastDrivePlays = 8;
        State.LastDriveYards = 75;
        State.LastDriveResult = TEXT("Touchdown");
        State.bHomeHasPossession = false;
        Bus->PublishGameState(State);
        CheckChyrons();
        FPSTelemetryScoreEvent FieldGoal;
        FieldGoal.ScoreType = TEXT("FieldGoal");
        FieldGoal.Points = 3;
        FieldGoal.HomeScore = 10;
        Bus->PublishScore(FieldGoal);
        CheckChyrons();
        FPSTelemetryTackleEvent Sack;
        Sack.TacklerName = TEXT("DE_1");
        Sack.BallCarrierName = TEXT("QB_1");
        Sack.YardsGained = -7;
        Sack.bIsSack = true;
        Bus->PublishTackle(Sack);
        CheckChyrons();
        FPSTelemetryTackleEvent Run;
        Run.BallCarrierName = TEXT("RB_1");
        Run.YardsGained = 12;
        Bus->PublishTackle(Run);
        FPSTelemetryCatchEvent Pick;
        Pick.ReceiverName = TEXT("CB_1");
        Pick.bIsInterception = true;
        Bus->PublishCatch(Pick);
        CheckChyrons();

        // Every other line the chyrons can carry.
        for (const int32 Points : { 1, 2, 3, 4, 6, 7 })
        {
            Check(TEXT("score headline"), UPSOverlayBroadcastSubsystem::MakeScoreHeadline(Points, FString()));
        }
        Check(TEXT("two-point headline"), UPSOverlayBroadcastSubsystem::MakeScoreHeadline(2, TEXT("TwoPointConversion")));
        Check(TEXT("unknown score headline"), UPSOverlayBroadcastSubsystem::MakeScoreHeadline(3, TEXT("Rouge")));
        for (const TCHAR* Result : { TEXT("Touchdown"), TEXT("Safety"), TEXT("Turnover on Downs"), TEXT("EPSKickResult::Blocked"), TEXT("") })
        {
            Check(TEXT("drive detail"), UPSOverlayBroadcastSubsystem::MakeDriveDetail(5, 40, Result));
        }
        for (const int32 Yards : { 12, 0, -3 })
        {
            Check(TEXT("gain"), UPSOverlayBroadcastSubsystem::MakeGainText(Yards));
        }
        Check(TEXT("drive headline"), UPSOverlayBroadcastSubsystem::MakeDriveHeadline(Broadcast->GetScoreBug().HomeLabel));
        Broadcast->SetTeams(NAME_None, NAME_None);
    }

    // The kick readout.
    if (const UPSOverlayBallFlightSubsystem* BallFlight = World->GetSubsystem<UPSOverlayBallFlightSubsystem>())
    {
        for (const EPSKickVerdict Verdict : { EPSKickVerdict::Good, EPSKickVerdict::WideLeft, EPSKickVerdict::WideRight, EPSKickVerdict::Short })
        {
            Check(TEXT("kick readout"), UPSOverlayBallFlightSubsystem::LocalizedKickLabel(BallFlight->GetStyle(), Verdict));
        }
    }

    // The position badges: role labels, and a button's name as the device writes it.
    const FPSOverlayBadgeStyle BadgeStyle = Controller->GetOverlayBadgeComponent()->GetStyle();
    for (const FPSBadgeRoleLabel& Entry : BadgeStyle.RoleLabels)
    {
        Check(TEXT("badge"), UPSOverlayBadgeComponent::LocalizedRoleLabel(BadgeStyle, Entry.Role));
    }
    Check(TEXT("badge button"), UPSLocalization::Verbatim(TEXT("RB")).ToString());

    // The personnel panels: counts, and names from the catalog and by rule.
    if (UPSOverlayPersonnelSubsystem* Personnel = World->GetSubsystem<UPSOverlayPersonnelSubsystem>())
    {
        const FPSPersonnelPanelStyle PanelStyle = Personnel->GetStyle();
        const FPSPersonnelCatalog PersonnelCatalog = Personnel->GetCatalog();
        TArray<FString> Counts;
        for (const FPSPersonnelRoleLabel& Entry : PanelStyle.OffenseRoles)
        {
            Counts.Add(UPSOverlayPersonnelSubsystem::FormatCount(UPSOverlayPersonnelSubsystem::LocalizedRoleLabel(Entry), 1));
        }
        for (const FPSPersonnelRoleLabel& Entry : PanelStyle.DefenseRoles)
        {
            Counts.Add(UPSOverlayPersonnelSubsystem::FormatCount(UPSOverlayPersonnelSubsystem::LocalizedRoleLabel(Entry), 4));
        }
        Check(TEXT("personnel counts"), FString::Join(Counts, *UPSOverlayPersonnelSubsystem::CountSeparator()));

        auto Lineup = [](int32 RB, int32 TE, int32 WR, int32 DL, int32 LB, int32 DB)
        {
            TMap<EPlayerRole, int32> Out;
            Out.Add(EPlayerRole::RunningBack, RB);
            Out.Add(EPlayerRole::TightEnd, TE);
            Out.Add(EPlayerRole::WideReceiver, WR);
            Out.Add(EPlayerRole::DefensiveLineman, DL);
            Out.Add(EPlayerRole::Linebacker, LB);
            Out.Add(EPlayerRole::DefensiveBack, DB);
            return Out;
        };
        TMap<EPlayerRole, int32> Offense = Lineup(1, 1, 3, 0, 0, 0);
        Offense.Add(EPlayerRole::Quarterback, 1);
        Offense.Add(EPlayerRole::OffensiveLineman, 5);
        Check(TEXT("catalog package"), UPSOverlayPersonnelSubsystem::NamePackage(Offense, true, PersonnelCatalog, PanelStyle));
        Offense.Add(EPlayerRole::TightEnd, 3);
        Offense.Add(EPlayerRole::WideReceiver, 1);
        Check(TEXT("offense by rule"), UPSOverlayPersonnelSubsystem::NamePackage(Offense, true, PersonnelCatalog, PanelStyle));
        Check(TEXT("catalog defense"), UPSOverlayPersonnelSubsystem::NamePackage(Lineup(0, 0, 0, 4, 2, 5), false, PersonnelCatalog, PanelStyle));
        Check(TEXT("defense by backs"), UPSOverlayPersonnelSubsystem::NamePackage(Lineup(0, 0, 0, 3, 3, 5), false, PersonnelCatalog, PanelStyle));
        Check(TEXT("defense by fallback"), UPSOverlayPersonnelSubsystem::NamePackage(Lineup(0, 0, 0, 5, 3, 3), false, PersonnelCatalog, PanelStyle));
    }

    TestEqual(TEXT("Everything shown came through the string tables"), Plain.Num(), 0);
    for (const FString& Entry : Plain)
    {
        AddInfo(FString::Printf(TEXT("Plain under pseudo-localization: %s"), *Entry));
    }
    TestFalse(TEXT("A string changed since the table was generated would be caught"), UPSLocalization::IsFullyLocalized(
        UPSLocalization::GetDataText(UPSLocalization::MenuKey(Menu->GetCatalog().RootScreen, TEXT("Title")), TEXT("A newer title")).ToString()));

    UPSLocalization::SetPseudoLocalization(false);
    TestEqual(TEXT("Off again, text is plain"), UPSLocalization::GetText(TEXT("Menu.ResetToDefaults")).ToString(), FString(TEXT("Reset to defaults")));
    Accessibility->OnNarrationMC.Remove(Handle);
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
