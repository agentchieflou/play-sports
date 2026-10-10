// PSPlaybookGeneratorTests.cpp -- the procedural playbook generator (Epic 121)
//
// Tests covered:
//   1. The tuning: the shipped grammar loads and is sound against the route library and the
//      personnel packages, and a broken one's mistakes are reported.
//   2. The concept grammar: flood, mesh and dagger line up as their slots say in a formation,
//      a concept that needs a role the formation lacks makes nothing, runs hand the ball to the
//      first back, play-action and zone-read variants; the library runs to hundreds of plays,
//      each with a unique PlayId and each valid; the defensive call sheet is fronts x coverages x
//      pressures the coverage can afford.
//   3. Scheme flavoring: each coaching identity's playbook keeps to its formations, has every
//      category it weighs, is the same from the same seed; the air raid's book throws and the
//      power run's runs, the pressure defense blitzes more than the Cover 2 one; the staff
//      manager keeps the whole book for the team whose coordinator runs the scheme.
//   4. The AI runs generated plays: the orchestrator hands every offensive player of a
//      formation his route, spot or block and every defender his rush, blitz, man or zone.
//   5. Written playbooks load back through UPSPlaybookIngestion, still valid. CI then validates
//      the same files (Saved/GeneratedPlaybooks) with tools/content.py check --strict.
//   6. Epic 35's art/AI consistency: every play the grammar makes, lined up in its formation,
//      compiles to art that is the jobs its players are handed (PSPlayArt::ValidatePlayArt),
//      its routes ranked in read order; a route the library lacks is caught.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayOrchestrator.h"
#include "PSPlaybookGenerator.h"
#include "PSPlaybookIngestion.h"
#include "PSPlayerPawn.h"
#include "PSRouteRunnerComponent.h"
#include "PSStaffManager.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlaybookGeneratorTests
{
    static const int32 BookSeed = 121;

    static TArray<FPSSchemeDef> LoadSchemes()
    {
        FPSCoachingLeague League;
        NewObject<UPSDataIngestion>()->LoadCoachingLeagueFromJson(UPSStaffManager::GetDefaultDataPath(), League);
        return League.Schemes;
    }

    static const FPSSchemeDef* FindScheme(const TArray<FPSSchemeDef>& Schemes, const TCHAR* SchemeId)
    {
        return Schemes.FindByPredicate([SchemeId](const FPSSchemeDef& Scheme) { return Scheme.SchemeId == FName(SchemeId); });
    }

    static const FPSPlayConcept* FindConcept(const FPSPlaybookGeneratorTuning& Tuning, const TCHAR* ConceptId)
    {
        return Tuning.Concepts.FindByPredicate([ConceptId](const FPSPlayConcept& Concept) { return Concept.ConceptId == FName(ConceptId); });
    }

    /** The RouteIds (or kinds) Role's assignments carry, in order: "Go", "Out", "PassBlock". */
    static TArray<FString> Jobs(const FPSPlayDefinition& Play, EPlayerRole Role)
    {
        TArray<FString> Out;
        for (const FPSPlayAssignment& Assignment : Play.Assignments)
        {
            if (Assignment.Role == Role)
            {
                Out.Add(Assignment.RouteId.IsNone() ? StaticEnum<EPSAssignmentKind>()->GetNameStringByValue(static_cast<int64>(Assignment.Kind)) : Assignment.RouteId.ToString());
            }
        }
        return Out;
    }

    static const FPSPlayAssignment* First(const FPSPlayDefinition& Play, EPlayerRole Role)
    {
        return Play.Assignments.FindByPredicate([Role](const FPSPlayAssignment& Assignment) { return Assignment.Role == Role; });
    }

    static float Share(const TArray<FPSPlayDefinition>& Plays, TFunctionRef<bool(const FPSPlayDefinition&)> Test)
    {
        int32 Count = 0;
        for (const FPSPlayDefinition& Play : Plays)
        {
            Count += Test(Play) ? 1 : 0;
        }
        return Plays.Num() > 0 ? static_cast<float>(Count) / Plays.Num() : 0.f;
    }

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

    /** A pawn of Role at Location under its side's AI controller. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.Role = Role;
        Attributes.Speed = 75.f;
        Attributes.Agility = 75.f;
        Attributes.Strength = 75.f;
        Attributes.Acceleration = 75.f;
        Attributes.Awareness = 75.f;
        Attributes.Stamina = 100.f;
        Pawn->InitializePlayer(Attributes);
        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            if (APSDefenseController* AI = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
            {
                AI->Possess(Pawn);
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
        }
        return Pawn;
    }

    /** The formation's players, spread across both sides of the ball behind (or in front of)
     *  the line. */
    static TArray<APSPlayerPawn*> SpawnFormation(UWorld* World, const TMap<EPlayerRole, int32>& Roles, float LineX, float Depth)
    {
        TArray<APSPlayerPawn*> Pawns;
        int32 Spot = 0;
        for (const TPair<EPlayerRole, int32>& Count : Roles)
        {
            for (int32 Index = 0; Index < Count.Value; ++Index, ++Spot)
            {
                const float Side = Spot % 2 == 0 ? 1.f : -1.f;
                Pawns.Add(SpawnPlayer(World, Count.Key, FVector(LineX + Depth, Side * (150.f + 120.f * Spot), 0.f)));
            }
        }
        return Pawns;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The tuning
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlaybookGeneratorTuningTest,
    "PlaySports.Content.PlaybookGenerator.Tuning",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlaybookGeneratorTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSPlaybookGeneratorTests;

    UPSPlaybookGenerator* Generator = NewObject<UPSPlaybookGenerator>();
    if (!TestTrue(TEXT("The shipped grammar loads"), Generator->LoadTuningFromJson(UPSPlaybookGenerator::GetDefaultTuningPath())))
    {
        return false;
    }
    const FPSPlaybookGeneratorTuning Shipped = Generator->GetTuning();
    const TArray<FString> Problems = PSPlaybookGenerator::ValidateTuning(Shipped, Generator->GetRouteLibrary(), Generator->GetPersonnel());
    TestEqual(TEXT("...and is sound against the route library and the personnel packages"), Problems.Num(), 0);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestTrue(TEXT("The roadmap's families are in it: flood, mesh, dagger"),
        FindConcept(Shipped, TEXT("Flood")) && FindConcept(Shipped, TEXT("Mesh")) && FindConcept(Shipped, TEXT("Dagger")));

    FPSPlaybookGeneratorTuning Broken = Shipped;
    Broken.Concepts[0].Slots[0].Routes.Add(TEXT("Wheel"));
    Broken.Concepts[1].Slots[0].Roles.Add(EPlayerRole::Quarterback);
    Broken.Concepts[2].Formations.Add(TEXT("Wishbone"));
    Broken.Concepts[3].Deceptions = { EPSDeception::ZoneRead };
    Broken.Coverages[0].Shell = TEXT("Cover 2");
    Broken.SchemeFlavors[0].ConceptWeights.Add(TEXT("Statue"), 1.f);
    Broken.Pressures.RemoveAll([](const FPSPressureDef& Pressure) { return Pressure.Blitzers.Num() == 0; });
    auto Reports = [](const TArray<FString>& Lines, const TCHAR* Text)
    {
        return Lines.ContainsByPredicate([Text](const FString& Line) { return Line.Contains(Text); });
    };
    const TArray<FString> BrokenProblems = PSPlaybookGenerator::ValidateTuning(Broken, Generator->GetRouteLibrary(), Generator->GetPersonnel());
    TestTrue(TEXT("A route the library lacks"), Reports(BrokenProblems, TEXT("route 'Wheel' is not in the route library")));
    TestTrue(TEXT("A quarterback in a route slot"), Reports(BrokenProblems, TEXT("Quarterback is not a receiver")));
    TestTrue(TEXT("A formation the grammar doesn't line up in"), Reports(BrokenProblems, TEXT("'Wishbone' is not in OffenseFormations")));
    TestTrue(TEXT("A zone read on a pass concept"), Reports(BrokenProblems, TEXT("ZoneRead doesn't go with")));
    TestTrue(TEXT("A shell that can't be a PlayId part"), Reports(BrokenProblems, TEXT("Coverages 'Cover 2'")));
    TestTrue(TEXT("A flavor liking a concept that isn't there"), Reports(BrokenProblems, TEXT("'Statue' is no concept")));
    TestTrue(TEXT("No base call without a pressure that sends nobody"), Reports(BrokenProblems, TEXT("one must send nobody")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The concept grammar and the call sheet
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlaybookGeneratorGrammarTest,
    "PlaySports.Content.PlaybookGenerator.ConceptGrammar",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlaybookGeneratorGrammarTest::RunTest(const FString& Parameters)
{
    using namespace PSPlaybookGeneratorTests;

    UPSPlaybookGenerator* Generator = NewObject<UPSPlaybookGenerator>();
    const FPSPlaybookGeneratorTuning Tuning = Generator->GetTuning();
    const FPSPersonnelCatalog Personnel = Generator->GetPersonnel();
    const FPSPlayConcept* Flood = FindConcept(Tuning, TEXT("Flood"));
    const FPSPlayConcept* Mesh = FindConcept(Tuning, TEXT("Mesh"));
    const FPSPlayConcept* Dagger = FindConcept(Tuning, TEXT("Dagger"));
    const FPSPlayConcept* Smash = FindConcept(Tuning, TEXT("Smash"));
    const FPSPlayConcept* InsideZone = FindConcept(Tuning, TEXT("InsideZone"));
    if (!TestTrue(TEXT("The concepts these checks use are shipped"), Flood && Mesh && Dagger && Smash && InsideZone))
    {
        return false;
    }

    // Flood from Trips Right (3 WR, 1 TE, 1 RB): clear, out and flat, the third receiver backside.
    const TMap<EPlayerRole, int32> Trips = PSPlaybookGenerator::GetFormationRoles(Personnel, TEXT("Trips Right"), true);
    TestEqual(TEXT("Trips Right is 11 personnel"), Trips.FindRef(EPlayerRole::WideReceiver), 3);
    const TArray<FPSPlayDefinition> Floods = PSPlaybookGenerator::BuildConceptPlays(Tuning, *Flood, TEXT("Trips Right"), Trips);
    if (!TestEqual(TEXT("Flood: 2 clears x 2 outs, with and without play-action"), Floods.Num(), 8))
    {
        return false;
    }
    TestTrue(TEXT("The first flood's receivers: go, out, then the backside curl"), Jobs(Floods[0], EPlayerRole::WideReceiver) == TArray<FString>({ TEXT("Go"), TEXT("Out"), TEXT("Curl") }));
    TestTrue(TEXT("...the back runs the flat"), Jobs(Floods[0], EPlayerRole::RunningBack) == TArray<FString>({ TEXT("Flat") }));
    TestEqual(TEXT("...the tight end and the line protect"), Jobs(Floods[0], EPlayerRole::OffensiveLineman).Num() + Jobs(Floods[0], EPlayerRole::TightEnd).Num(), 6);
    TestTrue(TEXT("...the quarterback drops to the concept's depth"), FMath::IsNearlyEqual(First(Floods[0], EPlayerRole::Quarterback)->FormationOffset.X, static_cast<double>(Flood->QBDrop)));
    TestTrue(TEXT("The variants vary the routes"), Jobs(Floods[3], EPlayerRole::WideReceiver) == TArray<FString>({ TEXT("Seam"), TEXT("Corner"), TEXT("Curl") }));
    const FPSPlayDefinition& PlayActionFlood = Floods[4];
    TestEqual(TEXT("Play-action makes it a PlayAction play"), PlayActionFlood.PlayCategory, FString(TEXT("PlayAction")));
    TestEqual(TEXT("...with the fake"), PlayActionFlood.Deception.Type, EPSDeception::PlayAction);
    TestTrue(TEXT("...and the play-action drop"), FMath::IsNearlyEqual(First(PlayActionFlood, EPlayerRole::Quarterback)->FormationOffset.X, static_cast<double>(Tuning.PlayActionDrop)));

    // Mesh from the spread (4 WR): two drags, a sit, the flat and a backside go.
    const TArray<FPSPlayDefinition> Meshes = PSPlaybookGenerator::BuildConceptPlays(Tuning, *Mesh, TEXT("Spread"), PSPlaybookGenerator::GetFormationRoles(Personnel, TEXT("Spread"), true));
    if (TestTrue(TEXT("Mesh lines up in the spread"), Meshes.Num() > 0))
    {
        TestTrue(TEXT("Mesh: the crossers, the sit and the clear"), Jobs(Meshes[0], EPlayerRole::WideReceiver) == TArray<FString>({ TEXT("Drag"), TEXT("Drag"), TEXT("Curl"), TEXT("Go") }));
    }
    TestEqual(TEXT("Mesh isn't run from the I"), PSPlaybookGenerator::BuildConceptPlays(Tuning, *Mesh, TEXT("I-Form"), PSPlaybookGenerator::GetFormationRoles(Personnel, TEXT("I-Form"), true)).Num(), 0);

    // Dagger: a clear over a deep dig.
    const TArray<FPSPlayDefinition> Daggers = PSPlaybookGenerator::BuildConceptPlays(Tuning, *Dagger, TEXT("Twins"), PSPlaybookGenerator::GetFormationRoles(Personnel, TEXT("Twins"), true));
    TestTrue(TEXT("Dagger runs a clear and the dig"), Daggers.Num() > 0 && Jobs(Daggers[0], EPlayerRole::WideReceiver).Contains(TEXT("Dig")) && Daggers[0].PlayCategory == TEXT("DeepPass"));

    // A slot needing a role the formation lacks: no play.
    TMap<EPlayerRole, int32> NoBack;
    NoBack.Add(EPlayerRole::Quarterback, 1);
    NoBack.Add(EPlayerRole::WideReceiver, 5);
    NoBack.Add(EPlayerRole::OffensiveLineman, 5);
    TestEqual(TEXT("Smash needs a back for its flat"), PSPlaybookGenerator::BuildConceptPlays(Tuning, *Smash, TEXT("Spread"), NoBack).Num(), 0);

    // A run from the I: the first back carries, the fullback leads, the line run-blocks.
    const TArray<FPSPlayDefinition> Zones = PSPlaybookGenerator::BuildConceptPlays(Tuning, *InsideZone, TEXT("I-Form"), PSPlaybookGenerator::GetFormationRoles(Personnel, TEXT("I-Form"), true));
    if (TestEqual(TEXT("Inside zone, and its zone read"), Zones.Num(), 2))
    {
        TestTrue(TEXT("The first back takes it at his spot, the second blocks"), Jobs(Zones[0], EPlayerRole::RunningBack) == TArray<FString>({ TEXT("Route"), TEXT("RunBlock") }));
        TestTrue(TEXT("...his spot is the concept's"), First(Zones[0], EPlayerRole::RunningBack)->FormationOffset.Equals(InsideZone->BackSpot, 0.01f));
        TestEqual(TEXT("The line run-blocks"), Jobs(Zones[0], EPlayerRole::OffensiveLineman)[0], FString(TEXT("RunBlock")));
        TestEqual(TEXT("The read is a zone read"), Zones[1].Deception.Type, EPSDeception::ZoneRead);
    }

    // The library: hundreds of plays, every one valid, every PlayId its own.
    const TArray<FPSPlayDefinition> Offense = Generator->BuildLibrary(true, TArray<FString>());
    const TArray<FPSPlayDefinition> Defense = Generator->BuildLibrary(false, TArray<FString>());
    TestTrue(*FString::Printf(TEXT("Hundreds of offensive plays from a few concepts (%d)"), Offense.Num()), Offense.Num() >= 200);
    TestTrue(*FString::Printf(TEXT("A full defensive call sheet (%d)"), Defense.Num()), Defense.Num() >= 60);
    TSet<FName> Ids;
    for (const TArray<FPSPlayDefinition>* Side : { &Offense, &Defense })
    {
        for (const FPSPlayDefinition& Play : *Side)
        {
            Ids.Add(Play.PlayId);
        }
    }
    TestEqual(TEXT("Every PlayId is its own"), Ids.Num(), Offense.Num() + Defense.Num());
    TArray<FPSPlayDefinition> All = Offense;
    All.Append(Defense);
    const TArray<FString> Invalid = Generator->ValidatePlays(All);
    TestEqual(TEXT("Every generated play is valid"), Invalid.Num(), 0);
    for (int32 Index = 0; Index < FMath::Min(Invalid.Num(), 10); ++Index)
    {
        AddError(Invalid[Index]);
    }

    // The call sheet: fronts x coverages x pressures the coverage can afford.
    const FPSDefensiveFrontDef* Base43 = Tuning.DefensiveFronts.FindByPredicate([](const FPSDefensiveFrontDef& Front) { return Front.Formation == TEXT("Base 4-3"); });
    if (TestNotNull(TEXT("The 4-3 front is shipped"), Base43))
    {
        const TArray<FPSPlayDefinition> Calls = PSPlaybookGenerator::BuildDefensiveCalls(Tuning, *Base43, PSPlaybookGenerator::GetFormationRoles(Personnel, Base43->Formation, false));
        bool bAffordable = true;
        bool bBlitzesAreBlitz = true;
        for (const FPSPlayDefinition& Call : Calls)
        {
            int32 Blitzers = 0;
            for (const FPSPlayAssignment& Assignment : Call.Assignments)
            {
                Blitzers += Assignment.Kind == EPSAssignmentKind::Blitz ? 1 : 0;
            }
            const FPSCoverageTemplate* Coverage = Tuning.Coverages.FindByPredicate([&Call](const FPSCoverageTemplate& Template) { return Template.Shell == Call.CoverageShell; });
            bAffordable &= Coverage && Blitzers <= Coverage->MaxBlitzers;
            bBlitzesAreBlitz &= (Blitzers > 0) == (Call.PlayCategory == TEXT("Blitz"));
        }
        TestTrue(TEXT("Every shell, with and without pressure"), Calls.Num() >= Tuning.Coverages.Num() * 2);
        TestTrue(TEXT("No coverage sends more than it can afford"), bAffordable);
        TestTrue(TEXT("A call that sends someone is a Blitz, one that doesn't isn't"), bBlitzesAreBlitz);
        const FPSPlayDefinition* Cover3Mike = Calls.FindByPredicate([](const FPSPlayDefinition& Call) { return Call.PlayId == FName(TEXT("Def_Base43_Cover3_Mike")); });
        if (TestNotNull(TEXT("4-3 Cover 3 Mike is on the sheet"), Cover3Mike))
        {
            TestTrue(TEXT("...the Mike blitzes first, the other backers drop"),
                Jobs(*Cover3Mike, EPlayerRole::Linebacker) == TArray<FString>({ TEXT("Blitz"), TEXT("ZoneCoverage"), TEXT("ZoneCoverage") }));
            TestTrue(TEXT("...the deep middle safety is the first defensive back's job"), FMath::IsNearlyZero(First(*Cover3Mike, EPlayerRole::DefensiveBack)->ZoneOffset.Y));
            TestEqual(TEXT("...and the line rushes"), Jobs(*Cover3Mike, EPlayerRole::DefensiveLineman).Num(), 4);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Scheme flavoring
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlaybookGeneratorSchemeTest,
    "PlaySports.Content.PlaybookGenerator.SchemeFlavors",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlaybookGeneratorSchemeTest::RunTest(const FString& Parameters)
{
    using namespace PSPlaybookGeneratorTests;

    const TArray<FPSSchemeDef> Schemes = LoadSchemes();
    UPSPlaybookGenerator* Generator = NewObject<UPSPlaybookGenerator>();
    const FPSPlaybookGeneratorTuning Tuning = Generator->GetTuning();
    if (!TestTrue(TEXT("The coaching identities load"), Schemes.Num() >= 4))
    {
        return false;
    }

    TMap<FName, TArray<FPSPlayDefinition>> Books;
    for (const FPSSchemeDef& Scheme : Schemes)
    {
        const FString Name = Scheme.SchemeId.ToString();
        const FPSGeneratedPlaybook Book = Generator->GeneratePlaybook(BookSeed, Scheme);
        Books.Add(Scheme.SchemeId, Book.Plays);
        TestEqual(*FString::Printf(TEXT("%s: a full book"), *Name), Book.Plays.Num(), Scheme.bOffense ? Tuning.OffensePlaybookSize : Tuning.DefensePlaybookSize);
        TestEqual(*FString::Printf(TEXT("%s: every play valid"), *Name), Generator->ValidatePlays(Book.Plays).Num(), 0);
        bool bOwnFormations = true;
        bool bOwnSide = true;
        bool bPrefixed = true;
        for (const FPSPlayDefinition& Play : Book.Plays)
        {
            bOwnFormations &= Scheme.Formations.Contains(Play.Formation);
            bOwnSide &= Play.bIsOffensivePlay == Scheme.bOffense;
            bPrefixed &= Play.PlayId.ToString().StartsWith(Name + TEXT("_"));
        }
        TestTrue(*FString::Printf(TEXT("%s: only its formations"), *Name), bOwnFormations);
        TestTrue(*FString::Printf(TEXT("%s: only its side"), *Name), bOwnSide);
        TestTrue(*FString::Printf(TEXT("%s: PlayIds carry the scheme"), *Name), bPrefixed);
        for (const TPair<FString, float>& Weight : Scheme.CategoryWeights)
        {
            if (Weight.Value > 0.f)
            {
                const FString Category = Weight.Key;
                TestTrue(*FString::Printf(TEXT("%s: something to call for %s"), *Name, *Category),
                    Book.Plays.ContainsByPredicate([&Category](const FPSPlayDefinition& Play) { return Play.PlayCategory == Category; }));
            }
        }
        TArray<FName> FirstIds;
        TArray<FName> AgainIds;
        TArray<FName> OtherIds;
        for (const FPSPlayDefinition& Play : Book.Plays)
        {
            FirstIds.Add(Play.PlayId);
        }
        for (const FPSPlayDefinition& Play : Generator->GeneratePlaybook(BookSeed, Scheme).Plays)
        {
            AgainIds.Add(Play.PlayId);
        }
        for (const FPSPlayDefinition& Play : Generator->GeneratePlaybook(BookSeed + 1, Scheme).Plays)
        {
            OtherIds.Add(Play.PlayId);
        }
        TestTrue(*FString::Printf(TEXT("%s: the same seed, the same book"), *Name), FirstIds == AgainIds);
        TestTrue(*FString::Printf(TEXT("%s: another seed, another book"), *Name), FirstIds != OtherIds);
    }

    // Air raid against ground-and-pound; pressure against Cover 2.
    const TArray<FPSPlayDefinition>* AirRaid = Books.Find(TEXT("AirRaid"));
    const TArray<FPSPlayDefinition>* PowerRun = Books.Find(TEXT("PowerRun"));
    const TArray<FPSPlayDefinition>* Pressure = Books.Find(TEXT("Pressure34"));
    const TArray<FPSPlayDefinition>* Cover2 = Books.Find(TEXT("Cover2Zone"));
    if (TestTrue(TEXT("The shipped identities are there"), AirRaid && PowerRun && Pressure && Cover2))
    {
        auto IsRun = [](const FPSPlayDefinition& Play) { return Play.PlayCategory == TEXT("Run"); };
        auto IsDropback = [](const FPSPlayDefinition& Play) { return Play.PlayCategory == TEXT("ShortPass") || Play.PlayCategory == TEXT("DeepPass"); };
        auto IsBlitz = [](const FPSPlayDefinition& Play) { return Play.PlayCategory == TEXT("Blitz"); };
        TestTrue(*FString::Printf(TEXT("The power run runs (%.0f%% runs vs the air raid's %.0f%%)"), 100.f * Share(*PowerRun, IsRun), 100.f * Share(*AirRaid, IsRun)),
            Share(*PowerRun, IsRun) > 2.f * Share(*AirRaid, IsRun));
        TestTrue(*FString::Printf(TEXT("The air raid throws (%.0f%% dropbacks vs %.0f%%)"), 100.f * Share(*AirRaid, IsDropback), 100.f * Share(*PowerRun, IsDropback)),
            Share(*AirRaid, IsDropback) > 2.f * Share(*PowerRun, IsDropback));
        TestTrue(*FString::Printf(TEXT("The pressure defense blitzes (%.0f%% vs %.0f%%)"), 100.f * Share(*Pressure, IsBlitz), 100.f * Share(*Cover2, IsBlitz)),
            Share(*Pressure, IsBlitz) > Share(*Cover2, IsBlitz));
        // The flavor inside a category: the air raid's mesh, over ten seeds' books.
        auto IsMesh = [](const FPSPlayDefinition& Play) { return Play.PlayId.ToString().Contains(TEXT("_Mesh_")); };
        float AirRaidMesh = 0.f;
        float PowerRunMesh = 0.f;
        for (int32 Seed = 0; Seed < 10; ++Seed)
        {
            AirRaidMesh += Share(Generator->GeneratePlaybook(Seed, *FindScheme(Schemes, TEXT("AirRaid"))).Plays, IsMesh);
            PowerRunMesh += Share(Generator->GeneratePlaybook(Seed, *FindScheme(Schemes, TEXT("PowerRun"))).Plays, IsMesh);
        }
        TestTrue(*FString::Printf(TEXT("The air raid lives on mesh (%.0f%% of its books vs %.0f%%)"), 10.f * AirRaidMesh, 10.f * PowerRunMesh), AirRaidMesh > 2.f * PowerRunMesh);
    }

    // The staff manager keeps a scheme's whole book for the team whose coordinator runs it.
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    if (TestTrue(TEXT("The staffs load"), Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath())))
    {
        const FName Team(TEXT("Hawks"));
        const TArray<FPSPlayDefinition>* TeamOffense = Books.Find(Staffs->GetTeamScheme(Team, true));
        const TArray<FPSPlayDefinition>* TeamDefense = Books.Find(Staffs->GetTeamScheme(Team, false));
        if (TestTrue(TEXT("The team's schemes have books"), TeamOffense && TeamDefense))
        {
            TArray<FPSPlayDefinition> Both = *TeamOffense;
            Both.Append(*TeamDefense);
            TestEqual(TEXT("Its playbook is the whole of both"), Staffs->BuildPlaybook(Team, Both).Num(), Both.Num());
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The AI runs generated plays
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlaybookGeneratorAITest,
    "PlaySports.Content.PlaybookGenerator.AIRunsGeneratedPlays",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlaybookGeneratorAITest::RunTest(const FString& Parameters)
{
    using namespace PSPlaybookGeneratorTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSPlaybookGenerator* Generator = NewObject<UPSPlaybookGenerator>();
    UDataTable* Routes = Generator->GetRouteLibrary();
    const FPSPersonnelCatalog Personnel = Generator->GetPersonnel();
    const FVector Line(1000.f, 0.f, 0.f);

    // One play of each concept, in the first formation it fits.
    TSet<FString> Seen;
    for (const FPSPlayDefinition& Play : Generator->BuildLibrary(true, TArray<FString>()))
    {
        FString Concept;
        FString Rest;
        Play.PlayId.ToString().Split(TEXT("_"), &Concept, &Rest);
        if (Seen.Contains(Concept))
        {
            continue;
        }
        Seen.Add(Concept);
        const TArray<APSPlayerPawn*> Pawns = SpawnFormation(World, PSPlaybookGenerator::GetFormationRoles(Personnel, Play.Formation, true), Line.X, -150.f);
        UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
        Orchestrator->DistributePlayCall(Play, Pawns, Routes, Line);

        TMap<EPlayerRole, int32> Cursor;
        bool bEveryoneHasHisJob = Pawns.Num() == 11;
        for (APSPlayerPawn* Pawn : Pawns)
        {
            APSOffenseController* AI = Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr;
            if (!AI)
            {
                bEveryoneHasHisJob = false;
                continue;
            }
            const EPlayerRole Role = Pawn->GetAttributes().Role;
            int32& Index = Cursor.FindOrAdd(Role);
            const FPSPlayAssignment* Job = UPSPlayOrchestrator::FindAssignmentSlot(Play, Role, Index++);
            if (!Job)
            {
                bEveryoneHasHisJob = false;
            }
            else if (Job->Kind != EPSAssignmentKind::Route)
            {
                bEveryoneHasHisJob &= AI->GetRouteWaypointCount() == 0;
            }
            else if (Job->RouteId.IsNone())
            {
                bEveryoneHasHisJob &= AI->GetRouteWaypointCount() == 1;
            }
            else
            {
                bEveryoneHasHisJob &= AI->GetRouteWaypointCount() > 0 && AI->GetRouteRunner() && AI->GetRouteRunner()->HasPlan();
            }
        }
        TestTrue(*FString::Printf(TEXT("%s (%s): every player runs his route, takes his spot or blocks"), *Play.DisplayName, *Play.Formation), bEveryoneHasHisJob);
    }
    TestTrue(TEXT("Every concept was run"), Seen.Num() == Generator->GetTuning().Concepts.Num());

    // One call per coverage, from the nickel.
    TSet<FString> Shells;
    for (const FPSPlayDefinition& Call : Generator->BuildLibrary(false, { TEXT("Nickel") }))
    {
        if (Shells.Contains(Call.CoverageShell))
        {
            continue;
        }
        Shells.Add(Call.CoverageShell);
        const TArray<APSPlayerPawn*> Pawns = SpawnFormation(World, PSPlaybookGenerator::GetFormationRoles(Personnel, Call.Formation, false), Line.X, 200.f);
        UPSPlayOrchestrator* Orchestrator = NewObject<UPSPlayOrchestrator>();
        Orchestrator->DistributePlayCall(Call, Pawns, Routes, Line);

        TMap<EPlayerRole, int32> Cursor;
        bool bEveryoneHasHisJob = Pawns.Num() == 11;
        for (APSPlayerPawn* Pawn : Pawns)
        {
            APSDefenseController* AI = Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
            const EPlayerRole Role = Pawn ? Pawn->GetAttributes().Role : EPlayerRole::DefensiveBack;
            int32& Index = Cursor.FindOrAdd(Role);
            const FPSPlayAssignment* Job = UPSPlayOrchestrator::FindAssignmentSlot(Call, Role, Index++);
            if (!AI || !Job)
            {
                bEveryoneHasHisJob = false;
                continue;
            }
            switch (Job->Kind)
            {
            case EPSAssignmentKind::ZoneCoverage:
            {
                // His zone, on his own side of the field.
                FVector Zone = Job->ZoneOffset;
                if (Pawn->GetActorLocation().Y * Zone.Y < 0.f)
                {
                    Zone.Y = -Zone.Y;
                }
                bEveryoneHasHisJob &= AI->GetAssignment() == EPSDefensiveAssignmentType::ZoneCoverage && AI->GetZoneLocation().Equals(Line + Zone, 1.f);
                break;
            }
            case EPSAssignmentKind::ManCoverage:
                bEveryoneHasHisJob &= AI->GetAssignment() == EPSDefensiveAssignmentType::ManCoverage;
                break;
            case EPSAssignmentKind::RunFit:
                bEveryoneHasHisJob &= AI->GetAssignment() == EPSDefensiveAssignmentType::RunFit;
                break;
            default:
                // A rush or a blitz: after the quarterback.
                bEveryoneHasHisJob &= AI->GetAssignment() == EPSDefensiveAssignmentType::PassRush;
                break;
            }
        }
        TestTrue(*FString::Printf(TEXT("%s: every defender plays his rush, blitz, man or zone"), *Call.DisplayName), bEveryoneHasHisJob);
    }
    TestEqual(TEXT("Every coverage was played"), Shells.Num(), Generator->GetTuning().Coverages.Num());

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Written playbooks
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlaybookGeneratorWriteTest,
    "PlaySports.Content.PlaybookGenerator.WritesValidContent",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlaybookGeneratorWriteTest::RunTest(const FString& Parameters)
{
    using namespace PSPlaybookGeneratorTests;

    // CI's "Validate the generated playbooks" step checks this directory with tools/content.py.
    const FString Root = FPaths::ProjectSavedDir() / TEXT("GeneratedPlaybooks");
    IFileManager::Get().DeleteDirectory(*Root, false, true);
    IFileManager::Get().MakeDirectory(*(Root / TEXT("Data/playbooks")), true);
    TestTrue(TEXT("The route library goes beside the playbooks"),
        IFileManager::Get().Copy(*(Root / TEXT("Data/sample_routes.json")), *UPSPlayCallSubsystem::GetDefaultRoutesPath()) == COPY_OK);

    UPSPlaybookGenerator* Generator = NewObject<UPSPlaybookGenerator>();
    UPSPlaybookIngestion* Ingestion = NewObject<UPSPlaybookIngestion>();
    const TArray<FPSSchemeDef> Schemes = LoadSchemes();
    TestTrue(TEXT("The coaching identities load"), Schemes.Num() > 0);
    for (const FPSSchemeDef& Scheme : Schemes)
    {
        const FString Name = Scheme.SchemeId.ToString();
        const FPSGeneratedPlaybook Book = Generator->GeneratePlaybook(BookSeed, Scheme);
        const FString Path = Root / FString::Printf(TEXT("Data/playbooks/%s.json"), *Name);
        if (!TestTrue(*FString::Printf(TEXT("%s: written"), *Name), Book.Plays.Num() > 0 && UPSPlaybookGenerator::WritePlaybook(Book.Plays, Path)))
        {
            continue;
        }

        UDataTable* Loaded = NewObject<UDataTable>();
        Loaded->RowStruct = FPSPlayDefinition::StaticStruct();
        TestTrue(*FString::Printf(TEXT("%s: the play loader takes it"), *Name), Ingestion->LoadPlaysFromJson(Path, Loaded));
        TestEqual(*FString::Printf(TEXT("%s: every play read back"), *Name), Loaded->GetRowMap().Num(), Book.Plays.Num());
        TArray<FPSPlayDefinition> ReadBack;
        bool bSame = true;
        for (const FPSPlayDefinition& Play : Book.Plays)
        {
            const FPSPlayDefinition* Row = Loaded->FindRow<FPSPlayDefinition>(Play.PlayId, TEXT("PSPlaybookGeneratorTests"), false);
            if (!Row)
            {
                bSame = false;
                continue;
            }
            ReadBack.Add(*Row);
            bSame &= Row->PlayCategory == Play.PlayCategory && Row->Formation == Play.Formation && Row->Front == Play.Front && Row->CoverageShell == Play.CoverageShell
                && Row->Deception.Type == Play.Deception.Type && Row->Assignments.Num() == Play.Assignments.Num();
            for (int32 Index = 0; bSame && Index < Play.Assignments.Num(); ++Index)
            {
                const FPSPlayAssignment& Want = Play.Assignments[Index];
                const FPSPlayAssignment& Got = Row->Assignments[Index];
                bSame &= Got.Role == Want.Role && Got.Kind == Want.Kind && Got.RouteId == Want.RouteId && Got.ReadOrder == Want.ReadOrder
                    && Got.ZoneOffset.Equals(Want.ZoneOffset, 0.01f) && Got.FormationOffset.Equals(Want.FormationOffset, 0.01f);
            }
        }
        TestTrue(*FString::Printf(TEXT("%s: every play reads back as generated"), *Name), bSame);
        TestEqual(*FString::Printf(TEXT("%s: and is still valid"), *Name), Generator->ValidatePlays(ReadBack).Num(), 0);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- Epic 35's art/AI consistency
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlaybookGeneratorPlayArtTest,
    "PlaySports.Content.PlaybookGenerator.PlayArtConsistency",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlaybookGeneratorPlayArtTest::RunTest(const FString& Parameters)
{
    using namespace PSPlaybookGeneratorTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UPSPlaybookGenerator* Generator = NewObject<UPSPlaybookGenerator>();

    // The slots are the quarterback's progression, so the art ranks them; the backside isn't read.
    const FPSPlaybookGeneratorTuning& Tuning = Generator->GetTuning();
    const FPSPlayConcept* Flood = FindConcept(Tuning, TEXT("Flood"));
    if (TestNotNull(TEXT("Flood is shipped"), Flood))
    {
        const TArray<FPSPlayDefinition> Floods = PSPlaybookGenerator::BuildConceptPlays(Tuning, *Flood, TEXT("Trips Right"),
            PSPlaybookGenerator::GetFormationRoles(Generator->GetPersonnel(), TEXT("Trips Right"), true));
        TArray<int32> Reads;
        for (const FPSPlayAssignment& Assignment : Floods.Num() > 0 ? Floods[0].Assignments : TArray<FPSPlayAssignment>())
        {
            if (!Assignment.RouteId.IsNone())
            {
                Reads.Add(Assignment.ReadOrder);
            }
        }
        // Go 1, Out 2, the backside Curl unread, the back's Flat 3 (in assignment order: back, receivers).
        TestTrue(TEXT("Flood reads the clear, the out, then the flat; the backside curl is unread"), Reads == TArray<int32>({ 3, 1, 2, 0 }));
    }

    // Every play the grammar makes passes the art/AI check: the art drawn for it is the jobs its
    // players are handed at the snap. Every scheme's book is drawn from these.
    TArray<FPSPlayDefinition> Library = Generator->BuildLibrary(true, TArray<FString>());
    Library.Append(Generator->BuildLibrary(false, TArray<FString>()));
    const TArray<FString> Problems = Generator->ValidatePlayArt(World, Library);
    TestTrue(*FString::Printf(TEXT("All %d generated plays pass the art/AI consistency check"), Library.Num()), Library.Num() > 300 && Problems.Num() == 0);
    for (int32 Index = 0; Index < FMath::Min(Problems.Num(), 20); ++Index)
    {
        AddError(Problems[Index]);
    }

    // The check bites: a route the library lacks has nothing to draw or run.
    const FPSPlayDefinition* Pass = Library.FindByPredicate([](const FPSPlayDefinition& Play) { return Play.PlayCategory == TEXT("ShortPass"); });
    if (TestNotNull(TEXT("A pass to break"), Pass))
    {
        FPSPlayDefinition Broken = *Pass;
        for (FPSPlayAssignment& Assignment : Broken.Assignments)
        {
            if (!Assignment.RouteId.IsNone())
            {
                Assignment.RouteId = TEXT("Wheel");
                break;
            }
        }
        const TArray<FString> BrokenProblems = Generator->ValidatePlayArt(World, { Broken });
        TestTrue(TEXT("...is caught"), BrokenProblems.ContainsByPredicate([](const FString& Problem) { return Problem.Contains(TEXT("no route library has")); }));
    }
    TestTrue(TEXT("Without a world's overlay there is nothing to check with"), Generator->ValidatePlayArt(nullptr, Library).Num() == 1);

    DestroyTestWorld(World);
    return true;
}

#endif
