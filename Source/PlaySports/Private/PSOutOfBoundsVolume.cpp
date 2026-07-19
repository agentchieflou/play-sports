#include "PSOutOfBoundsVolume.h"
#include "Components/BoxComponent.h"
#include "PSPlayerPawn.h"
#include "PSBall.h"
#include "PSGameMode.h"
#include "PSPlaySimulation.h"
#include "Kismet/GameplayStatics.h"

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
    APSGameMode* GM = Cast<APSGameMode>(UGameplayStatics::GetGameMode(this));
    if (!GM || !GM->PlaySimulation)
    {
        return;
    }

    if (APSPlayerPawn* Pawn = Cast<APSPlayerPawn>(OtherActor))
    {
        if (Pawn->HasPossession())
        {
            float YardsGained = (Pawn->GetActorLocation().X - Pawn->GetStartingLocation().X) / 100.f;
            GM->PlaySimulation->RecordTackle(FMath::RoundToInt(YardsGained));
            UE_LOG(LogTemp, Display, TEXT("PSOutOfBoundsVolume: Player with ball ran Out of Bounds. Yards gained: %.1f"), YardsGained);
        }
    }
    else if (APSBall* Ball = Cast<APSBall>(OtherActor))
    {
        if (GM->PlaySimulation->GetPlayState().Phase != EPlayPhase::Scoring && 
            GM->PlaySimulation->GetPlayState().Phase != EPlayPhase::PreSnap)
        {
            GM->PlaySimulation->SetPlayPhase(EPlayPhase::Scoring);
            UE_LOG(LogTemp, Display, TEXT("PSOutOfBoundsVolume: Ball went Out of Bounds. Resolving play."));
        }
    }
}
