// PSTouchHud.cpp - Epic 146.4: what the touch HUD draws, pure
#include "PSTouchHudTypes.h"
#include "PSDataIngestion.h"
#include "PSUITeamCatalog.h"
#include "Misc/Paths.h"

namespace PSTouchHudPrivate
{
    /** The arrowhead's barbs, each this far off the arrow's line. */
    constexpr float ArrowHeadHalfAngleDegrees = 30.f;

    FLinearColor ColorAt(const FString& Hex, float Opacity)
    {
        FLinearColor Color = FLinearColor::White;
        UPSUITeamCatalog::ParseHexColor(Hex, Color);
        Color.A = FMath::Clamp(Opacity, 0.f, 1.f);
        return Color;
    }

    /** Up is toward the top of the screen, where Y is 0. */
    FVector2D SwipeVector(EPSSwipeDirection Direction)
    {
        switch (Direction)
        {
        case EPSSwipeDirection::Left:
            return FVector2D(-1.0, 0.0);
        case EPSSwipeDirection::Right:
            return FVector2D(1.0, 0.0);
        case EPSSwipeDirection::Up:
            return FVector2D(0.0, -1.0);
        case EPSSwipeDirection::Down:
            return FVector2D(0.0, 1.0);
        default:
            return FVector2D::ZeroVector;
        }
    }

    /** A ring whose outer edge is Radius: the line runs down the middle of its Width. */
    FPSWidgetStroke Ring(const FVector2D& Center, float Radius, float Width, int32 Segments, const FLinearColor& Color, FName Tag)
    {
        return PSWidgetDrawing::MakeStroke(PSWidgetDrawing::Circle(Center, FMath::Max(Radius - Width * 0.5f, 0.f), Segments),
            Color, Width, Tag, 0, true);
    }
}

FString PSTouchHud::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/touch_hud.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

FPSTouchHudStyle PSTouchHud::LoadStyle(const FString& Path)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSTouchHudStyle Loaded;
    if (!Ingestion->LoadTouchHudStyleFromJson(Path, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("PSTouchHud: Could not load %s; using the default look."), *Path);
        return FPSTouchHudStyle();
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("PSTouchHud: %s: %s"), *Path, *Problem);
    }
    return Problems.Num() == 0 ? Loaded : FPSTouchHudStyle();
}

TArray<FString> PSTouchHud::ValidateStyle(const FPSTouchHudStyle& Style)
{
    TArray<FString> Problems;
    for (const TPair<const TCHAR*, float>& Share : {
        TPair<const TCHAR*, float>(TEXT("RestOpacity"), Style.RestOpacity),
        TPair<const TCHAR*, float>(TEXT("PressedOpacity"), Style.PressedOpacity),
        TPair<const TCHAR*, float>(TEXT("StickIdleFade"), Style.StickIdleFade) })
    {
        if (!(Share.Value >= 0.f && Share.Value <= 1.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be from 0 to 1"), Share.Key));
        }
    }
    for (const TPair<const TCHAR*, float>& Fraction : {
        TPair<const TCHAR*, float>(TEXT("RingWidth"), Style.RingWidth),
        TPair<const TCHAR*, float>(TEXT("KnobRadius"), Style.KnobRadius),
        TPair<const TCHAR*, float>(TEXT("SwipeArrowWidth"), Style.SwipeArrowWidth),
        TPair<const TCHAR*, float>(TEXT("SwipeArrowHead"), Style.SwipeArrowHead) })
    {
        if (!(Fraction.Value > 0.f && Fraction.Value <= 1.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be above 0 and at most 1"), Fraction.Key));
        }
    }
    for (const TPair<const TCHAR*, float>& Positive : {
        TPair<const TCHAR*, float>(TEXT("LabelSize"), Style.LabelSize),
        TPair<const TCHAR*, float>(TEXT("SwipeArrowLength"), Style.SwipeArrowLength) })
    {
        if (!(Positive.Value > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be above 0"), Positive.Key));
        }
    }
    if (!(Style.SwipeFlashSeconds >= 0.f))
    {
        Problems.Add(TEXT("SwipeFlashSeconds must be 0 or more"));
    }
    if (Style.CircleSegments < 3)
    {
        Problems.Add(TEXT("CircleSegments must be 3 or more"));
    }
    for (const TPair<const TCHAR*, const FString*>& Color : {
        TPair<const TCHAR*, const FString*>(TEXT("ControlColor"), &Style.ControlColor),
        TPair<const TCHAR*, const FString*>(TEXT("PressedColor"), &Style.PressedColor),
        TPair<const TCHAR*, const FString*>(TEXT("LabelColor"), &Style.LabelColor) })
    {
        FLinearColor Parsed;
        if (!UPSUITeamCatalog::ParseHexColor(*Color.Value, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("%s '%s' is not #RRGGBB"), Color.Key, **Color.Value));
        }
    }
    return Problems;
}

FPSTouchHudDrawing PSTouchHud::BuildDrawing(const TArray<FPSTouchControlView>& Controls, const FPSTouchSwipeView& Swipe,
    double NowSeconds, float PixelsToLocal, float ScreenHeight, bool bReducedMotion, const FPSTouchHudStyle& Style)
{
    using namespace PSTouchHudPrivate;

    FPSTouchHudDrawing Drawing;
    const int32 Segments = FMath::Max(Style.CircleSegments, 3);
    for (const FPSTouchControlView& Control : Controls)
    {
        const FVector2D Center = Control.Center * PixelsToLocal;
        const float Radius = Control.Radius * PixelsToLocal;
        const float RingThickness = Style.RingWidth * Radius;
        if (Control.Kind == EPSTouchControlKind::Button)
        {
            const FLinearColor Look = Control.bHeld ? ColorAt(Style.PressedColor, Style.PressedOpacity) : ColorAt(Style.ControlColor, Style.RestOpacity);
            Drawing.Strokes.Add(Ring(Center, Radius, RingThickness, Segments, Look, TEXT("Button")));

            FPSTouchHudLabel& Label = Drawing.Labels.AddDefaulted_GetRef();
            Label.ControlId = Control.ControlId;
            Label.ActionId = Control.ActionId;
            Label.ContextId = Control.ContextId;
            Label.Center = Center;
            Label.FontSize = Style.LabelSize * Radius;
            Label.Color = ColorAt(Style.LabelColor, Control.bHeld ? Style.PressedOpacity : Style.RestOpacity);
        }
        else if (Control.Kind == EPSTouchControlKind::Stick)
        {
            if (!Control.bHeld)
            {
                Drawing.Strokes.Add(Ring(Center, Radius, RingThickness, Segments,
                    ColorAt(Style.ControlColor, Style.RestOpacity * Style.StickIdleFade), TEXT("Stick")));
                continue;
            }
            // Held: the base where the finger landed, the knob at its deflection, kept on the rim.
            const FVector2D Base = Control.TouchOrigin * PixelsToLocal;
            FVector2D Deflection = (Control.TouchCurrent - Control.TouchOrigin) * PixelsToLocal;
            if (Deflection.Size() > Radius)
            {
                Deflection = Deflection.GetSafeNormal() * Radius;
            }
            Drawing.Strokes.Add(Ring(Base, Radius, RingThickness, Segments, ColorAt(Style.ControlColor, Style.PressedOpacity), TEXT("Stick")));
            // A filled knob: a ring as thick as the knob's radius.
            const float Knob = Style.KnobRadius * Radius;
            Drawing.Strokes.Add(Ring(Base + Deflection, Knob, Knob, Segments, ColorAt(Style.PressedColor, Style.PressedOpacity), TEXT("Knob")));
        }
    }

    // The last swipe's arrow, briefly.
    const FVector2D Way = SwipeVector(Swipe.Direction);
    const double Age = NowSeconds - Swipe.TimeSeconds;
    if (!Way.IsZero() && Style.SwipeFlashSeconds > 0.f && Age >= 0.0 && Age <= Style.SwipeFlashSeconds)
    {
        const float Fade = bReducedMotion ? 1.f : 1.f - static_cast<float>(Age / Style.SwipeFlashSeconds);
        const FLinearColor Look = ColorAt(Style.ControlColor, Style.PressedOpacity * Fade);
        const float Length = Style.SwipeArrowLength * ScreenHeight;
        const FVector2D Middle = Swipe.Position * PixelsToLocal;
        const FVector2D From = Middle - Way * (Length * 0.5f);
        const FVector2D Tip = Middle + Way * (Length * 0.5f);
        const float Thickness = Style.SwipeArrowWidth * Length;
        Drawing.Strokes.Add(PSWidgetDrawing::MakeStroke({ From, Tip }, Look, Thickness, TEXT("Swipe")));
        Drawing.Strokes.Add(PSWidgetDrawing::MakeStroke(PSWidgetDrawing::ArrowHead(From, Tip, Style.SwipeArrowHead * Length, ArrowHeadHalfAngleDegrees),
            Look, Thickness, TEXT("Swipe")));
    }
    return Drawing;
}
