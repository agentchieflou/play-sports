#include "PSEndZoneVolume.h"
#include "Components/BoxComponent.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"

APSEndZoneVolume::APSEndZoneVolume()
{
    PrimaryActorTick.bCanEverTick = false;

    CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
    RootComponent = CollisionBox;
    CollisionBox->SetCollisionProfileName(TEXT("Trigger"));
    CollisionBox->SetGenerateOverlapEvents(true);

    bIsEndZoneA = true;
}

void APSEndZoneVolume::BeginPlay()
{
    Super::BeginPlay();

    if (CollisionBox)
    {
        CollisionBox->OnComponentBeginOverlap.AddDynamic(this, &APSEndZoneVolume::OnOverlapBegin);
    }
}

void APSEndZoneVolume::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    ReportCrossing(OtherActor);
}

bool APSEndZoneVolume::ReportCrossing(AActor* OtherActor)
{
    const APSPlayerPawn* Pawn = Cast<APSPlayerPawn>(OtherActor);
    UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Pawn || !Pawn->HasPossession() || !Bus)
    {
        return false;
    }

    // Which goal line he crossed, in the offense's yard lines: the game mode places yard line N
    // at X = N * 100 cm with the offense attacking +X, so the far end zone is its target (100)
    // and the near one its own (0), whoever has the ball.
    FPSTelemetryBoundaryCrossedEvent Crossing;
    Crossing.CarrierName = Pawn->GetAttributes().DisplayName;
    Crossing.YardLine = Pawn->GetActorLocation().X >= 5000.f ? 100 : 0;
    Crossing.bEndZone = true;
    Bus->PublishBoundaryCrossed(Crossing);
    UE_LOG(LogTemp, Display, TEXT("PSEndZoneVolume: %s in the end zone at the %d."), *Crossing.CarrierName, Crossing.YardLine);
    return true;
}
