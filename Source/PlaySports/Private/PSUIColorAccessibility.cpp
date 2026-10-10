#include "PSUIColorAccessibility.h"

namespace PSUIColorMath
{
    /** A 3x3 matrix applied to a color's linear RGB. */
    struct FColorMatrix
    {
        float M[3][3];

        FLinearColor Apply(const FLinearColor& Color) const
        {
            return FLinearColor(
                M[0][0] * Color.R + M[0][1] * Color.G + M[0][2] * Color.B,
                M[1][0] * Color.R + M[1][1] * Color.G + M[1][2] * Color.B,
                M[2][0] * Color.R + M[2][1] * Color.G + M[2][2] * Color.B,
                Color.A);
        }
    };

    // Machado, Oliveira and Fernandes (2009), severity 1.0, for linear RGB.
    static const FColorMatrix Protanopia = { { { 0.152286f, 1.052583f, -0.204868f }, { 0.114503f, 0.786281f, 0.099216f }, { -0.003882f, -0.048116f, 1.051998f } } };
    static const FColorMatrix Deuteranopia = { { { 0.367322f, 0.860646f, -0.227968f }, { 0.280085f, 0.672501f, 0.047413f }, { -0.011820f, 0.042940f, 0.968881f } } };
    static const FColorMatrix Tritanopia = { { { 1.255528f, -0.076749f, -0.178779f }, { -0.078411f, 0.930809f, 0.147602f }, { 0.004733f, 0.691367f, 0.303900f } } };

    // Where the lost detail goes (Fidaner, Lin and Ozguven): into green and blue for a red or
    // green deficiency, into red and green for a blue one.
    static const FColorMatrix RedGreenShift = { { { 0.f, 0.f, 0.f }, { 0.7f, 1.f, 0.f }, { 0.7f, 0.f, 1.f } } };
    static const FColorMatrix BlueShift = { { { 1.f, 0.f, 0.7f }, { 0.f, 1.f, 0.7f }, { 0.f, 0.f, 0.f } } };

    static const FColorMatrix* SimulationFor(EPSColorblindMode Mode)
    {
        switch (Mode)
        {
        case EPSColorblindMode::Protanopia:
            return &Protanopia;
        case EPSColorblindMode::Deuteranopia:
            return &Deuteranopia;
        case EPSColorblindMode::Tritanopia:
            return &Tritanopia;
        default:
            return nullptr;
        }
    }

    static FLinearColor Clamp01(const FLinearColor& Color)
    {
        return FLinearColor(FMath::Clamp(Color.R, 0.f, 1.f), FMath::Clamp(Color.G, 0.f, 1.f), FMath::Clamp(Color.B, 0.f, 1.f), Color.A);
    }

    /** Linear sRGB to CIELAB (D65). */
    static FVector ToLab(const FLinearColor& Color)
    {
        const float X = (0.4124f * Color.R + 0.3576f * Color.G + 0.1805f * Color.B) / 0.95047f;
        const float Y = (0.2126f * Color.R + 0.7152f * Color.G + 0.0722f * Color.B) / 1.f;
        const float Z = (0.0193f * Color.R + 0.1192f * Color.G + 0.9505f * Color.B) / 1.08883f;
        auto F = [](float T)
        {
            return T > 0.008856f ? FMath::Pow(T, 1.f / 3.f) : 7.787f * T + 16.f / 116.f;
        };
        const float FX = F(X);
        const float FY = F(Y);
        const float FZ = F(Z);
        return FVector(116.f * FY - 16.f, 500.f * (FX - FY), 200.f * (FY - FZ));
    }
}

FLinearColor UPSUIColorLibrary::Simulate(const FLinearColor& Color, EPSColorblindMode Mode)
{
    const PSUIColorMath::FColorMatrix* Simulation = PSUIColorMath::SimulationFor(Mode);
    return Simulation ? PSUIColorMath::Clamp01(Simulation->Apply(Color)) : Color;
}

FLinearColor UPSUIColorLibrary::ResolveColor(const FLinearColor& Color, EPSColorblindMode Mode)
{
    const PSUIColorMath::FColorMatrix* Simulation = PSUIColorMath::SimulationFor(Mode);
    if (!Simulation)
    {
        return Color;
    }
    const FLinearColor Seen = Simulation->Apply(Color);
    const FLinearColor Lost(Color.R - Seen.R, Color.G - Seen.G, Color.B - Seen.B, 0.f);
    const PSUIColorMath::FColorMatrix& Shift = Mode == EPSColorblindMode::Tritanopia ? PSUIColorMath::BlueShift : PSUIColorMath::RedGreenShift;
    const FLinearColor Moved = Shift.Apply(Lost);
    return PSUIColorMath::Clamp01(FLinearColor(Color.R + Moved.R, Color.G + Moved.G, Color.B + Moved.B, Color.A));
}

float UPSUIColorLibrary::PerceivedDistance(const FLinearColor& A, const FLinearColor& B, EPSColorblindMode Mode)
{
    return float(FVector::Dist(PSUIColorMath::ToLab(Simulate(A, Mode)), PSUIColorMath::ToLab(Simulate(B, Mode))));
}

void UPSUIColorLibrary::ResolveMatchupColors(const FLinearColor& HomePrimary, const FLinearColor& HomeSecondary,
    const FLinearColor& AwayPrimary, const FLinearColor& AwaySecondary, EPSColorblindMode Mode, float MinDistance,
    FLinearColor& OutHome, FLinearColor& OutAway)
{
    // Candidates in order of preference: the primaries, then the away side's change, then the
    // home side's, then both.
    const FLinearColor Homes[] = { HomePrimary, HomePrimary, HomeSecondary, HomeSecondary };
    const FLinearColor Aways[] = { AwayPrimary, AwaySecondary, AwayPrimary, AwaySecondary };
    int32 Best = 0;
    float BestDistance = -1.f;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const float Distance = PerceivedDistance(ResolveColor(Homes[Index], Mode), ResolveColor(Aways[Index], Mode), Mode);
        if (Distance >= MinDistance)
        {
            Best = Index;
            break;
        }
        if (Distance > BestDistance)
        {
            BestDistance = Distance;
            Best = Index;
        }
    }
    OutHome = ResolveColor(Homes[Best], Mode);
    OutAway = ResolveColor(Aways[Best], Mode);
}
