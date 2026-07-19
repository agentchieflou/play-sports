#include "PSPlayerPawn.h"
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

    bHasPossession = false;
    TeamSide = EPSTeamSide::Offense;

    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    BaseAcceleration = 2000.f;
    CurrentStamina = 100.f;
    MaxStamina = 100.f;
    bIsBursted = false;
    EngagedOpponent = nullptr;
    bIsEngaged = false;
}

void APSPlayerPawn::BeginPlay()
{
    Super::BeginPlay();

    StartingLocation = GetActorLocation();

    if (CapsuleComponent)
    {
        CapsuleComponent->OnComponentBeginOverlap.AddDynamic(this, &APSPlayerPawn::OnPawnOverlap);
    }
}

void APSPlayerPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // Engagement steering override
    if (bIsEngaged && EngagedOpponent)
    {
        FVector SteerDir = EngagedOpponent->GetActorLocation() - GetActorLocation();
        SteerDir.Z = 0.f;
        if (!SteerDir.IsNearlyZero())
        {
            SteerDir.Normalize();
            AddMovementInput(SteerDir, 1.f);
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

        FMovementTuningRow Tuning;
        if (GetWorld())
        {
            if (APSGameMode* GM = Cast<APSGameMode>(GetWorld()->GetAuthGameMode()))
            {
                Tuning = GM->MovementTuningSettings;
            }
        }

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

    FMovementTuningRow Tuning;
    if (GetWorld())
    {
        if (APSGameMode* GM = Cast<APSGameMode>(GetWorld()->GetAuthGameMode()))
        {
            Tuning = GM->MovementTuningSettings;
        }
    }

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

    LosePossession();
    TargetPlayerPawn->GainPossession();

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
    if (!Ball)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: ThrowPass failed - Ball is null."));
        return false;
    }

    if (!bHasPossession)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: ThrowPass failed - Player %s does not have the ball."), *Attributes.DisplayName);
        return false;
    }

    // LaunchSpeed is scaled by Strength (0-100 rating -> 1500 to 3000 cm/s launch speed)
    float LaunchSpeed = 1500.f + (Attributes.Strength * 15.f);

    // Apply accuracy scatter to the target point based on Awareness (lower awareness = more error)
    FVector ScatterTarget = TargetLocation;
    if (Attributes.Awareness < 100.f)
    {
        float AccuracyError = (100.f - Attributes.Awareness) * 2.f; // Max error up to 200cm
        FVector ErrorOffset = FMath::VRand() * FMath::FRandRange(0.f, AccuracyError);
        ErrorOffset.Z = 0.f; // Keep error on 2D plane
        ScatterTarget += ErrorOffset;
    }

    FVector OutVelocity = FVector::ZeroVector;
    FVector StartLocation = GetActorLocation() + FVector(0.f, 0.f, 50.f); // Throw from hand/chest height

    bool bSuccess = UGameplayStatics::SuggestProjectileVelocity(
        this,
        OutVelocity,
        StartLocation,
        ScatterTarget,
        LaunchSpeed,
        bHighArc,
        0.f,
        0.f,
        ESuggestProjVelocityTraceOption::DoNotTrace
    );

    if (bSuccess)
    {
        Ball->Launch(OutVelocity);
        LosePossession();
        UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Player %s (ID: %s) threw a pass to %s. Launch velocity: %s"), 
            *Attributes.DisplayName, 
            *Attributes.PlayerId.ToString(), 
            *TargetLocation.ToString(), 
            *OutVelocity.ToString());
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: ThrowPass failed - Target is out of range for launch speed %.1f."), LaunchSpeed);
        return false;
    }
}

bool APSPlayerPawn::ExecuteHandoff(APSPlayerPawn* TargetPlayer)
{
    if (!TargetPlayer)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: ExecuteHandoff failed - TargetPlayer is null."));
        return false;
    }

    if (!bHasPossession)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: ExecuteHandoff failed - Player %s does not have the ball."), *Attributes.DisplayName);
        return false;
    }

    float Distance = FVector::Dist(GetActorLocation(), TargetPlayer->GetActorLocation());
    if (Distance > 200.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: ExecuteHandoff failed - TargetPlayer %s is out of range (%.1f > 200 cm)."), *TargetPlayer->GetAttributes().DisplayName, Distance);
        return false;
    }

    APSGameMode* GM = Cast<APSGameMode>(GetWorld()->GetAuthGameMode());
    if (GM && GM->ActiveBall)
    {
        if (TransferPossessionTo(TargetPlayer))
        {
            GM->ActiveBall->AttachToCarrier(TargetPlayer, TEXT("HandSocket"));
            UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Executed handoff from %s to %s."), *Attributes.DisplayName, *TargetPlayer->GetAttributes().DisplayName);
            return true;
        }
    }
    return false;
}

bool APSPlayerPawn::ExecutePitch(APSPlayerPawn* TargetPlayer)
{
    if (!TargetPlayer)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: ExecutePitch failed - TargetPlayer is null."));
        return false;
    }

    if (!bHasPossession)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: ExecutePitch failed - Player %s does not have the ball."), *Attributes.DisplayName);
        return false;
    }

    APSGameMode* GM = Cast<APSGameMode>(GetWorld()->GetAuthGameMode());
    if (!GM || !GM->ActiveBall)
    {
        return false;
    }

    FVector OutVelocity = FVector::ZeroVector;
    FVector StartLocation = GetActorLocation() + FVector(0.f, 0.f, 50.f);
    FVector TargetLocation = TargetPlayer->GetActorLocation() + FVector(0.f, 0.f, 50.f);

    float PitchSpeed = 1000.f;

    bool bSuccess = UGameplayStatics::SuggestProjectileVelocity(
        this,
        OutVelocity,
        StartLocation,
        TargetLocation,
        PitchSpeed,
        false,
        0.f,
        0.f,
        ESuggestProjVelocityTraceOption::DoNotTrace
    );

    if (bSuccess)
    {
        GM->ActiveBall->Launch(OutVelocity);
        LosePossession();
        UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Executed lateral pitch from %s to %s. Launch velocity: %s"), 
            *Attributes.DisplayName, 
            *TargetPlayer->GetAttributes().DisplayName, 
            *OutVelocity.ToString());
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: ExecutePitch failed - Target is out of range for pitch speed %.1f."), PitchSpeed);
        return false;
    }
}

void APSPlayerPawn::FumbleBall()
{
    if (!bHasPossession)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSPlayerPawn: FumbleBall failed - Player %s does not have the ball."), *Attributes.DisplayName);
        return;
    }

    APSGameMode* GM = Cast<APSGameMode>(GetWorld()->GetAuthGameMode());
    if (GM && GM->ActiveBall)
    {
        FVector FumbleVelocity = GetActorForwardVector() * 300.f + FVector(0.f, 0.f, 200.f);
        FumbleVelocity += FMath::VRand() * 100.f;
        FumbleVelocity.Z = FMath::Max(50.f, FumbleVelocity.Z);

        GM->ActiveBall->Fumble(FumbleVelocity);
        LosePossession();
        UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Player %s (ID: %s) fumbled the ball!"), *Attributes.DisplayName, *Attributes.PlayerId.ToString());
    }
}

void APSPlayerPawn::OnPawnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (APSPlayerPawn* OtherPawn = Cast<APSPlayerPawn>(OtherActor))
    {
        if (bHasPossession && OtherPawn->TeamSide != TeamSide)
        {
            UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Contact detected! Ball carrier %s contacted by defender %s."), *Attributes.DisplayName, *OtherPawn->GetAttributes().DisplayName);
            ResolveTackle(OtherPawn);
        }
    }
}

bool APSPlayerPawn::ResolveTackle(APSPlayerPawn* Defender)
{
    if (!Defender)
    {
        return false;
    }

    APSGameMode* GM = Cast<APSGameMode>(GetWorld()->GetAuthGameMode());
    if (!GM)
    {
        return false;
    }

    FPlayerAttributes CarrierAttr = GetAttributes();
    FPlayerAttributes DefenderAttr = Defender->GetAttributes();

    float CarrierSpeed = GetVelocity().Size();
    float DefenderSpeed = Defender->GetVelocity().Size();

    float DefenderPower = DefenderAttr.Strength * 0.5f + (DefenderSpeed * 0.1f);
    float CarrierPower = CarrierAttr.Strength * 0.3f + CarrierAttr.Agility * 0.3f + (CarrierSpeed * 0.05f);

    float TackleChance = 0.50f + (DefenderPower - CarrierPower) * 0.005f;
    TackleChance = FMath::Clamp(TackleChance, 0.10f, 0.95f);

    float Roll = FMath::FRand();
    if (Roll <= TackleChance)
    {
        UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Tackle SUCCESS! Defender %s tackled carrier %s (Roll: %.2f <= Chance: %.2f)"), 
            *DefenderAttr.DisplayName, *CarrierAttr.DisplayName, Roll, TackleChance);

        // Fumble chance check
        float FumbleChance = 0.02f + (DefenderSpeed * 0.0001f);
        FumbleChance = FMath::Clamp(FumbleChance, 0.01f, 0.25f);
        if (FMath::FRand() <= FumbleChance)
        {
            FumbleBall();
            return true;
        }

        // Apply physics impulse (knockback)
        FVector KnockbackDir = GetActorLocation() - Defender->GetActorLocation();
        KnockbackDir.Z = 0.f;
        if (!KnockbackDir.IsNearlyZero())
        {
            KnockbackDir.Normalize();
        }
        else
        {
            KnockbackDir = -GetActorForwardVector();
        }

        if (MovementComponent)
        {
            MovementComponent->Velocity = KnockbackDir * 150.f;
        }

        // Down by contact
        if (MovementComponent)
        {
            MovementComponent->Velocity = FVector::ZeroVector;
            MovementComponent->StopActiveMovement();
        }

        int32 YardsGained = FMath::RoundToInt((GetActorLocation().X - StartingLocation.X) / 100.f);
        if (GM->PlaySimulation)
        {
            GM->PlaySimulation->RecordTackle(YardsGained);
        }
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Tackle BROKEN! Carrier %s broke tackle from defender %s (Roll: %.2f > Chance: %.2f)"), 
            *CarrierAttr.DisplayName, *DefenderAttr.DisplayName, Roll, TackleChance);

        if (MovementComponent)
        {
            MovementComponent->Velocity *= 0.5f;
        }
        return false;
    }
}
