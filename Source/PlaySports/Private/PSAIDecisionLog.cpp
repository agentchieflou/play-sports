#include "PSAIDecisionLog.h"
#include "PSAIFieldSnapshot.h"
#include "PSDataIngestion.h"
#include "PSPlayerPawn.h"
#include "DrawDebugHelpers.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PSAIDecisionLogPrivate
{
    static TAutoConsoleVariable<int32> CVarDecisionLog(
        TEXT("ps.AI.DecisionLog"),
        0,
        TEXT("1: record every AI decision (Epic 85): who, what, at what, why and the options weighed."),
        ECVF_Default);

    static TAutoConsoleVariable<int32> CVarDebugOverlay(
        TEXT("ps.AI.DebugOverlay"),
        0,
        TEXT("1: draw each player's latest AI decision above him, with a line to his target (turns the decision log on)."),
        ECVF_Default);

    static TAutoConsoleVariable<int32> CVarPostMortem(
        TEXT("ps.AI.PostMortem"),
        0,
        TEXT("1: write a JSON post-mortem of every play's AI decisions under Saved/ (turns the decision log on)."),
        ECVF_Default);

    TSharedRef<FJsonObject> SnapToJson(const FPSTelemetrySnapEvent& Event)
    {
        TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        FJsonObjectConverter::UStructToJsonObject(FPSTelemetrySnapEvent::StaticStruct(), &Event, Object, 0, 0);
        return Object;
    }
}

void UPSAIDecisionLog::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    BindToBus(Collection.InitializeDependency<UPSTelemetryBus>());
}

void UPSAIDecisionLog::Deinitialize()
{
    UnbindFromBus();
    Super::Deinitialize();
}

bool UPSAIDecisionLog::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPSAIDecisionLog::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSAIDecisionLog, STATGROUP_Tickables);
}

void UPSAIDecisionLog::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (PSAIDecisionLogPrivate::CVarDebugOverlay.GetValueOnGameThread() != 0)
    {
        DrawOverlay();
    }
}

FString UPSAIDecisionLog::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/ai_debug.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

UPSAIDecisionLog* UPSAIDecisionLog::Get(const UWorld* World)
{
    return World ? World->GetSubsystem<UPSAIDecisionLog>() : nullptr;
}

const FPSAIDebugTuning& UPSAIDecisionLog::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSAIDecisionLog::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSAIDebugTuning Loaded;
    if (!Ingestion->LoadAIDebugTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSAIDecisionLog: Could not load the AI debug settings from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    Tuning = Loaded;
    return true;
}

void UPSAIDecisionLog::SetTuning(const FPSAIDebugTuning& InTuning)
{
    Tuning = InTuning;
    bTuningLoaded = true;
}

bool UPSAIDecisionLog::IsLogging()
{
    using namespace PSAIDecisionLogPrivate;
    if (LoggingOverride >= 0)
    {
        return LoggingOverride > 0 || IsWritingPostMortems();
    }
    return GetTuning().bLogDecisions || CVarDecisionLog.GetValueOnGameThread() != 0 || CVarDebugOverlay.GetValueOnGameThread() != 0 || IsWritingPostMortems();
}

void UPSAIDecisionLog::SetLogging(bool bOn)
{
    LoggingOverride = bOn ? 1 : 0;
}

bool UPSAIDecisionLog::IsWritingPostMortems()
{
    if (PostMortemOverride >= 0)
    {
        return PostMortemOverride > 0;
    }
    return GetTuning().bWritePostMortems || PSAIDecisionLogPrivate::CVarPostMortem.GetValueOnGameThread() != 0;
}

void UPSAIDecisionLog::SetWritePostMortems(bool bOn)
{
    PostMortemOverride = bOn ? 1 : 0;
}

void UPSAIDecisionLog::Record(const FPSAIDecisionRecord& InRecord)
{
    if (!IsLogging())
    {
        return;
    }
    FPSAIDecisionRecord Entry = InRecord;
    if (Entry.System == TEXT("PlayCall") && !IsAtSnap())
    {
        PendingRecords.Add(MoveTemp(Entry));
        return;
    }
    Entry.PlayIndex = PlayIndex;
    Latest.Add(Entry.AgentId, Entry);
    if (PlayRecords.Num() < FMath::Max(0, GetTuning().MaxRecordsPerPlay))
    {
        PlayRecords.Add(MoveTemp(Entry));
    }
}

TArray<FPSAIDecisionRecord> UPSAIDecisionLog::GetAgentStream(FName AgentId) const
{
    TArray<FPSAIDecisionRecord> Stream;
    for (const FPSAIDecisionRecord& Entry : PlayRecords)
    {
        if (Entry.AgentId == AgentId)
        {
            Stream.Add(Entry);
        }
    }
    return Stream;
}

bool UPSAIDecisionLog::GetLatest(FName AgentId, FPSAIDecisionRecord& OutRecord) const
{
    if (const FPSAIDecisionRecord* Found = Latest.Find(AgentId))
    {
        OutRecord = *Found;
        return true;
    }
    return false;
}

FString UPSAIDecisionLog::DescribeForOverlay(const FPSAIDecisionRecord& InRecord)
{
    FString Text = InRecord.AgentId.ToString();
    if (!InRecord.Assignment.IsEmpty())
    {
        Text += FString::Printf(TEXT(" [%s]"), *InRecord.Assignment);
    }
    Text += TEXT(" ") + InRecord.Action;
    if (!InRecord.Target.IsEmpty())
    {
        Text += TEXT(" -> ") + InRecord.Target;
    }
    if (!InRecord.Reason.IsEmpty())
    {
        Text += TEXT("\n") + InRecord.Reason;
    }
    return Text;
}

bool UPSAIDecisionLog::IsAtSnap() const
{
    const UPSTelemetryBus* Bus = BoundBus.Get();
    if (!bPlayOpen || !Bus)
    {
        return false;
    }
    const TArray<FPSTelemetryEvent> History = Bus->GetEventHistory();
    for (int32 Index = History.Num() - 1; Index >= 0; --Index)
    {
        if (History[Index].Sequence <= SnapSequence)
        {
            return History[Index].Sequence == SnapSequence;
        }
        if (History[Index].EventType != EPSTelemetryEventType::PlayCall)
        {
            return false;
        }
    }
    return false;
}

void UPSAIDecisionLog::DrawOverlay()
{
#if ENABLE_DRAW_DEBUG
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    const FPSAIDebugTuning& Settings = GetTuning();
    for (APSPlayerPawn* Pawn : UPSAIFieldSnapshot::GetFieldPawns(World))
    {
        FPSAIDecisionRecord Entry;
        if (!Pawn || !GetLatest(Pawn->GetAttributes().PlayerId, Entry))
        {
            continue;
        }
        const FColor Color = Pawn->TeamSide == EPSTeamSide::Offense ? FColor::Cyan : FColor::Orange;
        const FVector Location = Pawn->GetActorLocation();
        DrawDebugString(World, Location + FVector(0.f, 0.f, Settings.OverlayHeightCm), DescribeForOverlay(Entry), nullptr, Color, 0.f, true, Settings.OverlayFontScale);
        if (!Entry.TargetLocation.IsZero())
        {
            DrawDebugLine(World, Location, Entry.TargetLocation, Color, false, -1.f, 0, 2.f);
        }
    }
#endif
}

FString UPSAIDecisionLog::GetPostMortemDirectory()
{
    return FPaths::ProjectSavedDir() / GetTuning().PostMortemDirectory;
}

FString UPSAIDecisionLog::BuildPostMortemJson() const
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("PlayIndex"), PlayIndex);

    // Where the play sits in the bus's event history: the replay's join key (Epic 41).
    const UPSTelemetryBus* Bus = BoundBus.Get();
    const int32 EndSequence = Bus ? Bus->GetLastEventSequence() : SnapSequence;
    Root->SetNumberField(TEXT("SnapSequence"), SnapSequence);
    Root->SetNumberField(TEXT("EndSequence"), EndSequence);
    Root->SetObjectField(TEXT("Snap"), PSAIDecisionLogPrivate::SnapToJson(Snap));

    TArray<TSharedPtr<FJsonValue>> Calls;
    for (const FPSTelemetryPlayCallEvent& Call : PlayCalls)
    {
        TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        FJsonObjectConverter::UStructToJsonObject(FPSTelemetryPlayCallEvent::StaticStruct(), &Call, Object, 0, 0);
        Calls.Add(MakeShared<FJsonValueObject>(Object));
    }
    Root->SetArrayField(TEXT("Calls"), Calls);

    TArray<TSharedPtr<FJsonValue>> Events;
    if (Bus)
    {
        for (const FPSTelemetryEvent& Event : Bus->GetEventHistory())
        {
            if (Event.Sequence < SnapSequence || Event.Sequence > EndSequence)
            {
                continue;
            }
            TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
            Object->SetNumberField(TEXT("Sequence"), Event.Sequence);
            Object->SetStringField(TEXT("EventType"), StaticEnum<EPSTelemetryEventType>()->GetNameStringByValue(static_cast<int64>(Event.EventType)));
            Object->SetStringField(TEXT("Description"), Event.Description);
            Events.Add(MakeShared<FJsonValueObject>(Object));
        }
    }
    Root->SetArrayField(TEXT("BusEvents"), Events);

    // Every player's decision stream, in the order they first decided.
    TArray<FName> Agents;
    for (const FPSAIDecisionRecord& Entry : PlayRecords)
    {
        Agents.AddUnique(Entry.AgentId);
    }
    TArray<TSharedPtr<FJsonValue>> Streams;
    for (const FName& AgentId : Agents)
    {
        TArray<TSharedPtr<FJsonValue>> Decisions;
        for (const FPSAIDecisionRecord& Entry : PlayRecords)
        {
            if (Entry.AgentId == AgentId)
            {
                TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
                FJsonObjectConverter::UStructToJsonObject(FPSAIDecisionRecord::StaticStruct(), &Entry, Object, 0, 0);
                Decisions.Add(MakeShared<FJsonValueObject>(Object));
            }
        }
        TSharedRef<FJsonObject> Stream = MakeShared<FJsonObject>();
        Stream->SetStringField(TEXT("AgentId"), AgentId.ToString());
        Stream->SetArrayField(TEXT("Decisions"), Decisions);
        Streams.Add(MakeShared<FJsonValueObject>(Stream));
    }
    Root->SetArrayField(TEXT("Agents"), Streams);

    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(Root, Writer);
    return Json;
}

FString UPSAIDecisionLog::WritePostMortem()
{
    const FPSAIDebugTuning& Settings = GetTuning();
    const FString Directory = GetPostMortemDirectory();
    const FString Path = Directory / FString::Printf(TEXT("Play_%s_%04d.json"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")), PlayIndex);
    if (!FFileHelper::SaveStringToFile(BuildPostMortemJson(), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSAIDecisionLog: Could not write the play's post-mortem to %s."), *Path);
        return FString();
    }

    // Keep the newest MaxPostMortemFiles: the names sort oldest first.
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Directory / TEXT("Play_*.json")), true, false);
    Files.Sort();
    for (int32 Index = 0; Index < Files.Num() - FMath::Max(1, Settings.MaxPostMortemFiles); ++Index)
    {
        IFileManager::Get().Delete(*(Directory / Files[Index]));
    }
    return Path;
}

void UPSAIDecisionLog::FinishPlay()
{
    if (!bPlayOpen)
    {
        return;
    }
    bPlayOpen = false;
    if (IsWritingPostMortems())
    {
        WritePostMortem();
    }
}

void UPSAIDecisionLog::BindToBus(UPSTelemetryBus* Bus)
{
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnSnapMC.AddUObject(this, &UPSAIDecisionLog::HandleSnap);
    Bus->OnPhaseChangeMC.AddUObject(this, &UPSAIDecisionLog::HandlePhaseChange);
    Bus->OnPlayCallMC.AddUObject(this, &UPSAIDecisionLog::HandlePlayCall);
    BoundBus = Bus;
}

void UPSAIDecisionLog::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
        Bus->OnPlayCallMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSAIDecisionLog::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    // A play nobody ended (no Scoring phase) is finished by the next snap.
    FinishPlay();
    ++PlayIndex;
    bPlayOpen = true;
    Snap = Event;
    const UPSTelemetryBus* Bus = BoundBus.Get();
    SnapSequence = Bus ? Bus->GetLastEventSequence() : 0;
    PlayRecords = MoveTemp(PendingRecords);
    PendingRecords.Reset();
    Latest.Reset();
    for (FPSAIDecisionRecord& Entry : PlayRecords)
    {
        Entry.PlayIndex = PlayIndex;
        Latest.Add(Entry.AgentId, Entry);
    }
    PlayCalls = MoveTemp(PendingCalls);
    PendingCalls.Reset();
}

void UPSAIDecisionLog::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring"))
    {
        FinishPlay();
    }
}

void UPSAIDecisionLog::HandlePlayCall(const FPSTelemetryPlayCallEvent& Event)
{
    // A side's latest call before the snap is the one it runs. One made at the snap (a snap
    // outside a call window), when the open play has no call for that side, is this play's.
    auto SameSide = [&Event](const FPSTelemetryPlayCallEvent& Call) { return Call.bOffense == Event.bOffense; };
    TArray<FPSTelemetryPlayCallEvent>& Into = bPlayOpen && !PlayCalls.ContainsByPredicate(SameSide) ? PlayCalls : PendingCalls;
    Into.RemoveAll(SameSide);
    Into.Add(Event);
}
