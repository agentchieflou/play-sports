#include "PSEndZoneVolume.h"
#include "Components/BoxComponent.h"
#include "PSFieldDimensions.h"
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

    // Which goal line he crossed, in the offense's yard lines: on the field's one frame (PSField)
    // the offense attacks +X, so the far end zone is its target (100) and the near one its own
    // (0), whoever has the ball.
    FPSTelemetryBoundaryCrossedEvent Crossing;
    Crossing.CarrierName = Pawn->GetAttributes().DisplayName;
    Crossing.YardLine = Pawn->GetActorLocation().X >= PSField::MidfieldX() ? FMath::RoundToInt(PSField::GetDimensions().FieldLengthYards) : 0;
    Crossing.bEndZone = true;
    Bus->PublishBoundaryCrossed(Crossing);
    UE_LOG(LogTemp, Display, TEXT("PSEndZoneVolume: %s in the end zone at the %d."), *Crossing.CarrierName, Crossing.YardLine);
    return true;
}
