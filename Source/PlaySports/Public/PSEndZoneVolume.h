#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSEndZoneVolume.generated.h"

UCLASS(Blueprintable)
class PLAYSPORTS_API APSEndZoneVolume : public AActor
{
    GENERATED_BODY()

public:
    APSEndZoneVolume();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UBoxComponent* CollisionBox;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EndZone")
    bool bIsEndZoneA;

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
