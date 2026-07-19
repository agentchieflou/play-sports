#include "PSEndZoneVolume.h"
#include "Components/BoxComponent.h"
#include "PSPlayerPawn.h"
#include "PSGameMode.h"
#include "PSPlaySimulation.h"
#include "Kismet/GameplayStatics.h"

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
    if (APSPlayerPawn* Pawn = Cast<APSPlayerPawn>(OtherActor))
    {
        if (Pawn->bHasPossession)
        {
            APSGameMode* GM = Cast<APSGameMode>(UGameplayStatics::GetGameMode(this));
            if (GM && GM->PlaySimulation)
            {
                bool bHomePossession = GM->PlaySimulation->GetPlayState().bHomeHasPossession;
                bool bIsTargetEndZone = (bHomePossession && !bIsEndZoneA) || (!bHomePossession && bIsEndZoneA);

                if (bIsTargetEndZone)
                {
                    GM->PlaySimulation->RecordTouchdown();
                }
            }
        }
    }
}
