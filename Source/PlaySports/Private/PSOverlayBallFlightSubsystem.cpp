#include "PSOverlayBallFlightSubsystem.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSKickMeterComponent.h"
#include "PSLocalization.h"
#include "PSOverlayBallFlight.h"
#include "PSOverlayBallFlightActor.h"
#include "PSPlayerPawn.h"
#include "PSUITeamCatalog.h"
#include "Components/SphereComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

void UPSOverlayBallFlightSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    LoadStyleFromJson(GetDefaultStylePath());
    SetOverlayDetail(PSPlatformTiers::GetActiveTier().OverlayDetail);

    if (UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>())
    {
        Bus->OnThrowMC.AddUObject(this, &UPSOverlayBallFlightSubsystem::HandleThrow);
        Bus->OnCatchMC.AddUObject(this, &UPSOverlayBallFlightSubsystem::HandleCatch);
        Bus->OnGameStateMC.AddUObject(this, &UPSOverlayBallFlightSubsystem::HandleGameState);
        BoundBus = Bus;
    }
}

void UPSOverlayBallFlightSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnThrowMC.RemoveAll(this);
        Bus->OnCatchMC.RemoveAll(this);
        Bus->OnGameStateMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSOverlayBallFlightSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSOverlayBallFlightSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AdvanceTime(DeltaTime);
}

TStatId UPSOverlayBallFlightSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSOverlayBallFlightSubsystem, STATGROUP_Tickables);
}

FString UPSOverlayBallFlightSubsystem::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/ball_flight_overlay.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSOverlayBallFlightSubsystem::LoadStyleFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSBallFlightStyle Loaded;
    if (!Ingestion->LoadBallFlightStyleFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOverlayBallFlightSubsystem: Could not load the ball-flight style from %s; keeping the current one."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSOverlayBallFlightSubsystem: %s: %s"), *JsonFilePath, *Problem);
        }
        return false;
    }
    SetStyle(Loaded);
    return true;
}

void UPSOverlayBallFlightSubsystem::SetStyle(const FPSBallFlightStyle& NewStyle)
{
    Style = NewStyle;
    if (APSOverlayBallFlight* Actor = OverlayActor.Get())
    {
        Actor->ApplyStyle(Style);
    }
    Redraw();
}

TArray<FString> UPSOverlayBallFlightSubsystem::ValidateStyle(const FPSBallFlightStyle& InStyle)
{
    TArray<FString> Problems;

    struct FNamedText
    {
        const TCHAR* Name;
        const FString* Value;
    };
    const FNamedText Colors[] = {
        { TEXT("ArcColor"), &InStyle.ArcColor },
        { TEXT("LandingColor"), &InStyle.LandingColor },
        { TEXT("LeadOnTargetColor"), &InStyle.LeadOnTargetColor },
        { TEXT("LeadOffTargetColor"), &InStyle.LeadOffTargetColor },
        { TEXT("GoodColor"), &InStyle.GoodColor },
        { TEXT("NoGoodColor"), &InStyle.NoGoodColor },
    };
    for (const FNamedText& Color : Colors)
    {
        FLinearColor Parsed;
        if (!UPSUITeamCatalog::ParseHexColor(*Color.Value, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("%s: '%s' must be #RRGGBB"), Color.Name, **Color.Value));
        }
    }

    const FNamedText Required[] = {
        { TEXT("DotMeshPath"), &InStyle.DotMeshPath },
        { TEXT("RingMeshPath"), &InStyle.RingMeshPath },
        { TEXT("MaterialPath"), &InStyle.MaterialPath },
        { TEXT("GoodLabel"), &InStyle.GoodLabel },
        { TEXT("WideLeftLabel"), &InStyle.WideLeftLabel },
        { TEXT("WideRightLabel"), &InStyle.WideRightLabel },
        { TEXT("ShortLabel"), &InStyle.ShortLabel },
    };
    for (const FNamedText& Text : Required)
    {
        if (Text.Value->IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("%s: must not be empty"), Text.Name));
        }
    }
    if (InStyle.ColorParameter.IsNone())
    {
        Problems.Add(TEXT("ColorParameter: must name the material's color parameter"));
    }

    struct FNamedNumber
    {
        const TCHAR* Name;
        float Value;
        bool bZeroAllowed;
    };
    const FNamedNumber Numbers[] = {
        { TEXT("MeshDiameter"), InStyle.MeshDiameter, false },
        { TEXT("ArcDotDiameter"), InStyle.ArcDotDiameter, false },
        { TEXT("RingThickness"), InStyle.RingThickness, true },
        { TEXT("GroundClearance"), InStyle.GroundClearance, true },
        { TEXT("LandingRadiusFallback"), InStyle.LandingRadiusFallback, false },
        { TEXT("LeadRadius"), InStyle.LeadRadius, false },
        { TEXT("DeviationTolerance"), InStyle.DeviationTolerance, false },
        { TEXT("MaxFlightSeconds"), InStyle.MaxFlightSeconds, false },
        { TEXT("LingerSeconds"), InStyle.LingerSeconds, true },
        { TEXT("ReadoutSeconds"), InStyle.ReadoutSeconds, true },
        { TEXT("UprightWidth"), InStyle.UprightWidth, false },
        { TEXT("CrossbarHeight"), InStyle.CrossbarHeight, true },
        { TEXT("ReadoutHeight"), InStyle.ReadoutHeight, true },
        { TEXT("ReadoutTextSize"), InStyle.ReadoutTextSize, false },
    };
    for (const FNamedNumber& Number : Numbers)
    {
        if (Number.bZeroAllowed ? Number.Value < 0.f : Number.Value <= 0.f)
        {
            Problems.Add(FString::Printf(TEXT("%s: %.2f must be %s"), Number.Name, Number.Value, Number.bZeroAllowed ? TEXT("0 or more") : TEXT("above 0")));
        }
    }

    if (InStyle.ArcPoints < 2)
    {
        Problems.Add(FString::Printf(TEXT("ArcPoints: %d must be 2 or more (release and landing)"), InStyle.ArcPoints));
    }
    if (InStyle.GoalPostX.Num() == 0)
    {
        Problems.Add(TEXT("GoalPostX: no goal posts to judge kicks at"));
    }
    return Problems;
}

FString UPSOverlayBallFlightSubsystem::LocalizedKickLabel(const FPSBallFlightStyle& InStyle, EPSKickVerdict Verdict)
{
    // The readout's words are the style's (Data/ball_flight_overlay.json), through the generated
    // data table (Epic 106).
    const TCHAR* Field = nullptr;
    switch (Verdict)
    {
    case EPSKickVerdict::Good:      Field = TEXT("GoodLabel"); break;
    case EPSKickVerdict::WideLeft:  Field = TEXT("WideLeftLabel"); break;
    case EPSKickVerdict::WideRight: Field = TEXT("WideRightLabel"); break;
    case EPSKickVerdict::Short:     Field = TEXT("ShortLabel"); break;
    default:                        return FString();
    }
    return UPSLocalization::GetDataText(FString::Printf(TEXT("BallFlight.%s"), Field), InStyle.LabelFor(Verdict)).ToString();
}

void UPSOverlayBallFlightSubsystem::SetOverlayDetail(EPSOverlayDetail InDetail)
{
    OverlayDetail = InDetail;
    Redraw();
}

bool UPSOverlayBallFlightSubsystem::IsInFreeFlight(const APSBall& Ball)
{
    const UProjectileMovementComponent* Movement = Ball.GetProjectileMovement();
    return !Ball.bIsFumbled && Ball.GetAttachParentActor() == nullptr && Movement && Movement->IsActive()
        && Movement->UpdatedComponent != nullptr && !Movement->Velocity.IsNearlyZero();
}

bool UPSOverlayBallFlightSubsystem::TrackFlight(APSBall* Ball, EPSBallFlightKind Kind, const FString& ReceiverName, float LandingHeight)
{
    if (!IsValid(Ball) || Kind == EPSBallFlightKind::None || !IsInFreeFlight(*Ball))
    {
        return false;
    }

    // The ball's own physics: its projectile velocity (held under the movement's MaxSpeed, as
    // the movement holds it) and the gravity the movement flies it under.
    const UProjectileMovementComponent* Movement = Ball->GetProjectileMovement();
    FVector Velocity = Movement->Velocity;
    if (Movement->GetMaxSpeed() > 0.f)
    {
        Velocity = Velocity.GetClampedToMaxSize(Movement->GetMaxSpeed());
    }

    TrackedBall = Ball;
    Flight = FPSBallFlightState();
    Flight.Kind = Kind;
    Flight.bActive = true;
    Flight.bVisible = true;
    Flight.Prediction = PSOverlayBallFlight::Predict(Kind, Ball->GetActorLocation(), Velocity, Movement->GetGravityZ(), LandingHeight,
        Style.GroundZ, Style.ArcPoints, Style.MaxFlightSeconds);

    Receiver = Kind == EPSBallFlightKind::Pass ? FindPawn(ReceiverName) : nullptr;
    Flight.LandingRadius = CatchRadiusFor(Receiver.Get(), *Ball);
    Flight.Lead.ReceiverName = Kind == EPSBallFlightKind::Pass ? ReceiverName : FString();
    if (Kind == EPSBallFlightKind::Kick)
    {
        Flight.Kick = PSOverlayBallFlight::JudgeKick(Flight.Prediction, Style);
        Flight.Kick.Label = LocalizedKickLabel(Style, Flight.Kick.Verdict);
    }
    FlightClock = 0.f;
    LingerRemaining = 0.f;
    UpdateLead();
    Redraw();
    return true;
}

void UPSOverlayBallFlightSubsystem::AdvanceTime(float DeltaSeconds)
{
    const float Step = FMath::Max(DeltaSeconds, 0.f);
    APSBall* Ball = Flight.bActive ? TrackedBall.Get() : FindBall();
    const bool bInFlight = IsValid(Ball) && IsInFreeFlight(*Ball);

    if (Flight.bActive)
    {
        FlightClock += Step;
        if (!bInFlight)
        {
            // Caught, carried or stopped.
            EndFlight();
        }
        else
        {
            const FVector Location = Ball->GetActorLocation();
            const float Along = PSOverlayBallFlight::ProgressSeconds(Flight.Prediction, Location, FlightClock);
            if (PSOverlayBallFlight::DeviationAt(Flight.Prediction, Location, Along) > Style.DeviationTolerance)
            {
                // Touched, deflected or bounced: the prediction no longer holds.
                EndFlight();
            }
            else
            {
                Flight.ElapsedSeconds = FMath::Min(Along, Flight.Prediction.LandingSeconds);
                if (Flight.Prediction.bLands && Along >= Flight.Prediction.LandingSeconds)
                {
                    EndFlight();
                }
                else
                {
                    UpdateLead();
                }
            }
        }
    }
    else if (bInFlight && !bBallWasInFlight && UPSKickMeterComponent::IsKickPhase(Phase))
    {
        // The ball has just left its carrier during a kick.
        TrackFlight(Ball, EPSBallFlightKind::Kick, FString(), Style.GroundZ);
    }
    else if (Flight.bVisible)
    {
        LingerRemaining -= Step;
        if (LingerRemaining <= 0.f)
        {
            Flight.bVisible = false;
        }
    }

    bBallWasInFlight = bInFlight;
    Redraw();
}

void UPSOverlayBallFlightSubsystem::HandleThrow(const FPSTelemetryThrowEvent& Event)
{
    // Published as the pass leaves the passer's hand: the ball is already launched.
    APSBall* Ball = FindBall();
    if (Ball && TrackFlight(Ball, EPSBallFlightKind::Pass, Event.TargetReceiverName, Event.LandingLocation.Z))
    {
        bBallWasInFlight = true;
    }
}

void UPSOverlayBallFlightSubsystem::HandleCatch(const FPSTelemetryCatchEvent& Event)
{
    if (Flight.bActive)
    {
        EndFlight();
        Redraw();
    }
}

void UPSOverlayBallFlightSubsystem::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    Phase = Event.Phase;
}

void UPSOverlayBallFlightSubsystem::EndFlight()
{
    Flight.bActive = false;
    Receiver.Reset();
    LingerRemaining = Flight.Kind == EPSBallFlightKind::Kick ? Style.ReadoutSeconds : Style.LingerSeconds;
    if (LingerRemaining <= 0.f)
    {
        Flight.bVisible = false;
    }
}

void UPSOverlayBallFlightSubsystem::UpdateLead()
{
    const APSPlayerPawn* Target = Receiver.Get();
    if (Flight.Kind != EPSBallFlightKind::Pass || !IsValid(Target) || !Flight.Prediction.bLands)
    {
        Flight.Lead.bValid = false;
        return;
    }

    const UFloatingPawnMovement* Movement = Target->GetFloatingMovementComponent();
    const FVector Velocity = Movement ? Movement->Velocity : Target->GetVelocity();
    const FString ReceiverName = Flight.Lead.ReceiverName;
    Flight.Lead = PSOverlayBallFlight::ComputeLead(Target->GetActorLocation(), Velocity, Flight.Prediction.LandingSeconds - Flight.ElapsedSeconds,
        Flight.Prediction.LandingLocation, Flight.LandingRadius, Style.GroundZ);
    Flight.Lead.ReceiverName = ReceiverName;
}

void UPSOverlayBallFlightSubsystem::Redraw()
{
    APSOverlayBallFlight* Actor = OverlayActor.Get();
    if (!Actor)
    {
        UWorld* World = GetWorld();
        if (!Flight.bVisible || !World)
        {
            return;
        }
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnParams.ObjectFlags |= RF_Transient;
        Actor = World->SpawnActor<APSOverlayBallFlight>(APSOverlayBallFlight::StaticClass(), FTransform::Identity, SpawnParams);
        if (!Actor)
        {
            return;
        }
        Actor->ApplyStyle(Style);
        OverlayActor = Actor;
    }
    if (Flight.bVisible || !Actor->IsHidden())
    {
        Actor->ShowFlight(Flight, OverlayDetail);
    }
}

APSBall* UPSOverlayBallFlightSubsystem::FindBall()
{
    APSBall* Cached = TrackedBall.Get();
    if (IsValid(Cached) && IsInFreeFlight(*Cached))
    {
        return Cached;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }
    // The ball in flight if there is one; otherwise the one already known, or the first.
    APSBall* First = nullptr;
    for (TActorIterator<APSBall> It(World); It; ++It)
    {
        APSBall* Candidate = *It;
        if (!IsValid(Candidate))
        {
            continue;
        }
        if (IsInFreeFlight(*Candidate))
        {
            TrackedBall = Candidate;
            return Candidate;
        }
        if (!First)
        {
            First = Candidate;
        }
    }
    if (IsValid(Cached))
    {
        return Cached;
    }
    TrackedBall = First;
    return First;
}

APSPlayerPawn* UPSOverlayBallFlightSubsystem::FindPawn(const FString& DisplayName) const
{
    UWorld* World = GetWorld();
    if (!World || DisplayName.IsEmpty())
    {
        return nullptr;
    }
    for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
    {
        if (IsValid(*It) && It->GetAttributes().DisplayName == DisplayName)
        {
            return *It;
        }
    }
    return nullptr;
}

float UPSOverlayBallFlightSubsystem::CatchRadiusFor(const APSPlayerPawn* InReceiver, const APSBall& Ball) const
{
    // The ball is caught when it touches him: his capsule's radius plus the ball's.
    const float ReceiverRadius = IsValid(InReceiver) ? InReceiver->GetSimpleCollisionRadius() : 0.f;
    if (ReceiverRadius <= 0.f)
    {
        return Style.LandingRadiusFallback;
    }
    const USphereComponent* BallCollision = Ball.GetCollisionComponent();
    return ReceiverRadius + (BallCollision ? BallCollision->GetScaledSphereRadius() : 0.f);
}
