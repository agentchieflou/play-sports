#include "PSPlayArt.h"
#include "PSRouteRunning.h"
#include "PSUITeamCatalog.h"
#include "DrawDebugHelpers.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

namespace PSPlayArtPrivate
{
    /** A ribbon of Path (already on the turf) for Entry's route, colored and sized by his read. */
    FPSPlayArtPrimitive MakeRibbon(const FPSResolvedAssignment& Entry, const TArray<FVector>& Path, const FPSPlayArtStyle& Style, float BreakMinAngleDegrees)
    {
        const int32 ReadOrder = FMath::Max(Entry.Assignment.ReadOrder, 0);
        FPSPlayArtPrimitive Ribbon;
        Ribbon.Shape = EPSPlayArtShape::Ribbon;
        Ribbon.Points = Path;
        Ribbon.BreakIndices = PSPlayArt::FindCuts(Path, BreakMinAngleDegrees);
        Ribbon.Color = PSPlayArt::ColorForRead(Style, ReadOrder);
        Ribbon.Size = Style.RibbonWidth * (ReadOrder == 1 ? Style.PrimaryWidthScale : 1.f);
        Ribbon.ReadOrder = ReadOrder;
        Ribbon.Source = Entry.Assignment.RouteId;
        Ribbon.Pawn = Entry.Pawn;
        return Ribbon;
    }

    /** The ring where Ribbon ends. */
    FPSPlayArtPrimitive MakeEndRing(const FPSPlayArtPrimitive& Ribbon, const FPSPlayArtStyle& Style)
    {
        FPSPlayArtPrimitive Ring = Ribbon;
        Ring.Shape = EPSPlayArtShape::Ring;
        Ring.Points = { Ribbon.Points.Last() };
        Ring.BreakIndices.Reset();
        Ring.FakeIndices.Reset();
        Ring.Size = Style.RingRadius;
        return Ring;
    }

    /** The five-pointed star around Center, its points Radius out, the first one upfield (+X). */
    TArray<FVector> StarOutline(const FVector& Center, float Radius)
    {
        TArray<FVector> Outline;
        for (int32 Index = 0; Index < 10; ++Index)
        {
            const float Angle = PI * Index / 5.f;
            const float Reach = (Index % 2 == 0) ? Radius : Radius * 0.4f;
            Outline.Add(Center + FVector(FMath::Cos(Angle) * Reach, FMath::Sin(Angle) * Reach, 0.f));
        }
        return Outline;
    }
}

FLinearColor PSPlayArt::ColorForRead(const FPSPlayArtStyle& Style, int32 ReadOrder)
{
    const FString* Hex = &Style.UnrankedColor;
    if (ReadOrder > 0 && Style.ReadColors.Num() > 0)
    {
        Hex = &Style.ReadColors[FMath::Min(ReadOrder, Style.ReadColors.Num()) - 1];
    }
    FLinearColor Parsed = FLinearColor::White;
    UPSUITeamCatalog::ParseHexColor(*Hex, Parsed);
    return Parsed;
}

TArray<FString> PSPlayArt::ValidateStyle(const FPSPlayArtStyle& Style)
{
    TArray<FString> Problems;
    if (Style.RibbonWidth <= 0.f || Style.PrimaryWidthScale <= 0.f)
    {
        Problems.Add(TEXT("RibbonWidth and PrimaryWidthScale must be above 0"));
    }
    if (Style.GroundOffset < 0.f || Style.BreakMarkerRadius < 0.f)
    {
        Problems.Add(TEXT("GroundOffset and BreakMarkerRadius must be 0 or more"));
    }
    if (Style.RingRadius <= 0.f)
    {
        Problems.Add(TEXT("RingRadius must be above 0"));
    }
    if (Style.ReadColors.Num() == 0)
    {
        Problems.Add(TEXT("ReadColors needs a color for the primary read at least"));
    }
    FLinearColor Parsed;
    for (int32 Index = 0; Index < Style.ReadColors.Num(); ++Index)
    {
        if (!UPSUITeamCatalog::ParseHexColor(Style.ReadColors[Index], Parsed))
        {
            Problems.Add(FString::Printf(TEXT("ReadColors[%d]: '%s' must be #RRGGBB"), Index, *Style.ReadColors[Index]));
        }
    }
    if (!UPSUITeamCatalog::ParseHexColor(Style.UnrankedColor, Parsed))
    {
        Problems.Add(FString::Printf(TEXT("UnrankedColor: '%s' must be #RRGGBB"), *Style.UnrankedColor));
    }
    if (Style.BranchOpacity < 0.f || Style.BranchOpacity > 1.f)
    {
        Problems.Add(TEXT("BranchOpacity must be between 0 and 1"));
    }
    if (Style.SnapFadeSeconds < 0.f)
    {
        Problems.Add(TEXT("SnapFadeSeconds must be 0 or more"));
    }
    for (int32 Index = 0; Index < Style.NoRouteArtCategories.Num(); ++Index)
    {
        if (Style.NoRouteArtCategories[Index].IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("NoRouteArtCategories[%d] is empty"), Index));
        }
    }
    return Problems;
}

TArray<FVector> PSPlayArt::OnTurf(const TArray<FVector>& Points, float GroundZ, const FPSPlayArtStyle& Style)
{
    TArray<FVector> Laid = Points;
    for (FVector& Point : Laid)
    {
        Point.Z = GroundZ + Style.GroundOffset;
    }
    return Laid;
}

TArray<int32> PSPlayArt::FindCuts(const TArray<FVector>& Points, float BreakMinAngleDegrees)
{
    TArray<int32> Cuts;
    for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
    {
        if (PSRouteRunning::TurnAngleDegrees(Points[Index - 1], Points[Index], Points[Index + 1]) >= BreakMinAngleDegrees)
        {
            Cuts.Add(Index);
        }
    }
    return Cuts;
}

TArray<FPSPlayArtPrimitive> PSPlayArt::CompileRouteArt(const TArray<FPSResolvedAssignment>& Resolved, const UDataTable* RouteLibrary,
    const FPSPlayArtStyle& Style, float BreakMinAngleDegrees, float GroundZ)
{
    using namespace PSPlayArtPrivate;

    TArray<FPSPlayArtPrimitive> Art;
    if (!RouteLibrary)
    {
        return Art;
    }
    for (const FPSResolvedAssignment& Entry : Resolved)
    {
        // Only a route from the library is drawn: a blocker has none, and "go to your spot" (no
        // RouteId) is a drop or a mesh point, not a pattern.
        const FName RouteId = Entry.Assignment.RouteId;
        const FPSRoute* Route = Entry.RunsRoute() && !RouteId.IsNone() ? RouteLibrary->FindRow<FPSRoute>(RouteId, TEXT("PSPlayArt"), false) : nullptr;
        if (!Route || Route->Waypoints.Num() == 0 || Route->Waypoints.Num() != Entry.Waypoints.Num())
        {
            continue;
        }

        // An option route is run to its read; a branch takes it on from there (Epic 68).
        const int32 ReadIndex = Route->Waypoints.IsValidIndex(Route->OptionReadWaypoint) ? Route->OptionReadWaypoint : INDEX_NONE;
        const int32 LastIndex = ReadIndex != INDEX_NONE ? ReadIndex : Route->Waypoints.Num() - 1;

        // From his feet, through what the AI is handed.
        TArray<FVector> Path;
        Path.Add(Entry.PawnLocation);
        for (int32 Index = 0; Index <= LastIndex; ++Index)
        {
            Path.Add(Entry.Waypoints[Index]);
        }
        FPSPlayArtPrimitive Ribbon = MakeRibbon(Entry, OnTurf(Path, GroundZ, Style), Style, BreakMinAngleDegrees);
        for (int32 Index = 0; Index <= LastIndex; ++Index)
        {
            if (Route->Waypoints[Index].bFake)
            {
                // A double move's fake is sold, not run: marked as a fake, not a cut.
                Ribbon.FakeIndices.Add(Index + 1);
                Ribbon.BreakIndices.Remove(Index + 1);
            }
        }
        if (ReadIndex != INDEX_NONE)
        {
            // The read is where the route splits.
            Ribbon.BreakIndices.AddUnique(Ribbon.Points.Num() - 1);
        }
        Art.Add(Ribbon);

        int32 BranchCount = 0;
        if (ReadIndex != INDEX_NONE)
        {
            const FVector ReadPoint = Entry.Waypoints[ReadIndex];
            for (const FName BranchId : { Route->VsManBranch, Route->VsZoneBranch })
            {
                const FPSRoute* Branch = BranchId.IsNone() ? nullptr : RouteLibrary->FindRow<FPSRoute>(BranchId, TEXT("PSPlayArt"), false);
                if (!Branch || Branch->Waypoints.Num() == 0)
                {
                    continue;
                }
                // Placed at the read the way the route runner places it, as authored (outside).
                TArray<FVector> BranchPath;
                BranchPath.Add(ReadPoint);
                BranchPath.Append(PSPlayResolution::PlaceRoute(*Branch, ReadPoint, Entry.Mirror));
                FPSPlayArtPrimitive BranchRibbon = MakeRibbon(Entry, OnTurf(BranchPath, GroundZ, Style), Style, BreakMinAngleDegrees);
                BranchRibbon.Source = BranchId;
                BranchRibbon.bBranch = true;
                BranchRibbon.Opacity = Style.BranchOpacity;
                Art.Add(BranchRibbon);
                Art.Add(MakeEndRing(BranchRibbon, Style));
                ++BranchCount;
            }
        }
        if (BranchCount == 0)
        {
            // A plain route ends in a ring; so does an option route with no branch to run, at its read.
            Art.Add(MakeEndRing(Ribbon, Style));
        }
    }
    return Art;
}

void PSPlayArt::DrawDebug(const UWorld* World, const TArray<FPSPlayArtPrimitive>& Primitives, const FPSPlayArtStyle& Style, float Opacity)
{
#if ENABLE_DRAW_DEBUG
    if (!World || Opacity <= 0.f)
    {
        return;
    }
    // A circle in the plane of these two axes lies flat on the turf.
    const FVector FlatY(0.f, 1.f, 0.f);
    const FVector FlatZ(1.f, 0.f, 0.f);
    for (const FPSPlayArtPrimitive& Art : Primitives)
    {
        FLinearColor Tint = Art.Color;
        Tint.A = FMath::Clamp(Art.Opacity * Opacity, 0.f, 1.f);
        const FColor Color = Tint.ToFColor(true);
        const TArray<FVector>& Points = Art.Points;
        if (Points.Num() == 0)
        {
            continue;
        }
        switch (Art.Shape)
        {
        case EPSPlayArtShape::Ribbon:
            for (int32 Index = 1; Index < Points.Num(); ++Index)
            {
                DrawDebugLine(World, Points[Index - 1], Points[Index], Color, false, -1.f, 0, Art.Size);
            }
            for (const int32 Cut : Art.BreakIndices)
            {
                if (Points.IsValidIndex(Cut))
                {
                    DrawDebugCircle(World, Points[Cut], Style.BreakMarkerRadius, 12, Color, false, -1.f, 0, 3.f, FlatY, FlatZ, false);
                }
            }
            for (const int32 Fake : Art.FakeIndices)
            {
                if (Points.IsValidIndex(Fake))
                {
                    DrawDebugCircle(World, Points[Fake], Style.BreakMarkerRadius * 0.5f, 8, Color, false, -1.f, 0, 2.f, FlatY, FlatZ, false);
                }
            }
            break;
        case EPSPlayArtShape::Ring:
            DrawDebugCircle(World, Points[0], Art.Size, 24, Color, false, -1.f, 0, 4.f, FlatY, FlatZ, false);
            break;
        case EPSPlayArtShape::Star:
        {
            const TArray<FVector> Outline = PSPlayArtPrivate::StarOutline(Points[0], Art.Size);
            for (int32 Index = 0; Index < Outline.Num(); ++Index)
            {
                DrawDebugLine(World, Outline[Index], Outline[(Index + 1) % Outline.Num()], Color, false, -1.f, 0, 3.f);
            }
            break;
        }
        case EPSPlayArtShape::Connector:
            if (Points.Num() > 1)
            {
                DrawDebugLine(World, Points[0], Points[1], Color, false, -1.f, 0, Art.Size);
            }
            break;
        case EPSPlayArtShape::Arrow:
            if (Points.Num() > 1)
            {
                DrawDebugDirectionalArrow(World, Points[0], Points[1], FMath::Max(Art.Size * 4.f, 20.f), Color, false, -1.f, 0, Art.Size);
            }
            break;
        default:
            break;
        }
    }
#endif
}
