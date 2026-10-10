#include "PSDefenderGapSubsystem.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlaybookData.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

namespace PSDefenderGapsPrivate
{
    /** Slots in a gap-spot array: one per EPSRunGap, None included. */
    constexpr int32 GapSlots = static_cast<int32>(EPSRunGap::DRight) + 1;

    /** A defender with a run responsibility on this call: rushing or in a run fit, not in
     *  coverage. A defender a person controls plays his front's gap. */
    bool HasRunResponsibility(const APSPlayerPawn* Pawn)
    {
        const APSDefenseController* Controller = Cast<APSDefenseController>(Pawn->GetController());
        if (!Controller)
        {
            return true;
        }
        const EPSDefensiveAssignmentType Assignment = Controller->GetAssignment();
        return Assignment == EPSDefensiveAssignmentType::PassRush || Assignment == EPSDefensiveAssignmentType::Contain
            || Assignment == EPSDefensiveAssignmentType::RunFit;
    }

    FString GapName(EPSRunGap Gap)
    {
        return StaticEnum<EPSRunGap>()->GetNameStringByValue(static_cast<int64>(Gap));
    }
}

int32 PSDefenderGaps::GetGapLevel(EPSRunGap Gap)
{
    switch (Gap)
    {
    case EPSRunGap::ALeft:
    case EPSRunGap::ARight:
        return 0;
    case EPSRunGap::BLeft:
    case EPSRunGap::BRight:
        return 1;
    case EPSRunGap::CLeft:
    case EPSRunGap::CRight:
        return 2;
    case EPSRunGap::DLeft:
    case EPSRunGap::DRight:
        return 3;
    default:
        return -1;
    }
}

bool PSDefenderGaps::IsLeftGap(EPSRunGap Gap)
{
    return Gap == EPSRunGap::DLeft || Gap == EPSRunGap::CLeft || Gap == EPSRunGap::BLeft || Gap == EPSRunGap::ALeft;
}

EPSRunGap PSDefenderGaps::MakeGap(int32 Level, bool bLeft)
{
    if (Level < 0 || Level > 3)
    {
        return EPSRunGap::None;
    }
    const int32 Value = bLeft ? static_cast<int32>(EPSRunGap::ALeft) - Level : static_cast<int32>(EPSRunGap::ARight) + Level;
    return static_cast<EPSRunGap>(Value);
}

bool PSDefenderGaps::ComputeGapSpots(const TArray<APSPlayerPawn*>& Pawns, float BallY, const FPSRunFitCatalog& Catalog, TArray<FVector>& OutSpots)
{
    OutSpots.Init(FVector::ZeroVector, PSDefenderGapsPrivate::GapSlots);
    TArray<const APSPlayerPawn*> Line;
    TArray<const APSPlayerPawn*> TightEnds;
    for (const APSPlayerPawn* Pawn : Pawns)
    {
        if (!Pawn || Pawn->TeamSide != EPSTeamSide::Offense)
        {
            continue;
        }
        const EPlayerRole Role = Pawn->GetAttributes().Role;
        if (Role == EPlayerRole::OffensiveLineman)
        {
            Line.Add(Pawn);
        }
        else if (Role == EPlayerRole::TightEnd)
        {
            TightEnds.Add(Pawn);
        }
    }
    if (Line.Num() == 0)
    {
        return false;
    }
    Line.Sort([](const APSPlayerPawn& A, const APSPlayerPawn& B)
    {
        return A.GetActorLocation().Y < B.GetActorLocation().Y;
    });

    float LineX = 0.f;
    for (const APSPlayerPawn* Lineman : Line)
    {
        LineX += Lineman->GetActorLocation().X;
    }
    LineX /= Line.Num();
    const float LineZ = Line[0]->GetActorLocation().Z;

    // An inline tight end (the nearest, each side) extends the line.
    const float LeftEdge = Line[0]->GetActorLocation().Y;
    const float RightEdge = Line.Last()->GetActorLocation().Y;
    const APSPlayerPawn* LeftEnd = nullptr;
    const APSPlayerPawn* RightEnd = nullptr;
    for (const APSPlayerPawn* TightEnd : TightEnds)
    {
        const float EndY = TightEnd->GetActorLocation().Y;
        if (EndY > RightEdge && EndY - RightEdge <= Catalog.InlineTightEndWidth && (!RightEnd || EndY < RightEnd->GetActorLocation().Y))
        {
            RightEnd = TightEnd;
        }
        else if (EndY < LeftEdge && LeftEdge - EndY <= Catalog.InlineTightEndWidth && (!LeftEnd || EndY > LeftEnd->GetActorLocation().Y))
        {
            LeftEnd = TightEnd;
        }
    }
    TArray<float> Surface;
    if (LeftEnd)
    {
        Surface.Add(LeftEnd->GetActorLocation().Y);
    }
    for (const APSPlayerPawn* Lineman : Line)
    {
        Surface.Add(Lineman->GetActorLocation().Y);
    }
    if (RightEnd)
    {
        Surface.Add(RightEnd->GetActorLocation().Y);
    }

    // The center is the lineman nearest the ball.
    int32 CenterIndex = 0;
    float CenterDistance = TNumericLimits<float>::Max();
    for (int32 Index = 0; Index < Line.Num(); ++Index)
    {
        const float Distance = FMath::Abs(Line[Index]->GetActorLocation().Y - BallY);
        if (Distance < CenterDistance)
        {
            CenterDistance = Distance;
            CenterIndex = Index;
        }
    }
    if (LeftEnd)
    {
        ++CenterIndex;
    }

    // Each gap spans two neighbours on the line, from the center out; past the last man, a
    // gap is GapWidth wide.
    for (const bool bLeft : { true, false })
    {
        const float Outward = bLeft ? -1.f : 1.f;
        float Inner = Surface[CenterIndex];
        for (int32 Level = 0; Level < 4; ++Level)
        {
            const int32 OuterIndex = CenterIndex + (bLeft ? -(Level + 1) : Level + 1);
            const float Outer = Surface.IsValidIndex(OuterIndex) ? Surface[OuterIndex] : Inner + Outward * Catalog.GapWidth;
            OutSpots[static_cast<int32>(MakeGap(Level, bLeft))] = FVector(LineX, (Inner + Outer) * 0.5f, LineZ);
            Inner = Outer;
        }
    }
    return true;
}

void PSDefenderGaps::AssignFrontGaps(const FPSRunFitFront& Front, const TArray<APSPlayerPawn*>& Defenders, TArray<EPSRunGap>& OutGaps)
{
    OutGaps.Init(EPSRunGap::None, Defenders.Num());
    for (const FPSRunFitRoleGaps& RoleGaps : Front.Fits)
    {
        TArray<int32> Slots;
        for (int32 Index = 0; Index < Defenders.Num(); ++Index)
        {
            if (Defenders[Index] && Defenders[Index]->GetAttributes().Role == RoleGaps.Role)
            {
                Slots.Add(Index);
            }
        }
        Slots.Sort([&Defenders](int32 A, int32 B)
        {
            return Defenders[A]->GetActorLocation().Y < Defenders[B]->GetActorLocation().Y;
        });
        for (int32 Slot = 0; Slot < Slots.Num() && Slot < RoleGaps.Gaps.Num(); ++Slot)
        {
            OutGaps[Slots[Slot]] = RoleGaps.Gaps[Slot];
        }
    }
}

TArray<FString> PSDefenderGaps::ValidateCatalog(const FPSRunFitCatalog& Catalog)
{
    TArray<FString> Problems;
    TSet<FString> FrontNames;
    for (int32 FrontIndex = 0; FrontIndex < Catalog.Fronts.Num(); ++FrontIndex)
    {
        const FPSRunFitFront& Front = Catalog.Fronts[FrontIndex];
        const FString Where = FString::Printf(TEXT("Fronts[%d]"), FrontIndex);
        if (Front.Front.IsEmpty() || FrontNames.Contains(Front.Front))
        {
            Problems.Add(Where + TEXT(": Front is empty or listed twice"));
        }
        FrontNames.Add(Front.Front);
        TSet<EPlayerRole> Roles;
        TSet<EPSRunGap> Gaps;
        for (const FPSRunFitRoleGaps& RoleGaps : Front.Fits)
        {
            if (Roles.Contains(RoleGaps.Role))
            {
                Problems.Add(Where + TEXT(": a role is listed twice"));
            }
            Roles.Add(RoleGaps.Role);
            for (const EPSRunGap Gap : RoleGaps.Gaps)
            {
                if (Gap == EPSRunGap::None || Gaps.Contains(Gap))
                {
                    Problems.Add(Where + FString::Printf(TEXT(": gap %s is None or given twice"), *PSDefenderGapsPrivate::GapName(Gap)));
                }
                Gaps.Add(Gap);
            }
        }
    }
    if (!FrontNames.Contains(Catalog.DefaultFront))
    {
        Problems.Add(TEXT("DefaultFront must name a listed front"));
    }
    if (Catalog.GapWidth <= 0.f || Catalog.InlineTightEndWidth < 0.f || Catalog.FitDepth < 0.f || Catalog.SecondLevelDepth < 0.f
        || Catalog.LeverageOffset < 0.f || Catalog.FlowWeight < 0.f || Catalog.FlowWeight > 1.f || Catalog.AttackRadius < 0.f || Catalog.FillRadius < 0.f)
    {
        Problems.Add(TEXT("a distance or weight is out of range"));
    }
    return Problems;
}

void UPSDefenderGapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnSnapMC.AddUObject(this, &UPSDefenderGapSubsystem::HandleSnap);
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSDefenderGapSubsystem::HandlePhaseChange);
        BoundBus = Bus;
    }
}

void UPSDefenderGapSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();

    Super::Deinitialize();
}

bool UPSDefenderGapSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPSDefenderGapSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSDefenderGapSubsystem, STATGROUP_Tickables);
}

void UPSDefenderGapSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    UpdateFits(DeltaTime);
}

FString UPSDefenderGapSubsystem::GetDefaultCatalogPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/run_fits.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSRunFitCatalog& UPSDefenderGapSubsystem::GetCatalog()
{
    if (!bCatalogLoaded)
    {
        LoadCatalogFromJson(GetDefaultCatalogPath());
    }
    return Catalog;
}

bool UPSDefenderGapSubsystem::LoadCatalogFromJson(const FString& JsonFilePath)
{
    bCatalogLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSRunFitCatalog Loaded;
    if (!Ingestion->LoadRunFitsFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDefenderGapSubsystem: Could not load the run fits from %s; no gaps."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : PSDefenderGaps::ValidateCatalog(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDefenderGapSubsystem: %s"), *Problem);
    }
    Catalog = Loaded;
    return true;
}

void UPSDefenderGapSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    // The call's assignments are handed out on this same event, so the gaps are mapped on the
    // first update after it.
    LineOfScrimmage = Event.LineOfScrimmage;
    Fits.Reset();
    ExchangedGaps.Reset();
    Integrity.Reset();
    AssignedFront.Reset();
    PublishedOpenMask = -1;
    PublishedExchangeCount = 0;
    ExchangeCount = 0;
    bAssignPending = true;
    bPlayLive = true;
}

void UPSDefenderGapSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") || Event.NewPhase == TEXT("PreSnap"))
    {
        bPlayLive = false;
    }
}

void UPSDefenderGapSubsystem::EnsureAssigned()
{
    if (!bAssignPending)
    {
        return;
    }
    FString Front;
    UPSPlayCallSubsystem* PlayCall = GetWorld() ? GetWorld()->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    FPSPlayDefinition DefensePlay;
    if (PlayCall && PlayCall->GetDefensivePlayToRun(DefensePlay))
    {
        Front = DefensePlay.Front;
    }
    AssignGaps(Front);
}

void UPSDefenderGapSubsystem::AssignGaps(const FString& Front)
{
    bAssignPending = false;
    Fits.Reset();
    ExchangedGaps.Reset();
    Integrity.Reset();
    PublishedOpenMask = -1;
    PublishedExchangeCount = 0;
    ExchangeCount = 0;

    const FPSRunFitCatalog& Data = GetCatalog();
    const FPSRunFitFront* Found = Data.Fronts.FindByPredicate([&Front](const FPSRunFitFront& Candidate) { return Candidate.Front == Front; });
    if (!Found)
    {
        const FString& Fallback = Data.DefaultFront;
        Found = Data.Fronts.FindByPredicate([&Fallback](const FPSRunFitFront& Candidate) { return Candidate.Front == Fallback; });
    }
    AssignedFront = Found ? Found->Front : FString();
    if (!Found)
    {
        return;
    }

    // The front maps every defender by position; the call then takes those in coverage out of
    // the run fit, leaving their gaps open.
    TArray<APSPlayerPawn*> Defenders;
    for (APSPlayerPawn* Pawn : GetFieldPawns())
    {
        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            Defenders.Add(Pawn);
        }
    }
    TArray<EPSRunGap> Gaps;
    PSDefenderGaps::AssignFrontGaps(*Found, Defenders, Gaps);
    for (int32 Index = 0; Index < Defenders.Num(); ++Index)
    {
        if (Gaps[Index] == EPSRunGap::None || !PSDefenderGapsPrivate::HasRunResponsibility(Defenders[Index]))
        {
            continue;
        }
        FPSDefenderGapFit Fit;
        Fit.Gap = Gaps[Index];
        Fit.bFirstLevel = Defenders[Index]->GetAttributes().Role == EPlayerRole::DefensiveLineman;
        Fits.Add(MakeWeakObjectPtr(Defenders[Index]), Fit);
    }
    AssignTechniques();
}

void UPSDefenderGapSubsystem::AssignTechniques()
{
    // The outermost fitter on each side forces (box); everyone inside him spills.
    for (auto& Pair : Fits)
    {
        Pair.Value.Technique = Pair.Value.Gap == EPSRunGap::None ? EPSFitTechnique::None : EPSFitTechnique::Spill;
    }
    for (const bool bLeft : { true, false })
    {
        FPSDefenderGapFit* Force = nullptr;
        for (auto& Pair : Fits)
        {
            const EPSRunGap Gap = Pair.Value.Gap;
            if (Gap != EPSRunGap::None && PSDefenderGaps::IsLeftGap(Gap) == bLeft
                && (!Force || PSDefenderGaps::GetGapLevel(Gap) > PSDefenderGaps::GetGapLevel(Force->Gap)))
            {
                Force = &Pair.Value;
            }
        }
        if (Force)
        {
            Force->Technique = EPSFitTechnique::Box;
        }
    }
}

void UPSDefenderGapSubsystem::UpdateFits(float DeltaSeconds)
{
    if (!bPlayLive)
    {
        return;
    }
    EnsureAssigned();

    const TArray<APSPlayerPawn*> Pawns = GetFieldPawns();
    TArray<FVector> Spots;
    const bool bLine = PSDefenderGaps::ComputeGapSpots(Pawns, LineOfScrimmage.Y, GetCatalog(), Spots);
    const APSPlayerPawn* Carrier = FindRunCarrier(Pawns);
    if (bLine && Carrier)
    {
        TryScrapeExchange(Carrier, Spots);
    }

    // Integrity: a gap is filled when its owner is lined up on it, blocked or not.
    Integrity.Reset();
    int32 OpenMask = 0;
    TArray<FString> OpenNames;
    for (int32 Index = static_cast<int32>(EPSRunGap::DLeft); Index <= static_cast<int32>(EPSRunGap::DRight); ++Index)
    {
        FPSGapStatus Status;
        Status.Gap = static_cast<EPSRunGap>(Index);
        for (const auto& Pair : Fits)
        {
            if (Pair.Value.Gap == Status.Gap)
            {
                Status.Owner = Pair.Key;
                break;
            }
        }
        if (const APSPlayerPawn* Owner = Status.Owner.Get())
        {
            Status.bOwnerBlocked = Owner->bIsEngaged;
            Status.bFilled = bLine && FMath::Abs(Owner->GetActorLocation().Y - Spots[Index].Y) <= Catalog.FillRadius;
        }
        if (!Status.bFilled)
        {
            OpenMask |= 1 << Index;
            OpenNames.Add(PSDefenderGapsPrivate::GapName(Status.Gap));
        }
        Integrity.Add(Status);
    }

    if (OpenMask == PublishedOpenMask && ExchangeCount == PublishedExchangeCount)
    {
        return;
    }
    PublishedOpenMask = OpenMask;
    PublishedExchangeCount = ExchangeCount;
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        FPSTelemetryGapIntegrityEvent Event;
        Event.Front = AssignedFront;
        Event.OpenGaps = FString::Join(OpenNames, TEXT(","));
        Event.OpenGapCount = OpenNames.Num();
        Event.bRunRead = Carrier != nullptr;
        Event.ScrapeExchanges = ExchangeCount;
        Bus->PublishGapIntegrity(Event);
    }
}

void UPSDefenderGapSubsystem::TryScrapeExchange(const APSPlayerPawn* Carrier, const TArray<FVector>& Spots)
{
    // The gap the carrier is heading for: the one nearest him across the field.
    EPSRunGap Target = EPSRunGap::None;
    float TargetDistance = TNumericLimits<float>::Max();
    for (int32 Index = static_cast<int32>(EPSRunGap::DLeft); Index <= static_cast<int32>(EPSRunGap::DRight); ++Index)
    {
        const float Distance = FMath::Abs(Spots[Index].Y - Carrier->GetActorLocation().Y);
        if (Distance < TargetDistance)
        {
            TargetDistance = Distance;
            Target = static_cast<EPSRunGap>(Index);
        }
    }
    if (Target == EPSRunGap::None || ExchangedGaps.Contains(Target))
    {
        return;
    }

    // Its owner, free, fills it himself.
    FPSDefenderGapFit* OwnerFit = nullptr;
    for (auto& Pair : Fits)
    {
        if (Pair.Value.Gap == Target)
        {
            const APSPlayerPawn* Owner = Pair.Key.Get();
            if (Owner && !Owner->bIsEngaged)
            {
                return;
            }
            OwnerFit = &Pair.Value;
            break;
        }
    }

    // Blocked or missing: the nearest free linebacker scrapes over the top into it, and the two
    // exchange gaps.
    const float TargetY = Spots[static_cast<int32>(Target)].Y;
    FPSDefenderGapFit* ScraperFit = nullptr;
    float ScraperDistance = TNumericLimits<float>::Max();
    for (auto& Pair : Fits)
    {
        const APSPlayerPawn* Candidate = Pair.Key.Get();
        if (!Candidate || Pair.Value.bFirstLevel || Candidate->bIsEngaged || Pair.Value.Gap == Target)
        {
            continue;
        }
        const float Distance = FMath::Abs(Candidate->GetActorLocation().Y - TargetY);
        if (Distance < ScraperDistance)
        {
            ScraperDistance = Distance;
            ScraperFit = &Pair.Value;
        }
    }
    if (!ScraperFit)
    {
        return;
    }
    const EPSRunGap Vacated = ScraperFit->Gap;
    ScraperFit->Gap = Target;
    ScraperFit->bExchanged = true;
    if (OwnerFit)
    {
        OwnerFit->Gap = Vacated;
        OwnerFit->bExchanged = true;
    }
    ExchangedGaps.Add(Target);
    ++ExchangeCount;
    AssignTechniques();
}

FPSDefenderGapFit UPSDefenderGapSubsystem::GetFit(const APSPlayerPawn* Defender)
{
    EnsureAssigned();
    const FPSDefenderGapFit* Fit = Defender ? Fits.Find(MakeWeakObjectPtr(const_cast<APSPlayerPawn*>(Defender))) : nullptr;
    return Fit ? *Fit : FPSDefenderGapFit();
}

APSPlayerPawn* UPSDefenderGapSubsystem::GetGapOwner(EPSRunGap Gap)
{
    EnsureAssigned();
    for (const auto& Pair : Fits)
    {
        if (Pair.Value.Gap == Gap && Gap != EPSRunGap::None)
        {
            return Pair.Key.Get();
        }
    }
    return nullptr;
}

FVector UPSDefenderGapSubsystem::GetGapSpot(EPSRunGap Gap)
{
    TArray<FVector> Spots;
    if (Gap == EPSRunGap::None || !ReadGapSpots(Spots))
    {
        return FVector::ZeroVector;
    }
    return Spots[static_cast<int32>(Gap)];
}

bool UPSDefenderGapSubsystem::GetFitTarget(const APSPlayerPawn* Defender, const APSPlayerPawn* Carrier, FVector& OutTarget)
{
    EnsureAssigned();
    const FPSDefenderGapFit* Fit = Defender ? Fits.Find(MakeWeakObjectPtr(const_cast<APSPlayerPawn*>(Defender))) : nullptr;
    TArray<FVector> Spots;
    if (!Fit || Fit->Gap == EPSRunGap::None || !Carrier || !ReadGapSpots(Spots))
    {
        return false;
    }
    const FPSRunFitCatalog& Data = GetCatalog();
    const FVector Spot = Spots[static_cast<int32>(Fit->Gap)];
    const FVector CarrierLocation = Carrier->GetActorLocation();

    // Past the line, or coming through his gap: no more fitting, go get him.
    if (CarrierLocation.X >= LineOfScrimmage.X || FMath::Abs(CarrierLocation.Y - Spot.Y) <= Data.AttackRadius)
    {
        return false;
    }

    // A spill fitter plays the inside of his gap, the force player its outside; the second
    // level flows with the carrier across the field.
    const float Inward = Spot.Y > LineOfScrimmage.Y ? -1.f : 1.f;
    float TargetY = Spot.Y + Data.LeverageOffset * (Fit->Technique == EPSFitTechnique::Box ? -Inward : Inward);
    if (!Fit->bFirstLevel)
    {
        TargetY = FMath::Lerp(TargetY, CarrierLocation.Y, Data.FlowWeight);
    }
    const float Depth = Fit->bFirstLevel ? Data.FitDepth : Data.SecondLevelDepth;
    OutTarget = FVector(LineOfScrimmage.X + Depth, TargetY, Defender->GetActorLocation().Z);
    return true;
}

int32 UPSDefenderGapSubsystem::GetOpenGapCount() const
{
    int32 Open = 0;
    for (const FPSGapStatus& Status : Integrity)
    {
        Open += Status.bFilled ? 0 : 1;
    }
    return Open;
}

bool UPSDefenderGapSubsystem::ReadGapSpots(TArray<FVector>& OutSpots)
{
    return PSDefenderGaps::ComputeGapSpots(GetFieldPawns(), LineOfScrimmage.Y, GetCatalog(), OutSpots);
}

APSPlayerPawn* UPSDefenderGapSubsystem::FindRunCarrier(const TArray<APSPlayerPawn*>& Pawns) const
{
    for (APSPlayerPawn* Pawn : Pawns)
    {
        if (Pawn->HasPossession() && Pawn->TeamSide == EPSTeamSide::Offense && Pawn->GetAttributes().Role != EPlayerRole::Quarterback)
        {
            return Pawn;
        }
    }
    return nullptr;
}

TArray<APSPlayerPawn*> UPSDefenderGapSubsystem::GetFieldPawns() const
{
    TArray<APSPlayerPawn*> Pawns;
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        Pawns.Add(*It);
    }
    return Pawns;
}
