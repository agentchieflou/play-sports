// PSFieldSurfaceTests.cpp -- Epic 146.3: the field you see is built from data, on the game's frame
//
// Tests covered:
//   1. Data/field_markings.json loads through UPSDataIngestion, equals FPSFieldMarkingsStyle's
//      defaults and validates; an unsound style is caught.
//   2. The layout sits on the game's frame (PSField): the goal lines at the game's 0 and 100, a
//      yard line every five yards between them, the end zones from each goal line to its end line,
//      sidelines and end lines on the boundary, a pair of hash marks at every other yard, and the
//      ground reaching as far as the out-of-bounds volumes. It scales with the frame's yard.
//   3. The field grid spawns the surface once and it builds in a world: every line drawn as one
//      instance, the ground's top at Z = 0 with collision the ball lands on, the flat pieces with
//      none; a rebuild replaces what was there.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PSDataIngestion.h"
#include "PSFieldDimensions.h"
#include "PSFieldGrid.h"
#include "PSFieldSurface.h"
#include "PSFieldSurfaceTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSFieldSurfaceTests
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

    /** GetStaticMesh may hand back a TObjectPtr; a raw pointer is what TestNotNull takes. */
    static const UStaticMesh* MeshOf(const UStaticMeshComponent* Piece)
    {
        const UStaticMesh* Mesh = nullptr;
        if (Piece)
        {
            Mesh = Piece->GetStaticMesh();
        }
        return Mesh;
    }

    static TArray<FPSFieldMark> MarksOfKind(const TArray<FPSFieldMark>& Marks, EPSFieldMarkKind Kind)
    {
        return Marks.FilterByPredicate([Kind](const FPSFieldMark& Mark) { return Mark.Kind == Kind; });
    }

    /** Lines that run across the field (narrow in X) at world X, within Tolerance. */
    static int32 CountCrossLinesAt(const TArray<FPSFieldMark>& Lines, double X, double MinLengthY, double Tolerance)
    {
        int32 Count = 0;
        for (const FPSFieldMark& Line : Lines)
        {
            if (FMath::Abs(Line.Center.X - X) <= Tolerance && Line.Size.Y >= MinLengthY && Line.Size.X < Line.Size.Y)
            {
                ++Count;
            }
        }
        return Count;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The style's data file
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFieldMarkingsDataTest,
    "PlaySports.Field.MarkingsData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFieldMarkingsDataTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSFieldMarkingsStyle Loaded;
    if (!TestTrue(TEXT("Data/field_markings.json loads"), Ingestion->LoadFieldMarkingsStyleFromJson(APSFieldSurface::GetDefaultStylePath(), Loaded)))
    {
        return false;
    }
    TestEqual(TEXT("...and validates"), APSFieldSurface::ValidateStyle(Loaded).Num(), 0);

    // The struct's defaults equal the file, field by field.
    const FPSFieldMarkingsStyle Defaults;
    TestEqual(TEXT("GroundMeshPath"), Loaded.GroundMeshPath, Defaults.GroundMeshPath);
    TestEqual(TEXT("PlaneMeshPath"), Loaded.PlaneMeshPath, Defaults.PlaneMeshPath);
    TestEqual(TEXT("MeshSizeCm"), Loaded.MeshSizeCm, Defaults.MeshSizeCm);
    TestEqual(TEXT("MaterialPath"), Loaded.MaterialPath, Defaults.MaterialPath);
    TestEqual(TEXT("ColorParameter"), Loaded.ColorParameter, Defaults.ColorParameter);
    TestEqual(TEXT("FieldColor"), Loaded.FieldColor, Defaults.FieldColor);
    TestEqual(TEXT("SurroundColor"), Loaded.SurroundColor, Defaults.SurroundColor);
    TestEqual(TEXT("NearEndZoneColor"), Loaded.NearEndZoneColor, Defaults.NearEndZoneColor);
    TestEqual(TEXT("FarEndZoneColor"), Loaded.FarEndZoneColor, Defaults.FarEndZoneColor);
    TestEqual(TEXT("LineColor"), Loaded.LineColor, Defaults.LineColor);
    TestEqual(TEXT("GroundThicknessCm"), Loaded.GroundThicknessCm, Defaults.GroundThicknessCm);
    TestEqual(TEXT("LayerLiftCm"), Loaded.LayerLiftCm, Defaults.LayerLiftCm);
    TestEqual(TEXT("LineWidthYards"), Loaded.LineWidthYards, Defaults.LineWidthYards);
    TestEqual(TEXT("YardLineSpacingYards"), Loaded.YardLineSpacingYards, Defaults.YardLineSpacingYards);
    TestEqual(TEXT("HashSpacingYards"), Loaded.HashSpacingYards, Defaults.HashSpacingYards);
    TestEqual(TEXT("HashLengthYards"), Loaded.HashLengthYards, Defaults.HashLengthYards);
    TestEqual(TEXT("HashOffsetYards"), Loaded.HashOffsetYards, Defaults.HashOffsetYards);

    FPSFieldMarkingsStyle Broken;
    Broken.FieldColor = TEXT("green");
    Broken.YardLineSpacingYards = 0.f;
    Broken.MaterialPath.Empty();
    TestEqual(TEXT("An unsound style is caught, one problem each"), APSFieldSurface::ValidateStyle(Broken).Num(), 3);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The layout is the game's frame
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFieldMarkingsLayoutTest,
    "PlaySports.Field.MarkingsLayout",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFieldMarkingsLayoutTest::RunTest(const FString& Parameters)
{
    using namespace PSFieldSurfaceTests;

    const FPSFieldDimensions& Field = PSField::GetDimensions();
    const FPSFieldMarkingsStyle Style;
    const TArray<FPSFieldMark> Marks = APSFieldSurface::ComputeMarks(Field, Style);
    const double Tolerance = 0.01;
    const double Width = PSField::SidelineY() * 2.0;
    const double EndZoneDepth = PSField::YardsToCentimetres(Field.EndZoneDepthYards);
    const double OutDepth = PSField::YardsToCentimetres(Field.OutOfBoundsDepthYards);

    const TArray<FPSFieldMark> Grounds = MarksOfKind(Marks, EPSFieldMarkKind::Ground);
    if (TestEqual(TEXT("One ground"), Grounds.Num(), 1))
    {
        TestEqual(TEXT("...from the out-of-bounds depth behind one end line"), Grounds[0].Center.X - Grounds[0].Size.X * 0.5, static_cast<double>(PSField::EndLineX(false)) - OutDepth, Tolerance);
        TestEqual(TEXT("...to the same beyond the other"), Grounds[0].Center.X + Grounds[0].Size.X * 0.5, static_cast<double>(PSField::EndLineX(true)) + OutDepth, Tolerance);
        TestEqual(TEXT("...and past each sideline"), Grounds[0].Size.Y, Width + 2.0 * OutDepth, Tolerance);
    }
    const TArray<FPSFieldMark> Fields = MarksOfKind(Marks, EPSFieldMarkKind::Field);
    if (TestEqual(TEXT("One field"), Fields.Num(), 1))
    {
        TestEqual(TEXT("...end line to end line"), Fields[0].Size.X, static_cast<double>(PSField::EndLineX(true) - PSField::EndLineX(false)), Tolerance);
        TestEqual(TEXT("...sideline to sideline"), Fields[0].Size.Y, Width, Tolerance);
    }

    // The end zones: from each goal line to its end line, where the grid's volumes score.
    const TArray<FPSFieldMark> Near = MarksOfKind(Marks, EPSFieldMarkKind::NearEndZone);
    const TArray<FPSFieldMark> Far = MarksOfKind(Marks, EPSFieldMarkKind::FarEndZone);
    if (TestTrue(TEXT("One end zone at each end"), Near.Num() == 1 && Far.Num() == 1))
    {
        TestEqual(TEXT("The near end zone ends at the near goal line"), Near[0].Center.X + Near[0].Size.X * 0.5, static_cast<double>(PSField::GoalLineX(false)), Tolerance);
        TestEqual(TEXT("...and starts at its end line"), Near[0].Center.X - Near[0].Size.X * 0.5, static_cast<double>(PSField::EndLineX(false)), Tolerance);
        TestEqual(TEXT("The far end zone starts at the far goal line"), Far[0].Center.X - Far[0].Size.X * 0.5, static_cast<double>(PSField::GoalLineX(true)), Tolerance);
        TestEqual(TEXT("...and is the end zone's depth"), Far[0].Size.X, EndZoneDepth, Tolerance);
    }

    // The lines: a yard line every five yards, goal lines included, sideline to sideline.
    const TArray<FPSFieldMark> Lines = MarksOfKind(Marks, EPSFieldMarkKind::Line);
    for (int32 YardLine = 0; YardLine <= 100; YardLine += 5)
    {
        const double X = PSField::YardLineToWorld(static_cast<float>(YardLine)).X;
        TestEqual(*FString::Printf(TEXT("One yard line at the %d"), YardLine), CountCrossLinesAt(Lines, X, Width - Tolerance, Tolerance), 1);
    }
    TestEqual(TEXT("No yard line at the 7"), CountCrossLinesAt(Lines, PSField::YardLineToWorld(7.f).X, Width - Tolerance, Tolerance), 0);
    TestEqual(TEXT("An end line at each end"), CountCrossLinesAt(Lines, PSField::EndLineX(false), Width, Tolerance)
        + CountCrossLinesAt(Lines, PSField::EndLineX(true), Width, Tolerance), 2);

    // The sidelines: along the whole field, on the boundary.
    int32 Sidelines = 0;
    for (const FPSFieldMark& Line : Lines)
    {
        if (FMath::IsNearlyEqual(FMath::Abs(Line.Center.Y), static_cast<double>(PSField::SidelineY()), Tolerance)
            && Line.Size.X >= PSField::EndLineX(true) - PSField::EndLineX(false))
        {
            ++Sidelines;
        }
    }
    TestEqual(TEXT("Two sidelines"), Sidelines, 2);

    // The hash marks: a pair at every yard from the 1 to the 99, except where a yard line crosses.
    const double HashY = PSField::YardsToCentimetres(Style.HashOffsetYards);
    int32 Hashes = 0;
    int32 HashesOnFiveYardLines = 0;
    for (const FPSFieldMark& Line : Lines)
    {
        if (FMath::IsNearlyEqual(FMath::Abs(Line.Center.Y), HashY, Tolerance))
        {
            ++Hashes;
            const int32 Yard = FMath::RoundToInt(PSField::WorldToYardLine(FVector(Line.Center.X, 0.0, 0.0)));
            HashesOnFiveYardLines += Yard % 5 == 0 ? 1 : 0;
            TestTrue(TEXT("...each between the goal lines"), Yard > 0 && Yard < 100);
        }
    }
    TestEqual(TEXT("80 pairs of hash marks"), Hashes, 160);
    TestEqual(TEXT("...none where a yard line crosses"), HashesOnFiveYardLines, 0);
    TestEqual(TEXT("Lines: 2 sidelines, 2 end lines, 21 yard lines and 160 hash marks"), Lines.Num(), 185);

    // The same field on a real yard (91.44 cm) keeps its proportions.
    FPSFieldDimensions RealYards = Field;
    RealYards.CentimetresPerYard = 91.44f;
    const TArray<FPSFieldMark> RealMarks = APSFieldSurface::ComputeMarks(RealYards, Style);
    const TArray<FPSFieldMark> RealLines = MarksOfKind(RealMarks, EPSFieldMarkKind::Line);
    TestEqual(TEXT("On a real yard, the far goal line is at 9,144 cm"), CountCrossLinesAt(RealLines, 9144.0, RealYards.FieldWidthYards * 91.44 - Tolerance, Tolerance), 1);
    TestEqual(TEXT("...and the field is as wide as the spec's 4,876.8 cm"), MarksOfKind(RealMarks, EPSFieldMarkKind::Field)[0].Size.Y, 4876.8, 0.5);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The surface builds in a world
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFieldSurfaceBuildTest,
    "PlaySports.Field.SurfaceBuilds",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFieldSurfaceBuildTest::RunTest(const FString& Parameters)
{
    using namespace PSFieldSurfaceTests;

    UWorld* World = CreateTestWorld();
    if (!TestNotNull(TEXT("World"), World))
    {
        return false;
    }
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSFieldGrid* Grid = World->SpawnActor<APSFieldGrid>(APSFieldGrid::StaticClass(), FVector(1234.f, -567.f, 0.f), FRotator::ZeroRotator, SpawnParams);
    APSFieldSurface* Surface = Grid ? Grid->SpawnFieldSurface() : nullptr;
    if (!TestNotNull(TEXT("The grid spawns the field's surface"), Surface))
    {
        DestroyTestWorld(World);
        return false;
    }
    TestTrue(TEXT("...and builds it from data"), Surface->IsBuilt());
    TestTrue(TEXT("Asking again returns the same surface"), Grid->SpawnFieldSurface() == Surface);
    int32 Surfaces = 0;
    for (TActorIterator<APSFieldSurface> It(World); It; ++It)
    {
        ++Surfaces;
    }
    TestEqual(TEXT("...so the world has one"), Surfaces, 1);
    TestTrue(TEXT("It stands at the world's origin, the field's frame"), Surface->GetActorLocation().IsNearlyZero());

    const TArray<FPSFieldMark> Marks = APSFieldSurface::ComputeMarks(PSField::GetDimensions(), APSFieldSurface::LoadStyle(APSFieldSurface::GetDefaultStylePath()));
    const int32 LineCount = MarksOfKind(Marks, EPSFieldMarkKind::Line).Num();
    UInstancedStaticMeshComponent* Lines = Surface->GetLines();
    if (TestNotNull(TEXT("Lines"), Lines))
    {
        TestEqual(TEXT("Every line is one instance"), Lines->GetInstanceCount(), LineCount);
        TestNotNull(TEXT("...of the plane mesh"), MeshOf(Lines));
        TestTrue(TEXT("...with no collision"), Lines->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    }

    UStaticMeshComponent* Ground = Surface->GetGround();
    if (TestNotNull(TEXT("Ground"), Ground) && TestNotNull(TEXT("...with its mesh"), MeshOf(Ground)))
    {
        const FBox Box = Ground->Bounds.GetBox();
        TestEqual(TEXT("The ground's top is Z = 0"), static_cast<double>(Box.Max.Z), 0.0, 0.5);
        TestTrue(TEXT("...under the whole field, end zones included"), Box.Min.X <= PSField::EndLineX(false) && Box.Max.X >= PSField::EndLineX(true)
            && Box.Min.Y <= -PSField::SidelineY() && Box.Max.Y >= PSField::SidelineY());
        TestTrue(TEXT("...with collision"), Ground->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
        TestTrue(TEXT("...that the ball (a physics body) lands on"), Ground->GetCollisionResponseToChannel(ECC_PhysicsBody) == ECR_Block);
    }
    for (UStaticMeshComponent* Piece : { Surface->GetFieldPlane(), Surface->GetEndZone(false), Surface->GetEndZone(true) })
    {
        if (TestNotNull(TEXT("Flat piece"), Piece))
        {
            TestNotNull(TEXT("...with its mesh"), MeshOf(Piece));
            TestTrue(TEXT("...with no collision"), Piece->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
            TestTrue(TEXT("...above the ground"), Piece->GetComponentLocation().Z > 0.0);
        }
    }
    if (UStaticMeshComponent* FarEndZone = Surface->GetEndZone(true))
    {
        TestTrue(TEXT("The far end zone lies past the far goal line"), FarEndZone->GetComponentLocation().X > PSField::GoalLineX(true));
    }

    TestTrue(TEXT("A rebuild succeeds"), Surface->BuildFromData());
    TestEqual(TEXT("...and replaces the lines rather than adding to them"), Lines ? Lines->GetInstanceCount() : -1, LineCount);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
