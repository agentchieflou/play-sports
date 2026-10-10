#include "PSDefenderAIComponent.h"
#include "PSAIFieldSnapshot.h"
#include "PSDataIngestion.h"
#include "PSDefenseController.h"
#include "PSDefenderGapSubsystem.h"
#include "PSPlatformTiers.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSDefenderAIPrivate
{
    /** Unit direction from From to To on the ground; zero when they coincide. */
    FVector GroundDirection(const FVector& From, const FVector& To)
    {
        FVector Direction = To - From;
        Direction.Z = 0.f;
        return Direction.GetSafeNormal();
    }

    /** A player the defense covers: receivers, tight ends and backs of the offense. Role is
     *  the pawn's role from the field snapshot. */
    bool IsEligibleReceiver(const APSPlayerPawn* Pawn, EPlayerRole Role)
    {
        if (!Pawn || Pawn->TeamSide != EPSTeamSide::Offense)
        {
            return false;
        }
        return Role == EPlayerRole::WideReceiver || Role == EPlayerRole::TightEnd || Role == EPlayerRole::RunningBack;
    }
}

DECLARE_CYCLE_STAT(TEXT("Defender AI decision"), STAT_PSAIDefenderDecision, STATGROUP_PSAI);

UPSDefenderAIComponent::UPSDefenderAIComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

FString UPSDefenderAIComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/defense_ai_tuning.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FDefenderAITuningRow& UPSDefenderAIComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSDefenderAIComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FDefenderAITuningRow Loaded;
    if (!Ingestion->LoadDefenderAITuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDefenderAIComponent: Could not load AI tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    Tuning = Loaded;
    return true;
}

void UPSDefenderAIComponent::BeginPlay()
{
    Super::BeginPlay();
    GetTuning();
    BindToBus();
}

void UPSDefenderAIComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromBus();
    Super::EndPlay(EndPlayReason);
}

void UPSDefenderAIComponent::BindToBus()
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnSnapMC.AddUObject(this, &UPSDefenderAIComponent::HandleSnap);
    Bus->OnThrowMC.AddUObject(this, &UPSDefenderAIComponent::HandleThrow);
    Bus->OnCatchMC.AddUObject(this, &UPSDefenderAIComponent::HandleCatch);
    Bus->OnPumpFakeMC.AddUObject(this, &UPSDefenderAIComponent::HandlePumpFake);
    Bus->OnRouteRunningMC.AddUObject(this, &UPSDefenderAIComponent::HandleRouteRunning);
    Bus->OnBlownCoverageMC.AddUObject(this, &UPSDefenderAIComponent::HandleBlownCoverage);
    Bus->OnPhaseChangeMC.AddUObject(this, &UPSDefenderAIComponent::HandlePhaseChange);
    Bus->OnControlChangeMC.AddUObject(this, &UPSDefenderAIComponent::HandleControlChange);
    BoundBus = Bus;
}

void UPSDefenderAIComponent::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnThrowMC.RemoveAll(this);
        Bus->OnCatchMC.RemoveAll(this);
        Bus->OnPumpFakeMC.RemoveAll(this);
        Bus->OnRouteRunningMC.RemoveAll(this);
        Bus->OnBlownCoverageMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
        Bus->OnControlChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSDefenderAIComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UpdateAI(DeltaTime, PSPlatformTiers::GetActiveTier().AIDecisionInterval);
}

void UPSDefenderAIComponent::UpdateAI(float DeltaSeconds, float DecisionInterval)
{
    // Decide at the platform tier's rate (Epic 129), but steer every frame: movement input is
    // used up each frame, so between decisions the last direction is held.
    DecisionClock += DeltaSeconds;
    if (DecisionClock >= DecisionInterval)
    {
        TickAI(DecisionClock);
        DecisionClock = 0.f;
    }
    else if (APSPlayerPawn* Self = GetSelf())
    {
        if (!DesiredDirection.IsNearlyZero())
        {
            Self->AddMovementInput(DesiredDirection, 1.f);
        }
    }
}

APSPlayerPawn* UPSDefenderAIComponent::GetCoveredReceiver() const
{
    return CoveredReceiver.Get();
}

float UPSDefenderAIComponent::GetReactionSeconds()
{
    const APSPlayerPawn* Self = GetSelf();
    const float Awareness = Self ? FMath::Clamp(Self->GetAttributes().Awareness, 0.f, 100.f) : 0.f;
    return GetTuning().MaxReactionSeconds * (1.f - Awareness / 100.f);
}

void UPSDefenderAIComponent::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    // The orchestrator hands out assignments on this same event, possibly after this
    // handler, so the assignment is taken up on the first tick of the play.
    bPlayLive = true;
    bSnapPending = true;
    bBallInAir = false;
    TimeSinceSnap = 0.f;
    PursueAt = -1.f;
    PassReadAt = -1.f;
    BallHawkAt = -1.f;
    FrozenUntil = -1.f;
    LineOfScrimmage = Event.LineOfScrimmage;
    CoveredReceiver.Reset();
    Action = EPSDefenderAction::Idle;
}

void UPSDefenderAIComponent::HandleThrow(const FPSTelemetryThrowEvent& Event)
{
    APSPlayerPawn* Self = GetSelf();
    if (!bPlayLive)
    {
        return;
    }
    if (!Self)
    {
        // A human has this defender: the AI still notes the ball is up, so it picks the play
        // up correctly if control comes back before it lands (Epic 30).
        bBallInAir = true;
        LandingSpot = Event.LandingLocation.IsZero() ? Event.TargetLocation : Event.LandingLocation;
        return;
    }
    if (bSnapPending)
    {
        bSnapPending = false;
        StartAssignment(Self);
    }

    bBallInAir = true;
    LandingSpot = Event.LandingLocation.IsZero() ? Event.TargetLocation : Event.LandingLocation;

    // Defenders in coverage near where it comes down break on it, once they react.
    const bool bInCoverage = Action == EPSDefenderAction::Cover || Action == EPSDefenderAction::Zone || Action == EPSDefenderAction::Read;
    if (bInCoverage && FVector::Dist2D(Self->GetActorLocation(), LandingSpot) <= GetTuning().BallHawkRadius)
    {
        BallHawkAt = TimeSinceSnap + GetReactionSeconds();
    }
}

void UPSDefenderAIComponent::HandleControlChange(const FPSTelemetryControlChangeEvent& Event)
{
    const APSPlayerPawn* Self = GetSelf();
    if (!Event.bHumanControlled && Self && Self->GetAttributes().PlayerId == Event.PlayerId)
    {
        ResumeFromHuman();
    }
}

void UPSDefenderAIComponent::ResumeFromHuman()
{
    const APSPlayerPawn* Self = GetSelf();
    if (!Self)
    {
        return;
    }
    // Until the next decision the pawn keeps going the way the human was taking him; the
    // decision itself reads the field as it is now.
    FVector Heading = Self->GetVelocity();
    Heading.Z = 0.f;
    DesiredDirection = Heading.GetSafeNormal();
}

void UPSDefenderAIComponent::HandleCatch(const FPSTelemetryCatchEvent& Event)
{
    // Caught (by either side): the next tick sees the carrier and pursues or returns.
    bBallInAir = false;
    BallHawkAt = -1.f;
}

void UPSDefenderAIComponent::HandlePumpFake(const FPSTelemetryPumpFakeEvent& Event)
{
    // Coverage bites on the fake: it freezes, the less aware the longer.
    const bool bInCoverage = Action == EPSDefenderAction::Cover || Action == EPSDefenderAction::Zone || Action == EPSDefenderAction::Read;
    const APSPlayerPawn* Self = GetSelf();
    if (!bPlayLive || !bInCoverage || !Self)
    {
        return;
    }
    const float Awareness = FMath::Clamp(Self->GetAttributes().Awareness, 0.f, 100.f);
    FrozenUntil = TimeSinceSnap + GetTuning().PumpFakeFreezeSeconds * (1.f - Awareness / 100.f);
}

void UPSDefenderAIComponent::HandleRouteRunning(const FPSTelemetryRouteEvent& Event)
{
    // A double move this defender bit on (Epic 68; the receiver's route runner decides the
    // bite): he freezes, as on a pump fake.
    const APSPlayerPawn* Self = GetSelf();
    if (bPlayLive && Self && Event.Kind == EPSRouteEventKind::DoubleMove && Event.Seconds > 0.f
        && Event.DefenderName == Self->GetAttributes().DisplayName)
    {
        FrozenUntil = FMath::Max(FrozenUntil, TimeSinceSnap + Event.Seconds);
    }
}

void UPSDefenderAIComponent::HandleBlownCoverage(const FPSTelemetryBlownCoverageEvent& Event)
{
    // A receiver running free (Epic 17's broken-play reactions; UPSBlownCoverageSubsystem picks
    // who helps): the defender it names leaves his zone and takes him in man coverage.
    const APSPlayerPawn* Self = GetSelf();
    if (!bPlayLive || bBallInAir || !Self || Event.HelperName.IsEmpty() || Event.HelperName != Self->GetAttributes().DisplayName)
    {
        return;
    }
    UPSAIFieldSnapshot* Field = GetFieldSnapshot();
    if (!Field)
    {
        return;
    }
    for (APSPlayerPawn* Candidate : Field->GetPawns())
    {
        if (Candidate && Candidate->TeamSide != Self->TeamSide && Candidate->GetAttributes().DisplayName == Event.ReceiverName)
        {
            CoveredReceiver = Candidate;
            Action = EPSDefenderAction::Cover;
            return;
        }
    }
}

void UPSDefenderAIComponent::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") || Event.NewPhase == TEXT("PreSnap"))
    {
        bPlayLive = false;
        bBallInAir = false;
        Action = EPSDefenderAction::Idle;
    }
}

void UPSDefenderAIComponent::StartAssignment(APSPlayerPawn* Self)
{
    const APSDefenseController* Controller = GetDefenseController();
    const EPSDefensiveAssignmentType Assignment = Controller ? Controller->GetAssignment() : EPSDefensiveAssignmentType::RunFit;
    ZoneSpot = Self->GetActorLocation();

    switch (Assignment)
    {
    case EPSDefensiveAssignmentType::PassRush:
        Action = EPSDefenderAction::Rush;
        break;
    case EPSDefensiveAssignmentType::Contain:
        Action = EPSDefenderAction::Contain;
        break;
    case EPSDefensiveAssignmentType::ManCoverage:
    {
        APSPlayerPawn* Named = Controller ? Cast<APSPlayerPawn>(Controller->GetCoverageTarget()) : nullptr;
        CoveredReceiver = Named ? Named : PickReceiverToCover(Self);
        // Nobody left to cover: he plays the spot he lined up on.
        Action = CoveredReceiver.IsValid() ? EPSDefenderAction::Cover : EPSDefenderAction::Zone;
        break;
    }
    case EPSDefensiveAssignmentType::ZoneCoverage:
    {
        // The play's zone (line of scrimmage + offset); a zone with no offset is his own spot.
        const FVector Assigned = Controller ? Controller->GetZoneLocation() : FVector::ZeroVector;
        if (!Assigned.IsZero() && !Assigned.Equals(LineOfScrimmage))
        {
            ZoneSpot = Assigned;
        }
        Action = EPSDefenderAction::Zone;
        break;
    }
    case EPSDefensiveAssignmentType::RunFit:
        Action = EPSDefenderAction::Read;
        break;
    default:
        Action = EPSDefenderAction::Idle;
        break;
    }
}

APSPlayerPawn* UPSDefenderAIComponent::PickReceiverToCover(const APSPlayerPawn* Self) const
{
    UPSAIFieldSnapshot* Field = GetFieldSnapshot();
    if (!Field)
    {
        return nullptr;
    }
    const TArray<APSPlayerPawn*>& Pawns = Field->GetPawns();
    const TArray<EPlayerRole>& Roles = Field->GetRoles();

    // Receivers another defender already has in man coverage are taken.
    TSet<const APSPlayerPawn*> Taken;
    for (const APSPlayerPawn* Pawn : Pawns)
    {
        const APSDefenseController* OtherController = Cast<APSDefenseController>(Pawn->GetController());
        const UPSDefenderAIComponent* Other = OtherController ? OtherController->GetDefenderAI() : nullptr;
        if (Other && Other != this && Other->GetCoveredReceiver())
        {
            Taken.Add(Other->GetCoveredReceiver());
        }
    }

    APSPlayerPawn* Nearest = nullptr;
    float NearestDistance = TNumericLimits<float>::Max();
    for (int32 Index = 0; Index < Pawns.Num(); ++Index)
    {
        APSPlayerPawn* Candidate = Pawns[Index];
        if (!PSDefenderAIPrivate::IsEligibleReceiver(Candidate, Roles[Index]) || Taken.Contains(Candidate))
        {
            continue;
        }
        const float Distance = FVector::Dist2D(Candidate->GetActorLocation(), Self->GetActorLocation());
        if (Distance < NearestDistance)
        {
            NearestDistance = Distance;
            Nearest = Candidate;
        }
    }
    return Nearest;
}

bool UPSDefenderAIComponent::IsBallOut(const APSPlayerPawn* Carrier) const
{
    // The ball is out of the passer's hands: a back or a receiver has it, or the QB has run
    // past the line. Either way the play is now a chase.
    return Carrier && Carrier->TeamSide == EPSTeamSide::Offense
        && (Carrier->GetAttributes().Role != EPlayerRole::Quarterback || Carrier->GetActorLocation().X > LineOfScrimmage.X);
}

void UPSDefenderAIComponent::TickAI(float DeltaSeconds)
{
    SCOPE_CYCLE_COUNTER(STAT_PSAIDefenderDecision);
    DesiredDirection = FVector::ZeroVector;
    APSPlayerPawn* Self = GetSelf();
    if (!Self || !bPlayLive)
    {
        return;
    }
    const FDefenderAITuningRow& Settings = GetTuning();
    TimeSinceSnap += DeltaSeconds;

    if (bSnapPending)
    {
        bSnapPending = false;
        StartAssignment(Self);
    }

    APSPlayerPawn* Carrier = FindCarrier();
    if (Self->HasPossession())
    {
        Action = EPSDefenderAction::Return;
    }
    else if (Action != EPSDefenderAction::Return && Action != EPSDefenderAction::Idle)
    {
        if (IsBallOut(Carrier))
        {
            if (PursueAt < 0.f)
            {
                PursueAt = TimeSinceSnap + GetReactionSeconds();
            }
            if (TimeSinceSnap >= PursueAt)
            {
                // On a run still behind the line, a defender with a gap fits it until the
                // carrier comes to it; then, or with no gap, he pursues (Epic 81).
                FVector FitTarget;
                Action = Action != EPSDefenderAction::Pursue && GetFitTarget(Self, Carrier, FitTarget)
                    ? EPSDefenderAction::Fit : EPSDefenderAction::Pursue;
            }
        }
        else if (bBallInAir && BallHawkAt >= 0.f && TimeSinceSnap >= BallHawkAt)
        {
            Action = EPSDefenderAction::BallHawk;
        }

        // A run-fit defender who sees the passer drop back calls it a pass and drops.
        if (Action == EPSDefenderAction::Read && Carrier && Carrier->GetAttributes().Role == EPlayerRole::Quarterback
            && Carrier->GetActorLocation().X < LineOfScrimmage.X - Settings.PassReadDepth)
        {
            if (PassReadAt < 0.f)
            {
                PassReadAt = TimeSinceSnap + GetReactionSeconds();
            }
            if (TimeSinceSnap >= PassReadAt)
            {
                ZoneSpot = FVector(LineOfScrimmage.X + Settings.PassDropDepth, Self->GetActorLocation().Y, Self->GetActorLocation().Z);
                Action = EPSDefenderAction::Zone;
            }
        }
    }

    // One who bit on a pump fake stands still until it wears off.
    if (IsFrozen())
    {
        return;
    }

    // A blocked defender fights the blocker (APSPlayerPawn::Tick steers that). A blocked
    // fitter also works across the blocker's face to stay in his gap.
    if (Self->bIsEngaged)
    {
        FVector FitTarget;
        if (Action == EPSDefenderAction::Fit && GetFitTarget(Self, Carrier, FitTarget))
        {
            const float Across = FitTarget.Y - Self->GetActorLocation().Y;
            if (!FMath::IsNearlyZero(Across))
            {
                DesiredDirection = FVector(0.f, FMath::Sign(Across), 0.f);
                Self->AddMovementInput(DesiredDirection, FMath::Min(1.f, FMath::Abs(Across) / FMath::Max(1.f, Settings.ArrivalRadius)));
            }
        }
        return;
    }

    FVector Direction = FVector::ZeroVector;
    switch (Action)
    {
    case EPSDefenderAction::Rush:
        Direction = SteerToRush(Self);
        break;
    case EPSDefenderAction::Contain:
        Direction = SteerToContain(Self);
        break;
    case EPSDefenderAction::Cover:
        Direction = SteerToCover(Self);
        break;
    case EPSDefenderAction::Zone:
        Direction = SteerInZone(Self);
        break;
    case EPSDefenderAction::Pursue:
        Direction = SteerToPursue(Self, Carrier);
        break;
    case EPSDefenderAction::Fit:
        Direction = SteerToFit(Self, Carrier);
        break;
    case EPSDefenderAction::BallHawk:
        Direction = SteerToward(Self, LandingSpot);
        break;
    case EPSDefenderAction::Return:
        // The defense's end zone is behind the offense: -X.
        Direction = FVector(-1.f, 0.f, 0.f);
        break;
    default:
        break;
    }

    if (!Direction.IsNearlyZero())
    {
        Self->AddMovementInput(Direction, 1.f);
    }
    DesiredDirection = Direction;
}

FVector UPSDefenderAIComponent::SteerToward(const APSPlayerPawn* Self, const FVector& Target) const
{
    if (FVector::Dist2D(Self->GetActorLocation(), Target) <= Tuning.ArrivalRadius)
    {
        return FVector::ZeroVector;
    }
    return PSDefenderAIPrivate::GroundDirection(Self->GetActorLocation(), Target);
}

FVector UPSDefenderAIComponent::SteerToRush(const APSPlayerPawn* Self) const
{
    // After the passer, on the controller's pursuit angle.
    const APSPlayerPawn* Passer = FindOpponent(EPlayerRole::Quarterback);
    return Passer ? SteerToPursue(Self, Passer) : FVector::ZeroVector;
}

FVector UPSDefenderAIComponent::SteerToContain(const APSPlayerPawn* Self) const
{
    // Outside the passer on this rusher's side, so he can't escape that way.
    const APSPlayerPawn* Passer = FindOpponent(EPlayerRole::Quarterback);
    if (!Passer)
    {
        return FVector::ZeroVector;
    }
    const FVector PasserLocation = Passer->GetActorLocation();
    const float Side = Self->GetActorLocation().Y >= PasserLocation.Y ? 1.f : -1.f;
    return SteerToward(Self, PasserLocation + FVector(0.f, Side * Tuning.ContainWidth, 0.f));
}

FVector UPSDefenderAIComponent::SteerToCover(const APSPlayerPawn* Self) const
{
    const APSPlayerPawn* Receiver = CoveredReceiver.Get();
    if (!Receiver)
    {
        return SteerInZone(Self);
    }
    // Downfield of him by the cushion, reading where he is going as well as Awareness lets.
    const float Awareness = FMath::Clamp(Self->GetAttributes().Awareness, 0.f, 100.f);
    const FVector Anticipated = Receiver->GetVelocity() * Tuning.ManAnticipationSeconds * (Awareness / 100.f);
    return SteerToward(Self, Receiver->GetActorLocation() + Anticipated + FVector(Tuning.ManCushion, 0.f, 0.f));
}

FVector UPSDefenderAIComponent::SteerInZone(const APSPlayerPawn* Self) const
{
    // The receiver nearest the spot, if he's in the zone, pulls the defender part-way to him.
    const APSPlayerPawn* Threat = nullptr;
    float ThreatDistance = Tuning.ZoneRadius;
    if (UPSAIFieldSnapshot* Field = GetFieldSnapshot())
    {
        const TArray<APSPlayerPawn*>& Pawns = Field->GetPawns();
        const TArray<EPlayerRole>& Roles = Field->GetRoles();
        for (int32 Index = 0; Index < Pawns.Num(); ++Index)
        {
            const APSPlayerPawn* Candidate = Pawns[Index];
            if (!PSDefenderAIPrivate::IsEligibleReceiver(Candidate, Roles[Index]))
            {
                continue;
            }
            const float Distance = FVector::Dist2D(Candidate->GetActorLocation(), ZoneSpot);
            if (Distance <= ThreatDistance)
            {
                ThreatDistance = Distance;
                Threat = Candidate;
            }
        }
    }
    const FVector Target = Threat ? FMath::Lerp(ZoneSpot, Threat->GetActorLocation(), Tuning.ZoneShadeWeight) : ZoneSpot;
    return SteerToward(Self, Target);
}

FVector UPSDefenderAIComponent::SteerToPursue(const APSPlayerPawn* Self, const APSPlayerPawn* Carrier) const
{
    if (!Carrier)
    {
        return FVector::ZeroVector;
    }
    // APSDefenseController's Awareness-scaled intercept angle (Epic 15).
    const APSDefenseController* Controller = GetDefenseController();
    const FVector Intercept = Controller
        ? Controller->ComputePursuitInterceptPoint(Carrier->GetActorLocation(), Carrier->GetVelocity(), Self->GetActorLocation())
        : Carrier->GetActorLocation();
    return PSDefenderAIPrivate::GroundDirection(Self->GetActorLocation(), Intercept);
}

FVector UPSDefenderAIComponent::SteerToFit(const APSPlayerPawn* Self, const APSPlayerPawn* Carrier) const
{
    FVector Target;
    return GetFitTarget(Self, Carrier, Target) ? SteerToward(Self, Target) : SteerToPursue(Self, Carrier);
}

bool UPSDefenderAIComponent::GetFitTarget(const APSPlayerPawn* Self, const APSPlayerPawn* Carrier, FVector& OutTarget) const
{
    // A run: the ball handed or pitched to someone other than the passer.
    if (!Carrier || Carrier->TeamSide != EPSTeamSide::Offense || Carrier->GetAttributes().Role == EPlayerRole::Quarterback)
    {
        return false;
    }
    UWorld* World = GetWorld();
    UPSDefenderGapSubsystem* Gaps = World ? World->GetSubsystem<UPSDefenderGapSubsystem>() : nullptr;
    return Gaps && Gaps->GetFitTarget(Self, Carrier, OutTarget);
}

APSDefenseController* UPSDefenderAIComponent::GetDefenseController() const
{
    return Cast<APSDefenseController>(GetOwner());
}

APSPlayerPawn* UPSDefenderAIComponent::GetSelf() const
{
    const APSDefenseController* Controller = GetDefenseController();
    return Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
}

APSPlayerPawn* UPSDefenderAIComponent::FindCarrier() const
{
    UPSAIFieldSnapshot* Field = GetFieldSnapshot();
    return Field ? Field->FindBallCarrier() : nullptr;
}

APSPlayerPawn* UPSDefenderAIComponent::FindOpponent(EPlayerRole Role) const
{
    UPSAIFieldSnapshot* Field = GetFieldSnapshot();
    return Field ? Field->FindPawn(EPSTeamSide::Offense, Role) : nullptr;
}

UPSAIFieldSnapshot* UPSDefenderAIComponent::GetFieldSnapshot() const
{
    const UWorld* OwningWorld = GetWorld();
    return OwningWorld ? OwningWorld->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
}
