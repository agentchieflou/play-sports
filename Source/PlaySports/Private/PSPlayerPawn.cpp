#include "PSPlayerPawn.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "AIController.h"

APSPlayerPawn::APSPlayerPawn()
{
    PrimaryActorTick.bCanEverTick = false;

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
}

void APSPlayerPawn::BeginPlay()
{
    Super::BeginPlay();
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

    // Log player pawn initialization details
    FString RoleName = UEnum::GetValueAsString(Attributes.Role);
    UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Initialized player pawn for %s (ID: %s, Role: %s, Height: %.1f cm, Weight: %.1f kg, MaxSpeed: %.1f cm/s)"), 
        *Attributes.DisplayName, 
        *Attributes.PlayerId.ToString(), 
        *RoleName, 
        Attributes.HeightCm, 
        Attributes.WeightKg,
        ScaledMaxSpeed);
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
