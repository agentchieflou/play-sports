#include "PSPlayCallSubsystem.h"
#include "PSAIDecisionLog.h"
#include "PSOpponentModel.h"
#include "PSCoachingAI.h"
#include "PSDataIngestion.h"
#include "PSLocalization.h"
#include "PSPlaybookIngestion.h"
#include "PSPlayOrchestrator.h"
#include "PSPlayerPawn.h"
#include "PSPlaySimulation.h"
#include "PSProfileSaveGame.h"
#include "PSSaveSubsystem.h"
#include "PSSituationAI.h"
#include "PSSpecialTeamsData.h"
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

    FText DownOrdinal(int32 Down)
    {
        switch (Down)
        {
        case 1:  return UPSLocalization::GetText(TEXT("PlayCall.Down1"));
        case 2:  return UPSLocalization::GetText(TEXT("PlayCall.Down2"));
        case 3:  return UPSLocalization::GetText(TEXT("PlayCall.Down3"));
        default: return UPSLocalization::GetText(TEXT("PlayCall.Down4"));
        }
    }

    FText RoleAbbreviation(EPlayerRole Role)
    {
        switch (Role)
        {
        case EPlayerRole::Quarterback:      return UPSLocalization::GetText(TEXT("PlayCall.Role.Quarterback"));
        case EPlayerRole::RunningBack:      return UPSLocalization::GetText(TEXT("PlayCall.Role.RunningBack"));
        case EPlayerRole::WideReceiver:     return UPSLocalization::GetText(TEXT("PlayCall.Role.WideReceiver"));
        case EPlayerRole::TightEnd:         return UPSLocalization::GetText(TEXT("PlayCall.Role.TightEnd"));
        case EPlayerRole::OffensiveLineman: return UPSLocalization::GetText(TEXT("PlayCall.Role.OffensiveLineman"));
        case EPlayerRole::DefensiveLineman: return UPSLocalization::GetText(TEXT("PlayCall.Role.DefensiveLineman"));
        case EPlayerRole::Linebacker:       return UPSLocalization::GetText(TEXT("PlayCall.Role.Linebacker"));
        case EPlayerRole::DefensiveBack:    return UPSLocalization::GetText(TEXT("PlayCall.Role.DefensiveBack"));
        default:                            return UPSLocalization::Verbatim(TEXT("?"));
        }
    }

    FText KindLabel(EPSAssignmentKind Kind)
    {
        switch (Kind)
        {
        case EPSAssignmentKind::Route:        return UPSLocalization::GetText(TEXT("PlayCall.Kind.Route"));
        case EPSAssignmentKind::PassBlock:    return UPSLocalization::GetText(TEXT("PlayCall.Kind.PassBlock"));
        case EPSAssignmentKind::RunBlock:     return UPSLocalization::GetText(TEXT("PlayCall.Kind.RunBlock"));
        case EPSAssignmentKind::ManCoverage:  return UPSLocalization::GetText(TEXT("PlayCall.Kind.ManCoverage"));
        case EPSAssignmentKind::ZoneCoverage: return UPSLocalization::GetText(TEXT("PlayCall.Kind.ZoneCoverage"));
        case EPSAssignmentKind::PassRush:     return UPSLocalization::GetText(TEXT("PlayCall.Kind.PassRush"));
        case EPSAssignmentKind::RunFit:       return UPSLocalization::GetText(TEXT("PlayCall.Kind.RunFit"));
        case EPSAssignmentKind::Blitz:        return UPSLocalization::GetText(TEXT("PlayCall.Kind.Blitz"));
        default:                              return UPSLocalization::Verbatim(TEXT("?"));
        }
    }

    /** A play category's name (PlayCall.Category.<Category>), else its ID split into words. */
    FText CategoryName(const FString& Category)
    {
        const FString Key = FString::Printf(TEXT("PlayCall.Category.%s"), *Category);
        return UPSLocalization::HasText(Key) ? UPSLocalization::GetText(Key) : UPSLocalization::Verbatim(SplitCategory(Category));
    }

    /** Parts of a readout, joined by the separator dot. */
    FText JoinParts(const TArray<FText>& Parts)
    {
        return FText::Join(UPSLocalization::GetText(TEXT("PlayCall.Separator")), Parts);
    }

    /** A play listed with its formation (recent plays, favorites); both names are the
     *  playbook's own. */
    FString PlayWithFormation(const FPSPlayDefinition& Play)
    {
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Play"), UPSLocalization::Verbatim(Play.DisplayName));
        Arguments.Add(TEXT("Formation"), UPSLocalization::Verbatim(Play.Formation));
        return UPSLocalization::Format(TEXT("PlayCall.PlayWithFormation"), Arguments).ToString();
    }
}

void UPSPlayCallSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    CoachingAI = NewObject<UPSCoachingAI>(this);

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
    // A kickoff runs kickoff calls and returns; a scrimmage down everything else (Epic 75). The
    // team keeps its scheme's plays (Epic 89).
    const bool bKickoff = Situation.bKickoff;
    const TArray<FName>& Kept = GetCallingPlan(bOffense).PlayIds;
    return Plays.FilterByPredicate([bOffense, bKickoff, &Kept](const FPSPlayDefinition& Play)
    {
        return Play.bIsOffensivePlay == bOffense && PSSpecialTeams::IsCallableAt(PSSpecialTeams::FromCategory(Play.PlayCategory), bKickoff)
            && (Kept.Num() == 0 || Kept.Contains(Play.PlayId));
    });
}

const TArray<FPSPlayDefinition>& UPSPlayCallSubsystem::GetPlaybook()
{
    EnsurePlaybookLoaded();
    return Plays;
}

void UPSPlayCallSubsystem::SetTeamPlan(bool bHome, const FPSTeamPlan& Plan)
{
    (bHome ? HomePlan : AwayPlan) = Plan;
}

void UPSPlayCallSubsystem::ClearTeamPlans()
{
    HomePlan = FPSTeamPlan();
    AwayPlan = FPSTeamPlan();
}

const FPSTeamPlan& UPSPlayCallSubsystem::GetCallingPlan(bool bOffense) const
{
    return GetTeamPlan(Situation.bHomeHasPossession == bOffense);
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
        Option.Label = UPSLocalization::Verbatim(Formation).ToString();
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Count"), FText::AsNumber(GetPlaysInFormation(Formation, bOffense).Num()));
        Option.Detail = UPSLocalization::Format(TEXT("PlayCall.PlayCount"), Arguments).ToString();
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
        // Play, formation, front and coverage names are the playbook's own, not translated.
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Play"), UPSLocalization::Verbatim(Play.DisplayName));
        Arguments.Add(TEXT("Category"), PSPlayCallPrivate::CategoryName(Play.PlayCategory));
        Arguments.Add(TEXT("Front"), UPSLocalization::Verbatim(Play.Front));
        Arguments.Add(TEXT("Coverage"), UPSLocalization::Verbatim(Play.CoverageShell));
        Options.Add(MakePlayOption(Play, bOffense
            ? UPSLocalization::Format(TEXT("PlayCall.PlayWithCategory"), Arguments).ToString()
            : UPSLocalization::Format(TEXT("PlayCall.PlayWithDefense"), Arguments).ToString()));
    }
    return Options;
}

FPSMenuOptionDef UPSPlayCallSubsystem::MakePlayOption(const FPSPlayDefinition& Play, const FString& Label)
{
    FPSMenuOptionDef Option;
    Option.OptionId = Play.PlayId;
    // Starred plays are marked wherever they are listed.
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Play"), UPSLocalization::FromLocalized(Label));
    Option.Label = IsFavorite(Play.PlayId) ? UPSLocalization::Format(TEXT("PlayCall.Favorite"), Arguments).ToString() : Label;
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
    const FPSTeamPlan& Plan = GetCallingPlan(bOffense);
    return CoachingAI->RankPlays(Situation, bOffense ? Plan.OffenseTendency : Plan.DefenseTendency, GetPlays(bOffense), bOffense);
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
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Play"), UPSLocalization::Verbatim(Top.DisplayName));
    Option.Label = UPSLocalization::Format(TEXT("PlayCall.Suggested"), Arguments).ToString();
    // The coaching AI's reasons are its own English for now (shown verbatim).
    TArray<FText> Reasons;
    for (const FString& Reason : Top.Reasons)
    {
        Reasons.Add(UPSLocalization::Verbatim(Reason));
    }
    Option.Detail = (Reasons.Num() > 0 ? PSPlayCallPrivate::JoinParts(Reasons) : UPSLocalization::GetText(TEXT("PlayCall.NoLean"))).ToString();
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
        Options.Add(MakePlayOption(Play, PSPlayCallPrivate::PlayWithFormation(Play)));
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
            Options.Add(MakePlayOption(Play, PSPlayCallPrivate::PlayWithFormation(Play)));
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
    AsCalled.Label = UPSLocalization::GetText(TEXT("PlayCall.NoAdjustment")).ToString();
    AsCalled.Detail = UPSLocalization::GetText(TEXT("PlayCall.AsCalled")).ToString();
    AsCalled.Command = EPSMenuCommand::ApplyAdjustment;
    Options.Add(AsCalled);

    for (const FPSDefensiveAdjustmentDef& Adjustment : GetAdjustments())
    {
        FPSMenuOptionDef Option;
        Option.OptionId = Adjustment.AdjustmentId;
        Option.Label = UPSLocalization::GetDataText(UPSLocalization::AdjustmentKey(Adjustment.AdjustmentId, TEXT("Label")), Adjustment.Label).ToString();
        Option.Detail = UPSLocalization::GetDataText(UPSLocalization::AdjustmentKey(Adjustment.AdjustmentId, TEXT("Description")), Adjustment.Description).ToString();
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
    if (!Offense)
    {
        return UPSLocalization::GetText(TEXT("PlayCall.OffenseNotLinedUp")).ToString();
    }
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Formation"), UPSLocalization::Verbatim(Offense->Formation));
    return UPSLocalization::Format(TEXT("PlayCall.OffenseLinesUp"), Arguments).ToString();
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
    FText SituationLine = UPSLocalization::FromLocalized(DescribeSituation(Situation));
    FText TempoLine;

    // The leverage moment, and the offense's tempo (Epic 76).
    if (const UPSSituationAI* Read = CoachingAI ? CoachingAI->GetSituationAI() : nullptr)
    {
        const FString Moment = UPSSituationAI::DescribeSituation(Read->ClassifySituation(Situation));
        if (!Moment.IsEmpty())
        {
            // The situation AI's words are its own English for now (shown verbatim).
            FFormatNamedArguments Arguments;
            Arguments.Add(TEXT("Situation"), SituationLine);
            Arguments.Add(TEXT("Moment"), UPSLocalization::Verbatim(Moment));
            SituationLine = UPSLocalization::Format(TEXT("PlayCall.WithMoment"), Arguments);
        }
        const FPSTempoDef* TempoDef = bOffense ? Read->FindTempo(HumanTempo) : nullptr;
        if (TempoDef)
        {
            FFormatNamedArguments Arguments;
            Arguments.Add(TEXT("Tempo"), UPSLocalization::Verbatim(TempoDef->Label));
            TempoLine = UPSLocalization::Format(TEXT("PlayCall.Tempo"), Arguments);
        }
    }
    TArray<FText> Lines = { SituationLine };
    if (!TempoLine.IsEmpty())
    {
        Lines.Add(TempoLine);
    }

    // The team's scheme (Epic 89); its label is the coaching data's own English for now.
    const FPSTeamPlan& Plan = GetCallingPlan(bOffense);
    const FString& Scheme = bOffense ? Plan.OffenseTendency.Label : Plan.DefenseTendency.Label;
    if (!Scheme.IsEmpty())
    {
        FFormatNamedArguments SchemeArguments;
        SchemeArguments.Add(TEXT("Scheme"), UPSLocalization::Verbatim(Scheme));
        Lines.Add(UPSLocalization::Format(TEXT("PlayCall.Scheme"), SchemeArguments));
    }
    if (!Tendencies.IsEmpty())
    {
        Lines.Add(UPSLocalization::FromLocalized(Tendencies));
    }
    return UPSLocalization::JoinLines(Lines).ToString();
}

FString UPSPlayCallSubsystem::DescribeSituation(const FPSSituationContext& InSituation)
{
    // YardLine counts from the offense's own goal line (0) to the opponent's (100).
    FFormatNamedArguments Arguments;
    if (InSituation.bKickoff)
    {
        Arguments.Add(TEXT("Yard"), FText::AsNumber(InSituation.YardLine));
        return UPSLocalization::Format(TEXT("PlayCall.Kickoff"), Arguments).ToString();
    }
    FText Spot;
    if (InSituation.YardLine == 50)
    {
        Spot = UPSLocalization::GetText(TEXT("PlayCall.Midfield"));
    }
    else
    {
        FFormatNamedArguments Yard;
        Yard.Add(TEXT("Yard"), FText::AsNumber(InSituation.YardLine < 50 ? InSituation.YardLine : 100 - InSituation.YardLine));
        Spot = InSituation.YardLine < 50 ? UPSLocalization::Format(TEXT("PlayCall.Own"), Yard) : UPSLocalization::Format(TEXT("PlayCall.Opp"), Yard);
    }
    Arguments.Add(TEXT("Down"), PSPlayCallPrivate::DownOrdinal(InSituation.Down));
    Arguments.Add(TEXT("Distance"), FText::AsNumber(InSituation.Distance));
    Arguments.Add(TEXT("Spot"), Spot);
    return UPSLocalization::Format(TEXT("PlayCall.Situation"), Arguments).ToString();
}

TArray<FName> UPSPlayCallSubsystem::GetRecentCalls(bool bOffense, int32 MaxCount) const
{
    TArray<FName> Recent;
    for (int32 Index = CallHistory.Num() - 1; Index >= 0 && Recent.Num() < MaxCount; --Index)
    {
        if (IsOwnRecord(CallHistory[Index], bOffense))
        {
            Recent.AddUnique(CallHistory[Index].PlayId);
        }
    }
    return Recent;
}

bool UPSPlayCallSubsystem::IsOwnRecord(const FPSPlayCallRecord& Record, bool bOffense) const
{
    if (Record.bOffense != bOffense)
    {
        return false;
    }
    return !IsHeadToHead() || Record.bHomeTeam == IsHomeTeamCalling(bOffense);
}

FString UPSPlayCallSubsystem::DescribeTendencies(bool bOffense) const
{
    TMap<FString, int32> Counts;
    TArray<FString> Order;
    int32 Total = 0;
    for (const FPSPlayCallRecord& Record : CallHistory)
    {
        if (!IsOwnRecord(Record, bOffense))
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
    TArray<FText> Parts;
    for (const FString& Category : Order)
    {
        FFormatNamedArguments Share;
        Share.Add(TEXT("Category"), PSPlayCallPrivate::CategoryName(Category));
        Share.Add(TEXT("Percent"), UPSLocalization::FormatPercent(FMath::RoundToInt(100.f * Counts[Category] / Total) / 100.f));
        Parts.Add(UPSLocalization::Format(TEXT("PlayCall.CategoryShare"), Share));
    }
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Calls"), PSPlayCallPrivate::JoinParts(Parts));
    return UPSLocalization::Format(TEXT("PlayCall.YourCalls"), Arguments).ToString();
}

FString UPSPlayCallSubsystem::DescribePlay(const FPSPlayDefinition& Play)
{
    TArray<FText> Parts;
    for (const FPSPlayAssignment& Assignment : Play.Assignments)
    {
        // The quarterback's dropback is implied; a named route says what everyone runs.
        if (Assignment.Role == EPlayerRole::Quarterback && Assignment.RouteId.IsNone())
        {
            continue;
        }
        // Route names are the route library's own, not translated.
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Role"), PSPlayCallPrivate::RoleAbbreviation(Assignment.Role));
        Arguments.Add(TEXT("What"), (Assignment.Kind == EPSAssignmentKind::Route && !Assignment.RouteId.IsNone())
            ? UPSLocalization::Verbatim(Assignment.RouteId.ToString())
            : PSPlayCallPrivate::KindLabel(Assignment.Kind));
        Parts.Add(UPSLocalization::Format(TEXT("PlayCall.Assignment"), Arguments));
    }
    return PSPlayCallPrivate::JoinParts(Parts).ToString();
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
    Context.OpponentTimeoutsRemaining = State.bHomeHasPossession ? State.AwayTimeoutsRemaining : State.HomeTimeoutsRemaining;
    Context.bClockRunning = State.bIsClockRunning;
    Context.bKickoff = State.bKickoff;
    Context.bHomeHasPossession = State.bHomeHasPossession;
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

    // A CPU side that needs to stop a running clock calls its timeout first (Epic 76).
    for (const bool bOffense : { true, false })
    {
        if (!IsHumanSide(bOffense) && CoachingAI && CoachingAI->ShouldCallTimeout(Situation, bOffense))
        {
            RequestTimeout(bOffense, false);
        }
    }

    // A human offense in hurry-up runs its last play again, with no call screen.
    const bool bOffenseRerun = IsHumanSide(true) && RerunLastHumanCall();
    for (const bool bOffense : { true, false })
    {
        if (IsHumanSide(bOffense) && !(bOffense && bOffenseRerun))
        {
            OnHumanCallNeeded.Broadcast(bOffense);
        }
    }
}

bool UPSPlayCallSubsystem::RerunLastHumanCall()
{
    const UPSSituationAI* Read = CoachingAI ? CoachingAI->GetSituationAI() : nullptr;
    const FPSTempoDef* TempoDef = Read ? Read->FindTempo(HumanTempo) : nullptr;
    if (!TempoDef || !TempoDef->bRerunLastCall)
    {
        return false;
    }

    // The last real play: a spike or a kneel is never rerun.
    for (int32 Index = CallHistory.Num() - 1; Index >= 0; --Index)
    {
        const FPSPlayCallRecord& Record = CallHistory[Index];
        FPSPlayDefinition Play;
        if (IsOwnRecord(Record, true) && PSSituation::ClockPlayFromCategory(Record.PlayCategory) == EPSClockPlay::None && FindPlay(Record.PlayId, Play))
        {
            SetCall(Play, EPSPlayCaller::Human);
            return true;
        }
    }
    return false;
}

void UPSPlayCallSubsystem::SetHumanTempo(EPSTempo InTempo)
{
    HumanTempo = InTempo;
    FPSPlayDefinition Play;
    if (bWindowOpen && OffenseCall.IsSet() && OffenseCall.Caller != EPSPlayCaller::CPU && FindPlay(OffenseCall.PlayId, Play))
    {
        SetCall(Play, OffenseCall.Caller);
    }
}

EPSTempo UPSPlayCallSubsystem::CycleHumanTempo()
{
    const UPSSituationAI* Read = CoachingAI ? CoachingAI->GetSituationAI() : nullptr;
    SetHumanTempo(Read ? Read->GetNextHumanTempo(HumanTempo) : HumanTempo);
    return HumanTempo;
}

bool UPSPlayCallSubsystem::RequestTimeout(bool bOffense, bool bHumanCall)
{
    int32& Left = bOffense ? Situation.TimeoutsRemaining : Situation.OpponentTimeoutsRemaining;
    UPSTelemetryBus* Bus = BoundBus.Get();
    if (!bWindowOpen || Left <= 0 || !Bus)
    {
        return false;
    }
    --Left;
    Situation.bClockRunning = false;
    UE_LOG(LogTemp, Display, TEXT("UPSPlayCallSubsystem: %s timeout (%s), %d left."),
        bOffense ? TEXT("Offense") : TEXT("Defense"), bHumanCall ? TEXT("human") : TEXT("CPU"), Left);

    FPSTelemetryTimeoutEvent Event;
    Event.bOffense = bOffense;
    Event.bHumanCall = bHumanCall;
    Event.GameClockSeconds = Situation.GameClockSeconds;
    Bus->PublishTimeout(Event);
    return true;
}

bool UPSPlayCallSubsystem::CallPlay(FName PlayId, EPSPlayCaller Caller)
{
    FPSPlayDefinition Play;
    if (!bWindowOpen || Caller == EPSPlayCaller::None || !FindPlay(PlayId, Play))
    {
        return false;
    }
    const TArray<FName>& Kept = GetCallingPlan(Play.bIsOffensivePlay).PlayIds;
    if (Kept.Num() > 0 && !Kept.Contains(PlayId))
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

    // The offense's tempo: the CPU's from the situation, a human's own; a spike or a kneel
    // takes its own (Epic 76).
    const UPSSituationAI* Read = CoachingAI ? CoachingAI->GetSituationAI() : nullptr;
    Call.Tempo = EPSTempo::Huddle;
    if (Play.bIsOffensivePlay && Read)
    {
        Call.Tempo = Read->GetTempoForPlay(Play, Caller == EPSPlayCaller::CPU ? Read->ChooseTempo(Situation) : HumanTempo);
    }

    UE_LOG(LogTemp, Display, TEXT("UPSPlayCallSubsystem: %s calls %s (%s, %s)."),
        Play.bIsOffensivePlay ? TEXT("Offense") : TEXT("Defense"), *Play.DisplayName, *UEnum::GetValueAsString(Caller), *UEnum::GetValueAsString(Call.Tempo));

    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        FPSTelemetryPlayCallEvent Event;
        Event.PlayId = Play.PlayId;
        Event.DisplayName = Play.DisplayName;
        Event.Formation = Play.Formation;
        Event.PlayCategory = Play.PlayCategory;
        Event.bOffense = Play.bIsOffensivePlay;
        Event.bHumanCall = Caller == EPSPlayCaller::Human;
        Event.Tempo = Call.Tempo;
        if (Play.bIsOffensivePlay && Read)
        {
            Event.SnapAtPlayClockSeconds = Read->GetSnapPlayClock(Call.Tempo);
            Event.BoundaryIntent = Read->GetBoundaryIntent(Situation);
        }
        Bus->PublishPlayCall(Event);
    }

    // The defense sees the offense line up to kick (Epic 75): a CPU defense calls its return or
    // block against it, or its regular defense again when the offense comes out of the kick.
    const EPSSpecialTeamsPlay ShownKick = PSSpecialTeams::GetShownKick(PSSpecialTeams::FromCategory(Play.PlayCategory));
    if (Play.bIsOffensivePlay && ShownKick != Situation.OffenseKick)
    {
        Situation.OffenseKick = ShownKick;
        if (DefenseCall.Caller == EPSPlayCaller::CPU)
        {
            CallForCpu(false);
        }
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

    const FPSTeamPlan& Plan = GetCallingPlan(bOffense);
    FPSTendencyProfile Tendency = bOffense ? Plan.OffenseTendency : Plan.DefenseTendency;
    // Against a human, the CPU counters what it has seen him call (Epic 78).
    UPSOpponentModel* Opponent = GetWorld() ? GetWorld()->GetSubsystem<UPSOpponentModel>() : nullptr;
    if (Opponent && IsHumanSide(!bOffense))
    {
        Tendency = Opponent->CounterTendency(bOffense, Situation, Tendency);
    }
    const FName Chosen = bOffense
        ? CoachingAI->SelectOffensivePlay(Situation, Tendency, Candidates)
        : CoachingAI->SelectDefensivePlay(Situation, Tendency, Candidates);
    const FPSPlayDefinition* Play = Candidates.FindByPredicate([Chosen](const FPSPlayDefinition& Candidate) { return Candidate.PlayId == Chosen; });

    // The decision log (Epic 85): every play weighed, the pick and its reasons.
    UPSAIDecisionLog* DecisionLog = UPSAIDecisionLog::Get(GetWorld());
    if (DecisionLog && DecisionLog->IsLogging())
    {
        FPSAIDecisionRecord Decision;
        Decision.AgentId = bOffense ? TEXT("CPUOffense") : TEXT("CPUDefense");
        Decision.System = TEXT("PlayCall");
        Decision.Action = (Play ? Play->PlayId : Candidates[0].PlayId).ToString();
        for (const FPSPlaySuggestion& Suggestion : CoachingAI->RankPlays(Situation, Tendency, Candidates, bOffense))
        {
            FPSAIDecisionOption Option;
            Option.Option = Suggestion.PlayId.ToString();
            Option.Score = Suggestion.Weight;
            Option.Note = Suggestion.Category;
            Option.bChosen = Option.Option == Decision.Action;
            if (Option.bChosen)
            {
                Decision.Target = Suggestion.Category;
                Decision.Reason = Suggestion.Reasons.Num() > 0 ? FString::Join(Suggestion.Reasons, TEXT("; ")) : FString(TEXT("A weighted pick from the playbook"));
            }
            Decision.Options.Add(Option);
        }
        DecisionLog->Record(Decision);
    }
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
    if (!bWindowOpen || OffenseCall.Caller != EPSPlayCaller::Human || IsWaitingForHuman(false))
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
        Situation.bKickoff = false;
        Situation.OffenseKick = EPSSpecialTeamsPlay::None;
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
            Record.bHomeTeam = IsHomeTeamCalling(bOffense);
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
