#include "PSHighlightSubsystem.h"
#include "PSDataIngestion.h"
#include "PSGameStateEvents.h"
#include "PSReplaySubsystem.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/Paths.h"

namespace PSHighlightSubsystemPrivate
{
    // How a moment ranks as a play's key moment: the slow-motion beat is built around the
    // highest.
    constexpr int32 TackleRank = 0;
    constexpr int32 BrokenTackleRank = 1;
    constexpr int32 CatchRank = 2;
    constexpr int32 TurnoverRank = 3;
    constexpr int32 ScoreRank = 4;

    template <typename EventStruct>
    bool ReadPayload(const FPSTelemetryEvent& Event, EventStruct& OutPayload)
    {
        return FJsonObjectConverter::JsonObjectStringToUStruct(Event.PayloadJson, &OutPayload, 0, 0);
    }

    bool IsKickPhase(const FString& Phase)
    {
        return Phase == TEXT("Kickoff") || Phase == TEXT("Punt") || Phase == TEXT("FieldGoal");
    }
}

void UPSHighlightSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>())
    {
        Bus->OnEventRecordedMC.AddUObject(this, &UPSHighlightSubsystem::HandleEventRecorded);
        BoundBus = Bus;
    }
}

void UPSHighlightSubsystem::Deinitialize()
{
    StopReel();
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnEventRecordedMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSHighlightSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSHighlightSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AdvanceTime(DeltaTime);
}

TStatId UPSHighlightSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSHighlightSubsystem, STATGROUP_Tickables);
}

// --- Tuning ------------------------------------------------------------------------------

FString UPSHighlightSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/highlights.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSHighlightTuning& UPSHighlightSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSHighlightSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSHighlightTuning Loaded;
    if (!Ingestion->LoadHighlightTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSHighlightSubsystem: Could not load highlight tuning from %s; keeping the current tuning."), *JsonFilePath);
        return false;
    }
    return SetTuning(Loaded);
}

bool UPSHighlightSubsystem::SetTuning(const FPSHighlightTuning& NewTuning)
{
    const TArray<FString> Problems = ValidateTuning(NewTuning);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSHighlightSubsystem: Highlight tuning refused: %s"), *Problem);
    }
    if (Problems.Num() > 0)
    {
        return false;
    }
    Tuning = NewTuning;
    bTuningLoaded = true;
    return true;
}

TArray<FString> UPSHighlightSubsystem::ValidateTuning(const FPSHighlightTuning& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.YardWeight < 0.f || InTuning.PointsWeight < 0.f || InTuning.TurnoverWeight < 0.f
        || InTuning.BrokenTackleWeight < 0.f || InTuning.WinProbabilityWeight < 0.f || InTuning.MinImportance < 0.f)
    {
        Problems.Add(TEXT("The weights and MinImportance must be 0 or more"));
    }
    if (InTuning.ReelSize < 1 || InTuning.SeasonHighlightsKept < 1)
    {
        Problems.Add(TEXT("ReelSize and SeasonHighlightsKept must be 1 or more"));
    }
    for (const EPSHighlightKind Kind : { EPSHighlightKind::Score, EPSHighlightKind::Turnover, EPSHighlightKind::BigPlay })
    {
        int32 Count = 0;
        for (const FPSHighlightShot& KindShot : InTuning.KindShots)
        {
            if (KindShot.Kind == Kind)
            {
                ++Count;
                if (KindShot.Shot == EPSDirectorShot::None)
                {
                    Problems.Add(FString::Printf(TEXT("KindShots: %s needs a Shot"), *StaticEnum<EPSHighlightKind>()->GetNameStringByValue(static_cast<int64>(Kind))));
                }
            }
        }
        if (Count != 1)
        {
            Problems.Add(FString::Printf(TEXT("KindShots: %s must have exactly one shot"), *StaticEnum<EPSHighlightKind>()->GetNameStringByValue(static_cast<int64>(Kind))));
        }
    }
    if (InTuning.BeatLeadSeconds < 0.f || InTuning.BeatSeconds < 0.f || InTuning.ClipGapSeconds < 0.f || InTuning.GameEndReelDelaySeconds < 0.f)
    {
        Problems.Add(TEXT("BeatLeadSeconds, BeatSeconds, ClipGapSeconds and GameEndReelDelaySeconds must be 0 or more"));
    }
    if (!(InTuning.BeatPlaybackRate > 0.f) || InTuning.BeatPlaybackRate > 1.f)
    {
        Problems.Add(TEXT("BeatPlaybackRate must be above 0 and at most 1"));
    }
    if (!(InTuning.SettleAfterWhistleSeconds > 0.f))
    {
        Problems.Add(TEXT("SettleAfterWhistleSeconds must be above 0"));
    }
    const FPSWinProbabilityTuning& Win = InTuning.WinProbability;
    if (!(Win.MarginScale > 0.f) || Win.PossessionPoints < 0.f || !(Win.TimeFloor > 0.f) || !(Win.GameSeconds > 0.f) || !(Win.QuarterSeconds > 0.f))
    {
        Problems.Add(TEXT("WinProbability: MarginScale, TimeFloor, GameSeconds and QuarterSeconds must be above 0, PossessionPoints 0 or more"));
    }
    return Problems;
}

// --- Importance --------------------------------------------------------------------------

float UPSHighlightSubsystem::HomeWinProbability(const FPlayState& State, const FPSWinProbabilityTuning& InTuning)
{
    // Past the fourth quarter the game is over.
    if (State.Quarter > 4)
    {
        return State.HomeScore > State.AwayScore ? 1.f : (State.HomeScore < State.AwayScore ? 0.f : 0.5f);
    }

    // What holding the ball is worth: nothing on your own goal line, PossessionPoints on theirs.
    const float BallValue = InTuning.PossessionPoints * static_cast<float>(FMath::Clamp(State.YardLine, 0, 100)) / 100.f;
    const float Margin = static_cast<float>(State.HomeScore - State.AwayScore) + (State.bHomeHasPossession ? BallValue : -BallValue);

    // The same margin matters more the less time there is to answer it.
    const float Clock = FMath::Max(0.f, State.GameClockSeconds);
    const float SecondsLeft = static_cast<float>(FMath::Max(0, 4 - State.Quarter)) * InTuning.QuarterSeconds + Clock;
    const float ShareLeft = FMath::Max(0.f, SecondsLeft / InTuning.GameSeconds) + InTuning.TimeFloor;
    const float Logit = InTuning.MarginScale * Margin / FMath::Sqrt(ShareLeft);
    return 1.f / (1.f + FMath::Exp(-Logit));
}

float UPSHighlightSubsystem::ScoreImportance(const FPSPlayImpact& Impact, const FPSHighlightTuning& InTuning)
{
    return InTuning.YardWeight * static_cast<float>(FMath::Abs(Impact.Yards))
        + InTuning.PointsWeight * static_cast<float>(Impact.Points)
        + (Impact.bTurnover ? InTuning.TurnoverWeight : 0.f)
        + InTuning.BrokenTackleWeight * static_cast<float>(Impact.BrokenTackles)
        + InTuning.WinProbabilityWeight * Impact.WinProbabilitySwing;
}

EPSHighlightKind UPSHighlightSubsystem::ClassifyPlay(const FPSPlayImpact& Impact)
{
    if (Impact.Points > 0)
    {
        return EPSHighlightKind::Score;
    }
    return Impact.bTurnover ? EPSHighlightKind::Turnover : EPSHighlightKind::BigPlay;
}

// --- Following the plays -----------------------------------------------------------------

void UPSHighlightSubsystem::HandleEventRecorded(const FPSTelemetryEvent& Event)
{
    using namespace PSHighlightSubsystemPrivate;

    switch (Event.EventType)
    {
    case EPSTelemetryEventType::Snap:
        // A play not yet settled settles now, on what is known.
        if (Track.bActive)
        {
            SettlePlay();
        }
        Track = FPlayTrack();
        Track.bActive = true;
        Track.PlayNumber = ++PlaysStarted;
        Track.SnapSequence = Event.Sequence;
        Track.AtSnap = LastState;
        Track.bHaveStateAtSnap = bHaveLastState;
        Track.bKickPlay = bHaveLastState && LastState.bKickoff;
        break;
    case EPSTelemetryEventType::Catch:
    {
        FPSTelemetryCatchEvent Catch;
        if (Track.bActive && ReadPayload(Event, Catch))
        {
            if (!Track.bHaveYards || FMath::Abs(Catch.YardsGained) > FMath::Abs(Track.LongestYards))
            {
                Track.LongestYards = Catch.YardsGained;
                Track.bHaveYards = true;
            }
            Track.bTurnoverEvent |= Catch.bIsInterception;
            NoteKeyMoment(Event.Sequence, Catch.bIsInterception ? TurnoverRank : CatchRank);
        }
        break;
    }
    case EPSTelemetryEventType::Tackle:
    {
        FPSTelemetryTackleEvent Tackle;
        if (Track.bActive && ReadPayload(Event, Tackle))
        {
            if (!Track.bHaveYards || FMath::Abs(Tackle.YardsGained) > FMath::Abs(Track.LongestYards))
            {
                Track.LongestYards = Tackle.YardsGained;
                Track.bHaveYards = true;
            }
            NoteKeyMoment(Event.Sequence, TackleRank);
        }
        break;
    }
    case EPSTelemetryEventType::Fumble:
    {
        FPSTelemetryFumbleEvent Fumble;
        if (Track.bActive && ReadPayload(Event, Fumble) && Fumble.bIsTurnover)
        {
            Track.bTurnoverEvent = true;
            NoteKeyMoment(Event.Sequence, TurnoverRank);
        }
        break;
    }
    case EPSTelemetryEventType::Damage:
    {
        // A hit the carrier survives is a tackle he broke (Epic 139's hitpoints).
        FPSTelemetryDamageEvent Damage;
        if (Track.bActive && ReadPayload(Event, Damage) && Damage.RemainingHitPoints > 0.f)
        {
            ++Track.BrokenTackles;
            NoteKeyMoment(Event.Sequence, BrokenTackleRank);
        }
        break;
    }
    case EPSTelemetryEventType::Score:
    {
        FPSTelemetryScoreEvent Score;
        if (Track.bActive && ReadPayload(Event, Score))
        {
            Track.EventPoints += Score.Points;
            NoteKeyMoment(Event.Sequence, ScoreRank);
        }
        break;
    }
    case EPSTelemetryEventType::PhaseChange:
    {
        FPSTelemetryPhaseChangeEvent Phase;
        if (Track.bActive && Track.WhistleSequence == 0 && ReadPayload(Event, Phase) && Phase.NewPhase == TEXT("Scoring"))
        {
            Track.WhistleSequence = Event.Sequence;
            Track.SinceWhistle = 0.f;
        }
        break;
    }
    case EPSTelemetryEventType::GameState:
    {
        // The play simulation's game state is the authority on the score and the ball.
        FPSTelemetryGameStateEvent GameState;
        if (!ReadPayload(Event, GameState))
        {
            break;
        }
        LastState = PSGameStateEvents::ToPlayState(GameState);
        bHaveLastState = true;
        if (Track.bActive)
        {
            Track.bKickPlay |= IsKickPhase(GameState.Phase);
            if (GameState.Phase == TEXT("Scoring"))
            {
                if (Track.WhistleSequence == 0)
                {
                    Track.WhistleSequence = Event.Sequence;
                    Track.SinceWhistle = 0.f;
                }
            }
            else if (Track.WhistleSequence != 0)
            {
                // The first state after the whistle that isn't the whistle's: the play is settled.
                SettlePlay();
            }
        }
        if (LastState.Quarter > 4 && !bGameOver)
        {
            bGameOver = true;
            if (Track.bActive)
            {
                SettlePlay();
            }
            if (GetTuning().bPlayReelAtGameEnd && !bReelShownForGame)
            {
                GameEndCountdown = GetTuning().GameEndReelDelaySeconds;
            }
        }
        break;
    }
    default:
        break;
    }
}

void UPSHighlightSubsystem::NoteKeyMoment(int32 Sequence, int32 Rank)
{
    if (Rank > Track.KeyMomentRank)
    {
        Track.KeyMomentRank = Rank;
        Track.KeyMomentSequence = Sequence;
    }
}

void UPSHighlightSubsystem::SettlePlay()
{
    if (!Track.bActive)
    {
        return;
    }
    Track.bActive = false;
    const FPSHighlightTuning& Settle = GetTuning();

    // What the play did, from what the bus said during it and the game state either side.
    FPSPlayImpact Impact;
    const bool bStates = Track.bHaveStateAtSnap && bHaveLastState;
    const FPlayState& Before = Track.AtSnap;
    const FPlayState& After = LastState;
    const int32 StatePoints = bStates ? FMath::Max(0, (After.HomeScore + After.AwayScore) - (Before.HomeScore + Before.AwayScore)) : 0;
    Impact.Points = FMath::Max(Track.EventPoints, StatePoints);
    const bool bChangedHands = bStates && After.bHomeHasPossession != Before.bHomeHasPossession;
    Impact.bTurnover = Track.bTurnoverEvent || (bChangedHands && !Track.bKickPlay && Impact.Points == 0);
    if (Track.bHaveYards)
    {
        Impact.Yards = Track.LongestYards;
    }
    else if (bStates && !bChangedHands && Impact.Points == 0)
    {
        Impact.Yards = After.YardLine - Before.YardLine;
    }
    Impact.BrokenTackles = Track.BrokenTackles;
    if (bStates)
    {
        Impact.WinProbabilitySwing = FMath::Abs(HomeWinProbability(After, Settle.WinProbability) - HomeWinProbability(Before, Settle.WinProbability));
    }
    PlayImpacts.Add(Impact);

    // Only a play worth it, and better than the reel's least, takes a clip.
    const float Importance = ScoreImportance(Impact, Settle);
    if (Importance < Settle.MinImportance)
    {
        return;
    }
    if (Highlights.Num() >= Settle.ReelSize && Highlights.Num() > 0 && Importance <= Highlights.Last().Importance)
    {
        return;
    }

    FPSPlayHighlight Highlight;
    Highlight.PlayNumber = Track.PlayNumber;
    Highlight.Kind = ClassifyPlay(Impact);
    Highlight.Importance = Importance;
    Highlight.Impact = Impact;
    Highlight.Situation = Before;
    const EPSHighlightKind Kind = Highlight.Kind;
    if (const FPSHighlightShot* KindShot = Settle.KindShots.FindByPredicate([Kind](const FPSHighlightShot& Candidate) { return Candidate.Kind == Kind; }))
    {
        Highlight.Shot = KindShot->Shot;
    }

    const UPSTelemetryBus* Bus = BoundBus.Get();
    const int32 ToSequence = Track.WhistleSequence != 0 ? Track.WhistleSequence : (Bus ? Bus->GetLastEventSequence() : Track.SnapSequence);
    UPSReplaySubsystem* Replay = GetReplay();
    if (Replay && Replay->CaptureClip(Track.SnapSequence, ToSequence, Highlight.Clip) && Highlight.Clip.Frames.Num() > 0)
    {
        // The beat: from BeatLeadSeconds before the key moment, for BeatSeconds.
        const UWorld* World = GetWorld();
        const UPSTelemetrySamplingSubsystem* Sampler = World ? World->GetSubsystem<UPSTelemetrySamplingSubsystem>() : nullptr;
        const float ClipStart = Highlight.Clip.Frames[0].Time;
        const float ClipLength = Highlight.Clip.Frames.Last().Time - ClipStart;
        float KeyTime = ClipStart;
        if (Sampler && !Sampler->GetEventTime(Track.KeyMomentSequence != 0 ? Track.KeyMomentSequence : ToSequence, KeyTime))
        {
            KeyTime = ClipStart;
        }
        Highlight.BeatStartSeconds = FMath::Clamp(KeyTime - Settle.BeatLeadSeconds - ClipStart, 0.f, ClipLength);
        Highlight.BeatEndSeconds = FMath::Clamp(Highlight.BeatStartSeconds + Settle.BeatSeconds, 0.f, ClipLength);
    }

    Highlights.Add(Highlight);
    Highlights.StableSort([](const FPSPlayHighlight& A, const FPSPlayHighlight& B) { return A.Importance > B.Importance; });
    while (Highlights.Num() > Settle.ReelSize)
    {
        Highlights.Pop();
    }
}

void UPSHighlightSubsystem::AdvanceTime(float DeltaSeconds)
{
    const float Step = FMath::Max(0.f, DeltaSeconds);
    const UWorld* World = GetWorld();
    const bool bGamePaused = World && World->IsPaused();
    UPSReplaySubsystem* Replay = GetReplay();

    // A play settles on time when no game state says so, counting only time the game runs.
    if (Track.bActive && Track.SinceWhistle >= 0.f && !bGamePaused)
    {
        Track.SinceWhistle += Step;
        if (Track.SinceWhistle >= GetTuning().SettleAfterWhistleSeconds)
        {
            SettlePlay();
        }
    }

    // The game's over: the reel plays by itself, once whatever replay is showing ends.
    if (GameEndCountdown >= 0.f && !bPlayingReel && !(Replay && Replay->IsReplaying()))
    {
        GameEndCountdown -= Step;
        if (GameEndCountdown <= 0.f)
        {
            GameEndCountdown = -1.f;
            bReelShownForGame = true;
            PlayReel();
        }
    }

    if (!bPlayingReel || !Replay || !Replay->IsReplaying() || !PlayingReel.IsValidIndex(ReelIndex))
    {
        return;
    }

    // Real time, then the beat in slow motion, then real time; the next clip after a moment.
    const FPSPlayHighlight& Current = PlayingReel[ReelIndex];
    const float Playhead = Replay->GetPlayhead();
    const bool bInBeat = Playhead >= Current.BeatStartSeconds && Playhead < Current.BeatEndSeconds;
    const float Rate = bInBeat ? GetTuning().BeatPlaybackRate : 1.f;
    if (!FMath::IsNearlyEqual(Replay->GetPlaybackRate(), Rate))
    {
        Replay->SetPlaybackRate(Rate);
    }
    if (Replay->IsAtEnd())
    {
        ClipEndHeld += Step;
        if (ClipEndHeld >= GetTuning().ClipGapSeconds)
        {
            StartNextClip();
        }
    }
    else
    {
        ClipEndHeld = 0.f;
    }
}

TArray<FPSPlayHighlight> UPSHighlightSubsystem::GetReel() const
{
    TArray<FPSPlayHighlight> Reel = Highlights;
    Reel.StableSort([](const FPSPlayHighlight& A, const FPSPlayHighlight& B) { return A.PlayNumber < B.PlayNumber; });
    return Reel;
}

void UPSHighlightSubsystem::ResetGame()
{
    StopReel();
    Track = FPlayTrack();
    PlaysStarted = 0;
    PlayImpacts.Reset();
    Highlights.Reset();
    bGameOver = false;
    bReelShownForGame = false;
    GameEndCountdown = -1.f;
}

// --- The package -------------------------------------------------------------------------

UPSReplaySubsystem* UPSHighlightSubsystem::GetReplay() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSReplaySubsystem>() : nullptr;
}

bool UPSHighlightSubsystem::PlayReel()
{
    StopReel();
    UPSReplaySubsystem* Replay = GetReplay();
    if (!Replay)
    {
        return false;
    }
    PlayingReel = GetReel().FilterByPredicate([](const FPSPlayHighlight& Highlight) { return Highlight.Clip.Frames.Num() > 0; });
    if (PlayingReel.Num() == 0)
    {
        return false;
    }
    bPlayingReel = true;
    ReelIndex = INDEX_NONE;
    Replay->OnReplayEnded.AddUniqueDynamic(this, &UPSHighlightSubsystem::HandleReplayEnded);
    StartNextClip();
    return bPlayingReel;
}

void UPSHighlightSubsystem::StartNextClip()
{
    UPSReplaySubsystem* Replay = GetReplay();
    ClipEndHeld = 0.f;
    while (Replay && ++ReelIndex < PlayingReel.Num())
    {
        const FPSPlayHighlight& Next = PlayingReel[ReelIndex];
        // Switching clips ends the last one's replay: that isn't the viewer leaving.
        bSwitchingClip = true;
        const bool bStarted = Replay->StartReplay(Next.Clip);
        bSwitchingClip = false;
        if (bStarted)
        {
            Replay->CutToDirectorShot(Next.Shot);
            Replay->SetPlaybackRate(1.f);
            return;
        }
    }
    FinishReel();
}

void UPSHighlightSubsystem::StopReel()
{
    if (bPlayingReel)
    {
        FinishReel();
    }
}

void UPSHighlightSubsystem::FinishReel()
{
    const bool bWasPlaying = bPlayingReel;
    bPlayingReel = false;
    ReelIndex = INDEX_NONE;
    PlayingReel.Reset();
    if (UPSReplaySubsystem* Replay = GetReplay())
    {
        Replay->OnReplayEnded.RemoveDynamic(this, &UPSHighlightSubsystem::HandleReplayEnded);
        bSwitchingClip = true;
        Replay->StopReplay();
        bSwitchingClip = false;
    }
    if (bWasPlaying)
    {
        OnReelFinished.Broadcast();
    }
}

void UPSHighlightSubsystem::HandleReplayEnded()
{
    // The viewer left the replay: the reel ends with it.
    if (bPlayingReel && !bSwitchingClip)
    {
        FinishReel();
    }
}

// --- The franchise -----------------------------------------------------------------------

FString UPSHighlightSubsystem::GetDefaultArchiveDirectory()
{
    return UPSReplaySubsystem::GetDefaultSaveDirectory() / TEXT("Highlights");
}

int32 UPSHighlightSubsystem::ArchiveForSeason(TArray<FPSSeasonHighlight>& InOutSeason, int32 Week, FName HomeTeamId, FName AwayTeamId, const FString& Directory)
{
    const FString ArchiveDirectory = Directory.IsEmpty() ? GetDefaultArchiveDirectory() : Directory;
    UPSReplaySubsystem* Replay = GetReplay();
    for (const FPSPlayHighlight& Highlight : GetReel())
    {
        FPSSeasonHighlight& Entry = InOutSeason.AddDefaulted_GetRef();
        Entry.Week = Week;
        Entry.HomeTeamId = HomeTeamId;
        Entry.AwayTeamId = AwayTeamId;
        Entry.PlayNumber = Highlight.PlayNumber;
        Entry.Kind = Highlight.Kind;
        Entry.Importance = Highlight.Importance;
        Entry.Quarter = Highlight.Situation.Quarter;
        Entry.GameClockSeconds = Highlight.Situation.GameClockSeconds;
        Entry.Yards = Highlight.Impact.Yards;
        Entry.Points = Highlight.Impact.Points;
        if (Replay && Highlight.Clip.Frames.Num() > 0)
        {
            const FString Name = FString::Printf(TEXT("Week%02d_%s_at_%s_Play%03d"), Week, *AwayTeamId.ToString(), *HomeTeamId.ToString(), Highlight.PlayNumber);
            Replay->SaveClip(Highlight.Clip, Name, ArchiveDirectory, Entry.ClipFile);
        }
    }

    TArray<FPSSeasonHighlight> Dropped;
    KeepSeasonBest(InOutSeason, GetTuning().SeasonHighlightsKept, Dropped);
    for (const FPSSeasonHighlight& Gone : Dropped)
    {
        if (!Gone.ClipFile.IsEmpty())
        {
            IFileManager::Get().Delete(*Gone.ClipFile);
        }
    }
    return InOutSeason.FilterByPredicate([Week, HomeTeamId, AwayTeamId](const FPSSeasonHighlight& Entry)
    {
        return Entry.Week == Week && Entry.HomeTeamId == HomeTeamId && Entry.AwayTeamId == AwayTeamId;
    }).Num();
}

void UPSHighlightSubsystem::KeepSeasonBest(TArray<FPSSeasonHighlight>& InOutSeason, int32 MaxKept, TArray<FPSSeasonHighlight>& OutDropped)
{
    OutDropped.Reset();
    if (InOutSeason.Num() <= MaxKept)
    {
        return;
    }
    TArray<int32> ByImportance;
    for (int32 Index = 0; Index < InOutSeason.Num(); ++Index)
    {
        ByImportance.Add(Index);
    }
    ByImportance.StableSort([&InOutSeason](int32 A, int32 B) { return InOutSeason[A].Importance > InOutSeason[B].Importance; });
    TSet<int32> Kept;
    for (int32 Rank = 0; Rank < FMath::Max(0, MaxKept); ++Rank)
    {
        Kept.Add(ByImportance[Rank]);
    }

    TArray<FPSSeasonHighlight> Remaining;
    for (int32 Index = 0; Index < InOutSeason.Num(); ++Index)
    {
        (Kept.Contains(Index) ? Remaining : OutDropped).Add(InOutSeason[Index]);
    }
    InOutSeason = MoveTemp(Remaining);
}
