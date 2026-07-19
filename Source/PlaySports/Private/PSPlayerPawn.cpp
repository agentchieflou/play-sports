#include "PSPlayerPawn.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "AIController.h"

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
}

void APSPlayerPawn::BeginPlay()
{
    Super::BeginPlay();
}

void APSPlayerPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (MovementComponent)
    {
        float CurrentSpeed = MovementComponent->Velocity.Size();
        float MaxSpeed = MovementComponent->MaxSpeed;

        if (MaxSpeed > 0.f)
        {
            float SpeedRatio = FMath::Clamp(CurrentSpeed / MaxSpeed, 0.f, 1.f);
            
            // Asymptotic acceleration curve: tapers off quadratically as speed reaches max
            float CurveMultiplier = 1.f - (SpeedRatio * SpeedRatio);
            
            // Ensure a minimum acceleration (10% of base) so top speed is always reached
            float ActiveAcceleration = BaseAcceleration * FMath::Max(0.1f, CurveMultiplier);
            MovementComponent->Acceleration = ActiveAcceleration;

            // Deceleration is also physically scaled with base acceleration (e.g. 1.5x base)
            MovementComponent->Deceleration = BaseAcceleration * 1.5f;
        }

        // Turning / change-of-direction mechanics
        if (!MovementComponent->Velocity.IsNearlyZero())
        {
            // 1. Smooth rotation alignment with movement direction on Yaw axis
            FRotator CurrentRotation = GetActorRotation();
            FRotator TargetRotation = MovementComponent->Velocity.Rotation();
            TargetRotation.Pitch = 0.f;
            TargetRotation.Roll = 0.f;

            // Base turn rate of 180 deg/s + Agility scaling (up to +600 deg/s), scaled down by Weight penalty
            float BaseTurnRate = 180.f + (Attributes.Agility * 6.f);
            float WeightFactor = 100.f / FMath::Max(50.f, Attributes.WeightKg);
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
                if (DotProduct < 0.8f) // Diverging angle greater than ~36 degrees
                {
                    // Calculate a minimum speed multiplier (high agility/low weight preserves more speed)
                    float MinSpeedMultiplier = 0.4f + (Attributes.Agility * 0.003f) - (Attributes.WeightKg * 0.001f);
                    MinSpeedMultiplier = FMath::Clamp(MinSpeedMultiplier, 0.2f, 0.95f);

                    // Interpolate speed penalty based on sharpness of the cut (DotProduct range [0.8, -1.0])
                    float Sharpness = FMath::Clamp((DotProduct - 0.8f) / -1.8f, 0.f, 1.f);
                    float PenaltyFactor = FMath::Lerp(1.0f, MinSpeedMultiplier, Sharpness);

                    // Apply the cutting deceleration directly to current velocity
                    MovementComponent->Velocity *= PenaltyFactor;

                    // Log cutting events (e.g. for significant cuts)
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

    // Scale MaxSpeed linearly based on Speed attribute (0-100 rating -> 300 to 900 cm/s max speed)
    float ScaledMaxSpeed = 300.f + (Attributes.Speed * 6.f);
    if (MovementComponent)
    {
        MovementComponent->MaxSpeed = ScaledMaxSpeed;
    }

    // Scale BaseAcceleration from Acceleration attribute (0-100 rating -> 500 to 2000 cm/s^2 base acceleration)
    BaseAcceleration = 500.f + (Attributes.Acceleration * 15.f);

    // Log player pawn initialization details
    FString RoleName = UEnum::GetValueAsString(Attributes.Role);
    UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Initialized player pawn for %s (ID: %s, Role: %s, Height: %.1f cm, Weight: %.1f kg, MaxSpeed: %.1f cm/s, BaseAccel: %.1f cm/s^2)"), 
        *Attributes.DisplayName, 
        *Attributes.PlayerId.ToString(), 
        *RoleName, 
        Attributes.HeightCm, 
        Attributes.WeightKg,
        ScaledMaxSpeed,
        BaseAcceleration);
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
