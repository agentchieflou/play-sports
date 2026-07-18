#include "PSBroadcastCamera.h"

APSBroadcastCamera::APSBroadcastCamera()
{
    PrimaryActorTick.bCanEverTick = true;

    TargetActor = nullptr;
    SidelineY = -2800.0f; // Standard sideline placement (field width is Y = +/- 2438.4 cm)
    CameraHeight = 600.0f;  // Elevated to look down on the play
    TrackingSpeed = 5.0f;
    bIsFollowing = true;
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

    // Side camera slides along the X-axis tracking target's X, keeping Y and Z at fixed sideline coordinates
    float NewX = FMath::FInterpTo(CurrentLocation.X, TargetLocation.X, DeltaTime, TrackingSpeed);

    FVector NewLocation(NewX, SidelineY, CameraHeight);
    SetActorLocation(NewLocation);

    // Rotate camera to look directly at the target actor
    FRotator TargetRotation = (TargetLocation - NewLocation).Rotation();
    SetActorRotation(TargetRotation);
}
