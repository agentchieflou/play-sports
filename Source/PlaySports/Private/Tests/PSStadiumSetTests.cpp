// PSStadiumSetTests.cpp -- Epic 147.1: the goal posts, benches and stands, built from data on the field's frame
//
// Tests covered:
//   1. Data/stadium_set.json loads through UPSDataIngestion, equals FPSStadiumSetStyle's defaults and
//      validates; an unsound style is caught.
//   2. The layout sits on the field's frame (PSField): a goal post on each end line, its crossbar
//      over the end line at the crossbar's height, the uprights the crossbar's width apart and
//      rising from it, the base behind the end line; a bench past each sideline inside the
//      out-of-bounds depth, between the yard lines the data names; every stand tier outside the
//      ground, each rising above the one in front.
//   3. The field grid spawns the set once and it builds in a world: one instance per piece in its
//      kind's instanced mesh, nothing that collides, and a rebuild replaces rather than adds.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PSDataIngestion.h"
#include "PSFieldDimensions.h"
#include "PSFieldGrid.h"
#include "PSStadiumSet.h"
#include "PSStadiumSetTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSStadiumSetTests
{
    static TArray<FPSStadiumPiece> PiecesOfKind(const TArray<FPSStadiumPiece>& Pieces, EPSStadiumPieceKind Kind)
    {
        return Pieces.FilterByPredicate([Kind](const FPSStadiumPiece& Piece) { return Piece.Kind == Kind; });
    }

    /** A box piece's footprint on the ground (rotation-free pieces only). */
    static FBox2D Footprint(const FPSStadiumPiece& Piece)
    {
        const FVector2D Center(Piece.Center.X, Piece.Center.Y);
        const FVector2D Half(Piece.Size.X * 0.5, Piece.Size.Y * 0.5);
        return FBox2D(Center - Half, Center + Half);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The style's data file
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStadiumSetDataTest,
    "PlaySports.Field.StadiumSetData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStadiumSetDataTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSStadiumSetStyle Loaded;
    if (!TestTrue(TEXT("Data/stadium_set.json loads"), Ingestion->LoadStadiumSetStyleFromJson(APSStadiumSet::GetDefaultStylePath(), Loaded)))
    {
        return false;
    }
    TestEqual(TEXT("...and validates"), APSStadiumSet::ValidateStyle(Loaded).Num(), 0);

    const FPSStadiumSetStyle Defaults;
    TestEqual(TEXT("BoxMeshPath"), Loaded.BoxMeshPath, Defaults.BoxMeshPath);
    TestEqual(TEXT("CylinderMeshPath"), Loaded.CylinderMeshPath, Defaults.CylinderMeshPath);
    TestEqual(TEXT("MeshSizeCm"), Loaded.MeshSizeCm, Defaults.MeshSizeCm);
    TestEqual(TEXT("MaterialPath"), Loaded.MaterialPath, Defaults.MaterialPath);
    TestEqual(TEXT("ColorParameter"), Loaded.ColorParameter, Defaults.ColorParameter);
    TestEqual(TEXT("GoalPostColor"), Loaded.GoalPostColor, Defaults.GoalPostColor);
    TestEqual(TEXT("CrossbarHeightYards"), Loaded.CrossbarHeightYards, Defaults.CrossbarHeightYards);
    TestEqual(TEXT("CrossbarWidthYards"), Loaded.CrossbarWidthYards, Defaults.CrossbarWidthYards);
    TestEqual(TEXT("UprightHeightYards"), Loaded.UprightHeightYards, Defaults.UprightHeightYards);
    TestEqual(TEXT("PostDiameterYards"), Loaded.PostDiameterYards, Defaults.PostDiameterYards);
    TestEqual(TEXT("BasePostDiameterYards"), Loaded.BasePostDiameterYards, Defaults.BasePostDiameterYards);
    TestEqual(TEXT("BaseSetbackYards"), Loaded.BaseSetbackYards, Defaults.BaseSetbackYards);
    TestEqual(TEXT("BenchColor"), Loaded.BenchColor, Defaults.BenchColor);
    TestEqual(TEXT("BenchFromYardLine"), Loaded.BenchFromYardLine, Defaults.BenchFromYardLine);
    TestEqual(TEXT("BenchToYardLine"), Loaded.BenchToYardLine, Defaults.BenchToYardLine);
    TestEqual(TEXT("BenchDistanceYards"), Loaded.BenchDistanceYards, Defaults.BenchDistanceYards);
    TestEqual(TEXT("BenchDepthYards"), Loaded.BenchDepthYards, Defaults.BenchDepthYards);
    TestEqual(TEXT("BenchHeightYards"), Loaded.BenchHeightYards, Defaults.BenchHeightYards);
    TestEqual(TEXT("StandColor"), Loaded.StandColor, Defaults.StandColor);
    TestEqual(TEXT("StandAltColor"), Loaded.StandAltColor, Defaults.StandAltColor);
    TestEqual(TEXT("StandTiers"), Loaded.StandTiers, Defaults.StandTiers);
    TestEqual(TEXT("StandGapYards"), Loaded.StandGapYards, Defaults.StandGapYards);
    TestEqual(TEXT("StandTierDepthYards"), Loaded.StandTierDepthYards, Defaults.StandTierDepthYards);
    TestEqual(TEXT("StandTierRiseYards"), Loaded.StandTierRiseYards, Defaults.StandTierRiseYards);
    TestTrue(TEXT("bCastShadows"), Loaded.bCastShadows == Defaults.bCastShadows);

    FPSStadiumSetStyle Broken;
    Broken.GoalPostColor = TEXT("yellow");
    Broken.CrossbarHeightYards = 0.f;
    Broken.BenchFromYardLine = 80.f;
    TestEqual(TEXT("An unsound style is caught, one problem each"), APSStadiumSet::ValidateStyle(Broken).Num(), 3);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The layout is the field's frame
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStadiumSetLayoutTest,
    "PlaySports.Field.StadiumSetLayout",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStadiumSetLayoutTest::RunTest(const FString& Parameters)
{
    using namespace PSStadiumSetTests;

    const FPSFieldDimensions& Field = PSField::GetDimensions();
    const FPSStadiumSetStyle Style;
    const TArray<FPSStadiumPiece> Pieces = APSStadiumSet::ComputePieces(Field, Style);
    const double Tolerance = 0.01;
    const double CrossbarZ = PSField::YardsToCentimetres(Style.CrossbarHeightYards);
    const double CrossbarWidth = PSField::YardsToCentimetres(Style.CrossbarWidthYards);
    const double SidelineY = PSField::SidelineY();
    const double OutDepth = PSField::YardsToCentimetres(Field.OutOfBoundsDepthYards);

    // The goal posts: five pieces at each end, all cylinders.
    const TArray<FPSStadiumPiece> Posts = PiecesOfKind(Pieces, EPSStadiumPieceKind::GoalPost);
    TestEqual(TEXT("Five goal-post pieces at each end"), Posts.Num(), 10);
    for (const bool bFar : { false, true })
    {
        const double EndLineX = PSField::EndLineX(bFar);
        const double Outward = bFar ? 1.0 : -1.0;
        const TArray<FPSStadiumPiece> AtEnd = Posts.FilterByPredicate([EndLineX](const FPSStadiumPiece& Piece)
        {
            return FMath::Abs(Piece.Center.X - EndLineX) < 1000.0;
        });
        const TCHAR* End = bFar ? TEXT("far") : TEXT("near");
        TestEqual(*FString::Printf(TEXT("The %s goal post has its five pieces"), End), AtEnd.Num(), 5);
        int32 Crossbars = 0;
        int32 Uprights = 0;
        int32 Bases = 0;
        for (const FPSStadiumPiece& Piece : AtEnd)
        {
            TestTrue(TEXT("Every goal-post piece is a cylinder"), Piece.bCylinder);
            const bool bOverEndLine = FMath::IsNearlyEqual(Piece.Center.X, EndLineX, Tolerance);
            if (bOverEndLine && FMath::IsNearlyEqual(Piece.Center.Z, CrossbarZ, Tolerance) && FMath::IsNearlyEqual(Piece.Size.Z, CrossbarWidth, Tolerance))
            {
                ++Crossbars;
            }
            if (bOverEndLine && FMath::IsNearlyEqual(FMath::Abs(Piece.Center.Y), CrossbarWidth * 0.5, Tolerance)
                && FMath::IsNearlyEqual(Piece.Center.Z - Piece.Size.Z * 0.5, CrossbarZ, Tolerance))
            {
                ++Uprights;
            }
            if (Outward * (Piece.Center.X - EndLineX) > 0.0 && FMath::IsNearlyEqual(Piece.Center.Z - Piece.Size.Z * 0.5, 0.0, Tolerance))
            {
                ++Bases;
            }
        }
        TestEqual(*FString::Printf(TEXT("The %s crossbar is over the end line at the crossbar's height"), End), Crossbars, 1);
        TestEqual(*FString::Printf(TEXT("The %s uprights are the crossbar's width apart, rising from it"), End), Uprights, 2);
        TestEqual(*FString::Printf(TEXT("The %s base post stands on the ground behind the end line"), End), Bases, 1);
    }

    // The benches: past each sideline, inside the out-of-bounds depth, between the 30 and the 70.
    const TArray<FPSStadiumPiece> Benches = PiecesOfKind(Pieces, EPSStadiumPieceKind::Bench);
    if (TestEqual(TEXT("A bench past each sideline"), Benches.Num(), 2))
    {
        for (const FPSStadiumPiece& Bench : Benches)
        {
            const FBox2D Area = Footprint(Bench);
            TestTrue(TEXT("...out of bounds, inside the out-of-bounds depth"),
                FMath::Min(FMath::Abs(Area.Min.Y), FMath::Abs(Area.Max.Y)) > SidelineY && FMath::Max(FMath::Abs(Area.Min.Y), FMath::Abs(Area.Max.Y)) < SidelineY + OutDepth);
            TestEqual(TEXT("...from the 30"), Area.Min.X, static_cast<double>(PSField::YardLineToWorld(Style.BenchFromYardLine).X), Tolerance);
            TestEqual(TEXT("...to the 70"), Area.Max.X, static_cast<double>(PSField::YardLineToWorld(Style.BenchToYardLine).X), Tolerance);
        }
    }

    // The stands: four sides a tier, every one outside the ground, each tier higher than the last.
    TArray<FPSStadiumPiece> Stands = PiecesOfKind(Pieces, EPSStadiumPieceKind::Stand);
    Stands.Append(PiecesOfKind(Pieces, EPSStadiumPieceKind::StandAlt));
    TestEqual(TEXT("Four sides of stands per tier"), Stands.Num(), 4 * Style.StandTiers);
    const FBox2D Ground(FVector2D(PSField::EndLineX(false) - OutDepth, -(SidelineY + OutDepth)), FVector2D(PSField::EndLineX(true) + OutDepth, SidelineY + OutDepth));
    double LastHeight = 0.0;
    for (const FPSStadiumPiece& Stand : Stands)
    {
        const FBox2D Area = Footprint(Stand);
        const bool bOutside = Area.Min.X >= Ground.Max.X - Tolerance || Area.Max.X <= Ground.Min.X + Tolerance
            || Area.Min.Y >= Ground.Max.Y - Tolerance || Area.Max.Y <= Ground.Min.Y + Tolerance;
        TestTrue(TEXT("A stand tier stands outside the ground"), bOutside);
        TestTrue(TEXT("...on the ground"), FMath::IsNearlyEqual(Stand.Center.Z - Stand.Size.Z * 0.5, 0.0, Tolerance));
        LastHeight = FMath::Max(LastHeight, Stand.Size.Z);
    }
    TestEqual(TEXT("The top tier rises StandTiers times a tier's rise"), LastHeight, static_cast<double>(PSField::YardsToCentimetres(Style.StandTiers * Style.StandTierRiseYards)), 0.1);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The set builds in a world
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStadiumSetBuildTest,
    "PlaySports.Field.StadiumSetBuilds",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStadiumSetBuildTest::RunTest(const FString& Parameters)
{
    using namespace PSStadiumSetTests;

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("World"), World))
    {
        return false;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSFieldGrid* Grid = World->SpawnActor<APSFieldGrid>(APSFieldGrid::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    APSStadiumSet* Set = Grid ? Grid->SpawnStadiumSet() : nullptr;
    if (!TestNotNull(TEXT("The grid spawns the stadium set"), Set))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }
    TestTrue(TEXT("...and builds it from data"), Set->IsBuilt());
    TestTrue(TEXT("Asking again returns the same set"), Grid->SpawnStadiumSet() == Set);

    const TArray<FPSStadiumPiece> Pieces = APSStadiumSet::ComputePieces(PSField::GetDimensions(), APSStadiumSet::LoadStyle(APSStadiumSet::GetDefaultStylePath()));
    auto CheckKind = [this, Set, &Pieces](EPSStadiumPieceKind Kind, const TCHAR* Label)
    {
        UInstancedStaticMeshComponent* Mesh = Set->GetPieces(Kind);
        if (!TestNotNull(Label, Mesh))
        {
            return;
        }
        TestEqual(*FString::Printf(TEXT("%s: one instance per piece"), Label), Mesh->GetInstanceCount(), PiecesOfKind(Pieces, Kind).Num());
        TestTrue(*FString::Printf(TEXT("%s: nothing collides but the field's ground"), Label), Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    };
    CheckKind(EPSStadiumPieceKind::GoalPost, TEXT("Goal posts"));
    CheckKind(EPSStadiumPieceKind::Bench, TEXT("Benches"));
    CheckKind(EPSStadiumPieceKind::Stand, TEXT("Stands"));
    CheckKind(EPSStadiumPieceKind::StandAlt, TEXT("Alternate stands"));

    TestTrue(TEXT("A rebuild succeeds"), Set->BuildFromData());
    TestEqual(TEXT("...and replaces the goal posts rather than adding to them"), Set->GetPieces(EPSStadiumPieceKind::GoalPost)->GetInstanceCount(), 10);

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
