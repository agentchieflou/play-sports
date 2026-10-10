#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSOutOfBoundsVolume.generated.h"

/** A sideline or end line. A ball carrier, or the ball on its own, crossing it is reported on the
 *  bus (BoundaryCrossed); the play simulation rules on it. */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSOutOfBoundsVolume : public AActor
{
    GENERATED_BODY()

public:
    APSOutOfBoundsVolume();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UBoxComponent* CollisionBox;

    /** OtherActor overlapped the volume: a pawn with the ball, or the ball when nobody carries
     *  it, is reported out of bounds at its yard line. True when a crossing was published. */
    bool ReportCrossing(AActor* OtherActor);

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
