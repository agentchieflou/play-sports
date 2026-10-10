// PSCameraFraming.h - Epic 40: the all-22 coaches film rigs and the framing math behind them
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PSCameraFraming.generated.h"

/** Where an all-22 rig stands (Epic 40). */
UENUM(BlueprintType)
enum class EPSAll22RigPlacement : uint8
{
    /** High on the -Y sideline, the broadcast camera's side, so a cut keeps screen direction
     *  (the 180-degree rule). */
    Sideline,
    /** High behind the end zone the offense defends, looking downfield. */
    EndZone
};

/** One fixed, elevated all-22 camera rig, as data (Data/camera_all22.json; rule 4). The
 *  defaults are the file's sideline rig. */
USTRUCT(BlueprintType)
struct FPSAll22RigDef
{
    GENERATED_BODY()

    /** Names the rig; the film view toggles through rigs in file order. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    FName RigId = FName(TEXT("Sideline"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    EPSAll22RigPlacement Placement = EPSAll22RigPlacement::Sideline;

    /** Camera height above the field (cm). Fixed: the rig never cranes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    float HeightCm = 2700.f;

    /** Distance from the field's centre to the rig (cm): across the field to the -Y sideline
     *  rig, or along it to the end-zone rig. Fixed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    float StandoffCm = 5200.f;

    /** True: the rig slides along its rail (X for the sideline rig, Y for the end-zone rig) to
     *  stay square to the players. False: it stays at the rail's centre and only pans. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    bool bTrackPlay = true;

    /** How far the rail runs either side of its centre (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    float RailHalfLengthCm = 4572.f;

    /** The tightest zoom (horizontal field of view, degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    float MinFieldOfView = 30.f;

    /** The widest zoom (degrees). When the players don't fit even at this angle, the rig
     *  backs away along its line of sight until they do. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    float MaxFieldOfView = 80.f;
};

/** The all-22 film view's tuning (Data/camera_all22.json, Epic 40). Defaults equal the file. */
USTRUCT(BlueprintType)
struct FPSAll22CameraTuning
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    TArray<FPSAll22RigDef> All22Rigs;

    /** Padding kept around the players on the ground (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    float FramingMarginCm = 300.f;

    /** A player's height (cm), centred on the pawn's location, so heads and feet stay in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    float PlayerHeightCm = 200.f;

    /** Width over height of the frame when no game viewport says otherwise (headless runs). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    float AspectRatio = 1.7778f;

    /** How fast the framing closes in once the players bunch up (FInterpTo speed). Widening is
     *  immediate, so nobody ever leaves the frame. 0 closes in at once. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|All22")
    float ReframeSpeed = 2.f;
};

/** One camera view: where it stands, where it looks, how wide. */
USTRUCT(BlueprintType)
struct FPSCameraShot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    FVector Location = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    FRotator Rotation = FRotator::ZeroRotator;

    /** Horizontal field of view (degrees), as UCameraComponent uses it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float FieldOfView = 90.f;

    /** Width over height of the frame. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float AspectRatio = 1.7778f;
};

/**
 * UPSCameraFraming is the all-22 framing math (Epic 40), kept pure so it runs headless and so a
 * replay (Epic 41) can re-frame from replayed positions without recording a camera track.
 *
 * The framed region is the players' bounding box, padded by FramingMarginCm on the ground and
 * PlayerHeightCm in height. A rig stands at its fixed height and standoff, aims at the box's
 * centre, and zooms to the narrowest field of view that holds all eight corners of the box. The
 * frustum is convex, so holding the corners holds every player inside the box.
 */
UCLASS()
class PLAYSPORTS_API UPSCameraFraming : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** The padded box around Locations. An empty list gives an empty (invalid) box. */
    static FBox ComputePlayerBox(const TArray<FVector>& Locations, const FPSAll22CameraTuning& Tuning);

    /**
     * The shot Rig takes of Box. AttackDirection is +1 when the offense attacks +X and -1 when
     * it attacks -X; the end-zone rig stands behind the offense. An invalid box frames the
     * field's centre.
     */
    static FPSCameraShot FrameBox(const FPSAll22RigDef& Rig, const FBox& Box, float AttackDirection, float AspectRatio);

    /** FrameBox of ComputePlayerBox: the shot holding every one of Locations. */
    static FPSCameraShot FrameAll22(const FPSAll22RigDef& Rig, const FPSAll22CameraTuning& Tuning,
        const TArray<FVector>& Locations, float AttackDirection, float AspectRatio);

    /**
     * Where Point lands in Shot's frame, normalized: (0,0) top left, (1,1) bottom right. False
     * when Point is behind the camera or outside the frame (OutScreen is still set when it is
     * in front).
     */
    UFUNCTION(BlueprintPure, Category = "Camera|All22")
    static bool ProjectToShot(const FPSCameraShot& Shot, const FVector& Point, FVector2D& OutScreen);

    /** True when Point is in front of the camera and inside Shot's frame. */
    UFUNCTION(BlueprintPure, Category = "Camera|All22")
    static bool IsPointInShot(const FPSCameraShot& Shot, const FVector& Point);

    /** Problems with Tuning (empty when sound): no rigs, a missing or repeated RigId, a
     *  non-positive height, standoff or aspect ratio, a zoom range outside (0, 170) or upside
     *  down, a negative margin, height, rail or speed. */
    static TArray<FString> ValidateTuning(const FPSAll22CameraTuning& Tuning);
};
