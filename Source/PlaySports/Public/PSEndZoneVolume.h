#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSEndZoneVolume.generated.h"

/** An end zone. A ball carrier entering it is reported on the bus (BoundaryCrossed) at its goal
 *  line; the play simulation rules on it (a touchdown, a return's end). */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSEndZoneVolume : public AActor
{
    GENERATED_BODY()

public:
    APSEndZoneVolume();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UBoxComponent* CollisionBox;

    /** Which end of the field this is, for the level's layout (APSFieldGrid tags it). The goal
     *  line a crossing reports comes from where the carrier is, not from this flag. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EndZone")
    bool bIsEndZoneA;

    /** OtherActor entered the end zone: a pawn with the ball is reported at its goal line. True
     *  when a crossing was published. */
    bool ReportCrossing(AActor* OtherActor);

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
