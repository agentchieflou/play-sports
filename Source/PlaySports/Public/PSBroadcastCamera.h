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

    // Minimum X bound (stadium back endline A)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera|Bounds")
    float MinX;

    // Maximum X bound (stadium back endline B)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera|Bounds")
    float MaxX;

    // Minimum Y bound (sideline A limit)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera|Bounds")
    float MinY;

    // Maximum Y bound (sideline B limit)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera|Bounds")
    float MaxY;

    // Minimum Z bound (minimum height to prevent going below field plane)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera|Bounds")
    float MinZ;

    // Maximum Z bound (maximum height ceiling)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera|Bounds")
    float MaxZ;

    // Instantly snap the camera to center on a specific yard line (pre-play framing)
    UFUNCTION(BlueprintCallable, Category = "Broadcast Camera")
    void SnapToScrimmage(float ScrimmageYardLine);

    // Active state of free cam mode
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera")
    bool bIsFreeCam;

    // Toggle free cam mode on or off
    UFUNCTION(BlueprintCallable, Category = "Broadcast Camera")
    void ToggleFreeCam(bool bEnabled);
};
