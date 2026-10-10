// PSOverlayPersonnelTests.cpp -- Epic 29 (personnel package HUD panels)
//
// Tests covered:
//   1. The panel style loads and validates, and a bad one is caught. Naming: a package the
//      personnel catalog lists by its exact counts goes by the catalog's name (the announced one
//      first when two match); any other by the style's rules -- "13 Personnel", "Nickel" by
//      defensive backs, "5-3-3" for a count no name covers.
//   2. The panels follow the field: the broadcast frame's "RB 1 | TE 3 | WR 1" against
//      "DL 3 | LB 4 | DB 4" with their names, the side with the ball as the offense in its team's
//      label; a substitution recounts, renames, and flashes its panel and the counts it changed,
//      fading out (Full tier only); the panels go at the snap and on a Minimal tier.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSOverlayBroadcastSubsystem.h"
#include "PSOverlayPersonnelSubsystem.h"
#include "PSPersonnelManager.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSOverlayPersonnelTests
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

    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const FString& PlayerId, double X, double Y)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector(X, Y, 100.0), FRotator::ZeroRotator, SpawnParams);
        if (Pawn)
        {
            FPlayerAttributes Attributes;
            Attributes.PlayerId = FName(*PlayerId);
            Attributes.DisplayName = PlayerId;
            Attributes.Role = Role;
            Pawn->InitializePlayer(Attributes);
        }
        return Pawn;
    }

    static TMap<EPlayerRole, int32> Counts(int32 QB, int32 RB, int32 TE, int32 WR, int32 OL, int32 DL, int32 LB, int32 DB)
    {
        TMap<EPlayerRole, int32> Out;
        const int32 Values[] = { QB, RB, TE, WR, OL, DL, LB, DB };
        const EPlayerRole Roles[] = { EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::TightEnd, EPlayerRole::WideReceiver,
            EPlayerRole::OffensiveLineman, EPlayerRole::DefensiveLineman, EPlayerRole::Linebacker, EPlayerRole::DefensiveBack };
        for (int32 Index = 0; Index < 8; ++Index)
        {
            if (Values[Index] > 0)
            {
                Out.Add(Roles[Index], Values[Index]);
            }
        }
        return Out;
    }

    static void AnnounceState(UPSTelemetryBus* Bus, const TCHAR* Phase, bool bHomeBall)
    {
        FPSTelemetryGameStateEvent Event;
        Event.Phase = Phase;
        Event.bHomeHasPossession = bHomeBall;
        Bus->PublishGameState(Event);
    }

    static void AnnounceSubstitution(UPSTelemetryBus* Bus, bool bOffense, FName PackageId)
    {
        FPSTelemetryPersonnelEvent Event;
        Event.bOffense = bOffense;
        Event.PackageId = PackageId;
        Bus->PublishPersonnel(Event);
    }

    static const FPSPersonnelRoleCount* FindCount(const FPSPersonnelPanel& Panel, EPlayerRole Role)
    {
        return Panel.Counts.FindByPredicate([Role](const FPSPersonnelRoleCount& Count) { return Count.Role == Role; });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Style and naming
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPersonnelNamingTest,
    "PlaySports.Overlay.PersonnelStyleAndNaming",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPersonnelNamingTest::RunTest(const FString& Parameters)
{
    using namespace PSOverlayPersonnelTests;

    FPSPersonnelPanelStyle Style;
    FPSPersonnelCatalog Catalog;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (TestTrue(TEXT("personnel_panel.json loads"), Ingestion->LoadPersonnelPanelStyleFromJson(UPSOverlayPersonnelSubsystem::GetDefaultStylePath(), Style)))
    {
        for (const FString& Problem : UPSOverlayPersonnelSubsystem::ValidateStyle(Style))
        {
            AddError(FString::Printf(TEXT("personnel_panel.json: %s"), *Problem));
        }
    }
    FString CatalogPath = UPSPersonnelManager::GetDefaultCatalogPath();
    FPaths::CollapseRelativeDirectories(CatalogPath);
    TestTrue(TEXT("personnel_packages.json loads"), Ingestion->LoadPersonnelCatalogFromJson(CatalogPath, Catalog));

    FPSPersonnelPanelStyle Bad = Style;
    FPSPersonnelRoleLabel Wrong;
    Wrong.Role = EPlayerRole::Linebacker;
    Wrong.Label = TEXT("LB");
    Bad.OffenseRoles.Add(Wrong);
    FPSDefenseName Twice;
    Twice.DefensiveBacks = 5;
    Twice.Name = TEXT("Nickel again");
    Bad.DefenseNames.Add(Twice);
    Bad.FlashColor = TEXT("gold");
    Bad.TitleFontSize = 0;
    TestEqual(TEXT("A bad style is caught: a role on the wrong side, a count named twice, a color, a type size"),
        UPSOverlayPersonnelSubsystem::ValidateStyle(Bad).Num(), 4);

    // The catalog's names for counts it lists.
    TestEqual(TEXT("1 back, 1 tight end, 3 receivers: the catalog's 11 Personnel"),
        UPSOverlayPersonnelSubsystem::NamePackage(Counts(1, 1, 1, 3, 5, 0, 0, 0), true, Catalog, Style), FString(TEXT("11 Personnel")));
    TestEqual(TEXT("3 linemen, 4 linebackers, 4 backs: the catalog's Base 3-4"),
        UPSOverlayPersonnelSubsystem::NamePackage(Counts(0, 0, 0, 0, 0, 3, 4, 4), false, Catalog, Style), FString(TEXT("Base 3-4")));

    // The rules for counts it doesn't.
    TestEqual(TEXT("The broadcast frame's 1 back, 3 tight ends: 13 Personnel by rule"),
        UPSOverlayPersonnelSubsystem::NamePackage(Counts(1, 1, 3, 1, 5, 0, 0, 0), true, Catalog, Style), FString(TEXT("13 Personnel")));
    TestEqual(TEXT("A 3-3-5: Nickel by its five backs"),
        UPSOverlayPersonnelSubsystem::NamePackage(Counts(0, 0, 0, 0, 0, 3, 3, 5), false, Catalog, Style), FString(TEXT("Nickel")));
    TestEqual(TEXT("Three backs, a count with no name: 5-3-3"),
        UPSOverlayPersonnelSubsystem::NamePackage(Counts(0, 0, 0, 0, 0, 5, 3, 3), false, Catalog, Style), FString(TEXT("5-3-3")));

    // Two packages with the same counts: the one announced wins.
    FPSPersonnelCatalog Twins;
    FPSPersonnelPackage Ace;
    Ace.PackageId = TEXT("Ace");
    Ace.DisplayName = TEXT("Ace");
    Ace.bOffense = true;
    Ace.RoleCounts.Add(TEXT("Quarterback"), 1);
    Ace.RoleCounts.Add(TEXT("RunningBack"), 1);
    Ace.RoleCounts.Add(TEXT("TightEnd"), 2);
    Ace.RoleCounts.Add(TEXT("WideReceiver"), 2);
    Ace.RoleCounts.Add(TEXT("OffensiveLineman"), 5);
    FPSPersonnelPackage Twelve = Ace;
    Twelve.PackageId = TEXT("P12");
    Twelve.DisplayName = TEXT("12 Personnel");
    Twins.Packages.Add(Ace);
    Twins.Packages.Add(Twelve);
    const TMap<EPlayerRole, int32> TwoTightEnds = Counts(1, 1, 2, 2, 5, 0, 0, 0);
    TestEqual(TEXT("Same counts, P12 announced: 12 Personnel"),
        UPSOverlayPersonnelSubsystem::NamePackage(TwoTightEnds, true, Twins, Style, TEXT("P12")), FString(TEXT("12 Personnel")));
    TestEqual(TEXT("...Ace announced: Ace"),
        UPSOverlayPersonnelSubsystem::NamePackage(TwoTightEnds, true, Twins, Style, TEXT("Ace")), FString(TEXT("Ace")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The panels follow the field
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPersonnelPanelTest,
    "PlaySports.Overlay.PersonnelPanelsFollowTheField",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPersonnelPanelTest::RunTest(const FString& Parameters)
{
    using namespace PSOverlayPersonnelTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSOverlayPersonnelSubsystem* Personnel = World ? World->GetSubsystem<UPSOverlayPersonnelSubsystem>() : nullptr;
    UPSOverlayBroadcastSubsystem* Broadcast = World ? World->GetSubsystem<UPSOverlayBroadcastSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Personnel panels"), Personnel) || !TestNotNull(TEXT("Broadcast"), Broadcast))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Personnel->SetOverlayDetail(EPSOverlayDetail::Full);
    const FPSPersonnelPanelStyle Style = Personnel->GetStyle();

    // The broadcast frame's 22: RB 1 | TE 3 | WR 1 against DL 3 | LB 4 | DB 4.
    struct FSpot
    {
        EPlayerRole Role;
        int32 Count;
    };
    const FSpot Lineup[] = {
        { EPlayerRole::Quarterback, 1 }, { EPlayerRole::RunningBack, 1 }, { EPlayerRole::TightEnd, 3 }, { EPlayerRole::WideReceiver, 1 },
        { EPlayerRole::OffensiveLineman, 5 }, { EPlayerRole::DefensiveLineman, 3 }, { EPlayerRole::Linebacker, 4 }, { EPlayerRole::DefensiveBack, 4 },
    };
    TArray<APSPlayerPawn*> TightEnds;
    int32 Spawned = 0;
    for (const FSpot& Spot : Lineup)
    {
        for (int32 Index = 0; Index < Spot.Count; ++Index)
        {
            const FString PlayerId = FString::Printf(TEXT("%s_%d"), *UEnum::GetValueAsString(Spot.Role), Index + 1);
            APSPlayerPawn* Pawn = SpawnPlayer(World, Spot.Role, PlayerId, Spawned * 10.0, Spawned * 100.0);
            Spawned += Pawn ? 1 : 0;
            if (Pawn && Spot.Role == EPlayerRole::TightEnd)
            {
                TightEnds.Add(Pawn);
            }
        }
    }
    if (!TestEqual(TEXT("22 on the field"), Spawned, 22) || TightEnds.Num() != 3)
    {
        DestroyTestWorld(World);
        return false;
    }

    // Before the snap, the away side with the ball.
    AnnounceState(Bus, TEXT("PreSnap"), false);
    FPSPersonnelPanel Offense = Personnel->GetPanel(true);
    FPSPersonnelPanel Defense = Personnel->GetPanel(false);
    TestTrue(TEXT("Both panels up before the snap"), Offense.bVisible && Defense.bVisible);
    TestEqual(TEXT("The offense's counts"), Offense.CountsText, FString(TEXT("RB 1 | TE 3 | WR 1")));
    TestEqual(TEXT("...13 Personnel"), Offense.PackageName, FString(TEXT("13 Personnel")));
    TestEqual(TEXT("The defense's counts"), Defense.CountsText, FString(TEXT("DL 3 | LB 4 | DB 4")));
    TestEqual(TEXT("...Base 3-4"), Defense.PackageName, FString(TEXT("Base 3-4")));
    const FPSScoreBugState Bug = Broadcast->GetScoreBug();
    TestEqual(TEXT("The away side has the ball: its label on the offense"), Offense.TeamLabel, Bug.AwayLabel);
    TestTrue(TEXT("...in its color"), Offense.TeamColor.Equals(Bug.AwayColor));
    TestEqual(TEXT("The home side's on the defense"), Defense.TeamLabel, Bug.HomeLabel);
    TestEqual(TEXT("No flash yet"), Offense.FlashAlpha, 0.f);

    // A substitution: a tight end out, a receiver in -- 12 personnel.
    FPlayerAttributes Receiver;
    Receiver.PlayerId = TEXT("WR_SUB");
    Receiver.DisplayName = TEXT("WR_SUB");
    Receiver.Role = EPlayerRole::WideReceiver;
    TightEnds[0]->InitializePlayer(Receiver);
    AnnounceSubstitution(Bus, true, TEXT("P12"));
    Offense = Personnel->GetPanel(true);
    TestEqual(TEXT("Recounted"), Offense.CountsText, FString(TEXT("RB 1 | TE 2 | WR 2")));
    TestEqual(TEXT("...12 Personnel"), Offense.PackageName, FString(TEXT("12 Personnel")));
    TestEqual(TEXT("The offense's panel flashes"), Offense.FlashAlpha, 1.f, 0.001f);
    const FPSPersonnelRoleCount* Backs = FindCount(Offense, EPlayerRole::RunningBack);
    const FPSPersonnelRoleCount* Ends = FindCount(Offense, EPlayerRole::TightEnd);
    const FPSPersonnelRoleCount* Wideouts = FindCount(Offense, EPlayerRole::WideReceiver);
    if (TestNotNull(TEXT("RB count"), Backs) && TestNotNull(TEXT("TE count"), Ends) && TestNotNull(TEXT("WR count"), Wideouts))
    {
        TestTrue(TEXT("The tight ends and receivers changed"), Ends->bChanged && Wideouts->bChanged);
        TestFalse(TEXT("The backs didn't"), Backs->bChanged);
    }
    TestEqual(TEXT("The defense doesn't flash"), Personnel->GetPanel(false).FlashAlpha, 0.f);
    Personnel->AdvanceTime(Style.ChangeFlashSeconds * 0.5f);
    TestEqual(TEXT("Halfway through the flash"), Personnel->GetPanel(true).FlashAlpha, 0.5f, 0.01f);
    Personnel->AdvanceTime(Style.ChangeFlashSeconds);
    Offense = Personnel->GetPanel(true);
    TestEqual(TEXT("The flash is over"), Offense.FlashAlpha, 0.f);
    Ends = FindCount(Offense, EPlayerRole::TightEnd);
    TestTrue(TEXT("...and the change is no longer news"), Ends && !Ends->bChanged);

    // Below Full, no flash.
    Personnel->SetOverlayDetail(EPSOverlayDetail::Simplified);
    AnnounceSubstitution(Bus, false, TEXT("Base34"));
    TestEqual(TEXT("Simplified: a substitution doesn't flash"), Personnel->GetPanel(false).FlashAlpha, 0.f);

    // The snap takes the panels down; a Minimal tier never shows them.
    AnnounceState(Bus, TEXT("PassRush"), false);
    TestFalse(TEXT("Down at the snap"), Personnel->GetPanel(true).bVisible || Personnel->GetPanel(false).bVisible);
    AnnounceState(Bus, TEXT("PreSnap"), true);
    TestTrue(TEXT("Back before the next snap"), Personnel->GetPanel(true).bVisible);
    TestEqual(TEXT("...the home side on offense now"), Personnel->GetPanel(true).TeamLabel, Broadcast->GetScoreBug().HomeLabel);
    Personnel->SetOverlayDetail(EPSOverlayDetail::Minimal);
    TestFalse(TEXT("Minimal: no panels"), Personnel->GetPanel(true).bVisible);

    DestroyTestWorld(World);
    return true;
}

#endif
