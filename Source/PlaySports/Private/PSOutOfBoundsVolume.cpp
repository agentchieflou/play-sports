#include "PSOutOfBoundsVolume.h"
#include "Components/BoxComponent.h"
#include "PSPlayerPawn.h"
#include "PSBall.h"
#include "PSFieldDimensions.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"

APSOutOfBoundsVolume::APSOutOfBoundsVolume()
{
    PrimaryActorTick.bCanEverTick = false;

    CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
    RootComponent = CollisionBox;
    CollisionBox->SetCollisionProfileName(TEXT("Trigger"));
    CollisionBox->SetGenerateOverlapEvents(true);
}

void APSOutOfBoundsVolume::BeginPlay()
{
    Super::BeginPlay();

    if (CollisionBox)
    {
        CollisionBox->OnComponentBeginOverlap.AddDynamic(this, &APSOutOfBoundsVolume::OnOverlapBegin);
    }
}

void APSOutOfBoundsVolume::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    ReportCrossing(OtherActor);
}

bool APSOutOfBoundsVolume::ReportCrossing(AActor* OtherActor)
{
    UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || !OtherActor)
    {
        return false;
    }

    FPSTelemetryBoundaryCrossedEvent Crossing;
    if (const APSPlayerPawn* Pawn = Cast<APSPlayerPawn>(OtherActor))
    {
        if (!Pawn->HasPossession())
        {
            return false;
        }
        Crossing.CarrierName = Pawn->GetAttributes().DisplayName;
    }
    else if (const APSBall* Ball = Cast<APSBall>(OtherActor))
    {
        // A carried ball goes out with its carrier, who reports it.
        if (Ball->GetAttachParentActor())
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    // The spot on the field's one frame (PSField), where the game mode lines up: the offense
    // attacking +X from its goal line.
    Crossing.YardLine = PSField::WorldToSpot(OtherActor->GetActorLocation());
    Bus->PublishBoundaryCrossed(Crossing);
    UE_LOG(LogTemp, Display, TEXT("PSOutOfBoundsVolume: %s out of bounds at the %d."), Crossing.CarrierName.IsEmpty() ? TEXT("The ball") : *Crossing.CarrierName, Crossing.YardLine);
    return true;
}
