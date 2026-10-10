// PSFormationTests.cpp -- where each player lines up for a call (Data/formations.json, PSFormations)
//
// Tests covered:
//   1. The shipped catalog loads and validates, and a broken one's problems are each reported.
//   2. The offense lines up in its formation: trips puts three receivers to its side, the shotgun
//      quarterback stands deep, the I stacks its backs, the line is centred on the ball; an
//      unknown formation lines up by role.
//   3. The defense lines up in its front and shell against the offense: its techniques over the
//      line, to the strength it reads; the corners over the widest receivers; a back whose side has
//      no receiver left travels to trips.
//   4. Every call of the shipped playbook and of the generator's library lines up from the data.
//   5. Epic 80's classifier reads each formation's lineup as the formation says it should.
//   6. In a game: each call lines its side up, an offensive call lines the defense up again against
//      it, and at the snap the defense reads the formation the offense called.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "PSAIFieldSnapshot.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSFieldDimensions.h"
#include "PSFieldGrid.h"
#include "PSFormations.h"
#include "PSOffenseController.h"
#include "PSPersonnelManager.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlaybookData.h"
#include "PSPlaybookGenerator.h"
#include "PSPlayResolution.h"
#include "PSPlayRecognitionSubsystem.h"
#include "PSPlayRecognitionTypes.h"
#include "PSPlayerPawn.h"
#include "PSRoster.h"
#include "PSTelemetryBus.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSFormationTests
{
    /** The line of scrimmage every test lines up at (the offense's 20). */
    constexpr float LineX = 2000.f;

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

    static bool Near(double A, double B, double Tolerance = 1.0)
    {
        return FMath::Abs(A - B) <= Tolerance;
    }

    /** A personnel grouping: the quarterback, the backs, tight ends and receivers, then the line. */
    static TArray<EPlayerRole> Personnel(int32 Backs, int32 TightEnds, int32 Receivers)
    {
        TArray<EPlayerRole> Roles = { EPlayerRole::Quarterback };
        for (int32 Index = 0; Index < Backs; ++Index)
        {
            Roles.Add(EPlayerRole::RunningBack);
        }
        for (int32 Index = 0; Index < TightEnds; ++Index)
        {
            Roles.Add(EPlayerRole::TightEnd);
        }
        for (int32 Index = 0; Index < Receivers; ++Index)
        {
            Roles.Add(EPlayerRole::WideReceiver);
        }
        for (int32 Index = 0; Index < 5; ++Index)
        {
            Roles.Add(EPlayerRole::OffensiveLineman);
        }
        return Roles;
    }

    static TArray<EPlayerRole> Defense(int32 Linemen, int32 Linebackers, int32 Backs)
    {
        TArray<EPlayerRole> Roles;
        for (int32 Index = 0; Index < Linemen; ++Index)
        {
            Roles.Add(EPlayerRole::DefensiveLineman);
        }
        for (int32 Index = 0; Index < Linebackers; ++Index)
        {
            Roles.Add(EPlayerRole::Linebacker);
        }
        for (int32 Index = 0; Index < Backs; ++Index)
        {
            Roles.Add(EPlayerRole::DefensiveBack);
        }
        return Roles;
    }

    /** A package's players, role by role in the enum's order (as the art check lines them up). */
    static TArray<EPlayerRole> RolesOf(const TMap<EPlayerRole, int32>& Counts)
    {
        TArray<EPlayerRole> Roles;
        const UEnum* RoleEnum = StaticEnum<EPlayerRole>();
        for (int32 Index = 0; Index < RoleEnum->NumEnums() - 1; ++Index)
        {
            const EPlayerRole Role = static_cast<EPlayerRole>(RoleEnum->GetValueByIndex(Index));
            for (int32 Count = 0; Count < Counts.FindRef(Role); ++Count)
            {
                Roles.Add(Role);
            }
        }
        return Roles;
    }

    static FPSLineupCall OffenseCall(const TCHAR* Formation)
    {
        FPSLineupCall Call;
        Call.OffenseFormation = Formation;
        return Call;
    }

    static FPSLineupCall DefenseCall(const FString& Front, const FString& Shell)
    {
        FPSLineupCall Call;
        Call.DefenseFront = Front;
        Call.DefenseShell = Shell;
        return Call;
    }

    static FPSLineupResult LineUp(const TArray<EPlayerRole>& Roles, const FPSLineupCall& Call, const TArray<FPSAlignedPlayer>* Offense = nullptr)
    {
        const FPSPlayRecognitionTuning Recognition;
        return PSFormations::LineUp(PSFormations::GetCatalog(), Roles, FVector(LineX, 0.f, 0.f), Call, Offense, Recognition);
    }

    static TArray<FPSAlignedPlayer> Align(const TArray<EPlayerRole>& Roles, const TArray<FVector>& Spots)
    {
        TArray<FPSAlignedPlayer> Players;
        for (int32 Index = 0; Index < Roles.Num() && Index < Spots.Num(); ++Index)
        {
            FPSAlignedPlayer& Player = Players.AddDefaulted_GetRef();
            Player.Role = Roles[Index];
            Player.Location = Spots[Index];
        }
        return Players;
    }

    /** The spots of Role's players, in order. */
    static TArray<FVector> SpotsOf(const TArray<EPlayerRole>& Roles, const TArray<FVector>& Spots, EPlayerRole Role)
    {
        TArray<FVector> Found;
        for (int32 Index = 0; Index < Roles.Num() && Index < Spots.Num(); ++Index)
        {
            if (Roles[Index] == Role)
            {
                Found.Add(Spots[Index]);
            }
        }
        return Found;
    }

    /** The offensive line's Y, left to right. */
    static TArray<double> LineYs(const TArray<FPSAlignedPlayer>& Offense)
    {
        TArray<double> Ys;
        for (const FPSAlignedPlayer& Player : Offense)
        {
            if (Player.Role == EPlayerRole::OffensiveLineman)
            {
                Ys.Add(Player.Location.Y);
            }
        }
        Ys.Sort();
        return Ys;
    }

    static double Shade()
    {
        return PSField::YardsToCentimetres(PSFormations::GetCatalog().ShadeYards);
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
        Table->GetAllRows<FPlayerAttributes>(TEXT("PSFormationTests"), Rows);
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
            AController* AI = Pawn->TeamSide == EPSTeamSide::Defense
                ? static_cast<AController*>(World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Pawn->GetActorLocation(), FRotator::ZeroRotator, SpawnParams))
                : static_cast<AController*>(World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Pawn->GetActorLocation(), FRotator::ZeroRotator, SpawnParams));
            if (AI)
            {
                AI->Possess(Pawn);
            }
        }
    }

    static TArray<APSPlayerPawn*> SidePawns(const TArray<APSPlayerPawn*>& Pawns, EPSTeamSide Side, EPlayerRole Role)
    {
        TArray<APSPlayerPawn*> Found;
        for (APSPlayerPawn* Pawn : Pawns)
        {
            if (Pawn && Pawn->TeamSide == Side && Pawn->GetAttributes().Role == Role)
            {
                Found.Add(Pawn);
            }
        }
        return Found;
    }

    static TArray<FPSAlignedPlayer> AlignPawns(const TArray<APSPlayerPawn*>& Pawns, EPSTeamSide Side)
    {
        TArray<FPSAlignedPlayer> Players;
        for (const APSPlayerPawn* Pawn : Pawns)
        {
            if (Pawn && Pawn->TeamSide == Side)
            {
                FPSAlignedPlayer& Player = Players.AddDefaulted_GetRef();
                Player.Role = Pawn->GetAttributes().Role;
                Player.Location = Pawn->GetActorLocation();
            }
        }
        return Players;
    }

    /** True when some pawn of the list stands at Y (within a centimetre). */
    static bool AnyAtY(const TArray<APSPlayerPawn*>& Pawns, double Y)
    {
        return Pawns.ContainsByPredicate([Y](const APSPlayerPawn* Pawn) { return Near(Pawn->GetActorLocation().Y, Y); });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The shipped catalog
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFormationCatalogTest,
    "PlaySports.Formations.Catalog",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFormationCatalogTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSFormationCatalog Shipped;
    if (!TestTrue(TEXT("Data/formations.json loads"), Ingestion->LoadFormationCatalogFromJson(PSFormations::GetDefaultDataPath(), Shipped)))
    {
        return false;
    }
    const TArray<FString> Problems = PSFormations::Validate(Shipped);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("...and is sound"), Problems.Num(), 0);
    TestTrue(TEXT("The catalog in use is the shipped one"), Shipped.OffenseFormations.Num() > 0
        && PSFormations::GetCatalog().OffenseFormations.Num() == Shipped.OffenseFormations.Num()
        && PSFormations::GetCatalog().FrontAlignments.Num() == Shipped.FrontAlignments.Num()
        && PSFormations::GetCatalog().ShellAlignments.Num() == Shipped.ShellAlignments.Num());
    const FPSFormationCatalog Defaults;
    TestTrue(TEXT("The struct's defaults are the file's"), Defaults.LinemanSpacingYards == Shipped.LinemanSpacingYards
        && Defaults.LineSetbackYards == Shipped.LineSetbackYards && Defaults.ShadeYards == Shipped.ShadeYards && Defaults.BoundaryMarginYards == Shipped.BoundaryMarginYards);
    TestNotNull(TEXT("Formation names compare as the plays' do, ignoring case"), PSFormations::FindFormation(Shipped, TEXT("trips right")));
    TestNotNull(TEXT("Fronts are keyed as run_fits.json keys them"), PSFormations::FindFront(Shipped, TEXT("4-3")));
    TestNotNull(TEXT("Shells as coverage_matchups.json keys them"), PSFormations::FindShell(Shipped, TEXT("Cover2")));
    TestTrue(TEXT("An empty shell or None places nobody"), PSFormations::IsNoShell(TEXT("")) && PSFormations::IsNoShell(TEXT("None")) && !PSFormations::IsNoShell(TEXT("Cover3")));

    FPSFormationCatalog Broken = Shipped;
    Broken.LinemanSpacingYards = 0.f;
    const FPSTechniqueDef Repeated = Broken.Techniques[0];
    Broken.Techniques.Add(Repeated);
    Broken.OffenseFormations[0].Strength = TEXT("Up");
    FPSFormationSpawnPoint Lineman;
    Lineman.Role = EPlayerRole::OffensiveLineman;
    Broken.OffenseFormations[0].Slots.Add(Lineman);
    const FPSOffenseFormationDef Twice = Broken.OffenseFormations[1];
    Broken.OffenseFormations.Add(Twice);
    Broken.FrontAlignments[0].Slots[0].Technique = TEXT("8");
    Broken.ShellAlignments[0].Slots[0].Role = EPlayerRole::Linebacker;
    const TArray<FString> Found = PSFormations::Validate(Broken);
    auto Mentions = [&Found](const TCHAR* Text)
    {
        return Found.ContainsByPredicate([Text](const FString& Problem) { return Problem.Contains(Text); });
    };
    TestTrue(TEXT("No line spacing is caught"), Mentions(TEXT("LinemanSpacingYards")));
    TestTrue(TEXT("A technique listed twice is caught"), Mentions(TEXT("Techniques[")));
    TestTrue(TEXT("A strength that isn't a side is caught"), Mentions(TEXT("Strength is Right or Left")));
    TestTrue(TEXT("A slot for a lineman is caught"), Mentions(TEXT("doesn't take a slot here")));
    TestTrue(TEXT("A formation listed twice is caught"), Mentions(TEXT("listed twice")));
    TestTrue(TEXT("An unknown technique is caught"), Mentions(TEXT("unknown technique '8'")));
    TestTrue(TEXT("A shell placing a linebacker is caught"), Mentions(TEXT("a shell places the defensive backs")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The offense lines up in its formation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFormationOffenseTest,
    "PlaySports.Formations.OffenseLinesUpInItsFormation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFormationOffenseTest::RunTest(const FString& Parameters)
{
    using namespace PSFormationTests;
    const FPSPlayRecognitionTuning Reads;
    const TArray<EPlayerRole> Eleven = Personnel(1, 1, 3);

    // Trips Right: the three receivers right of the ball, the tight end left, the quarterback
    // under center.
    const FPSLineupResult Trips = LineUp(Eleven, OffenseCall(TEXT("Trips Right")));
    TestTrue(TEXT("Trips Right lines up from the data, everyone on a slot"), Trips.bFormationFound && Trips.RoleFallbacks == 0);
    TestEqual(TEXT("...strong to the right"), Trips.StrongSide, 1);
    const TArray<FVector> TripsReceivers = SpotsOf(Eleven, Trips.Spots, EPlayerRole::WideReceiver);
    TestTrue(TEXT("...three receivers right of the ball"), TripsReceivers.Num() == 3
        && !TripsReceivers.ContainsByPredicate([](const FVector& Spot) { return Spot.Y <= 0.0; }));
    TestTrue(TEXT("...the tight end on the other side"), SpotsOf(Eleven, Trips.Spots, EPlayerRole::TightEnd)[0].Y < 0.0);
    const double UnderCenterDepth = LineX - SpotsOf(Eleven, Trips.Spots, EPlayerRole::Quarterback)[0].X;
    TestTrue(TEXT("...the quarterback under center"), UnderCenterDepth > 0.0 && UnderCenterDepth <= Reads.UnderCenterMaxDepth);

    // The line: centred on the ball, the catalog's spacing apart.
    const TArray<FVector> Line = SpotsOf(Eleven, Trips.Spots, EPlayerRole::OffensiveLineman);
    const double Spacing = PSField::YardsToCentimetres(PSFormations::GetCatalog().LinemanSpacingYards);
    bool bLineSpaced = Line.Num() == 5 && Near(Line[2].Y, 0.0);
    for (int32 Index = 1; Index < Line.Num(); ++Index)
    {
        bLineSpaced &= Near(Line[Index].Y - Line[Index - 1].Y, Spacing) && Line[Index].X < LineX;
    }
    TestTrue(TEXT("The line is centred on the ball, the catalog's spacing apart"), bLineSpaced);

    // Trips Left mirrors it.
    const FPSLineupResult Mirror = LineUp(Eleven, OffenseCall(TEXT("Trips Left")));
    TestEqual(TEXT("Trips Left is strong to the left"), Mirror.StrongSide, -1);
    bool bMirrored = Mirror.bFormationFound;
    for (int32 Index = 0; Index < Eleven.Num(); ++Index)
    {
        // The line is the same either way: it is centred on the ball.
        const bool bLineman = Eleven[Index] == EPlayerRole::OffensiveLineman;
        bMirrored &= Near(Mirror.Spots[Index].X, Trips.Spots[Index].X)
            && Near(Mirror.Spots[Index].Y, bLineman ? Trips.Spots[Index].Y : -Trips.Spots[Index].Y);
    }
    TestTrue(TEXT("...everyone but the line mirrored across the ball"), bMirrored);

    // The shotgun: the same personnel, the quarterback deep.
    const FPSLineupResult Gun = LineUp(Eleven, OffenseCall(TEXT("Shotgun")));
    const double ShotgunDepth = LineX - SpotsOf(Eleven, Gun.Spots, EPlayerRole::Quarterback)[0].X;
    TestTrue(TEXT("The shotgun quarterback stands deeper than the pistol"), Gun.bFormationFound && ShotgunDepth > Reads.PistolMaxDepth);
    bool bDiffers = false;
    for (int32 Index = 0; Index < Eleven.Num(); ++Index)
    {
        bDiffers |= !Gun.Spots[Index].Equals(Trips.Spots[Index], 1.0);
    }
    TestTrue(TEXT("Two formations of one personnel line up differently"), bDiffers);

    // The I: the tailback behind the fullback, both straight behind the quarterback.
    const TArray<EPlayerRole> TwentyOne = Personnel(2, 1, 2);
    const FPSLineupResult IForm = LineUp(TwentyOne, OffenseCall(TEXT("I-Form")));
    const TArray<FVector> Backs = SpotsOf(TwentyOne, IForm.Spots, EPlayerRole::RunningBack);
    const FVector Passer = SpotsOf(TwentyOne, IForm.Spots, EPlayerRole::Quarterback)[0];
    TestTrue(TEXT("I-Form stacks its two backs behind the quarterback"), IForm.bFormationFound && Backs.Num() == 2
        && Near(Backs[0].Y, Passer.Y) && Near(Backs[1].Y, Passer.Y) && Backs[1].X < Passer.X);
    TestTrue(TEXT("...the first back (the ball carrier) the deeper"), Backs.Num() == 2 && Backs[0].X < Backs[1].X);

    // More players than the formation has slots: the extra one lines up by role.
    const FPSLineupResult Extra = LineUp(Personnel(1, 1, 4), OffenseCall(TEXT("Trips Right")));
    TestEqual(TEXT("A fourth receiver in a three-receiver formation lines up by role"), Extra.RoleFallbacks, 1);

    // A formation the data doesn't know: by role, as before formations came from data.
    const FPSLineupResult Unknown = LineUp(Eleven, OffenseCall(TEXT("Wishbone")));
    const TArray<FVector> ByRole = APSFieldGrid::ComputeLineup(Eleven, LineX);
    bool bByRole = !Unknown.bFormationFound && Unknown.Spots.Num() == ByRole.Num();
    for (int32 Index = 0; bByRole && Index < ByRole.Num(); ++Index)
    {
        bByRole &= Unknown.Spots[Index].Equals(ByRole[Index], 0.01);
    }
    TestTrue(TEXT("An unknown formation lines up by role"), bByRole);
    const TArray<FVector> Delegated = APSFieldGrid::ComputeLineup(Eleven, LineX, OffenseCall(TEXT("Trips Right")));
    TestTrue(TEXT("APSFieldGrid::ComputeLineup delegates to the formations"), Delegated.Num() == Trips.Spots.Num() && Delegated[3].Equals(Trips.Spots[3], 0.01));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The defense lines up in its front and shell
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFormationDefenseTest,
    "PlaySports.Formations.DefenseLinesUpInItsFrontAndShell",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFormationDefenseTest::RunTest(const FString& Parameters)
{
    using namespace PSFormationTests;

    // Against Ace: two inline tight ends and two wide receivers, balanced, so strong right.
    const TArray<EPlayerRole> Twelve = Personnel(1, 2, 2);
    const TArray<FPSAlignedPlayer> Ace = Align(Twelve, LineUp(Twelve, OffenseCall(TEXT("Ace"))).Spots);
    const TArray<double> AceLine = LineYs(Ace);
    const TArray<EPlayerRole> Base = Defense(4, 3, 4);
    const FPSLineupResult Over = LineUp(Base, DefenseCall(TEXT("4-3"), TEXT("Cover2")), &Ace);
    if (!TestTrue(TEXT("The 4-3 and Cover 2 line up from the data, everyone on a slot"), Over.bFrontFound && Over.bShellFound && Over.RoleFallbacks == 0)
        || !TestEqual(TEXT("Ace has five linemen"), AceLine.Num(), 5))
    {
        return false;
    }
    TestEqual(TEXT("...to the strength it reads, the right"), Over.StrongSide, 1);

    // The front's techniques over the line: the weak end in a 5 (outside the left tackle), the
    // nose in a 1 (the center's weak shade), the 3-technique on the strong guard's outside shade,
    // the strong end in a 5.
    const TArray<FVector> Linemen = SpotsOf(Base, Over.Spots, EPlayerRole::DefensiveLineman);
    TestTrue(TEXT("Weak end: a 5-technique"), Near(Linemen[0].Y, AceLine[0] - Shade()));
    TestTrue(TEXT("Nose: a 1-technique to the weak side"), Near(Linemen[1].Y, AceLine[2] - Shade()));
    TestTrue(TEXT("Tackle: a 3-technique to the strength"), Near(Linemen[2].Y, AceLine[3] + Shade()));
    TestTrue(TEXT("Strong end: a 5-technique"), Near(Linemen[3].Y, AceLine[4] + Shade()));
    TestTrue(TEXT("...all across the line from the ball"), !Linemen.ContainsByPredicate([](const FVector& Spot) { return Spot.X <= LineX; }));
    const TArray<FVector> Backers = SpotsOf(Base, Over.Spots, EPlayerRole::Linebacker);
    double StrongTightEndY = 0.0;
    for (const FPSAlignedPlayer& Player : Ace)
    {
        StrongTightEndY = Player.Role == EPlayerRole::TightEnd ? FMath::Max(StrongTightEndY, Player.Location.Y) : StrongTightEndY;
    }
    TestTrue(TEXT("The Sam lines up outside the strong tight end"), Backers.Num() == 3 && Backers[2].Y > StrongTightEndY);
    TestTrue(TEXT("...the linebackers behind the line"), Backers.Num() == 3 && Backers[0].X > Linemen[0].X && Backers[1].X > Linemen[0].X);

    // Cover 2: two deep safeties, one each side; the corners over the widest receivers.
    double WideRight = 0.0;
    double WideLeft = 0.0;
    for (const FPSAlignedPlayer& Player : Ace)
    {
        if (Player.Role == EPlayerRole::WideReceiver)
        {
            WideRight = FMath::Max(WideRight, Player.Location.Y);
            WideLeft = FMath::Min(WideLeft, Player.Location.Y);
        }
    }
    const TArray<FVector> Secondary = SpotsOf(Base, Over.Spots, EPlayerRole::DefensiveBack);
    const double DeepDepth = PSField::YardsToCentimetres(10.f);
    TestTrue(TEXT("Cover 2's safeties are deep, one either side"), Secondary[0].X - LineX >= DeepDepth && Secondary[1].X - LineX >= DeepDepth
        && Secondary[0].Y * Secondary[1].Y < 0.0);
    TestTrue(TEXT("...its corners over the widest receivers"), Near(Secondary[2].Y, WideRight) && Near(Secondary[3].Y, WideLeft));

    // Against Trips Left the front turns to the strength it reads: the 3-technique to the left.
    const TArray<EPlayerRole> Eleven = Personnel(1, 1, 3);
    const TArray<FPSAlignedPlayer> TripsLeft = Align(Eleven, LineUp(Eleven, OffenseCall(TEXT("Trips Left"))).Spots);
    const FPSLineupResult Turned = LineUp(Base, DefenseCall(TEXT("4-3"), TEXT("Cover2")), &TripsLeft);
    const TArray<double> TripsLine = LineYs(TripsLeft);
    TestEqual(TEXT("Against trips left the defense reads the strength left"), Turned.StrongSide, -1);
    TestTrue(TEXT("...its 3-technique on the left guard's outside shade"), Near(SpotsOf(Base, Turned.Spots, EPlayerRole::DefensiveLineman)[2].Y, TripsLine[1] - Shade()));

    // Nickel Cover 1 against Trips Right: the corners over each side's #1, the nickel over trips'
    // #2, and the fifth back, with nobody left on the weak side, travels to trips' #3.
    const TArray<FPSAlignedPlayer> TripsRight = Align(Eleven, LineUp(Eleven, OffenseCall(TEXT("Trips Right"))).Spots);
    TArray<double> Trips;
    double WeakTightEnd = 0.0;
    for (const FPSAlignedPlayer& Player : TripsRight)
    {
        if (Player.Role == EPlayerRole::WideReceiver)
        {
            Trips.Add(Player.Location.Y);
        }
        WeakTightEnd = Player.Role == EPlayerRole::TightEnd ? Player.Location.Y : WeakTightEnd;
    }
    Trips.Sort([](double A, double B) { return A > B; });
    const TArray<EPlayerRole> Nickel = Defense(4, 2, 5);
    const FPSLineupResult Man = LineUp(Nickel, DefenseCall(TEXT("Nickel"), TEXT("Cover1")), &TripsRight);
    const TArray<FVector> ManBacks = SpotsOf(Nickel, Man.Spots, EPlayerRole::DefensiveBack);
    if (TestTrue(TEXT("Nickel Cover 1 lines up from the data"), Man.bFrontFound && Man.bShellFound && Man.RoleFallbacks == 0 && ManBacks.Num() == 5 && Trips.Num() == 3))
    {
        TestTrue(TEXT("The free safety in the middle, deep"), Near(ManBacks[0].Y, 0.0) && ManBacks[0].X - LineX >= DeepDepth);
        TestTrue(TEXT("The corners over each side's #1"), Near(ManBacks[1].Y, Trips[0]) && Near(ManBacks[2].Y, WeakTightEnd));
        TestTrue(TEXT("The nickel over trips' #2"), Near(ManBacks[3].Y, Trips[1]));
        TestTrue(TEXT("The fifth back travels to trips' #3"), Near(ManBacks[4].Y, Trips[2]));
    }

    // A front the data doesn't know lines up by role.
    const FPSLineupResult Unknown = LineUp(Base, DefenseCall(TEXT("5-2 Monster"), TEXT("None")), &Ace);
    TestTrue(TEXT("An unknown front lines up by role"), !Unknown.bFrontFound && !Unknown.bShellFound && Unknown.Spots[0].Equals(APSFieldGrid::ComputeLineup(Base, LineX)[0], 0.01));
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Every shipped call lines up from the data
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFormationShippedCallsTest,
    "PlaySports.Formations.EveryShippedCallLinesUp",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFormationShippedCallsTest::RunTest(const FString& Parameters)
{
    using namespace PSFormationTests;

    UWorld* World = CreateTestWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Play-call subsystem"), PlayCall))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSPlaybookGenerator* Generator = NewObject<UPSPlaybookGenerator>();
    const FPSPersonnelCatalog& Packages = Generator->GetPersonnel();
    TArray<FPSPlayDefinition> Plays = PlayCall->GetPlaybook();
    const int32 PlaybookPlays = Plays.Num();
    Plays.Append(Generator->BuildLibrary(true, TArray<FString>()));
    Plays.Append(Generator->BuildLibrary(false, TArray<FString>()));
    TestTrue(TEXT("The playbook and the generator's library are there"), PlaybookPlays > 20 && Plays.Num() > PlaybookPlays + 300);

    // A defense lines up against the default offensive package's first formation, as the art
    // check lines it up.
    const FPSPersonnelPackage* BaseOffense = Packages.Packages.FindByPredicate([&Packages](const FPSPersonnelPackage& Package) { return Package.PackageId == Packages.DefaultOffensePackage; });
    const FString OpposingFormation = BaseOffense && BaseOffense->Formations.Num() > 0 ? BaseOffense->Formations[0] : FString();
    const TArray<EPlayerRole> OpposingRoles = RolesOf(PSPlaybookGenerator::GetFormationRoles(Packages, OpposingFormation, true));
    const FPSLineupCall OpposingCall = OffenseCall(*OpposingFormation);
    const TArray<FPSAlignedPlayer> Opponents = Align(OpposingRoles, LineUp(OpposingRoles, OpposingCall).Spots);

    int32 Failures = 0;
    TSet<FString> Reported;
    for (const FPSPlayDefinition& Play : Plays)
    {
        const TArray<EPlayerRole> Roles = RolesOf(PSPlaybookGenerator::GetFormationRoles(Packages, Play.Formation, Play.bIsOffensivePlay));
        FPSLineupCall Call;
        if (Play.bIsOffensivePlay)
        {
            Call.OffenseFormation = Play.Formation;
        }
        else
        {
            Call.DefenseFront = Play.Front;
            Call.DefenseShell = Play.CoverageShell;
        }
        const FPSLineupResult Result = LineUp(Roles, Call, Play.bIsOffensivePlay ? nullptr : &Opponents);
        const bool bFromData = Play.bIsOffensivePlay ? Result.bFormationFound
            : (Result.bFrontFound && (Result.bShellFound || PSFormations::IsNoShell(Play.CoverageShell)));
        if (Roles.Num() != 11 || !bFromData || Result.RoleFallbacks > 0)
        {
            ++Failures;
            const FString Key = FString::Printf(TEXT("%s/%s/%s/%s"), Play.bIsOffensivePlay ? TEXT("O") : TEXT("D"), *Play.Formation, *Play.Front, *Play.CoverageShell);
            if (!Reported.Contains(Key))
            {
                Reported.Add(Key);
                AddError(FString::Printf(TEXT("%s (%s): %d players, %s, %d lined up by role"), *Play.PlayId.ToString(), *Key, Roles.Num(),
                    bFromData ? TEXT("from the data") : TEXT("not in Data/formations.json"), Result.RoleFallbacks));
            }
        }
    }
    TestEqual(*FString::Printf(TEXT("All %d shipped and generated calls line up from the data, everyone on a slot"), Plays.Num()), Failures, 0);
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Epic 80 reads each formation as it says
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFormationRecognitionTest,
    "PlaySports.Formations.RecognitionReadsTheLineups",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFormationRecognitionTest::RunTest(const FString& Parameters)
{
    using namespace PSFormationTests;

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSPlayRecognitionTuning Reads;
    FPSPersonnelCatalog Packages;
    if (!TestTrue(TEXT("The recognition tuning loads"), Ingestion->LoadPlayRecognitionTuningFromJson(UPSPlayRecognitionSubsystem::GetDefaultTuningPath(), Reads))
        || !TestTrue(TEXT("The personnel packages load"), Ingestion->LoadPersonnelCatalogFromJson(UPSPersonnelManager::GetDefaultCatalogPath(), Packages)))
    {
        return false;
    }

    // Every formation lined up in its package reads as its QBAlignment, Backfield and Strength.
    const FVector Ball(LineX, 0.f, 0.f);
    TMap<FString, FPSFormationRead> ReadsByFormation;
    for (const FPSOffenseFormationDef& Def : PSFormations::GetCatalog().OffenseFormations)
    {
        const TArray<EPlayerRole> Roles = RolesOf(PSPlaybookGenerator::GetFormationRoles(Packages, Def.Formation, true));
        const FPSLineupResult Lineup = PSFormations::LineUp(PSFormations::GetCatalog(), Roles, Ball, OffenseCall(*Def.Formation), nullptr, Reads);
        const FPSFormationRead Read = PSPlayRecognition::ClassifyFormation(Align(Roles, Lineup.Spots), Ball, Reads);
        ReadsByFormation.Add(Def.Formation, Read);
        TestTrue(*FString::Printf(TEXT("%s is read as %s, %s backfield, strong %s (read %s, %s, %d)"), *Def.Formation, *Def.QBAlignment.ToString(),
            *Def.Backfield.ToString(), *Def.Strength.ToString(), *Read.QBAlignment.ToString(), *Read.Backfield.ToString(), Read.StrongSide),
            Read.bValid && Read.QBAlignment == Def.QBAlignment && Read.Backfield == Def.Backfield
                && Read.StrongSide == (Def.Strength == FName(TEXT("Left")) ? -1 : 1));
    }

    // The payoff: the same 11 personnel reads as different looks, by formation.
    auto ClassOf = [&ReadsByFormation](const TCHAR* Formation)
    {
        const FPSFormationRead* Read = ReadsByFormation.Find(Formation);
        return Read ? Read->ClassId : NAME_None;
    };
    TestEqual(TEXT("Trips Right reads as trips"), ClassOf(TEXT("Trips Right")), FName(TEXT("Trips")));
    TestEqual(TEXT("Trips Left reads as trips"), ClassOf(TEXT("Trips Left")), FName(TEXT("Trips")));
    TestTrue(TEXT("...strong to the left"), ReadsByFormation.Contains(TEXT("Trips Left")) && ReadsByFormation[TEXT("Trips Left")].StrongSide == -1);
    TestEqual(TEXT("Twins reads as a singleback set"), ClassOf(TEXT("Twins")), FName(TEXT("Singleback")));
    TestEqual(TEXT("Shotgun reads as the shotgun"), ClassOf(TEXT("Shotgun")), FName(TEXT("Shotgun")));
    TestEqual(TEXT("I-Form reads as the I"), ClassOf(TEXT("I-Form")), FName(TEXT("IForm")));
    TestEqual(TEXT("Strong-I reads as the I"), ClassOf(TEXT("Strong-I")), FName(TEXT("IForm")));
    TestEqual(TEXT("Ace's two tight ends read heavy"), ClassOf(TEXT("Ace")), FName(TEXT("Heavy")));
    TestTrue(TEXT("Trips leans less to the run than the I"), ReadsByFormation.Contains(TEXT("Trips Right")) && ReadsByFormation.Contains(TEXT("I-Form"))
        && ReadsByFormation[TEXT("Trips Right")].RunLean < ReadsByFormation[TEXT("I-Form")].RunLean);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- In a game, each call lines its side up
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFormationLiveTest,
    "PlaySports.Formations.PlayCallsLineTheSidesUp",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFormationLiveTest::RunTest(const FString& Parameters)
{
    using namespace PSFormationTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSPlayRecognitionSubsystem* Recognition = UPSPlayRecognitionSubsystem::Get(World);
    UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    UPSRoster* Roster = LoadSampleRoster();
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Recognition"), Recognition)
        || !TestNotNull(TEXT("Field snapshot"), Field) || !TestNotNull(TEXT("Roster"), Roster))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    UPSPersonnelManager* Manager = NewObject<UPSPersonnelManager>();
    Manager->Initialize(Roster);
    Manager->LoadCatalogFromJson(UPSPersonnelManager::GetDefaultCatalogPath());
    const TArray<APSPlayerPawn*> Pawns = APSFieldGrid::SpawnPlayersFromRoster(Manager->GetStartingLineup(), LineX, World);
    if (!TestEqual(TEXT("22 pawns take the field"), Pawns.Num(), 22))
    {
        DestroyTestWorld(World);
        return false;
    }
    GiveAIControllers(World, Pawns);
    Manager->BindPawns(Pawns);
    Manager->BindToBus(Bus);
    Manager->BeginNewPlay(LineX, 1);

    TArray<FPSTelemetryLineupEvent> Lineups;
    const FDelegateHandle LineupHandle = Bus->OnLineupMC.AddLambda([&Lineups](const FPSTelemetryLineupEvent& Event) { Lineups.Add(Event); });
    const TArray<APSPlayerPawn*> Receivers = SidePawns(Pawns, EPSTeamSide::Offense, EPlayerRole::WideReceiver);
    const TArray<APSPlayerPawn*> Linemen = SidePawns(Pawns, EPSTeamSide::Defense, EPlayerRole::DefensiveLineman);
    const TArray<APSPlayerPawn*> Backs = SidePawns(Pawns, EPSTeamSide::Defense, EPlayerRole::DefensiveBack);

    FPSSituationContext FirstAndTen;
    FirstAndTen.Down = 1;
    FirstAndTen.Distance = 10;
    FirstAndTen.YardLine = 20;
    PlayCall->OpenPlayCall(FirstAndTen);

    // The offense calls a play from Trips Right: it lines up in it.
    TestTrue(TEXT("The offense calls Slant-Flat from Trips Right"), PlayCall->CallPlay(TEXT("Offense_SlantFlat"), EPSPlayCaller::Human));
    TestTrue(TEXT("...and lines up in it: three receivers right of the ball"), Receivers.Num() == 3
        && !Receivers.ContainsByPredicate([](const APSPlayerPawn* Pawn) { return Pawn->GetActorLocation().Y <= 0.0; }));
    if (TestEqual(TEXT("The lineup is announced"), Lineups.Num(), 1))
    {
        TestTrue(TEXT("...for the offense, in Trips Right, from the data"), Lineups[0].bOffense && Lineups[0].Formation == TEXT("Trips Right") && Lineups[0].bFromData && Lineups[0].StrongSide == 1);
    }

    // The defense calls 4-3 Cover 2: the 3-technique to the strength, the corners over the #1s.
    TestTrue(TEXT("The defense calls 4-3 Cover 2"), PlayCall->CallPlay(TEXT("Defense_43Cover2"), EPSPlayCaller::CPU));
    TArray<double> Line = LineYs(AlignPawns(Pawns, EPSTeamSide::Offense));
    TestTrue(TEXT("...its 3-technique on the right guard's outside shade"), Line.Num() == 5 && AnyAtY(Linemen, Line[3] + Shade()));
    double Widest = 0.0;
    for (const APSPlayerPawn* Receiver : Receivers)
    {
        Widest = FMath::Max(Widest, Receiver->GetActorLocation().Y);
    }
    TestTrue(TEXT("...a corner over trips' #1"), AnyAtY(Backs, Widest));
    TestTrue(TEXT("The defense's lineup is announced"), Lineups.Num() == 2 && !Lineups[1].bOffense && Lineups[1].Front == TEXT("4-3") && Lineups[1].bFromData);

    // The offense changes to Trips Left: it lines up again, and so does the defense, to the new
    // strength.
    TestTrue(TEXT("The offense calls the deep post from Trips Left"), PlayCall->CallPlay(TEXT("Offense_DeepPost"), EPSPlayCaller::Human));
    TestTrue(TEXT("...three receivers left of the ball"), !Receivers.ContainsByPredicate([](const APSPlayerPawn* Pawn) { return Pawn->GetActorLocation().Y >= 0.0; }));
    Line = LineYs(AlignPawns(Pawns, EPSTeamSide::Offense));
    TestTrue(TEXT("The defense lines up again: its 3-technique now on the left guard"), Line.Num() == 5 && AnyAtY(Linemen, Line[1] - Shade()));
    if (TestEqual(TEXT("Both lineups are announced"), Lineups.Num(), 4))
    {
        TestTrue(TEXT("...the offense's, then the defense's against it, strong left"), Lineups[2].bOffense && !Lineups[3].bOffense && Lineups[3].StrongSide == -1);
    }

    // At the snap the defense reads the formation the offense called.
    Field->Invalidate();
    FPSTelemetrySnapEvent Snap;
    Snap.Down = 1;
    Snap.Distance = 10;
    Snap.YardLine = 20;
    Snap.LineOfScrimmage = FVector(LineX, 0.f, 0.f);
    Bus->PublishSnap(Snap);
    TestEqual(TEXT("At the snap the defense reads trips"), Recognition->GetFormationRead().ClassId, FName(TEXT("Trips")));
    TestEqual(TEXT("...strong to the left"), Recognition->GetFormationRead().StrongSide, -1);
    TestEqual(TEXT("...the quarterback under center"), Recognition->GetFormationRead().QBAlignment, FName(TEXT("UnderCenter")));

    // A quarterback in the shotgun holds his depth on a shorter drop, and drops on a deeper one.
    const TArray<APSPlayerPawn*> Passers = SidePawns(Pawns, EPSTeamSide::Offense, EPlayerRole::Quarterback);
    if (TestEqual(TEXT("One quarterback"), Passers.Num(), 1))
    {
        Passers[0]->SetActorLocation(FVector(LineX - 500.f, 0.f, 100.f));
        FPSPlayDefinition Drop;
        Drop.bIsOffensivePlay = true;
        FPSPlayAssignment Spot;
        Spot.Role = EPlayerRole::Quarterback;
        Spot.Kind = EPSAssignmentKind::Route;
        Spot.FormationOffset = FVector(-300.f, 0.f, 0.f);
        Drop.Assignments.Add(Spot);
        const TArray<FPSResolvedAssignment> Short = PSPlayResolution::ResolvePlay(Drop, Passers, nullptr, Snap.LineOfScrimmage, false);
        TestTrue(TEXT("A three-yard drop from the shotgun holds him at five yards"), Short.Num() == 1 && Short[0].Waypoints.Num() == 1 && Near(Short[0].Waypoints[0].X, LineX - 500.0));
        Drop.Assignments[0].FormationOffset = FVector(-700.f, 0.f, 0.f);
        const TArray<FPSResolvedAssignment> Deep = PSPlayResolution::ResolvePlay(Drop, Passers, nullptr, Snap.LineOfScrimmage, false);
        TestTrue(TEXT("...a seven-yard drop takes him to seven"), Deep.Num() == 1 && Deep[0].Waypoints.Num() == 1 && Near(Deep[0].Waypoints[0].X, LineX - 700.0));
    }

    // The next play starts by role again until its calls.
    Manager->BeginNewPlay(LineX, 2);
    TestEqual(TEXT("A new play forgets the last play's calls"), Manager->GetLineupCall().OffenseFormation, FString());

    Bus->OnLineupMC.Remove(LineupHandle);
    Manager->UnbindFromBus();
    DestroyTestWorld(World);
    return true;
}

#endif
