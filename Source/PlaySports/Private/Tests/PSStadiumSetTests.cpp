// PSStadiumSetTests.cpp -- Epics 147.1 and 52: the stadium bowl, built from data on the field's frame
//
// Tests covered:
//   1. Data/stadium_set.json loads through UPSDataIngestion, equals FPSStadiumSetStyle's defaults
//      (the bowl, both decks and the structures included) and validates; an unsound style is caught.
//   2. The goal posts and benches sit on the field's frame: a goal post on each end line, its
//      crossbar over the end line at the crossbar's height, the uprights the crossbar's width apart
//      and rising from it, a padded base behind the end line, a ribbon on each upright; a bench past
//      each sideline inside the out-of-bounds depth, between the yard lines the data names.
//   3. The bowl: every seat outside the field wall and facing the field, a pitch apart along its
//      row, each row higher than the one in front, the upper deck above the lower; every section
//      seated in both decks; the vomitories leave their rows short of seats; the light banks aim
//      down at the field; a phone's detail keeps every seat but draws far fewer pieces.
//   4. The field grid spawns the set once and it builds in a world: one instance per piece in its
//      kind's instanced mesh, nothing that collides, the crowd in its seats, and a rebuild replaces
//      rather than adds.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PSCrowdRenderComponent.h"
#include "PSDataIngestion.h"
#include "PSFieldDimensions.h"
#include "PSFieldGrid.h"
#include "PSPlatformTiers.h"
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

    /** How far outside the field wall's line (the rounded rectangle at the ground's edge) Point is. */
    static double OffsetFromWall(const FVector& Point, const FPSStadiumSetStyle& Style)
    {
        const FPSFieldDimensions& Field = PSField::GetDimensions();
        const double OutDepth = PSField::YardsToCentimetres(Field.OutOfBoundsDepthYards);
        const double MinX = PSField::EndLineX(false) - OutDepth;
        const double MaxX = PSField::EndLineX(true) + OutDepth;
        const double HalfX = (MaxX - MinX) * 0.5 + Style.Bowl.WallOffsetCm;
        const double HalfY = PSField::SidelineY() + OutDepth + Style.Bowl.WallOffsetCm;
        const double Radius = FMath::Min<double>(Style.Bowl.CornerRadiusCm, FMath::Min(HalfX, HalfY));
        const double QX = FMath::Abs(Point.X - (MinX + MaxX) * 0.5) - (HalfX - Radius);
        const double QY = FMath::Abs(Point.Y) - (HalfY - Radius);
        return FVector2D(FMath::Max(QX, 0.0), FMath::Max(QY, 0.0)).Size() + FMath::Min(FMath::Max(QX, QY), 0.0) - Radius;
    }

    /** Whether Point is inside Piece's box (a cylinder counts as its box). */
    static bool IsInside(const FPSStadiumPiece& Piece, const FVector& Point)
    {
        const FVector Local = Piece.Rotation.UnrotateVector(Point - Piece.Center);
        return FMath::Abs(Local.X) <= Piece.Size.X * 0.5 && FMath::Abs(Local.Y) <= Piece.Size.Y * 0.5 && FMath::Abs(Local.Z) <= Piece.Size.Z * 0.5;
    }

    /** Whether nothing the stadium builds stands between From and To (sampled every few cm). */
    static bool IsSightClear(const TArray<FPSStadiumPiece>& Pieces, const FVector& From, const FVector& To)
    {
        TArray<const FPSStadiumPiece*> Near;
        for (const FPSStadiumPiece& Piece : Pieces)
        {
            if (FMath::PointDistToSegment(Piece.Center, From, To) <= Piece.Size.Size() * 0.5 + 1.0)
            {
                Near.Add(&Piece);
            }
        }
        const int32 Steps = 2000;
        for (int32 Step = 1; Step < Steps; ++Step)
        {
            const FVector Point = FMath::Lerp(From, To, Step / static_cast<double>(Steps));
            for (const FPSStadiumPiece* Piece : Near)
            {
                if (IsInside(*Piece, Point))
                {
                    return false;
                }
            }
        }
        return true;
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
    TestEqual(TEXT("...and validates by default"), APSStadiumSet::ValidateStyle(Defaults).Num(), 0);
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
    TestEqual(TEXT("GoalPostPadColor"), Loaded.GoalPostPadColor, Defaults.GoalPostPadColor);
    TestEqual(TEXT("GoalPostPadHeightYards"), Loaded.GoalPostPadHeightYards, Defaults.GoalPostPadHeightYards);
    TestEqual(TEXT("GoalPostPadDiameterYards"), Loaded.GoalPostPadDiameterYards, Defaults.GoalPostPadDiameterYards);
    TestEqual(TEXT("RibbonColor"), Loaded.RibbonColor, Defaults.RibbonColor);
    TestEqual(TEXT("RibbonLengthYards"), Loaded.RibbonLengthYards, Defaults.RibbonLengthYards);
    TestEqual(TEXT("RibbonWidthYards"), Loaded.RibbonWidthYards, Defaults.RibbonWidthYards);
    TestEqual(TEXT("BenchColor"), Loaded.BenchColor, Defaults.BenchColor);
    TestEqual(TEXT("BenchFromYardLine"), Loaded.BenchFromYardLine, Defaults.BenchFromYardLine);
    TestEqual(TEXT("BenchToYardLine"), Loaded.BenchToYardLine, Defaults.BenchToYardLine);
    TestEqual(TEXT("BenchDistanceYards"), Loaded.BenchDistanceYards, Defaults.BenchDistanceYards);
    TestEqual(TEXT("BenchDepthYards"), Loaded.BenchDepthYards, Defaults.BenchDepthYards);
    TestEqual(TEXT("BenchHeightYards"), Loaded.BenchHeightYards, Defaults.BenchHeightYards);

    const FPSStadiumBowl& Bowl = Loaded.Bowl;
    const FPSStadiumBowl& DefaultBowl = Defaults.Bowl;
    TestEqual(TEXT("Bowl.WallOffsetCm"), Bowl.WallOffsetCm, DefaultBowl.WallOffsetCm);
    TestEqual(TEXT("Bowl.CornerRadiusCm"), Bowl.CornerRadiusCm, DefaultBowl.CornerRadiusCm);
    TestEqual(TEXT("Bowl.SectionWidthCm"), Bowl.SectionWidthCm, DefaultBowl.SectionWidthCm);
    TestEqual(TEXT("Bowl.CornerSections"), Bowl.CornerSections, DefaultBowl.CornerSections);
    TestEqual(TEXT("Bowl.AisleWidthCm"), Bowl.AisleWidthCm, DefaultBowl.AisleWidthCm);
    TestEqual(TEXT("Bowl.MaxSegmentCm"), Bowl.MaxSegmentCm, DefaultBowl.MaxSegmentCm);
    TestEqual(TEXT("Bowl.SeatPitchCm"), Bowl.SeatPitchCm, DefaultBowl.SeatPitchCm);
    TestEqual(TEXT("Bowl.SeatWidthCm"), Bowl.SeatWidthCm, DefaultBowl.SeatWidthCm);
    TestEqual(TEXT("Bowl.SeatDepthCm"), Bowl.SeatDepthCm, DefaultBowl.SeatDepthCm);
    TestEqual(TEXT("Bowl.SeatHeightCm"), Bowl.SeatHeightCm, DefaultBowl.SeatHeightCm);
    TestEqual(TEXT("Bowl.SeatBackHeightCm"), Bowl.SeatBackHeightCm, DefaultBowl.SeatBackHeightCm);
    TestEqual(TEXT("Bowl.SeatSetbackCm"), Bowl.SeatSetbackCm, DefaultBowl.SeatSetbackCm);
    TestEqual(TEXT("Bowl.WallHeightCm"), Bowl.WallHeightCm, DefaultBowl.WallHeightCm);
    TestEqual(TEXT("Bowl.WallThicknessCm"), Bowl.WallThicknessCm, DefaultBowl.WallThicknessCm);
    TestEqual(TEXT("Bowl.WalkwayHeightCm"), Bowl.WalkwayHeightCm, DefaultBowl.WalkwayHeightCm);
    TestEqual(TEXT("Bowl.RailHeightCm"), Bowl.RailHeightCm, DefaultBowl.RailHeightCm);
    TestEqual(TEXT("Bowl.ConcreteColor"), Bowl.ConcreteColor, DefaultBowl.ConcreteColor);
    TestEqual(TEXT("Bowl.StairColor"), Bowl.StairColor, DefaultBowl.StairColor);
    TestEqual(TEXT("Bowl.SeatColor"), Bowl.SeatColor, DefaultBowl.SeatColor);
    TestEqual(TEXT("Bowl.WallColor"), Bowl.WallColor, DefaultBowl.WallColor);
    TestEqual(TEXT("Bowl.RailColor"), Bowl.RailColor, DefaultBowl.RailColor);
    TestEqual(TEXT("Bowl.FasciaColor"), Bowl.FasciaColor, DefaultBowl.FasciaColor);
    TestEqual(TEXT("Bowl.RibbonBoardColor"), Bowl.RibbonBoardColor, DefaultBowl.RibbonBoardColor);
    TestEqual(TEXT("Bowl.GlassColor"), Bowl.GlassColor, DefaultBowl.GlassColor);
    TestEqual(TEXT("Bowl.PortalColor"), Bowl.PortalColor, DefaultBowl.PortalColor);
    TestEqual(TEXT("Bowl.RoofColor"), Bowl.RoofColor, DefaultBowl.RoofColor);
    TestEqual(TEXT("Bowl.FixtureColor"), Bowl.FixtureColor, DefaultBowl.FixtureColor);
    TestEqual(TEXT("Bowl.BoardFrameColor"), Bowl.BoardFrameColor, DefaultBowl.BoardFrameColor);
    TestEqual(TEXT("Bowl.BoardScreenColor"), Bowl.BoardScreenColor, DefaultBowl.BoardScreenColor);
    TestEqual(TEXT("Bowl.PlazaColor"), Bowl.PlazaColor, DefaultBowl.PlazaColor);

    if (TestEqual(TEXT("Two decks"), Loaded.Decks.Num(), Defaults.Decks.Num()))
    {
        for (int32 Index = 0; Index < Loaded.Decks.Num(); ++Index)
        {
            const FPSStadiumDeck& Deck = Loaded.Decks[Index];
            const FPSStadiumDeck& DefaultDeck = Defaults.Decks[Index];
            const FString Where = FString::Printf(TEXT("Decks[%d]."), Index);
            TestEqual(*(Where + TEXT("DeckId")), Deck.DeckId, DefaultDeck.DeckId);
            TestEqual(*(Where + TEXT("FrontOffsetCm")), Deck.FrontOffsetCm, DefaultDeck.FrontOffsetCm);
            TestEqual(*(Where + TEXT("FrontHeightCm")), Deck.FrontHeightCm, DefaultDeck.FrontHeightCm);
            TestEqual(*(Where + TEXT("Rows")), Deck.Rows, DefaultDeck.Rows);
            TestEqual(*(Where + TEXT("RowDepthCm")), Deck.RowDepthCm, DefaultDeck.RowDepthCm);
            TestEqual(*(Where + TEXT("RowRiseCm")), Deck.RowRiseCm, DefaultDeck.RowRiseCm);
            TestEqual(*(Where + TEXT("SlabCm")), Deck.SlabCm, DefaultDeck.SlabCm);
            TestEqual(*(Where + TEXT("FasciaHeightCm")), Deck.FasciaHeightCm, DefaultDeck.FasciaHeightCm);
            TestEqual(*(Where + TEXT("ParapetHeightCm")), Deck.ParapetHeightCm, DefaultDeck.ParapetHeightCm);
            TestEqual(*(Where + TEXT("VomitoryEverySections")), Deck.VomitoryEverySections, DefaultDeck.VomitoryEverySections);
            TestEqual(*(Where + TEXT("VomitoryFirstRow")), Deck.VomitoryFirstRow, DefaultDeck.VomitoryFirstRow);
            TestEqual(*(Where + TEXT("VomitoryRows")), Deck.VomitoryRows, DefaultDeck.VomitoryRows);
            TestEqual(*(Where + TEXT("VomitoryWidthCm")), Deck.VomitoryWidthCm, DefaultDeck.VomitoryWidthCm);
        }
    }

    const FPSStadiumStructures& Structures = Loaded.Structures;
    const FPSStadiumStructures& DefaultStructures = Defaults.Structures;
    TestEqual(TEXT("Structures.CrossAisleCm"), Structures.CrossAisleCm, DefaultStructures.CrossAisleCm);
    TestEqual(TEXT("Structures.SuiteLevels"), Structures.SuiteLevels, DefaultStructures.SuiteLevels);
    TestEqual(TEXT("Structures.SuiteGlassHeightCm"), Structures.SuiteGlassHeightCm, DefaultStructures.SuiteGlassHeightCm);
    TestEqual(TEXT("Structures.SuiteFloorCm"), Structures.SuiteFloorCm, DefaultStructures.SuiteFloorCm);
    TestEqual(TEXT("Structures.RibbonHeightCm"), Structures.RibbonHeightCm, DefaultStructures.RibbonHeightCm);
    TestEqual(TEXT("Structures.BackWallHeightCm"), Structures.BackWallHeightCm, DefaultStructures.BackWallHeightCm);
    TestEqual(TEXT("Structures.BackWallThicknessCm"), Structures.BackWallThicknessCm, DefaultStructures.BackWallThicknessCm);
    TestEqual(TEXT("Structures.CanopyDepthCm"), Structures.CanopyDepthCm, DefaultStructures.CanopyDepthCm);
    TestEqual(TEXT("Structures.CanopyHeightCm"), Structures.CanopyHeightCm, DefaultStructures.CanopyHeightCm);
    TestEqual(TEXT("Structures.CanopyThicknessCm"), Structures.CanopyThicknessCm, DefaultStructures.CanopyThicknessCm);
    TestTrue(TEXT("Structures.bCanopySidelinesOnly"), Structures.bCanopySidelinesOnly == DefaultStructures.bCanopySidelinesOnly);
    TestEqual(TEXT("Structures.LightBankSpacingCm"), Structures.LightBankSpacingCm, DefaultStructures.LightBankSpacingCm);
    TestEqual(TEXT("Structures.LightBankWidthCm"), Structures.LightBankWidthCm, DefaultStructures.LightBankWidthCm);
    TestEqual(TEXT("Structures.LightBankHeightCm"), Structures.LightBankHeightCm, DefaultStructures.LightBankHeightCm);
    TestEqual(TEXT("Structures.LightBankTiltDegrees"), Structures.LightBankTiltDegrees, DefaultStructures.LightBankTiltDegrees);
    TestEqual(TEXT("Structures.PressBoxLengthCm"), Structures.PressBoxLengthCm, DefaultStructures.PressBoxLengthCm);
    TestEqual(TEXT("Structures.PressBoxHeightCm"), Structures.PressBoxHeightCm, DefaultStructures.PressBoxHeightCm);
    TestEqual(TEXT("Structures.PressBoxDepthCm"), Structures.PressBoxDepthCm, DefaultStructures.PressBoxDepthCm);
    TestEqual(TEXT("Structures.VideoBoardWidthCm"), Structures.VideoBoardWidthCm, DefaultStructures.VideoBoardWidthCm);
    TestEqual(TEXT("Structures.VideoBoardHeightCm"), Structures.VideoBoardHeightCm, DefaultStructures.VideoBoardHeightCm);
    TestEqual(TEXT("Structures.VideoBoardLiftCm"), Structures.VideoBoardLiftCm, DefaultStructures.VideoBoardLiftCm);
    TestEqual(TEXT("Structures.VideoBoardBezelCm"), Structures.VideoBoardBezelCm, DefaultStructures.VideoBoardBezelCm);
    TestEqual(TEXT("Structures.PlazaMarginCm"), Structures.PlazaMarginCm, DefaultStructures.PlazaMarginCm);

    FPSStadiumSetStyle Broken;
    Broken.GoalPostColor = TEXT("yellow");
    Broken.CrossbarHeightYards = 0.f;
    Broken.BenchFromYardLine = 80.f;
    Broken.Bowl.SeatColor = TEXT("red");
    Broken.Decks[1].FasciaHeightCm = 2000.f;
    TestEqual(TEXT("An unsound style is caught, one problem each"), APSStadiumSet::ValidateStyle(Broken).Num(), 5);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The goal posts and benches are the field's frame
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
    const FPSStadiumLayout Layout = APSStadiumSet::ComputeLayout(Field, Style, EPSStadiumDetail::Reduced);
    const TArray<FPSStadiumPiece>& Pieces = Layout.Pieces;
    const double Tolerance = 0.01;
    const double CrossbarZ = PSField::YardsToCentimetres(Style.CrossbarHeightYards);
    const double CrossbarWidth = PSField::YardsToCentimetres(Style.CrossbarWidthYards);
    const double SidelineY = PSField::SidelineY();
    const double OutDepth = PSField::YardsToCentimetres(Field.OutOfBoundsDepthYards);

    // The goal posts: five pieces at each end, all cylinders, and a pad and two ribbons.
    const TArray<FPSStadiumPiece> Posts = PiecesOfKind(Pieces, EPSStadiumPieceKind::GoalPost);
    TestEqual(TEXT("Five goal-post pieces at each end"), Posts.Num(), 10);
    TestEqual(TEXT("A pad on each base post"), PiecesOfKind(Pieces, EPSStadiumPieceKind::GoalPostPad).Num(), 2);
    TestEqual(TEXT("A ribbon on each upright"), PiecesOfKind(Pieces, EPSStadiumPieceKind::Ribbon).Num(), 4);
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
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The bowl: seats, rows, decks, sections, vomitories, light banks and detail
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSStadiumBowlLayoutTest,
    "PlaySports.Field.StadiumBowlLayout",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSStadiumBowlLayoutTest::RunTest(const FString& Parameters)
{
    using namespace PSStadiumSetTests;

    const FPSFieldDimensions& Field = PSField::GetDimensions();
    const FPSStadiumSetStyle Style;
    const FPSStadiumLayout Full = APSStadiumSet::ComputeLayout(Field, Style, EPSStadiumDetail::Full);
    const TArray<FPSStadiumSeat>& Seats = Full.Seats;
    const FPSStadiumDeck& Lower = Style.Decks[0];
    const FPSStadiumDeck& Upper = Style.Decks[1];

    // A stadium's worth of seats, every one outside the wall and turned to the field.
    TestTrue(*FString::Printf(TEXT("Tens of thousands of seats (%d)"), Seats.Num()), Seats.Num() > 40000 && Seats.Num() < 90000);
    const FVector FieldCentre(PSField::MidfieldX(), 0.0, 0.0);
    int32 Inside = 0;
    int32 FacingAway = 0;
    int32 WrongHeight = 0;
    double LowerTop = 0.0;
    double UpperBottom = TNumericLimits<double>::Max();
    TMap<int32, int32> SectionsByDeck;
    for (const FPSStadiumSeat& Seat : Seats)
    {
        if (OffsetFromWall(Seat.Location, Style) < Lower.FrontOffsetCm - 1.0)
        {
            ++Inside;
        }
        const FVector Facing = FRotator(0.f, Seat.Yaw, 0.f).Vector();
        if (FVector::DotProduct(Facing, (FieldCentre - Seat.Location).GetSafeNormal2D()) <= 0.0)
        {
            ++FacingAway;
        }
        const FPSStadiumDeck& Deck = Style.Decks[Seat.Deck];
        if (!FMath::IsNearlyEqual(Seat.Location.Z, static_cast<double>(Deck.FrontHeightCm + Seat.Row * Deck.RowRiseCm), 0.01))
        {
            ++WrongHeight;
        }
        if (Seat.Deck == 0)
        {
            LowerTop = FMath::Max(LowerTop, Seat.Location.Z);
        }
        else
        {
            UpperBottom = FMath::Min(UpperBottom, Seat.Location.Z);
        }
        SectionsByDeck.FindOrAdd(Seat.Deck * 1000 + Seat.Section) = 1;
    }
    TestEqual(TEXT("No seat inside the field wall"), Inside, 0);
    TestEqual(TEXT("Every seat faces the field"), FacingAway, 0);
    TestEqual(TEXT("Each row's seats are on its tread: a row higher than the one in front"), WrongHeight, 0);
    TestTrue(TEXT("The upper deck sits above the lower one"), UpperBottom > LowerTop);
    TestTrue(*FString::Printf(TEXT("The bowl has its sections (%d)"), Full.NumSections), Full.NumSections >= 20);
    TestEqual(TEXT("Every section is seated in both decks"), SectionsByDeck.Num(), 2 * Full.NumSections);

    // Along a row, a seat every SeatPitchCm, never closer.
    TMap<FIntVector, TArray<FVector>> ByRow;
    for (const FPSStadiumSeat& Seat : Seats)
    {
        ByRow.FindOrAdd(FIntVector(Seat.Deck, Seat.Row, Seat.Section)).Add(Seat.Location);
    }
    double Closest = TNumericLimits<double>::Max();
    for (const TPair<FIntVector, TArray<FVector>>& Row : ByRow)
    {
        for (int32 Index = 1; Index < Row.Value.Num(); ++Index)
        {
            Closest = FMath::Min(Closest, FVector::Dist(Row.Value[Index], Row.Value[Index - 1]));
        }
    }
    TestTrue(*FString::Printf(TEXT("Seats in a row are a pitch apart (closest %.1f cm)"), Closest), Closest >= Style.Bowl.SeatPitchCm - 0.5);

    // The vomitories: their rows hold fewer seats than the rows in front of them.
    auto SeatsInRow = [&Seats](int32 Deck, int32 Row)
    {
        int32 Count = 0;
        for (const FPSStadiumSeat& Seat : Seats)
        {
            Count += Seat.Deck == Deck && Seat.Row == Row ? 1 : 0;
        }
        return Count;
    };
    const int32 BeforeTunnels = SeatsInRow(0, Lower.VomitoryFirstRow - 1);
    const int32 BesideTunnels = SeatsInRow(0, Lower.VomitoryFirstRow);
    TestTrue(*FString::Printf(TEXT("The lower deck's vomitories cut seats from their rows (%d beside, %d in front)"), BesideTunnels, BeforeTunnels), BesideTunnels < BeforeTunnels);
    TestTrue(TEXT("...and leave tunnel mouths"), PiecesOfKind(Full.Pieces, EPSStadiumPieceKind::Portal).Num() > 0);
    TestTrue(TEXT("The upper deck has none: its rows grow with the bowl"), SeatsInRow(1, Upper.VomitoryFirstRow) >= SeatsInRow(1, Upper.VomitoryFirstRow - 1));

    // Everything the bowl is made of is there.
    for (const EPSStadiumPieceKind Kind : { EPSStadiumPieceKind::Concrete, EPSStadiumPieceKind::Stair, EPSStadiumPieceKind::Wall,
        EPSStadiumPieceKind::Rail, EPSStadiumPieceKind::Fascia, EPSStadiumPieceKind::RibbonBoard, EPSStadiumPieceKind::Glass,
        EPSStadiumPieceKind::Roof, EPSStadiumPieceKind::LightFixture, EPSStadiumPieceKind::BoardFrame, EPSStadiumPieceKind::BoardScreen,
        EPSStadiumPieceKind::Plaza })
    {
        TestTrue(*FString::Printf(TEXT("The bowl has its %s"), *StaticEnum<EPSStadiumPieceKind>()->GetNameStringByValue(static_cast<int64>(Kind))),
            PiecesOfKind(Full.Pieces, Kind).Num() > 0);
    }
    TestEqual(TEXT("A video board over each end"), PiecesOfKind(Full.Pieces, EPSStadiumPieceKind::BoardScreen).Num(), 2);
    int32 BelowField = 0;
    for (const FPSStadiumPiece& Piece : Full.Pieces)
    {
        BelowField += Piece.Kind != EPSStadiumPieceKind::Plaza && Piece.Center.Z - Piece.Size.Z * 0.5 < -0.5 && Piece.Rotation.IsNearlyZero() ? 1 : 0;
    }
    TestEqual(TEXT("Nothing but the plaza is built below the field"), BelowField, 0);
    const TArray<FPSStadiumPiece> Plaza = PiecesOfKind(Full.Pieces, EPSStadiumPieceKind::Plaza);
    if (TestEqual(TEXT("The stadium stands in a plaza"), Plaza.Num(), 1))
    {
        TestTrue(TEXT("...whose top is under the field's ground"), Plaza[0].Center.Z + Plaza[0].Size.Z * 0.5 < 0.0);
        int32 OffPlaza = 0;
        for (const FPSStadiumSeat& Seat : Seats)
        {
            OffPlaza += IsInside(Plaza[0], FVector(Seat.Location.X, Seat.Location.Y, Plaza[0].Center.Z)) ? 0 : 1;
        }
        TestEqual(TEXT("...and reaches past every seat"), OffPlaza, 0);
    }

    // The light banks: over the sidelines, high, aimed down at the field.
    TestTrue(TEXT("Light banks along both sidelines"), Full.LightBanks.Num() >= 4);
    for (const FTransform& Bank : Full.LightBanks)
    {
        const FVector Aim = Bank.GetRotation().GetForwardVector();
        TestTrue(TEXT("A light bank is above the top row"), Bank.GetLocation().Z > UpperBottom);
        TestTrue(TEXT("...aimed down"), Aim.Z < -0.1);
        TestTrue(TEXT("...at the field"), FVector::DotProduct(Aim.GetSafeNormal2D(), (FieldCentre - Bank.GetLocation()).GetSafeNormal2D()) > 0.5);
    }

    // The camera positions stay open: the broadcast position on the press level, at the 20 and 70 m
    // from the field's centre line, 25 m up (under the upper deck's overhang, in front of the
    // suites), and the all-22 high behind the end zone, both see the field past everything built.
    const FVector Broadcast(2000.0, -7000.0, 2500.0);
    const FVector EndZoneHigh(PSField::EndLineX(false) - 1500.0, 0.0, 3500.0);
    const double Sideline = PSField::SidelineY();
    for (const TPair<FVector, FVector>& Sight : {
        TPair<FVector, FVector>(Broadcast, FVector(2000.0, 0.0, 0.0)),
        TPair<FVector, FVector>(Broadcast, FVector(2000.0, -Sideline, 0.0)),
        TPair<FVector, FVector>(Broadcast, FVector(PSField::EndLineX(false), Sideline, 0.0)),
        TPair<FVector, FVector>(Broadcast, FVector(PSField::EndLineX(true), Sideline, 0.0)),
        TPair<FVector, FVector>(EndZoneHigh, FVector(PSField::MidfieldX(), 0.0, 0.0)),
        TPair<FVector, FVector>(EndZoneHigh, FVector(PSField::EndLineX(true), Sideline, 0.0)) })
    {
        TestTrue(*FString::Printf(TEXT("The camera at %s sees %s"), *Sight.Key.ToCompactString(), *Sight.Value.ToCompactString()),
            IsSightClear(Full.Pieces, Sight.Key, Sight.Value));
    }

    // A phone's detail: the same seats, but a strip per run of them instead of a pan and back each.
    const FPSStadiumLayout Reduced = APSStadiumSet::ComputeLayout(Field, Style, EPSStadiumDetail::Reduced);
    TestEqual(TEXT("The phone's bowl has the same seats"), Reduced.Seats.Num(), Seats.Num());
    const int32 FullSeatPieces = PiecesOfKind(Full.Pieces, EPSStadiumPieceKind::Seat).Num();
    const int32 ReducedSeatPieces = PiecesOfKind(Reduced.Pieces, EPSStadiumPieceKind::Seat).Num();
    TestEqual(TEXT("A PC draws each seat's pan and back"), FullSeatPieces, 2 * Seats.Num());
    TestTrue(*FString::Printf(TEXT("A phone draws far fewer seat pieces (%d)"), ReducedSeatPieces), ReducedSeatPieces > 0 && ReducedSeatPieces * 8 < FullSeatPieces);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The set builds in a world
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

    const FPSPlatformTier& Tier = PSPlatformTiers::GetActiveTier();
    const FPSStadiumLayout Layout = APSStadiumSet::ComputeLayout(PSField::GetDimensions(), APSStadiumSet::LoadStyle(APSStadiumSet::GetDefaultStylePath()), Tier.StadiumDetail);
    TestEqual(TEXT("The set's seats are the layout's"), Set->GetSeats().Num(), Layout.Seats.Num());
    for (int32 Index = 0; Index < static_cast<int32>(EPSStadiumPieceKind::Count); ++Index)
    {
        const EPSStadiumPieceKind Kind = static_cast<EPSStadiumPieceKind>(Index);
        const FString Label = StaticEnum<EPSStadiumPieceKind>()->GetNameStringByValue(Index);
        UInstancedStaticMeshComponent* Mesh = Set->GetPieces(Kind);
        if (!TestNotNull(*Label, Mesh))
        {
            continue;
        }
        TestEqual(*FString::Printf(TEXT("%s: one instance per piece"), *Label), Mesh->GetInstanceCount(), PiecesOfKind(Layout.Pieces, Kind).Num());
        TestTrue(*FString::Printf(TEXT("%s: nothing collides but the field's ground"), *Label), Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    }

    // The crowd fills the seats at the tier's density.
    UPSCrowdRenderComponent* Crowd = Set->GetCrowd();
    if (TestNotNull(TEXT("The set has its crowd"), Crowd) && Tier.CrowdDetail != EPSCrowdDetail::None)
    {
        const double Expected = Tier.CrowdDensity * Layout.Seats.Num();
        TestTrue(*FString::Printf(TEXT("...a fan in about %.0f%% of the seats (%d of %d)"), Tier.CrowdDensity * 100.f, Crowd->GetNumFans(), Layout.Seats.Num()),
            FMath::Abs(Crowd->GetNumFans() - Expected) <= 0.02 * Layout.Seats.Num() + 1.0);
    }

    TestTrue(TEXT("A rebuild succeeds"), Set->Build(PSField::GetDimensions(), FPSStadiumSetStyle(), EPSStadiumDetail::Reduced));
    TestEqual(TEXT("...and replaces the goal posts rather than adding to them"), Set->GetPieces(EPSStadiumPieceKind::GoalPost)->GetInstanceCount(), 10);
    TestEqual(TEXT("...and the seats, as strips at the phone's detail"), Set->GetPieces(EPSStadiumPieceKind::Seat)->GetInstanceCount(),
        PiecesOfKind(APSStadiumSet::ComputeLayout(PSField::GetDimensions(), FPSStadiumSetStyle(), EPSStadiumDetail::Reduced).Pieces, EPSStadiumPieceKind::Seat).Num());
    TestFalse(TEXT("...which casts no shadows"), Set->GetPieces(EPSStadiumPieceKind::Concrete)->CastShadow);

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
