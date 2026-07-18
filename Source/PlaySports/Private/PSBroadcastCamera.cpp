#include "PSBroadcastCamera.h"

APSBroadcastCamera::APSBroadcastCamera()
{
    PrimaryActorTick.bCanEverTick = true;

    TargetActor = nullptr;
    SidelineY = -2800.0f; // Standard sideline placement (field width is Y = +/- 2438.4 cm)
    CameraHeight = 600.0f;  // Elevated to look down on the play
    TrackingSpeed = 5.0f;
    bIsFollowing = true;

    // Field bounds: Endlines are at +/- 5486.4 cm. Expand slightly for padding.
    MinX = -6000.0f;
    MaxX = 6000.0f;

    // Sideline limits to keep within the stadium structure
    MinY = -4000.0f;
    MaxY = 4000.0f;

    // Height limit (MinZ >= 100.0f prevents camera from clipping through the ground field plane)
    MinZ = 100.0f;
    MaxZ = 2500.0f;
}

void APSBroadcastCamera::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bIsFollowing || !TargetActor)
    {
        return;
    }

    FVector TargetLocation = TargetActor->GetActorLocation();
    FVector CurrentLocation = GetActorLocation();

    // Slide along the X-axis tracking target's X position, keeping it bounded within MinX/MaxX
    float TargetX = FMath::Clamp(TargetLocation.X, MinX, MaxX);
    float NewX = FMath::FInterpTo(CurrentLocation.X, TargetX, DeltaTime, TrackingSpeed);

    // Enforce sideline and height boundaries
    float ClampedY = FMath::Clamp(SidelineY, MinY, MaxY);
    float ClampedZ = FMath::Clamp(CameraHeight, MinZ, MaxZ);

    FVector NewLocation(NewX, ClampedY, ClampedZ);
    SetActorLocation(NewLocation);

    // Rotate camera to look directly at the target actor
    FRotator TargetRotation = (TargetLocation - NewLocation).Rotation();
    SetActorRotation(TargetRotation);
}
