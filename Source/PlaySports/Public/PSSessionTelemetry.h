// PSSessionTelemetry.h - Epic 117: session health from the telemetry bus
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "PSSessionTelemetryTypes.h"
#include "PSTelemetryBus.h"
#include "PSSessionTelemetry.generated.h"

class UPSSaveSubsystem;

/**
 * One session per game world (Epic 117): the mode it was opened in, the plays snapped on the
 * telemetry bus, and frame-time percentiles. It listens to the bus (Architecture rule 5) and
 * owns nothing else's state.
 *
 * - The crash context: on every bus event it refreshes the PS.* keys a crash report carries
 *   (FPSCrashContext), breadcrumbs taken from the bus's own event history.
 * - Persistence, only when the player opted in (UPSSessionTelemetrySave): the session is saved
 *   as open at BeginPlay, again every CheckpointEveryPlays plays, and as ended cleanly at the
 *   world's cleanup. A stored session that never ended cleanly is how a crash shows up in
 *   session health.
 *
 * Headless tests have no BeginPlay and no ticking: they call StartSession and RecordFrame.
 */
UCLASS()
class PLAYSPORTS_API UPSSessionTelemetrySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    /** Opens the session in InMode. InSaves is where an opted-in player's sessions go (null:
     *  memory and the crash context only); InSlotName is the store's slot (empty: the game's). */
    void StartSession(const FString& InMode, UPSSaveSubsystem* InSaves, const FString& InSlotName = FString());

    /** Closes the session as ended cleanly and saves it if the player opted in. The world's
     *  cleanup calls it; it does nothing when no session is open. */
    void EndSession();

    /** Adds one frame of real (undilated) time to the session. The tick passes the engine's
     *  frame time; tests pass their own. */
    void RecordFrame(float DeltaSeconds);

    /** The session so far. */
    FPSSessionSummary BuildSummary(bool bEndedCleanly) const;

    bool IsSessionOpen() const { return bSessionOpen; }

    int32 GetPlayCount() const { return PlayCount; }

    bool LoadTuningFromJson(const FString& JsonFilePath);

    const FPSSessionTelemetryTuning& GetTuning();

    static FString GetDefaultTuningPath();

    /** Problems with Candidate (empty when sound); LoadTuningFromJson keeps the defaults over
     *  tuning with any. */
    static TArray<FString> ValidateTuning(const FPSSessionTelemetryTuning& Candidate);

    /** The session's mode from a travel URL: its mode option ("?mode=PlayNow"), else its game
     *  option ("?game=Menu"), else "Unspecified". */
    static FString ModeFromURL(const FURL& URL);

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandleEventRecorded(const FPSTelemetryEvent& Event);
    void HandleWorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources);

    /** Rewrites the crash context's session keys and breadcrumbs. */
    void RefreshCrashContext();

    /** Saves the session to the store when the player opted in. */
    void Persist(bool bEndedCleanly);

    void BindToBus(UPSTelemetryBus* Bus);

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;

    /** Held from StartSession so the session can still be saved at shutdown, after the game
     *  instance's subsystems are gone (its saves are stateless). */
    UPROPERTY(Transient)
    UPSSaveSubsystem* Saves = nullptr;

    FString StoreSlot;
    FPSSessionTelemetryTuning Tuning;
    bool bTuningLoaded = false;

    bool bSessionOpen = false;
    FGuid SessionId;
    FString Mode;
    FString PlatformTier;
    int32 PlayCount = 0;
    float DurationSeconds = 0.f;
    FPSFrameTimeHistogram FrameTimes;
};
