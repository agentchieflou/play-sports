#include "PSPlayCallSubsystem.h"
#include "PSCoachingAI.h"
#include "PSDataIngestion.h"
#include "PSPlaybookIngestion.h"
#include "PSPlayOrchestrator.h"
#include "PSPlayerPawn.h"
#include "PSPlaySimulation.h"
#include "PSProfileSaveGame.h"
#include "PSSaveSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

namespace PSPlayCallPrivate
{
    FString DataPath(const TCHAR* FileName)
    {
        FString Path = FPaths::ProjectDir() / TEXT("Data") / FileName;
        FPaths::CollapseRelativeDirectories(Path);
        return Path;
    }

    /** "ShortPass" -> "Short pass". */
    FString SplitCategory(const FString& Category)
    {
        FString Out;
        for (int32 Index = 0; Index < Category.Len(); ++Index)
        {
            const TCHAR Char = Category[Index];
            if (Index > 0 && FChar::IsUpper(Char))
            {
                Out.AppendChar(TEXT(' '));
                Out.AppendChar(FChar::ToLower(Char));
            }
            else
            {
                Out.AppendChar(Char);
            }
        }
        return Out;
    }

    const TCHAR* DownOrdinal(int32 Down)
    {
        switch (Down)
        {
        case 1:  return TEXT("1st");
        case 2:  return TEXT("2nd");
        case 3:  return TEXT("3rd");
        default: return TEXT("4th");
        }
    }

    const TCHAR* RoleAbbreviation(EPlayerRole Role)
    {
        switch (Role)
        {
        case EPlayerRole::Quarterback:      return TEXT("QB");
        case EPlayerRole::RunningBack:      return TEXT("RB");
        case EPlayerRole::WideReceiver:     return TEXT("WR");
        case EPlayerRole::TightEnd:         return TEXT("TE");
        case EPlayerRole::OffensiveLineman: return TEXT("OL");
        case EPlayerRole::DefensiveLineman: return TEXT("DL");
        case EPlayerRole::Linebacker:       return TEXT("LB");
        case EPlayerRole::DefensiveBack:    return TEXT("DB");
        default:                            return TEXT("?");
        }
    }

    const TCHAR* KindLabel(EPSAssignmentKind Kind)
    {
        switch (Kind)
        {
        case EPSAssignmentKind::Route:        return TEXT("route");
        case EPSAssignmentKind::PassBlock:    return TEXT("pass block");
        case EPSAssignmentKind::RunBlock:     return TEXT("run block");
        case EPSAssignmentKind::ManCoverage:  return TEXT("man");
        case EPSAssignmentKind::ZoneCoverage: return TEXT("zone");
        case EPSAssignmentKind::PassRush:     return TEXT("rush");
        case EPSAssignmentKind::RunFit:       return TEXT("run fit");
        case EPSAssignmentKind::Blitz:        return TEXT("blitz");
        default:                              return TEXT("?");
        }
    }
}

void UPSPlayCallSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnSnapMC.AddUObject(this, &UPSPlayCallSubsystem::HandleSnap);
        Bus->OnControlChangeMC.AddUObject(this, &UPSPlayCallSubsystem::HandleControlChange);
        BoundBus = Bus;
    }
}

void UPSPlayCallSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnControlChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();

    Super::Deinitialize();
}

bool UPSPlayCallSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FString UPSPlayCallSubsystem::GetDefaultPlaybookPath()
{
    return PSPlayCallPrivate::DataPath(TEXT("sample_playbook.json"));
}

FString UPSPlayCallSubsystem::GetDefaultRoutesPath()
{
    return PSPlayCallPrivate::DataPath(TEXT("sample_routes.json"));
}

FString UPSPlayCallSubsystem::GetDefaultTuningPath()
{
    return PSPlayCallPrivate::DataPath(TEXT("play_call.json"));
}

FString UPSPlayCallSubsystem::GetDefaultAdjustmentsPath()
{
    return PSPlayCallPrivate::DataPath(TEXT("defensive_adjustments.json"));
}

bool UPSPlayCallSubsystem::LoadPlaybook(const FString& PlaysJsonPath, const FString& RoutesJsonPath)
{
    bPlaybookLoaded = true;

    if (!PlaysTable)
    {
        PlaysTable = NewObject<UDataTable>(this, TEXT("PlayCallPlays"));
        PlaysTable->RowStruct = FPSPlayDefinition::StaticStruct();
    }
    if (!RoutesTable)
    {
        RoutesTable = NewObject<UDataTable>(this, TEXT("PlayCallRoutes"));
        RoutesTable->RowStruct = FPSRoute::StaticStruct();
    }

    UPSPlaybookIngestion* Ingestion = NewObject<UPSPlaybookIngestion>(this);
    const bool bPlaysLoaded = Ingestion->LoadPlaysFromJson(PlaysJsonPath, PlaysTable);
    const bool bRoutesLoaded = Ingestion->LoadRoutesFromJson(RoutesJsonPath, RoutesTable);
    if (!bPlaysLoaded || !bRoutesLoaded)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayCallSubsystem: Could not load the playbook (%s, %s)."), *PlaysJsonPath, *RoutesJsonPath);
    }

    Plays.Reset();
    for (const TPair<FName, uint8*>& Row : PlaysTable->GetRowMap())
    {
        Plays.Add(*reinterpret_cast<const FPSPlayDefinition*>(Row.Value));
    }
    return bPlaysLoaded && bRoutesLoaded;
}

void UPSPlayCallSubsystem::EnsurePlaybookLoaded()
{
    if (!bPlaybookLoaded)
    {
        LoadPlaybook(GetDefaultPlaybookPath(), GetDefaultRoutesPath());
    }
}

bool UPSPlayCallSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPlayCallTuningRow Loaded;
    if (!Ingestion->LoadPlayCallTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayCallSubsystem: Could not load play-call tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    Tuning = Loaded;
    return true;
}

const FPlayCallTuningRow& UPSPlayCallSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

TArray<FPSPlayDefinition> UPSPlayCallSubsystem::GetPlays(bool bOffense)
{
    EnsurePlaybookLoaded();
    return Plays.FilterByPredicate([bOffense](const FPSPlayDefinition& Play) { return Play.bIsOffensivePlay == bOffense; });
}

TArray<FString> UPSPlayCallSubsystem::GetFormations(bool bOffense)
{
    TArray<FString> Formations;
    for (const FPSPlayDefinition& Play : GetPlays(bOffense))
    {
        Formations.AddUnique(Play.Formation);
    }
    return Formations;
}

TArray<FPSPlayDefinition> UPSPlayCallSubsystem::GetPlaysInFormation(const FString& Formation, bool bOffense)
{
    return GetPlays(bOffense).FilterByPredicate([&Formation](const FPSPlayDefinition& Play) { return Play.Formation == Formation; });
}

bool UPSPlayCallSubsystem::FindPlay(FName PlayId, FPSPlayDefinition& OutPlay)
{
    EnsurePlaybookLoaded();
    if (const FPSPlayDefinition* Found = Plays.FindByPredicate([PlayId](const FPSPlayDefinition& Play) { return Play.PlayId == PlayId; }))
    {
        OutPlay = *Found;
        return true;
    }
    return false;
}

const UDataTable* UPSPlayCallSubsystem::GetRouteLibrary()
{
    EnsurePlaybookLoaded();
    return RoutesTable;
}

TArray<FPSMenuOptionDef> UPSPlayCallSubsystem::BuildFormationOptions(bool bOffense, FName PlaysScreenId)
{
    TArray<FPSMenuOptionDef> Options;
    for (const FString& Formation : GetFormations(bOffense))
    {
        FPSMenuOptionDef Option;
        Option.OptionId = FName(*Formation);
        Option.Label = Formation;
        Option.Detail = FString::Printf(TEXT("%d play(s)"), GetPlaysInFormation(Formation, bOffense).Num());
        Option.TargetScreen = PlaysScreenId;
        Option.Payload = FName(*Formation);
        Options.Add(Option);
    }
    return Options;
}

TArray<FPSMenuOptionDef> UPSPlayCallSubsystem::BuildPlayOptions(const FString& Formation, bool bOffense)
{
    TArray<FPSMenuOptionDef> Options;
    for (const FPSPlayDefinition& Play : GetPlaysInFormation(Formation, bOffense))
    {
        Options.Add(MakePlayOption(Play, bOffense
            ? FString::Printf(TEXT("%s    %s"), *Play.DisplayName, *PSPlayCallPrivate::SplitCategory(Play.PlayCategory))
            : FString::Printf(TEXT("%s    %s %s"), *Play.DisplayName, *Play.Front, *Play.CoverageShell)));
    }
    return Options;
}

FPSMenuOptionDef UPSPlayCallSubsystem::MakePlayOption(const FPSPlayDefinition& Play, const FString& Label)
{
    FPSMenuOptionDef Option;
    Option.OptionId = Play.PlayId;
    // Starred plays are marked wherever they are listed.
    Option.Label = IsFavorite(Play.PlayId) ? FString::Printf(TEXT("* %s"), *Label) : Label;
    Option.Detail = DescribePlay(Play);
    Option.Command = EPSMenuCommand::CallPlay;
    Option.Payload = Play.PlayId;
    return Option;
}

TArray<FPSPlaySuggestion> UPSPlayCallSubsystem::RankPlays(bool bOffense)
{
    if (!CoachingAI)
    {
        CoachingAI = NewObject<UPSCoachingAI>(this);
    }
    const FPSTendencyProfile Tendency = FPSTendencyProfile();
    return CoachingAI->RankPlays(Situation, Tendency, GetPlays(bOffense), bOffense);
}

TArray<FPSMenuOptionDef> UPSPlayCallSubsystem::BuildSuggestionOptions(bool bOffense)
{
    TArray<FPSMenuOptionDef> Options;
    const TArray<FPSPlaySuggestion> Ranked = RankPlays(bOffense);
    if (Ranked.Num() == 0)
    {
        return Options;
    }

    const FPSPlaySuggestion& Top = Ranked[0];
    FPSMenuOptionDef Option;
    Option.OptionId = TEXT("Suggested");
    Option.Label = FString::Printf(TEXT("Suggested: %s"), *Top.DisplayName);
    Option.Detail = Top.Reasons.Num() > 0 ? FString::Join(Top.Reasons, TEXT(" \u00B7 ")) : FString(TEXT("Nothing in the situation leans either way"));
    Option.Command = EPSMenuCommand::CallPlay;
    Option.Payload = Top.PlayId;
    Options.Add(Option);
    return Options;
}

TArray<FPSMenuOptionDef> UPSPlayCallSubsystem::BuildRecentOptions(bool bOffense)
{
    TArray<FPSMenuOptionDef> Options;
    for (const FName PlayId : GetRecentCalls(bOffense, GetTuning().RecentPlaysShown))
    {
        FPSPlayDefinition Play;
        if (!FindPlay(PlayId, Play))
        {
            continue;
        }
        Options.Add(MakePlayOption(Play, FString::Printf(TEXT("%s    %s"), *Play.DisplayName, *Play.Formation)));
    }
    return Options;
}

TArray<FPSMenuOptionDef> UPSPlayCallSubsystem::BuildFavoriteOptions(bool bOffense)
{
    TArray<FPSMenuOptionDef> Options;
    for (const FName PlayId : GetFavorites(bOffense))
    {
        FPSPlayDefinition Play;
        if (FindPlay(PlayId, Play))
        {
            Options.Add(MakePlayOption(Play, FString::Printf(TEXT("%s    %s"), *Play.DisplayName, *Play.Formation)));
        }
    }
    return Options;
}

const TArray<FPSDefensiveAdjustmentDef>& UPSPlayCallSubsystem::GetAdjustments()
{
    if (!bAdjustmentsLoaded)
    {
        LoadAdjustmentsFromJson(GetDefaultAdjustmentsPath());
    }
    return AdjustmentCatalog.Adjustments;
}

bool UPSPlayCallSubsystem::LoadAdjustmentsFromJson(const FString& JsonFilePath)
{
    bAdjustmentsLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSDefensiveAdjustmentCatalog Loaded;
    if (!Ingestion->LoadDefensiveAdjustmentsFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayCallSubsystem: Could not load defensive adjustments from %s."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateAdjustments(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayCallSubsystem: %s"), *Problem);
    }
    AdjustmentCatalog = MoveTemp(Loaded);
    return true;
}

TArray<FString> UPSPlayCallSubsystem::ValidateAdjustments(const FPSDefensiveAdjustmentCatalog& InCatalog)
{
    TArray<FString> Problems;
    TSet<FName> Seen;
    for (const FPSDefensiveAdjustmentDef& Adjustment : InCatalog.Adjustments)
    {
        const FString Id = Adjustment.AdjustmentId.ToString();
        if (Adjustment.AdjustmentId.IsNone() || Seen.Contains(Adjustment.AdjustmentId))
        {
            Problems.Add(FString::Printf(TEXT("Adjustment '%s': empty or duplicate AdjustmentId."), *Id));
        }
        Seen.Add(Adjustment.AdjustmentId);
        if (Adjustment.Label.IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("Adjustment '%s' has no Label."), *Id));
        }
        if (Adjustment.Role != EPlayerRole::DefensiveLineman && Adjustment.Role != EPlayerRole::Linebacker && Adjustment.Role != EPlayerRole::DefensiveBack)
        {
            Problems.Add(FString::Printf(TEXT("Adjustment '%s': %s is not a defender."), *Id, *UEnum::GetValueAsString(Adjustment.Role)));
        }
        const EPSAssignmentKind Kind = Adjustment.Kind;
        if (Kind != EPSAssignmentKind::PassRush && Kind != EPSAssignmentKind::Blitz && Kind != EPSAssignmentKind::RunFit
            && Kind != EPSAssignmentKind::ManCoverage && Kind != EPSAssignmentKind::ZoneCoverage)
        {
            Problems.Add(FString::Printf(TEXT("Adjustment '%s': %s is not a defensive assignment."), *Id, *UEnum::GetValueAsString(Kind)));
        }
    }
    return Problems;
}

bool UPSPlayCallSubsystem::SetDefensiveAdjustment(FName AdjustmentId)
{
    if (!bWindowOpen || !DefenseCall.IsSet())
    {
        return false;
    }
    if (!AdjustmentId.IsNone() && !GetAdjustments().ContainsByPredicate([AdjustmentId](const FPSDefensiveAdjustmentDef& Def) { return Def.AdjustmentId == AdjustmentId; }))
    {
        return false;
    }
    DefensiveAdjustment = AdjustmentId;
    UE_LOG(LogTemp, Display, TEXT("UPSPlayCallSubsystem: Defensive adjustment: %s."), AdjustmentId.IsNone() ? TEXT("none") : *AdjustmentId.ToString());
    return true;
}

bool UPSPlayCallSubsystem::GetDefensivePlayToRun(FPSPlayDefinition& OutPlay)
{
    if (!FindPlay(DefenseCall.PlayId, OutPlay))
    {
        return false;
    }
    const FName AdjustmentId = DefensiveAdjustment;
    const FPSDefensiveAdjustmentDef* Adjustment = GetAdjustments().FindByPredicate([AdjustmentId](const FPSDefensiveAdjustmentDef& Def) { return Def.AdjustmentId == AdjustmentId; });
    if (Adjustment)
    {
        for (FPSPlayAssignment& Assignment : OutPlay.Assignments)
        {
            if (Assignment.Role == Adjustment->Role)
            {
                Assignment.Kind = Adjustment->Kind;
            }
        }
    }
    return true;
}

TArray<FPSMenuOptionDef> UPSPlayCallSubsystem::BuildAdjustmentOptions()
{
    TArray<FPSMenuOptionDef> Options;
    FPSMenuOptionDef AsCalled;
    AsCalled.OptionId = TEXT("NoAdjustment");
    AsCalled.Label = TEXT("No adjustment");
    AsCalled.Detail = TEXT("Play the call as it is");
    AsCalled.Command = EPSMenuCommand::ApplyAdjustment;
    Options.Add(AsCalled);

    for (const FPSDefensiveAdjustmentDef& Adjustment : GetAdjustments())
    {
        FPSMenuOptionDef Option;
        Option.OptionId = Adjustment.AdjustmentId;
        Option.Label = Adjustment.Label;
        Option.Detail = Adjustment.Description;
        Option.Command = EPSMenuCommand::ApplyAdjustment;
        Option.Payload = Adjustment.AdjustmentId;
        Options.Add(Option);
    }
    return Options;
}

FString UPSPlayCallSubsystem::BuildAdjustmentScreenBody() const
{
    // The offense's formation is visible at the line; its play is not.
    const FPSPlayDefinition* Offense = OffenseCall.IsSet()
        ? Plays.FindByPredicate([this](const FPSPlayDefinition& Play) { return Play.PlayId == OffenseCall.PlayId; })
        : nullptr;
    return Offense ? FString::Printf(TEXT("Offense lines up in %s"), *Offense->Formation) : FString(TEXT("Offense hasn't lined up yet"));
}

UPSSaveSubsystem* UPSPlayCallSubsystem::GetSaveSubsystem() const
{
    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<UPSSaveSubsystem>() : nullptr;
}

void UPSPlayCallSubsystem::EnsureFavoritesLoaded()
{
    if (bFavoritesLoaded)
    {
        return;
    }
    bFavoritesLoaded = true;

    UPSSaveSubsystem* Saves = GetSaveSubsystem();
    const FString Slot = UPSProfileSaveGame::GetDefaultSlotName();
    if (Saves && Saves->DoesSlotExist(Slot))
    {
        if (const UPSProfileSaveGame* Profile = Cast<UPSProfileSaveGame>(Saves->LoadFromSlot(Slot)))
        {
            FavoritePlays = Profile->FavoritePlays;
        }
    }
}

void UPSPlayCallSubsystem::SaveFavorites()
{
    UPSSaveSubsystem* Saves = GetSaveSubsystem();
    if (!Saves)
    {
        return;
    }

    // Keep whatever else the profile holds; only the favourites change.
    const FString Slot = UPSProfileSaveGame::GetDefaultSlotName();
    UPSProfileSaveGame* Profile = Saves->DoesSlotExist(Slot) ? Cast<UPSProfileSaveGame>(Saves->LoadFromSlot(Slot)) : nullptr;
    if (!Profile)
    {
        Profile = NewObject<UPSProfileSaveGame>(this);
    }
    Profile->FavoritePlays = FavoritePlays;
    if (!Saves->SaveToSlot(Profile, Slot))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayCallSubsystem: Could not save favourite plays to %s."), *Slot);
    }
}

bool UPSPlayCallSubsystem::IsFavorite(FName PlayId)
{
    EnsureFavoritesLoaded();
    return FavoritePlays.Contains(PlayId);
}

bool UPSPlayCallSubsystem::ToggleFavorite(FName PlayId)
{
    FPSPlayDefinition Play;
    if (!FindPlay(PlayId, Play))
    {
        return false;
    }
    EnsureFavoritesLoaded();
    const bool bNowFavorite = FavoritePlays.Remove(PlayId) == 0;
    if (bNowFavorite)
    {
        FavoritePlays.Add(PlayId);
    }
    SaveFavorites();
    return bNowFavorite;
}

TArray<FName> UPSPlayCallSubsystem::GetFavorites(bool bOffense)
{
    EnsureFavoritesLoaded();
    TArray<FName> Favorites;
    for (const FName PlayId : FavoritePlays)
    {
        FPSPlayDefinition Play;
        if (FindPlay(PlayId, Play) && Play.bIsOffensivePlay == bOffense)
        {
            Favorites.Add(PlayId);
        }
    }
    return Favorites;
}

FString UPSPlayCallSubsystem::BuildCallScreenBody(bool bOffense) const
{
    const FString Tendencies = DescribeTendencies(bOffense);
    const FString SituationLine = DescribeSituation(Situation);
    return Tendencies.IsEmpty() ? SituationLine : FString::Printf(TEXT("%s\n%s"), *SituationLine, *Tendencies);
}

FString UPSPlayCallSubsystem::DescribeSituation(const FPSSituationContext& InSituation)
{
    // YardLine counts from the offense's own goal line (0) to the opponent's (100).
    FString Spot;
    if (InSituation.YardLine == 50)
    {
        Spot = TEXT("midfield");
    }
    else if (InSituation.YardLine < 50)
    {
        Spot = FString::Printf(TEXT("own %d"), InSituation.YardLine);
    }
    else
    {
        Spot = FString::Printf(TEXT("opp %d"), 100 - InSituation.YardLine);
    }
    return FString::Printf(TEXT("%s & %d at %s"), PSPlayCallPrivate::DownOrdinal(InSituation.Down), InSituation.Distance, *Spot);
}

TArray<FName> UPSPlayCallSubsystem::GetRecentCalls(bool bOffense, int32 MaxCount) const
{
    TArray<FName> Recent;
    for (int32 Index = CallHistory.Num() - 1; Index >= 0 && Recent.Num() < MaxCount; --Index)
    {
        if (CallHistory[Index].bOffense == bOffense)
        {
            Recent.AddUnique(CallHistory[Index].PlayId);
        }
    }
    return Recent;
}

FString UPSPlayCallSubsystem::DescribeTendencies(bool bOffense) const
{
    TMap<FString, int32> Counts;
    TArray<FString> Order;
    int32 Total = 0;
    for (const FPSPlayCallRecord& Record : CallHistory)
    {
        if (Record.bOffense != bOffense)
        {
            continue;
        }
        if (!Counts.Contains(Record.PlayCategory))
        {
            Order.Add(Record.PlayCategory);
        }
        Counts.FindOrAdd(Record.PlayCategory)++;
        ++Total;
    }
    if (Total == 0)
    {
        return FString();
    }

    // Most-called first, so the readout leads with what an opponent would key on.
    Order.StableSort([&Counts](const FString& A, const FString& B) { return Counts[A] > Counts[B]; });
    TArray<FString> Parts;
    for (const FString& Category : Order)
    {
        Parts.Add(FString::Printf(TEXT("%s %d%%"), *PSPlayCallPrivate::SplitCategory(Category), FMath::RoundToInt(100.f * Counts[Category] / Total)));
    }
    return FString::Printf(TEXT("Your calls: %s"), *FString::Join(Parts, TEXT(" \u00B7 ")));
}

FString UPSPlayCallSubsystem::DescribePlay(const FPSPlayDefinition& Play)
{
    TArray<FString> Parts;
    for (const FPSPlayAssignment& Assignment : Play.Assignments)
    {
        // The quarterback's dropback is implied; a named route says what everyone runs.
        if (Assignment.Role == EPlayerRole::Quarterback && Assignment.RouteId.IsNone())
        {
            continue;
        }
        const FString What = (Assignment.Kind == EPSAssignmentKind::Route && !Assignment.RouteId.IsNone())
            ? Assignment.RouteId.ToString()
            : FString(PSPlayCallPrivate::KindLabel(Assignment.Kind));
        Parts.Add(FString::Printf(TEXT("%s %s"), PSPlayCallPrivate::RoleAbbreviation(Assignment.Role), *What));
    }
    return FString::Join(Parts, TEXT(" \u00B7 "));
}

FPSSituationContext UPSPlayCallSubsystem::MakeSituation(const FPlayState& State)
{
    FPSSituationContext Context;
    Context.Down = State.Down;
    Context.Distance = State.Distance;
    Context.YardLine = State.YardLine;
    Context.Quarter = State.Quarter;
    Context.GameClockSeconds = State.GameClockSeconds;
    Context.ScoreDifferential = State.bHomeHasPossession ? State.HomeScore - State.AwayScore : State.AwayScore - State.HomeScore;
    Context.TimeoutsRemaining = State.bHomeHasPossession ? State.HomeTimeoutsRemaining : State.AwayTimeoutsRemaining;
    return Context;
}

void UPSPlayCallSubsystem::OpenPlayCall(const FPSSituationContext& InSituation)
{
    EnsurePlaybookLoaded();
    Situation = InSituation;
    OffenseCall = FPSPlayCall();
    DefenseCall = FPSPlayCall();
    DefensiveAdjustment = NAME_None;
    TimeSinceCallsComplete = 0.f;
    bSnapRequested = false;
    bWindowOpen = true;

    for (const bool bOffense : { true, false })
    {
        if (IsHumanSide(bOffense))
        {
            OnHumanCallNeeded.Broadcast(bOffense);
        }
    }
}

bool UPSPlayCallSubsystem::CallPlay(FName PlayId, EPSPlayCaller Caller)
{
    FPSPlayDefinition Play;
    if (!bWindowOpen || Caller == EPSPlayCaller::None || !FindPlay(PlayId, Play))
    {
        return false;
    }
    SetCall(Play, Caller);
    return true;
}

void UPSPlayCallSubsystem::SetCall(const FPSPlayDefinition& Play, EPSPlayCaller Caller)
{
    FPSPlayCall& Call = Play.bIsOffensivePlay ? OffenseCall : DefenseCall;
    Call.PlayId = Play.PlayId;
    Call.Caller = Caller;
    TimeSinceCallsComplete = 0.f;

    UE_LOG(LogTemp, Display, TEXT("UPSPlayCallSubsystem: %s calls %s (%s)."),
        Play.bIsOffensivePlay ? TEXT("Offense") : TEXT("Defense"), *Play.DisplayName, *UEnum::GetValueAsString(Caller));

    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        FPSTelemetryPlayCallEvent Event;
        Event.PlayId = Play.PlayId;
        Event.DisplayName = Play.DisplayName;
        Event.Formation = Play.Formation;
        Event.PlayCategory = Play.PlayCategory;
        Event.bOffense = Play.bIsOffensivePlay;
        Event.bHumanCall = Caller == EPSPlayCaller::Human;
        Bus->PublishPlayCall(Event);
    }
}

void UPSPlayCallSubsystem::CallForCpu(bool bOffense)
{
    const TArray<FPSPlayDefinition> Candidates = GetPlays(bOffense);
    if (Candidates.Num() == 0)
    {
        return;
    }
    if (!CoachingAI)
    {
        CoachingAI = NewObject<UPSCoachingAI>(this);
    }

    FPSTendencyProfile Tendency;
    const FName Chosen = bOffense
        ? CoachingAI->SelectOffensivePlay(Situation, Tendency, Candidates)
        : CoachingAI->SelectDefensivePlay(Situation, Tendency, Candidates);
    const FPSPlayDefinition* Play = Candidates.FindByPredicate([Chosen](const FPSPlayDefinition& Candidate) { return Candidate.PlayId == Chosen; });
    SetCall(Play ? *Play : Candidates[0], EPSPlayCaller::CPU);
}

void UPSPlayCallSubsystem::QuickCall(bool bOffense)
{
    const TArray<FPSPlaySuggestion> Ranked = RankPlays(bOffense);
    FPSPlayDefinition Play;
    if (Ranked.Num() > 0 && FindPlay(Ranked[0].PlayId, Play))
    {
        UE_LOG(LogTemp, Display, TEXT("UPSPlayCallSubsystem: Play clock at %.1f s with no call; quick-calling the suggestion."), PlayClockSeconds);
        SetCall(Play, EPSPlayCaller::QuickCall);
    }
}

bool UPSPlayCallSubsystem::IsHumanSide(bool bOffense) const
{
    for (const TPair<FName, bool>& Entry : HumanPawnSides)
    {
        if (Entry.Value == bOffense)
        {
            return true;
        }
    }
    return false;
}

bool UPSPlayCallSubsystem::IsWaitingForHuman(bool bOffense) const
{
    return bWindowOpen && IsHumanSide(bOffense) && !GetCall(bOffense).IsSet();
}

bool UPSPlayCallSubsystem::RequestSnap()
{
    if (!bWindowOpen || OffenseCall.Caller != EPSPlayCaller::Human)
    {
        return false;
    }
    bSnapRequested = true;
    return true;
}

bool UPSPlayCallSubsystem::PollReadyToSnap(float DeltaSeconds)
{
    if (!bWindowOpen)
    {
        return false;
    }

    for (const bool bOffense : { true, false })
    {
        if (!GetCall(bOffense).IsSet() && !IsHumanSide(bOffense))
        {
            CallForCpu(bOffense);
        }
        else if (!GetCall(bOffense).IsSet() && PlayClockSeconds >= 0.f && PlayClockSeconds <= GetTuning().QuickCallPlayClockSeconds)
        {
            QuickCall(bOffense);
        }
    }
    if (!OffenseCall.IsSet() || !DefenseCall.IsSet())
    {
        TimeSinceCallsComplete = 0.f;
        return false;
    }

    TimeSinceCallsComplete += DeltaSeconds;
    if (OffenseCall.Caller == EPSPlayCaller::Human && IsHumanSide(true))
    {
        return bSnapRequested;
    }
    return TimeSinceCallsComplete >= GetTuning().CpuSnapDelaySeconds;
}

void UPSPlayCallSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    // A snap outside a call window (the functional gym, scripted tests) still runs called
    // plays: the CPU calls both sides for the snap's situation.
    if (!bWindowOpen)
    {
        OffenseCall = FPSPlayCall();
        DefenseCall = FPSPlayCall();
        DefensiveAdjustment = NAME_None;
        Situation.Down = Event.Down;
        Situation.Distance = Event.Distance;
        Situation.YardLine = Event.YardLine;
        Situation.GameClockSeconds = Event.GameClockSeconds;
    }
    for (const bool bOffense : { true, false })
    {
        if (!GetCall(bOffense).IsSet())
        {
            CallForCpu(bOffense);
        }
        // What the human chose and ran, for the recent list and the tendency readout.
        FPSPlayDefinition Ran;
        if (GetCall(bOffense).Caller == EPSPlayCaller::Human && FindPlay(GetCall(bOffense).PlayId, Ran))
        {
            FPSPlayCallRecord Record;
            Record.PlayId = Ran.PlayId;
            Record.PlayCategory = Ran.PlayCategory;
            Record.bOffense = bOffense;
            CallHistory.Add(Record);
        }
    }

    Distribute(Event.LineOfScrimmage);
    bWindowOpen = false;
    bSnapRequested = false;
}

void UPSPlayCallSubsystem::Distribute(const FVector& LineOfScrimmage)
{
    if (!Orchestrator)
    {
        Orchestrator = NewObject<UPSPlayOrchestrator>(this);
    }

    TArray<APSPlayerPawn*> OnFieldPawns;
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        OnFieldPawns.Add(*It);
    }

    FPSPlayDefinition OffensePlay;
    if (FindPlay(OffenseCall.PlayId, OffensePlay))
    {
        Orchestrator->DistributePlayCall(OffensePlay, OnFieldPawns, RoutesTable, LineOfScrimmage);
    }
    FPSPlayDefinition DefensePlay;
    if (GetDefensivePlayToRun(DefensePlay))
    {
        Orchestrator->DistributePlayCall(DefensePlay, OnFieldPawns, RoutesTable, LineOfScrimmage);
    }
}

void UPSPlayCallSubsystem::HandleControlChange(const FPSTelemetryControlChangeEvent& Event)
{
    if (!Event.bHumanControlled)
    {
        HumanPawnSides.Remove(Event.PlayerId);
        return;
    }

    const APSPlayerPawn* Pawn = FindPawnByPlayerId(Event.PlayerId);
    if (!Pawn)
    {
        return;
    }
    const bool bOffense = Pawn->TeamSide == EPSTeamSide::Offense;
    HumanPawnSides.Add(Event.PlayerId, bOffense);

    // A human arriving on a side the CPU already called takes the call over.
    FPSPlayCall& Call = bOffense ? OffenseCall : DefenseCall;
    if (bWindowOpen && Call.Caller == EPSPlayCaller::CPU)
    {
        Call = FPSPlayCall();
        TimeSinceCallsComplete = 0.f;
    }
    if (IsWaitingForHuman(bOffense))
    {
        OnHumanCallNeeded.Broadcast(bOffense);
    }
}

APSPlayerPawn* UPSPlayCallSubsystem::FindPawnByPlayerId(FName PlayerId) const
{
    for (TActorIterator<APSPlayerPawn> It(GetWorld()); It; ++It)
    {
        if (It->GetAttributes().PlayerId == PlayerId)
        {
            return *It;
        }
    }
    return nullptr;
}
