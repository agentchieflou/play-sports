#include "PSAIDebugOverlay.h"
#include "PSOverlayBadgeLayout.h"
#include "PSUITeamCatalog.h"

TArray<FString> PSAIDebugOverlay::ValidateTuning(const FPSAIDebugTuning& Tuning)
{
    TArray<FString> Problems;
    if (!(Tuning.OverlayFontScale > 0.f) || Tuning.OverlayFontSize < 1)
    {
        Problems.Add(TEXT("OverlayFontScale must be above 0 and OverlayFontSize 1 or more"));
    }
    if (!(Tuning.OverlayCharWidth > 0.f) || !(Tuning.OverlayLineHeight > 0.f) || !(Tuning.OverlayTargetLineWidth > 0.f))
    {
        Problems.Add(TEXT("OverlayCharWidth, OverlayLineHeight and OverlayTargetLineWidth must be above 0"));
    }
    if (Tuning.OverlayPadding < 0.f || Tuning.OverlayNudgeStep < 0.f || Tuning.OverlayMaxNudges < 0)
    {
        Problems.Add(TEXT("OverlayPadding, OverlayNudgeStep and OverlayMaxNudges must be 0 or more"));
    }
    if (Tuning.OverlayMaxLineChars < 8)
    {
        Problems.Add(TEXT("OverlayMaxLineChars must be 8 or more"));
    }
    if (Tuning.OverlayOpacity < 0.f || Tuning.OverlayOpacity > 1.f || Tuning.OverlayCrowdedOpacity < 0.f || Tuning.OverlayCrowdedOpacity > 1.f)
    {
        Problems.Add(TEXT("OverlayOpacity and OverlayCrowdedOpacity must be between 0 and 1"));
    }
    FLinearColor Parsed;
    for (const FString* Hex : { &Tuning.OverlayOffenseColor, &Tuning.OverlayDefenseColor, &Tuning.OverlayTextColor })
    {
        if (!UPSUITeamCatalog::ParseHexColor(*Hex, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("'%s' must be #RRGGBB (OverlayOffenseColor, OverlayDefenseColor, OverlayTextColor)"), **Hex));
        }
    }
    return Problems;
}

FString PSAIDebugOverlay::WrapText(const FString& Text, int32 MaxLineChars)
{
    if (MaxLineChars <= 0)
    {
        return Text;
    }
    TArray<FString> Lines;
    Text.ParseIntoArray(Lines, TEXT("\n"), false);
    TArray<FString> Wrapped;
    for (FString Line : Lines)
    {
        while (Line.Len() > MaxLineChars)
        {
            // The last space that keeps the line within the limit; none, and the word is cut.
            int32 Break = INDEX_NONE;
            for (int32 Index = MaxLineChars; Index > 0; --Index)
            {
                if (Line[Index] == TEXT(' '))
                {
                    Break = Index;
                    break;
                }
            }
            if (Break == INDEX_NONE)
            {
                Wrapped.Add(Line.Left(MaxLineChars));
                Line = Line.Mid(MaxLineChars);
            }
            else
            {
                Wrapped.Add(Line.Left(Break));
                Line = Line.Mid(Break + 1);
            }
        }
        Wrapped.Add(Line);
    }
    return FString::Join(Wrapped, TEXT("\n"));
}

FVector2D PSAIDebugOverlay::MeasureText(const FString& Text, int32 FontSize, const FPSAIDebugTuning& Tuning)
{
    TArray<FString> Lines;
    Text.ParseIntoArray(Lines, TEXT("\n"), false);
    int32 Longest = 0;
    for (const FString& Line : Lines)
    {
        Longest = FMath::Max(Longest, Line.Len());
    }
    const float Type = static_cast<float>(FMath::Max(1, FontSize));
    return FVector2D(Longest * Tuning.OverlayCharWidth * Type + 2.f * Tuning.OverlayPadding,
        FMath::Max(1, Lines.Num()) * Tuning.OverlayLineHeight * Type + 2.f * Tuning.OverlayPadding);
}

TArray<FPSAIDebugCard> PSAIDebugOverlay::LayoutCards(const TArray<FPSAIDebugCardSource>& Sources, const FPSBadgeView& View,
    const FPSOverlayBadgeStyle& BadgeStyle, const FPSAIDebugTuning& Tuning, float PixelsPerUnit)
{
    const float Pixels = PixelsPerUnit > 0.f ? PixelsPerUnit : 1.f;
    TArray<FPSAIDebugCard> Cards;
    TArray<FPSPositionBadge> Plates;
    Cards.Reserve(Sources.Num());
    Plates.Reserve(Sources.Num());
    for (const FPSAIDebugCardSource& Source : Sources)
    {
        FPSAIDebugCard& Card = Cards.AddDefaulted_GetRef();
        Card.PlayerId = Source.PlayerId;
        Card.Text = WrapText(Source.Text, Tuning.OverlayMaxLineChars);
        Card.bOffense = Source.bOffense;

        // Placed and sized as a badge is: over the head, scaled by distance.
        const FVector Anchor = Source.Location + FVector(0.0, 0.0, Tuning.OverlayHeightCm);
        FVector2D Screen = FVector2D::ZeroVector;
        float Depth = 0.f;
        const bool bInFront = PSOverlayBadgeLayout::ProjectToScreen(View, Anchor, Screen, Depth);
        const float Distance = static_cast<float>(FVector::Dist(View.CameraLocation, Anchor));
        Card.Scale = PSOverlayBadgeLayout::ScaleForDistance(BadgeStyle, Distance);
        Card.FontSize = FMath::Max(1, FMath::RoundToInt(Tuning.OverlayFontSize * Tuning.OverlayFontScale * Card.Scale));
        Card.Size = MeasureText(Card.Text, Card.FontSize, Tuning) * Pixels;
        Card.ScreenPosition = Screen;

        FPSPositionBadge& Plate = Plates.AddDefaulted_GetRef();
        Plate.PlayerId = Source.PlayerId;
        Plate.ScreenPosition = Screen;
        Plate.Size = Card.Size;
        Plate.Scale = Card.Scale;
        Plate.Distance = Distance;
        Plate.bOnScreen = bInFront && PSOverlayBadgeLayout::IsInsideViewport(PSOverlayBadgeLayout::BadgeRect(Plate), View.ViewportSize);
        Plate.bVisible = Plate.bOnScreen;
        Card.bVisible = Plate.bVisible;

        // The line to his target, when both ends are in front of the camera.
        FVector2D PlayerScreen = FVector2D::ZeroVector;
        FVector2D TargetScreen = FVector2D::ZeroVector;
        float PlayerDepth = 0.f;
        float TargetDepth = 0.f;
        if (Source.bHasTarget && PSOverlayBadgeLayout::ProjectToScreen(View, Source.Location, PlayerScreen, PlayerDepth)
            && PSOverlayBadgeLayout::ProjectToScreen(View, Source.TargetLocation, TargetScreen, TargetDepth))
        {
            Card.bHasTarget = true;
            Card.PlayerScreen = PlayerScreen;
            Card.TargetScreen = TargetScreen;
        }
    }

    // Clear of each other as badges are, with the overlay's own nudge (its cards are bigger).
    FPSOverlayBadgeStyle Nudging = BadgeStyle;
    Nudging.NudgeStep = Tuning.OverlayNudgeStep * Pixels;
    Nudging.MaxNudges = Tuning.OverlayMaxNudges;
    PSOverlayBadgeLayout::ResolveOverlaps(Plates, nullptr, Nudging, View.ViewportSize);
    for (int32 Index = 0; Index < Cards.Num(); ++Index)
    {
        if (!Cards[Index].bVisible)
        {
            continue;
        }
        // A crowded card keeps its spot: a debug view hides nothing, it just dims it.
        Cards[Index].bCrowdedOut = Plates[Index].bCrowdedOut;
        Cards[Index].ScreenPosition = Plates[Index].ScreenPosition;
    }
    return Cards;
}
