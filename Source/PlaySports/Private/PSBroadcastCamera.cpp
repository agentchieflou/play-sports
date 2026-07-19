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

    bIsFreeCam = false;
}

void APSBroadcastCamera::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (bIsFreeCam || !bIsFollowing || !TargetActor)
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

void APSBroadcastCamera::SnapToScrimmage(float ScrimmageYardLine)
{
    // Convert yard line to world X coordinate
    float TargetWorldX = (ScrimmageYardLine - 50.0f) * 91.44f;
    TargetWorldX = FMath::Clamp(TargetWorldX, MinX, MaxX);

    float ClampedY = FMath::Clamp(SidelineY, MinY, MaxY);
    float ClampedZ = FMath::Clamp(CameraHeight, MinZ, MaxZ);

    FVector NewLocation(TargetWorldX, ClampedY, ClampedZ);
    SetActorLocation(NewLocation);

    // Look at the center of the field at the scrimmage line
    FVector ScrimmageCenter(TargetWorldX, 0.0f, 0.0f);
    FRotator TargetRotation = (ScrimmageCenter - NewLocation).Rotation();
    SetActorRotation(TargetRotation);

    // Turn off follow mode until the play starts and it is re-enabled
    bIsFollowing = false;
}

void APSBroadcastCamera::ToggleFreeCam(bool bEnabled)
{
    bIsFreeCam = bEnabled;
    if (bIsFreeCam)
    {
        bIsFollowing = false;
        UE_LOG(LogTemp, Display, TEXT("APSBroadcastCamera: Debug free-cam enabled. Active tracking suspended."));
    }
    else
    {
        bIsFollowing = true;
        UE_LOG(LogTemp, Display, TEXT("APSBroadcastCamera: Debug free-cam disabled. Active tracking restored."));
    }
}

void APSBroadcastCamera::SetTargetActor(AActor* NewTarget)
{
    TargetActor = NewTarget;
    if (NewTarget)
    {
        bIsFollowing = true;
        UE_LOG(LogTemp, Display, TEXT("APSBroadcastCamera: Target set to %s. Following enabled."),
            *NewTarget->GetName());
    }
    else
    {
        bIsFollowing = false;
        UE_LOG(LogTemp, Warning, TEXT("APSBroadcastCamera: Target cleared. Following disabled."));
    }
}
