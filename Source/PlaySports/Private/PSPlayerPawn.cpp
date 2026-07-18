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
}

void APSPlayerPawn::BeginPlay()
{
    Super::BeginPlay();
}

void APSPlayerPawn::InitializePlayer(const FPlayerAttributes& InAttributes)
{
    Attributes = InAttributes;

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
