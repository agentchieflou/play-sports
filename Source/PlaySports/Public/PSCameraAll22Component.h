// PSCameraAll22Component.h - Epic 40: the all-22 coaches film view on the broadcast camera
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSCameraFraming.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSCameraAll22Component.generated.h"

class ACameraActor;
class APSPlayerController;

/** One player in an exported film frame. */
USTRUCT(BlueprintType)
struct FPSFilmFramePlayer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    EPSTeamSide TeamSide = EPSTeamSide::Offense;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    FVector WorldLocation = FVector::ZeroVector;

    /** Where the player stands in the frame, normalized: (0,0) top left, (1,1) bottom right. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    FVector2D ScreenPosition = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    bool bInFrame = false;
};

/**
 * One frame of film, as the analysis tools (the telestrator, Epic 44) need it: the exact shot
 * and every player's place in it, so drawings can be pinned to players and the field.
 */
USTRUCT(BlueprintType)
struct FPSFilmFrame
{
    GENERATED_BODY()

    /** Counts up from 0 for each frame a camera captures. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    int32 FrameIndex = 0;

    /** World time (s): the telemetry bus's clock, so a frame lines up with the event history
     *  and with replay events (FPSReplayEventRecord::TimestampSeconds). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    float TimestampSeconds = 0.f;

    /** The all-22 rig the frame was taken from; None for the broadcast view. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    FName RigId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    FPSCameraShot Shot;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    TArray<FPSFilmFramePlayer> Players;

    /** The still requested alongside an exported frame; empty when none was (no game viewport,
     *  e.g. headless runs). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Film")
    FString ImageFile;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSFilmFrameSignature, const FPSFilmFrame&, Frame);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSFilmViewChangedSignature, FName, RigId);

/**
 * UPSCameraAll22Component is the all-22 coaches film view (Epic 40). It lives on
 * APSBroadcastCamera, so the broadcast camera stays the game's one view and film is a mode of it
 * rather than a second camera.
 *
 * The rigs are fixed and elevated: one high on the sideline, one high behind the offense's end
 * zone (Data/camera_all22.json). In film view the camera stands on the active rig and frames all
 * 22 players with UPSCameraFraming. Widening is immediate and closing in eases at ReframeSpeed, so
 * every frame holds every player. The end-zone rig stands behind the offense: which way it
 * attacks is read from the formation at each cut and at each snap on UPSTelemetryBus.
 *
 * Toggling: the catalog's FilmView action (View, or F) steps broadcast -> each rig in file order
 * -> broadcast. The component hears it on the APSPlayerController currently viewing through
 * the camera (APSBroadcastCamera binds it in BecomeViewTarget). Leaving film view puts the
 * broadcast view back as it was. Film view turns motion blur off: frames are for analysis.
 *
 * Frame export (the hook for Epic 44): CaptureFrame records the live shot and each player's
 * place in it and fires OnFrameCaptured; ExportFrame also writes it as JSON, plus a screenshot
 * request when a game viewport exists.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSCameraAll22Component : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSCameraAll22Component();

    static FString GetDefaultTuningPath();

    /** Saved/Film: where ExportFrame writes when given no directory. */
    static FString GetDefaultExportDirectory();

    /** The tuning in use, loaded from the default path on first use. If the file can't be
     *  read, a single sideline rig with the struct defaults stands in. */
    const FPSAll22CameraTuning& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** The catalog action that toggles the view (Data/input_actions.json). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|All22")
    FName ToggleActionId;

    /** Listens to Controller's catalog actions for the toggle. Idempotent. */
    void BindToController(APSPlayerController* Controller);

    void UnbindFromController(APSPlayerController* Controller);

    /** Listens for the snap on the world's telemetry bus. Idempotent. */
    void BindToBus();

    void UnbindFromBus();

    /** Steps the view: broadcast -> each rig in file order -> broadcast. Returns the rig now
     *  live, or None for the broadcast view. */
    UFUNCTION(BlueprintCallable, Category = "Camera|All22")
    FName CycleView();

    /** Cuts to the rig named RigId, or back to the broadcast view for None. False when no rig
     *  has that name. */
    UFUNCTION(BlueprintCallable, Category = "Camera|All22")
    bool SetFilmView(FName RigId);

    UFUNCTION(BlueprintPure, Category = "Camera|All22")
    bool IsFilmViewActive() const { return ActiveRigIndex != INDEX_NONE; }

    /** The live rig, or None for the broadcast view. */
    UFUNCTION(BlueprintPure, Category = "Camera|All22")
    FName GetActiveRigId() const;

    /** +1 when the offense attacks +X, -1 when it attacks -X. */
    UFUNCTION(BlueprintPure, Category = "Camera|All22")
    float GetAttackDirection() const { return AttackDirection; }

    /** The owning camera's view right now, whichever mode is live. */
    UFUNCTION(BlueprintPure, Category = "Camera|All22")
    FPSCameraShot GetCurrentShot() const;

    /** Reframes the players from the active rig. APSBroadcastCamera calls it from Tick while
     *  film view is on; public so headless tests can step it. Does nothing in broadcast view. */
    void StepFraming(float DeltaTime);

    /** Records the live view and every player's place in it, and fires OnFrameCaptured. */
    UFUNCTION(BlueprintCallable, Category = "Camera|Film")
    FPSFilmFrame CaptureFrame();

    /** CaptureFrame, then writes the frame as JSON to Directory (Saved/Film when empty) and,
     *  when a game viewport exists, requests a screenshot beside it. False when the file can't
     *  be written. */
    UFUNCTION(BlueprintCallable, Category = "Camera|Film")
    bool ExportFrame(const FString& Directory, FPSFilmFrame& OutFrame);

    /** Fires for every captured frame (the telestrator's hook, Epic 44). */
    UPROPERTY(BlueprintAssignable, Category = "Camera|Film")
    FPSFilmFrameSignature OnFrameCaptured;

    /** Fires when the view changes, with the rig now live (None for the broadcast view). */
    UPROPERTY(BlueprintAssignable, Category = "Camera|All22")
    FPSFilmViewChangedSignature OnFilmViewChanged;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleActionStarted(FName ActionId);

    void HandleSnap(const FPSTelemetrySnapEvent& Event);

    ACameraActor* GetCamera() const;
    void GatherPlayers(TArray<APSPlayerPawn*>& OutPlayers) const;
    static TArray<FVector> GetLocations(const TArray<APSPlayerPawn*>& Players);

    /** +1 when the defense lines up at higher X than the offense, -1 when lower; Fallback when
     *  either side is missing or they are level. */
    static float ReadAttackDirection(const TArray<APSPlayerPawn*>& Players, float Fallback);

    float ResolveAspectRatio() const;
    void EnterRig(int32 RigIndex);
    void LeaveFilmView();

    /** Reads the attack direction from the formation and frames the players from the active
     *  rig at once, with no easing. */
    void CutToActiveRig();

    void ApplyShot(const FPSCameraShot& Shot);

    UPROPERTY(Transient)
    FPSAll22CameraTuning Tuning;

    bool bTuningLoaded = false;

    int32 ActiveRigIndex = INDEX_NONE;

    /** The region the live film shot holds: always contains the players' padded box. */
    FBox FramedBox = FBox(ForceInit);

    /** Read from the formation at each cut and each snap, and held in between. */
    float AttackDirection = 1.f;

    /** The broadcast view to restore on leaving film view. */
    FTransform ReturnTransform = FTransform::Identity;
    float ReturnFieldOfView = 90.f;
    bool bReturnMotionBlurOverride = false;
    float ReturnMotionBlurAmount = 0.f;

    int32 NextFrameIndex = 0;

    TArray<TWeakObjectPtr<APSPlayerController>> BoundControllers;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
