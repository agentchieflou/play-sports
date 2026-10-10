// PSAudioSubsystem.h - Epic 23.1: gameplay moments on the telemetry bus become sound
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSAudioTypes.h"
#include "PSPlatformTiers.h"
#include "PSTelemetryBus.h"
#include "PSAudioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class UPSSettingsSubsystem;

DECLARE_MULTICAST_DELEGATE_OneParam(FPSAudioCueRequestedMC, const FPSAudioCueRequest& /* Request */);

/**
 * UPSAudioSubsystem is the game's audio event mapping (Epic 23.1). Every sound it makes answers a
 * telemetry bus event (the reality note of Track H): it never polls the game or asks the game mode.
 *
 *  - Moments: each bus event it hears becomes an FPSAudioMoment, a trigger (EPSAudioTrigger) with a
 *    Detail that narrows it: the snap; the whistle (the game state or a phase change going from a
 *    live phase to a dead one, once a play); a tackle (Sack); a hit (Big at BigHitDamage, its
 *    intensity the damage over FullIntensityDamage); linemen engaging (a pass-rush move, Won or
 *    Stopped); a throw (Deep at DeepPassCm); a catch (Interception); a fumble (Turnover); a kick;
 *    the score and the result of a play (the simulation's PlayResult); a flag (the simulation's
 *    Penalty); a timeout; a carrier crossing the goal line; the crowd's level and reactions
 *    (UPSCrowdExcitementSubsystem's Crowd events); a pre-snap call (the cadence); a quarter's end;
 *    a line the commentary booth said (Epic 96: its LineId, for the line's recorded voice-over).
 *  - Cues: Data/audio_cues.json maps each trigger (and Detail) to cues; every matching rule plays.
 *    A cue's sound is a soft object path, empty until an editor session imports it: the request is
 *    made and recorded either way, so the mapping is testable headlessly.
 *  - The mix: a cue plays at its Volume times its layer's volume setting (LayerSettings, the
 *    ui_settings.json sliders); a muted layer drops it. A cue doesn't repeat within its
 *    CooldownSeconds.
 *  - Voices: at most the platform tier's AudioMaxVoices one-shots sound at once
 *    (Data/platform_tiers.json); a cue takes the voice of the lowest-priority one below it, or is
 *    dropped. A one-shot holds its voice for its sound's length (DurationSeconds without one). Loops
 *    (a crowd bed, the ambience) play one per LoopGroup, outside the voice count.
 *  - Its update (pruning finished voices, following the volume settings) runs AudioUpdateHz times
 *    a second (every frame at 0), timed under PS_PERF_SCOPE(Audio).
 *  - RequestCue is the hook for sounds the bus doesn't carry: an animation's footsteps (Epic 98).
 *
 * Every request is kept in a log (GetRequestLog, the latest MaxRequestsKept) and told to
 * OnCueRequestedMC. In a match it binds to the world's bus when the world begins play; headless
 * tests call BindToBus and AdvanceTime themselves.
 */
UCLASS()
class PLAYSPORTS_API UPSAudioSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    // --- Tuning ----------------------------------------------------------------------------

    static FString GetDefaultTuningPath();

    /** The tuning, loaded from the default path on first use. */
    const FPSAudioTuning& GetTuning();

    /** Replaces the tuning with JsonFilePath's (through UPSDataIngestion); a file that can't be
     *  read or fails ValidateTuning is refused and the current tuning kept. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Applies InTuning when it passes ValidateTuning. */
    bool SetTuning(const FPSAudioTuning& InTuning);

    /** Problems with InTuning, one line each (empty when sound): cue ids empty or repeated,
     *  volumes outside 0-1, priorities outside 0-100, negative times, a loop without a group, a
     *  rule or startup loop naming an unknown cue, a startup cue that isn't a loop, a layer with
     *  two settings, thresholds not above 0, no log. */
    static TArray<FString> ValidateTuning(const FPSAudioTuning& InTuning);

    /** The cue CueId, or null. */
    const FPSAudioCueDef* FindCue(FName CueId);

    // --- The platform tier ---------------------------------------------------------------

    /** Takes the tier's AudioUpdateHz and AudioMaxVoices. BindToBus applies the active tier. */
    void ApplyPlatformTier(const FPSPlatformTier& Tier);

    int32 GetMaxVoices() const { return MaxVoices; }

    // --- The bus ---------------------------------------------------------------------------

    /** Hears Bus's events. OnWorldBeginPlay binds the world's bus. */
    void BindToBus(UPSTelemetryBus* Bus);

    void UnbindFromBus();

    bool IsBound() const { return BoundBus.IsValid(); }

    // --- Cues --------------------------------------------------------------------------------

    /** Plays every cue Data/audio_cues.json maps Moment to. Returns how many took a voice. */
    int32 HandleMoment(const FPSAudioMoment& Moment);

    /** Asks for CueId outside the bus's moments: an animation's footstep, a level's emitter
     *  (Epic 98). True when it took a voice. */
    UFUNCTION(BlueprintCallable, Category = "Audio")
    bool RequestCue(FName CueId, FVector Location, float Intensity = 1.f);

    /** Stops the loop playing in LoopGroup. */
    UFUNCTION(BlueprintCallable, Category = "Audio")
    void StopLoop(FName LoopGroup);

    /** The cue looping in LoopGroup, or None. */
    UFUNCTION(BlueprintPure, Category = "Audio")
    FName GetActiveLoop(FName LoopGroup) const;

    /** One-shots holding a voice now. */
    UFUNCTION(BlueprintPure, Category = "Audio")
    int32 GetActiveVoiceCount() const { return Voices.Num(); }

    /** A layer's volume, 0-1: its setting (a 0-100 slider), or 1 without one. */
    float GetLayerVolume(EPSAudioLayer Layer);

    /** The settings the layer volumes come from: the game instance's, or these (tests). */
    void SetSettings(UPSSettingsSubsystem* InSettings) { SettingsOverride = InSettings; }

    /** One step: the audio's clock moves on by DeltaSeconds, and every 1 / AudioUpdateHz it
     *  releases the voices whose sounds have ended and follows the volume settings. The tick calls
     *  it. */
    void AdvanceTime(float DeltaSeconds);

    // --- The log -----------------------------------------------------------------------------

    /** Every request, oldest first (the latest MaxRequestsKept). */
    const TArray<FPSAudioCueRequest>& GetRequestLog() const { return RequestLog; }

    /** How many of the log's requests asked for CueId (only those that took a voice, unless
     *  bIncludeDropped). */
    int32 CountRequests(FName CueId, bool bIncludeDropped = false) const;

    /** The latest request for CueId in the log. */
    bool FindLastRequest(FName CueId, FPSAudioCueRequest& OutRequest) const;

    void ClearRequestLog() { RequestLog.Reset(); }

    /** A cue was asked for, played or dropped. */
    FPSAudioCueRequestedMC OnCueRequestedMC;

private:
    /** A one-shot holding a voice, or a group's loop. */
    struct FVoice
    {
        FName CueId;
        int32 Priority = 0;
        float EndsAt = 0.f;
        TWeakObjectPtr<UAudioComponent> Component;
    };

    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);
    void HandleTackle(const FPSTelemetryTackleEvent& Event);
    void HandleDamage(const FPSTelemetryDamageEvent& Event);
    void HandlePassRush(const FPSTelemetryPassRushEvent& Event);
    void HandleThrow(const FPSTelemetryThrowEvent& Event);
    void HandleCatch(const FPSTelemetryCatchEvent& Event);
    void HandleFumble(const FPSTelemetryFumbleEvent& Event);
    void HandleKick(const FPSTelemetryKickEvent& Event);
    void HandlePlayResult(const FPSTelemetryPlayResultEvent& Event);
    void HandlePenalty(const FPSTelemetryPenaltyEvent& Event);
    void HandleTimeout(const FPSTelemetryTimeoutEvent& Event);
    void HandleBoundaryCrossed(const FPSTelemetryBoundaryCrossedEvent& Event);
    void HandleCrowd(const FPSTelemetryCrowdEvent& Event);
    void HandlePreSnap(const FPSTelemetryPreSnapEvent& Event);
    void HandleSpeech(const FPSTelemetrySpeechEvent& Event);

    /** The play's phase is now Phase: a live phase starts the play, a dead one after a live one
     *  blows the whistle. */
    void FollowPhase(const FString& Phase);

    /** Asks for Def for Moment: cooldown, mute and voices, then plays and records it. */
    bool PlayCue(const FPSAudioCueDef& Def, const FPSAudioMoment& Moment);

    /** Adds Request to the log and tells the listeners. */
    void Record(const FPSAudioCueRequest& Request);

    /** Starts Sound (when one is imported) for Def; null without a sound or an audio device. */
    UAudioComponent* StartSound(const FPSAudioCueDef& Def, float Volume, const FVector& Location);

    /** Loads the cues' imported sounds, once the world plays. */
    void PreloadSounds();

    UPSSettingsSubsystem* GetSettings() const;

    UPROPERTY(Transient)
    FPSAudioTuning Tuning;

    bool bTuningLoaded = false;

    /** The cues' sounds, by cue id, once loaded. */
    UPROPERTY(Transient)
    TMap<FName, USoundBase*> LoadedSounds;

    UPROPERTY(Transient)
    TArray<FPSAudioCueRequest> RequestLog;

    TArray<FVoice> Voices;

    /** Each group's loop. */
    TMap<FName, FVoice> Loops;

    /** When each cue last took a voice, on the audio's clock. */
    TMap<FName, float> LastPlayed;

    /** Each loop's volume when it started, so the update can follow the settings. */
    TMap<FName, float> LoopBaseVolume;

    float Clock = 0.f;
    float SinceUpdate = 0.f;
    float UpdateIntervalSeconds = 0.f;
    int32 MaxVoices = 32;

    /** The play's state, as the bus told it: for the whistle and the quarters. */
    bool bBallLive = false;
    bool bHaveQuarter = false;
    int32 LastQuarter = 1;

    UPROPERTY(Transient)
    UPSSettingsSubsystem* SettingsOverride = nullptr;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
