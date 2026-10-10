// PSOverlayBallFlightTypes.h - Epic 32: what the ball-flight overlay predicts, and its look
#pragma once

#include "CoreMinimal.h"
#include "PSOverlayBallFlightTypes.generated.h"

/** The flight the overlay is following. */
UENUM(BlueprintType)
enum class EPSBallFlightKind : uint8
{
    None,
    /** A forward pass (a Throw event): the arc, the landing spot at catch height, the lead. */
    Pass,
    /** A ball that leaves the kicker during a kick phase: the arc, where it comes down, and
     *  where it passes the uprights. */
    Kick
};

/** Where a kick is headed against the uprights. */
UENUM(BlueprintType)
enum class EPSKickVerdict : uint8
{
    /** Not judged: no goal ahead of the ball. */
    None,
    Good,
    /** Outside the left upright, as the kicker sees it. */
    WideLeft,
    WideRight,
    /** Comes down before the goal, or passes under the crossbar. */
    Short
};

/**
 * The ball-flight overlay's look and rules (Data/ball_flight_overlay.json; Architecture rule 4).
 * Colors are "#RRGGBB"; lengths are cm in the game mode's field frame (100 units a yard along
 * the field, the offense attacking +X from its own goal line at X = 0).
 */
USTRUCT(BlueprintType)
struct FPSBallFlightStyle
{
    GENERATED_BODY()

    /** The predicted arc. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString ArcColor = TEXT("#F2F2F2");

    /** The landing-spot ring. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString LandingColor = TEXT("#F2C94C");

    /** The receiver's lead ring when he gets there in time for the ball ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString LeadOnTargetColor = TEXT("#27AE60");

    /** ... and when he doesn't. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString LeadOffTargetColor = TEXT("#EB5757");

    /** The kick readout when it is good ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString GoodColor = TEXT("#27AE60");

    /** ... and when it isn't. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString NoGoodColor = TEXT("#EB5757");

    /** The arc's dots: engine basic shapes until an editor session authors a ribbon
     *  (Specs/Ball_Flight_Overlay_Spec.md). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString DotMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

    /** The landing and lead rings. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString RingMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

    /** The material's vector parameter the color goes into. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FName ColorParameter = TEXT("Color");

    /** Both meshes' size at scale 1, cm across (the engine shapes are 100). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float MeshDiameter = 100.f;

    /** Points along the arc, release to landing, evenly in time; a dot is drawn at each. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    int32 ArcPoints = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float ArcDotDiameter = 12.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float RingThickness = 2.f;

    /** Gap between the ground and a ring's underside, so it never z-fights the grass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float GroundClearance = 1.f;

    /** The height of the field's surface. Kicks come down to it; rings are drawn on it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float GroundZ = 0.f;

    /** The landing ring's radius when no receiver gives one (kicks; a target not found).
     *  For a pass to a known receiver it is his catch radius: his capsule plus the ball. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float LandingRadiusFallback = 60.f;

    /** The receiver's lead ring. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float LeadRadius = 40.f;

    /** The ball this far from its predicted path has been touched, caught or has bounced:
     *  the prediction no longer holds and the flight is over. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float DeviationTolerance = 75.f;

    /** The longest flight predicted; a ball that doesn't come down by then is drawn this far. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float MaxFlightSeconds = 8.f;

    /** A pass's marks stay up this long after the flight is over. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float LingerSeconds = 0.75f;

    /** A kick's marks and readout stay up this long after the flight is over. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float ReadoutSeconds = 2.5f;

    /** Where the goal posts stand along the field: the X of each end line. A kick is judged
     *  at the first one ahead of it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    TArray<float> GoalPostX;

    /** The posts' center across the field. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float GoalPostY = 0.f;

    /** Inside width between the uprights. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float UprightWidth = 617.f;

    /** The crossbar's height above the ground. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float CrossbarHeight = 305.f;

    /** The readout floats this high above the crossbar. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float ReadoutHeight = 250.f;

    /** The readout's letter height, cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float ReadoutTextSize = 120.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString GoodLabel = TEXT("GOOD");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString WideLeftLabel = TEXT("WIDE LEFT");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString WideRightLabel = TEXT("WIDE RIGHT");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString ShortLabel = TEXT("SHORT");

    FPSBallFlightStyle()
    {
        // Both end lines in the game mode's frame: the goal lines are X = 0 and X = 10000.
        GoalPostX.Add(-1000.f);
        GoalPostX.Add(11000.f);
    }

    /** The readout's text for a verdict; empty for None. */
    FString LabelFor(EPSKickVerdict Verdict) const
    {
        switch (Verdict)
        {
        case EPSKickVerdict::Good:      return GoodLabel;
        case EPSKickVerdict::WideLeft:  return WideLeftLabel;
        case EPSKickVerdict::WideRight: return WideRightLabel;
        case EPSKickVerdict::Short:     return ShortLabel;
        default:                        return FString();
        }
    }
};

/** A ball's predicted flight from one instant of its physics state. Drag-free, as the ball's
 *  projectile movement flies it, so the prediction is exact until something touches the ball. */
USTRUCT(BlueprintType)
struct FPSBallFlightPrediction
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bValid = false;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    EPSBallFlightKind Kind = EPSBallFlightKind::None;

    /** Where and how fast the ball was going when the prediction was made. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FVector ReleaseLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FVector ReleaseVelocity = FVector::ZeroVector;

    /** cm/s^2; negative is down. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float GravityZ = 0.f;

    /** The height the ball is judged to land at: catch height for a pass, the ground for a kick
     *  (or for a pass that never comes down to catch height). */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float LandingHeight = 0.f;

    /** False when the ball doesn't come down to LandingHeight within the longest flight. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bLands = false;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FVector LandingLocation = FVector::ZeroVector;

    /** Seconds from release to landing (or to the end of the drawn arc when it doesn't land). */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float LandingSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FVector ApexLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float ApexSeconds = 0.f;

    /** The arc, release to landing, evenly in time. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    TArray<FVector> ArcPoints;
};

/** Where the intended receiver will be when the ball arrives. */
USTRUCT(BlueprintType)
struct FPSPassLead
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bValid = false;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FString ReceiverName;

    /** On the ground: where he is now plus his velocity until the ball comes down. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FVector LeadLocation = FVector::ZeroVector;

    /** Seconds until the ball comes down. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float ArrivalSeconds = 0.f;

    /** Across the ground from the lead to the landing spot. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float MissDistance = 0.f;

    /** How close to the landing spot he must be to catch it: his capsule plus the ball. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float CatchRadius = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bOnTarget = false;
};

/** A kick against the uprights it heads for. */
USTRUCT(BlueprintType)
struct FPSKickReadout
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    EPSKickVerdict Verdict = EPSKickVerdict::None;

    /** The readout's text (the style's label for the verdict). */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FString Label;

    /** The X of the end line the kick is judged at. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float GoalPostX = 0.f;

    /** False when the ball comes down before it gets there. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bReachesGoal = false;

    /** Seconds from the prediction's release to the goal line of the posts. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float CrossingSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FVector CrossingLocation = FVector::ZeroVector;

    /** Across the posts from their center, positive to the kicker's right. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float LateralOffset = 0.f;

    /** Above the crossbar; negative under it. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float HeightOverBar = 0.f;

    /** Inside the nearer upright; negative outside it. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float MarginInside = 0.f;
};

/** Everything the ball-flight overlay shows, as UPSOverlayBallFlightSubsystem works it out. */
USTRUCT(BlueprintType)
struct FPSBallFlightState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    EPSBallFlightKind Kind = EPSBallFlightKind::None;

    /** The ball is still on its predicted path. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bActive = false;

    /** Drawn: during the flight and while its marks linger after it. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bVisible = false;

    /** How far along the arc the ball is, in seconds since the prediction's release. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float ElapsedSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FPSBallFlightPrediction Prediction;

    /** A pass's landing-spot ring. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float LandingRadius = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FPSPassLead Lead;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FPSKickReadout Kick;
};
