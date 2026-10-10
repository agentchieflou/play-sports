// PSCommentaryEngine.h - Epic 96: the commentary booth, two voices calling the game from the bus
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSCommentaryTypes.h"
#include "PSNarrativeTypes.h"
#include "PSPlatformTiers.h"
#include "PSTelemetryBus.h"
#include "PSCommentaryEngine.generated.h"

class UPSCommentaryEventModel;
class UPSFranchiseSaveGame;
class UPSLeagueNarrative;

/**
 * UPSCommentaryEngine is the booth (Epic 96). It hears the commentary hooks' Commentary events on
 * the telemetry bus (UPSCommentaryEventModel: what happened, who, the stakes and the novelty) and
 * speaks through the bus's Speech event, which Epic 103.3 captions (Channel Commentary; the line's
 * LineId lets the audio play its recorded voice-over). It never polls the game.
 *
 *  - Lines (96.4, template library first): Data/commentary_lines.json lists each line's voice,
 *    moment, conditions (the moment's Detail, down, yards, stakes, a first down, a turnover, the
 *    players and statistics its text names), priority, cooldown and caps. Its text is the string
 *    table's Commentary.Line.<LineId> row (UPSLocalization), filled with the moment's facts.
 *  - Selection (96.2): the lines that fit a moment score their priority plus the moment's stakes
 *    and novelty by their weights, less a penalty for each use this game; one is picked among
 *    those within VarietyBand of the best (seeded per game). A line said before its cooldown, or
 *    past its cap, isn't a candidate.
 *  - Pacing and interruption (96.2): each voice says one line at a time, for its words' length.
 *    A line at least InterruptMargin above the one being said cuts it off (a big play cuts off
 *    the filler); otherwise it waits in the voice's queue (QueueLength), and a play-by-play line
 *    still waiting after MaxDelaySeconds is dropped: the moment has passed.
 *  - Two voices (96.3): the play-by-play voice calls the action as it happens. The analyst (Color)
 *    speaks in the window between plays, from ColorWindowDelaySeconds after the play is over (its
 *    result, a timeout, a quarter's end) until the snap, and never over the play-by-play: the
 *    play-by-play cuts him off, and the snap closes his window and his queue.
 *  - Outside models (96.4, bridge-gated via Epic 82): a line an outside model wrote for the play
 *    (the hooks' OnModelLineMC, only while the bridge is online) becomes the analyst's line in
 *    place of his template one, as long as the play it describes is still the current one.
 *  - Storylines (96.5): the league's talking points (Epic 93, UPSLeagueNarrative::GetTalkingPoints
 *    through FeedFromNarrative, or SetTalkingPoints) fill the analyst's quiet windows, each once,
 *    at most MaxTalkingPointsPerGame, TalkingPointGapSeconds apart.
 *  - Repetition (96.6): every line's uses this game and this season are counted; MaxPerGame and
 *    MaxPerSeason cap them, the season's counts persist in the franchise save, and
 *    BuildRepetitionReport measures the game's reuse (logged at the final whistle).
 *  - Its update (ending lines, starting the next) runs the platform tier's AudioUpdateHz times a
 *    second (Data/platform_tiers.json), timed as Audio.
 *
 * In a match it binds to the world's bus and the hooks when the world begins play; headless tests
 * call BindToBus, SetEventModel and AdvanceTime themselves.
 */
UCLASS()
class PLAYSPORTS_API UPSCommentaryEngine : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    // --- The library -----------------------------------------------------------------------

    static FString GetDefaultLibraryPath();

    /** The library, loaded from the default path on first use. */
    const FPSCommentaryLibrary& GetLibrary();

    /** Replaces the library with JsonFilePath's (through UPSDataIngestion); a file that can't be
     *  read or fails ValidateLibrary is refused and the current library kept. */
    bool LoadLibraryFromJson(const FString& JsonFilePath);

    /** Applies InLibrary when it passes ValidateLibrary. */
    bool SetLibrary(const FPSCommentaryLibrary& InLibrary);

    /** Problems with InLibrary, one line each (empty when sound): pacing and weights out of range,
     *  line ids empty or repeated, priorities outside 0-100, negative cooldowns or caps,
     *  conditions that can't hold, a line without its string-table text. */
    static TArray<FString> ValidateLibrary(const FPSCommentaryLibrary& InLibrary);

    /** The string table's key for a line's text: Commentary.Line.<LineId>. */
    static FString LineTextKey(FName LineId);

    // --- Sources ---------------------------------------------------------------------------

    /** Hears Bus's Commentary events and speaks on it. OnWorldBeginPlay binds the world's. */
    void BindToBus(UPSTelemetryBus* Bus);

    void UnbindFromBus();

    /** The commentary hooks whose outside-model lines the analyst voices. OnWorldBeginPlay takes
     *  the world's. */
    void SetEventModel(UPSCommentaryEventModel* InModel);

    /** Takes the tier's AudioUpdateHz. BindToBus applies the active tier. */
    void ApplyPlatformTier(const FPSPlatformTier& Tier);

    // --- Selection (96.2) ------------------------------------------------------------------

    /** Whether Line's conditions hold for Moment (its moment, detail, down, yards, stakes,
     *  requirements and the facts its text names). */
    static bool LineFits(const FPSCommentaryLineDef& Line, const FPSTelemetryCommentaryEvent& Moment);

    /** Line's text for Moment, localized: its string-table row with the moment's facts. */
    static FString FormatLine(const FPSCommentaryLineDef& Line, const FPSTelemetryCommentaryEvent& Moment);

    /** The line Voice would say for Moment now (cooldowns, caps, scores and the seeded variety),
     *  or null. Ruling a capped line out counts in the repetition report. */
    const FPSCommentaryLineDef* SelectLine(const FPSTelemetryCommentaryEvent& Moment, EPSCommentaryVoice Voice);

    /** The booth hears Moment: each voice picks its line, the play-by-play's starts or waits, the
     *  analyst's waits for his window. The bus's Commentary events call it. */
    void HandleMoment(const FPSTelemetryCommentaryEvent& Moment);

    /** One step: the booth's clock moves on by DeltaSeconds, and every 1 / AudioUpdateHz the lines
     *  that are over end and the next ones start. The tick calls it. */
    void AdvanceTime(float DeltaSeconds);

    // --- Storylines (96.5) -----------------------------------------------------------------

    /** The talking points for this game, the heaviest first. */
    UFUNCTION(BlueprintCallable, Category = "Commentary")
    void SetTalkingPoints(const TArray<FPSNewsItem>& InPoints);

    /** Takes the game's talking points from the league's narrative: its storylines about either
     *  team (UPSLeagueNarrative::GetTalkingPoints). Returns how many. */
    UFUNCTION(BlueprintCallable, Category = "Commentary")
    int32 FeedFromNarrative(UPSLeagueNarrative* Narrative, FName HomeTeamId, FName AwayTeamId);

    UFUNCTION(BlueprintPure, Category = "Commentary")
    int32 GetTalkingPointsSpoken() const { return TalkingPointsSpoken; }

    // --- Repetition (96.6) -----------------------------------------------------------------

    /** A new season: its counts start again. */
    UFUNCTION(BlueprintCallable, Category = "Commentary")
    void BeginSeason(int32 InSeason);

    FPSCommentarySeasonUsage GetSeasonUsage() const;

    void SetSeasonUsage(const FPSCommentarySeasonUsage& Usage);

    /** Writes the season's line use into the franchise save. */
    void SaveTo(UPSFranchiseSaveGame* Save) const;

    /** Reads it back; false (keeping the current counts) when the save has none. */
    bool LoadFrom(const UPSFranchiseSaveGame* Save);

    UFUNCTION(BlueprintPure, Category = "Commentary")
    int32 GetGameUses(FName LineId) const;

    UFUNCTION(BlueprintPure, Category = "Commentary")
    int32 GetSeasonUses(FName LineId) const;

    /** How repetitive the booth has been this game. */
    UFUNCTION(BlueprintCallable, Category = "Commentary")
    FPSCommentaryRepetitionReport BuildRepetitionReport() const;

    // --- What is being said ----------------------------------------------------------------

    /** Every line said this game, oldest first (the latest MaxSpokenKept). */
    const TArray<FPSCommentaryUtterance>& GetSpoken() const { return Spoken; }

    UFUNCTION(BlueprintPure, Category = "Commentary")
    bool IsSpeaking(EPSCommentaryVoice Voice) const;

    /** The line Voice is saying. */
    bool GetCurrentLine(EPSCommentaryVoice Voice, FPSCommentaryUtterance& OutLine) const;

    UFUNCTION(BlueprintPure, Category = "Commentary")
    int32 GetQueueLength(EPSCommentaryVoice Voice) const;

    /** True between plays, once ColorWindowDelaySeconds have passed. */
    UFUNCTION(BlueprintPure, Category = "Commentary")
    bool IsColorWindowOpen() const;

private:
    /** One voice: the line it is saying and the ones waiting. */
    struct FVoiceState
    {
        bool bSpeaking = false;
        float EndsAt = 0.f;
        FPSCommentaryUtterance Current;
        TArray<FPSCommentaryUtterance> Queue;
    };

    void HandleCommentary(const FPSTelemetryCommentaryEvent& Moment);
    void HandleModelLine(const FPSTelemetryCommentaryEvent& Moment, const FString& Text);

    /** Forgets the game: its voices, its uses, its talking points; reseeds the variety. */
    void BeginGame();

    FPSCommentaryUtterance MakeUtterance(const FPSCommentaryLineDef& Line, const FPSTelemetryCommentaryEvent& Moment) const;

    /** A line's speaking time from its words. */
    float SpeakingSeconds(const FString& Text);

    /** The play-by-play starts or queues Line; the analyst queues it for his window. */
    void Submit(const FPSCommentaryUtterance& Line);

    /** Adds Line to Voice's queue, keeping the QueueLength most important. */
    void Enqueue(FVoiceState& Voice, const FPSCommentaryUtterance& Line);

    /** Starts whatever may start now: the play-by-play's next, then the analyst's in his window
     *  (or a talking point when he has nothing else). */
    void Pump();

    /** Says Line now: on the bus as Speech, counted, logged. */
    void Start(const FPSCommentaryUtterance& Line);

    /** Cuts Voice off. */
    void Interrupt(EPSCommentaryVoice Voice);

    /** The snap: the analyst stops, his queue is cleared, his window closes. */
    void CloseColorWindow();

    FVoiceState& StateOf(EPSCommentaryVoice Voice) { return Voices[static_cast<int32>(Voice)]; }
    const FVoiceState& StateOf(EPSCommentaryVoice Voice) const { return Voices[static_cast<int32>(Voice)]; }

    UPROPERTY(Transient)
    FPSCommentaryLibrary Library;

    bool bLibraryLoaded = false;

    FVoiceState Voices[2];

    float Clock = 0.f;
    float SinceUpdate = 0.f;
    float UpdateIntervalSeconds = 0.f;

    bool bBetweenPlays = false;
    float WindowOpensAt = 0.f;
    int32 CurrentPlay = 0;

    UPROPERTY(Transient)
    TArray<FPSNewsItem> TalkingPoints;

    int32 TalkingPointsSpoken = 0;
    float LastTalkingPointAt = -1.f;

    UPROPERTY(Transient)
    TArray<FPSCommentaryUtterance> Spoken;

    TMap<FName, int32> GameUses;
    TMap<FName, int32> SeasonUses;
    TMap<FName, float> LastSaidAt;
    int32 Season = 0;

    int32 InterruptedCount = 0;
    int32 DroppedStaleCount = 0;
    int32 SuppressedByCapCount = 0;
    TSet<FName> CappedLines;

    FRandomStream Variety;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    TWeakObjectPtr<UPSCommentaryEventModel> EventModel;
    FDelegateHandle ModelLineHandle;
};
