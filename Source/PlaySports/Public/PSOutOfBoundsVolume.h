#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSOutOfBoundsVolume.generated.h"

UCLASS(Blueprintable)
class PLAYSPORTS_API APSOutOfBoundsVolume : public AActor
{
    GENERATED_BODY()

public:
    APSOutOfBoundsVolume();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UBoxComponent* CollisionBox;

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
