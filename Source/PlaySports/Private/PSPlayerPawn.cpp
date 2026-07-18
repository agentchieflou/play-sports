#include "PSPlayerPawn.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"

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
}

void APSPlayerPawn::BeginPlay()
{
    Super::BeginPlay();
}

void APSPlayerPawn::InitializePlayer(const FPlayerAttributes& InAttributes)
{
    Attributes = InAttributes;

    // Log player pawn initialization details
    FString RoleName = UEnum::GetValueAsString(Attributes.Role);
    UE_LOG(LogTemp, Display, TEXT("APSPlayerPawn: Initialized player pawn for %s (ID: %s, Role: %s, Height: %.1f cm, Weight: %.1f kg)"), 
        *Attributes.DisplayName, 
        *Attributes.PlayerId.ToString(), 
        *RoleName, 
        Attributes.HeightCm, 
        Attributes.WeightKg);
}
