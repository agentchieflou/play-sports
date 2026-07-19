#include "PSPlayerPawn.h"
#include "PSBallActionComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "AIController.h"
#include "PSGameMode.h"
#include "PSPlaySimulation.h"
#include "Engine/World.h"
#include "PSBall.h"
#include "Kismet/GameplayStatics.h"

APSPlayerPawn::APSPlayerPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleComp"));
    CapsuleComponent->InitCapsuleSize(44.f, 88.f);
    CapsuleComponent->SetCollisionProfileName(TEXT("Pawn"));
    RootComponent = CapsuleComponent;

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    MeshComponent->SetupAttachment(RootComponent);
    MeshComponent->SetCollisionProfileName(TEXT("NoCollision"));

    MovementComponent = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("MovementComp"));

    // C3: PossessionComponent owns possession state
    PossessionComponent = CreateDefaultSubobject<UPSPossessionComponent>(TEXT("PossessionComp"));

    // C3: BallActionComponent owns ball-action mechanics
    BallActionComponent = CreateDefaultSubobject<UPSBallActionComponent>(TEXT("BallActionComp"));

    bHasPossession = false;
    TeamSide = EPSTeamSide::Offense;

    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    BaseAcceleration = 2000.f;
    CurrentStamina = 100.f;
    MaxStamina = 100.f;
    bIsBursted = false;
    EngagedOpponent = nullptr;
    bIsEngaged = false;
    EngagementTime = 0.f;
    CachedGameMode = nullptr;
}

void APSPlayerPawn::BeginPlay()
{
    Super::BeginPlay();

    StartingLocation = GetActorLocation();

    if (CapsuleComponent)
    {
        CapsuleComponent->OnComponentBeginOverlap.AddDynamic(this, &APSPlayerPawn::OnPawnOverlap);
    }

    if (GetWorld())
    {
        CachedGameMode = Cast<APSGameMode>(GetWorld()->GetAuthGameMode());
    }
}

void APSPlayerPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // Engagement steering override
    if (bIsEngaged && EngagedOpponent)
    {
        // 1. Increment engagement time
        EngagementTime += DeltaSeconds;

        FVector SteerDir = EngagedOpponent->GetActorLocation() - GetActorLocation();
        SteerDir.Z = 0.f;
        if (!SteerDir.IsNearlyZero())
        {
            SteerDir.Normalize();
            AddMovementInput(SteerDir, 1.f);
        }

        // 2. Perform contested blocking push & pocket drift & block shedding from OL's tick to avoid duplicates
        if (TeamSide == EPSTeamSide::Offense)
        {
            float Dist = FVector::Dist(GetActorLocation(), EngagedOpponent->GetActorLocation());
            if (Dist <= 150.f)
            {
                FPlayerAttributes OLAttr = Attributes;
                FPlayerAttributes DLAttr = EngagedOpponent->GetAttributes();

                float OLPower = OLAttr.Strength * 0.4f + OLAttr.WeightKg * 0.4f + (GetVelocity().Size() * 0.2f);
                float DLPower = DLAttr.Strength * 0.4f + DLAttr.WeightKg * 0.4f + (EngagedOpponent->GetVelocity().Size() * 0.2f);

                float NetForce = OLPower - DLPower;

                FVector PushDir = EngagedOpponent->GetActorLocation() - GetActorLocation();
                PushDir.Z = 0.f;
                if (!PushDir.IsNearlyZero())
                {
                    PushDir.Normalize();
                }
                else
                {
                    PushDir = GetActorForwardVector();
                }

                if (MovementComponent && EngagedOpponent->GetFloatingMovementComponent())
                {
                    float ForceScale = DeltaSeconds * 100.f;
                    if (NetForce > 0.f)
                    {
                        // OL pushes DL back
                        EngagedOpponent->GetFloatingMovementComponent()->Velocity += PushDir * (NetForce * 0.05f * ForceScale);
                        MovementComponent->Velocity += PushDir * (NetForce * 0.02f * ForceScale);
                    }
                    else
                    {
                        // DL pushes OL back
                        MovementComponent->Velocity += (-PushDir) * (-NetForce * 0.05f * ForceScale);
                        EngagedOpponent->GetFloatingMovementComponent()->Velocity += (-PushDir) * (-NetForce * 0.02f * ForceScale);
                    }

                    // 3. Pocket formation: add a backward drift to offensive lineman during block
                    MovementComponent->Velocity += FVector(-60.f, 0.f, 0.f) * DeltaSeconds;
                }
            }

            // 4. Block shedding contest: DL tries to break block
            if (EngagementTime >= 1.0f)
            {
                FPlayerAttributes OLAttr = Attributes;
                FPlayerAttributes DLAttr = EngagedOpponent->GetAttributes();

                float ShedChance = 0.05f + (DLAttr.Strength + DLAttr.Agility - OLAttr.Strength) * 0.002f;
                ShedChance = FMath::Clamp(ShedChance, 0.01f, 0.20f);

                if (FMath::FRand() <= ShedChance * DeltaSeconds * 10.f)
                {
                    UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Block SHED! Defender %s broke free from OL %s after %.2fs."), 
                        *DLAttr.DisplayName, *OLAttr.DisplayName, EngagementTime);

                    // Add forward shed speed boost to defender
                    if (EngagedOpponent->GetFloatingMovementComponent())
                    {
                        EngagedOpponent->GetFloatingMovementComponent()->Velocity += EngagedOpponent->GetActorForwardVector() * 200.f;
                    }

                    // Clear engagement status
                    EngagedOpponent->bIsEngaged = false;
                    EngagedOpponent->EngagedOpponent = nullptr;
                    bIsEngaged = false;
                    EngagedOpponent = nullptr;
                }
            }
        }
    }
    // Defender pursuit steering behavior
    else if (TeamSide == EPSTeamSide::Defense && !IsPlayerControlled())
    {
        APSPlayerPawn* BallCarrier = nullptr;
        APSGameMode* GM = Cast<APSGameMode>(GetWorld()->GetAuthGameMode());
        if (GM && GM->ActiveBall)
        {
            BallCarrier = Cast<APSPlayerPawn>(GM->ActiveBall->GetAttachParentActor());
        }

        if (BallCarrier)
        {
            FVector TargetLoc = BallCarrier->GetActorLocation();
            FVector TargetVel = BallCarrier->GetVelocity();

            FVector SeekDir = TargetLoc - GetActorLocation();
            float Distance = SeekDir.Size();
            float MaxSpeed = MovementComponent ? MovementComponent->MaxSpeed : 600.f;
            float PredictionTime = (MaxSpeed > 0.f) ? (Distance / MaxSpeed) : 0.f;
            PredictionTime = FMath::Min(PredictionTime, 1.0f);

            FVector PursueTarget = TargetLoc + TargetVel * PredictionTime;
            FVector SteerDir = PursueTarget - GetActorLocation();
            SteerDir.Z = 0.f;
            if (!SteerDir.IsNearlyZero())
            {
                SteerDir.Normalize();
                AddMovementInput(SteerDir, 1.f);
            }
        }
    }

    if (MovementComponent)
    {
        float CurrentSpeed = MovementComponent->Velocity.Size();
        float MaxSpeed = MovementComponent->MaxSpeed;

        static const FMovementTuningRow DefaultTuning;
        const FMovementTuningRow& Tuning = CachedGameMode ? CachedGameMode->MovementTuningSettings : DefaultTuning;

        if (MaxSpeed > 0.f)
        {
            float SpeedRatio = FMath::Clamp(CurrentSpeed / MaxSpeed, 0.f, 1.f);
            
            // Asymptotic acceleration curve: tapers off quadratically as speed reaches max
            float CurveMultiplier = 1.f - (SpeedRatio * SpeedRatio);
            
            // Ensure a minimum acceleration (10% of base) so top speed is always reached
            float ActiveAcceleration = BaseAcceleration * FMath::Max(0.1f, CurveMultiplier);
            MovementComponent->Acceleration = ActiveAcceleration;

            // Deceleration is also physically scaled with base acceleration
            MovementComponent->Deceleration = BaseAcceleration * Tuning.DecelerationMultiplier;
        }

        // Turning / change-of-direction mechanics
        if (!MovementComponent->Velocity.IsNearlyZero())
        {
            // 1. Smooth rotation alignment with movement direction on Yaw axis
            FRotator CurrentRotation = GetActorRotation();
            FRotator TargetRotation = MovementComponent->Velocity.Rotation();
            TargetRotation.Pitch = 0.f;
            TargetRotation.Roll = 0.f;

            float BaseTurnRate = Tuning.BaseTurnRateMin + (Attributes.Agility * Tuning.BaseTurnRateMaxMultiplier);
            float WeightFactor = Tuning.WeightTurnRateReference / FMath::Max(Tuning.WeightMin, Attributes.WeightKg);
            float TurnSpeed = BaseTurnRate * WeightFactor;

            // Interpolate rotation (TurnSpeed / 50.f maps TurnSpeed in deg/s to RInterpTo speed constant)
            FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaSeconds, TurnSpeed / 50.f);
            SetActorRotation(NewRotation);

            // 2. Change of direction velocity cost (cutting penalty)
            FVector DesiredDir = MovementComponent->GetPendingInputVector();
            if (!DesiredDir.IsNearlyZero())
            {
                DesiredDir.Normalize();
                FVector CurrentDir = MovementComponent->Velocity;
                CurrentDir.Normalize();

                float DotProduct = FVector::DotProduct(CurrentDir, DesiredDir);
                if (DotProduct < Tuning.CutDivergenceThreshold)
                {
                    // Calculate a minimum speed multiplier (high agility/low weight preserves more speed)
                    float MinSpeedMultiplier = Tuning.CutSpeedMinMultiplierBase + (Attributes.Agility * Tuning.CutSpeedAgilityMultiplier) - (Attributes.WeightKg * Tuning.CutSpeedWeightMultiplier);
                    MinSpeedMultiplier = FMath::Clamp(MinSpeedMultiplier, 0.2f, 0.95f);

                    // Interpolate speed penalty based on sharpness of the cut
                    float Sharpness = 0.f;
                    float Range = -1.0f - Tuning.CutDivergenceThreshold;
                    if (Range != 0.f)
                    {
                        Sharpness = FMath::Clamp((DotProduct - Tuning.CutDivergenceThreshold) / Range, 0.f, 1.f);
                    }
                    float PenaltyFactor = FMath::Lerp(1.0f, MinSpeedMultiplier, Sharpness);

                    // Apply the cutting deceleration directly to current velocity
                    MovementComponent->Velocity *= PenaltyFactor;

                    if (DotProduct < 0.0f)
                    {
                        UE_LOG(LogTemp, Verbose, TEXT("APSPlayerPawn: Cut detected (Dot: %.2f, Speed penalty factor: %.2f)"), DotProduct, PenaltyFactor);
                    }
                }
            }
        }
    }
}

void APSPlayerPawn::InitializePlayer(const FPlayerAttributes& InAttributes)
{
    Attributes = InAttributes;
    bHasPossession = false;

    if (!CachedGameMode && GetWorld())
    {
        CachedGameMode = Cast<APSGameMode>(GetWorld()->GetAuthGameMode());
    }

    static const FMovementTuningRow DefaultTuning;
    const FMovementTuningRow& Tuning = CachedGameMode ? CachedGameMode->MovementTuningSettings : DefaultTuning;

    // Scale MaxSpeed linearly based on Speed attribute
    float SpeedRange = Tuning.BaseMaxSpeedMax - Tuning.BaseMaxSpeedMin;
    float ScaledMaxSpeed = Tuning.BaseMaxSpeedMin + (Attributes.Speed * (SpeedRange / 100.f));
    if (MovementComponent)
    {
        MovementComponent->MaxSpeed = ScaledMaxSpeed;
    }

    // Scale BaseAcceleration from Acceleration attribute
    float AccelRange = Tuning.BaseAccelerationMax - Tuning.BaseAccelerationMin;
    BaseAcceleration = Tuning.BaseAccelerationMin + (Attributes.Acceleration * (AccelRange / 100.f));

    // Initialize stamina from attributes
    MaxStamina = Attributes.Stamina;
    CurrentStamina = MaxStamina;

    // Log player pawn initialization details
    FString RoleName = UEnum::GetValueAsString(Attributes.Role);
    UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Initialized player pawn for %s (ID: %s, Role: %s, Height: %.1f cm, Weight: %.1f kg, MaxSpeed: %.1f cm/s, BaseAccel: %.1f cm/s^2, Stamina: %.1f)"), 
        *Attributes.DisplayName, 
        *Attributes.PlayerId.ToString(), 
        *RoleName, 
        Attributes.HeightCm, 
        Attributes.WeightKg,
        ScaledMaxSpeed,
        BaseAcceleration,
        MaxStamina);
}

void APSPlayerPawn::InitializePlayerPointer(const FPlayerAttributes* InAttributes)
{
    AttributesPtr = InAttributes;
    if (InAttributes)
    {
        InitializePlayer(*InAttributes);
    }
}

void APSPlayerPawn::MoveToLocation(const FVector& TargetLocation)
{
    if (AAIController* AIC = Cast<AAIController>(GetController()))
    {
        AIC->MoveToLocation(TargetLocation);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: MoveToLocation failed - Pawn has no AIController."));
    }
}

void APSPlayerPawn::GainPossession()
{
    if (!bHasPossession)
    {
        bHasPossession = true;
        if (PossessionComponent)
        {
            PossessionComponent->GainPossession();
        }
        UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Player %s (ID: %s) gained ball possession."),
            *Attributes.DisplayName,
            *Attributes.PlayerId.ToString());
    }
}

void APSPlayerPawn::LosePossession()
{
    if (bHasPossession)
    {
        bHasPossession = false;
        if (PossessionComponent)
        {
            PossessionComponent->LosePossession();
        }
        UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Player %s (ID: %s) lost ball possession."),
            *Attributes.DisplayName,
            *Attributes.PlayerId.ToString());
    }
}

bool APSPlayerPawn::TransferPossessionTo(APSPlayerPawn* TargetPlayerPawn)
{
    if (!bHasPossession)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: Transfer possession failed - %s does not have the ball."),
            *Attributes.DisplayName);
        return false;
    }

    if (!TargetPlayerPawn)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: Transfer possession failed - Target player pawn is null."));
        return false;
    }

    // Delegate to component; pawn Gain/LosePossession keeps bHasPossession in sync
    if (PossessionComponent)
    {
        PossessionComponent->TransferPossessionTo(TargetPlayerPawn);
        bHasPossession = false; // this pawn lost it
    }
    else
    {
        LosePossession();
        TargetPlayerPawn->GainPossession();
    }

    UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Transferred ball possession from %s to %s."),
        *Attributes.DisplayName,
        *TargetPlayerPawn->GetAttributes().DisplayName);

    return true;
}

void APSPlayerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &APSPlayerPawn::MoveForward);
    PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &APSPlayerPawn::MoveRight);
}

void APSPlayerPawn::MoveForward(float Value)
{
    if (Value != 0.0f)
    {
        AddMovementInput(GetActorForwardVector(), Value);
    }
}

void APSPlayerPawn::MoveRight(float Value)
{
    if (Value != 0.0f)
    {
        AddMovementInput(GetActorRightVector(), Value);
    }
}

FVector APSPlayerPawn::GetMomentum() const
{
    if (MovementComponent)
    {
        // Convert velocity from cm/s to m/s
        FVector VelocityInMps = MovementComponent->Velocity / 100.f;
        return Attributes.WeightKg * VelocityInMps;
    }
    return FVector::ZeroVector;
}

float APSPlayerPawn::GetMomentumMagnitude() const
{
    return GetMomentum().Size();
}

void APSPlayerPawn::UseBurst(bool bEnable)
{
    bIsBursted = bEnable;
    UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: UseBurst set to %s for %s."), bEnable ? TEXT("true") : TEXT("false"), *Attributes.DisplayName);
}

void APSPlayerPawn::ApplyFatigue(float Amount)
{
    CurrentStamina = FMath::Clamp(CurrentStamina - Amount, 0.f, MaxStamina);
    UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Applied %.1f fatigue to %s. CurrentStamina: %.1f/%.1f"), Amount, *Attributes.DisplayName, CurrentStamina, MaxStamina);
}

void APSPlayerPawn::ResetFatigue()
{
    CurrentStamina = MaxStamina;
    UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Reset fatigue for %s. CurrentStamina: %.1f/%.1f"), *Attributes.DisplayName, CurrentStamina, MaxStamina);
}

bool APSPlayerPawn::ThrowPass(APSBall* Ball, const FVector& TargetLocation, bool bHighArc)
{
    return BallActionComponent ? BallActionComponent->ThrowPass(Ball, TargetLocation, bHighArc) : false;
}

bool APSPlayerPawn::ExecuteHandoff(APSPlayerPawn* TargetPlayer)
{
    return BallActionComponent ? BallActionComponent->ExecuteHandoff(TargetPlayer) : false;
}

bool APSPlayerPawn::ExecutePitch(APSPlayerPawn* TargetPlayer)
{
    return BallActionComponent ? BallActionComponent->ExecutePitch(TargetPlayer) : false;
}

bool APSPlayerPawn::ExecuteKick(APSBall* Ball, float KickPower, float LaunchAngle)
{
    return BallActionComponent ? BallActionComponent->ExecuteKick(Ball, KickPower, LaunchAngle) : false;
}

void APSPlayerPawn::FumbleBall()
{
    if (BallActionComponent)
    {
        BallActionComponent->FumbleBall();
    }
}

bool APSPlayerPawn::ResolveTackle(APSPlayerPawn* Defender)
{
    return BallActionComponent ? BallActionComponent->ResolveTackle(Defender) : false;
}
