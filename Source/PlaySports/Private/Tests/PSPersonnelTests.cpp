// PSPersonnelTests.cpp -- Epic 19.5 (substitution and personnel packages)
//
// Tests covered:
//   1. The shipped personnel packages load and validate, formations bring their packages
//      on side by side, and a broken catalog's problems are each reported.
//   2. Selection from the shipped roster's depth chart: the starting 22 are the default
//      packages, each package picks the right players, a sit-out and a player on another
//      pawn are skipped, and a package the roster can't fill is refused.
//   3. Play calls through UPSPlayCallSubsystem change who is on the field: 12 personnel,
//      nickel and goal line come on with only the changes moving, each side lines up again,
//      the bus hears every change, and at the snap the substitutes run the call's jobs.
//   4. Between plays: a downed carrier sits a play for his backup, a tired receiver rests a
//      play, a player on the 4th-down extra pawn isn't fielded twice, and a substituted pawn
//      a human controls stays the human's.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSFieldGrid.h"
#include "PSOffenseController.h"
#include "PSPersonnelManager.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSRoster.h"
#include "PSTelemetryBus.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPersonnelTests
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

    /** The shipped roster (Data/sample_players.json) with its default depth chart. */
    static UPSRoster* LoadSampleRoster()
    {
        UDataTable* Table = NewObject<UDataTable>();
        Table->RowStruct = FPlayerAttributes::StaticStruct();
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        if (!Ingestion->LoadPlayerAttributesFromJson(FPaths::ProjectDir() / TEXT("Data/sample_players.json"), Table))
        {
            return nullptr;
        }

        TArray<FPlayerAttributes*> Rows;
        Table->GetAllRows<FPlayerAttributes>(TEXT("PSPersonnelTests"), Rows);
        TArray<FPlayerAttributes> Players;
        for (const FPlayerAttributes* Row : Rows)
        {
            if (Row)
            {
                Players.Add(*Row);
            }
        }
        UPSRoster* Roster = NewObject<UPSRoster>();
        Roster->InitializeRoster(Players);
        Roster->BuildDefaultDepthChart();
        return Roster;
    }

    /** A manager over Roster with the shipped packages. */
    static UPSPersonnelManager* MakeManager(UPSRoster* Roster)
    {
        UPSPersonnelManager* Manager = NewObject<UPSPersonnelManager>();
        Manager->Initialize(Roster);
        Manager->LoadCatalogFromJson(UPSPersonnelManager::GetDefaultCatalogPath());
        return Manager;
    }

    static FPSSituationContext FirstAndTen()
    {
        FPSSituationContext Situation;
        Situation.Down = 1;
        Situation.Distance = 10;
        Situation.YardLine = 20;
        return Situation;
    }

    static bool Has(const TArray<FName>& PlayerIds, const TCHAR* PlayerId)
    {
        return PlayerIds.Contains(FName(PlayerId));
    }

    static bool IsOnly(const TArray<FName>& PlayerIds, const TCHAR* PlayerId)
    {
        return PlayerIds.Num() == 1 && PlayerIds[0] == FName(PlayerId);
    }

    static APSPlayerPawn* FindPawn(const TArray<APSPlayerPawn*>& Pawns, const TCHAR* PlayerId)
    {
        for (APSPlayerPawn* Pawn : Pawns)
        {
            if (Pawn && Pawn->GetAttributes().PlayerId == FName(PlayerId))
            {
                return Pawn;
            }
        }
        return nullptr;
    }

    static bool HasPlayer(const APSPlayerPawn* Pawn, const TCHAR* PlayerId)
    {
        return Pawn && Pawn->GetAttributes().PlayerId == FName(PlayerId);
    }

    static int32 CountRole(const TArray<APSPlayerPawn*>& Pawns, EPlayerRole Role)
    {
        int32 Count = 0;
        for (const APSPlayerPawn* Pawn : Pawns)
        {
            if (Pawn && Pawn->GetAttributes().Role == Role)
            {
                ++Count;
            }
        }
        return Count;
    }

    static TArray<APSPlayerPawn*> SidePawns(const TArray<APSPlayerPawn*>& Pawns, EPSTeamSide Side)
    {
        TArray<APSPlayerPawn*> Result;
        for (APSPlayerPawn* Pawn : Pawns)
        {
            if (Pawn && Pawn->TeamSide == Side)
            {
                Result.Add(Pawn);
            }
        }
        return Result;
    }

    /** True when the pawns stand where APSFieldGrid::ComputeLineup puts their roles. */
    static bool IsLinedUp(const TArray<APSPlayerPawn*>& Pawns, float ScrimmageX)
    {
        TArray<EPlayerRole> Roles;
        for (const APSPlayerPawn* Pawn : Pawns)
        {
            Roles.Add(Pawn->GetAttributes().Role);
        }
        const TArray<FVector> Lineup = APSFieldGrid::ComputeLineup(Roles, ScrimmageX);
        for (int32 PawnIndex = 0; PawnIndex < Pawns.Num(); ++PawnIndex)
        {
            if (!Pawns[PawnIndex]->GetActorLocation().Equals(Lineup[PawnIndex], 1.f))
            {
                return false;
            }
        }
        return true;
    }

    /** Headless worlds don't auto-possess spawned pawns: give each its side's AI. */
    static void GiveAIControllers(UWorld* World, const TArray<APSPlayerPawn*>& Pawns)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        for (APSPlayerPawn* Pawn : Pawns)
        {
            if (!Pawn || Pawn->GetController())
            {
                continue;
            }
            AController* AI = nullptr;
            if (Pawn->TeamSide == EPSTeamSide::Defense)
            {
                AI = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Pawn->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);
            }
            else
            {
                AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Pawn->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);
            }
            if (AI)
            {
                AI->Possess(Pawn);
            }
        }
    }

    static int32 CountPawnsInWorld(UWorld* World)
    {
        int32 Count = 0;
        for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
        {
            ++Count;
        }
        return Count;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The shipped catalog, formation lookup and validation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPersonnelCatalogTest,
    "PlaySports.Personnel.Catalog",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPersonnelCatalogTest::RunTest(const FString& Parameters)
{
    UPSPersonnelManager* Manager = NewObject<UPSPersonnelManager>();
    if (!TestTrue(TEXT("The shipped personnel packages load"), Manager->LoadCatalogFromJson(UPSPersonnelManager::GetDefaultCatalogPath())))
    {
        return false;
    }

    const TArray<FString> Problems = UPSPersonnelManager::ValidateCatalog(Manager->GetCatalog());
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("The shipped packages are sound"), Problems.Num(), 0);
    TestEqual(TEXT("Nine packages ship"), Manager->GetCatalog().Packages.Num(), 9);
    TestEqual(TEXT("11 personnel is the offense's default"), Manager->GetCatalog().DefaultOffensePackage, FName(TEXT("P11")));
    TestEqual(TEXT("Base 4-3 is the defense's default"), Manager->GetCatalog().DefaultDefensePackage, FName(TEXT("Base43")));
    TestTrue(TEXT("The fatigue threshold comes from data"), FMath::IsNearlyEqual(Manager->GetCatalog().FatigueSubstitutionThreshold, 0.3f));

    const FPSPersonnelPackage* Twelve = Manager->FindPackage(TEXT("P12"));
    TestTrue(TEXT("12 personnel fields two tight ends and two receivers"),
        Twelve && Twelve->bOffense && Twelve->RoleCounts.FindRef(TEXT("TightEnd")) == 2 && Twelve->RoleCounts.FindRef(TEXT("WideReceiver")) == 2);
    const FPSPersonnelPackage* Dime = Manager->FindPackage(TEXT("Dime"));
    TestTrue(TEXT("Dime fields six defensive backs"), Dime && !Dime->bOffense && Dime->RoleCounts.FindRef(TEXT("DefensiveBack")) == 6);
    TestNull(TEXT("An unknown package is not found"), Manager->FindPackage(TEXT("P99")));

    // Formations bring their packages on, each side on its own.
    TestEqual(TEXT("Trips Right is 11 personnel"), Manager->GetPackageForFormation(TEXT("Trips Right"), true), FName(TEXT("P11")));
    TestEqual(TEXT("Ace is 12 personnel"), Manager->GetPackageForFormation(TEXT("Ace"), true), FName(TEXT("P12")));
    TestEqual(TEXT("I-Form is 21 personnel"), Manager->GetPackageForFormation(TEXT("I-Form"), true), FName(TEXT("P21")));
    TestEqual(TEXT("Spread is 10 personnel"), Manager->GetPackageForFormation(TEXT("Spread"), true), FName(TEXT("P10")));
    TestEqual(TEXT("Base 3-4 is the 3-4 package"), Manager->GetPackageForFormation(TEXT("Base 3-4"), false), FName(TEXT("Base34")));
    TestEqual(TEXT("Nickel is nickel"), Manager->GetPackageForFormation(TEXT("Nickel"), false), FName(TEXT("Nickel")));
    TestEqual(TEXT("The blitz package plays from nickel"), Manager->GetPackageForFormation(TEXT("Blitz Package"), false), FName(TEXT("Nickel")));
    TestEqual(TEXT("Goal Line is the goal-line package"), Manager->GetPackageForFormation(TEXT("Goal Line"), false), FName(TEXT("GoalLine")));
    TestEqual(TEXT("A formation no package lists brings the default on"), Manager->GetPackageForFormation(TEXT("Wishbone"), true), FName(TEXT("P11")));
    TestEqual(TEXT("...the default of the calling side"), Manager->GetPackageForFormation(TEXT("Ace"), false), FName(TEXT("Base43")));

    // A broken catalog: every problem is named.
    FPSPersonnelCatalog Broken = Manager->GetCatalog();
    Broken.DefaultDefensePackage = TEXT("P11");
    Broken.FatigueSubstitutionThreshold = 1.5f;
    Broken.Packages[0].RoleCounts.Add(TEXT("Linebacker"), 1);
    Broken.Packages[1].RoleCounts.Add(TEXT("Kicker"), 1);
    Broken.Packages[2].Formations.Add(TEXT("Ace"));
    Broken.Packages[3].PackageId = TEXT("P11");
    Broken.Packages[4].DisplayName = TEXT(" ");
    FPSPersonnelPackage NoSnap;
    NoSnap.PackageId = TEXT("NoSnap");
    NoSnap.DisplayName = TEXT("No Snap");
    NoSnap.RoleCounts.Add(TEXT("WideReceiver"), 11);
    Broken.Packages.Add(NoSnap);

    const TArray<FString> BrokenProblems = UPSPersonnelManager::ValidateCatalog(Broken);
    auto Mentions = [&BrokenProblems](const TCHAR* Text)
    {
        return BrokenProblems.ContainsByPredicate([Text](const FString& Line) { return Line.Contains(Text); });
    };
    TestTrue(TEXT("A defender in an offensive package is caught"), Mentions(TEXT("P11: Linebacker doesn't play on offense")));
    TestTrue(TEXT("...and so is the twelfth man"), Mentions(TEXT("Package P11 fields 12 players, not 11")));
    TestTrue(TEXT("An unknown role is caught"), Mentions(TEXT("'Kicker' is not an EPlayerRole")));
    TestTrue(TEXT("A formation claimed twice is caught"), Mentions(TEXT("Formation 'Ace' brings on both P12 and P21")));
    TestTrue(TEXT("A duplicate ID is caught"), Mentions(TEXT("Package P11: duplicate PackageId")));
    TestTrue(TEXT("A package without a name is caught"), Mentions(TEXT("Package Base43: no DisplayName")));
    TestTrue(TEXT("An offense nobody can snap to is caught"), Mentions(TEXT("Package NoSnap: an offense needs a Quarterback")));
    TestTrue(TEXT("A default from the wrong side is caught"), Mentions(TEXT("DefaultDefensePackage 'P11' is not a defensive package")));
    TestTrue(TEXT("A threshold above 1 is caught"), Mentions(TEXT("FatigueSubstitutionThreshold 1.50")));

    FPSPersonnelCatalog Empty;
    TestTrue(TEXT("An empty catalog is caught"), UPSPersonnelManager::ValidateCatalog(Empty).Contains(FString(TEXT("No personnel packages."))));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Selection from the depth chart and the starting lineup
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPersonnelSelectionTest,
    "PlaySports.Personnel.SelectionFromDepthChart",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPersonnelSelectionTest::RunTest(const FString& Parameters)
{
    using namespace PSPersonnelTests;

    UPSRoster* Roster = LoadSampleRoster();
    if (!TestNotNull(TEXT("The sample roster loads"), Roster))
    {
        return false;
    }
    TestEqual(TEXT("The sample roster carries a bench: 22 starters and 9 backups"), Roster->GetFullRoster().Num(), 31);
    UPSPersonnelManager* Manager = MakeManager(Roster);

    // The starting lineup: each side's default package, offense first, as roster rows.
    const TArray<const FPlayerAttributes*> Starters = Manager->GetStartingLineup();
    TestEqual(TEXT("22 players start"), Starters.Num(), 22);
    TArray<FName> StarterIds;
    int32 OffenseStarters = 0;
    bool bAllRosterRows = true;
    for (const FPlayerAttributes* Player : Starters)
    {
        StarterIds.Add(Player->PlayerId);
        OffenseStarters += APSFieldGrid::GetSideForRole(Player->Role) == EPSTeamSide::Offense ? 1 : 0;
        bAllRosterRows &= Player == Roster->FindPlayerPtr(Player->PlayerId);
    }
    TestEqual(TEXT("11 of them on offense"), OffenseStarters, 11);
    TestTrue(TEXT("Every starter is the roster's own row"), bAllRosterRows);
    TestTrue(TEXT("The offense comes first"), Starters.Num() > 0 && Starters[0]->PlayerId == FName(TEXT("QB_001")));
    TestTrue(TEXT("The first 22 on the depth chart start"), Has(StarterIds, TEXT("WR_003")) && Has(StarterIds, TEXT("TE_001")) && Has(StarterIds, TEXT("LB_003")) && Has(StarterIds, TEXT("DB_004")));
    TestFalse(TEXT("The bench starts on the sideline"), Has(StarterIds, TEXT("QB_002")) || Has(StarterIds, TEXT("TE_002")) || Has(StarterIds, TEXT("DB_005")));

    const FPSPersonnelPackage* Twelve = Manager->FindPackage(TEXT("P12"));
    const FPSPersonnelPackage* Dime = Manager->FindPackage(TEXT("Dime"));
    const FPSPersonnelPackage* GoalLine = Manager->FindPackage(TEXT("GoalLine"));
    const FPSPersonnelPackage* Base = Manager->FindPackage(TEXT("Base43"));
    const FPSPersonnelPackage* Eleven = Manager->FindPackage(TEXT("P11"));
    if (!TestNotNull(TEXT("12 personnel"), Twelve) || !TestNotNull(TEXT("Dime"), Dime) || !TestNotNull(TEXT("Goal line"), GoalLine)
        || !TestNotNull(TEXT("Base 4-3"), Base) || !TestNotNull(TEXT("11 personnel"), Eleven))
    {
        return false;
    }

    TArray<FName> Picked;
    TestTrue(TEXT("12 personnel can be fielded"), Manager->SelectPlayers(*Twelve, TSet<FName>(), Picked));
    TestEqual(TEXT("...with 11 players"), Picked.Num(), 11);
    TestTrue(TEXT("...both tight ends"), Has(Picked, TEXT("TE_001")) && Has(Picked, TEXT("TE_002")));
    TestTrue(TEXT("...and the top two receivers only"), Has(Picked, TEXT("WR_001")) && Has(Picked, TEXT("WR_002")) && !Has(Picked, TEXT("WR_003")));

    TestTrue(TEXT("Dime can be fielded"), Manager->SelectPlayers(*Dime, TSet<FName>(), Picked));
    TestTrue(TEXT("...with the fifth and sixth backs"), Has(Picked, TEXT("DB_005")) && Has(Picked, TEXT("DB_006")));
    TestTrue(TEXT("...and one linebacker, the starter"), Has(Picked, TEXT("LB_001")) && !Has(Picked, TEXT("LB_002")));

    TestTrue(TEXT("Goal line can be fielded"), Manager->SelectPlayers(*GoalLine, TSet<FName>(), Picked));
    TestTrue(TEXT("...with six linemen"), Has(Picked, TEXT("DL_005")) && Has(Picked, TEXT("DL_006")));
    TestFalse(TEXT("...and three backs"), Has(Picked, TEXT("DB_004")));

    TSet<FName> OnAnotherPawn;
    OnAnotherPawn.Add(FName(TEXT("LB_002")));
    TestTrue(TEXT("Base 4-3 without LB_002 can be fielded"), Manager->SelectPlayers(*Base, OnAnotherPawn, Picked));
    TestTrue(TEXT("...the next linebacker steps in"), Has(Picked, TEXT("LB_004")) && !Has(Picked, TEXT("LB_002")));

    // A carrier downed on play 4 sits out play 5 only.
    Roster->MarkDownedForNextPlay(TEXT("RB_001"), 4);
    Manager->BeginNewPlay(0.f, 5);
    TestTrue(TEXT("11 personnel can be fielded during the sit-out"), Manager->SelectPlayers(*Eleven, TSet<FName>(), Picked));
    TestTrue(TEXT("...with the backup back"), Has(Picked, TEXT("RB_002")) && !Has(Picked, TEXT("RB_001")));
    Manager->BeginNewPlay(0.f, 6);
    Manager->SelectPlayers(*Eleven, TSet<FName>(), Picked);
    TestTrue(TEXT("The starter is picked again the play after"), Has(Picked, TEXT("RB_001")) && !Has(Picked, TEXT("RB_002")));

    FPSPersonnelPackage Jumbo;
    Jumbo.PackageId = TEXT("P13");
    Jumbo.RoleCounts.Add(TEXT("Quarterback"), 1);
    Jumbo.RoleCounts.Add(TEXT("RunningBack"), 1);
    Jumbo.RoleCounts.Add(TEXT("TightEnd"), 3);
    Jumbo.RoleCounts.Add(TEXT("WideReceiver"), 1);
    Jumbo.RoleCounts.Add(TEXT("OffensiveLineman"), 5);
    TestFalse(TEXT("Three tight ends can't be fielded from two"), Manager->SelectPlayers(Jumbo, TSet<FName>(), Picked));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Play calls bring their packages on
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPersonnelPlayCallTest,
    "PlaySports.Personnel.PlayCallsSubstitute",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPersonnelPlayCallTest::RunTest(const FString& Parameters)
{
    using namespace PSPersonnelTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSRoster* Roster = LoadSampleRoster();
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Roster"), Roster))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    UPSPersonnelManager* Manager = MakeManager(Roster);

    // Every formation in the shipped playbook brings on a package that lists it.
    for (const bool bOffense : { true, false })
    {
        for (const FString& Formation : PlayCall->GetFormations(bOffense))
        {
            const FPSPersonnelPackage* Package = Manager->FindPackage(Manager->GetPackageForFormation(Formation, bOffense));
            TestTrue(*FString::Printf(TEXT("Formation '%s' has its own package"), *Formation), Package && Package->Formations.Contains(Formation));
        }
    }

    const float ScrimmageX = 2000.f;
    const TArray<APSPlayerPawn*> Pawns = APSFieldGrid::SpawnPlayersFromRoster(Manager->GetStartingLineup(), ScrimmageX, World);
    if (!TestEqual(TEXT("22 pawns take the field"), Pawns.Num(), 22))
    {
        DestroyTestWorld(World);
        return false;
    }
    GiveAIControllers(World, Pawns);
    Manager->BindPawns(Pawns);
    Manager->BindToBus(Bus);
    Manager->BeginNewPlay(ScrimmageX, 1);
    TestEqual(TEXT("The offense starts in 11 personnel"), Manager->GetCurrentPackage(true), FName(TEXT("P11")));
    TestEqual(TEXT("The defense starts in base 4-3"), Manager->GetCurrentPackage(false), FName(TEXT("Base43")));

    TArray<FPSTelemetryPersonnelEvent> Changes;
    const FDelegateHandle ChangesHandle = Bus->OnPersonnelMC.AddLambda([&Changes](const FPSTelemetryPersonnelEvent& Change) { Changes.Add(Change); });

    // Play-action from Ace: 12 personnel. The third receiver's pawn takes the second tight end;
    // nobody else changes.
    APSPlayerPawn* SlotPawn = FindPawn(Pawns, TEXT("WR_003"));
    APSPlayerPawn* QuarterbackPawn = FindPawn(Pawns, TEXT("QB_001"));
    PlayCall->OpenPlayCall(FirstAndTen());
    TestTrue(TEXT("The offense calls play-action from Ace"), PlayCall->CallPlay(TEXT("Offense_PlayActionPost"), EPSPlayCaller::Human));
    TestEqual(TEXT("12 personnel is on"), Manager->GetCurrentPackage(true), FName(TEXT("P12")));
    TestEqual(TEXT("Two tight ends are on the field"), CountRole(Pawns, EPlayerRole::TightEnd), 2);
    TestEqual(TEXT("...and two receivers"), CountRole(Pawns, EPlayerRole::WideReceiver), 2);
    TestTrue(TEXT("TE_002 took the third receiver's pawn"), HasPlayer(SlotPawn, TEXT("TE_002")));
    TestTrue(TEXT("...which stays on offense"), SlotPawn && SlotPawn->TeamSide == EPSTeamSide::Offense);
    TestTrue(TEXT("The quarterback kept his pawn"), HasPlayer(QuarterbackPawn, TEXT("QB_001")));
    TestEqual(TEXT("No pawn was spawned or destroyed"), CountPawnsInWorld(World), 22);
    TestTrue(TEXT("The offense lines up for its new personnel"), IsLinedUp(SidePawns(Pawns, EPSTeamSide::Offense), ScrimmageX));
    TestEqual(TEXT("The change is announced once"), Changes.Num(), 1);
    if (Changes.Num() == 1)
    {
        TestTrue(TEXT("...for the offense"), Changes[0].bOffense);
        TestEqual(TEXT("...as 12 personnel"), Changes[0].PackageId, FName(TEXT("P12")));
        TestEqual(TEXT("...by name"), Changes[0].PackageName, FString(TEXT("12 Personnel")));
        TestTrue(TEXT("...TE_002 in"), IsOnly(Changes[0].PlayersIn, TEXT("TE_002")));
        TestTrue(TEXT("...WR_003 out"), IsOnly(Changes[0].PlayersOut, TEXT("WR_003")));
    }

    // Calling the same personnel again changes nothing and says nothing.
    TestTrue(TEXT("The offense calls play-action again"), PlayCall->CallPlay(TEXT("Offense_PlayActionPost"), EPSPlayCaller::Human));
    TestEqual(TEXT("No new announcement for the same personnel"), Changes.Num(), 1);

    // The defense answers in nickel, then goal line.
    APSPlayerPawn* ThirdLinebackerPawn = FindPawn(Pawns, TEXT("LB_003"));
    TestTrue(TEXT("The defense calls nickel man"), PlayCall->CallPlay(TEXT("Defense_NickelManFree"), EPSPlayCaller::CPU));
    TestEqual(TEXT("Nickel is on"), Manager->GetCurrentPackage(false), FName(TEXT("Nickel")));
    TestEqual(TEXT("Five defensive backs"), CountRole(Pawns, EPlayerRole::DefensiveBack), 5);
    TestEqual(TEXT("Two linebackers"), CountRole(Pawns, EPlayerRole::Linebacker), 2);
    TestTrue(TEXT("The nickel back took the third linebacker's pawn"), HasPlayer(ThirdLinebackerPawn, TEXT("DB_005")));
    TestTrue(TEXT("The defense lines up for nickel"), IsLinedUp(SidePawns(Pawns, EPSTeamSide::Defense), ScrimmageX));
    TestTrue(TEXT("Nickel is announced for the defense"), Changes.Num() == 2 && !Changes[1].bOffense && IsOnly(Changes[1].PlayersIn, TEXT("DB_005")) && IsOnly(Changes[1].PlayersOut, TEXT("LB_003")));

    TestTrue(TEXT("The defense switches to the goal-line stack"), PlayCall->CallPlay(TEXT("Defense_GoalLineStack"), EPSPlayCaller::CPU));
    TestEqual(TEXT("Goal line is on"), Manager->GetCurrentPackage(false), FName(TEXT("GoalLine")));
    TestEqual(TEXT("Six linemen"), CountRole(Pawns, EPlayerRole::DefensiveLineman), 6);
    TestEqual(TEXT("Three backs"), CountRole(Pawns, EPlayerRole::DefensiveBack), 3);
    TestTrue(TEXT("Two backs made way for two linemen"), Changes.Num() == 3 && Changes[2].PlayersIn.Num() == 2 && Has(Changes[2].PlayersIn, TEXT("DL_006")) && Has(Changes[2].PlayersOut, TEXT("DB_005")));
    TestEqual(TEXT("Still 11 on defense"), SidePawns(Pawns, EPSTeamSide::Defense).Num(), 11);

    // Back to nickel for the snap: the substitutes run the called play's jobs. The pawn that
    // was the third linebacker (its AI started on a linebacker's run fit) is a back again.
    TestTrue(TEXT("The defense goes back to nickel"), PlayCall->CallPlay(TEXT("Defense_NickelManFree"), EPSPlayCaller::CPU));
    TestTrue(TEXT("The third linebacker's pawn holds a defensive back"), ThirdLinebackerPawn && ThirdLinebackerPawn->GetAttributes().Role == EPlayerRole::DefensiveBack);
    FPSTelemetrySnapEvent Snap;
    Snap.Down = 1;
    Snap.Distance = 10;
    Snap.YardLine = 20;
    Snap.LineOfScrimmage = FVector(ScrimmageX, 0.f, 0.f);
    Bus->PublishSnap(Snap);
    // Play-action post: receivers run a Post (first cut 900 cm upfield), tight ends an Out (600).
    const APSOffenseController* SecondTightEnd = SlotPawn ? Cast<APSOffenseController>(SlotPawn->GetController()) : nullptr;
    TestTrue(TEXT("The second tight end runs the tight ends' Out, not the receiver's Post"),
        SecondTightEnd && SecondTightEnd->GetRouteWaypointCount() == 2 && FMath::IsNearlyEqual(SecondTightEnd->GetCurrentTargetLocation().X, ScrimmageX + 600.f, 1.f));
    const APSDefenseController* NickelBack = ThirdLinebackerPawn ? Cast<APSDefenseController>(ThirdLinebackerPawn->GetController()) : nullptr;
    TestTrue(TEXT("The back on the linebacker's pawn plays the call's man coverage"), NickelBack && NickelBack->GetAssignment() == EPSDefensiveAssignmentType::ManCoverage);

    // A formation no package lists puts the side's default back on.
    FPSTelemetryPlayCallEvent Wishbone;
    Wishbone.PlayId = TEXT("Offense_Wishbone");
    Wishbone.Formation = TEXT("Wishbone");
    Wishbone.bOffense = true;
    Bus->PublishPlayCall(Wishbone);
    TestEqual(TEXT("11 personnel is back"), Manager->GetCurrentPackage(true), FName(TEXT("P11")));
    TestTrue(TEXT("WR_003 is back on the same pawn"), HasPlayer(SlotPawn, TEXT("WR_003")));
    TestEqual(TEXT("One tight end again"), CountRole(Pawns, EPlayerRole::TightEnd), 1);

    Bus->OnPersonnelMC.Remove(ChangesHandle);
    Manager->UnbindFromBus();
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Sit-outs, tired players, the extra pawn and human control
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPersonnelBetweenPlaysTest,
    "PlaySports.Personnel.SitOutsFatigueAndControl",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPersonnelBetweenPlaysTest::RunTest(const FString& Parameters)
{
    using namespace PSPersonnelTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSRoster* Roster = LoadSampleRoster();
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Roster"), Roster))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    UPSPersonnelManager* Manager = MakeManager(Roster);
    const float ScrimmageX = 2000.f;
    const TArray<APSPlayerPawn*> Pawns = APSFieldGrid::SpawnPlayersFromRoster(Manager->GetStartingLineup(), ScrimmageX, World);
    if (!TestEqual(TEXT("22 pawns take the field"), Pawns.Num(), 22))
    {
        DestroyTestWorld(World);
        return false;
    }
    GiveAIControllers(World, Pawns);
    Manager->BindPawns(Pawns);
    Manager->BindToBus(Bus);
    Manager->BeginNewPlay(ScrimmageX, 1);

    TArray<FPSTelemetryPersonnelEvent> Changes;
    const FDelegateHandle ChangesHandle = Bus->OnPersonnelMC.AddLambda([&Changes](const FPSTelemetryPersonnelEvent& Change) { Changes.Add(Change); });

    // A ball carrier downed on play 1 sits out play 2 for his backup, then comes back.
    APSPlayerPawn* BackPawn = FindPawn(Pawns, TEXT("RB_001"));
    Roster->MarkDownedForNextPlay(TEXT("RB_001"), 1);
    const float NextScrimmageX = ScrimmageX + 500.f;
    Manager->BeginNewPlay(NextScrimmageX, 2);
    TestTrue(TEXT("The backup back takes the downed carrier's pawn"), HasPlayer(BackPawn, TEXT("RB_002")));
    TestFalse(TEXT("The downed carrier is off the field"), Has(Manager->GetOnFieldPlayerIds(true), TEXT("RB_001")));
    TestEqual(TEXT("The offense stays in 11 personnel"), Manager->GetCurrentPackage(true), FName(TEXT("P11")));
    FPSPlayerLiveState BackupState;
    TestTrue(TEXT("The backup starts the play fresh"), Roster->FindLiveState(TEXT("RB_002"), BackupState) && !BackupState.bIsDowned && BackupState.CurrentHitPoints > 0.f);
    TestTrue(TEXT("The backup lines up at the new line of scrimmage"),
        BackPawn && FMath::IsNearlyEqual(BackPawn->GetActorLocation().X, NextScrimmageX - APSFieldGrid::RunningBackDepth, 1.f));
    TestTrue(TEXT("The sit-out is announced"), Changes.Num() == 1 && IsOnly(Changes[0].PlayersIn, TEXT("RB_002")) && IsOnly(Changes[0].PlayersOut, TEXT("RB_001")));

    Manager->BeginNewPlay(NextScrimmageX, 3);
    TestTrue(TEXT("The carrier is back the play after"), HasPlayer(BackPawn, TEXT("RB_001")));
    FPSPlayerLiveState CarrierState;
    TestTrue(TEXT("...healed for it"), Roster->FindLiveState(TEXT("RB_001"), CarrierState) && !CarrierState.bIsDowned && CarrierState.CurrentHitPoints > 0.f);

    // A receiver out of breath rests a play while the fourth receiver plays.
    APSPlayerPawn* FlankerPawn = FindPawn(Pawns, TEXT("WR_001"));
    if (!TestNotNull(TEXT("WR_001's pawn"), FlankerPawn))
    {
        DestroyTestWorld(World);
        return false;
    }
    FlankerPawn->ApplyFatigue(FlankerPawn->MaxStamina * 0.9f);
    Manager->BeginNewPlay(NextScrimmageX, 4);
    TestTrue(TEXT("The tired receiver rests"), Manager->GetRestingPlayerIds().Contains(FName(TEXT("WR_001"))));
    TestTrue(TEXT("...and the fourth receiver plays"), HasPlayer(FlankerPawn, TEXT("WR_004")));
    TestEqual(TEXT("...still three receivers"), CountRole(Pawns, EPlayerRole::WideReceiver), 3);
    Manager->BeginNewPlay(NextScrimmageX, 5);
    TestTrue(TEXT("Rested, he returns"), HasPlayer(FlankerPawn, TEXT("WR_001")));
    TestTrue(TEXT("...on fresh legs"), FMath::IsNearlyEqual(FlankerPawn->CurrentStamina, FlankerPawn->MaxStamina));

    // The 4th-down extra defender (Epic 140) is LB_002 on a pawn of its own: LB_002 isn't
    // fielded twice, and the extra pawn lines up with the defense.
    TArray<const FPlayerAttributes*> ExtraPlayers;
    ExtraPlayers.Add(Roster->FindPlayerPtr(TEXT("LB_002")));
    const TArray<APSPlayerPawn*> ExtraPawns = APSFieldGrid::SpawnPlayersFromRoster(ExtraPlayers, NextScrimmageX, World);
    APSPlayerPawn* SecondLinebackerPawn = FindPawn(Pawns, TEXT("LB_002"));
    if (!TestEqual(TEXT("The extra defender is spawned"), ExtraPawns.Num(), 1) || !TestNotNull(TEXT("LB_002's pawn"), SecondLinebackerPawn))
    {
        DestroyTestWorld(World);
        return false;
    }
    Manager->BeginNewPlay(NextScrimmageX, 6);
    TestTrue(TEXT("The bound LB_002 pawn goes to the next linebacker"), HasPlayer(SecondLinebackerPawn, TEXT("LB_004")));
    TSet<FName> FieldedIds;
    bool bDuplicate = false;
    for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
    {
        bool bAlreadyFielded = false;
        FieldedIds.Add(It->GetAttributes().PlayerId, &bAlreadyFielded);
        bDuplicate |= bAlreadyFielded;
    }
    TestFalse(TEXT("No player is on the field twice"), bDuplicate);
    TArray<APSPlayerPawn*> Defense = SidePawns(Pawns, EPSTeamSide::Defense);
    Defense.Append(ExtraPawns);
    TestTrue(TEXT("The defense, extra pawn included, lines up together"), IsLinedUp(Defense, NextScrimmageX));

    ExtraPawns[0]->Destroy();
    Manager->BeginNewPlay(NextScrimmageX, 7);
    TestTrue(TEXT("With the extra pawn gone, LB_002 is back"), HasPlayer(SecondLinebackerPawn, TEXT("LB_002")));

    // A human on a pawn whose player is substituted stays on the pawn; the bus hears who left
    // human control and who came under it.
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSPlayerPawn* SlotPawn = FindPawn(Pawns, TEXT("WR_003"));
    if (!TestNotNull(TEXT("Player controller"), Controller) || !TestNotNull(TEXT("WR_003's pawn"), SlotPawn))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestTrue(TEXT("The human takes the slot receiver"), Controller->TakeControlOf(SlotPawn));
    TestTrue(TEXT("The offense is the human's"), PlayCall->IsHumanSide(true));

    TArray<FPSTelemetryControlChangeEvent> Controls;
    const FDelegateHandle ControlsHandle = Bus->OnControlChangeMC.AddLambda([&Controls](const FPSTelemetryControlChangeEvent& Control) { Controls.Add(Control); });
    PlayCall->OpenPlayCall(FirstAndTen());
    TestTrue(TEXT("The human calls play-action from Ace"), PlayCall->CallPlay(TEXT("Offense_PlayActionPost"), EPSPlayCaller::Human));
    TestTrue(TEXT("The human's pawn is now the second tight end"), HasPlayer(SlotPawn, TEXT("TE_002")));
    TestTrue(TEXT("...still under the human's control"), SlotPawn->IsUserControlled());
    TestTrue(TEXT("The receiver who left reports leaving human control"), Controls.ContainsByPredicate([](const FPSTelemetryControlChangeEvent& Control)
    {
        return Control.PlayerId == FName(TEXT("WR_003")) && !Control.bHumanControlled;
    }));
    TestTrue(TEXT("The tight end who came on reports coming under it"), Controls.ContainsByPredicate([](const FPSTelemetryControlChangeEvent& Control)
    {
        return Control.PlayerId == FName(TEXT("TE_002")) && Control.bHumanControlled;
    }));
    TestTrue(TEXT("The offense is still the human's"), PlayCall->IsHumanSide(true));
    TestEqual(TEXT("...and the human's call stands"), PlayCall->GetCall(true).Caller, EPSPlayCaller::Human);

    Controller->ReleaseControl();
    Bus->OnControlChangeMC.Remove(ControlsHandle);
    Bus->OnPersonnelMC.Remove(ChangesHandle);
    Manager->UnbindFromBus();
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
