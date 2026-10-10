#include "PSWeeklyPreparation.h"
#include "PSCoachingData.h"
#include "PSDataIngestion.h"
#include "PSEconomyData.h"
#include "PSFranchiseSaveGame.h"
#include "PSInjuryModel.h"
#include "PSOpponentModel.h"
#include "PSOwnerEconomy.h"
#include "PSRoster.h"
#include "PSStaffData.h"
#include "PSStaffManager.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"

namespace PSWeeklyPreparationPrivate
{
    /** Calls Visit(Rating, Weight) for each of Player's skill ratings, with its weight in Weights. */
    template <typename FunctorType>
    void ForEachRating(FPlayerAttributes& Player, const FPSRatingWeights& Weights, FunctorType&& Visit)
    {
        Visit(Player.Speed, Weights.Speed);
        Visit(Player.Agility, Weights.Agility);
        Visit(Player.Strength, Weights.Strength);
        Visit(Player.Acceleration, Weights.Acceleration);
        Visit(Player.Awareness, Weights.Awareness);
    }

    bool HasNegativeWeight(const FPSRatingWeights& Weights)
    {
        return Weights.Speed < 0.f || Weights.Agility < 0.f || Weights.Strength < 0.f || Weights.Acceleration < 0.f || Weights.Awareness < 0.f;
    }

    float SumWeights(const FPSRatingWeights& Weights)
    {
        return Weights.Speed + Weights.Agility + Weights.Strength + Weights.Acceleration + Weights.Awareness;
    }

    /** Into += From x Scale, rating by rating. */
    void AddWeighted(FPSRatingWeights& Into, const FPSRatingWeights& From, float Scale)
    {
        Into.Speed += From.Speed * Scale;
        Into.Agility += From.Agility * Scale;
        Into.Strength += From.Strength * Scale;
        Into.Acceleration += From.Acceleration * Scale;
        Into.Awareness += From.Awareness * Scale;
    }

    FString Percent(float Fraction)
    {
        return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Fraction * 100.f));
    }

    /** A seed for a team's PracticeWeek-th week of practice that is the same in every run. */
    int32 PracticeSeed(int32 BaseSeed, FName TeamId, int32 PracticeWeek)
    {
        return static_cast<int32>(FCrc::StrCrc32(*FString::Printf(TEXT("%d:%s:%d"), BaseSeed, *TeamId.ToString(), PracticeWeek)));
    }

    FPSTrainingEvent MakeEvent(EPSTrainingEventKind Kind, FName PlayerId, FName TeamId, int32 Week, int32 Weeks, const FString& Description)
    {
        FPSTrainingEvent Event;
        Event.Kind = Kind;
        Event.PlayerId = PlayerId;
        Event.TeamId = TeamId;
        Event.Week = Week;
        Event.Weeks = Weeks;
        Event.Description = Description;
        return Event;
    }

    bool IsFraction(float Value)
    {
        return Value >= 0.f && Value <= 1.f;
    }
}

FString UPSWeeklyPreparation::GetDefaultTuningPath()
{
    return FPaths::ProjectDir() / TEXT("Data/training.json");
}

bool UPSWeeklyPreparation::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSTrainingTuning Loaded;
    if (!Ingestion->LoadTrainingTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSWeeklyPreparation: Could not load the training tuning from %s."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded, OpponentTuning.Counters.Num() > 0 ? &OpponentTuning : nullptr))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSWeeklyPreparation: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

bool UPSWeeklyPreparation::LoadDefaults()
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSOpponentModelTuning Loaded;
    const bool bOpponentModel = Ingestion->LoadOpponentModelTuningFromJson(UPSOpponentModel::GetDefaultTuningPath(), Loaded);
    if (bOpponentModel)
    {
        OpponentTuning = Loaded;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSWeeklyPreparation: Could not load the opponent model's tuning; scouting reads schemes only."));
    }
    return LoadTuningFromJson(GetDefaultTuningPath()) && bOpponentModel;
}

TArray<FString> UPSWeeklyPreparation::ValidateTuning(const FPSTrainingTuning& InTuning, const FPSOpponentModelTuning* InOpponentTuning)
{
    using namespace PSWeeklyPreparationPrivate;

    TArray<FString> Problems;
    const FPSTrainingAllocation& Default = InTuning.DefaultAllocation;
    if (Default.Develop < 0.f || Default.Gameplan < 0.f || Default.Rest < 0.f || Default.GetTotal() <= 0.f)
    {
        Problems.Add(TEXT("DefaultAllocation: shares must be 0 or more, and not all 0"));
    }
    for (const float Value : { InTuning.DevelopIntensity, InTuning.GameplanIntensity, InTuning.DevelopPointsPerWeek, InTuning.MinCoachDevelopment,
        InTuning.GameplanBonusPerShare, InTuning.MaxRelevance, InTuning.UnscoutedRelevance })
    {
        if (Value < 0.f)
        {
            Problems.Add(TEXT("DevelopIntensity, GameplanIntensity, DevelopPointsPerWeek, MinCoachDevelopment, GameplanBonusPerShare, MaxRelevance and UnscoutedRelevance must be 0 or more"));
            break;
        }
    }
    if (HasNegativeWeight(InTuning.DevelopRatings))
    {
        Problems.Add(TEXT("DevelopRatings: weights must be 0 or more"));
    }
    if (InTuning.DevelopHeadroom <= 0.f || InTuning.DevelopHeadroom > 100.f)
    {
        Problems.Add(TEXT("DevelopHeadroom must be above 0 and at most 100"));
    }
    if (InTuning.MaxCoachDevelopment < InTuning.MinCoachDevelopment)
    {
        Problems.Add(TEXT("MaxCoachDevelopment must not be below MinCoachDevelopment"));
    }
    if (!IsFraction(InTuning.FundingFloor) || InTuning.MaxFundingMultiplier < 1.f)
    {
        Problems.Add(TEXT("FundingFloor must be 0-1 and MaxFundingMultiplier at least 1"));
    }
    if (InTuning.MaxGameplanBonus < 0.f || InTuning.MaxGameplanBonus >= 1.f)
    {
        Problems.Add(TEXT("MaxGameplanBonus must be in [0, 1)"));
    }
    if (InTuning.UnscoutedRelevance > InTuning.MaxRelevance)
    {
        Problems.Add(TEXT("UnscoutedRelevance must not exceed MaxRelevance"));
    }
    if (InTuning.MaxFocusAreas < 1 || InTuning.AIFocusAreas < 0 || InTuning.AIFocusAreas > InTuning.MaxFocusAreas)
    {
        Problems.Add(TEXT("MaxFocusAreas must be at least 1, and AIFocusAreas 0 to MaxFocusAreas"));
    }
    for (const float Fraction : { InTuning.GameFatigue, InTuning.PracticeFatigue, InTuning.WeeklyRecovery, InTuning.RestRecovery, InTuning.StaminaFatigueRelief,
        InTuning.AIRestFreshness, InTuning.AIRestShift, InTuning.AILateSeasonProgress, InTuning.AILateGameplanShift })
    {
        if (!IsFraction(Fraction))
        {
            Problems.Add(TEXT("GameFatigue, PracticeFatigue, WeeklyRecovery, RestRecovery, StaminaFatigueRelief and the AI's freshness, shifts and season progress are 0-1"));
            break;
        }
    }
    if (InTuning.FatiguePerformanceSwing < 0.f || InTuning.FatiguePerformanceSwing >= 1.f)
    {
        Problems.Add(TEXT("FatiguePerformanceSwing must be in [0, 1)"));
    }
    const FPSInjuryTuning& Injury = InTuning.PracticeInjury;
    if (!IsFraction(Injury.BaseInjuryChance) || Injury.MaxFatigueMultiplier < 1.f || Injury.MinRecoveryWeeks < 1 || Injury.MaxRecoveryWeeks < Injury.MinRecoveryWeeks)
    {
        Problems.Add(TEXT("PracticeInjury: BaseInjuryChance 0-1, MaxFatigueMultiplier at least 1, MinRecoveryWeeks at least 1 and MaxRecoveryWeeks not below it"));
    }

    TSet<FName> Seen;
    for (const FPSGameplanFocusDef& Focus : InTuning.FocusAreas)
    {
        const FString Name = Focus.FocusId.ToString();
        if (Focus.FocusId.IsNone() || Seen.Contains(Focus.FocusId))
        {
            Problems.Add(FString::Printf(TEXT("FocusAreas: empty or duplicate focus '%s'"), *Name));
        }
        Seen.Add(Focus.FocusId);
        TSet<FString> Categories;
        for (const FString& Category : Focus.Categories)
        {
            if (Category.IsEmpty() || Categories.Contains(Category))
            {
                Problems.Add(FString::Printf(TEXT("FocusAreas: %s has an empty or repeated category '%s'"), *Name, *Category));
            }
            else if (InOpponentTuning && !PSOpponentModel::IsTracked(*InOpponentTuning, Focus.bVersusOffense, Category))
            {
                Problems.Add(FString::Printf(TEXT("FocusAreas: %s's category %s isn't one the opponent model tracks on the %s"), *Name, *Category,
                    Focus.bVersusOffense ? TEXT("offense") : TEXT("defense")));
            }
            Categories.Add(Category);
        }
        if (Focus.Categories.Num() == 0 || Focus.Roles.Num() == 0)
        {
            Problems.Add(FString::Printf(TEXT("FocusAreas: %s needs categories and roles"), *Name));
        }
        if (HasNegativeWeight(Focus.Ratings) || SumWeights(Focus.Ratings) <= 0.f)
        {
            Problems.Add(FString::Printf(TEXT("FocusAreas: %s's ratings must be 0 or more, and not all 0"), *Name));
        }
    }
    return Problems;
}

bool UPSWeeklyPreparation::SetAllocation(FName TeamId, const FPSTrainingAllocation& Allocation)
{
    if (TeamId.IsNone() || Allocation.Develop < 0.f || Allocation.Gameplan < 0.f || Allocation.Rest < 0.f || Allocation.GetTotal() <= 0.f)
    {
        return false;
    }
    FPSTeamTraining& Team = FindOrAddTeam(TeamId);
    Team.ChosenAllocation = Allocation.Normalized();
    Team.bAllocationChosen = true;
    return true;
}

void UPSWeeklyPreparation::ClearAllocation(FName TeamId)
{
    if (!TeamId.IsNone())
    {
        FindOrAddTeam(TeamId).bAllocationChosen = false;
    }
}

bool UPSWeeklyPreparation::SetGameplanFocus(FName TeamId, const TArray<FName>& FocusIds)
{
    if (TeamId.IsNone() || FocusIds.Num() > Tuning.MaxFocusAreas)
    {
        return false;
    }
    TSet<FName> Seen;
    for (const FName& FocusId : FocusIds)
    {
        if (!Tuning.FindFocus(FocusId) || Seen.Contains(FocusId))
        {
            return false;
        }
        Seen.Add(FocusId);
    }
    FindOrAddTeam(TeamId).ChosenFocus = FocusIds;
    return true;
}

void UPSWeeklyPreparation::SetObservedCalls(FName TeamId, const TArray<FPSTendencyCell>& Cells)
{
    if (Cells.Num() > 0)
    {
        ObservedCalls.Add(TeamId, Cells);
    }
    else
    {
        ObservedCalls.Remove(TeamId);
    }
}

FPSTrainingAllocation UPSWeeklyPreparation::RecommendAllocation(const UPSRoster* Roster, float SeasonProgress) const
{
    FPSTrainingAllocation Result = Tuning.DefaultAllocation.Normalized();

    // Tired: rest more.
    float FreshnessSum = 0.f;
    int32 Healthy = 0;
    if (Roster)
    {
        for (const FPlayerAttributes& Player : Roster->GetFullRoster())
        {
            const FPSPlayerCondition* Condition = FindPlayer(Player.PlayerId);
            if (!Condition || Condition->InjuryWeeks == 0)
            {
                FreshnessSum += Condition ? Condition->Freshness : 1.f;
                ++Healthy;
            }
        }
    }
    if (Healthy > 0 && FreshnessSum / Healthy < Tuning.AIRestFreshness)
    {
        const float Shift = FMath::Min(Tuning.AIRestShift, Result.Develop);
        Result.Develop -= Shift;
        Result.Rest += Shift;
    }

    // Late in the season: this week's game over next year's ratings.
    if (SeasonProgress >= Tuning.AILateSeasonProgress)
    {
        const float Shift = FMath::Min(Tuning.AILateGameplanShift, Result.Develop);
        Result.Develop -= Shift;
        Result.Gameplan += Shift;
    }
    return Result;
}

TArray<FString> UPSWeeklyPreparation::GetSideCategories(bool bVersusOffense) const
{
    TArray<FString> Categories;
    for (const FPSGameplanFocusDef& Focus : Tuning.FocusAreas)
    {
        if (Focus.bVersusOffense == bVersusOffense)
        {
            for (const FString& Category : Focus.Categories)
            {
                Categories.AddUnique(Category);
            }
        }
    }
    return Categories;
}

FPSTendencyRead UPSWeeklyPreparation::ReadScheme(FName TeamId, bool bOffense, const UPSStaffManager* Staffs) const
{
    FPSTendencyRead Read;
    if (!Staffs)
    {
        return Read;
    }
    const FPSTendencyProfile Tendency = Staffs->BuildTendency(TeamId, bOffense);
    const TArray<FString> Categories = GetSideCategories(bOffense);
    if (Tendency.Label.IsEmpty() || Categories.Num() == 0)
    {
        // No coordinator, or one without a scheme: nothing to read.
        return Read;
    }

    // A category the scheme doesn't weigh is called at the neutral weight, 1.
    float Total = 0.f;
    for (const FString& Category : Categories)
    {
        const float* Weight = Tendency.CategoryWeights.Find(Category);
        Total += Weight ? FMath::Max(*Weight, 0.f) : 1.f;
    }
    if (Total <= 0.f)
    {
        return Read;
    }
    for (const FString& Category : Categories)
    {
        const float* Weight = Tendency.CategoryWeights.Find(Category);
        const float Share = (Weight ? FMath::Max(*Weight, 0.f) : 1.f) / Total;
        Read.Shares.Add(Category, Share);
        if (Share > Read.TopShare)
        {
            Read.TopShare = Share;
            Read.TopCategory = Category;
        }
    }
    Read.Basis = EPSTendencyBasis::Overall;
    return Read;
}

FPSOpponentScoutingReport UPSWeeklyPreparation::BuildScoutingReport(FName OpponentId, const UPSStaffManager* Staffs) const
{
    FPSOpponentScoutingReport Report;
    Report.TeamId = OpponentId;
    const TArray<FPSTendencyCell>* Seen = ObservedCalls.Find(OpponentId);
    for (const bool bOffense : { true, false })
    {
        // What the opponent model has seen him call, every situation together (no down matches
        // 0), once there are enough calls for a read; else his coordinator's scheme.
        FPSTendencyRead Read;
        if (Seen)
        {
            Read = PSOpponentModel::ReadTendency(TArray<FPSTendencyCell>(), *Seen, bOffense, 0, 0, NAME_None, OpponentTuning);
        }
        const bool bObserved = Read.Basis != EPSTendencyBasis::None;
        if (!bObserved)
        {
            Read = ReadScheme(OpponentId, bOffense, Staffs);
        }
        (bOffense ? Report.Offense : Report.Defense) = Read;
        (bOffense ? Report.bOffenseObserved : Report.bDefenseObserved) = bObserved;
    }
    return Report;
}

float UPSWeeklyPreparation::GetFocusRelevance(const FPSOpponentScoutingReport& Report, FName FocusId) const
{
    const FPSGameplanFocusDef* Focus = Tuning.FindFocus(FocusId);
    if (!Focus || Focus->Categories.Num() == 0)
    {
        return 0.f;
    }
    const FPSTendencyRead& Read = Focus->bVersusOffense ? Report.Offense : Report.Defense;
    const TArray<FString> Side = GetSideCategories(Focus->bVersusOffense);
    float SideTotal = 0.f;
    for (const FString& Category : Side)
    {
        SideTotal += Read.Shares.FindRef(Category);
    }
    if (Read.Basis == EPSTendencyBasis::None || SideTotal <= 0.f)
    {
        return FMath::Min(Tuning.UnscoutedRelevance, Tuning.MaxRelevance);
    }

    // The share of his calls the focus area prepares for, against its share of an even mix.
    float FocusTotal = 0.f;
    for (const FString& Category : Focus->Categories)
    {
        FocusTotal += Read.Shares.FindRef(Category);
    }
    const float EvenShare = static_cast<float>(Focus->Categories.Num()) / Side.Num();
    return FMath::Clamp(FocusTotal / SideTotal / EvenShare, 0.f, Tuning.MaxRelevance);
}

TArray<FName> UPSWeeklyPreparation::RecommendFocus(const FPSOpponentScoutingReport& Report) const
{
    TArray<TPair<FName, float>> Ranked;
    for (const FPSGameplanFocusDef& Focus : Tuning.FocusAreas)
    {
        const float Relevance = GetFocusRelevance(Report, Focus.FocusId);
        if (Relevance > 0.f)
        {
            Ranked.Emplace(Focus.FocusId, Relevance);
        }
    }
    // Most relevant first; data order breaks ties.
    Ranked.StableSort([](const TPair<FName, float>& A, const TPair<FName, float>& B) { return A.Value > B.Value; });

    TArray<FName> Picked;
    const int32 Count = FMath::Min(Tuning.AIFocusAreas, Tuning.MaxFocusAreas);
    for (int32 Index = 0; Index < Ranked.Num() && Picked.Num() < Count; ++Index)
    {
        Picked.Add(Ranked[Index].Key);
    }
    return Picked;
}

float UPSWeeklyPreparation::GetFundingMultiplier(float FundingIndex) const
{
    return FMath::Min(Tuning.FundingFloor + (1.f - Tuning.FundingFloor) * FMath::Max(FundingIndex, 0.f), Tuning.MaxFundingMultiplier);
}

float UPSWeeklyPreparation::GetFatigueFactor(const FPlayerAttributes& Player) const
{
    return 1.f - Tuning.StaminaFatigueRelief * FMath::Clamp(Player.Stamina / 100.f, 0.f, 1.f);
}

float UPSWeeklyPreparation::GetCoachMultiplier(FName TeamId, const FPlayerAttributes& Player, const UPSStaffManager* Staffs) const
{
    const FPSCoachDef* Coordinator = Staffs ? Staffs->FindTeamCoach(TeamId, PSStaff::CoordinatorRole(PSStaff::IsOffensiveRole(Player.Role))) : nullptr;
    return Coordinator ? FMath::Lerp(Tuning.MinCoachDevelopment, Tuning.MaxCoachDevelopment, FMath::Clamp(Coordinator->Development / 100.f, 0.f, 1.f)) : 1.f;
}

TArray<FPSTrainingEvent> UPSWeeklyPreparation::PrepareTeam(FName TeamId, UPSRoster* Roster, int32 Week, FName OpponentId, float SeasonProgress,
    const UPSOwnerEconomy* Economy, const UPSStaffManager* Staffs)
{
    using namespace PSWeeklyPreparationPrivate;

    TArray<FPSTrainingEvent> Events;
    if (TeamId.IsNone() || !Roster || Week <= 0)
    {
        return Events;
    }
    const FPSTeamTraining* Prepared = FindTeam(TeamId);
    if (Prepared && Prepared->PreparedWeek == Week)
    {
        return Events;
    }

    FPSTeamTraining& Team = FindOrAddTeam(TeamId);
    const FPSTrainingAllocation Allocation = Team.bAllocationChosen ? Team.ChosenAllocation.Normalized() : RecommendAllocation(Roster, SeasonProgress);
    const float FundingIndex = Economy ? Economy->GetFundingIndex(TeamId, EPSBudgetDepartment::Training) : 1.f;
    const float Funding = GetFundingMultiplier(FundingIndex);
    const float Intensity = Allocation.Develop * Tuning.DevelopIntensity + Allocation.Gameplan * Tuning.GameplanIntensity;
    const float Recovery = Tuning.WeeklyRecovery + Allocation.Rest * Tuning.RestRecovery;
    const float Headroom = FMath::Max(Tuning.DevelopHeadroom, KINDA_SMALL_NUMBER);

    // Practice injuries through Core 19's injury model: a harder week, a likelier injury.
    FPSInjuryTuning Risk = Tuning.PracticeInjury;
    Risk.BaseInjuryChance *= Intensity;
    if (!InjuryModel)
    {
        InjuryModel = NewObject<UPSInjuryModel>(this);
    }
    InjuryModel->SeedDeterminism(PracticeSeed(Tuning.RandomSeed, TeamId, Team.PracticeWeeks));

    float DevelopmentPoints = 0.f;
    int32 Injuries = 0;
    for (FPlayerAttributes& Player : Roster->GetMutableFullRoster())
    {
        if (Player.PlayerId.IsNone())
        {
            continue;
        }
        FPSPlayerCondition& Condition = FindOrAddPlayer(Player.PlayerId, TeamId);
        Condition.TeamId = TeamId;
        Condition.Freshness = FMath::Min(Condition.Freshness + Recovery, 1.f);

        // The injured rehab instead of practising, a week closer to playing.
        if (Condition.InjuryWeeks > 0)
        {
            if (--Condition.InjuryWeeks == 0)
            {
                Events.Add(MakeEvent(EPSTrainingEventKind::Recovered, Player.PlayerId, TeamId, Week, 0,
                    FString::Printf(TEXT("%s is back from his injury"), *Player.PlayerId.ToString())));
            }
            continue;
        }

        const FPSInjuryResult Injury = InjuryModel->RollForInjury(Condition.Freshness, Risk);
        if (Injury.bInjured)
        {
            Condition.InjuryWeeks = FMath::Max(Injury.RecoveryWeeks, 1);
            ++Injuries;
            Events.Add(MakeEvent(EPSTrainingEventKind::Injury, Player.PlayerId, TeamId, Week, Condition.InjuryWeeks,
                FString::Printf(TEXT("%s was hurt in practice: out %d week(s)"), *Player.PlayerId.ToString(), Condition.InjuryWeeks)));
            continue;
        }
        Condition.Freshness = FMath::Max(Condition.Freshness - Intensity * Tuning.PracticeFatigue * GetFatigueFactor(Player), 0.f);

        // Development: the roster's own ratings rise, less the nearer 100 they are.
        const float Points = Tuning.DevelopPointsPerWeek * Allocation.Develop * Funding * GetCoachMultiplier(TeamId, Player, Staffs);
        float Gained = 0.f;
        ForEachRating(Player, Tuning.DevelopRatings, [Points, Headroom, &Gained](float& Rating, float Weight)
        {
            const float Gain = Points * Weight * FMath::Clamp((100.f - Rating) / Headroom, 0.f, 1.f);
            Rating += Gain;
            Gained += Gain;
        });
        Condition.DevelopmentGained += Gained;
        DevelopmentPoints += Gained;
    }

    // The gameplan for this week's opponent, from his scouting report.
    Team.Gameplan = FPSTeamGameplan();
    Team.Gameplan.Week = Week;
    Team.Gameplan.OpponentId = OpponentId;
    if (!OpponentId.IsNone())
    {
        const FPSOpponentScoutingReport Report = BuildScoutingReport(OpponentId, Staffs);
        const TArray<FName> FocusIds = Team.ChosenFocus.Num() > 0 ? Team.ChosenFocus : RecommendFocus(Report);
        const float TotalBonus = Tuning.GameplanBonusPerShare * Allocation.Gameplan * Funding;
        for (const FName& FocusId : FocusIds)
        {
            FPSGameplanFocus& Focus = Team.Gameplan.Focus.AddDefaulted_GetRef();
            Focus.FocusId = FocusId;
            Focus.Relevance = GetFocusRelevance(Report, FocusId);
            Focus.Bonus = FMath::Min(TotalBonus / FocusIds.Num() * Focus.Relevance, Tuning.MaxGameplanBonus);
        }
    }
    Team.ChosenFocus.Reset();

    Team.PreparedWeek = Week;
    ++Team.PracticeWeeks;
    Team.Allocation = Allocation;
    Team.bRecommendedAllocation = !Team.bAllocationChosen;
    Team.FundingIndex = FundingIndex;
    Team.Intensity = Intensity;
    Team.DevelopmentPoints = DevelopmentPoints;
    Team.Injuries = Injuries;
    return Events;
}

void UPSWeeklyPreparation::RecordGame(FName TeamId, const TArray<FPlayerAttributes>& Played)
{
    for (const FPlayerAttributes& Player : Played)
    {
        if (!Player.PlayerId.IsNone())
        {
            FPSPlayerCondition& Condition = FindOrAddPlayer(Player.PlayerId, TeamId);
            Condition.Freshness = FMath::Max(Condition.Freshness - Tuning.GameFatigue * GetFatigueFactor(Player), 0.f);
        }
    }
}

FPlayerAttributes UPSWeeklyPreparation::ApplyPreparation(FName TeamId, FName OpponentId, const FPlayerAttributes& Player) const
{
    using namespace PSWeeklyPreparationPrivate;

    const float Freshness = GetFreshness(Player.PlayerId);
    const float FatigueMultiplier = 1.f - Tuning.FatiguePerformanceSwing * (1.f - FMath::Clamp(Freshness, 0.f, 1.f));

    // The gameplan lifts its focus areas' players, against the opponent it was made for only.
    FPSRatingWeights Lift;
    const FPSTeamTraining* Team = FindTeam(TeamId);
    if (Team && !OpponentId.IsNone() && Team->Gameplan.OpponentId == OpponentId)
    {
        for (const FPSGameplanFocus& Focus : Team->Gameplan.Focus)
        {
            const FPSGameplanFocusDef* Def = Tuning.FindFocus(Focus.FocusId);
            if (Def && Def->Roles.Contains(Player.Role))
            {
                AddWeighted(Lift, Def->Ratings, Focus.Bonus);
            }
        }
    }

    FPlayerAttributes Prepared = Player;
    ForEachRating(Prepared, Lift, [FatigueMultiplier](float& Rating, float Bonus)
    {
        Rating *= FatigueMultiplier * (1.f + Bonus);
    });
    return Prepared;
}

void UPSWeeklyPreparation::EndSeason()
{
    for (FPSPlayerCondition& Condition : State.Players)
    {
        Condition.Freshness = 1.f;
        Condition.InjuryWeeks = 0;
        Condition.DevelopmentGained = 0.f;
    }
    for (FPSTeamTraining& Team : State.Teams)
    {
        Team.PreparedWeek = 0;
        Team.ChosenFocus.Reset();
        Team.Gameplan = FPSTeamGameplan();
        Team.DevelopmentPoints = 0.f;
        Team.Injuries = 0;
    }
}

bool UPSWeeklyPreparation::IsInjured(FName PlayerId) const
{
    const FPSPlayerCondition* Condition = FindPlayer(PlayerId);
    return Condition && Condition->InjuryWeeks > 0;
}

float UPSWeeklyPreparation::GetFreshness(FName PlayerId) const
{
    const FPSPlayerCondition* Condition = FindPlayer(PlayerId);
    return Condition ? Condition->Freshness : 1.f;
}

bool UPSWeeklyPreparation::GetCondition(FName PlayerId, FPSPlayerCondition& OutCondition) const
{
    const FPSPlayerCondition* Condition = FindPlayer(PlayerId);
    if (Condition)
    {
        OutCondition = *Condition;
    }
    return Condition != nullptr;
}

bool UPSWeeklyPreparation::GetTeamTraining(FName TeamId, FPSTeamTraining& OutTeam) const
{
    const FPSTeamTraining* Team = FindTeam(TeamId);
    if (Team)
    {
        OutTeam = *Team;
    }
    return Team != nullptr;
}

TArray<FString> UPSWeeklyPreparation::DescribeTeamWeek(FName TeamId) const
{
    using namespace PSWeeklyPreparationPrivate;

    TArray<FString> Lines;
    const FPSTeamTraining* Team = FindTeam(TeamId);
    if (!Team || Team->PreparedWeek <= 0)
    {
        Lines.Add(FString::Printf(TEXT("%s: no practice week yet"), *TeamId.ToString()));
        return Lines;
    }

    Lines.Add(FString::Printf(TEXT("Week %d practice: develop %s, gameplan %s, rest %s (%s)"), Team->PreparedWeek, *Percent(Team->Allocation.Develop),
        *Percent(Team->Allocation.Gameplan), *Percent(Team->Allocation.Rest), Team->bRecommendedAllocation ? TEXT("recommended") : TEXT("chosen")));
    Lines.Add(FString::Printf(TEXT("Training funding: %.2fx the league's average (development and gameplan x%.2f)"), Team->FundingIndex,
        GetFundingMultiplier(Team->FundingIndex)));
    if (Team->Gameplan.OpponentId.IsNone())
    {
        Lines.Add(TEXT("No gameplan: a bye week"));
    }
    else if (Team->Gameplan.Focus.Num() == 0)
    {
        Lines.Add(FString::Printf(TEXT("Gameplan vs %s: no focus"), *Team->Gameplan.OpponentId.ToString()));
    }
    for (const FPSGameplanFocus& Focus : Team->Gameplan.Focus)
    {
        const FPSGameplanFocusDef* Def = Tuning.FindFocus(Focus.FocusId);
        Lines.Add(FString::Printf(TEXT("Gameplan vs %s: %s (relevance %.2f): +%.1f%%"), *Team->Gameplan.OpponentId.ToString(),
            Def && !Def->Label.IsEmpty() ? *Def->Label : *Focus.FocusId.ToString(), Focus.Relevance, Focus.Bonus * 100.f));
    }
    Lines.Add(FString::Printf(TEXT("Practice: intensity %.2f, %d injur%s, %.1f rating points developed"), Team->Intensity, Team->Injuries,
        Team->Injuries == 1 ? TEXT("y") : TEXT("ies"), Team->DevelopmentPoints));

    float FreshnessSum = 0.f;
    int32 Healthy = 0;
    for (const FPSPlayerCondition& Condition : State.Players)
    {
        if (Condition.TeamId != TeamId)
        {
            continue;
        }
        if (Condition.InjuryWeeks > 0)
        {
            Lines.Add(FString::Printf(TEXT("Injured: %s (%d week(s))"), *Condition.PlayerId.ToString(), Condition.InjuryWeeks));
        }
        else
        {
            FreshnessSum += Condition.Freshness;
            ++Healthy;
        }
    }
    Lines.Add(FString::Printf(TEXT("Average freshness: %.2f"), Healthy > 0 ? FreshnessSum / Healthy : 1.f));
    return Lines;
}

void UPSWeeklyPreparation::SaveTo(UPSFranchiseSaveGame* Save) const
{
    if (Save)
    {
        Save->Training = State;
    }
}

bool UPSWeeklyPreparation::LoadFrom(const UPSFranchiseSaveGame* Save)
{
    if (!Save || (Save->Training.Players.Num() == 0 && Save->Training.Teams.Num() == 0))
    {
        return false;
    }
    State = Save->Training;
    return true;
}

FPSPlayerCondition& UPSWeeklyPreparation::FindOrAddPlayer(FName PlayerId, FName TeamId)
{
    FPSPlayerCondition* Found = State.Players.FindByPredicate([PlayerId](const FPSPlayerCondition& Condition) { return Condition.PlayerId == PlayerId; });
    if (Found)
    {
        return *Found;
    }
    FPSPlayerCondition& Added = State.Players.AddDefaulted_GetRef();
    Added.PlayerId = PlayerId;
    Added.TeamId = TeamId;
    return Added;
}

const FPSPlayerCondition* UPSWeeklyPreparation::FindPlayer(FName PlayerId) const
{
    return State.Players.FindByPredicate([PlayerId](const FPSPlayerCondition& Condition) { return Condition.PlayerId == PlayerId; });
}

FPSTeamTraining& UPSWeeklyPreparation::FindOrAddTeam(FName TeamId)
{
    FPSTeamTraining* Found = State.Teams.FindByPredicate([TeamId](const FPSTeamTraining& Team) { return Team.TeamId == TeamId; });
    if (Found)
    {
        return *Found;
    }
    FPSTeamTraining& Added = State.Teams.AddDefaulted_GetRef();
    Added.TeamId = TeamId;
    Added.Allocation = Tuning.DefaultAllocation.Normalized();
    return Added;
}

const FPSTeamTraining* UPSWeeklyPreparation::FindTeam(FName TeamId) const
{
    return State.Teams.FindByPredicate([TeamId](const FPSTeamTraining& Team) { return Team.TeamId == TeamId; });
}
