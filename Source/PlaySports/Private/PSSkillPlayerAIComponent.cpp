#include "PSSkillPlayerAIComponent.h"
#include "PSBall.h"
#include "PSBallActionComponent.h"
#include "PSDataIngestion.h"
#include "PSFieldReads.h"
#include "PSOffenseController.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

namespace PSSkillPlayerAIPrivate
{
    /** Unit direction from From to To on the ground; zero when they coincide. */
    FVector GroundDirection(const FVector& From, const FVector& To)
    {
        FVector Direction = To - From;
        Direction.Z = 0.f;
        return Direction.GetSafeNormal();
    }
}

UPSSkillPlayerAIComponent::UPSSkillPlayerAIComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

FString UPSSkillPlayerAIComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/skill_ai_tuning.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FSkillPlayerAITuningRow& UPSSkillPlayerAIComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSSkillPlayerAIComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FSkillPlayerAITuningRow Loaded;
    if (!Ingestion->LoadSkillPlayerAITuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSSkillPlayerAIComponent: Could not load AI tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    Tuning = Loaded;
    return true;
}

void UPSSkillPlayerAIComponent::BeginPlay()
{
    Super::BeginPlay();
    GetTuning();
    BindToBus();
}

void UPSSkillPlayerAIComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromBus();
    Super::EndPlay(EndPlayReason);
}

void UPSSkillPlayerAIComponent::BindToBus()
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnPlayCallMC.AddUObject(this, &UPSSkillPlayerAIComponent::HandlePlayCall);
    Bus->OnSnapMC.AddUObject(this, &UPSSkillPlayerAIComponent::HandleSnap);
    Bus->OnThrowMC.AddUObject(this, &UPSSkillPlayerAIComponent::HandleThrow);
    Bus->OnPhaseChangeMC.AddUObject(this, &UPSSkillPlayerAIComponent::HandlePhaseChange);
    BoundBus = Bus;
}

void UPSSkillPlayerAIComponent::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnPlayCallMC.RemoveAll(this);
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnThrowMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSSkillPlayerAIComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    TickAI(DeltaTime);
}

void UPSSkillPlayerAIComponent::HandlePlayCall(const FPSTelemetryPlayCallEvent& Event)
{
    if (Event.bOffense)
    {
        bRunPlay = Event.PlayCategory == TEXT("Run");
    }
}

void UPSSkillPlayerAIComponent::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    // The orchestrator hands out routes on this same event, possibly after this handler, so
    // the opening action is chosen on the first tick of the play rather than here.
    bPlayLive = true;
    bSnapPending = true;
    TimeSinceSnap = 0.f;
    LineOfScrimmage = Event.LineOfScrimmage;
}

void UPSSkillPlayerAIComponent::HandleThrow(const FPSTelemetryThrowEvent& Event)
{
    const APSPlayerPawn* Self = GetSelf();
    if (bPlayLive && Self && Event.TargetReceiverName == Self->GetAttributes().DisplayName)
    {
        // The throw outranks the opening action, even if it comes before this player's
        // first tick of the play.
        bSnapPending = false;
        Action = EPSSkillPlayerAction::TrackBall;
        TrackTarget = Event.LandingLocation.IsZero() ? Event.TargetLocation : Event.LandingLocation;
    }
}

void UPSSkillPlayerAIComponent::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") || Event.NewPhase == TEXT("PreSnap"))
    {
        bPlayLive = false;
        Action = EPSSkillPlayerAction::Idle;
    }
}

void UPSSkillPlayerAIComponent::TickAI(float DeltaSeconds)
{
    DesiredDirection = FVector::ZeroVector;
    APSPlayerPawn* Self = GetSelf();
    if (!Self || !bPlayLive)
    {
        return;
    }
    GetTuning();
    TimeSinceSnap += DeltaSeconds;

    const EPlayerRole Role = Self->GetAttributes().Role;
    if (Role == EPlayerRole::OffensiveLineman)
    {
        // Linemen block through the game mode's engagement pairing.
        return;
    }

    APSOffenseController* Controller = GetOffenseController();
    if (bSnapPending)
    {
        bSnapPending = false;
        const bool bHasRoute = Controller && Controller->GetRouteWaypointCount() > 0;
        Action = bHasRoute ? EPSSkillPlayerAction::RunRoute
            : (Role == EPlayerRole::Quarterback ? EPSSkillPlayerAction::ReadDefense : EPSSkillPlayerAction::Block);
    }

    // Whoever holds the ball carries it -- except a QB who hasn't decided what to do yet.
    if (Self->HasPossession())
    {
        if (Role == EPlayerRole::Quarterback && Action != EPSSkillPlayerAction::CarryBall)
        {
            TickQuarterback(Self);
        }
        else
        {
            Action = EPSSkillPlayerAction::CarryBall;
        }
    }
    else if (Action == EPSSkillPlayerAction::CarryBall)
    {
        Action = EPSSkillPlayerAction::Idle;
    }

    FVector Direction = FVector::ZeroVector;
    switch (Action)
    {
    case EPSSkillPlayerAction::RunRoute:
        // A run play's QB carries the ball to his back for the hand-off.
        if (Role == EPlayerRole::Quarterback && bRunPlay && Self->HasPossession())
        {
            const APSPlayerPawn* RunningBack = FindTeammate(EPlayerRole::RunningBack);
            Direction = RunningBack ? PSSkillPlayerAIPrivate::GroundDirection(Self->GetActorLocation(), RunningBack->GetActorLocation()) : FVector::ZeroVector;
        }
        else
        {
            Direction = SteerAlongRoute(Self);
        }
        break;
    case EPSSkillPlayerAction::CarryBall:
        Direction = SteerAsCarrier(Self);
        break;
    case EPSSkillPlayerAction::TrackBall:
        if (FVector::Dist2D(Self->GetActorLocation(), TrackTarget) > GetTuning().WaypointArrivalRadius)
        {
            Direction = PSSkillPlayerAIPrivate::GroundDirection(Self->GetActorLocation(), TrackTarget);
        }
        break;
    case EPSSkillPlayerAction::Block:
        Direction = SteerAsBlocker(Self);
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

void UPSSkillPlayerAIComponent::TickQuarterback(APSPlayerPawn* Self)
{
    const FSkillPlayerAITuningRow& Settings = GetTuning();

    if (bRunPlay)
    {
        APSPlayerPawn* RunningBack = FindTeammate(EPlayerRole::RunningBack);
        if (RunningBack && FVector::Dist2D(Self->GetActorLocation(), RunningBack->GetActorLocation()) <= Settings.HandoffRadius && Self->ExecuteHandoff(RunningBack))
        {
            Action = EPSSkillPlayerAction::Idle;
            return;
        }
        // Meet the back (RunRoute steers to him) until the hand-off or the timeout, then keep
        // it and run.
        Action = (RunningBack && TimeSinceSnap < Settings.HandoffTimeoutSeconds) ? EPSSkillPlayerAction::RunRoute : EPSSkillPlayerAction::CarryBall;
        return;
    }

    float PressureDistance = TNumericLimits<float>::Max();
    PSFieldReads::NearestOpponent(GetFieldPawns(), Self->TeamSide, Self->GetActorLocation(), &PressureDistance);
    const bool bPressured = PressureDistance <= Settings.PressureRadius;
    if (TimeSinceSnap < Settings.MinReadSeconds && !bPressured)
    {
        return;
    }

    bool bOpen = false;
    float Separation = 0.f;
    APSPlayerPawn* Receiver = ChooseReceiver(bOpen, Separation);
    if (Receiver && bOpen)
    {
        ThrowTo(Self, Receiver);
    }
    else if (bPressured || TimeSinceSnap >= Settings.MaxReadSeconds)
    {
        if (Receiver && Separation >= Settings.PressuredThrowSeparation)
        {
            ThrowTo(Self, Receiver);
        }
        else
        {
            Action = EPSSkillPlayerAction::CarryBall;
        }
    }
}

APSPlayerPawn* UPSSkillPlayerAIComponent::ChooseReceiver(bool& bOutOpen, float& OutSeparation)
{
    bOutOpen = false;
    OutSeparation = 0.f;
    const APSPlayerPawn* Self = GetSelf();
    if (!Self)
    {
        return nullptr;
    }

    const TArray<APSPlayerPawn*> Pawns = GetFieldPawns();
    APSPlayerPawn* Best = nullptr;
    for (APSPlayerPawn* Candidate : Pawns)
    {
        if (!Candidate || Candidate == Self || Candidate->TeamSide != Self->TeamSide)
        {
            continue;
        }
        const EPlayerRole Role = Candidate->GetAttributes().Role;
        if (Role != EPlayerRole::WideReceiver && Role != EPlayerRole::TightEnd && Role != EPlayerRole::RunningBack)
        {
            continue;
        }
        const float Separation = PSFieldReads::Separation(Pawns, Candidate);
        if (!Best || Separation > OutSeparation)
        {
            Best = Candidate;
            OutSeparation = Separation;
        }
    }

    // A less aware QB needs a receiver more open before he sees it (Awareness 0-100).
    const FSkillPlayerAITuningRow& Settings = GetTuning();
    const float Awareness = FMath::Clamp(Self->GetAttributes().Awareness, 0.f, 100.f);
    const float Required = Settings.OpenSeparation + Settings.AwarenessMisreadSeparation * (1.f - Awareness / 100.f);
    bOutOpen = Best && OutSeparation >= Required;
    return Best;
}

void UPSSkillPlayerAIComponent::ThrowTo(APSPlayerPawn* Self, APSPlayerPawn* Receiver)
{
    UPSBallActionComponent* BallAction = Self->GetBallActionComponent();
    APSBall* Ball = BallAction ? BallAction->GetCarriedBall() : nullptr;
    if (!Ball)
    {
        return;
    }

    // Lead the receiver by where he will be when the ball arrives.
    const FVector Lead = PSFieldReads::LeadPoint(Self->GetActorLocation(), Receiver, GetTuning().ThrowLeadSpeed);
    if (Self->ThrowPass(Ball, Lead, false, Receiver))
    {
        Action = EPSSkillPlayerAction::Idle;
    }
}

FVector UPSSkillPlayerAIComponent::SteerAlongRoute(APSPlayerPawn* Self)
{
    APSOffenseController* Controller = GetOffenseController();
    if (!Controller || Controller->GetRouteWaypointCount() == 0)
    {
        return FVector::ZeroVector;
    }

    const float Arrival = GetTuning().WaypointArrivalRadius;
    FVector Target = Controller->GetCurrentTargetLocation();
    if (FVector::Dist2D(Self->GetActorLocation(), Target) <= Arrival)
    {
        if (Controller->GetRouteWaypointIndex() + 1 < Controller->GetRouteWaypointCount())
        {
            Controller->AdvanceToNextWaypoint();
            Target = Controller->GetCurrentTargetLocation();
        }
        else
        {
            // Route run: the QB sets up to read, a run play's back waits for the ball, a
            // receiver settles where he is (still a target).
            const EPlayerRole Role = Self->GetAttributes().Role;
            if (Role == EPlayerRole::Quarterback)
            {
                Action = EPSSkillPlayerAction::ReadDefense;
            }
            else if (Role == EPlayerRole::RunningBack && bRunPlay)
            {
                Action = EPSSkillPlayerAction::WaitHandoff;
            }
            else
            {
                Action = EPSSkillPlayerAction::Idle;
            }
            return FVector::ZeroVector;
        }
    }
    return PSSkillPlayerAIPrivate::GroundDirection(Self->GetActorLocation(), Target);
}

FVector UPSSkillPlayerAIComponent::SteerAsCarrier(APSPlayerPawn* Self) const
{
    const FSkillPlayerAITuningRow& Settings = Tuning;
    const TArray<APSPlayerPawn*> Pawns = GetFieldPawns();
    const FVector Location = Self->GetActorLocation();

    // Upfield is +X. A run play's back still behind the line aims through the widest gap.
    FVector Heading(1.f, 0.f, 0.f);
    if (bRunPlay && Self->GetAttributes().Role == EPlayerRole::RunningBack && Location.X < LineOfScrimmage.X)
    {
        const FVector Gap = PSFieldReads::LargestRunLaneGap(Pawns);
        if (!Gap.IsZero())
        {
            Heading = PSSkillPlayerAIPrivate::GroundDirection(Location, Gap + Heading * Settings.WaypointArrivalRadius);
        }
    }

    // Veer away from the nearest defender, harder the closer he is.
    float Distance = TNumericLimits<float>::Max();
    const APSPlayerPawn* Defender = PSFieldReads::NearestOpponent(Pawns, Self->TeamSide, Location, &Distance);
    if (Defender && Distance < Settings.CarrierAvoidRadius && Settings.CarrierAvoidRadius > 0.f)
    {
        FVector Away = Location - Defender->GetActorLocation();
        Away.X = 0.f;
        Away.Z = 0.f;
        if (Away.IsNearlyZero())
        {
            Away = FVector(0.f, 1.f, 0.f);
        }
        const float Urgency = 1.f - Distance / Settings.CarrierAvoidRadius;
        Heading += Away.GetSafeNormal() * Settings.CarrierAvoidWeight * Urgency;
    }
    Heading.Z = 0.f;
    return Heading.GetSafeNormal();
}

FVector UPSSkillPlayerAIComponent::SteerAsBlocker(APSPlayerPawn* Self) const
{
    const FSkillPlayerAITuningRow& Settings = Tuning;
    const APSPlayerPawn* Quarterback = FindTeammate(EPlayerRole::Quarterback);
    if (!Quarterback)
    {
        return FVector::ZeroVector;
    }

    // Take on the rusher nearest the QB, else set up in front of him.
    float Distance = TNumericLimits<float>::Max();
    const APSPlayerPawn* Rusher = PSFieldReads::NearestOpponent(GetFieldPawns(), Self->TeamSide, Quarterback->GetActorLocation(), &Distance);
    if (Rusher && Distance <= Settings.BlockEngageRadius)
    {
        return PSSkillPlayerAIPrivate::GroundDirection(Self->GetActorLocation(), Rusher->GetActorLocation());
    }
    const FVector SetPoint = Quarterback->GetActorLocation() + FVector(Settings.BlockSetDistance, 0.f, 0.f);
    if (FVector::Dist2D(Self->GetActorLocation(), SetPoint) <= Settings.WaypointArrivalRadius)
    {
        return FVector::ZeroVector;
    }
    return PSSkillPlayerAIPrivate::GroundDirection(Self->GetActorLocation(), SetPoint);
}

APSOffenseController* UPSSkillPlayerAIComponent::GetOffenseController() const
{
    return Cast<APSOffenseController>(GetOwner());
}

APSPlayerPawn* UPSSkillPlayerAIComponent::GetSelf() const
{
    const APSOffenseController* Controller = GetOffenseController();
    return Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
}

APSPlayerPawn* UPSSkillPlayerAIComponent::FindTeammate(EPlayerRole Role) const
{
    const APSPlayerPawn* Self = GetSelf();
    if (!Self)
    {
        return nullptr;
    }
    for (APSPlayerPawn* Pawn : GetFieldPawns())
    {
        if (Pawn && Pawn != Self && Pawn->TeamSide == Self->TeamSide && Pawn->GetAttributes().Role == Role)
        {
            return Pawn;
        }
    }
    return nullptr;
}

TArray<APSPlayerPawn*> UPSSkillPlayerAIComponent::GetFieldPawns() const
{
    TArray<APSPlayerPawn*> Pawns;
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        Pawns.Add(*It);
    }
    return Pawns;
}
