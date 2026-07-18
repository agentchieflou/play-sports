#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "PSBroadcastCamera.generated.h"

/**
 * APSBroadcastCamera tracks the play from a sideline perspective.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSBroadcastCamera : public ACameraActor
{
    GENERATED_BODY()

public:
    APSBroadcastCamera();

    virtual void Tick(float DeltaTime) override;

    // Target actor to track (e.g. the ball or the current ball carrier pawn)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera")
    AActor* TargetActor;

    // Fixed sideline Y coordinate for side tracking camera track
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera")
    float SidelineY;

    // Elevated height for the camera view
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera")
    float CameraHeight;

    // Speed of tracking movement interpolation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera")
    float TrackingSpeed;

    // Controls if camera currently tracks the target actor
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera")
    bool bIsFollowing;
};
