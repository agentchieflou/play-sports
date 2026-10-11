// PSStadiumSet.cpp - Epics 147.1 and 52: the stadium bowl around the field, built at runtime from data
#include "PSStadiumSet.h"
#include "PSCrowdRenderComponent.h"
#include "PSDataIngestion.h"
#include "PSUITeamCatalog.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

namespace PSStadiumSetPrivate
{
    template <typename AssetType>
    AssetType* LoadAsset(const FString& Path, const TCHAR* What)
    {
        AssetType* Asset = Path.IsEmpty() ? nullptr : Cast<AssetType>(FSoftObjectPath(Path).TryLoad());
        if (!Asset)
        {
            UE_LOG(LogTemp, Warning, TEXT("APSStadiumSet: Could not load the %s '%s'."), What, *Path);
        }
        return Asset;
    }

    FPSStadiumPiece MakePiece(EPSStadiumPieceKind Kind, bool bCylinder, const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator)
    {
        FPSStadiumPiece Piece;
        Piece.Kind = Kind;
        Piece.bCylinder = bCylinder;
        Piece.Center = Center;
        Piece.Size = Size;
        Piece.Rotation = Rotation;
        return Piece;
    }

    /** Whole numbers from doubles, without FMath's 64-bit overloads. */
    int32 FloorToCount(double Value)
    {
        return static_cast<int32>(FMath::FloorToDouble(Value));
    }

    int32 CeilToCount(double Value)
    {
        return static_cast<int32>(FMath::CeilToDouble(Value));
    }

    int32 RoundToCount(double Value)
    {
        return static_cast<int32>(FMath::RoundToDouble(Value));
    }

    /** The component names of the piece kinds, in EPSStadiumPieceKind's order. */
    const TCHAR* const PieceKindNames[] =
    {
        TEXT("GoalPosts"), TEXT("GoalPostPads"), TEXT("Ribbons"), TEXT("Benches"), TEXT("Concrete"), TEXT("Stairs"),
        TEXT("Seats"), TEXT("Wall"), TEXT("Rails"), TEXT("Fasciae"), TEXT("RibbonBoards"), TEXT("Glass"), TEXT("Portals"),
        TEXT("Roof"), TEXT("LightFixtures"), TEXT("BoardFrames"), TEXT("BoardScreens")
    };
    static_assert(UE_ARRAY_COUNT(PieceKindNames) == static_cast<int32>(EPSStadiumPieceKind::Count), "A name for every piece kind");

    double YawOf(const FVector2D& Direction)
    {
        return FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
    }

    /** One straight or one corner of the bowl's plan. Offsets d are measured outward from the
     *  field wall's line; a straight's station s runs along it from Origin, a corner's is its angle
     *  (radians, counterclockwise from +X). */
    struct FPlanSpan
    {
        bool bArc = false;
        FVector2D Origin = FVector2D::ZeroVector;
        FVector2D Tangent = FVector2D(1.0, 0.0);
        FVector2D Normal = FVector2D(0.0, -1.0);
        double Length = 0.0;
        FVector2D ArcCentre = FVector2D::ZeroVector;
        double Radius = 0.0;
        double Angle0 = 0.0;
        double Angle1 = 0.0;
        int32 Sections = 0;
        int32 FirstSection = 0;
        bool bSideline = false;
        bool bNearSideline = false;
        bool bEnd = false;

        double Start() const { return bArc ? Angle0 : 0.0; }
        double End() const { return bArc ? Angle1 : Length; }

        /** Section Index's stations. */
        void SectionRange(int32 Index, double& OutU0, double& OutU1) const
        {
            const double Step = (End() - Start()) / FMath::Max(Sections, 1);
            OutU0 = Start() + Index * Step;
            OutU1 = OutU0 + Step;
        }

        /** The station U at offset D: where it is, and which way is out (away from the field). */
        FVector2D PointAt(double U, double D) const
        {
            if (bArc)
            {
                return ArcCentre + FVector2D(FMath::Cos(U), FMath::Sin(U)) * (Radius + D);
            }
            return Origin + Tangent * U + Normal * D;
        }

        FVector2D OutwardAt(double U) const
        {
            return bArc ? FVector2D(FMath::Cos(U), FMath::Sin(U)) : Normal;
        }

        FVector2D TangentAt(double U) const
        {
            return bArc ? FVector2D(-FMath::Sin(U), FMath::Cos(U)) : Tangent;
        }

        /** How far apart, along a row at offset D, two stations are per unit of station. */
        double MetricAt(double D) const
        {
            return bArc ? (Radius + D) : 1.0;
        }
    };

    struct FPlan
    {
        TArray<FPlanSpan> Spans;
        int32 NumSections = 0;
    };

    /** The rounded rectangle at the ground's edge, counterclockwise from the -Y sideline. */
    FPlan MakePlan(const FPSFieldDimensions& Dimensions, const FPSStadiumBowl& Bowl)
    {
        const double Cm = Dimensions.CentimetresPerYard;
        const double EndZoneDepth = Dimensions.EndZoneDepthYards * Cm;
        const double OutDepth = Dimensions.OutOfBoundsDepthYards * Cm;
        const double GroundMinX = -EndZoneDepth - OutDepth;
        const double GroundMaxX = Dimensions.FieldLengthYards * Cm + EndZoneDepth + OutDepth;
        const double CentreX = (GroundMinX + GroundMaxX) * 0.5;
        const double HalfX = (GroundMaxX - GroundMinX) * 0.5 + Bowl.WallOffsetCm;
        const double HalfY = Dimensions.FieldWidthYards * Cm * 0.5 + OutDepth + Bowl.WallOffsetCm;
        const double Radius = FMath::Clamp<double>(Bowl.CornerRadiusCm, 0.0, FMath::Min(HalfX, HalfY));
        const double StraightX = HalfX - Radius;
        const double StraightY = HalfY - Radius;

        FPlan Plan;
        auto AddStraight = [&Plan, &Bowl](const FVector2D& Origin, const FVector2D& Tangent, const FVector2D& Normal, double Length, bool bSideline, bool bNearSideline)
        {
            FPlanSpan Span;
            Span.Origin = Origin;
            Span.Tangent = Tangent;
            Span.Normal = Normal;
            Span.Length = Length;
            Span.bSideline = bSideline;
            Span.bNearSideline = bNearSideline;
            Span.bEnd = !bSideline;
            Span.Sections = Length > 1.0 ? FMath::Max(1, RoundToCount(Length / FMath::Max<double>(Bowl.SectionWidthCm, 1.0))) : 0;
            Plan.Spans.Add(Span);
        };
        auto AddCorner = [&Plan, &Bowl, Radius](const FVector2D& Centre, double FromDegrees)
        {
            FPlanSpan Span;
            Span.bArc = true;
            Span.ArcCentre = Centre;
            Span.Radius = Radius;
            Span.Angle0 = FMath::DegreesToRadians(FromDegrees);
            Span.Angle1 = FMath::DegreesToRadians(FromDegrees + 90.0);
            Span.Sections = Radius > 1.0 ? FMath::Max(1, Bowl.CornerSections) : 0;
            Plan.Spans.Add(Span);
        };
        AddStraight(FVector2D(CentreX - StraightX, -HalfY), FVector2D(1.0, 0.0), FVector2D(0.0, -1.0), 2.0 * StraightX, true, true);
        AddCorner(FVector2D(CentreX + StraightX, -StraightY), -90.0);
        AddStraight(FVector2D(CentreX + HalfX, -StraightY), FVector2D(0.0, 1.0), FVector2D(1.0, 0.0), 2.0 * StraightY, false, false);
        AddCorner(FVector2D(CentreX + StraightX, StraightY), 0.0);
        AddStraight(FVector2D(CentreX + StraightX, HalfY), FVector2D(-1.0, 0.0), FVector2D(0.0, 1.0), 2.0 * StraightX, true, false);
        AddCorner(FVector2D(CentreX - StraightX, StraightY), 90.0);
        AddStraight(FVector2D(CentreX - HalfX, StraightY), FVector2D(0.0, -1.0), FVector2D(-1.0, 0.0), 2.0 * StraightY, false, false);
        AddCorner(FVector2D(CentreX - StraightX, -StraightY), 180.0);

        for (FPlanSpan& Span : Plan.Spans)
        {
            Span.FirstSection = Plan.NumSections;
            Plan.NumSections += Span.Sections;
        }
        return Plan;
    }

    /** Boxes covering stations U0..U1 of Span between offsets D0..D1 and heights Z0..Z1: one box
     *  on a straight, a box per short chord round a corner (each as long as its chord at D1, so
     *  neighbours meet without a gap). */
    void AddBand(TArray<FPSStadiumPiece>& Pieces, EPSStadiumPieceKind Kind, const FPlanSpan& Span, double U0, double U1, double D0, double D1, double Z0, double Z1, double MaxSegment)
    {
        if (!(U1 > U0) || !(D1 > D0) || !(Z1 > Z0))
        {
            return;
        }
        const double Depth = D1 - D0;
        const double DMid = (D0 + D1) * 0.5;
        const double ZMid = (Z0 + Z1) * 0.5;
        if (!Span.bArc)
        {
            const FVector2D Point = Span.PointAt((U0 + U1) * 0.5, DMid);
            Pieces.Add(MakePiece(Kind, false, FVector(Point.X, Point.Y, ZMid), FVector(U1 - U0, Depth, Z1 - Z0), FRotator(0.0, YawOf(Span.Tangent), 0.0)));
            return;
        }
        const double OuterRadius = Span.Radius + D1;
        const int32 Segments = FMath::Max(1, CeilToCount((U1 - U0) * OuterRadius / FMath::Max(MaxSegment, 10.0)));
        const double Step = (U1 - U0) / Segments;
        const double Chord = 2.0 * OuterRadius * FMath::Sin(Step * 0.5);
        for (int32 Segment = 0; Segment < Segments; ++Segment)
        {
            const double Angle = U0 + (Segment + 0.5) * Step;
            const FVector2D Point = Span.PointAt(Angle, DMid);
            Pieces.Add(MakePiece(Kind, false, FVector(Point.X, Point.Y, ZMid), FVector(Chord, Depth, Z1 - Z0), FRotator(0.0, FMath::RadiansToDegrees(Angle) + 90.0, 0.0)));
        }
    }

    /** A band all the way round the plan (or along its sidelines only). */
    void AddRing(TArray<FPSStadiumPiece>& Pieces, EPSStadiumPieceKind Kind, const FPlan& Plan, double D0, double D1, double Z0, double Z1, double MaxSegment, bool bSidelinesOnly = false)
    {
        for (const FPlanSpan& Span : Plan.Spans)
        {
            if (!bSidelinesOnly || Span.bSideline)
            {
                AddBand(Pieces, Kind, Span, Span.Start(), Span.End(), D0, D1, Z0, Z1, MaxSegment);
            }
        }
    }

    /** A deck's numbers for one row. */
    struct FRowFrame
    {
        double Front = 0.0;
        double Back = 0.0;
        double Tread = 0.0;
        /** Where the row's concrete starts: the ground for a solid deck, else under its slab. */
        double Bottom = 0.0;
    };

    FRowFrame RowFrame(const FPSStadiumDeck& Deck, int32 Row)
    {
        FRowFrame Frame;
        Frame.Front = Deck.FrontOffsetCm + Row * Deck.RowDepthCm;
        Frame.Back = Frame.Front + Deck.RowDepthCm;
        Frame.Tread = Deck.FrontHeightCm + Row * Deck.RowRiseCm;
        Frame.Bottom = Deck.SlabCm > 0.f ? Frame.Tread - Deck.RowRiseCm - Deck.SlabCm : 0.0;
        return Frame;
    }

    /** The deck's underside at offset D (its front's fascia when D is in front of it). */
    double SoffitAt(const FPSStadiumDeck& Deck, double D)
    {
        if (D < Deck.FrontOffsetCm)
        {
            return Deck.FrontHeightCm - Deck.RowRiseCm - Deck.SlabCm - Deck.FasciaHeightCm;
        }
        const int32 Row = FMath::Clamp(FloorToCount((D - Deck.FrontOffsetCm) / FMath::Max(Deck.RowDepthCm, 1.f)), 0, FMath::Max(Deck.Rows - 1, 0));
        const FRowFrame Frame = RowFrame(Deck, Row);
        return Deck.SlabCm > 0.f ? Frame.Bottom : Frame.Tread - Deck.RowRiseCm;
    }

    bool IsVomitorySection(const FPSStadiumDeck& Deck, const FPlanSpan& Span, int32 LocalSection)
    {
        return !Span.bArc && Deck.VomitoryEverySections > 0 && Deck.VomitoryRows > 0
            && LocalSection % Deck.VomitoryEverySections == Deck.VomitoryEverySections / 2;
    }

    bool IsVomitoryRow(const FPSStadiumDeck& Deck, int32 Row)
    {
        return Row >= Deck.VomitoryFirstRow && Row < Deck.VomitoryFirstRow + Deck.VomitoryRows;
    }

    /** A seat's pan and back, facing Yaw from its spot on the tread. */
    void AddSeatPieces(TArray<FPSStadiumPiece>& Pieces, const FPSStadiumBowl& Bowl, const FVector& OnTread, double Yaw)
    {
        const FRotator Facing(0.0, Yaw, 0.0);
        const FVector Forward = Facing.Vector();
        const double PanThickness = 6.0;
        Pieces.Add(MakePiece(EPSStadiumPieceKind::Seat, false, OnTread + FVector(0.0, 0.0, Bowl.SeatHeightCm - PanThickness * 0.5),
            FVector(Bowl.SeatDepthCm, Bowl.SeatWidthCm, PanThickness), Facing));
        const FVector BackCentre = OnTread - Forward * (Bowl.SeatDepthCm * 0.5 - PanThickness * 0.5) + FVector(0.0, 0.0, Bowl.SeatHeightCm + Bowl.SeatBackHeightCm * 0.5);
        Pieces.Add(MakePiece(EPSStadiumPieceKind::Seat, false, BackCentre, FVector(PanThickness, Bowl.SeatWidthCm, Bowl.SeatBackHeightCm), FRotator(12.0, Yaw, 0.0)));
    }

    /** The goal posts and the benches: the field's furniture, in yards of its frame. */
    void AddFieldFurniture(TArray<FPSStadiumPiece>& Pieces, const FPSFieldDimensions& Dimensions, const FPSStadiumSetStyle& Style)
    {
        const float Cm = Dimensions.CentimetresPerYard;
        const float FieldLength = Dimensions.FieldLengthYards * Cm;
        const float EndZoneDepth = Dimensions.EndZoneDepthYards * Cm;
        const float SidelineY = Dimensions.FieldWidthYards * Cm * 0.5f;

        // The goal posts, on each end line: the near one behind X = 0, the far one past the far goal.
        const float CrossbarZ = Style.CrossbarHeightYards * Cm;
        const float CrossbarWidth = Style.CrossbarWidthYards * Cm;
        const float UprightLength = Style.UprightHeightYards * Cm;
        const float Post = Style.PostDiameterYards * Cm;
        const float BasePost = Style.BasePostDiameterYards * Cm;
        const float Setback = Style.BaseSetbackYards * Cm;
        const float PadHeight = FMath::Min(Style.GoalPostPadHeightYards * Cm, CrossbarZ);
        const float Pad = Style.GoalPostPadDiameterYards * Cm;
        const float RibbonLength = Style.RibbonLengthYards * Cm;
        const float RibbonWidth = Style.RibbonWidthYards * Cm;
        for (const float Outward : { -1.f, 1.f })
        {
            const float EndLineX = Outward < 0.f ? -EndZoneDepth : FieldLength + EndZoneDepth;
            const float BaseX = EndLineX + Outward * Setback;
            Pieces.Add(MakePiece(EPSStadiumPieceKind::GoalPost, true, FVector(BaseX, 0.f, CrossbarZ * 0.5f), FVector(BasePost, BasePost, CrossbarZ)));
            Pieces.Add(MakePiece(EPSStadiumPieceKind::GoalPostPad, true, FVector(BaseX, 0.f, PadHeight * 0.5f), FVector(Pad, Pad, PadHeight)));
            // The neck reaches forward from the base post's top to the crossbar over the end line.
            Pieces.Add(MakePiece(EPSStadiumPieceKind::GoalPost, true, FVector((BaseX + EndLineX) * 0.5f, 0.f, CrossbarZ),
                FVector(BasePost, BasePost, Setback), FRotator(90.f, 0.f, 0.f)));
            Pieces.Add(MakePiece(EPSStadiumPieceKind::GoalPost, true, FVector(EndLineX, 0.f, CrossbarZ),
                FVector(Post, Post, CrossbarWidth), FRotator(0.f, 0.f, 90.f)));
            for (const float Side : { -1.f, 1.f })
            {
                Pieces.Add(MakePiece(EPSStadiumPieceKind::GoalPost, true, FVector(EndLineX, Side * CrossbarWidth * 0.5f, CrossbarZ + UprightLength * 0.5f),
                    FVector(Post, Post, UprightLength)));
                // The wind ribbon hangs from the upright's top, just behind it.
                Pieces.Add(MakePiece(EPSStadiumPieceKind::Ribbon, false,
                    FVector(EndLineX + Outward * Post, Side * CrossbarWidth * 0.5f, CrossbarZ + UprightLength - RibbonLength * 0.5f),
                    FVector(1.f, RibbonWidth, RibbonLength)));
            }
        }

        // A team bench past each sideline, between the yard lines the data names.
        const float BenchFrom = Style.BenchFromYardLine * Cm;
        const float BenchTo = Style.BenchToYardLine * Cm;
        const float BenchDepth = Style.BenchDepthYards * Cm;
        const float BenchHeight = Style.BenchHeightYards * Cm;
        for (const float Side : { -1.f, 1.f })
        {
            Pieces.Add(MakePiece(EPSStadiumPieceKind::Bench, false,
                FVector((BenchFrom + BenchTo) * 0.5f, Side * (SidelineY + Style.BenchDistanceYards * Cm + BenchDepth * 0.5f), BenchHeight * 0.5f),
                FVector(BenchTo - BenchFrom, BenchDepth, BenchHeight)));
        }
    }

    /** One deck's rows: risers, aisle steps, vomitories and seats. */
    void AddDeck(FPSStadiumLayout& Layout, const FPlan& Plan, const FPSStadiumBowl& Bowl, const FPSStadiumDeck& Deck, int32 DeckIndex, EPSStadiumDetail Detail)
    {
        const double Aisle = Bowl.AisleWidthCm;
        const double Pitch = FMath::Max<double>(Bowl.SeatPitchCm, 1.0);
        const double MaxSegment = Bowl.MaxSegmentCm;
        const double StripHeight = Bowl.SeatHeightCm + Bowl.SeatBackHeightCm;

        for (int32 Row = 0; Row < Deck.Rows; ++Row)
        {
            const FRowFrame Frame = RowFrame(Deck, Row);
            const double Middle = (Frame.Front + Frame.Back) * 0.5;
            const double SeatLine = Frame.Front + Bowl.SeatSetbackCm;
            for (const FPlanSpan& Span : Plan.Spans)
            {
                for (int32 Local = 0; Local < Span.Sections; ++Local)
                {
                    double U0 = 0.0;
                    double U1 = 0.0;
                    Span.SectionRange(Local, U0, U1);
                    const double UMid = (U0 + U1) * 0.5;

                    // The aisles at both ends of the section: half an aisle each.
                    const double RiserHalfAisle = Aisle * 0.5 / Span.MetricAt(Middle);
                    const double SeatHalfAisle = Aisle * 0.5 / Span.MetricAt(SeatLine);

                    // The runs of seats (and of riser): all of it, or both sides of a vomitory.
                    TArray<TPair<double, double>, TInlineAllocator<2>> RiserRuns;
                    TArray<TPair<double, double>, TInlineAllocator<2>> SeatRuns;
                    const bool bVomitory = IsVomitorySection(Deck, Span, Local) && IsVomitoryRow(Deck, Row);
                    if (bVomitory)
                    {
                        const double HalfPortal = Deck.VomitoryWidthCm * 0.5;
                        RiserRuns.Add(TPair<double, double>(U0 + RiserHalfAisle, UMid - HalfPortal));
                        RiserRuns.Add(TPair<double, double>(UMid + HalfPortal, U1 - RiserHalfAisle));
                        SeatRuns.Add(TPair<double, double>(U0 + SeatHalfAisle, UMid - HalfPortal));
                        SeatRuns.Add(TPair<double, double>(UMid + HalfPortal, U1 - SeatHalfAisle));

                        // The tunnel's floor, level with the row in front of it.
                        const double Floor = Deck.VomitoryFirstRow > 0 ? RowFrame(Deck, Deck.VomitoryFirstRow - 1).Tread : Deck.FrontHeightCm - Deck.RowRiseCm;
                        AddBand(Layout.Pieces, EPSStadiumPieceKind::Concrete, Span, UMid - HalfPortal, UMid + HalfPortal, Frame.Front, Frame.Back, Frame.Bottom, Floor, MaxSegment);
                        if (Row == Deck.VomitoryFirstRow + Deck.VomitoryRows - 1 && Row + 1 < Deck.Rows)
                        {
                            // The tunnel's mouth, in the face of the row behind the last one cut away.
                            const FRowFrame Behind = RowFrame(Deck, Row + 1);
                            AddBand(Layout.Pieces, EPSStadiumPieceKind::Portal, Span, UMid - HalfPortal + 25.0, UMid + HalfPortal - 25.0,
                                Behind.Front - 4.0, Behind.Front, Floor, Frame.Tread, MaxSegment);
                        }
                    }
                    else
                    {
                        RiserRuns.Add(TPair<double, double>(U0 + RiserHalfAisle, U1 - RiserHalfAisle));
                        SeatRuns.Add(TPair<double, double>(U0 + SeatHalfAisle, U1 - SeatHalfAisle));
                    }

                    for (const TPair<double, double>& Run : RiserRuns)
                    {
                        AddBand(Layout.Pieces, EPSStadiumPieceKind::Concrete, Span, Run.Key, Run.Value, Frame.Front, Frame.Back, Frame.Bottom, Frame.Tread, MaxSegment);
                    }

                    const double SeatMetric = Span.MetricAt(SeatLine);
                    for (const TPair<double, double>& Run : SeatRuns)
                    {
                        const double RunLength = (Run.Value - Run.Key) * SeatMetric;
                        const int32 Count = RunLength > 0.0 ? FloorToCount(RunLength / Pitch) : 0;
                        if (Count <= 0)
                        {
                            continue;
                        }
                        const double RunMid = (Run.Key + Run.Value) * 0.5;
                        const double Step = Pitch / SeatMetric;
                        for (int32 Seat = 0; Seat < Count; ++Seat)
                        {
                            const double U = RunMid + (Seat - (Count - 1) * 0.5) * Step;
                            const FVector2D Point = Span.PointAt(U, SeatLine);
                            FPSStadiumSeat& Placed = Layout.Seats.AddDefaulted_GetRef();
                            Placed.Location = FVector(Point.X, Point.Y, Frame.Tread);
                            Placed.Yaw = static_cast<float>(YawOf(-Span.OutwardAt(U)));
                            Placed.Section = Span.FirstSection + Local;
                            Placed.Deck = DeckIndex;
                            Placed.Row = Row;
                            if (Detail == EPSStadiumDetail::Full)
                            {
                                AddSeatPieces(Layout.Pieces, Bowl, Placed.Location, Placed.Yaw);
                            }
                        }
                        if (Detail != EPSStadiumDetail::Full)
                        {
                            // A phone draws the run's seats as one strip, their silhouette.
                            const double HalfRun = Count * Step * 0.5;
                            AddBand(Layout.Pieces, EPSStadiumPieceKind::Seat, Span, RunMid - HalfRun, RunMid + HalfRun,
                                SeatLine - Bowl.SeatDepthCm * 0.5, SeatLine + Bowl.SeatDepthCm * 0.5, Frame.Tread, Frame.Tread + StripHeight, MaxSegment);
                        }
                    }
                }

                // The aisles' steps, two to a row, at every section boundary; a corner's ends are
                // its neighbouring straights' boundaries, so a corner adds only its inner ones.
                const int32 FirstBoundary = Span.bArc ? 1 : 0;
                const int32 LastBoundary = Span.bArc ? Span.Sections - 1 : Span.Sections;
                for (int32 Boundary = FirstBoundary; Boundary <= LastBoundary && Span.Sections > 0; ++Boundary)
                {
                    const double U = Span.Start() + (Span.End() - Span.Start()) * Boundary / Span.Sections;
                    const double HalfAisle = Aisle * 0.5 / Span.MetricAt(Middle);
                    AddBand(Layout.Pieces, EPSStadiumPieceKind::Stair, Span, U - HalfAisle, U + HalfAisle, Frame.Front, Middle,
                        Frame.Bottom, Frame.Tread - Deck.RowRiseCm * 0.5, Aisle);
                    AddBand(Layout.Pieces, EPSStadiumPieceKind::Stair, Span, U - HalfAisle, U + HalfAisle, Middle, Frame.Back,
                        Frame.Bottom, Frame.Tread, Aisle);
                }
            }
        }
    }
}

APSStadiumSet::APSStadiumSet()
{
    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Movable);
    RootComponent = Root;

    for (int32 Index = 0; Index < static_cast<int32>(EPSStadiumPieceKind::Count); ++Index)
    {
        const FName ComponentName(PSStadiumSetPrivate::PieceKindNames[Index]);
        UInstancedStaticMeshComponent* Pieces = CreateDefaultSubobject<UInstancedStaticMeshComponent>(ComponentName);
        // The set only draws: the field's ground is the one surface that collides.
        Pieces->SetupAttachment(RootComponent);
        Pieces->SetMobility(EComponentMobility::Movable);
        Pieces->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Pieces->SetGenerateOverlapEvents(false);
        Pieces->SetCanEverAffectNavigation(false);
        Pieces->SetCastShadow(false);
        PieceMeshes.Add(Pieces);
    }

    Crowd = CreateDefaultSubobject<UPSCrowdRenderComponent>(TEXT("Crowd"));
}

void APSStadiumSet::BeginPlay()
{
    Super::BeginPlay();
    if (!bBuilt)
    {
        BuildFromData();
    }
}

UInstancedStaticMeshComponent* APSStadiumSet::GetPieces(EPSStadiumPieceKind Kind) const
{
    const int32 Index = static_cast<int32>(Kind);
    return PieceMeshes.IsValidIndex(Index) ? PieceMeshes[Index] : nullptr;
}

FString APSStadiumSet::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/stadium_set.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

FPSStadiumSetStyle APSStadiumSet::LoadStyle(const FString& Path)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSStadiumSetStyle Loaded;
    if (!Ingestion->LoadStadiumSetStyleFromJson(Path, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("APSStadiumSet: Could not load %s; using the default set."), *Path);
        return FPSStadiumSetStyle();
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSStadiumSet: %s: %s"), *Path, *Problem);
    }
    return Problems.Num() == 0 ? Loaded : FPSStadiumSetStyle();
}

TArray<FString> APSStadiumSet::ValidateStyle(const FPSStadiumSetStyle& Style)
{
    TArray<FString> Problems;
    for (const TPair<const TCHAR*, const FString*>& Path : {
        TPair<const TCHAR*, const FString*>(TEXT("BoxMeshPath"), &Style.BoxMeshPath),
        TPair<const TCHAR*, const FString*>(TEXT("CylinderMeshPath"), &Style.CylinderMeshPath),
        TPair<const TCHAR*, const FString*>(TEXT("MaterialPath"), &Style.MaterialPath) })
    {
        if (Path.Value->IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("%s must name an asset"), Path.Key));
        }
    }
    if (Style.ColorParameter.IsNone())
    {
        Problems.Add(TEXT("ColorParameter must name the material's color parameter"));
    }
    const FPSStadiumBowl& Bowl = Style.Bowl;
    for (const TPair<const TCHAR*, const FString*>& Color : {
        TPair<const TCHAR*, const FString*>(TEXT("GoalPostColor"), &Style.GoalPostColor),
        TPair<const TCHAR*, const FString*>(TEXT("GoalPostPadColor"), &Style.GoalPostPadColor),
        TPair<const TCHAR*, const FString*>(TEXT("RibbonColor"), &Style.RibbonColor),
        TPair<const TCHAR*, const FString*>(TEXT("BenchColor"), &Style.BenchColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.ConcreteColor"), &Bowl.ConcreteColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.StairColor"), &Bowl.StairColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.SeatColor"), &Bowl.SeatColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.WallColor"), &Bowl.WallColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.RailColor"), &Bowl.RailColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.FasciaColor"), &Bowl.FasciaColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.RibbonBoardColor"), &Bowl.RibbonBoardColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.GlassColor"), &Bowl.GlassColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.PortalColor"), &Bowl.PortalColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.RoofColor"), &Bowl.RoofColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.FixtureColor"), &Bowl.FixtureColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.BoardFrameColor"), &Bowl.BoardFrameColor),
        TPair<const TCHAR*, const FString*>(TEXT("Bowl.BoardScreenColor"), &Bowl.BoardScreenColor) })
    {
        FLinearColor Parsed;
        if (!UPSUITeamCatalog::ParseHexColor(*Color.Value, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("%s '%s' is not #RRGGBB"), Color.Key, **Color.Value));
        }
    }
    const FPSStadiumStructures& Structures = Style.Structures;
    for (const TPair<const TCHAR*, float>& Positive : {
        TPair<const TCHAR*, float>(TEXT("MeshSizeCm"), Style.MeshSizeCm),
        TPair<const TCHAR*, float>(TEXT("CrossbarHeightYards"), Style.CrossbarHeightYards),
        TPair<const TCHAR*, float>(TEXT("CrossbarWidthYards"), Style.CrossbarWidthYards),
        TPair<const TCHAR*, float>(TEXT("UprightHeightYards"), Style.UprightHeightYards),
        TPair<const TCHAR*, float>(TEXT("PostDiameterYards"), Style.PostDiameterYards),
        TPair<const TCHAR*, float>(TEXT("BasePostDiameterYards"), Style.BasePostDiameterYards),
        TPair<const TCHAR*, float>(TEXT("BaseSetbackYards"), Style.BaseSetbackYards),
        TPair<const TCHAR*, float>(TEXT("GoalPostPadHeightYards"), Style.GoalPostPadHeightYards),
        TPair<const TCHAR*, float>(TEXT("GoalPostPadDiameterYards"), Style.GoalPostPadDiameterYards),
        TPair<const TCHAR*, float>(TEXT("RibbonLengthYards"), Style.RibbonLengthYards),
        TPair<const TCHAR*, float>(TEXT("RibbonWidthYards"), Style.RibbonWidthYards),
        TPair<const TCHAR*, float>(TEXT("BenchDepthYards"), Style.BenchDepthYards),
        TPair<const TCHAR*, float>(TEXT("BenchHeightYards"), Style.BenchHeightYards),
        TPair<const TCHAR*, float>(TEXT("Bowl.CornerRadiusCm"), Bowl.CornerRadiusCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.SectionWidthCm"), Bowl.SectionWidthCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.AisleWidthCm"), Bowl.AisleWidthCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.MaxSegmentCm"), Bowl.MaxSegmentCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.SeatPitchCm"), Bowl.SeatPitchCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.SeatWidthCm"), Bowl.SeatWidthCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.SeatDepthCm"), Bowl.SeatDepthCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.SeatHeightCm"), Bowl.SeatHeightCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.SeatBackHeightCm"), Bowl.SeatBackHeightCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.SeatSetbackCm"), Bowl.SeatSetbackCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.WallHeightCm"), Bowl.WallHeightCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.WallThicknessCm"), Bowl.WallThicknessCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.WalkwayHeightCm"), Bowl.WalkwayHeightCm),
        TPair<const TCHAR*, float>(TEXT("Bowl.RailHeightCm"), Bowl.RailHeightCm),
        TPair<const TCHAR*, float>(TEXT("Structures.BackWallHeightCm"), Structures.BackWallHeightCm),
        TPair<const TCHAR*, float>(TEXT("Structures.BackWallThicknessCm"), Structures.BackWallThicknessCm),
        TPair<const TCHAR*, float>(TEXT("Structures.CanopyThicknessCm"), Structures.CanopyThicknessCm),
        TPair<const TCHAR*, float>(TEXT("Structures.LightBankSpacingCm"), Structures.LightBankSpacingCm),
        TPair<const TCHAR*, float>(TEXT("Structures.LightBankWidthCm"), Structures.LightBankWidthCm),
        TPair<const TCHAR*, float>(TEXT("Structures.LightBankHeightCm"), Structures.LightBankHeightCm) })
    {
        if (!(Positive.Value > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be above 0"), Positive.Key));
        }
    }
    for (const TPair<const TCHAR*, float>& NonNegative : {
        TPair<const TCHAR*, float>(TEXT("BenchDistanceYards"), Style.BenchDistanceYards),
        TPair<const TCHAR*, float>(TEXT("Bowl.WallOffsetCm"), Bowl.WallOffsetCm),
        TPair<const TCHAR*, float>(TEXT("Structures.CrossAisleCm"), Structures.CrossAisleCm),
        TPair<const TCHAR*, float>(TEXT("Structures.SuiteGlassHeightCm"), Structures.SuiteGlassHeightCm),
        TPair<const TCHAR*, float>(TEXT("Structures.SuiteFloorCm"), Structures.SuiteFloorCm),
        TPair<const TCHAR*, float>(TEXT("Structures.RibbonHeightCm"), Structures.RibbonHeightCm),
        TPair<const TCHAR*, float>(TEXT("Structures.CanopyDepthCm"), Structures.CanopyDepthCm),
        TPair<const TCHAR*, float>(TEXT("Structures.CanopyHeightCm"), Structures.CanopyHeightCm),
        TPair<const TCHAR*, float>(TEXT("Structures.LightBankTiltDegrees"), Structures.LightBankTiltDegrees),
        TPair<const TCHAR*, float>(TEXT("Structures.PressBoxLengthCm"), Structures.PressBoxLengthCm),
        TPair<const TCHAR*, float>(TEXT("Structures.PressBoxHeightCm"), Structures.PressBoxHeightCm),
        TPair<const TCHAR*, float>(TEXT("Structures.PressBoxDepthCm"), Structures.PressBoxDepthCm),
        TPair<const TCHAR*, float>(TEXT("Structures.VideoBoardWidthCm"), Structures.VideoBoardWidthCm),
        TPair<const TCHAR*, float>(TEXT("Structures.VideoBoardHeightCm"), Structures.VideoBoardHeightCm),
        TPair<const TCHAR*, float>(TEXT("Structures.VideoBoardLiftCm"), Structures.VideoBoardLiftCm),
        TPair<const TCHAR*, float>(TEXT("Structures.VideoBoardBezelCm"), Structures.VideoBoardBezelCm) })
    {
        if (!(NonNegative.Value >= 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be 0 or more"), NonNegative.Key));
        }
    }
    if (!(Style.BenchFromYardLine >= 0.f && Style.BenchFromYardLine < Style.BenchToYardLine))
    {
        Problems.Add(TEXT("BenchFromYardLine must be 0 or more and before BenchToYardLine"));
    }
    if (Bowl.CornerSections < 1 || Structures.SuiteLevels < 0)
    {
        Problems.Add(TEXT("Bowl.CornerSections must be 1 or more and Structures.SuiteLevels 0 or more"));
    }
    if (Bowl.SeatWidthCm > Bowl.SeatPitchCm)
    {
        Problems.Add(TEXT("Bowl.SeatWidthCm must fit within Bowl.SeatPitchCm"));
    }
    if (Style.Decks.Num() == 0)
    {
        Problems.Add(TEXT("Decks must hold at least one deck"));
    }
    TSet<FName> DeckIds;
    for (int32 Index = 0; Index < Style.Decks.Num(); ++Index)
    {
        const FPSStadiumDeck& Deck = Style.Decks[Index];
        if (Deck.DeckId.IsNone() || DeckIds.Contains(Deck.DeckId))
        {
            Problems.Add(FString::Printf(TEXT("Decks[%d]: DeckId is empty or used twice"), Index));
        }
        DeckIds.Add(Deck.DeckId);
        if (Deck.Rows < 1 || !(Deck.RowDepthCm > 0.f) || !(Deck.RowRiseCm > 0.f) || !(Deck.FrontHeightCm > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("Decks[%d]: Rows, RowDepthCm, RowRiseCm and FrontHeightCm must be above 0"), Index));
        }
        if (Deck.RowDepthCm < Bowl.SeatSetbackCm + Bowl.SeatDepthCm * 0.5f)
        {
            Problems.Add(FString::Printf(TEXT("Decks[%d]: RowDepthCm must hold a seat at Bowl.SeatSetbackCm"), Index));
        }
        if (!(Deck.FrontOffsetCm >= Bowl.WallThicknessCm) || !(Deck.SlabCm >= 0.f) || !(Deck.FasciaHeightCm >= 0.f) || !(Deck.ParapetHeightCm >= 0.f))
        {
            Problems.Add(FString::Printf(TEXT("Decks[%d]: FrontOffsetCm must clear the field wall; SlabCm, FasciaHeightCm and ParapetHeightCm must be 0 or more"), Index));
        }
        if (Index == 0 && Deck.SlabCm > 0.f)
        {
            Problems.Add(TEXT("Decks[0]: the lower bowl stands on the ground: SlabCm must be 0"));
        }
        if (Index > 0)
        {
            const FPSStadiumDeck& Below = Style.Decks[Index - 1];
            const float BelowTop = Below.FrontHeightCm + (Below.Rows - 1) * Below.RowRiseCm;
            if (!(Deck.FrontHeightCm - Deck.RowRiseCm - Deck.SlabCm - Deck.FasciaHeightCm > BelowTop))
            {
                Problems.Add(FString::Printf(TEXT("Decks[%d]: its fascia's bottom must clear the top row of the deck below"), Index));
            }
            if (!(Deck.FrontOffsetCm > Below.FrontOffsetCm))
            {
                Problems.Add(FString::Printf(TEXT("Decks[%d]: it must start behind the deck below's front"), Index));
            }
        }
        if (Deck.VomitoryEverySections < 0 || Deck.VomitoryFirstRow < 0 || Deck.VomitoryRows < 0 || !(Deck.VomitoryWidthCm >= 0.f))
        {
            Problems.Add(FString::Printf(TEXT("Decks[%d]: the vomitory numbers must be 0 or more"), Index));
        }
        else if (Deck.VomitoryEverySections > 0 && (Deck.VomitoryFirstRow + Deck.VomitoryRows >= Deck.Rows || Deck.VomitoryWidthCm + Bowl.AisleWidthCm >= Bowl.SectionWidthCm))
        {
            Problems.Add(FString::Printf(TEXT("Decks[%d]: a vomitory must end before the deck's last row and fit in a section"), Index));
        }
    }
    return Problems;
}

FPSStadiumLayout APSStadiumSet::ComputeLayout(const FPSFieldDimensions& Dimensions, const FPSStadiumSetStyle& Style, EPSStadiumDetail Detail)
{
    using namespace PSStadiumSetPrivate;

    FPSStadiumLayout Result;
    AddFieldFurniture(Result.Pieces, Dimensions, Style);

    const FPSStadiumBowl& Bowl = Style.Bowl;
    const FPSStadiumStructures& Structures = Style.Structures;
    const FPlan Plan = MakePlan(Dimensions, Bowl);
    Result.NumSections = Plan.NumSections;
    if (Style.Decks.Num() == 0)
    {
        return Result;
    }
    const double MaxSegment = Bowl.MaxSegmentCm;
    const double Wall = Bowl.WallThicknessCm;
    const double Rail = Bowl.RailHeightCm;

    // The padded field wall at the ground's edge, its rail, and the walkway behind it.
    AddRing(Result.Pieces, EPSStadiumPieceKind::Wall, Plan, 0.0, Wall, 0.0, Bowl.WallHeightCm, MaxSegment);
    AddRing(Result.Pieces, EPSStadiumPieceKind::Rail, Plan, -2.0, Wall + 2.0, Bowl.WallHeightCm, Bowl.WallHeightCm + Rail, MaxSegment);
    AddRing(Result.Pieces, EPSStadiumPieceKind::Concrete, Plan, Wall, Style.Decks[0].FrontOffsetCm, 0.0, Bowl.WalkwayHeightCm, MaxSegment);

    for (int32 DeckIndex = 0; DeckIndex < Style.Decks.Num(); ++DeckIndex)
    {
        const FPSStadiumDeck& Deck = Style.Decks[DeckIndex];
        AddDeck(Result, Plan, Bowl, Deck, DeckIndex, Detail);

        // The deck's front: a fascia under the front row, its ribbon board, and the parapet's rail.
        const FRowFrame First = RowFrame(Deck, 0);
        const double FasciaTop = First.Tread + Deck.ParapetHeightCm;
        const double FasciaBottom = First.Tread - Deck.RowRiseCm - Deck.SlabCm - Deck.FasciaHeightCm;
        if (Deck.FasciaHeightCm > 0.f || Deck.ParapetHeightCm > 0.f)
        {
            const double Bottom = Deck.FasciaHeightCm > 0.f ? FasciaBottom : First.Tread;
            AddRing(Result.Pieces, EPSStadiumPieceKind::Fascia, Plan, Deck.FrontOffsetCm - Wall, Deck.FrontOffsetCm, Bottom, FasciaTop, MaxSegment);
            AddRing(Result.Pieces, EPSStadiumPieceKind::Rail, Plan, Deck.FrontOffsetCm - Wall - 2.0, Deck.FrontOffsetCm + 2.0, FasciaTop, FasciaTop + Rail, MaxSegment);
        }
        if (Structures.RibbonHeightCm > 0.f && Deck.FasciaHeightCm >= Structures.RibbonHeightCm)
        {
            const double Band = First.Tread - Deck.RowRiseCm - Deck.SlabCm - FasciaBottom;
            const double RibbonBottom = FasciaBottom + (Band - Structures.RibbonHeightCm) * 0.5;
            AddRing(Result.Pieces, EPSStadiumPieceKind::RibbonBoard, Plan, Deck.FrontOffsetCm - Wall - 6.0, Deck.FrontOffsetCm - Wall,
                RibbonBottom, RibbonBottom + Structures.RibbonHeightCm, MaxSegment);
        }

        const FRowFrame Top = RowFrame(Deck, Deck.Rows - 1);
        if (DeckIndex + 1 < Style.Decks.Num())
        {
            // Behind it: the cross-aisle, then the suites' facade up to the deck above.
            const FPSStadiumDeck& Above = Style.Decks[DeckIndex + 1];
            const double AisleBottom = Deck.SlabCm > 0.f ? Top.Bottom : 0.0;
            const double Facade = Top.Back + Structures.CrossAisleCm;
            AddRing(Result.Pieces, EPSStadiumPieceKind::Concrete, Plan, Top.Back, Facade, AisleBottom, Top.Tread, MaxSegment);
            double Level = Top.Tread;
            for (int32 Suite = 0; Suite < Structures.SuiteLevels; ++Suite)
            {
                AddRing(Result.Pieces, EPSStadiumPieceKind::Glass, Plan, Facade, Facade + 30.0, Level, Level + Structures.SuiteGlassHeightCm, MaxSegment);
                Level += Structures.SuiteGlassHeightCm;
                AddRing(Result.Pieces, EPSStadiumPieceKind::Concrete, Plan, Facade, Facade + 30.0, Level, Level + Structures.SuiteFloorCm, MaxSegment);
                Level += Structures.SuiteFloorCm;
            }
            AddRing(Result.Pieces, EPSStadiumPieceKind::Concrete, Plan, Facade, Facade + 30.0, Level, SoffitAt(Above, Facade + 30.0), MaxSegment);

            // The press box: on the first cross-aisle, at the 50 of the -Y sideline.
            if (DeckIndex == 0 && Structures.PressBoxLengthCm > 0.f && Structures.PressBoxHeightCm > 0.f && Structures.PressBoxDepthCm > 0.f)
            {
                for (const FPlanSpan& Span : Plan.Spans)
                {
                    if (!Span.bNearSideline)
                    {
                        continue;
                    }
                    const double HalfLength = FMath::Min<double>(Structures.PressBoxLengthCm, Span.Length) * 0.5;
                    const double Middle = Span.Length * 0.5;
                    const double Front = Facade - Structures.PressBoxDepthCm;
                    const double Base = 60.0;
                    AddBand(Result.Pieces, EPSStadiumPieceKind::Concrete, Span, Middle - HalfLength, Middle + HalfLength, Front, Facade, Top.Tread, Top.Tread + Base, MaxSegment);
                    AddBand(Result.Pieces, EPSStadiumPieceKind::Glass, Span, Middle - HalfLength, Middle + HalfLength, Front, Facade,
                        Top.Tread + Base, Top.Tread + Structures.PressBoxHeightCm, MaxSegment);
                    AddBand(Result.Pieces, EPSStadiumPieceKind::Fascia, Span, Middle - HalfLength - 20.0, Middle + HalfLength + 20.0, Front - 20.0, Facade,
                        Top.Tread + Structures.PressBoxHeightCm, Top.Tread + Structures.PressBoxHeightCm + 40.0, MaxSegment);
                }
            }
            continue;
        }

        // The last deck: its back wall, the canopy, the light banks and the video boards.
        const double BackFront = Top.Back;
        const double BackRear = Top.Back + Structures.BackWallThicknessCm;
        const double WallTop = Top.Tread + Structures.BackWallHeightCm;
        const bool bCanopy = Structures.CanopyDepthCm > 0.f;
        const double CanopyBottom = Top.Tread + Structures.CanopyHeightCm;
        const double CanopyTop = CanopyBottom + Structures.CanopyThicknessCm;
        for (const FPlanSpan& Span : Plan.Spans)
        {
            const bool bCovered = bCanopy && (Span.bSideline || !Structures.bCanopySidelinesOnly);
            AddBand(Result.Pieces, EPSStadiumPieceKind::Concrete, Span, Span.Start(), Span.End(), BackFront, BackRear, 0.0,
                bCovered ? FMath::Max(WallTop, CanopyTop) : WallTop, MaxSegment);
            if (bCovered)
            {
                AddBand(Result.Pieces, EPSStadiumPieceKind::Roof, Span, Span.Start(), Span.End(), BackRear - Structures.CanopyDepthCm, BackRear,
                    CanopyBottom, CanopyTop, MaxSegment);
            }

            if (Span.bSideline && Span.Length > 0.0)
            {
                // The light banks: under the canopy's front edge, or on the back wall.
                const int32 Banks = FMath::Max(1, FloorToCount(Span.Length / Structures.LightBankSpacingCm));
                const double Edge = bCanopy ? BackRear - Structures.CanopyDepthCm + 40.0 : (BackFront + BackRear) * 0.5;
                const double BankZ = bCanopy ? CanopyBottom - Structures.LightBankHeightCm * 0.5 - 20.0 : WallTop + Structures.LightBankHeightCm * 0.5;
                const FRotator Aim(-Structures.LightBankTiltDegrees, YawOf(-Span.Normal), 0.0);
                for (int32 Bank = 0; Bank < Banks; ++Bank)
                {
                    const double U = Span.Length * (Bank + 0.5) / Banks;
                    const FVector2D Point = Span.PointAt(U, Edge);
                    const FVector Centre(Point.X, Point.Y, BankZ);
                    Result.Pieces.Add(MakePiece(EPSStadiumPieceKind::LightFixture, false, Centre,
                        FVector(60.0, Structures.LightBankWidthCm, Structures.LightBankHeightCm), Aim));
                    Result.LightBanks.Add(FTransform(Aim, Centre + Aim.Vector() * 31.0));
                }
            }

            if (Span.bEnd && Span.Length > 0.0 && Structures.VideoBoardWidthCm > 0.f && Structures.VideoBoardHeightCm > 0.f)
            {
                // A video board over the end, facing the field, on two legs from the wall's top.
                const double Middle = Span.Length * 0.5;
                const double HalfWidth = Structures.VideoBoardWidthCm * 0.5;
                const double Bezel = Structures.VideoBoardBezelCm;
                const double FrameBottom = WallTop + Structures.VideoBoardLiftCm;
                const double FrameTop = FrameBottom + Structures.VideoBoardHeightCm + 2.0 * Bezel;
                const double Centre = (BackFront + BackRear) * 0.5;
                AddBand(Result.Pieces, EPSStadiumPieceKind::BoardFrame, Span, Middle - HalfWidth - Bezel, Middle + HalfWidth + Bezel,
                    Centre - 40.0, Centre + 40.0, FrameBottom, FrameTop, MaxSegment);
                AddBand(Result.Pieces, EPSStadiumPieceKind::BoardScreen, Span, Middle - HalfWidth, Middle + HalfWidth,
                    Centre - 46.0, Centre - 40.0, FrameBottom + Bezel, FrameTop - Bezel, MaxSegment);
                for (const double Leg : { -1.0, 1.0 })
                {
                    const double LegU = Middle + Leg * HalfWidth * 0.6;
                    AddBand(Result.Pieces, EPSStadiumPieceKind::Concrete, Span, LegU - 60.0, LegU + 60.0, Centre - 30.0, Centre + 30.0, WallTop, FrameBottom, MaxSegment);
                }
            }
        }
    }
    return Result;
}

bool APSStadiumSet::BuildFromData()
{
    const FPSPlatformTier& Tier = PSPlatformTiers::GetActiveTier();
    const FPSStadiumSetStyle Style = LoadStyle(GetDefaultStylePath());
    if (!Build(PSField::GetDimensions(), Style, Tier.StadiumDetail))
    {
        return false;
    }
    if (Crowd)
    {
        // The fans sit on the seats the style built: their height is the bowl's.
        Crowd->PopulateFromData(GetSeats(), Style.Bowl.SeatHeightCm);
    }
    return true;
}

bool APSStadiumSet::Build(const FPSFieldDimensions& Dimensions, const FPSStadiumSetStyle& Style, EPSStadiumDetail Detail)
{
    using namespace PSStadiumSetPrivate;

    UStaticMesh* BoxMesh = LoadAsset<UStaticMesh>(Style.BoxMeshPath, TEXT("box mesh"));
    UStaticMesh* CylinderMesh = LoadAsset<UStaticMesh>(Style.CylinderMeshPath, TEXT("cylinder mesh"));
    UMaterialInterface* Material = LoadAsset<UMaterialInterface>(Style.MaterialPath, TEXT("material"));
    if (!BoxMesh || !CylinderMesh || !Material)
    {
        return false;
    }

    // The set is the world's frame, wherever this actor was put.
    SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
    SetActorScale3D(FVector::OneVector);

    Layout = ComputeLayout(Dimensions, Style, Detail);

    const FPSStadiumBowl& Bowl = Style.Bowl;
    TArray<const FString*> Colors;
    Colors.Init(nullptr, static_cast<int32>(EPSStadiumPieceKind::Count));
    Colors[static_cast<int32>(EPSStadiumPieceKind::GoalPost)] = &Style.GoalPostColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::GoalPostPad)] = &Style.GoalPostPadColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::Ribbon)] = &Style.RibbonColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::Bench)] = &Style.BenchColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::Concrete)] = &Bowl.ConcreteColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::Stair)] = &Bowl.StairColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::Seat)] = &Bowl.SeatColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::Wall)] = &Bowl.WallColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::Rail)] = &Bowl.RailColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::Fascia)] = &Bowl.FasciaColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::RibbonBoard)] = &Bowl.RibbonBoardColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::Glass)] = &Bowl.GlassColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::Portal)] = &Bowl.PortalColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::Roof)] = &Bowl.RoofColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::LightFixture)] = &Bowl.FixtureColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::BoardFrame)] = &Bowl.BoardFrameColor;
    Colors[static_cast<int32>(EPSStadiumPieceKind::BoardScreen)] = &Bowl.BoardScreenColor;

    // Each kind's instances, in one batch per mesh.
    const float MeshSize = FMath::Max(Style.MeshSizeCm, 1.f);
    TArray<TArray<FTransform>> Transforms;
    Transforms.SetNum(static_cast<int32>(EPSStadiumPieceKind::Count));
    for (const FPSStadiumPiece& Piece : Layout.Pieces)
    {
        Transforms[static_cast<int32>(Piece.Kind)].Add(FTransform(Piece.Rotation, Piece.Center, Piece.Size / MeshSize));
    }

    Materials.Reset();
    const bool bShadows = Detail == EPSStadiumDetail::Full;
    for (int32 Index = 0; Index < static_cast<int32>(EPSStadiumPieceKind::Count); ++Index)
    {
        UInstancedStaticMeshComponent* Pieces = PieceMeshes.IsValidIndex(Index) ? PieceMeshes[Index] : nullptr;
        if (!Pieces)
        {
            continue;
        }
        const EPSStadiumPieceKind Kind = static_cast<EPSStadiumPieceKind>(Index);
        const bool bCylinder = Kind == EPSStadiumPieceKind::GoalPost || Kind == EPSStadiumPieceKind::GoalPostPad;
        FLinearColor Color = FLinearColor::White;
        if (Colors[Index])
        {
            UPSUITeamCatalog::ParseHexColor(*Colors[Index], Color);
        }
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
        Instance->SetVectorParameterValue(Style.ColorParameter, Color);
        Materials.Add(Instance);
        Pieces->ClearInstances();
        Pieces->SetStaticMesh(bCylinder ? CylinderMesh : BoxMesh);
        Pieces->SetMaterial(0, Instance);
        Pieces->SetCastShadow(bShadows && Kind != EPSStadiumPieceKind::BoardScreen && Kind != EPSStadiumPieceKind::RibbonBoard);
        if (Transforms[Index].Num() > 0)
        {
            Pieces->AddInstances(Transforms[Index], false);
        }
    }
    bBuilt = true;
    return true;
}
