// PSHighlightSubsystem.h - Epic 42: the game finds its own big plays and plays them back as a reel
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSHighlightTypes.h"
#include "PSTelemetryBus.h"
#include "PSHighlightSubsystem.generated.h"

class UPSReplaySubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPSHighlightReelSignature);

/**
 * UPSHighlightSubsystem finds a game's big moments by itself and assembles them into a highlight
 * reel (Epic 42).
 *
 *  - Importance: it follows every play on the telemetry bus, from the snap to the moment the
 *    play is settled (the first game state after the whistle that isn't the whistle's own, or
 *    SettleAfterWhistleSeconds after it, or the next snap). It reads the play's yards (the
 *    simulation's PlayResult, the outcome authority's measure from the line of scrimmage;
 *    without one, the longest catch, else the yard line's move), the points (the game state's score,
 *    the authority, or Score events), a turnover (an interception, a lost fumble, the ball
 *    changing hands on a play that wasn't a kick), the tackles the carrier broke (a hit he
 *    survived) and the swing in the home team's chance of winning (HomeWinProbability), and
 *    weighs them by Data/highlights.json.
 *  - Clips: a play at least MinImportance and among the ReelSize most important so far is cut
 *    into a clip as it settles (UPSReplaySubsystem::CaptureClip, Epic 41), with the director's
 *    angle for its kind and a slow-motion beat around its key moment (the score, the turnover,
 *    the catch, the broken tackle, the tackle, by that rank). Only the reel's plays keep a clip.
 *  - The package: PlayReel plays the reel in game order through the replay system, each clip
 *    from its kind's angle, slowing to BeatPlaybackRate through its beat. At the end of the
 *    game (a game state past the fourth quarter) it plays by itself. The viewer's exit from a
 *    replay ends the reel.
 *  - The franchise (Track G): ArchiveForSeason saves the reel's clips and adds them to the
 *    season's highlights (UPSFranchiseSaveGame::SeasonHighlights), keeping its
 *    SeasonHighlightsKept most important.
 *
 * It ticks with its world, paused or not (the reel plays with the game paused) and calls
 * AdvanceTime; headless tests call AdvanceTime themselves.
 */
UCLASS()
class PLAYSPORTS_API UPSHighlightSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickableWhenPaused() const override { return true; }

    // --- Tuning --------------------------------------------------------------------------

    static FString GetDefaultTuningPath();

    /** The tuning, loaded from the default path on first use. */
    const FPSHighlightTuning& GetTuning();

    /** Replaces the tuning with JsonFilePath's (through UPSDataIngestion); a file that can't be
     *  read or fails ValidateTuning is refused and the current tuning kept. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Applies NewTuning when it passes ValidateTuning. */
    bool SetTuning(const FPSHighlightTuning& NewTuning);

    /** Problems with InTuning, one line each (empty when sound): negative weights or times, no
     *  reel or season size, a kind without a shot or with two, a beat rate not in (0, 1], win
     *  probability numbers not above 0. */
    static TArray<FString> ValidateTuning(const FPSHighlightTuning& InTuning);

    // --- Importance ----------------------------------------------------------------------

    /** The home team's chance of winning from State (0 to 1); past the fourth quarter the game
     *  is over: 1, 0, or 0.5 for a tie. */
    static float HomeWinProbability(const FPlayState& State, const FPSWinProbabilityTuning& InTuning);

    /** A play's importance: each part of Impact by its weight. */
    static float ScoreImportance(const FPSPlayImpact& Impact, const FPSHighlightTuning& InTuning);

    /** Score if points were scored, else Turnover if the ball changed hands, else BigPlay. */
    static EPSHighlightKind ClassifyPlay(const FPSPlayImpact& Impact);

    // --- The game's highlights -------------------------------------------------------------

    /** One step: settles a play whose whistle is SettleAfterWhistleSeconds old, starts the reel
     *  once the game is over, and moves the reel along. The tick calls it. */
    void AdvanceTime(float DeltaSeconds);

    /** The reel's plays, in game order. */
    UFUNCTION(BlueprintCallable, Category = "Highlights")
    TArray<FPSPlayHighlight> GetReel() const;

    /** Every play settled this game, with its importance (the reel is the best of them). */
    UFUNCTION(BlueprintCallable, Category = "Highlights")
    TArray<FPSPlayImpact> GetPlayImpacts() const { return PlayImpacts; }

    /** Forgets the game: its plays, its reel, the game-over state. */
    UFUNCTION(BlueprintCallable, Category = "Highlights")
    void ResetGame();

    // --- The package -----------------------------------------------------------------------

    /** Plays the reel from its first clip. False when it has no clips. */
    UFUNCTION(BlueprintCallable, Category = "Highlights")
    bool PlayReel();

    /** Ends the reel and its replay. */
    UFUNCTION(BlueprintCallable, Category = "Highlights")
    void StopReel();

    UFUNCTION(BlueprintPure, Category = "Highlights")
    bool IsPlayingReel() const { return bPlayingReel; }

    /** The reel's clip playing, from 0. */
    UFUNCTION(BlueprintPure, Category = "Highlights")
    int32 GetReelIndex() const { return ReelIndex; }

    /** True from the end of the game until the reel starts by itself. */
    UFUNCTION(BlueprintPure, Category = "Highlights")
    bool IsReelPending() const { return GameEndCountdown >= 0.f; }

    /** Fires when the reel has played its last clip, or the viewer left it. */
    UPROPERTY(BlueprintAssignable, Category = "Highlights")
    FPSHighlightReelSignature OnReelFinished;

    // --- The franchise ---------------------------------------------------------------------

    /** Highlights/ under the replay system's save directory. */
    static FString GetDefaultArchiveDirectory();

    /**
     * Saves the reel's clips to Directory (GetDefaultArchiveDirectory when empty) and adds the
     * reel to InOutSeason as a game of Week between HomeTeamId and AwayTeamId, keeping the
     * season's SeasonHighlightsKept most important; the clip files of those it drops are
     * deleted. Returns how many of this game's highlights the season kept.
     */
    UFUNCTION(BlueprintCallable, Category = "Highlights")
    int32 ArchiveForSeason(UPARAM(ref) TArray<FPSSeasonHighlight>& InOutSeason, int32 Week, FName HomeTeamId, FName AwayTeamId, const FString& Directory);

    /** Keeps InOutSeason's MaxKept most important, in the order they were added; OutDropped
     *  gets the others. */
    static void KeepSeasonBest(TArray<FPSSeasonHighlight>& InOutSeason, int32 MaxKept, TArray<FPSSeasonHighlight>& OutDropped);

private:
    /** The play being followed, from its snap until it settles. */
    struct FPlayTrack
    {
        bool bActive = false;
        int32 PlayNumber = 0;
        int32 SnapSequence = 0;
        int32 WhistleSequence = 0;
        float SinceWhistle = -1.f;
        FPlayState AtSnap;
        bool bHaveStateAtSnap = false;
        int32 LongestYards = 0;
        bool bHaveYards = false;
        /** The play's yards as the simulation measured them (its PlayResult). */
        int32 ResultYards = 0;
        bool bHaveResult = false;
        int32 EventPoints = 0;
        bool bTurnoverEvent = false;
        int32 BrokenTackles = 0;
        bool bKickPlay = false;
        int32 KeyMomentSequence = 0;
        int32 KeyMomentRank = -1;
    };

    void HandleEventRecorded(const FPSTelemetryEvent& Event);

    UFUNCTION()
    void HandleReplayEnded();

    /** Marks Sequence as the play's key moment when Rank outranks the one it has. */
    void NoteKeyMoment(int32 Sequence, int32 Rank);

    /** Scores the play being followed, and cuts its clip when it makes the reel. */
    void SettlePlay();

    /** Plays the reel's next clip with a clip, or ends the reel after the last. */
    void StartNextClip();
    void FinishReel();

    UPSReplaySubsystem* GetReplay() const;

    UPROPERTY(Transient)
    FPSHighlightTuning Tuning;

    bool bTuningLoaded = false;

    FPlayTrack Track;
    int32 PlaysStarted = 0;

    /** The latest game state on the bus. */
    FPlayState LastState;
    bool bHaveLastState = false;

    UPROPERTY(Transient)
    TArray<FPSPlayImpact> PlayImpacts;

    /** The reel so far, most important first. */
    UPROPERTY(Transient)
    TArray<FPSPlayHighlight> Highlights;

    bool bGameOver = false;
    bool bReelShownForGame = false;
    float GameEndCountdown = -1.f;

    // The package playing.
    bool bPlayingReel = false;
    bool bSwitchingClip = false;
    int32 ReelIndex = INDEX_NONE;
    float ClipEndHeld = 0.f;

    UPROPERTY(Transient)
    TArray<FPSPlayHighlight> PlayingReel;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
