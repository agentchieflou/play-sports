// PSGameIntelligenceSubsystem.cpp - Epic 82: the game's hooks for outside models, gated on the bridge
#include "PSGameIntelligenceSubsystem.h"
#include "PSDataIngestion.h"
#include "PSHighlightSubsystem.h"
#include "PSOpponentModel.h"
#include "PSPersonnelManager.h"
#include "PSPlayCallSubsystem.h"
#include "PSStatsEngine.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Features/IModularFeatures.h"
#include "Misc/Paths.h"

namespace PSGameIntelligencePrivate
{
    int32 SideIndex(bool bOffense)
    {
        return bOffense ? 1 : 0;
    }

    FString KindName(EPSIntelRequestKind Kind)
    {
        return StaticEnum<EPSIntelRequestKind>()->GetNameStringByValue(static_cast<int64>(Kind));
    }

    FString StateName(EPSIntelRequestState State)
    {
        return StaticEnum<EPSIntelRequestState>()->GetNameStringByValue(static_cast<int64>(State));
    }

    /** The call window's own situation, standing in for a game state the bus hasn't announced
     *  (a scripted window): the down, the clock and the timeouts, and a score with its margin. */
    void ApplyCallSituation(const FPSSituationContext& Situation, FPSTelemetryGameStateEvent& OutState)
    {
        OutState.Phase = TEXT("PreSnap");
        OutState.Quarter = Situation.Quarter;
        OutState.GameClockSeconds = Situation.GameClockSeconds;
        OutState.bGameClockRunning = Situation.bClockRunning;
        OutState.Down = Situation.Down;
        OutState.Distance = Situation.Distance;
        OutState.YardLine = Situation.YardLine;
        OutState.YardLineToGain = Situation.YardLine + Situation.Distance;
        OutState.bHomeHasPossession = Situation.bHomeHasPossession;
        OutState.bKickoff = Situation.bKickoff;
        const int32 OffenseScore = FMath::Max(0, Situation.ScoreDifferential);
        const int32 DefenseScore = FMath::Max(0, -Situation.ScoreDifferential);
        OutState.HomeScore = Situation.bHomeHasPossession ? OffenseScore : DefenseScore;
        OutState.AwayScore = Situation.bHomeHasPossession ? DefenseScore : OffenseScore;
        OutState.HomeTimeoutsRemaining = Situation.bHomeHasPossession ? Situation.TimeoutsRemaining : Situation.OpponentTimeoutsRemaining;
        OutState.AwayTimeoutsRemaining = Situation.bHomeHasPossession ? Situation.OpponentTimeoutsRemaining : Situation.TimeoutsRemaining;
    }
}

void UPSGameIntelligenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    BindToBus(Collection.InitializeDependency<UPSTelemetryBus>());
}

void UPSGameIntelligenceSubsystem::Deinitialize()
{
    UnbindFromBus();
    Super::Deinitialize();
}

bool UPSGameIntelligenceSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSGameIntelligenceSubsystem::BindToBus(UPSTelemetryBus* Bus)
{
    UnbindFromBus();
    if (Bus)
    {
        GameStateHandle = Bus->OnGameStateMC.AddUObject(this, &UPSGameIntelligenceSubsystem::HandleGameState);
        BoundBus = Bus;
    }
}

void UPSGameIntelligenceSubsystem::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnGameStateMC.Remove(GameStateHandle);
    }
    GameStateHandle.Reset();
    BoundBus.Reset();
}

// --- Tuning ------------------------------------------------------------------------------

FString UPSGameIntelligenceSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/game_intelligence.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSGameIntelligenceTuning& UPSGameIntelligenceSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSGameIntelligenceSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSGameIntelligenceTuning Loaded;
    if (!Ingestion->LoadGameIntelligenceTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSGameIntelligenceSubsystem: Could not load %s; keeping the current tuning."), *JsonFilePath);
        return false;
    }
    return SetTuning(Loaded);
}

bool UPSGameIntelligenceSubsystem::SetTuning(const FPSGameIntelligenceTuning& InTuning)
{
    const TArray<FString> Problems = ValidateTuning(InTuning);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSGameIntelligenceSubsystem: Tuning refused: %s"), *Problem);
    }
    if (Problems.Num() > 0)
    {
        return false;
    }
    Tuning = InTuning;
    bTuningLoaded = true;
    return true;
}

TArray<FString> UPSGameIntelligenceSubsystem::ValidateTuning(const FPSGameIntelligenceTuning& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.ContextBudgetChars < PSGameStateSerializer::MinBudgetChars)
    {
        Problems.Add(FString::Printf(TEXT("ContextBudgetChars %d is under %d, the least the game state always fits in."),
            InTuning.ContextBudgetChars, PSGameStateSerializer::MinBudgetChars));
    }
    if (InTuning.PlayCallTimeoutSeconds <= 0.f)
    {
        Problems.Add(TEXT("PlayCallTimeoutSeconds must be above 0."));
    }
    if (InTuning.MaxOpenRequests < 1 || InTuning.MaxAnswerChars < 1 || InTuning.MaxKeyPlays < 1)
    {
        Problems.Add(TEXT("MaxOpenRequests, MaxAnswerChars and MaxKeyPlays must be 1 or more."));
    }
    if (InTuning.LeadersPerCategory < 0)
    {
        Problems.Add(TEXT("LeadersPerCategory can't be negative."));
    }
    if (InTuning.PlayCallTask.IsEmpty() || InTuning.DriveSummaryTask.IsEmpty() || InTuning.GameAnalysisTask.IsEmpty())
    {
        Problems.Add(TEXT("Every request needs a model-router task (routing.json)."));
    }
    if (InTuning.PlayCallInstructions.IsEmpty() || InTuning.DriveSummaryInstructions.IsEmpty() || InTuning.GameAnalysisInstructions.IsEmpty())
    {
        Problems.Add(TEXT("Every request needs instructions."));
    }
    return Problems;
}

// --- The bridge --------------------------------------------------------------------------

FName UPSGameIntelligenceSubsystem::GetBridgeFeatureName()
{
    return FName(TEXT("AgenticLinkBridge"));
}

bool UPSGameIntelligenceSubsystem::IsBridgeOnline()
{
    return IModularFeatures::Get().IsModularFeatureAvailable(GetBridgeFeatureName());
}

// --- The game-state contract ---------------------------------------------------------------

void UPSGameIntelligenceSubsystem::SetStateSources(UPSPersonnelManager* InPersonnel, UPSStatsEngine* InStats)
{
    Personnel = InPersonnel;
    Stats = InStats;
}

FPSGameStateSources UPSGameIntelligenceSubsystem::GatherSources()
{
    FPSGameStateSources Sources;
    Sources.State = LastState;
    Sources.Personnel = Personnel.Get();
    Sources.Stats = Stats.Get();
    Sources.LeadersPerCategory = GetTuning().LeadersPerCategory;
    if (UWorld* World = GetWorld())
    {
        Sources.Opponent = World->GetSubsystem<UPSOpponentModel>();
        if (const UPSPlayCallSubsystem* PlayCalls = World->GetSubsystem<UPSPlayCallSubsystem>())
        {
            Sources.bHumanOffense = PlayCalls->IsHumanSide(true);
            Sources.bHumanDefense = PlayCalls->IsHumanSide(false);
        }
    }
    return Sources;
}

FString UPSGameIntelligenceSubsystem::GetGameStateJson()
{
    return PSGameStateSerializer::Serialize(GatherSources(), GetTuning().ContextBudgetChars);
}

// --- Play-call consultation ----------------------------------------------------------------

bool UPSGameIntelligenceSubsystem::SetConsultation(bool bOffense, bool bDefense)
{
    if (!IsBridgeOnline())
    {
        UE_LOG(LogTemp, Display, TEXT("UPSGameIntelligenceSubsystem: Play-call consultation refused: the agent bridge is offline."));
        return false;
    }
    bConsultSide[PSGameIntelligencePrivate::SideIndex(true)] = bOffense;
    bConsultSide[PSGameIntelligencePrivate::SideIndex(false)] = bDefense;

    // A side no longer consulted stops waiting.
    for (const bool bSide : { true, false })
    {
        const int32 Side = PSGameIntelligencePrivate::SideIndex(bSide);
        FPSIntelRequest* Pending = bConsultSide[Side] ? nullptr : FindRequestMutable(PlayCallRequestIds[Side]);
        if (Pending && Pending->IsOpen())
        {
            Pending->State = EPSIntelRequestState::Closed;
        }
    }
    UE_LOG(LogTemp, Display, TEXT("UPSGameIntelligenceSubsystem: An outside model calls plays for the CPU's offense: %s, defense: %s."),
        bOffense ? TEXT("yes") : TEXT("no"), bDefense ? TEXT("yes") : TEXT("no"));
    return true;
}

bool UPSGameIntelligenceSubsystem::IsConsulting(bool bOffense) const
{
    return bConsultSide[PSGameIntelligencePrivate::SideIndex(bOffense)] && IsBridgeOnline();
}

int32 UPSGameIntelligenceSubsystem::OpenPlayConsultation(bool bOffense, const FPSSituationContext& Situation, const TArray<FPSPlayDefinition>& Candidates)
{
    const int32 Side = PSGameIntelligencePrivate::SideIndex(bOffense);

    // A new window replaces the side's earlier request.
    FPSIntelRequest* Earlier = FindRequestMutable(PlayCallRequestIds[Side]);
    if (Earlier && (Earlier->IsOpen() || Earlier->State == EPSIntelRequestState::Answered))
    {
        Earlier->State = EPSIntelRequestState::Closed;
    }
    PlayCallRequestIds[Side] = 0;
    if (!IsConsulting(bOffense) || Candidates.Num() == 0)
    {
        return 0;
    }

    const FPSGameIntelligenceTuning& Settings = GetTuning();
    FPSGameStateSources Sources = GatherSources();
    if (!bHaveState)
    {
        PSGameIntelligencePrivate::ApplyCallSituation(Situation, Sources.State);
    }

    FPSIntelRequest Request;
    Request.Kind = EPSIntelRequestKind::PlayCall;
    Request.Task = Settings.PlayCallTask;
    Request.Instructions = Settings.PlayCallInstructions;
    Request.Context = PSGameStateSerializer::Serialize(Sources, Settings.ContextBudgetChars);
    Request.bOffense = bOffense;
    Request.TimeoutSeconds = Settings.PlayCallTimeoutSeconds;
    for (const FPSPlayDefinition& Play : Candidates)
    {
        FPSIntelChoice& Choice = Request.Choices.AddDefaulted_GetRef();
        Choice.Id = Play.PlayId;
        Choice.Note = FString::Printf(TEXT("%s, %s: %s"), *Play.PlayCategory, *Play.Formation, *Play.DisplayName);
    }
    PlayCallRequestIds[Side] = AddRequest(Request);
    return PlayCallRequestIds[Side];
}

bool UPSGameIntelligenceSubsystem::WaitForConsultation(bool bOffense, float DeltaSeconds)
{
    FPSIntelRequest* Request = FindRequestMutable(PlayCallRequestIds[PSGameIntelligencePrivate::SideIndex(bOffense)]);
    if (!Request || !Request->IsOpen())
    {
        return false;
    }
    if (!IsBridgeOnline())
    {
        Request->State = EPSIntelRequestState::Closed;
        return false;
    }
    Request->WaitedSeconds += FMath::Max(0.f, DeltaSeconds);
    if (Request->WaitedSeconds >= Request->TimeoutSeconds)
    {
        Request->State = EPSIntelRequestState::TimedOut;
        UE_LOG(LogTemp, Display, TEXT("UPSGameIntelligenceSubsystem: Request %d had no answer in %.1f s; the CPU calls its own play."),
            Request->RequestId, Request->TimeoutSeconds);
        return false;
    }
    return true;
}

FName UPSGameIntelligenceSubsystem::GetAnsweredPlay(bool bOffense) const
{
    FPSIntelRequest Request;
    if (FindRequest(PlayCallRequestIds[PSGameIntelligencePrivate::SideIndex(bOffense)], Request) && Request.State == EPSIntelRequestState::Answered)
    {
        return FName(*Request.Answer);
    }
    return NAME_None;
}

FName UPSGameIntelligenceSubsystem::SuggestPlay_Implementation(const FPSSituationContext& Situation, bool bOffense)
{
    return GetAnsweredPlay(bOffense);
}

void UPSGameIntelligenceSubsystem::CloseConsultations(FName OffensePlayId, FName DefensePlayId)
{
    for (const bool bOffense : { true, false })
    {
        const int32 Side = PSGameIntelligencePrivate::SideIndex(bOffense);
        FPSIntelRequest* Request = FindRequestMutable(PlayCallRequestIds[Side]);
        PlayCallRequestIds[Side] = 0;
        if (!Request)
        {
            continue;
        }
        if (Request->State == EPSIntelRequestState::Answered)
        {
            const FName Ran = bOffense ? OffensePlayId : DefensePlayId;
            Request->State = FName(*Request->Answer) == Ran ? EPSIntelRequestState::Used : EPSIntelRequestState::Overruled;
            UE_LOG(LogTemp, Display, TEXT("UPSGameIntelligenceSubsystem: Request %d's play %s was %s."),
                Request->RequestId, *Request->Answer, *PSGameIntelligencePrivate::StateName(Request->State));
        }
        else if (Request->IsOpen())
        {
            Request->State = EPSIntelRequestState::Closed;
        }
    }
}

// --- Requests ------------------------------------------------------------------------------

int32 UPSGameIntelligenceSubsystem::AddRequest(FPSIntelRequest Request)
{
    const int32 MaxOpen = FMath::Max(1, GetTuning().MaxOpenRequests);

    // Too many open: the oldest that nothing waits on is closed.
    int32 OpenCount = 0;
    for (const FPSIntelRequest& Existing : Requests)
    {
        OpenCount += Existing.IsOpen() ? 1 : 0;
    }
    for (FPSIntelRequest& Existing : Requests)
    {
        if (OpenCount < MaxOpen)
        {
            break;
        }
        if (Existing.IsOpen() && Existing.TimeoutSeconds <= 0.f)
        {
            Existing.State = EPSIntelRequestState::Closed;
            --OpenCount;
        }
    }

    // Finished requests are kept a while for inspection, the oldest let go first.
    const int32 MaxKept = MaxOpen * 4;
    for (int32 Index = 0; Index < Requests.Num() && Requests.Num() >= MaxKept;)
    {
        const bool bFinished = !Requests[Index].IsOpen() && Requests[Index].State != EPSIntelRequestState::Answered;
        if (bFinished)
        {
            Requests.RemoveAt(Index);
        }
        else
        {
            ++Index;
        }
    }

    Request.RequestId = NextRequestId++;
    Request.State = EPSIntelRequestState::Open;
    Request.WaitedSeconds = 0.f;
    Requests.Add(Request);
    UE_LOG(LogTemp, Display, TEXT("UPSGameIntelligenceSubsystem: Request %d (%s, task '%s') is open for an outside model."),
        Request.RequestId, *PSGameIntelligencePrivate::KindName(Request.Kind), *Request.Task);

    OnRequestOpenedMC.Broadcast(Request);
    if (OnRequestOpened.IsBound())
    {
        OnRequestOpened.Broadcast(Request);
    }
    return Request.RequestId;
}

int32 UPSGameIntelligenceSubsystem::OpenTextRequest(EPSIntelRequestKind Kind, const FString& Task, const FString& Instructions, const FString& Context)
{
    if (!IsBridgeOnline() || Kind == EPSIntelRequestKind::PlayCall || Task.IsEmpty() || Instructions.IsEmpty() || Context.IsEmpty())
    {
        return 0;
    }
    FPSIntelRequest Request;
    Request.Kind = Kind;
    Request.Task = Task;
    Request.Instructions = Instructions;
    Request.Context = Context;
    return AddRequest(Request);
}

FPSIntelRequest* UPSGameIntelligenceSubsystem::FindRequestMutable(int32 RequestId)
{
    if (RequestId <= 0)
    {
        return nullptr;
    }
    return Requests.FindByPredicate([RequestId](const FPSIntelRequest& Candidate) { return Candidate.RequestId == RequestId; });
}

bool UPSGameIntelligenceSubsystem::FindRequest(int32 RequestId, FPSIntelRequest& OutRequest) const
{
    const FPSIntelRequest* Found = RequestId > 0
        ? Requests.FindByPredicate([RequestId](const FPSIntelRequest& Candidate) { return Candidate.RequestId == RequestId; })
        : nullptr;
    if (Found)
    {
        OutRequest = *Found;
    }
    return Found != nullptr;
}

FString UPSGameIntelligenceSubsystem::GetPendingRequestsJson()
{
    const bool bOnline = IsBridgeOnline();
    TArray<TSharedPtr<FJsonValue>> Pending;
    for (const FPSIntelRequest& Request : Requests)
    {
        if (!bOnline || !Request.IsOpen())
        {
            continue;
        }
        TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("id"), Request.RequestId);
        Entry->SetStringField(TEXT("kind"), PSGameIntelligencePrivate::KindName(Request.Kind));
        Entry->SetStringField(TEXT("task"), Request.Task);
        Entry->SetStringField(TEXT("instructions"), Request.Instructions);
        if (Request.Kind == EPSIntelRequestKind::PlayCall)
        {
            Entry->SetStringField(TEXT("side"), Request.bOffense ? TEXT("offense") : TEXT("defense"));
            Entry->SetNumberField(TEXT("secondsLeft"), FMath::Max(0, FMath::CeilToInt(Request.TimeoutSeconds - Request.WaitedSeconds)));
        }
        if (Request.Choices.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> Choices;
            for (const FPSIntelChoice& Choice : Request.Choices)
            {
                TSharedRef<FJsonObject> ChoiceObject = MakeShared<FJsonObject>();
                ChoiceObject->SetStringField(TEXT("id"), Choice.Id.ToString());
                ChoiceObject->SetStringField(TEXT("note"), Choice.Note);
                Choices.Add(MakeShared<FJsonValueObject>(ChoiceObject));
            }
            Entry->SetArrayField(TEXT("choices"), Choices);
        }
        const TSharedPtr<FJsonObject> Context = PSGameStateSerializer::ParseObject(Request.Context);
        if (Context.IsValid())
        {
            Entry->SetObjectField(TEXT("context"), Context);
        }
        else
        {
            Entry->SetStringField(TEXT("context"), Request.Context);
        }
        Pending.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("bridge"), bOnline);
    Root->SetArrayField(TEXT("requests"), Pending);
    return PSGameStateSerializer::ToCompactJson(Root);
}

bool UPSGameIntelligenceSubsystem::AnswerRequest(int32 RequestId, const FString& Answer, FString& OutReason)
{
    if (!IsBridgeOnline())
    {
        OutReason = TEXT("The agent bridge is offline.");
        return false;
    }
    FPSIntelRequest* Request = FindRequestMutable(RequestId);
    if (!Request)
    {
        OutReason = FString::Printf(TEXT("No request %d."), RequestId);
        return false;
    }
    if (!Request->IsOpen())
    {
        OutReason = FString::Printf(TEXT("Request %d is %s, no longer open."), RequestId, *PSGameIntelligencePrivate::StateName(Request->State));
        return false;
    }
    const FString Given = Answer.TrimStartAndEnd();
    if (Given.IsEmpty())
    {
        OutReason = TEXT("The answer is empty.");
        return false;
    }
    if (Given.Len() > GetTuning().MaxAnswerChars)
    {
        OutReason = FString::Printf(TEXT("The answer is %d characters; at most %d are taken."), Given.Len(), GetTuning().MaxAnswerChars);
        return false;
    }

    // A request with choices takes one of them, by id, and nothing else.
    FString Accepted = Given;
    if (Request->Choices.Num() > 0)
    {
        const FPSIntelChoice* Match = Request->Choices.FindByPredicate([&Given](const FPSIntelChoice& Choice)
        {
            return Choice.Id.ToString().Equals(Given, ESearchCase::IgnoreCase);
        });
        if (!Match)
        {
            TArray<FString> Ids;
            for (const FPSIntelChoice& Choice : Request->Choices)
            {
                Ids.Add(Choice.Id.ToString());
            }
            OutReason = FString::Printf(TEXT("'%s' is not one of request %d's choices: %s."), *Given.Left(64), RequestId, *FString::Join(Ids, TEXT(", ")));
            return false;
        }
        Accepted = Match->Id.ToString();
    }

    Request->Answer = Accepted;
    Request->State = EPSIntelRequestState::Answered;
    if (Request->Kind == EPSIntelRequestKind::DriveSummary)
    {
        Analysis.DriveSummaryText = Accepted;
    }
    else if (Request->Kind == EPSIntelRequestKind::GameAnalysis)
    {
        Analysis.GameAnalysisText = Accepted;
    }
    UE_LOG(LogTemp, Display, TEXT("UPSGameIntelligenceSubsystem: Request %d (%s) answered."), RequestId, *PSGameIntelligencePrivate::KindName(Request->Kind));
    OutReason.Reset();

    const FPSIntelRequest Answered = *Request;
    OnRequestAnsweredMC.Broadcast(Answered);
    return true;
}

// --- Post-game analysis --------------------------------------------------------------------

void UPSGameIntelligenceSubsystem::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    // A new game: the drive count went back, or the clock is back in regulation after a final.
    if (bHaveState && (Event.CompletedDrives < LastState.CompletedDrives || (bGameOver && Event.Quarter <= 4)))
    {
        ResetGame();
    }

    // A drive ended: the team that had the ball before this state, with the drive the
    // simulation announces as its last.
    if (bHaveState && Event.CompletedDrives > LastState.CompletedDrives)
    {
        FPSDriveRecap& Drive = Drives.AddDefaulted_GetRef();
        Drive.bHomeTeam = LastState.bHomeHasPossession;
        Drive.Quarter = LastState.Quarter;
        Drive.Plays = Event.LastDrivePlays;
        Drive.Yards = Event.LastDriveYards;
        Drive.Result = Event.LastDriveResult;
    }
    LastState = Event;
    bHaveState = true;

    // The final whistle, as Epic 42 reads it: a game state past the fourth quarter. The
    // highlights hear the event first (it is recorded before it is broadcast), so the game's
    // last play is already scored.
    if (Event.Quarter > 4 && !bGameOver)
    {
        bGameOver = true;
        BuildPostGameAnalysis();
        OpenPostGameRequests();
    }
}

void UPSGameIntelligenceSubsystem::ResetGame()
{
    Drives.Reset();
    Analysis = FPSGameAnalysis();
    bGameOver = false;
    bHaveState = false;
}

FPSGameAnalysis UPSGameIntelligenceSubsystem::BuildPostGameAnalysis()
{
    FPSGameAnalysis Built;
    Built.bFinal = bGameOver;
    Built.HomeScore = LastState.HomeScore;
    Built.AwayScore = LastState.AwayScore;
    Built.Drives = Drives;

    // The key plays: Epic 42's highlights, the most important first.
    UWorld* World = GetWorld();
    const UPSHighlightSubsystem* Highlights = World ? World->GetSubsystem<UPSHighlightSubsystem>() : nullptr;
    if (Highlights)
    {
        TArray<FPSPlayHighlight> Reel = Highlights->GetReel();
        Reel.StableSort([](const FPSPlayHighlight& A, const FPSPlayHighlight& B) { return A.Importance > B.Importance; });
        const int32 Count = FMath::Min(Reel.Num(), FMath::Max(0, GetTuning().MaxKeyPlays));
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const FPSPlayHighlight& Highlight = Reel[Index];
            FPSKeyPlay& Key = Built.KeyPlays.AddDefaulted_GetRef();
            Key.PlayNumber = Highlight.PlayNumber;
            Key.Kind = Highlight.Kind;
            Key.Importance = Highlight.Importance;
            Key.Yards = Highlight.Impact.Yards;
            Key.Points = Highlight.Impact.Points;
            Key.bTurnover = Highlight.Impact.bTurnover;
            Key.BrokenTackles = Highlight.Impact.BrokenTackles;
            Key.Quarter = Highlight.Situation.Quarter;
            Key.Down = Highlight.Situation.Down;
            Key.Distance = Highlight.Situation.Distance;
            Key.YardLine = Highlight.Situation.YardLine;
            Key.bHomeOffense = Highlight.Situation.bHomeHasPossession;
        }
    }

    // What a model already wrote stays.
    Built.DriveSummaryText = Analysis.DriveSummaryText;
    Built.GameAnalysisText = Analysis.GameAnalysisText;
    Built.DriveSummaryRequestId = Analysis.DriveSummaryRequestId;
    Built.GameAnalysisRequestId = Analysis.GameAnalysisRequestId;
    Analysis = Built;
    return Analysis;
}

FString UPSGameIntelligenceSubsystem::GetPostGameAnalysisJson()
{
    BuildPostGameAnalysis();
    return PSGameStateSerializer::SerializeAnalysis(Analysis, GetTuning().ContextBudgetChars);
}

void UPSGameIntelligenceSubsystem::OpenPostGameRequests()
{
    const FPSGameIntelligenceTuning& Settings = GetTuning();
    if (!IsBridgeOnline() || !Settings.bPostGameRequests)
    {
        return;
    }
    const FString Context = PSGameStateSerializer::SerializeAnalysis(Analysis, Settings.ContextBudgetChars);

    FPSIntelRequest Summary;
    Summary.Kind = EPSIntelRequestKind::DriveSummary;
    Summary.Task = Settings.DriveSummaryTask;
    Summary.Instructions = Settings.DriveSummaryInstructions;
    Summary.Context = Context;
    const int32 SummaryId = AddRequest(Summary);
    Analysis.DriveSummaryRequestId = SummaryId;

    FPSIntelRequest Explained;
    Explained.Kind = EPSIntelRequestKind::GameAnalysis;
    Explained.Task = Settings.GameAnalysisTask;
    Explained.Instructions = Settings.GameAnalysisInstructions;
    Explained.Context = Context;
    const int32 ExplainedId = AddRequest(Explained);
    Analysis.GameAnalysisRequestId = ExplainedId;
}
