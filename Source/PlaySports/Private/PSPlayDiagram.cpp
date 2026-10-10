#include "PSPlayDiagram.h"
#include "PSPlayArt.h"
#include "PSPlayerPawn.h"
#include "PSUITeamCatalog.h"

namespace PSPlayDiagramPrivate
{
    // The diagram's layers, lowest first.
    constexpr int32 GroundLayer = 0;
    constexpr int32 GuideLayer = 1;
    constexpr int32 ArtLayer = 2;
    constexpr int32 PlayerLayer = 3;

    FLinearColor ParseColor(const FString& Hex)
    {
        FLinearColor Parsed = FLinearColor::White;
        UPSUITeamCatalog::ParseHexColor(Hex, Parsed);
        return Parsed;
    }

    FLinearColor WithOpacity(FLinearColor Color, float Opacity)
    {
        Color.A *= FMath::Clamp(Opacity, 0.f, 1.f);
        return Color;
    }

    TArray<FVector2D> ToFieldPoints(const TArray<FVector>& Points)
    {
        TArray<FVector2D> Flat;
        Flat.Reserve(Points.Num());
        for (const FVector& Point : Points)
        {
            Flat.Add(PSPlayDiagram::ToField(Point));
        }
        return Flat;
    }

    /** A line through Points with an arrowhead where it ends (none when its last two points
     *  coincide). */
    void AddArrow(TArray<FPSWidgetStroke>& Strokes, const TArray<FVector2D>& Points, const FLinearColor& Color, float Width, FName Tag, int32 Layer,
        const FPSPlayDiagramStyle& Style, bool bHead)
    {
        if (Points.Num() < 2)
        {
            return;
        }
        Strokes.Add(PSWidgetDrawing::MakeStroke(Points, Color, Width, Tag, Layer));
        if (bHead)
        {
            const TArray<FVector2D> Head = PSWidgetDrawing::ArrowHead(Points[Points.Num() - 2], Points.Last(), Style.ArrowheadLength, Style.ArrowheadAngleDegrees);
            if (Head.Num() > 0)
            {
                Strokes.Add(PSWidgetDrawing::MakeStroke(Head, Color, Width, TEXT("Arrowhead"), Layer));
            }
        }
    }

    /** A player's mark at Spot: a ring for the offense, an X for the defense. */
    void AddPlayer(TArray<FPSWidgetStroke>& Strokes, const FVector2D& Spot, bool bOffensivePlayer, const FLinearColor& Color, FName Tag, int32 Layer,
        const FPSPlayDiagramStyle& Style)
    {
        if (bOffensivePlayer)
        {
            Strokes.Add(PSWidgetDrawing::MakeStroke(PSWidgetDrawing::Circle(Spot, Style.PlayerRadius, Style.CircleSegments), Color, Style.MarkWidth, Tag, Layer, true));
            return;
        }
        Strokes.Add(PSWidgetDrawing::MakeStroke(PSWidgetDrawing::CrossArm(Spot, Style.PlayerRadius, true), Color, Style.MarkWidth, Tag, Layer));
        Strokes.Add(PSWidgetDrawing::MakeStroke(PSWidgetDrawing::CrossArm(Spot, Style.PlayerRadius, false), Color, Style.MarkWidth, Tag, Layer));
    }

    /** A blocker's T: a stem from his spot (upfield for a run block, back for a pass block) and a
     *  bar across its end. */
    void AddBlock(TArray<FPSWidgetStroke>& Strokes, const FVector2D& Spot, bool bRunBlock, const FLinearColor& Color, const FPSPlayDiagramStyle& Style)
    {
        const float Stem = bRunBlock ? Style.RunBlockStemLength : -Style.PassBlockStemLength;
        const FVector2D End = Spot + FVector2D(Stem, 0.f);
        if (!FMath::IsNearlyZero(Stem))
        {
            Strokes.Add(PSWidgetDrawing::MakeStroke({ Spot, End }, Color, Style.MarkWidth, TEXT("Block"), GuideLayer));
        }
        const FVector2D HalfBar(0.f, Style.BlockBarWidth * 0.5f);
        Strokes.Add(PSWidgetDrawing::MakeStroke({ End - HalfBar, End + HalfBar }, Color, Style.MarkWidth, TEXT("Block"), GuideLayer));
    }
}

TArray<FString> PSPlayDiagram::ValidateStyle(const FPSPlayDiagramStyle& Style)
{
    TArray<FString> Problems;
    struct FNamedSize
    {
        const TCHAR* Field;
        float Value;
    };
    const FNamedSize AboveZero[] = {
        { TEXT("MinFieldWidth"), Style.MinFieldWidth }, { TEXT("MinFieldDepth"), Style.MinFieldDepth }, { TEXT("WidthScale"), Style.WidthScale },
        { TEXT("MinStrokeWidth"), Style.MinStrokeWidth }, { TEXT("PlayerRadius"), Style.PlayerRadius }, { TEXT("MarkWidth"), Style.MarkWidth },
        { TEXT("ArrowheadLength"), Style.ArrowheadLength }, { TEXT("BlockBarWidth"), Style.BlockBarWidth }, { TEXT("PreviewWidth"), Style.PreviewWidth },
        { TEXT("PreviewHeight"), Style.PreviewHeight } };
    for (const FNamedSize& Named : AboveZero)
    {
        if (!(Named.Value > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be above 0"), Named.Field));
        }
    }
    const FNamedSize ZeroOrMore[] = {
        { TEXT("FieldMargin"), Style.FieldMargin }, { TEXT("RunBlockStemLength"), Style.RunBlockStemLength }, { TEXT("PassBlockStemLength"), Style.PassBlockStemLength } };
    for (const FNamedSize& Named : ZeroOrMore)
    {
        if (Named.Value < 0.f)
        {
            Problems.Add(FString::Printf(TEXT("%s must be 0 or more"), Named.Field));
        }
    }
    const FNamedSize Fractions[] = {
        { TEXT("OpponentOpacity"), Style.OpponentOpacity }, { TEXT("GuideOpacity"), Style.GuideOpacity }, { TEXT("BackgroundOpacity"), Style.BackgroundOpacity } };
    for (const FNamedSize& Named : Fractions)
    {
        if (Named.Value < 0.f || Named.Value > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("%s must be between 0 and 1"), Named.Field));
        }
    }
    if (Style.ArrowheadAngleDegrees <= 0.f || Style.ArrowheadAngleDegrees >= 90.f)
    {
        Problems.Add(TEXT("ArrowheadAngleDegrees must be above 0 and below 90"));
    }
    if (Style.CircleSegments < 6)
    {
        Problems.Add(TEXT("CircleSegments must be 6 or more"));
    }
    struct FNamedColor
    {
        const TCHAR* Field;
        const FString* Hex;
    };
    const FNamedColor Colors[] = {
        { TEXT("OffenseColor"), &Style.OffenseColor }, { TEXT("DefenseColor"), &Style.DefenseColor }, { TEXT("BlockColor"), &Style.BlockColor },
        { TEXT("LineOfScrimmageColor"), &Style.LineOfScrimmageColor }, { TEXT("BackgroundColor"), &Style.BackgroundColor } };
    FLinearColor Parsed;
    for (const FNamedColor& Named : Colors)
    {
        if (!UPSUITeamCatalog::ParseHexColor(*Named.Hex, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("%s: '%s' must be #RRGGBB"), Named.Field, **Named.Hex));
        }
    }
    return Problems;
}

FPSPlayDiagram PSPlayDiagram::BuildDiagram(const FPSPlayDefinition& Play, const TArray<FPSResolvedAssignment>& Resolved, const TArray<FPSPlayArtPrimitive>& Art,
    const FVector& LineOfScrimmage, const FPSPlayArtStyle& Style)
{
    using namespace PSPlayDiagramPrivate;

    const FPSPlayDiagramStyle& Look = Style.Diagram;
    FPSPlayDiagram Diagram;
    Diagram.PlayId = Play.PlayId;
    Diagram.bOffense = Play.bIsOffensivePlay;
    const EPSTeamSide DrawnSide = Play.bIsOffensivePlay ? EPSTeamSide::Offense : EPSTeamSide::Defense;
    const FLinearColor OffenseColor = ParseColor(Look.OffenseColor);
    const FLinearColor DefenseColor = ParseColor(Look.DefenseColor);
    const FLinearColor SideColor = Play.bIsOffensivePlay ? OffenseColor : DefenseColor;
    const FLinearColor GuideColor = WithOpacity(SideColor, Look.GuideOpacity);
    const FLinearColor BlockColor = ParseColor(Look.BlockColor);

    // The players, and the jobs of the drawn side that the field art leaves out.
    TMap<const APSPlayerPawn*, FVector2D> Spots;
    TArray<FPSWidgetStroke>& Strokes = Diagram.Strokes;
    for (const FPSResolvedAssignment& Entry : Resolved)
    {
        const APSPlayerPawn* Player = Entry.Pawn.Get();
        if (!Player)
        {
            continue;
        }
        const FVector2D Spot = ToField(Entry.PawnLocation);
        Spots.Add(Player, Spot);
        const bool bOffensivePlayer = Player->TeamSide == EPSTeamSide::Offense;
        if (Player->TeamSide != DrawnSide)
        {
            if (Look.OpponentOpacity > 0.f)
            {
                AddPlayer(Strokes, Spot, bOffensivePlayer, WithOpacity(bOffensivePlayer ? OffenseColor : DefenseColor, Look.OpponentOpacity), TEXT("Opponent"), GroundLayer, Look);
            }
            continue;
        }
        AddPlayer(Strokes, Spot, bOffensivePlayer, SideColor, TEXT("Player"), PlayerLayer, Look);
        if (!Entry.bHasSlot || !Play.bIsOffensivePlay)
        {
            continue;
        }
        const EPSAssignmentKind Kind = Entry.Assignment.Kind;
        if (Kind == EPSAssignmentKind::RunBlock || Kind == EPSAssignmentKind::PassBlock)
        {
            AddBlock(Strokes, Spot, Kind == EPSAssignmentKind::RunBlock, BlockColor, Look);
        }
        else if (Entry.RunsRoute() && Entry.Assignment.RouteId.IsNone() && Entry.Waypoints.Num() > 0)
        {
            // "Go to your spot": the field draws nothing, the diagram shows where he goes.
            TArray<FVector2D> Path = { Spot };
            for (const FVector& Waypoint : Entry.Waypoints)
            {
                Path.Add(ToField(Waypoint));
            }
            if (FVector2D::Distance(Path[0], Path.Last()) > Look.PlayerRadius)
            {
                AddArrow(Strokes, Path, GuideColor, Look.MarkWidth, TEXT("Spot"), GuideLayer, Look, true);
            }
        }
    }

    // Which players' routes split at a read: their stem ends where the branches start.
    TSet<const APSPlayerPawn*> HasBranches;
    for (const FPSPlayArtPrimitive& Piece : Art)
    {
        if (Piece.Shape == EPSPlayArtShape::Ribbon && Piece.bBranch)
        {
            HasBranches.Add(Piece.Pawn.Get());
        }
    }

    // The field's art, laid flat.
    for (const FPSPlayArtPrimitive& Piece : Art)
    {
        if (Piece.Points.Num() == 0)
        {
            continue;
        }
        const TArray<FVector2D> Points = ToFieldPoints(Piece.Points);
        const FLinearColor Color = WithOpacity(Piece.Color, Piece.Opacity);
        const float Width = Piece.Size * Look.WidthScale;
        switch (Piece.Shape)
        {
        case EPSPlayArtShape::Ribbon:
        {
            const bool bStem = !Piece.bBranch && HasBranches.Contains(Piece.Pawn.Get());
            AddArrow(Strokes, Points, Color, Width, Piece.bBranch ? FName(TEXT("Branch")) : FName(TEXT("Route")), ArtLayer, Look, !bStem);
            break;
        }
        case EPSPlayArtShape::Star:
        {
            Strokes.Add(PSWidgetDrawing::MakeStroke(ToFieldPoints(PSPlayArt::StarOutline(Piece.Points[0], Piece.Size)), Color, Look.MarkWidth, TEXT("Zone"), ArtLayer, true));
            // His drop from where he stands to the landmark.
            const FVector2D* From = Spots.Find(Piece.Pawn.Get());
            if (From && FVector2D::Distance(*From, Points[0]) > Look.PlayerRadius + Piece.Size)
            {
                const FVector2D Toward = (Points[0] - *From).GetSafeNormal();
                Strokes.Add(PSWidgetDrawing::MakeStroke({ *From, Points[0] - Toward * Piece.Size }, GuideColor, Look.MarkWidth, TEXT("Drop"), GuideLayer));
            }
            break;
        }
        case EPSPlayArtShape::Connector:
            if (Points.Num() > 1)
            {
                Strokes.Add(PSWidgetDrawing::MakeStroke({ Points[0], Points[1] }, Color, Width, Piece.Source, ArtLayer));
            }
            break;
        case EPSPlayArtShape::Arrow:
            AddArrow(Strokes, Points, Color, Width, Piece.Source, ArtLayer, Look, true);
            break;
        default:
            // A ring marks a route's end on the field; the diagram's arrowhead does here.
            break;
        }
    }

    // The view: centred across on the ball, everything in it with a margin, at least the minimum
    // field.
    const FVector2D Ball = ToField(LineOfScrimmage);
    float MinX = Ball.X;
    float MaxX = Ball.X;
    float HalfWidth = 0.f;
    for (const FPSWidgetStroke& Stroke : Strokes)
    {
        for (const FVector2D& Point : Stroke.Points)
        {
            MinX = FMath::Min(MinX, static_cast<float>(Point.X));
            MaxX = FMath::Max(MaxX, static_cast<float>(Point.X));
            HalfWidth = FMath::Max(HalfWidth, static_cast<float>(FMath::Abs(Point.Y - Ball.Y)));
        }
    }
    MinX -= Look.FieldMargin;
    MaxX += Look.FieldMargin;
    HalfWidth = FMath::Max(HalfWidth + Look.FieldMargin, Look.MinFieldWidth * 0.5f);
    const float Grow = FMath::Max(0.f, Look.MinFieldDepth - (MaxX - MinX)) * 0.5f;
    MinX -= Grow;
    MaxX += Grow;
    Diagram.ViewCenter = FVector2D((MinX + MaxX) * 0.5f, Ball.Y);
    Diagram.ViewExtent = FVector2D((MaxX - MinX) * 0.5f, HalfWidth);

    // The line of scrimmage, across the whole view.
    Strokes.Add(PSWidgetDrawing::MakeStroke({ FVector2D(Ball.X, Ball.Y - HalfWidth), FVector2D(Ball.X, Ball.Y + HalfWidth) },
        ParseColor(Look.LineOfScrimmageColor), Look.MarkWidth, TEXT("Line"), GroundLayer));
    return Diagram;
}

FPSPlayDiagramTransform PSPlayDiagram::MakeTransform(const FPSPlayDiagram& Diagram, const FVector2D& WidgetSize)
{
    FPSPlayDiagramTransform Transform;
    Transform.ViewCenter = Diagram.ViewCenter;
    Transform.WidgetCenter = WidgetSize * 0.5;
    const double FieldWidth = Diagram.ViewExtent.Y * 2.0;
    const double FieldDepth = Diagram.ViewExtent.X * 2.0;
    if (FieldWidth > 0.0 && FieldDepth > 0.0 && WidgetSize.X > 0.0 && WidgetSize.Y > 0.0)
    {
        Transform.Scale = static_cast<float>(FMath::Min(WidgetSize.X / FieldWidth, WidgetSize.Y / FieldDepth));
    }
    return Transform;
}

TArray<FPSWidgetStroke> PSPlayDiagram::ToWidget(const FPSPlayDiagram& Diagram, const FVector2D& WidgetSize, float MinStrokeWidth)
{
    TArray<FPSWidgetStroke> Placed;
    const FPSPlayDiagramTransform Transform = MakeTransform(Diagram, WidgetSize);
    if (Transform.Scale <= 0.f)
    {
        return Placed;
    }
    Placed.Reserve(Diagram.Strokes.Num());
    for (const FPSWidgetStroke& Stroke : Diagram.Strokes)
    {
        FPSWidgetStroke& Out = Placed.Add_GetRef(Stroke);
        for (FVector2D& Point : Out.Points)
        {
            Point = Transform.ToWidget(Point);
        }
        Out.Width = FMath::Max(Stroke.Width * Transform.Scale, MinStrokeWidth);
    }
    return Placed;
}
