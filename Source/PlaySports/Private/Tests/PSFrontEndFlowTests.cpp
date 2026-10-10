// PSFrontEndFlowTests.cpp -- Epic 101 stories 3 and 4 (team select, loading screens and tips)
//
// Tests covered:
//   1. Team summaries: every team in Data/sample_teams.json loads with its identity and
//      roster-derived ratings; a rating matches an independent recomputation from the roster;
//      hex colors parse and bad ones are refused.
//   2. Loading tips: the authored tips validate; a mode's tips come out without repeats until
//      every eligible tip has shown, never a tip for another mode, and a seed repeats the order;
//      a broken tips file reports each mistake.
//   3. Flow: Play Now opens team select with one option per team (color, ratings), choosing a
//      team shows the Loading screen with a tip for the mode and queues travel with the team.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSLoadingTips.h"
#include "PSMenuComponent.h"
#include "PSMenuStack.h"
#include "PSPlayerController.h"
#include "PSUITeamCatalog.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Team summaries from team data and rosters
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTeamSummariesTest,
    "PlaySports.UI.TeamSummariesFromRosters",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTeamSummariesTest::RunTest(const FString& Parameters)
{
    TArray<FPSTeamSummary> Summaries;
    TArray<FString> Errors;
    TestTrue(TEXT("Summaries build"), UPSUITeamCatalog::BuildSummaries(UPSUITeamCatalog::GetDefaultTeamsPath(), Summaries, Errors));
    for (const FString& Error : Errors)
    {
        AddError(FString::Printf(TEXT("team data: %s"), *Error));
    }
    TestTrue(TEXT("At least four teams"), Summaries.Num() >= 4);

    for (const FPSTeamSummary& Team : Summaries)
    {
        const FString Name = Team.TeamId.ToString();
        TestTrue(*FString::Printf(TEXT("%s has players"), *Name), Team.PlayerCount > 0);
        TestTrue(*FString::Printf(TEXT("%s overall is 1-100"), *Name), Team.Overall > 0 && Team.Overall <= 100);
        TestTrue(*FString::Printf(TEXT("%s has offense and defense ratings"), *Name), Team.Offense > 0 && Team.Defense > 0);
        TestFalse(*FString::Printf(TEXT("%s has an abbreviation"), *Name), Team.Abbreviation.IsEmpty());
        TestTrue(*FString::Printf(TEXT("%s primary color is opaque"), *Name), FMath::IsNearlyEqual(Team.PrimaryColor.A, 1.f));
    }

    // Recompute one team's overall straight from its roster.
    const FPSTeamSummary* Hawks = Summaries.FindByPredicate([](const FPSTeamSummary& Team) { return Team.TeamId == FName(TEXT("Hawks")); });
    if (TestNotNull(TEXT("Hawks are in the league"), Hawks))
    {
        UDataTable* Roster = NewObject<UDataTable>();
        Roster->RowStruct = FPlayerAttributes::StaticStruct();
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        TestTrue(TEXT("Hawks roster loads"), Ingestion->LoadPlayerAttributesFromJson(FPaths::ProjectDir() / TEXT("Data/rosters/team_hawks.json"), Roster));
        TArray<FPlayerAttributes*> Players;
        Roster->GetAllRows<FPlayerAttributes>(TEXT("Test"), Players);
        float Sum = 0.f;
        for (const FPlayerAttributes* Player : Players)
        {
            Sum += (Player->Speed + Player->Agility + Player->Strength + Player->Acceleration + Player->Awareness) / 5.f;
        }
        TestEqual(TEXT("Hawks player count matches the roster"), Hawks->PlayerCount, Players.Num());
        TestEqual(TEXT("Hawks overall is the roster mean"), Hawks->Overall, Players.Num() > 0 ? FMath::RoundToInt(Sum / Players.Num()) : 0);
    }

    FLinearColor Parsed = FLinearColor::Black;
    TestTrue(TEXT("#00FF00 parses"), UPSUITeamCatalog::ParseHexColor(TEXT("#00FF00"), Parsed));
    TestTrue(TEXT("#00FF00 is green"), Parsed.Equals(FLinearColor(0.f, 1.f, 0.f, 1.f), 0.01f));
    FLinearColor Untouched = FLinearColor::Red;
    TestFalse(TEXT("A color name is refused"), UPSUITeamCatalog::ParseHexColor(TEXT("red"), Untouched));
    TestFalse(TEXT("A short hex is refused"), UPSUITeamCatalog::ParseHexColor(TEXT("#0F0"), Untouched));
    TestFalse(TEXT("A non-hex digit is refused"), UPSUITeamCatalog::ParseHexColor(TEXT("#00GG00"), Untouched));
    TestTrue(TEXT("A refused color leaves the output alone"), Untouched.Equals(FLinearColor::Red));

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Loading tips: per-mode shuffle bag, seeding, validation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLoadingTipsTest,
    "PlaySports.UI.LoadingTipsRotate",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLoadingTipsTest::RunTest(const FString& Parameters)
{
    UPSLoadingTips* Tips = NewObject<UPSLoadingTips>();
    TestTrue(TEXT("loading_tips.json loads"), Tips->LoadFromJson(UPSLoadingTips::GetDefaultTipsPath()));
    for (const FString& Error : Tips->Validate())
    {
        AddError(FString::Printf(TEXT("loading_tips.json: %s"), *Error));
    }

    const FName Franchise(TEXT("Franchise"));
    TSet<FString> Eligible;
    for (const FPSLoadingTipDef& Tip : Tips->GetCatalog().Tips)
    {
        if (Tip.Contexts.Contains(UPSLoadingTips::AnyContext) || Tip.Contexts.Contains(Franchise))
        {
            Eligible.Add(Tip.Text);
        }
    }
    TestTrue(TEXT("Franchise has tips"), Eligible.Num() > 0);

    Tips->SetSeed(7);
    TSet<FString> Drawn;
    TArray<FString> FirstOrder;
    for (int32 Draw = 0; Draw < Eligible.Num(); ++Draw)
    {
        const FString Tip = Tips->NextTip(Franchise);
        TestTrue(TEXT("Only tips for the mode (or Any) are drawn"), Eligible.Contains(Tip));
        TestFalse(TEXT("No tip repeats before the bag is empty"), Drawn.Contains(Tip));
        Drawn.Add(Tip);
        FirstOrder.Add(Tip);
    }
    TestFalse(TEXT("The bag refills once empty"), Tips->NextTip(Franchise).IsEmpty());

    UPSLoadingTips* Replay = NewObject<UPSLoadingTips>();
    Replay->LoadFromJson(UPSLoadingTips::GetDefaultTipsPath());
    Replay->SetSeed(7);
    bool bSameOrder = true;
    for (const FString& Expected : FirstOrder)
    {
        bSameOrder &= Replay->NextTip(Franchise) == Expected;
    }
    TestTrue(TEXT("The same seed gives the same order"), bSameOrder);

    // A broken tips file reports each mistake.
    const FString BrokenPath = FPaths::ProjectSavedDir() / TEXT("Automation/PSLoadingTipsBroken.json");
    FFileHelper::SaveStringToFile(TEXT(R"({"MinimumDisplaySeconds": -1, "Tips": [
        {"TipId": "A", "Text": "Fine", "Contexts": ["Any"]},
        {"TipId": "A", "Text": "  ", "Contexts": []},
        {"TipId": "B", "Text": "Odd", "Contexts": ["Lobby"]}]})"), *BrokenPath);
    UPSLoadingTips* Broken = NewObject<UPSLoadingTips>();
    TestTrue(TEXT("Broken file still parses"), Broken->LoadFromJson(BrokenPath));
    const FString AllErrors = FString::Join(Broken->Validate(), TEXT("\n"));
    TestTrue(TEXT("Duplicate TipId reported"), AllErrors.Contains(TEXT("empty or duplicate TipId")));
    TestTrue(TEXT("Empty text reported"), AllErrors.Contains(TEXT("empty Text")));
    TestTrue(TEXT("Missing context reported"), AllErrors.Contains(TEXT("names no context")));
    TestTrue(TEXT("Unknown context reported"), AllErrors.Contains(TEXT("unknown context 'Lobby'")));
    TestTrue(TEXT("Negative display time reported"), AllErrors.Contains(TEXT("MinimumDisplaySeconds")));
    IFileManager::Get().Delete(*BrokenPath);

    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Play Now -> team select -> Loading screen with a tip and the team queued
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTeamSelectFlowTest,
    "PlaySports.UI.TeamSelectAndLoadingFlow",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTeamSelectFlowTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    if (!TestNotNull(TEXT("Controller has a menu component"), Menu))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }

    Menu->OpenRootScreen();
    Menu->ChooseOption(TEXT("Play"));
    Menu->ChooseOption(TEXT("PlayNow"));
    const FName TeamSelect = Menu->GetTopScreenId();
    TestTrue(TEXT("Play Now opens team select"), TeamSelect == FName(TEXT("TeamSelect")));

    const FPSMenuScreenDef Presented = Menu->GetPresentedScreen(TeamSelect);
    TestEqual(TEXT("One option per team"), Presented.Options.Num(), Menu->GetTeamSummaries().Num());
    for (const FPSMenuOptionDef& Option : Presented.Options)
    {
        TestTrue(TEXT("A team option starts Play Now with that team"), Option.Command == EPSMenuCommand::StartPlayNow && Option.Payload == Option.OptionId);
        TestTrue(TEXT("A team option carries the team color"), Option.AccentColor.A > 0.f);
        TestTrue(TEXT("A team option shows the ratings"), Option.Label.Contains(TEXT("OVR")) && Option.Label.Contains(TEXT("DEF")));
    }

    TestEqual(TEXT("Play Now travels with the chosen team"), Menu->BuildTravelOptions(EPSMenuCommand::StartPlayNow, TEXT("Hawks")), FString(TEXT("mode=PlayNow?team=Hawks")));
    TestTrue(TEXT("Franchise tips context"), UPSMenuComponent::GetTipContext(EPSMenuCommand::StartFranchise) == FName(TEXT("Franchise")));
    TestTrue(TEXT("Quitting to the menu uses Any tips"), UPSMenuComponent::GetTipContext(EPSMenuCommand::QuitToMainMenu) == UPSLoadingTips::AnyContext);

    // Choosing a team shows the Loading screen with a Play Now tip. Travel waits for the next
    // tick, which a test world never runs.
    Menu->ChooseOption(TEXT("Hawks"));
    const FName Loading = Menu->GetCatalog().LoadingScreen;
    TestTrue(TEXT("Choosing a team shows the Loading screen"), Menu->GetTopScreenId() == Loading);
    TestEqual(TEXT("Only the Loading screen is open"), Menu->GetStack() ? Menu->GetStack()->Depth() : 0, 1);

    const FString Tip = Menu->GetPresentedScreen(Loading).Body;
    TestFalse(TEXT("The Loading screen shows a tip"), Tip.IsEmpty());
    UPSLoadingTips* Tips = NewObject<UPSLoadingTips>();
    Tips->LoadFromJson(UPSLoadingTips::GetDefaultTipsPath());
    const bool bTipFitsMode = Tips->GetCatalog().Tips.ContainsByPredicate([&Tip](const FPSLoadingTipDef& Def)
    {
        return Def.Text == Tip && (Def.Contexts.Contains(UPSLoadingTips::AnyContext) || Def.Contexts.Contains(FName(TEXT("PlayNow"))));
    });
    TestTrue(TEXT("The tip is one for Play Now (or Any)"), bTipFitsMode);
    TestFalse(TEXT("Back can't leave the Loading screen"), Menu->HandleBack());

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
