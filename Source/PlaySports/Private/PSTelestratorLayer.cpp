#include "PSTelestratorLayer.h"
#include "PSUITeamCatalog.h"

namespace PSTelestratorLayerPrivate
{
    FLinearColor LayerColor(const FString& Hex)
    {
        FLinearColor Parsed = FLinearColor::White;
        UPSUITeamCatalog::ParseHexColor(Hex, Parsed);
        return Parsed;
    }

    /** A stick past DeadZone, rescaled so the edge of the dead zone is 0 and full tilt is 1. */
    FVector2D PastDeadZone(const FVector2D& Stick, float DeadZone)
    {
        const double Tilt = Stick.Size();
        const double Zone = static_cast<double>(DeadZone);
        if (Tilt <= Zone || Tilt <= static_cast<double>(UE_KINDA_SMALL_NUMBER))
        {
            return FVector2D::ZeroVector;
        }
        const double Scaled = FMath::Min(1.0, (Tilt - Zone) / FMath::Max(1.0 - Zone, static_cast<double>(UE_KINDA_SMALL_NUMBER)));
        return Stick / Tilt * Scaled;
    }
}

EPSTelestratorTool PSTelestratorLayer::NextTool(EPSTelestratorTool Tool)
{
    switch (Tool)
    {
    case EPSTelestratorTool::Freehand:
        return EPSTelestratorTool::Arrow;
    case EPSTelestratorTool::Arrow:
        return EPSTelestratorTool::Circle;
    case EPSTelestratorTool::Circle:
        return EPSTelestratorTool::Player;
    default:
        return EPSTelestratorTool::Freehand;
    }
}

float PSTelestratorLayer::ShortSide(const FVector2D& WidgetSize)
{
    return static_cast<float>(FMath::Max(0.0, FMath::Min(WidgetSize.X, WidgetSize.Y)));
}

FVector2D PSTelestratorLayer::ToFrame(const FVector2D& Local, const FVector2D& WidgetSize)
{
    if (WidgetSize.X <= 0.0 || WidgetSize.Y <= 0.0)
    {
        return FVector2D(0.5, 0.5);
    }
    return FVector2D(FMath::Clamp(Local.X / WidgetSize.X, 0.0, 1.0), FMath::Clamp(Local.Y / WidgetSize.Y, 0.0, 1.0));
}

FVector2D PSTelestratorLayer::ToWidget(const FVector2D& FramePoint, const FVector2D& WidgetSize)
{
    return FVector2D(FramePoint.X * WidgetSize.X, FramePoint.Y * WidgetSize.Y);
}

TArray<FPSWidgetStroke> PSTelestratorLayer::MarkStrokes(EPSTelestratorTool Tool, const TArray<FVector2D>& ScreenPoints, const FLinearColor& Color,
    const FVector2D& WidgetSize, const FPSTelestratorTuning& Tuning)
{
    TArray<FPSWidgetStroke> Strokes;
    const float Short = ShortSide(WidgetSize);
    if (ScreenPoints.Num() == 0 || Short <= 0.f)
    {
        return Strokes;
    }
    TArray<FVector2D> Points;
    Points.Reserve(ScreenPoints.Num());
    for (const FVector2D& Point : ScreenPoints)
    {
        Points.Add(ToWidget(Point, WidgetSize));
    }
    const float Width = FMath::Max(Tuning.MarkWidth * Short, Tuning.MinStrokeWidth);
    switch (Tool)
    {
    case EPSTelestratorTool::Freehand:
        if (Points.Num() == 1)
        {
            // A tap is a dot.
            Strokes.Add(PSWidgetDrawing::MakeStroke(PSWidgetDrawing::Circle(Points[0], Width * 0.5f, Tuning.CircleSegments), Color, Width, TEXT("Freehand"), 0, true));
        }
        else
        {
            Strokes.Add(PSWidgetDrawing::MakeStroke(Points, Color, Width, TEXT("Freehand")));
        }
        break;
    case EPSTelestratorTool::Arrow:
        if (Points.Num() >= 2)
        {
            Strokes.Add(PSWidgetDrawing::MakeStroke({ Points[0], Points.Last() }, Color, Width, TEXT("Arrow")));
            const TArray<FVector2D> Head = PSWidgetDrawing::ArrowHead(Points[0], Points.Last(), Tuning.ArrowheadLength * Short, Tuning.ArrowheadAngleDegrees);
            if (Head.Num() > 0)
            {
                Strokes.Add(PSWidgetDrawing::MakeStroke(Head, Color, Width, TEXT("Arrowhead")));
            }
        }
        break;
    case EPSTelestratorTool::Circle:
    {
        // Measured on the screen, so the ring is round on a phone held either way.
        const float Radius = Points.Num() >= 2 ? static_cast<float>(FVector2D::Distance(Points[0], Points.Last())) : 0.f;
        if (Radius >= 1.f)
        {
            Strokes.Add(PSWidgetDrawing::MakeStroke(PSWidgetDrawing::Circle(Points[0], Radius, Tuning.CircleSegments), Color, Width, TEXT("Circle"), 0, true));
        }
        break;
    }
    case EPSTelestratorTool::Player:
        Strokes.Add(PSWidgetDrawing::MakeStroke(PSWidgetDrawing::Circle(Points[0], Tuning.PlayerRingRadius * Short, Tuning.CircleSegments), Color, Width, TEXT("Player"), 0, true));
        break;
    default:
        break;
    }
    return Strokes;
}

TArray<FPSWidgetStroke> PSTelestratorLayer::BuildStrokes(const FPSTelestration& Telestration, const TArray<FVector2D>& LiveStroke, EPSTelestratorTool LiveTool,
    const FVector2D& WidgetSize, const FPSTelestratorTuning& Tuning)
{
    using namespace PSTelestratorLayerPrivate;

    TArray<FPSWidgetStroke> Strokes;
    const FLinearColor HandColor = LayerColor(Tuning.MarkColor);
    const FLinearColor AutoColor = LayerColor(Tuning.AutoMarkColor);
    for (const FPSTelestratorMark& Mark : Telestration.Marks)
    {
        Strokes.Append(MarkStrokes(Mark.Tool, Mark.ScreenPoints, Mark.bAuto ? AutoColor : HandColor, WidgetSize, Tuning));
    }

    // The stroke in progress, as it would land.
    TArray<FVector2D> Live;
    if (LiveStroke.Num() > 0 && LiveTool != EPSTelestratorTool::Player)
    {
        Live = LiveTool == EPSTelestratorTool::Freehand ? LiveStroke : TArray<FVector2D>({ LiveStroke[0], LiveStroke.Last() });
    }
    for (FPSWidgetStroke& Stroke : MarkStrokes(LiveTool, Live, HandColor, WidgetSize, Tuning))
    {
        Stroke.Tag = TEXT("Live");
        Stroke.Layer = 1;
        Strokes.Add(Stroke);
    }
    return Strokes;
}

TArray<FPSWidgetStroke> PSTelestratorLayer::CursorStrokes(const FVector2D& Cursor, const FVector2D& WidgetSize, const FPSTelestratorTuning& Tuning)
{
    using namespace PSTelestratorLayerPrivate;

    TArray<FPSWidgetStroke> Strokes;
    const float Short = ShortSide(WidgetSize);
    if (Short <= 0.f)
    {
        return Strokes;
    }
    const float Radius = Tuning.CursorRadius * Short;
    const float Width = FMath::Max(Tuning.MarkWidth * Short * 0.5f, Tuning.MinStrokeWidth);
    const FLinearColor Color = LayerColor(Tuning.MarkColor);
    Strokes.Add(PSWidgetDrawing::MakeStroke(PSWidgetDrawing::Circle(Cursor, Radius, Tuning.CircleSegments), Color, Width, TEXT("Cursor"), 2, true));
    Strokes.Add(PSWidgetDrawing::MakeStroke({ Cursor - FVector2D(Radius * 0.5f, 0.0), Cursor + FVector2D(Radius * 0.5f, 0.0) }, Color, Width, TEXT("Cursor"), 2));
    Strokes.Add(PSWidgetDrawing::MakeStroke({ Cursor - FVector2D(0.0, Radius * 0.5f), Cursor + FVector2D(0.0, Radius * 0.5f) }, Color, Width, TEXT("Cursor"), 2));
    return Strokes;
}

FVector2D PSTelestratorLayer::CursorDirection(const TArray<FPSInputKeyBinding>& Bindings, const TSet<FName>& HeldKeys, const FVector2D& LeftStick,
    const FVector2D& RightStick, float DeadZone)
{
    using namespace PSTelestratorLayerPrivate;

    static const FName LeftStickKey(TEXT("Gamepad_Left2D"));
    static const FName RightStickKey(TEXT("Gamepad_Right2D"));
    FVector2D Direction = FVector2D::ZeroVector;
    for (const FPSInputKeyBinding& Binding : Bindings)
    {
        if (Binding.Key == LeftStickKey)
        {
            Direction += PastDeadZone(LeftStick, DeadZone);
        }
        else if (Binding.Key == RightStickKey)
        {
            Direction += PastDeadZone(RightStick, DeadZone);
        }
        else if (HeldKeys.Contains(Binding.Key))
        {
            const FVector2D Push = Binding.bSwizzleYX ? FVector2D(0.0, 1.0) : FVector2D(1.0, 0.0);
            Direction += Binding.bNegate ? -Push : Push;
        }
    }
    return Direction.SizeSquared() > 1.0 ? Direction.GetSafeNormal() : Direction;
}

FVector2D PSTelestratorLayer::MoveCursor(const FVector2D& Cursor, const FVector2D& Direction, float DeltaSeconds, const FVector2D& WidgetSize,
    const FPSTelestratorTuning& Tuning)
{
    const float Step = Tuning.CursorSpeed * ShortSide(WidgetSize) * FMath::Max(0.f, DeltaSeconds);
    const FVector2D Moved = Cursor + FVector2D(Direction.X, -Direction.Y) * Step;
    return FVector2D(FMath::Clamp(Moved.X, 0.0, FMath::Max(0.0, WidgetSize.X)), FMath::Clamp(Moved.Y, 0.0, FMath::Max(0.0, WidgetSize.Y)));
}
