// PSCameraDirectorComponent.h - Epic 38: the camera director that cuts the broadcast by itself
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSCameraFraming.h"
#include "PSTelemetryBus.h"
#include "PSCameraDirectorComponent.generated.h"

class ACameraActor;
class UPSCameraAll22Component;
struct FPSSnapshotFrame;

/** The director's shot vocabulary (Epic 38). */
UENUM(BlueprintType)
enum class EPSDirectorShot : uint8
{
    None,
    /** Wide from the near sideline, level with the ball: the formation before the snap. */
    LosWide,
    /** The all-22 sideline rig (Epic 40), framing every player. */
    All22High,
    /** Tight on the live subject from the near side, leading his run. */
    TightFollow,
    /** The all-22 end-zone rig (Epic 40), behind the offense. */
    EndZone,
    /** Low and close from the near sideline on the subject: reactions after the whistle. */
    SidelineReaction
};

/** What the director cuts on: the play's phases and its events on the bus. */
UENUM(BlueprintType)
enum class EPSDirectorTrigger : uint8
{
    /** The next down lines up (PhaseChange to PreSnap). */
    PreSnap,
    Snap,
    Throw,
    Catch,
    Tackle,
    Fumble,
    Score,
    /** The whistle (PhaseChange to Scoring). */
    PlayEnd
};

/** One shot of the vocabulary, as data (Data/camera_director.json; rule 4). */
USTRUCT(BlueprintType)
struct FPSDirectorShotDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    EPSDirectorShot Shot = EPSDirectorShot::None;

    /** All22High and EndZone: the Epic 40 rig (Data/camera_all22.json) that takes the shot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    FName RigId;

    /** The other shots: camera height (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float HeightCm = 1000.f;

    /** The other shots: how far the camera stands from its target, across the field toward the
     *  camera side (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float DistanceCm = 2000.f;

    /** The other shots: horizontal field of view (degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float FieldOfView = 40.f;

    /** The other shots: the height the camera aims at above the field (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float AimHeightCm = 100.f;

    /** TightFollow: the target runs this many seconds ahead of the subject, along his velocity. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float LeadSeconds = 0.f;
};

/** Which shot a trigger asks for. */
USTRUCT(BlueprintType)
struct FPSDirectorCutRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    EPSDirectorTrigger Trigger = EPSDirectorTrigger::PreSnap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    EPSDirectorShot Shot = EPSDirectorShot::LosWide;
};

/** How the director scores each player's interest to pick the live subject. */
USTRUCT(BlueprintType)
struct FPSDirectorInterestTuning
{
    GENERATED_BODY()

    /** Added for holding the ball. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float BallWeight = 10.f;

    /** Added in full on the ball, falling to nothing at ProximityRadiusCm from it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float ProximityWeight = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float ProximityRadiusCm = 1500.f;

    /** Added for a breakaway: at least BreakawaySpeedCms with no opponent within
     *  BreakawayClearanceCm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float BreakawayWeight = 6.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float BreakawaySpeedCms = 700.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float BreakawayClearanceCm = 500.f;

    /** Added in full the moment a player takes or makes a big hit, fading to nothing over
     *  BigHitSeconds. A tackle is always big; a hit is big from BigHitDamage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float BigHitWeight = 12.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float BigHitSeconds = 2.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float BigHitDamage = 20.f;

    /** A new subject must out-score the current one by this much, so the camera doesn't
     *  flick between two close candidates. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float SwitchMargin = 2.f;
};

/** The camera director's tuning (Data/camera_director.json, Epic 38). Defaults equal the file
 *  apart from the arrays. */
USTRUCT(BlueprintType)
struct FPSCameraDirectorTuning
{
    GENERATED_BODY()

    /** Off leaves the broadcast camera to its plain follow. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    bool bDirectorEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    TArray<FPSDirectorShotDef> Shots;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    TArray<FPSDirectorCutRule> CutRules;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    FPSDirectorInterestTuning Interest;

    /** No shot is cut away from sooner than this; a cut asked for earlier waits, and the latest
     *  ask wins. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float MinShotSeconds = 1.5f;

    /** How fast the camera eases after its target within a shot (cuts are instant). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float FollowInterpSpeed = 4.f;

    /** The camera side of the line of action (the line along the field through the ball): -1
     *  the -Y sideline, +1 the +Y one. Every shot stays on it (the 180-degree rule). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    int32 CameraSide = -1;

    /** A camera this close to the line of action counts as on it (an end-zone angle), which
     *  either side may cut to (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float NeutralBandCm = 300.f;
};

/** One player, as interest scoring sees him. */
USTRUCT(BlueprintType)
struct FPSDirectorInterestInput
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    bool bHasBall = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float DistanceToBallCm = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float SpeedCms = 0.f;

    /** Distance to the nearest player of the other side (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float NearestOpponentCm = 0.f;

    /** Seconds since his last big hit; negative for none. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float SecondsSinceBigHit = -1.f;
};

/** What a shot is computed from. */
USTRUCT(BlueprintType)
struct FPSDirectorView
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    FVector BallLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    FVector SubjectLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    FVector SubjectVelocity = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    TArray<FVector> PlayerLocations;

    /** +1 when the offense attacks +X, -1 when it attacks -X. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float AttackDirection = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Director")
    float AspectRatio = 1.7778f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPSDirectorShotChangedSignature, EPSDirectorShot, Shot, FName, SubjectId);

/**
 * UPSCameraDirectorComponent is the camera director (Epic 38): it cuts the broadcast camera
 * between its shots by itself, so a whole game can be watched with no camera work.
 *
 * - Shot vocabulary: LOS wide, all-22 high, tight follow, end zone and sideline reaction. The
 *   two high shots are Epic 40's all-22 rigs, framed by UPSCameraFraming; the others stand
 *   their DistanceCm toward the camera side of their target.
 * - Cut rules: the play's phases and events on UPSTelemetryBus (rule 5) each ask for a shot
 *   (Data/camera_director.json): wide before the snap, follow from the snap, tight after the
 *   whistle.
 * - Interest: every frame the director scores each player from Epic 26's latest snapshot (the
 *   ball, closeness to it, breakaways) and the bus's tackles and hits, and the best becomes the
 *   live subject, with SwitchMargin of hysteresis.
 * - Constraints: no shot is cut away from before MinShotSeconds, and every shot stays on the
 *   CameraSide of the line of action, so a cut never flips screen direction (the 180-degree
 *   rule). Within a shot the camera eases after its target; cuts are instant.
 *
 * It lives on APSBroadcastCamera, which hands it the camera every tick while it is enabled and
 * the film view (Epic 40) is off. Headless tests step it with AdvanceTime.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSCameraDirectorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSCameraDirectorComponent();

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FPSCameraDirectorTuning& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Problems with Tuning (empty when sound): a shot defined twice or never, a rule asking
     *  for an undefined shot or a trigger ruled twice, an all-22 shot without a rig (or, given
     *  All22, one it lacks), bad numbers or camera side. */
    static TArray<FString> ValidateTuning(const FPSCameraDirectorTuning& InTuning, const FPSAll22CameraTuning* All22);

    /** Listens for the play's phases and events on the world's bus. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

    /** True when the tuning turns the director on and nothing has switched it off. */
    UFUNCTION(BlueprintPure, Category = "Camera|Director")
    bool IsDirectorEnabled();

    UFUNCTION(BlueprintCallable, Category = "Camera|Director")
    void SetDirectorEnabled(bool bEnabled);

    /** One director step: pick the subject from the latest snapshot, make a cut that has
     *  waited out MinShotSeconds, and move the camera toward the current shot. APSBroadcastCamera
     *  calls it every tick; public so headless tests can step it. */
    void AdvanceTime(float DeltaSeconds);

    /** Asks for Shot. Cuts at once when the current shot has run MinShotSeconds; otherwise the
     *  ask waits (a later ask replaces it). Asking for the shot already live does nothing.
     *  True when it cut now. */
    UFUNCTION(BlueprintCallable, Category = "Camera|Director")
    bool RequestCut(EPSDirectorShot Shot);

    /** Applies Trigger's cut rule, if it has one. */
    UFUNCTION(BlueprintCallable, Category = "Camera|Director")
    void HandleTrigger(EPSDirectorTrigger Trigger);

    UFUNCTION(BlueprintPure, Category = "Camera|Director")
    EPSDirectorShot GetCurrentShot() const { return CurrentShot; }

    /** The shot waiting out MinShotSeconds, or None. */
    UFUNCTION(BlueprintPure, Category = "Camera|Director")
    EPSDirectorShot GetPendingShot() const { return PendingShot; }

    /** Seconds since the last cut. */
    UFUNCTION(BlueprintPure, Category = "Camera|Director")
    float GetShotAge() const { return static_cast<float>(Clock - ShotStartTime); }

    /** How many cuts the director has made. */
    UFUNCTION(BlueprintPure, Category = "Camera|Director")
    int32 GetCutCount() const { return CutCount; }

    /** The live subject's PlayerId, or None. */
    UFUNCTION(BlueprintPure, Category = "Camera|Director")
    FName GetSubjectId() const { return SubjectId; }

    /** The shot the camera is easing toward (where a cut lands at once). */
    UFUNCTION(BlueprintPure, Category = "Camera|Director")
    FPSCameraShot GetTargetShot() const { return TargetShot; }

    /** The line of action the last shot was kept to the camera side of: the Y of the ball. */
    UFUNCTION(BlueprintPure, Category = "Camera|Director")
    float GetLineOfActionY() const { return static_cast<float>(LineOfActionY); }

    /** One player's interest: the weights of whatever applies to him. */
    static float ScoreInterest(const FPSDirectorInterestInput& Input, const FPSDirectorInterestTuning& InterestTuning);

    /** The index of the subject among Scores: the best, unless CurrentIndex's is within
     *  SwitchMargin of it. INDEX_NONE when Scores is empty. */
    static int32 PickSubject(const TArray<float>& Scores, int32 CurrentIndex, float SwitchMargin);

    /** Def's shot of View, before the camera-side rule. All22High and EndZone need All22's
     *  rigs. OutTarget is where it aims. */
    static FPSCameraShot ComputeShot(const FPSDirectorShotDef& Def, const FPSDirectorView& View,
        const FPSAll22CameraTuning* All22, int32 InCameraSide, FVector& OutTarget);

    /**
     * The 180-degree rule. A shot more than NeutralBandCm on the wrong side of the line of
     * action (Y = LineY) moves to its mirror image on the camera side and re-aims at Target.
     * True when it had to move.
     */
    static bool EnforceCameraSide(FPSCameraShot& Shot, const FVector& Target, double LineY, int32 InCameraSide, float NeutralBandCm);

    /** True when Shot is on the camera side of the line, or within the neutral band of it. */
    static bool IsOnCameraSide(const FPSCameraShot& Shot, double LineY, int32 InCameraSide, float NeutralBandCm);

    /** Fires on every cut, with the new shot and the subject. */
    UPROPERTY(BlueprintAssignable, Category = "Camera|Director")
    FPSDirectorShotChangedSignature OnShotChanged;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);
    void HandleThrow(const FPSTelemetryThrowEvent& Event);
    void HandleCatch(const FPSTelemetryCatchEvent& Event);
    void HandleTackle(const FPSTelemetryTackleEvent& Event);
    void HandleFumble(const FPSTelemetryFumbleEvent& Event);
    void HandleScore(const FPSTelemetryScoreEvent& Event);
    void HandleDamage(const FPSTelemetryDamageEvent& Event);

    ACameraActor* GetCamera() const;
    UPSCameraAll22Component* GetAll22Component() const;
    const FPSDirectorShotDef* FindShotDef(EPSDirectorShot Shot) const;

    /** Builds this step's view from Frame and picks the subject; false when the frame has no
     *  players. */
    bool BuildView(const FPSSnapshotFrame& Frame, FPSDirectorView& OutView);

    void CutTo(EPSDirectorShot Shot);
    void ApplyToCamera(bool bSnap, float DeltaSeconds);

    UPROPERTY(Transient)
    FPSCameraDirectorTuning Tuning;

    bool bTuningLoaded = false;

    /** Switched off at runtime (SetDirectorEnabled), whatever the tuning says. */
    bool bDisabledAtRuntime = false;

    double Clock = 0.0;
    double ShotStartTime = 0.0;
    int32 CutCount = 0;

    EPSDirectorShot CurrentShot = EPSDirectorShot::None;
    EPSDirectorShot PendingShot = EPSDirectorShot::None;

    /** Set by a cut; the next step snaps the camera rather than easing. */
    bool bSnapNextStep = false;

    FName SubjectId;
    FPSCameraShot TargetShot;
    double LineOfActionY = 0.0;

    /** When each player (by display name, as the bus names him) last took or made a big hit. */
    TMap<FString, double> BigHitTimes;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
