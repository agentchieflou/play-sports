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
//      none; a rebuild replaces what was there. The border, numerals and arrows are one instance
//      each too, every arrow carrying the paint material's per-instance data.
//   4. The paint (lane V2): a white border outside the boundary; 10-20-30-40-50-40-30-20-10 on both
//      sides, each digit's strokes inside its 4 ft x 6 ft cell, the digits either side of their
//      yard line with their tops toward the middle of the field and their bottoms 7 yards in from
//      the sideline; an arrow beside every numeral but the 50, pointing to the goal line it counts
//      to. The low mobile tier draws the lighter turf.

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
    TestEqual(TEXT("TurfMaterialPath"), Loaded.TurfMaterialPath, Defaults.TurfMaterialPath);
    TestEqual(TEXT("PaintMaterialPath"), Loaded.PaintMaterialPath, Defaults.PaintMaterialPath);
    TestTrue(TEXT("MaterialScalars"), Loaded.MaterialScalars.OrderIndependentCompareEqual(Defaults.MaterialScalars));
    TestEqual(TEXT("StripeWidthYards"), Loaded.StripeWidthYards, Defaults.StripeWidthYards);
    TestEqual(TEXT("PaintCoverage"), Loaded.PaintCoverage, Defaults.PaintCoverage);
    TestEqual(TEXT("EndZonePaintCoverage"), Loaded.EndZonePaintCoverage, Defaults.EndZonePaintCoverage);
    if (TestEqual(TEXT("TierLooks"), Loaded.TierLooks.Num(), Defaults.TierLooks.Num()))
    {
        for (int32 Index = 0; Index < Loaded.TierLooks.Num(); ++Index)
        {
            TestEqual(TEXT("...TierId"), Loaded.TierLooks[Index].TierId, Defaults.TierLooks[Index].TierId);
            TestEqual(TEXT("...TurfMaterialPath"), Loaded.TierLooks[Index].TurfMaterialPath, Defaults.TierLooks[Index].TurfMaterialPath);
        }
    }
    TestEqual(TEXT("BorderWidthYards"), Loaded.BorderWidthYards, Defaults.BorderWidthYards);
    TestEqual(TEXT("BorderColor"), Loaded.BorderColor, Defaults.BorderColor);
    TestTrue(TEXT("bDrawNumerals"), Loaded.bDrawNumerals == Defaults.bDrawNumerals);
    TestEqual(TEXT("NumeralColor"), Loaded.NumeralColor, Defaults.NumeralColor);
    TestEqual(TEXT("NumeralEveryYards"), Loaded.NumeralEveryYards, Defaults.NumeralEveryYards);
    TestEqual(TEXT("NumeralHeightYards"), Loaded.NumeralHeightYards, Defaults.NumeralHeightYards);
    TestEqual(TEXT("NumeralWidthYards"), Loaded.NumeralWidthYards, Defaults.NumeralWidthYards);
    TestEqual(TEXT("NumeralStrokeYards"), Loaded.NumeralStrokeYards, Defaults.NumeralStrokeYards);
    TestEqual(TEXT("NumeralBottomFromSidelineYards"), Loaded.NumeralBottomFromSidelineYards, Defaults.NumeralBottomFromSidelineYards);
    TestEqual(TEXT("NumeralGapYards"), Loaded.NumeralGapYards, Defaults.NumeralGapYards);
    TestTrue(TEXT("bDrawArrows"), Loaded.bDrawArrows == Defaults.bDrawArrows);
    TestEqual(TEXT("ArrowLengthYards"), Loaded.ArrowLengthYards, Defaults.ArrowLengthYards);
    TestEqual(TEXT("ArrowBaseYards"), Loaded.ArrowBaseYards, Defaults.ArrowBaseYards);
    TestEqual(TEXT("ArrowGapYards"), Loaded.ArrowGapYards, Defaults.ArrowGapYards);
    TestEqual(TEXT("ArrowCenterFromSidelineYards"), Loaded.ArrowCenterFromSidelineYards, Defaults.ArrowCenterFromSidelineYards);

    FPSFieldMarkingsStyle Broken;
    Broken.FieldColor = TEXT("green");
    Broken.YardLineSpacingYards = 0.f;
    Broken.MaterialPath.Empty();
    TestEqual(TEXT("An unsound style is caught, one problem each"), APSFieldSurface::ValidateStyle(Broken).Num(), 3);

    FPSFieldMarkingsStyle BrokenPaint;
    BrokenPaint.PaintCoverage = 1.5f;
    BrokenPaint.NumeralStrokeYards = BrokenPaint.NumeralWidthYards;
    // The defaults already list MobileLow: a second entry for it is a duplicate.
    BrokenPaint.TierLooks.Add(FPSFieldTierLook(TEXT("MobileLow"), TEXT("/Game/X.X")));
    TestTrue(TEXT("...as are unsound paint settings"), APSFieldSurface::ValidateStyle(BrokenPaint).Num() >= 3);
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

    // The paint around and on the lines: one instance per mark, arrows with their shape's data.
    for (const TPair<EPSFieldMarkKind, UInstancedStaticMeshComponent*>& Layer : {
        TPair<EPSFieldMarkKind, UInstancedStaticMeshComponent*>(EPSFieldMarkKind::Border, Surface->GetBorder()),
        TPair<EPSFieldMarkKind, UInstancedStaticMeshComponent*>(EPSFieldMarkKind::Numeral, Surface->GetNumerals()),
        TPair<EPSFieldMarkKind, UInstancedStaticMeshComponent*>(EPSFieldMarkKind::Arrow, Surface->GetArrows()) })
    {
        if (TestNotNull(TEXT("Paint layer"), Layer.Value))
        {
            TestEqual(TEXT("...one instance per mark"), Layer.Value->GetInstanceCount(), MarksOfKind(Marks, Layer.Key).Num());
            TestTrue(TEXT("...with no collision"), Layer.Value->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        }
    }
    if (UInstancedStaticMeshComponent* Arrows = Surface->GetArrows())
    {
        TestEqual(TEXT("Each arrow carries its direction, centre, length and base"), Arrows->NumCustomDataFloats, APSFieldSurface::ArrowCustomDataFloats);
    }

    TestTrue(TEXT("A rebuild succeeds"), Surface->BuildFromData());
    TestEqual(TEXT("...and replaces the lines rather than adding to them"), Lines ? Lines->GetInstanceCount() : -1, LineCount);
    TestEqual(TEXT("...and the numerals"), Surface->GetNumerals() ? Surface->GetNumerals()->GetInstanceCount() : -1,
        MarksOfKind(Marks, EPSFieldMarkKind::Numeral).Num());

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The paint: border, numerals and arrows (lane V2)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFieldPaintLayoutTest,
    "PlaySports.Field.PaintLayout",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFieldPaintLayoutTest::RunTest(const FString& Parameters)
{
    using namespace PSFieldSurfaceTests;

    const FPSFieldDimensions& Field = PSField::GetDimensions();
    const FPSFieldMarkingsStyle Style;
    const TArray<FPSFieldMark> Marks = APSFieldSurface::ComputeMarks(Field, Style);
    const double Tolerance = 0.5;
    const double Cm = Field.CentimetresPerYard;
    const double SidelineY = PSField::SidelineY();

    // The border: two bands past the sidelines (reaching past the corners), two past the end lines.
    const TArray<FPSFieldMark> Border = MarksOfKind(Marks, EPSFieldMarkKind::Border);
    TestEqual(TEXT("Four border bands"), Border.Num(), 4);
    for (const FPSFieldMark& Band : Border)
    {
        const bool bAlongSideline = FMath::IsNearlyEqual(FMath::Abs(Band.Center.Y), SidelineY + Style.BorderWidthYards * Cm * 0.5, Tolerance);
        const bool bPastEndLine = FMath::IsNearlyEqual(Band.Center.X, PSField::EndLineX(false) - Style.BorderWidthYards * Cm * 0.5, Tolerance)
            || FMath::IsNearlyEqual(Band.Center.X, PSField::EndLineX(true) + Style.BorderWidthYards * Cm * 0.5, Tolerance);
        TestTrue(TEXT("...each outside the boundary"), bAlongSideline || bPastEndLine);
    }

    // The digits: a seven-segment block face, every stroke inside its cell.
    const float W = Style.NumeralWidthYards;
    const float H = Style.NumeralHeightYards;
    const float S = Style.NumeralStrokeYards;
    const int32 ExpectedStrokes[] = { 6, 2, 5, 5, 4, 5, 6, 3, 7, 6 };
    for (int32 Digit = 0; Digit <= 9; ++Digit)
    {
        const TArray<FBox2D> Strokes = APSFieldSurface::GetDigitStrokes(Digit, W, H, S);
        TestEqual(*FString::Printf(TEXT("Digit %d's strokes"), Digit), Strokes.Num(), ExpectedStrokes[Digit]);
        for (const FBox2D& Box : Strokes)
        {
            TestTrue(TEXT("...inside its cell"), Box.Min.X >= -KINDA_SMALL_NUMBER && Box.Min.Y >= -KINDA_SMALL_NUMBER
                && Box.Max.X <= W + KINDA_SMALL_NUMBER && Box.Max.Y <= H + KINDA_SMALL_NUMBER);
        }
    }
    TestEqual(TEXT("No strokes for a non-digit"), APSFieldSurface::GetDigitStrokes(10, W, H, S).Num(), 0);

    // The numerals: 10, 20, 30, 40, 50, 40, 30, 20, 10 on each side, every stroke between 7 and 9
    // yards in from its sideline and clear of its yard line by the gap.
    int32 ExpectedNumeralStrokes = 0;
    for (const int32 Label : { 10, 20, 30, 40, 50, 40, 30, 20, 10 })
    {
        ExpectedNumeralStrokes += 2 * (APSFieldSurface::GetDigitStrokes(Label / 10, W, H, S).Num() + APSFieldSurface::GetDigitStrokes(0, W, H, S).Num());
    }
    const TArray<FPSFieldMark> Numerals = MarksOfKind(Marks, EPSFieldMarkKind::Numeral);
    TestEqual(TEXT("Every numeral's strokes, both sides"), Numerals.Num(), ExpectedNumeralStrokes);
    int32 NearStrokes = 0;
    for (const FPSFieldMark& Stroke : Numerals)
    {
        const double InFromSideline = SidelineY - FMath::Abs(Stroke.Center.Y);
        TestTrue(TEXT("...between 7 and 9 yards in from the sideline"),
            InFromSideline - Stroke.Size.Y * 0.5 >= Style.NumeralBottomFromSidelineYards * Cm - Tolerance
            && InFromSideline + Stroke.Size.Y * 0.5 <= (Style.NumeralBottomFromSidelineYards + Style.NumeralHeightYards) * Cm + Tolerance);
        const double Yard = Stroke.Center.X / Cm;
        const double NearestTen = FMath::RoundToDouble(Yard / 10.0) * 10.0;
        TestTrue(TEXT("...and clear of its yard line"), FMath::Abs(Stroke.Center.X - NearestTen * Cm) - Stroke.Size.X * 0.5 >= Style.NumeralGapYards * Cm - Tolerance);
        NearStrokes += Stroke.Center.Y < 0.0 ? 1 : 0;
    }
    TestEqual(TEXT("...half on each side"), NearStrokes * 2, Numerals.Num());

    // Which way the near 20 reads: its tops toward the middle (+Y on the near side), its 2 on +X
    // (the reader faces +Y, so their left is +X) and its 0 on -X. The 2's top bar is at its top.
    const double TwentyX = PSField::YardLineToWorld(20.f).X;
    const double NumeralTopY = -SidelineY + (Style.NumeralBottomFromSidelineYards + Style.NumeralHeightYards) * Cm;
    bool bTwoTopOnPlusX = false;
    bool bZeroOnMinusX = false;
    for (const FPSFieldMark& Stroke : Numerals)
    {
        if (Stroke.Center.Y > 0.0 || FMath::Abs(Stroke.Center.X - TwentyX) > 3.0 * Cm)
        {
            continue;
        }
        const bool bTopBar = FMath::IsNearlyEqual(Stroke.Center.Y + Stroke.Size.Y * 0.5, NumeralTopY, Tolerance) && Stroke.Size.X > Stroke.Size.Y;
        bTwoTopOnPlusX |= bTopBar && Stroke.Center.X > TwentyX;
        bZeroOnMinusX |= Stroke.Center.X < TwentyX;
    }
    TestTrue(TEXT("The near 20's 2 is on the +X side, its top toward the middle"), bTwoTopOnPlusX);
    TestTrue(TEXT("...and its 0 on the -X side"), bZeroOnMinusX);

    // The arrows: beside every numeral but the 50, both sides, pointing to the goal line counted to.
    const TArray<FPSFieldMark> Arrows = MarksOfKind(Marks, EPSFieldMarkKind::Arrow);
    TestEqual(TEXT("Sixteen arrows"), Arrows.Num(), 16);
    const double MidfieldX = PSField::YardLineToWorld(50.f).X;
    for (const FPSFieldMark& Arrow : Arrows)
    {
        const double Expected = Arrow.Center.X < MidfieldX ? -1.0 : 1.0;
        TestEqual(TEXT("...each pointing to the nearer goal line"), static_cast<double>(Arrow.Direction), Expected);
        TestTrue(TEXT("...beyond its numeral on that side"), FMath::Abs(Arrow.Center.X - FMath::RoundToDouble(Arrow.Center.X / (10.0 * Cm)) * 10.0 * Cm)
            > Style.NumeralWidthYards * Cm);
    }

    // Nothing drawn when the data says so.
    FPSFieldMarkingsStyle Plain = Style;
    Plain.bDrawNumerals = false;
    Plain.BorderWidthYards = 0.f;
    const TArray<FPSFieldMark> PlainMarks = APSFieldSurface::ComputeMarks(Field, Plain);
    TestEqual(TEXT("No numerals when bDrawNumerals is off"), MarksOfKind(PlainMarks, EPSFieldMarkKind::Numeral).Num() + MarksOfKind(PlainMarks, EPSFieldMarkKind::Arrow).Num(), 0);
    TestEqual(TEXT("No border at width 0"), MarksOfKind(PlainMarks, EPSFieldMarkKind::Border).Num(), 0);
    TestEqual(TEXT("...and the lines unchanged"), MarksOfKind(PlainMarks, EPSFieldMarkKind::Line).Num(), MarksOfKind(Marks, EPSFieldMarkKind::Line).Num());

    // The low mobile tier draws the lighter turf; every other tier the full one.
    TestEqual(TEXT("MobileLow's turf"), APSFieldSurface::ResolveTurfMaterialPath(Style, TEXT("MobileLow")), FString(TEXT("/Game/Field/Materials/M_TurfLite.M_TurfLite")));
    TestEqual(TEXT("DesktopHigh's turf"), APSFieldSurface::ResolveTurfMaterialPath(Style, TEXT("DesktopHigh")), Style.TurfMaterialPath);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
