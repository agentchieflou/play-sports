// PSCrowdExcitementSubsystem.h - Epic 23.2: the crowd's excitement, the one authority on it
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSCrowdTypes.h"
#include "PSPlatformTiers.h"
#include "PSTelemetryBus.h"
#include "PSCrowdExcitementSubsystem.generated.h"

/**
 * UPSCrowdExcitementSubsystem is the stadium crowd's excitement (Epic 23.2): the one authority on
 * how excited the crowd is and how loud that makes it. Everything about the crowd reads it rather
 * than keeping its own: the audio plays its bed and stingers from its Crowd events
 * (UPSAudioSubsystem, layered by Epic 97); Epic 49's behavior model (reaction animations,
 * crowd-noise pressure on the visitors, rivalry intensity) extends it rather than adding another.
 *
 *  - Excitement, 0-1, settles toward a resting level with a half-life (HalfLifeSeconds). The
 *    crowd rests higher late in a close game (LateGameQuarter, CloseGameMargin, LateCloseBonus).
 *  - Moments (EPSCrowdStimulus) come from the bus: a deep ball (gasp), a sack, a big hit, an
 *    interception, a lost fumble and a carrier crossing the goal line as they happen; the play's
 *    result (UPSPlaySimulation's PlayResult, the authority) for the score, a field goal, a big
 *    gain, a first down, an incompletion, a turnover on downs and what the live events missed;
 *    a flag (the simulation's Penalty). Each benefits one team.
 *  - Home and away: the crowd is HomeShare home fans (DefaultHomeShare, or SetMatchContext). A
 *    moment's fans add FansDelta, the other side's RivalsDelta, each by its share; the majority
 *    reacts. A home touchdown erupts; a visitors' touchdown stuns the stadium quiet.
 *  - Levels (Hush, Murmur, Buzz, Roar, Eruption) rate the excitement, with LevelHysteresis.
 *  - Every reaction and level change goes out on the bus as a Crowd event.
 *  - Its update (settling, re-rating) runs the platform tier's CrowdUpdateHz times a second
 *    (Data/platform_tiers.json), timed under PS_PERF_SCOPE(Crowd). The settling is exact for any
 *    step, so the rate changes only how often the level is re-rated.
 *
 * In a match it binds to the world's bus when the world begins play; headless tests call
 * BindToBus and AdvanceTime themselves. Tuning: Data/crowd.json.
 */
UCLASS()
class PLAYSPORTS_API UPSCrowdExcitementSubsystem : public UTickableWorldSubsystem
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
    const FPSCrowdTuning& GetTuning();

    /** Replaces the tuning with JsonFilePath's (through UPSDataIngestion); a file that can't be
     *  read or fails ValidateTuning is refused and the current tuning kept. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Applies InTuning when it passes ValidateTuning. */
    bool SetTuning(const FPSCrowdTuning& InTuning);

    /** Problems with InTuning, one line each (empty when sound): 0-1 levels and shares, positive
     *  times and thresholds, every level once with Hush at 0 and thresholds rising, every
     *  stimulus once with deltas in -1..1. */
    static TArray<FString> ValidateTuning(const FPSCrowdTuning& InTuning);

    // --- The model (pure) ------------------------------------------------------------------

    /** Where the crowd settles: RestingExcitement, plus LateCloseBonus late in a close game. */
    static float ComputeRestingExcitement(const FPSCrowdTuning& InTuning, int32 InQuarter, int32 InHomeScore, int32 InAwayScore);

    /** Excitement after Seconds settling toward Rest with HalfLifeSeconds: exact for any step. */
    static float SettleToward(float InExcitement, float Rest, float HalfLifeSeconds, float Seconds);

    /** The level for Excitement, from Current: up as soon as a threshold is reached, down only
     *  LevelHysteresis under the current level's. */
    static EPSCrowdLevel RateLevel(const FPSCrowdTuning& InTuning, float InExcitement, EPSCrowdLevel Current);

    /** How much a stimulus moves the crowd when its benefiting team's fans are FansShare of it. */
    static float ExcitementDelta(const FPSCrowdReactionDef& Def, float FansShare);

    /** The majority's reaction: the fans' when they are at least half the crowd. */
    static EPSCrowdReaction ChooseReaction(const FPSCrowdReactionDef& Def, float FansShare);

    // --- The match -------------------------------------------------------------------------

    /** Who plays and how the stadium splits: InHomeShare of it home fans (0-1; negative for the
     *  tuning's DefaultHomeShare). The game mode sets it from the match's setup. */
    UFUNCTION(BlueprintCallable, Category = "Crowd")
    void SetMatchContext(FName InHomeTeamId, FName InAwayTeamId, float InHomeShare);

    UFUNCTION(BlueprintPure, Category = "Crowd")
    float GetHomeShare();

    UFUNCTION(BlueprintPure, Category = "Crowd")
    FName GetHomeTeamId() const { return HomeTeamId; }

    UFUNCTION(BlueprintPure, Category = "Crowd")
    FName GetAwayTeamId() const { return AwayTeamId; }

    // --- The crowd -------------------------------------------------------------------------

    /** Hears Bus's moments and settles the crowd at rest; the game's first state announces its
     *  level. OnWorldBeginPlay binds the world's bus. */
    void BindToBus(UPSTelemetryBus* Bus);

    void UnbindFromBus();

    /** Takes the tier's CrowdUpdateHz. BindToBus applies the active tier. */
    void ApplyPlatformTier(const FPSPlatformTier& Tier);

    /** The crowd reacts to Stimulus, which went the home team's way or not, at Scale (Epic 49's
     *  rivalry intensity scales it). Returns its reaction (None when the tuning has none). */
    UFUNCTION(BlueprintCallable, Category = "Crowd")
    EPSCrowdReaction ApplyStimulus(EPSCrowdStimulus Stimulus, bool bHomeBenefits, float Scale = 1.f);

    /** One step: every 1 / CrowdUpdateHz the excitement settles by the time passed and the level
     *  is re-rated. The tick calls it. */
    void AdvanceTime(float DeltaSeconds);

    UFUNCTION(BlueprintPure, Category = "Crowd")
    float GetExcitement() const { return Excitement; }

    UFUNCTION(BlueprintPure, Category = "Crowd")
    EPSCrowdLevel GetLevel() const { return Level; }

    /** Where the crowd settles now (the game's quarter and score). */
    UFUNCTION(BlueprintPure, Category = "Crowd")
    float GetRestingExcitement();

    UFUNCTION(BlueprintPure, Category = "Crowd")
    EPSCrowdReaction GetLastReaction() const { return LastReaction; }

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);
    void HandleThrow(const FPSTelemetryThrowEvent& Event);
    void HandleCatch(const FPSTelemetryCatchEvent& Event);
    void HandleTackle(const FPSTelemetryTackleEvent& Event);
    void HandleDamage(const FPSTelemetryDamageEvent& Event);
    void HandleFumble(const FPSTelemetryFumbleEvent& Event);
    void HandleBoundaryCrossed(const FPSTelemetryBoundaryCrossedEvent& Event);
    void HandlePenalty(const FPSTelemetryPenaltyEvent& Event);
    void HandlePlayResult(const FPSTelemetryPlayResultEvent& Event);

    /** Re-rates the level and announces a change. */
    void UpdateLevel();

    /** Announces the level as it is: at the game's first state and a new game's. */
    void AnnounceLevel();

    void Publish(const FPSTelemetryCrowdEvent& Event);

    UPROPERTY(Transient)
    FPSCrowdTuning Tuning;

    bool bTuningLoaded = false;

    float Excitement = 0.25f;
    EPSCrowdLevel Level = EPSCrowdLevel::Murmur;
    bool bLevelAnnounced = false;
    EPSCrowdReaction LastReaction = EPSCrowdReaction::None;

    FName HomeTeamId;
    FName AwayTeamId;
    float HomeShare = -1.f;

    /** The game, as the bus last told it. */
    int32 Quarter = 1;
    int32 HomeScore = 0;
    int32 AwayScore = 0;
    bool bHomeOffense = true;
    int32 CompletedDrives = 0;

    /** What the crowd already reacted to live this play, so the play's result doesn't repeat it. */
    bool bTouchdownThisPlay = false;
    bool bTurnoverThisPlay = false;
    bool bSackThisPlay = false;

    float SinceUpdate = 0.f;
    float UpdateIntervalSeconds = 0.f;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
