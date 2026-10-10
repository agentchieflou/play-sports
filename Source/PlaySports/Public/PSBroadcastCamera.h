#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "PSBroadcastCamera.generated.h"

class UPSCameraAll22Component;
class UPSCameraDirectorComponent;

/**
 * APSBroadcastCamera is the game's one view: the player controller looks through it. Each tick
 * one thing drives it, first match wins: the debug free cam; the coaches film view (Epic 40,
 * its all-22 component, on the FilmView action); the camera director (Epic 38), which cuts
 * between its shots by itself; and otherwise the plain sideline follow of TargetActor.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API APSBroadcastCamera : public ACameraActor
{
    GENERATED_BODY()

public:
    APSBroadcastCamera();

    virtual void Tick(float DeltaTime) override;

    /** Listens to the viewing APSPlayerController for the film view toggle (Epic 40). */
    virtual void BecomeViewTarget(APlayerController* PC) override;

    virtual void EndViewTarget(APlayerController* PC) override;

    /** The all-22 coaches film view (Epic 40). While it is on, it drives the camera and the
     *  broadcast follow waits. */
    UFUNCTION(BlueprintPure, Category = "Broadcast Camera")
    UPSCameraAll22Component* GetAll22Component() const { return All22Component; }

    /** The camera director (Epic 38). While it is enabled (and the film view is off), it drives
     *  the camera and the plain follow waits. */
    UFUNCTION(BlueprintPure, Category = "Broadcast Camera")
    UPSCameraDirectorComponent* GetDirectorComponent() const { return DirectorComponent; }

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

    /**
     * Set the actor to track. Wires the orphan TargetActor that was always null
     * (architecture review Epic C3 finding). GameMode calls this after snap so the
     * camera follows the ball carrier.
     */
    UFUNCTION(BlueprintCallable, Category = "Broadcast Camera")
    void SetTargetActor(AActor* NewTarget);

    // Active state of free cam mode
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast Camera")
    bool bIsFreeCam;

    // Toggle free cam mode on or off
    UFUNCTION(BlueprintCallable, Category = "Broadcast Camera")
    void ToggleFreeCam(bool bEnabled);

private:
    UPROPERTY(VisibleAnywhere, Category = "Broadcast Camera")
    UPSCameraAll22Component* All22Component;

    UPROPERTY(VisibleAnywhere, Category = "Broadcast Camera")
    UPSCameraDirectorComponent* DirectorComponent;
};
