#include "PSPlayArt.h"
#include "PSCoverageMatchupSubsystem.h"
#include "PSPlayerPawn.h"
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
        PSPlayArt::ApplyAnnotation(Ribbon, Entry.Assignment.Art, Style);
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
        Ring.Size = Style.RingRadius * (Ribbon.bEmphasized ? Style.EmphasisScale : 1.f);
        return Ring;
    }

    FLinearColor ParseColor(const FString& Hex)
    {
        FLinearColor Parsed = FLinearColor::White;
        UPSUITeamCatalog::ParseHexColor(Hex, Parsed);
        return Parsed;
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
    if (Style.RingRadius <= 0.f || Style.EmphasisScale <= 0.f)
    {
        Problems.Add(TEXT("RingRadius and EmphasisScale must be above 0"));
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
    if (Style.ZoneStarRadius <= 0.f || Style.ManLineWidth <= 0.f || Style.RushArrowWidth <= 0.f)
    {
        Problems.Add(TEXT("ZoneStarRadius, ManLineWidth and RushArrowWidth must be above 0"));
    }
    if (Style.RushArrowDepth < 0.f)
    {
        Problems.Add(TEXT("RushArrowDepth must be 0 or more"));
    }
    struct FNamedColor
    {
        const TCHAR* Field;
        const FString* Hex;
    };
    const FNamedColor DefenseColors[] = {
        { TEXT("ZoneStarColor"), &Style.ZoneStarColor }, { TEXT("ManLineColor"), &Style.ManLineColor },
        { TEXT("BlitzArrowColor"), &Style.BlitzArrowColor }, { TEXT("RushArrowColor"), &Style.RushArrowColor } };
    for (const FNamedColor& Named : DefenseColors)
    {
        if (!UPSUITeamCatalog::ParseHexColor(*Named.Hex, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("%s: '%s' must be #RRGGBB"), Named.Field, **Named.Hex));
        }
    }
    for (int32 Index = 0; Index < Style.NoDefenseArtCategories.Num(); ++Index)
    {
        if (Style.NoDefenseArtCategories[Index].IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("NoDefenseArtCategories[%d] is empty"), Index));
        }
    }
    return Problems;
}

bool PSPlayArt::IsValidBadgeLetter(const FString& Letter)
{
    if (Letter.Len() < 1 || Letter.Len() > 2)
    {
        return false;
    }
    for (const TCHAR Character : Letter)
    {
        if (!((Character >= TEXT('A') && Character <= TEXT('Z')) || (Character >= TEXT('0') && Character <= TEXT('9'))))
        {
            return false;
        }
    }
    return true;
}

TArray<FString> PSPlayArt::ValidateAnnotation(const FPSPlayArtAnnotation& Annotation)
{
    TArray<FString> Problems;
    FLinearColor Parsed;
    if (!Annotation.Color.IsEmpty() && !UPSUITeamCatalog::ParseHexColor(Annotation.Color, Parsed))
    {
        Problems.Add(FString::Printf(TEXT("Art.Color '%s' must be #RRGGBB or empty"), *Annotation.Color));
    }
    if (!Annotation.BadgeLetter.IsEmpty() && !IsValidBadgeLetter(Annotation.BadgeLetter))
    {
        Problems.Add(FString::Printf(TEXT("Art.BadgeLetter '%s' must be one or two capitals or digits"), *Annotation.BadgeLetter));
    }
    return Problems;
}

void PSPlayArt::ApplyAnnotation(FPSPlayArtPrimitive& Primitive, const FPSPlayArtAnnotation& Annotation, const FPSPlayArtStyle& Style)
{
    FLinearColor Parsed;
    if (!Annotation.Color.IsEmpty() && UPSUITeamCatalog::ParseHexColor(Annotation.Color, Parsed))
    {
        Primitive.Color = Parsed;
    }
    if (Annotation.bEmphasis)
    {
        Primitive.bEmphasized = true;
        Primitive.Size *= Style.EmphasisScale;
    }
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

TArray<FPSPlayArtPrimitive> PSPlayArt::CompileDefenseArt(const TArray<FPSResolvedAssignment>& Resolved, const FPSPlayArtStyle& Style,
    const FVector& LineOfScrimmage, const UPSCoverageMatchupSubsystem* Matchups)
{
    using namespace PSPlayArtPrivate;

    TArray<FPSPlayArtPrimitive> Art;
    const float GroundZ = LineOfScrimmage.Z;
    for (const FPSResolvedAssignment& Entry : Resolved)
    {
        const APSPlayerPawn* Defender = Entry.Pawn.Get();
        if (!Defender || !Entry.bHasSlot)
        {
            continue;
        }
        FPSPlayArtPrimitive Icon;
        Icon.Pawn = Entry.Pawn;
        const APSPlayerPawn* Receiver = Entry.ManReceiver.Get();
        if (Entry.DefensiveType == EPSDefensiveAssignmentType::ZoneCoverage
            || (Entry.DefensiveType == EPSDefensiveAssignmentType::ManCoverage && !Receiver))
        {
            // A zone's landmark; a man defender with nobody left to cover plays his spot.
            Icon.Shape = EPSPlayArtShape::Star;
            Icon.Points = OnTurf({ Entry.DefensiveType == EPSDefensiveAssignmentType::ZoneCoverage ? Entry.GetZoneLandmark() : Entry.PawnLocation }, GroundZ, Style);
            Icon.Size = Style.ZoneStarRadius;
            Icon.Color = ParseColor(Style.ZoneStarColor);
            Icon.Source = TEXT("Zone");
        }
        else if (Entry.DefensiveType == EPSDefensiveAssignmentType::ManCoverage)
        {
            // Defender to his man, named by how he got him.
            const AActor* Named = Entry.CoverageTarget.Get();
            const APSPlayerPawn* Pressed = Matchups ? Matchups->GetPlannedReceiver(Defender) : nullptr;
            Icon.Shape = EPSPlayArtShape::Connector;
            Icon.Points = OnTurf({ Entry.PawnLocation, Receiver->GetActorLocation() }, GroundZ, Style);
            Icon.Size = Style.ManLineWidth;
            Icon.Color = ParseColor(Style.ManLineColor);
            Icon.Source = Named == Receiver ? FName(TEXT("Shadow")) : (Pressed == Receiver ? FName(TEXT("Press")) : FName(TEXT("Man")));
            Icon.Target = Entry.ManReceiver;
        }
        else if (Entry.DefensiveType == EPSDefensiveAssignmentType::PassRush)
        {
            // Downhill from his spot, through the line into the backfield.
            const bool bBlitz = Entry.Assignment.Kind == EPSAssignmentKind::Blitz;
            const FVector Through(LineOfScrimmage.X - Style.RushArrowDepth, Entry.PawnLocation.Y, GroundZ);
            Icon.Shape = EPSPlayArtShape::Arrow;
            Icon.Points = OnTurf({ Entry.PawnLocation, Through }, GroundZ, Style);
            Icon.Size = Style.RushArrowWidth;
            Icon.Color = ParseColor(bBlitz ? Style.BlitzArrowColor : Style.RushArrowColor);
            Icon.Source = bBlitz ? FName(TEXT("Blitz")) : FName(TEXT("Rush"));
        }
        else
        {
            // A run fit reads the play: nothing to draw before it.
            continue;
        }
        ApplyAnnotation(Icon, Entry.Assignment.Art, Style);
        Art.Add(Icon);
    }
    return Art;
}

bool PSPlayArt::DrawsNoArt(const FPSPlayDefinition& Play, const FPSPlayArtStyle& Style)
{
    return (Play.bIsOffensivePlay ? Style.NoRouteArtCategories : Style.NoDefenseArtCategories).Contains(Play.PlayCategory);
}

TArray<FPSPlayArtPrimitive> PSPlayArt::CompilePlayArt(const FPSPlayDefinition& Play, const TArray<FPSResolvedAssignment>& Resolved, const UDataTable* RouteLibrary,
    const FPSPlayArtStyle& Style, float BreakMinAngleDegrees, const FVector& LineOfScrimmage, const UPSCoverageMatchupSubsystem* Matchups)
{
    if (DrawsNoArt(Play, Style))
    {
        return TArray<FPSPlayArtPrimitive>();
    }
    return Play.bIsOffensivePlay
        ? CompileRouteArt(Resolved, RouteLibrary, Style, BreakMinAngleDegrees, LineOfScrimmage.Z)
        : CompileDefenseArt(Resolved, Style, LineOfScrimmage, Matchups);
}

TArray<FString> PSPlayArt::ValidatePlayArt(const FPSPlayDefinition& Play, const TArray<FPSResolvedAssignment>& Resolved, const TArray<FPSPlayArtPrimitive>& Art,
    const UDataTable* RouteLibrary, const FPSPlayArtStyle& Style)
{
    TArray<FString> Problems;
    const FString PlayName = Play.PlayId.ToString();
    for (const FPSPlayAssignment& Slot : Play.Assignments)
    {
        for (const FString& Problem : ValidateAnnotation(Slot.Art))
        {
            Problems.Add(FString::Printf(TEXT("%s: %s slot: %s"), *PlayName, *UEnum::GetValueAsString(Slot.Role), *Problem));
        }
    }
    if (DrawsNoArt(Play, Style))
    {
        if (Art.Num() > 0)
        {
            Problems.Add(FString::Printf(TEXT("%s: a %s play draws no art, but %d pieces were drawn"), *PlayName, *Play.PlayCategory, Art.Num()));
        }
        return Problems;
    }

    TSet<const APSPlayerPawn*> ResolvedPawns;
    for (const FPSResolvedAssignment& Entry : Resolved)
    {
        const APSPlayerPawn* Player = Entry.Pawn.Get();
        if (!Player)
        {
            continue;
        }
        ResolvedPawns.Add(Player);
        const FString Who = FString::Printf(TEXT("%s: %s (%s)"), *PlayName, *Player->GetAttributes().DisplayName, *UEnum::GetValueAsString(Player->GetAttributes().Role));
        TArray<const FPSPlayArtPrimitive*> Mine;
        for (const FPSPlayArtPrimitive& Piece : Art)
        {
            if (Piece.Pawn.Get() == Player)
            {
                Mine.Add(&Piece);
            }
        }
        auto CountOf = [&Mine](EPSPlayArtShape Shape, bool bBranch)
        {
            int32 Count = 0;
            for (const FPSPlayArtPrimitive* Piece : Mine)
            {
                Count += (Piece->Shape == Shape && Piece->bBranch == bBranch) ? 1 : 0;
            }
            return Count;
        };
        auto FindShape = [&Mine](EPSPlayArtShape Shape) -> const FPSPlayArtPrimitive*
        {
            for (const FPSPlayArtPrimitive* Piece : Mine)
            {
                if (Piece->Shape == Shape && !Piece->bBranch)
                {
                    return Piece;
                }
            }
            return nullptr;
        };

        if (Play.bIsOffensivePlay)
        {
            // A route from the library is drawn as the waypoints he is handed; nothing else is.
            const FName RouteId = Entry.Assignment.RouteId;
            const FPSRoute* Route = Entry.RunsRoute() && !RouteId.IsNone() && RouteLibrary ? RouteLibrary->FindRow<FPSRoute>(RouteId, TEXT("PSPlayArt"), false) : nullptr;
            if (Entry.RunsRoute() && !RouteId.IsNone() && !Route)
            {
                Problems.Add(FString::Printf(TEXT("%s runs %s, which no route library has: nothing to run or draw"), *Who, *RouteId.ToString()));
            }
            const bool bDrawn = Route && Route->Waypoints.Num() > 0 && Route->Waypoints.Num() == Entry.Waypoints.Num();
            if (!bDrawn)
            {
                if (Mine.Num() > 0)
                {
                    Problems.Add(FString::Printf(TEXT("%s has no route to draw, but has art"), *Who));
                }
                if (Entry.Assignment.ReadOrder > 0)
                {
                    Problems.Add(FString::Printf(TEXT("%s is read %d, but runs no route to draw"), *Who, Entry.Assignment.ReadOrder));
                }
                continue;
            }
            const FPSPlayArtPrimitive* Ribbon = FindShape(EPSPlayArtShape::Ribbon);
            const int32 ReadIndex = Route->Waypoints.IsValidIndex(Route->OptionReadWaypoint) ? Route->OptionReadWaypoint : Route->Waypoints.Num() - 1;
            bool bMatches = Ribbon && CountOf(EPSPlayArtShape::Ribbon, false) == 1 && Ribbon->Points.Num() == ReadIndex + 2;
            for (int32 Index = 0; bMatches && Index <= ReadIndex; ++Index)
            {
                bMatches = FVector::Dist2D(Ribbon->Points[Index + 1], Entry.Waypoints[Index]) < 1.f;
            }
            if (!bMatches)
            {
                Problems.Add(FString::Printf(TEXT("%s: his ribbon isn't the %s he is handed"), *Who, *RouteId.ToString()));
            }
            if (CountOf(EPSPlayArtShape::Ring, false) + CountOf(EPSPlayArtShape::Ribbon, true) == 0)
            {
                Problems.Add(FString::Printf(TEXT("%s: his route has no end (a ring or an option's branches)"), *Who));
            }
            if (Ribbon && Ribbon->ReadOrder != FMath::Max(Entry.Assignment.ReadOrder, 0))
            {
                Problems.Add(FString::Printf(TEXT("%s: drawn as read %d, but the play reads him %d"), *Who, Ribbon->ReadOrder, Entry.Assignment.ReadOrder));
            }
            continue;
        }

        // The defense: each job its icon.
        const APSPlayerPawn* Receiver = Entry.ManReceiver.Get();
        const bool bZone = Entry.DefensiveType == EPSDefensiveAssignmentType::ZoneCoverage
            || (Entry.DefensiveType == EPSDefensiveAssignmentType::ManCoverage && !Receiver);
        if (!Entry.bHasSlot)
        {
            if (Mine.Num() > 0)
            {
                Problems.Add(FString::Printf(TEXT("%s has no job, but has art"), *Who));
            }
        }
        else if (bZone)
        {
            const FPSPlayArtPrimitive* Star = FindShape(EPSPlayArtShape::Star);
            const FVector Spot = Entry.DefensiveType == EPSDefensiveAssignmentType::ZoneCoverage ? Entry.GetZoneLandmark() : Entry.PawnLocation;
            if (!Star || Mine.Num() != 1 || FVector::Dist2D(Star->Points[0], Spot) >= 1.f)
            {
                Problems.Add(FString::Printf(TEXT("%s: his zone isn't one star at the spot he plays"), *Who));
            }
        }
        else if (Entry.DefensiveType == EPSDefensiveAssignmentType::ManCoverage)
        {
            const FPSPlayArtPrimitive* Line = FindShape(EPSPlayArtShape::Connector);
            if (!Line || Mine.Num() != 1 || Line->Target.Get() != Receiver)
            {
                Problems.Add(FString::Printf(TEXT("%s: not one line to the receiver he covers"), *Who));
            }
        }
        else if (Entry.DefensiveType == EPSDefensiveAssignmentType::PassRush)
        {
            const FPSPlayArtPrimitive* Arrow = FindShape(EPSPlayArtShape::Arrow);
            if (!Arrow || Mine.Num() != 1 || FVector::Dist2D(Arrow->Points[0], Entry.PawnLocation) >= 1.f)
            {
                Problems.Add(FString::Printf(TEXT("%s: his rush isn't one arrow from his spot"), *Who));
            }
        }
        else if (Mine.Num() > 0)
        {
            Problems.Add(FString::Printf(TEXT("%s fits the run, but has art"), *Who));
        }
    }
    for (const FPSPlayArtPrimitive& Piece : Art)
    {
        if (!ResolvedPawns.Contains(Piece.Pawn.Get()))
        {
            Problems.Add(FString::Printf(TEXT("%s: art (%s) for nobody in the play"), *PlayName, *Piece.Source.ToString()));
        }
    }
    return Problems;
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
