#include "PSCommentaryEngine.h"
#include "PSCommentaryEventModel.h"
#include "PSDataIngestion.h"
#include "PSFranchiseSaveGame.h"
#include "PSGameStateEvents.h"
#include "PSLeagueNarrative.h"
#include "PSLocalization.h"
#include "PSPerfBudget.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSCommentaryEnginePrivate
{
    /** The caption channel the booth speaks on (Epic 103.3). */
    const FName CommentaryChannel(TEXT("Commentary"));

    /** The moments after which the ball is dead: the analyst's window opens. */
    bool OpensColorWindow(EPSCommentaryMoment Moment)
    {
        return Moment == EPSCommentaryMoment::PlayResult || Moment == EPSCommentaryMoment::GameStart || Moment == EPSCommentaryMoment::Timeout
            || Moment == EPSCommentaryMoment::TwoMinuteWarning || Moment == EPSCommentaryMoment::QuarterEnd || Moment == EPSCommentaryMoment::GameEnd;
    }

    /** The voice's name as the caption shows it. */
    FString VoiceName(EPSCommentaryVoice Voice)
    {
        return Voice == EPSCommentaryVoice::Color
            ? UPSLocalization::GetText(TEXT("Commentary.Voice.Color")).ToString()
            : UPSLocalization::GetText(TEXT("Commentary.Voice.PlayByPlay")).ToString();
    }

    int32 Increment(TMap<FName, int32>& Counts, FName Key)
    {
        int32& Count = Counts.FindOrAdd(Key);
        return ++Count;
    }

    int32 CountOf(const TMap<FName, int32>& Counts, FName Key)
    {
        const int32* Count = Counts.Find(Key);
        return Count ? *Count : 0;
    }
}

void UPSCommentaryEngine::Deinitialize()
{
    UnbindFromBus();
    SetEventModel(nullptr);
    Super::Deinitialize();
}

bool UPSCommentaryEngine::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSCommentaryEngine::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    BindToBus(InWorld.GetSubsystem<UPSTelemetryBus>());
    SetEventModel(InWorld.GetSubsystem<UPSCommentaryEventModel>());
}

void UPSCommentaryEngine::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AdvanceTime(DeltaTime);
}

TStatId UPSCommentaryEngine::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSCommentaryEngine, STATGROUP_Tickables);
}

// --- The library ----------------------------------------------------------------------------

FString UPSCommentaryEngine::GetDefaultLibraryPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/commentary_lines.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSCommentaryLibrary& UPSCommentaryEngine::GetLibrary()
{
    if (!bLibraryLoaded)
    {
        bLibraryLoaded = true;
        LoadLibraryFromJson(GetDefaultLibraryPath());
    }
    return Library;
}

bool UPSCommentaryEngine::LoadLibraryFromJson(const FString& JsonFilePath)
{
    FPSCommentaryLibrary Loaded;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (!Ingestion->LoadCommentaryLibraryFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCommentaryEngine: could not read %s"), *JsonFilePath);
        return false;
    }
    return SetLibrary(Loaded);
}

bool UPSCommentaryEngine::SetLibrary(const FPSCommentaryLibrary& InLibrary)
{
    const TArray<FString> Problems = ValidateLibrary(InLibrary);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSCommentaryEngine: %s"), *Problem);
        }
        return false;
    }
    Library = InLibrary;
    bLibraryLoaded = true;
    return true;
}

FString UPSCommentaryEngine::LineTextKey(FName LineId)
{
    return FString::Printf(TEXT("Commentary.Line.%s"), *LineId.ToString());
}

TArray<FString> UPSCommentaryEngine::ValidateLibrary(const FPSCommentaryLibrary& InLibrary)
{
    TArray<FString> Problems;
    if (!(InLibrary.WordsPerSecond > 0.f) || !(InLibrary.MinLineSeconds > 0.f) || InLibrary.MaxLineSeconds < InLibrary.MinLineSeconds)
    {
        Problems.Add(TEXT("WordsPerSecond and MinLineSeconds must be above 0, MaxLineSeconds at least MinLineSeconds"));
    }
    if (!(InLibrary.MaxDelaySeconds > 0.f) || !(InLibrary.ColorMaxDelaySeconds > 0.f) || InLibrary.ColorWindowDelaySeconds < 0.f
        || InLibrary.TalkingPointGapSeconds < 0.f)
    {
        Problems.Add(TEXT("MaxDelaySeconds and ColorMaxDelaySeconds must be above 0, ColorWindowDelaySeconds and TalkingPointGapSeconds 0 or more"));
    }
    if (InLibrary.InterruptMargin < 0 || InLibrary.InterruptMargin > 100 || InLibrary.QueueLength < 1 || InLibrary.MaxSpokenKept < 1
        || InLibrary.MaxTalkingPointsPerGame < 0)
    {
        Problems.Add(TEXT("InterruptMargin must be 0-100, QueueLength and MaxSpokenKept 1 or more, MaxTalkingPointsPerGame 0 or more"));
    }
    if (InLibrary.TalkingPointPriority < 0 || InLibrary.TalkingPointPriority > 100 || InLibrary.ModelLinePriority < 0 || InLibrary.ModelLinePriority > 100)
    {
        Problems.Add(TEXT("TalkingPointPriority and ModelLinePriority must be 0-100"));
    }
    if (InLibrary.StakesWeight < 0.f || InLibrary.NoveltyWeight < 0.f || InLibrary.RepeatPenalty < 0.f || InLibrary.VarietyBand < 0.f)
    {
        Problems.Add(TEXT("StakesWeight, NoveltyWeight, RepeatPenalty and VarietyBand must be 0 or more"));
    }
    TSet<FName> Seen;
    for (int32 Index = 0; Index < InLibrary.Lines.Num(); ++Index)
    {
        const FPSCommentaryLineDef& Line = InLibrary.Lines[Index];
        const FString Id = Line.LineId.ToString();
        if (Line.LineId.IsNone() || Seen.Contains(Line.LineId))
        {
            Problems.Add(FString::Printf(TEXT("Lines[%d]: LineId is empty or used twice"), Index));
            continue;
        }
        Seen.Add(Line.LineId);
        if (Line.Priority < 0 || Line.Priority > 100 || Line.CooldownSeconds < 0.f || Line.MaxPerGame < 0 || Line.MaxPerSeason < 0)
        {
            Problems.Add(FString::Printf(TEXT("Lines[%d] %s: Priority must be 0-100, CooldownSeconds and the caps 0 or more"), Index, *Id));
        }
        if (Line.MinDown < 0 || Line.MaxDown > 4 || Line.MinDown > Line.MaxDown || Line.MinYards > Line.MaxYards
            || Line.MinStakes < 0.f || Line.MinStakes > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("Lines[%d] %s: its conditions can't hold (downs 0-4, MinYards at most MaxYards, MinStakes 0-1)"), Index, *Id));
        }
        if (!UPSLocalization::HasText(LineTextKey(Line.LineId)))
        {
            Problems.Add(FString::Printf(TEXT("Lines[%d] %s: no '%s' row in Data/ui_text.csv"), Index, *Id, *LineTextKey(Line.LineId)));
        }
    }
    return Problems;
}

// --- Sources --------------------------------------------------------------------------------

void UPSCommentaryEngine::BindToBus(UPSTelemetryBus* Bus)
{
    UnbindFromBus();
    if (!Bus)
    {
        return;
    }
    ApplyPlatformTier(PSPlatformTiers::GetActiveTier());
    Bus->OnCommentaryMC.AddUObject(this, &UPSCommentaryEngine::HandleCommentary);
    BoundBus = Bus;
    BeginGame();
}

void UPSCommentaryEngine::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnCommentaryMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSCommentaryEngine::SetEventModel(UPSCommentaryEventModel* InModel)
{
    if (UPSCommentaryEventModel* Old = EventModel.Get())
    {
        Old->OnModelLineMC.Remove(ModelLineHandle);
    }
    ModelLineHandle.Reset();
    EventModel = InModel;
    if (InModel)
    {
        ModelLineHandle = InModel->OnModelLineMC.AddUObject(this, &UPSCommentaryEngine::HandleModelLine);
    }
}

void UPSCommentaryEngine::ApplyPlatformTier(const FPSPlatformTier& Tier)
{
    UpdateIntervalSeconds = Tier.AudioUpdateHz > 0.f ? 1.f / Tier.AudioUpdateHz : 0.f;
}

// --- Selection ------------------------------------------------------------------------------

bool UPSCommentaryEngine::LineFits(const FPSCommentaryLineDef& Line, const FPSTelemetryCommentaryEvent& Moment)
{
    if (Line.Moment != Moment.Moment || (!Line.Detail.IsNone() && Line.Detail != Moment.Detail))
    {
        return false;
    }
    if ((Line.MinDown > 0 && Moment.Down < Line.MinDown) || Moment.Down > Line.MaxDown)
    {
        return false;
    }
    if (Moment.Yards < Line.MinYards || Moment.Yards > Line.MaxYards || Moment.Stakes < Line.MinStakes)
    {
        return false;
    }
    if ((Line.bRequireFirstDown && !Moment.bFirstDown) || (Line.bRequireTurnover && !Moment.bTurnover))
    {
        return false;
    }
    return (!Line.bNeedsPrimary || !Moment.PrimaryName.IsEmpty())
        && (!Line.bNeedsSecondary || !Moment.SecondaryName.IsEmpty())
        && (!Line.bNeedsTotal || (!Moment.PrimaryStat.IsNone() && Moment.PrimaryGameTotal > 0));
}

FString UPSCommentaryEngine::FormatLine(const FPSCommentaryLineDef& Line, const FPSTelemetryCommentaryEvent& Moment)
{
    // Names and the clock are not ours to translate; numbers follow the culture; statistics and
    // fouls have their own rows.
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Player"), UPSLocalization::Verbatim(Moment.PrimaryName));
    Arguments.Add(TEXT("Other"), UPSLocalization::Verbatim(Moment.SecondaryName));
    Arguments.Add(TEXT("Yards"), UPSLocalization::FormatNumber(static_cast<float>(FMath::Abs(Moment.Yards)), 0));
    Arguments.Add(TEXT("Points"), UPSLocalization::FormatNumber(static_cast<float>(Moment.Points), 0));
    Arguments.Add(TEXT("Down"), UPSLocalization::FormatNumber(static_cast<float>(Moment.Down), 0));
    Arguments.Add(TEXT("Distance"), UPSLocalization::FormatNumber(static_cast<float>(Moment.Distance), 0));
    Arguments.Add(TEXT("Quarter"), UPSLocalization::FormatNumber(static_cast<float>(Moment.Quarter), 0));
    Arguments.Add(TEXT("Clock"), UPSLocalization::Verbatim(PSGameStateEvents::ClockText(Moment.GameClockSeconds)));
    Arguments.Add(TEXT("HomeScore"), UPSLocalization::FormatNumber(static_cast<float>(Moment.HomeScore), 0));
    Arguments.Add(TEXT("AwayScore"), UPSLocalization::FormatNumber(static_cast<float>(Moment.AwayScore), 0));
    Arguments.Add(TEXT("Total"), UPSLocalization::FormatNumber(static_cast<float>(Moment.PrimaryGameTotal), 0));
    const FString StatKey = FString::Printf(TEXT("Narrative.Stat.%s"), *Moment.PrimaryStat.ToString());
    Arguments.Add(TEXT("Stat"), UPSLocalization::HasText(StatKey) ? UPSLocalization::GetText(StatKey) : UPSLocalization::Verbatim(Moment.PrimaryStat.ToString()));
    const FString PenaltyKey = FString::Printf(TEXT("Commentary.Penalty.%s"), *Moment.Detail.ToString());
    Arguments.Add(TEXT("Penalty"), UPSLocalization::HasText(PenaltyKey) ? UPSLocalization::GetText(PenaltyKey) : UPSLocalization::Verbatim(Moment.Detail.ToString()));
    return UPSLocalization::Format(LineTextKey(Line.LineId), Arguments).ToString();
}

const FPSCommentaryLineDef* UPSCommentaryEngine::SelectLine(const FPSTelemetryCommentaryEvent& Moment, EPSCommentaryVoice Voice)
{
    const FPSCommentaryLibrary& Active = GetLibrary();
    TArray<TPair<const FPSCommentaryLineDef*, float>> Candidates;
    float Best = -TNumericLimits<float>::Max();
    for (const FPSCommentaryLineDef& Line : Active.Lines)
    {
        if (Line.Voice != Voice || !LineFits(Line, Moment))
        {
            continue;
        }
        const float* Last = LastSaidAt.Find(Line.LineId);
        if (Last && Clock - *Last < Line.CooldownSeconds)
        {
            continue;
        }
        const int32 UsedThisGame = PSCommentaryEnginePrivate::CountOf(GameUses, Line.LineId);
        if ((Line.MaxPerGame > 0 && UsedThisGame >= Line.MaxPerGame)
            || (Line.MaxPerSeason > 0 && PSCommentaryEnginePrivate::CountOf(SeasonUses, Line.LineId) >= Line.MaxPerSeason))
        {
            ++SuppressedByCapCount;
            CappedLines.Add(Line.LineId);
            continue;
        }
        const float Score = Line.Priority + Active.StakesWeight * Moment.Stakes + Active.NoveltyWeight * Moment.Novelty - Active.RepeatPenalty * UsedThisGame;
        Candidates.Emplace(&Line, Score);
        Best = FMath::Max(Best, Score);
    }
    // Variety: any line within the band of the best, picked by the game's seeded stream.
    TArray<const FPSCommentaryLineDef*> Band;
    for (const TPair<const FPSCommentaryLineDef*, float>& Candidate : Candidates)
    {
        if (Candidate.Value >= Best - Active.VarietyBand)
        {
            Band.Add(Candidate.Key);
        }
    }
    return Band.Num() > 0 ? Band[Variety.RandRange(0, Band.Num() - 1)] : nullptr;
}

void UPSCommentaryEngine::HandleCommentary(const FPSTelemetryCommentaryEvent& Moment)
{
    PS_PERF_SCOPE_NESTED(Audio);
    HandleMoment(Moment);
}

void UPSCommentaryEngine::HandleMoment(const FPSTelemetryCommentaryEvent& Moment)
{
    if (Moment.Moment == EPSCommentaryMoment::GameStart)
    {
        BeginGame();
    }
    CurrentPlay = Moment.PlayNumber;
    if (Moment.Moment == EPSCommentaryMoment::Snap)
    {
        CloseColorWindow();
    }
    if (const FPSCommentaryLineDef* Call = SelectLine(Moment, EPSCommentaryVoice::PlayByPlay))
    {
        Submit(MakeUtterance(*Call, Moment));
    }
    if (const FPSCommentaryLineDef* Analysis = SelectLine(Moment, EPSCommentaryVoice::Color))
    {
        FPSCommentaryUtterance Line = MakeUtterance(*Analysis, Moment);
        Line.NotBefore = Clock + GetLibrary().ColorWindowDelaySeconds;
        Submit(Line);
    }
    if (PSCommentaryEnginePrivate::OpensColorWindow(Moment.Moment))
    {
        bBetweenPlays = true;
        WindowOpensAt = Clock + GetLibrary().ColorWindowDelaySeconds;
    }
    if (Moment.Moment == EPSCommentaryMoment::GameEnd)
    {
        const FPSCommentaryRepetitionReport Report = BuildRepetitionReport();
        UE_LOG(LogTemp, Display, TEXT("UPSCommentaryEngine: %d lines, %d different, %.0f%% repeats; most used %s (%d); %d cut off, %d stale, %d held by caps."),
            Report.Spoken, Report.DistinctLines, Report.RepeatShare * 100.f, *Report.MostUsedLineId.ToString(), Report.MostUsedCount,
            Report.Interrupted, Report.DroppedStale, Report.SuppressedByCap);
    }
    Pump();
}

void UPSCommentaryEngine::HandleModelLine(const FPSTelemetryCommentaryEvent& Moment, const FString& Text)
{
    PS_PERF_SCOPE_NESTED(Audio);
    const FPSCommentaryLibrary& Active = GetLibrary();
    if (!Active.bUseModelLines || Text.TrimStartAndEnd().IsEmpty())
    {
        return;
    }
    // Only while the play it describes is still the current one.
    if (Moment.PlayNumber != CurrentPlay)
    {
        ++DroppedStaleCount;
        return;
    }
    // The model's line takes the place of the analyst's template line for this play.
    FVoiceState& Color = StateOf(EPSCommentaryVoice::Color);
    Color.Queue.RemoveAll([this](const FPSCommentaryUtterance& Waiting)
    {
        return Waiting.Source == EPSCommentarySource::Template && Waiting.PlayNumber == CurrentPlay;
    });
    FPSCommentaryUtterance Line;
    Line.Voice = EPSCommentaryVoice::Color;
    Line.Source = EPSCommentarySource::Model;
    Line.Priority = Active.ModelLinePriority;
    Line.Speaker = PSCommentaryEnginePrivate::VoiceName(EPSCommentaryVoice::Color);
    Line.Text = UPSLocalization::Verbatim(Text.TrimStartAndEnd()).ToString();
    Line.DurationSeconds = SpeakingSeconds(Line.Text);
    Line.Moment = Moment.Moment;
    Line.PlayNumber = Moment.PlayNumber;
    Line.CreatedAt = Clock;
    Line.NotBefore = Clock;
    Submit(Line);
    Pump();
}

FPSCommentaryUtterance UPSCommentaryEngine::MakeUtterance(const FPSCommentaryLineDef& Line, const FPSTelemetryCommentaryEvent& Moment) const
{
    FPSCommentaryUtterance Utterance;
    Utterance.LineId = Line.LineId;
    Utterance.Voice = Line.Voice;
    Utterance.Source = EPSCommentarySource::Template;
    Utterance.Priority = Line.Priority;
    Utterance.Speaker = PSCommentaryEnginePrivate::VoiceName(Line.Voice);
    Utterance.Text = FormatLine(Line, Moment);
    Utterance.Moment = Moment.Moment;
    Utterance.PlayNumber = Moment.PlayNumber;
    Utterance.CreatedAt = Clock;
    Utterance.NotBefore = Clock;
    return Utterance;
}

float UPSCommentaryEngine::SpeakingSeconds(const FString& Text)
{
    const FPSCommentaryLibrary& Active = GetLibrary();
    TArray<FString> Words;
    Text.ParseIntoArrayWS(Words);
    return FMath::Clamp(static_cast<float>(Words.Num()) / Active.WordsPerSecond, Active.MinLineSeconds, Active.MaxLineSeconds);
}

// --- Pacing (96.2, 96.3) ----------------------------------------------------------------------

void UPSCommentaryEngine::Submit(const FPSCommentaryUtterance& Line)
{
    FPSCommentaryUtterance Timed = Line;
    if (Timed.DurationSeconds <= 0.f)
    {
        Timed.DurationSeconds = SpeakingSeconds(Timed.Text);
    }
    FVoiceState& Voice = StateOf(Timed.Voice);
    if (Timed.Voice == EPSCommentaryVoice::PlayByPlay && Voice.bSpeaking && Timed.Priority >= Voice.Current.Priority + GetLibrary().InterruptMargin)
    {
        // A big play cuts off the line being said.
        Interrupt(EPSCommentaryVoice::PlayByPlay);
        Start(Timed);
        return;
    }
    Enqueue(Voice, Timed);
}

void UPSCommentaryEngine::Enqueue(FVoiceState& Voice, const FPSCommentaryUtterance& Line)
{
    Voice.Queue.Add(Line);
    // The most important first; the earlier of two equals first.
    Voice.Queue.StableSort([](const FPSCommentaryUtterance& A, const FPSCommentaryUtterance& B) { return A.Priority > B.Priority; });
    const int32 Keep = FMath::Max(1, GetLibrary().QueueLength);
    if (Voice.Queue.Num() > Keep)
    {
        DroppedStaleCount += Voice.Queue.Num() - Keep;
        Voice.Queue.SetNum(Keep);
    }
}

void UPSCommentaryEngine::Pump()
{
    const FPSCommentaryLibrary& Active = GetLibrary();

    // The play-by-play: its most important line still fresh.
    FVoiceState& Call = StateOf(EPSCommentaryVoice::PlayByPlay);
    const int32 StaleCalls = Call.Queue.RemoveAll([this, &Active](const FPSCommentaryUtterance& Waiting) { return Clock - Waiting.CreatedAt > Active.MaxDelaySeconds; });
    DroppedStaleCount += StaleCalls;
    if (!Call.bSpeaking && Call.Queue.Num() > 0)
    {
        const FPSCommentaryUtterance Next = Call.Queue[0];
        Call.Queue.RemoveAt(0);
        Start(Next);
    }

    // The analyst: in his window, never over the play-by-play.
    FVoiceState& Color = StateOf(EPSCommentaryVoice::Color);
    const int32 StaleAnalysis = Color.Queue.RemoveAll([this, &Active](const FPSCommentaryUtterance& Waiting) { return Clock - Waiting.CreatedAt > Active.ColorMaxDelaySeconds; });
    DroppedStaleCount += StaleAnalysis;
    if (Color.bSpeaking || Call.bSpeaking || !IsColorWindowOpen())
    {
        return;
    }
    const int32 Ready = Color.Queue.IndexOfByPredicate([this](const FPSCommentaryUtterance& Waiting) { return Waiting.NotBefore <= Clock; });
    if (Ready != INDEX_NONE)
    {
        const FPSCommentaryUtterance Next = Color.Queue[Ready];
        Color.Queue.RemoveAt(Ready);
        Start(Next);
        return;
    }

    // Nothing else to say: a storyline (Epic 93).
    const bool bGapPassed = LastTalkingPointAt < 0.f || Clock - LastTalkingPointAt >= Active.TalkingPointGapSeconds;
    if (Color.Queue.Num() == 0 && bGapPassed && TalkingPointsSpoken < Active.MaxTalkingPointsPerGame && TalkingPointsSpoken < TalkingPoints.Num())
    {
        const FPSNewsItem& Point = TalkingPoints[TalkingPointsSpoken];
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Headline"), UPSLocalization::FromLocalized(Point.Headline));
        Arguments.Add(TEXT("Body"), UPSLocalization::FromLocalized(Point.Body));
        FPSCommentaryUtterance Story;
        Story.Voice = EPSCommentaryVoice::Color;
        Story.Source = EPSCommentarySource::TalkingPoint;
        Story.Priority = Active.TalkingPointPriority;
        Story.Speaker = PSCommentaryEnginePrivate::VoiceName(EPSCommentaryVoice::Color);
        Story.Text = UPSLocalization::Format(TEXT("Commentary.Storyline"), Arguments).ToString();
        Story.DurationSeconds = SpeakingSeconds(Story.Text);
        Story.PlayNumber = CurrentPlay;
        Story.CreatedAt = Clock;
        Story.NotBefore = Clock;
        ++TalkingPointsSpoken;
        LastTalkingPointAt = Clock;
        Start(Story);
    }
}

void UPSCommentaryEngine::Start(const FPSCommentaryUtterance& Line)
{
    if (Line.Voice == EPSCommentaryVoice::PlayByPlay && StateOf(EPSCommentaryVoice::Color).bSpeaking)
    {
        // The analyst yields to the play-by-play.
        Interrupt(EPSCommentaryVoice::Color);
    }
    FVoiceState& Voice = StateOf(Line.Voice);
    Voice.bSpeaking = true;
    Voice.Current = Line;
    Voice.Current.StartedAt = Clock;
    Voice.EndsAt = Clock + Line.DurationSeconds;

    if (!Line.LineId.IsNone())
    {
        PSCommentaryEnginePrivate::Increment(GameUses, Line.LineId);
        PSCommentaryEnginePrivate::Increment(SeasonUses, Line.LineId);
        LastSaidAt.Add(Line.LineId, Clock);
    }
    Spoken.Add(Voice.Current);
    const int32 Excess = Spoken.Num() - FMath::Max(1, GetLibrary().MaxSpokenKept);
    if (Excess > 0)
    {
        Spoken.RemoveAt(0, Excess);
    }

    // Said out loud: the caption (Epic 103.3) and the recorded voice-over hear the same event.
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        FPSTelemetrySpeechEvent Speech;
        Speech.Speaker = Line.Speaker;
        Speech.Text = Line.Text;
        Speech.Channel = PSCommentaryEnginePrivate::CommentaryChannel;
        Speech.DurationSeconds = Line.DurationSeconds;
        Speech.LineId = Line.LineId;
        Bus->PublishSpeech(Speech);
    }
}

void UPSCommentaryEngine::Interrupt(EPSCommentaryVoice Voice)
{
    FVoiceState& State = StateOf(Voice);
    if (!State.bSpeaking)
    {
        return;
    }
    State.bSpeaking = false;
    ++InterruptedCount;
    for (int32 Index = Spoken.Num() - 1; Index >= 0; --Index)
    {
        if (Spoken[Index].Voice == Voice && Spoken[Index].StartedAt == State.Current.StartedAt && Spoken[Index].Text == State.Current.Text)
        {
            Spoken[Index].bInterrupted = true;
            break;
        }
    }
}

void UPSCommentaryEngine::CloseColorWindow()
{
    bBetweenPlays = false;
    Interrupt(EPSCommentaryVoice::Color);
    FVoiceState& Color = StateOf(EPSCommentaryVoice::Color);
    DroppedStaleCount += Color.Queue.Num();
    Color.Queue.Reset();
}

bool UPSCommentaryEngine::IsColorWindowOpen() const
{
    return bBetweenPlays && Clock >= WindowOpensAt;
}

void UPSCommentaryEngine::AdvanceTime(float DeltaSeconds)
{
    Clock += DeltaSeconds;
    SinceUpdate += DeltaSeconds;
    if (SinceUpdate < UpdateIntervalSeconds)
    {
        return;
    }
    SinceUpdate = 0.f;
    PS_PERF_SCOPE_NESTED(Audio);
    for (FVoiceState& Voice : Voices)
    {
        if (Voice.bSpeaking && Clock >= Voice.EndsAt)
        {
            Voice.bSpeaking = false;
        }
    }
    Pump();
}

void UPSCommentaryEngine::BeginGame()
{
    for (FVoiceState& Voice : Voices)
    {
        Voice = FVoiceState();
    }
    GameUses.Reset();
    LastSaidAt.Reset();
    Spoken.Reset();
    InterruptedCount = 0;
    DroppedStaleCount = 0;
    SuppressedByCapCount = 0;
    CappedLines.Reset();
    TalkingPointsSpoken = 0;
    LastTalkingPointAt = -1.f;
    bBetweenPlays = false;
    CurrentPlay = 0;
    Variety.Initialize(GetLibrary().Seed);
}

// --- State ----------------------------------------------------------------------------------

bool UPSCommentaryEngine::IsSpeaking(EPSCommentaryVoice Voice) const
{
    return StateOf(Voice).bSpeaking;
}

bool UPSCommentaryEngine::GetCurrentLine(EPSCommentaryVoice Voice, FPSCommentaryUtterance& OutLine) const
{
    const FVoiceState& State = StateOf(Voice);
    if (State.bSpeaking)
    {
        OutLine = State.Current;
    }
    return State.bSpeaking;
}

int32 UPSCommentaryEngine::GetQueueLength(EPSCommentaryVoice Voice) const
{
    return StateOf(Voice).Queue.Num();
}

// --- Storylines -------------------------------------------------------------------------------

void UPSCommentaryEngine::SetTalkingPoints(const TArray<FPSNewsItem>& InPoints)
{
    TalkingPoints = InPoints;
    TalkingPointsSpoken = 0;
}

int32 UPSCommentaryEngine::FeedFromNarrative(UPSLeagueNarrative* Narrative, FName HomeTeamId, FName AwayTeamId)
{
    if (!Narrative)
    {
        return 0;
    }
    SetTalkingPoints(Narrative->GetTalkingPoints(HomeTeamId, AwayTeamId, GetLibrary().MaxTalkingPointsPerGame));
    return TalkingPoints.Num();
}

// --- Repetition -------------------------------------------------------------------------------

void UPSCommentaryEngine::BeginSeason(int32 InSeason)
{
    Season = InSeason;
    SeasonUses.Reset();
}

FPSCommentarySeasonUsage UPSCommentaryEngine::GetSeasonUsage() const
{
    FPSCommentarySeasonUsage Usage;
    Usage.Season = Season;
    for (const TPair<FName, int32>& Entry : SeasonUses)
    {
        FPSCommentaryLineCount& Count = Usage.Lines.AddDefaulted_GetRef();
        Count.LineId = Entry.Key;
        Count.Uses = Entry.Value;
    }
    Usage.Lines.Sort([](const FPSCommentaryLineCount& A, const FPSCommentaryLineCount& B) { return A.LineId.LexicalLess(B.LineId); });
    return Usage;
}

void UPSCommentaryEngine::SetSeasonUsage(const FPSCommentarySeasonUsage& Usage)
{
    Season = Usage.Season;
    SeasonUses.Reset();
    for (const FPSCommentaryLineCount& Count : Usage.Lines)
    {
        SeasonUses.Add(Count.LineId, Count.Uses);
    }
}

void UPSCommentaryEngine::SaveTo(UPSFranchiseSaveGame* Save) const
{
    if (Save)
    {
        Save->CommentaryUsage = GetSeasonUsage();
    }
}

bool UPSCommentaryEngine::LoadFrom(const UPSFranchiseSaveGame* Save)
{
    if (!Save || (Save->CommentaryUsage.Season == 0 && Save->CommentaryUsage.Lines.Num() == 0))
    {
        return false;
    }
    SetSeasonUsage(Save->CommentaryUsage);
    return true;
}

int32 UPSCommentaryEngine::GetGameUses(FName LineId) const
{
    return PSCommentaryEnginePrivate::CountOf(GameUses, LineId);
}

int32 UPSCommentaryEngine::GetSeasonUses(FName LineId) const
{
    return PSCommentaryEnginePrivate::CountOf(SeasonUses, LineId);
}

FPSCommentaryRepetitionReport UPSCommentaryEngine::BuildRepetitionReport() const
{
    FPSCommentaryRepetitionReport Report;
    for (const TPair<FName, int32>& Entry : GameUses)
    {
        Report.Spoken += Entry.Value;
        ++Report.DistinctLines;
        if (Entry.Value > Report.MostUsedCount || (Entry.Value == Report.MostUsedCount && Entry.Key.LexicalLess(Report.MostUsedLineId)))
        {
            Report.MostUsedCount = Entry.Value;
            Report.MostUsedLineId = Entry.Key;
        }
    }
    Report.RepeatShare = Report.Spoken > 0 ? static_cast<float>(Report.Spoken - Report.DistinctLines) / static_cast<float>(Report.Spoken) : 0.f;
    Report.Interrupted = InterruptedCount;
    Report.DroppedStale = DroppedStaleCount;
    Report.SuppressedByCap = SuppressedByCapCount;
    Report.LinesAtCap = CappedLines.Array();
    Report.LinesAtCap.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
    return Report;
}
