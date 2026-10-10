// PSPlayCallAdjustmentTests.cpp -- Epic 102 part 3 (defensive adjustments, favourite plays)
//
// Tests covered:
//   1. A human defense calls a front and coverage, then picks a pre-snap adjustment that
//      reaches the AI defenders at the snap; the authored adjustments validate and every
//      validation rule fires on a broken table.
//   2. Favourite plays: starring and unstarring from the play lists, the Favorites screen,
//      the star in every list, and the profile save that keeps them between sessions.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSMenuComponent.h"
#include "PSPlayCallComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSProfileSaveGame.h"
#include "PSSaveSubsystem.h"
#include "PSTelemetryBus.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlayCallAdjustmentTests
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

    static APSPlayerPawn* SpawnPawn(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        if (Pawn)
        {
            FPlayerAttributes Attributes;
            Attributes.PlayerId = FName(PlayerId);
            Attributes.DisplayName = PlayerId;
            Attributes.Role = Role;
            Pawn->InitializePlayer(Attributes);
        }
        return Pawn;
    }

    /** A player controller holding Target, bound to the play-call authority. */
    static APSPlayerController* SpawnHuman(UWorld* World, APSPlayerPawn* Target)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        if (Controller && Target)
        {
            Controller->GetPlayCallComponent()->BindToPlayCall();
            Controller->TakeControlOf(Target);
        }
        return Controller;
    }

    static FPSSituationContext FirstAndTen()
    {
        FPSSituationContext Situation;
        Situation.YardLine = 25;
        return Situation;
    }

    static bool HasProblem(const TArray<FString>& Problems, const TCHAR* Fragment)
    {
        return Problems.ContainsByPredicate([Fragment](const FString& Problem) { return Problem.Contains(Fragment); });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Defensive call flow with a pre-snap adjustment
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDefensiveAdjustmentTest,
    "PlaySports.PlayCall.DefensiveAdjustments",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDefensiveAdjustmentTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayCallAdjustmentTests;

    // The authored table validates; every rule fires on a broken one.
    FPSDefensiveAdjustmentCatalog FromFile;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    TestTrue(TEXT("defensive_adjustments.json loads through UPSDataIngestion"),
        Ingestion->LoadDefensiveAdjustmentsFromJson(UPSPlayCallSubsystem::GetDefaultAdjustmentsPath(), FromFile));
    for (const FString& Problem : UPSPlayCallSubsystem::ValidateAdjustments(FromFile))
    {
        AddError(FString::Printf(TEXT("defensive_adjustments.json: %s"), *Problem));
    }
    if (FromFile.Adjustments.Num() > 0)
    {
        FPSDefensiveAdjustmentCatalog Broken = FromFile;
        FPSDefensiveAdjustmentDef Bad = Broken.Adjustments[0];
        Bad.Role = EPlayerRole::Quarterback;
        Bad.Kind = EPSAssignmentKind::Route;
        Bad.Label.Empty();
        Broken.Adjustments.Add(Bad);
        const TArray<FString> Problems = UPSPlayCallSubsystem::ValidateAdjustments(Broken);
        TestTrue(TEXT("A duplicate ID is reported"), HasProblem(Problems, TEXT("duplicate AdjustmentId")));
        TestTrue(TEXT("A missing label is reported"), HasProblem(Problems, TEXT("has no Label")));
        TestTrue(TEXT("A role that isn't a defender is reported"), HasProblem(Problems, TEXT("is not a defender")));
        TestTrue(TEXT("A kind that isn't a defensive assignment is reported"), HasProblem(Problems, TEXT("is not a defensive assignment")));
    }

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }

    // The AI linebacker spawns first so it takes the play's linebacker assignment.
    APSPlayerPawn* AILinebacker = SpawnPawn(World, EPlayerRole::Linebacker, TEXT("LB_AI"));
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSDefenseController* AIController = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (AILinebacker && AIController)
    {
        AIController->Possess(AILinebacker);
    }
    APSPlayerPawn* HumanLinebacker = SpawnPawn(World, EPlayerRole::Linebacker, TEXT("LB_HUMAN"));
    APSPlayerController* Controller = SpawnHuman(World, HumanLinebacker);
    UPSPlayCallSubsystem* PlayCall = World->GetSubsystem<UPSPlayCallSubsystem>();
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    if (!TestNotNull(TEXT("AI linebacker"), AIController) || !TestNotNull(TEXT("Human controller"), Controller) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall)
        || !TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Menu"), Menu))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestFalse(TEXT("This human calls for the defense"), Controller->GetPlayCallComponent()->IsCallingForOffense());

    PlayCall->OpenPlayCall(FirstAndTen());
    PlayCall->PollReadyToSnap(0.f);
    TestTrue(TEXT("The CPU offense has called"), PlayCall->GetCall(true).IsSet());
    TestFalse(TEXT("No adjustment before the defense calls"), PlayCall->SetDefensiveAdjustment(TEXT("SendLinebacker")));
    TestTrue(TEXT("The defense's call screen is open"), Menu->IsPlayCallScreenOpen());

    // Front, then coverage, then the adjustment.
    Menu->ChooseOption(TEXT("Base 4-3"));
    Menu->ChooseOption(TEXT("Defense_43Cover2"));
    TestTrue(TEXT("The defense called 4-3 Cover 2"), PlayCall->GetCall(false).PlayId == FName(TEXT("Defense_43Cover2")));
    const FPSMenuScreenDef Adjust = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    TestTrue(TEXT("A defensive call goes on to the adjustments"), Adjust.Content == EPSMenuScreenContent::PlayCallAdjustments);
    TestTrue(TEXT("...which show the offense's formation"), Adjust.Body.StartsWith(TEXT("Offense lines up in")));
    TestEqual(TEXT("...and offer no adjustment plus each authored one"), Adjust.Options.Num(), 1 + PlayCall->GetAdjustments().Num());

    Menu->ChooseOption(TEXT("SendLinebacker"));
    TestFalse(TEXT("Choosing an adjustment closes the screens"), Menu->IsMenuOpen());
    TestEqual(TEXT("The adjustment is set"), PlayCall->GetDefensiveAdjustment(), FName(TEXT("SendLinebacker")));

    FPSPlayDefinition ToRun;
    TestTrue(TEXT("The defense has a play to run"), PlayCall->GetDefensivePlayToRun(ToRun));
    TestTrue(TEXT("The linebacker's run fit became a blitz"),
        ToRun.Assignments.ContainsByPredicate([](const FPSPlayAssignment& Assignment) { return Assignment.Role == EPlayerRole::Linebacker && Assignment.Kind == EPSAssignmentKind::Blitz; }));
    TestTrue(TEXT("The AI linebacker starts on its run fit"), AIController->GetAssignment() == EPSDefensiveAssignmentType::RunFit);

    FPSTelemetrySnapEvent Snap;
    Snap.LineOfScrimmage = FVector(2500.f, 0.f, 0.f);
    Bus->PublishSnap(Snap);
    TestTrue(TEXT("At the snap the AI linebacker blitzes"), AIController->GetAssignment() == EPSDefensiveAssignmentType::PassRush);
    TestFalse(TEXT("No adjustment after the snap"), PlayCall->SetDefensiveAdjustment(TEXT("ManUp")));

    PlayCall->OpenPlayCall(FirstAndTen());
    TestTrue(TEXT("A new down starts without an adjustment"), PlayCall->GetDefensiveAdjustment().IsNone());

    Controller->GetPlayCallComponent()->UnbindFromPlayCall();
    Controller->ReleaseControl();
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Favourite plays
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFavoritePlaysTest,
    "PlaySports.PlayCall.FavoritePlays",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFavoritePlaysTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayCallAdjustmentTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world created"), World))
    {
        return false;
    }
    APSPlayerPawn* QB = SpawnPawn(World, EPlayerRole::Quarterback, TEXT("QB_TEST"));
    APSPlayerController* Controller = SpawnHuman(World, QB);
    UPSPlayCallSubsystem* PlayCall = World->GetSubsystem<UPSPlayCallSubsystem>();
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    if (!TestNotNull(TEXT("Human controller"), Controller) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Menu"), Menu))
    {
        DestroyTestWorld(World);
        return false;
    }

    const FName SlantFlat(TEXT("Offense_SlantFlat"));
    TestFalse(TEXT("Nothing starred yet"), PlayCall->IsFavorite(SlantFlat));
    TestTrue(TEXT("Starring Slant-Flat"), PlayCall->ToggleFavorite(SlantFlat));
    TestTrue(TEXT("...makes it a favourite"), PlayCall->IsFavorite(SlantFlat));
    TestEqual(TEXT("It is the offense's only favourite"), PlayCall->GetFavorites(true).Num(), 1);
    TestEqual(TEXT("The defense has none"), PlayCall->GetFavorites(false).Num(), 0);
    TestFalse(TEXT("An unknown play can't be starred"), PlayCall->ToggleFavorite(TEXT("Offense_Nope")));

    // The call screen offers the favourites once there are any.
    PlayCall->OpenPlayCall(FirstAndTen());
    const FPSMenuScreenDef Formations = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    TestEqual(TEXT("Suggestion, favourites, then formations"), Formations.Options.Num(), 2 + PlayCall->GetFormations(true).Num());
    TestTrue(TEXT("A formation is not a play to star"), !Menu->ToggleFavoriteOption(TEXT("Trips Right")));

    Menu->ChooseOption(TEXT("Favorites"));
    FPSMenuScreenDef Favorites = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    if (TestEqual(TEXT("The Favorites screen lists Slant-Flat"), Favorites.Options.Num(), 1))
    {
        TestTrue(TEXT("...marked with a star"), Favorites.Options[0].Label.StartsWith(TEXT("* ")));
    }

    TestTrue(TEXT("Unstarring from the Favorites screen"), Menu->ToggleFavoriteOption(SlantFlat));
    TestFalse(TEXT("...removes the favourite"), PlayCall->IsFavorite(SlantFlat));
    Favorites = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    TestEqual(TEXT("...and the list empties"), Favorites.Options.Num(), 0);

    // Star it again from the formation's play list; the star shows there too.
    Menu->HandleBack();
    Menu->ChooseOption(TEXT("Trips Right"));
    TestTrue(TEXT("Starring from a play list"), Menu->ToggleFavoriteOption(SlantFlat));
    const FPSMenuScreenDef Plays = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    const FPSMenuOptionDef* Starred = Plays.Options.FindByPredicate([SlantFlat](const FPSMenuOptionDef& Option) { return Option.OptionId == SlantFlat; });
    TestTrue(TEXT("The play list shows the star"), Starred && Starred->Label.StartsWith(TEXT("* ")));

    Controller->GetPlayCallComponent()->UnbindFromPlayCall();
    Controller->ReleaseControl();
    DestroyTestWorld(World);

    // The profile save keeps favourites between sessions.
    UPSSaveSubsystem* Saves = NewObject<UPSSaveSubsystem>(NewObject<UGameInstance>());
    UPSProfileSaveGame* Profile = NewObject<UPSProfileSaveGame>();
    Profile->FavoritePlays = { SlantFlat, FName(TEXT("Defense_43Cover2")) };
    const FString Slot = TEXT("Test_ProfileFavorites");
    TestTrue(TEXT("The profile saves"), Saves->SaveToSlot(Profile, Slot));
    const UPSProfileSaveGame* Loaded = Cast<UPSProfileSaveGame>(Saves->LoadFromSlot(Slot));
    if (TestNotNull(TEXT("The profile loads back as a profile"), Loaded))
    {
        TestEqual(TEXT("Favourites survive the round trip"), Loaded->FavoritePlays.Num(), 2);
        TestTrue(TEXT("...as the Profile category"), Loaded->Category == EPSSaveCategory::Profile);
    }
    IFileManager::Get().Delete(*UPSSaveSubsystem::GetSlotPath(Slot), false, true, true);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
