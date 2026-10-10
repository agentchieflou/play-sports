// PSCameraSkycamComponent.h - Epic 39: the skycam, a cable-suspended camera flying over the field
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSCameraFraming.h"
#include "PSTelemetryBus.h"
#include "PSCameraSkycamComponent.generated.h"

struct FPSSnapshotFrame;
class UPSCameraAll22Component;

/** What the skycam is flying to do (Epic 39). */
UENUM(BlueprintType)
enum class EPSSkycamMode : uint8
{
    /** Before the snap: parked high behind the quarterback, looking downfield. */
    BehindQuarterback,
    /** From the snap: chasing the ball carrier from behind, along his run. */
    Chase
};

/** The skycam's cable rig and flying, as data (Data/camera_skycam.json; rule 4). Defaults
 *  equal the file. */
USTRUCT(BlueprintType)
struct FPSSkycamTuning
{
    GENERATED_BODY()

    /** The four cable towers stand at (+/-AnchorHalfLengthCm, +/-AnchorHalfWidthCm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float AnchorHalfLengthCm = 6400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float AnchorHalfWidthCm = 3600.f;

    /** Where the cables leave the towers. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float AnchorHeightCm = 4500.f;

    /** The cables' catenary parameter, tension over weight per length (cm). Smaller sags more. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float CatenaryParameterCm = 25000.f;

    /** The camera keeps this far inside the towers' rectangle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float EdgeMarginCm = 300.f;

    /** The lowest the camera flies, clear of the players. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float MinHeightCm = 700.f;

    /** The rig's spring toward where it wants to be (1/s^2): its mass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float StiffnessPerSecSq = 4.f;

    /** Its damping (1/s); 2 * sqrt(stiffness) arrives without overshoot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float DampingPerSec = 4.f;

    /** The winches' limits. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float MaxSpeedCms = 1400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float MaxAccelerationCms2 = 900.f;

    /** Before the snap: this far behind the quarterback, at this height. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float BehindQuarterbackDistanceCm = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float BehindQuarterbackHeightCm = 1300.f;

    /** In the play: this far behind the ball carrier, along his run, at this height. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float ChaseDistanceCm = 1300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float ChaseHeightCm = 1000.f;

    /** The camera looks this far ahead of whoever it follows. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float LookAheadCm = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Skycam")
    float FieldOfView = 60.f;
};

/**
 * UPSCameraSkycamComponent is the skycam (Epic 39): a camera hung from four cables over the
 * field, the modern broadcast's signature angle.
 *
 * - The rig: the camera flies inside the cables' envelope. It stays inside the towers'
 *   rectangle (less EdgeMarginCm), above MinHeightCm, and below the cables, which hang in
 *   catenaries. The ceiling at (X, Y) is AnchorHeightCm less both cable families' sag there,
 *   so the camera rides highest by the towers and lowest over midfield.
 * - Flying: the rig has mass. A critically damped spring pulls it toward where it wants to be,
 *   within its winches' speed and acceleration, so it lags and settles rather than snapping.
 * - Follow: before the snap it parks behind the quarterback. From the snap it chases the ball
 *   carrier from behind along his run (the bus's Snap and PhaseChange, rule 5). It reads the
 *   field from Epic 26's snapshots.
 * - It flies all the time, on air or not, so it is in position whenever the camera director
 *   (Epic 38) cuts to it. While the director's Skycam shot is live, this rig's shot is the
 *   camera's.
 *
 * It lives on APSBroadcastCamera, which steps it every tick. Headless tests step it with
 * AdvanceTime.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSCameraSkycamComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSCameraSkycamComponent();

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FPSSkycamTuning& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Problems with InTuning (empty when sound): non-positive sizes, speeds or catenary, the
     *  floor at or above the ceiling over midfield, or a negative margin, distance or damping. */
    static TArray<FString> ValidateTuning(const FPSSkycamTuning& InTuning);

    /** The highest the camera can fly at (X, Y): the anchors less both cables' catenary sag. */
    static double CeilingAt(const FPSSkycamTuning& InTuning, double X, double Y);

    /** Location moved to the nearest point inside the envelope. */
    static FVector ConstrainToEnvelope(const FPSSkycamTuning& InTuning, const FVector& Location);

    /** True when Location is inside the envelope, within Tolerance. */
    static bool IsInsideEnvelope(const FPSSkycamTuning& InTuning, const FVector& Location, double Tolerance);

    /**
     * One step of the rig's flight toward Desired: the damped spring, the winches' limits, then
     * the envelope. A blocked axis loses its velocity.
     */
    static void StepRig(const FPSSkycamTuning& InTuning, FVector& InOutLocation, FVector& InOutVelocity, const FVector& Desired, float DeltaSeconds);

    /** Listens for the snap and the next down on the world's bus. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

    /** One step: read the latest snapshot, pick where to fly and where to look, and fly. */
    void AdvanceTime(float DeltaSeconds);

    UFUNCTION(BlueprintPure, Category = "Camera|Skycam")
    EPSSkycamMode GetMode() const { return Mode; }

    UFUNCTION(BlueprintCallable, Category = "Camera|Skycam")
    void SetMode(EPSSkycamMode NewMode) { Mode = NewMode; }

    UFUNCTION(BlueprintPure, Category = "Camera|Skycam")
    FVector GetRigLocation() const { return RigLocation; }

    UFUNCTION(BlueprintPure, Category = "Camera|Skycam")
    FVector GetRigVelocity() const { return RigVelocity; }

    /** Where the rig is trying to fly to (inside the envelope). */
    UFUNCTION(BlueprintPure, Category = "Camera|Skycam")
    FVector GetDesiredLocation() const { return DesiredLocation; }

    /** The point the camera looks at. */
    UFUNCTION(BlueprintPure, Category = "Camera|Skycam")
    FVector GetLookTarget() const { return LookTarget; }

    /** The skycam's view: from the rig, at the look target. */
    UFUNCTION(BlueprintPure, Category = "Camera|Skycam")
    FPSCameraShot GetShot() const;

    /** True once the rig has a position (after its first step with players on the field). */
    UFUNCTION(BlueprintPure, Category = "Camera|Skycam")
    bool IsFlying() const { return bFlying; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    UPSCameraAll22Component* GetAll22Component() const;

    /** Where to fly and look from Frame; false when it has no players. */
    bool PickTargets(const FPSSnapshotFrame& Frame, FVector& OutDesired, FVector& OutLook) const;

    UPROPERTY(Transient)
    FPSSkycamTuning Tuning;

    bool bTuningLoaded = false;

    EPSSkycamMode Mode = EPSSkycamMode::BehindQuarterback;

    bool bFlying = false;
    FVector RigLocation = FVector::ZeroVector;
    FVector RigVelocity = FVector::ZeroVector;
    FVector DesiredLocation = FVector::ZeroVector;
    FVector LookTarget = FVector::ZeroVector;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
