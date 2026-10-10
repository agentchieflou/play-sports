#include "PSStaffManager.h"
#include "PSDataIngestion.h"
#include "PSFranchiseSaveGame.h"
#include "PSPlayCallSubsystem.h"
#include "PSSituationData.h"
#include "PSSpecialTeamsData.h"
#include "Misc/Paths.h"

namespace PSStaff
{
    bool IsOffensiveRole(EPlayerRole Role)
    {
        switch (Role)
        {
        case EPlayerRole::Quarterback:
        case EPlayerRole::RunningBack:
        case EPlayerRole::WideReceiver:
        case EPlayerRole::TightEnd:
        case EPlayerRole::OffensiveLineman:
            return true;
        default:
            return false;
        }
    }

    EPSCoachRole CoordinatorRole(bool bOffense)
    {
        return bOffense ? EPSCoachRole::OffensiveCoordinator : EPSCoachRole::DefensiveCoordinator;
    }

    bool GetAttribute(const FPlayerAttributes& Player, FName Attribute, float& OutValue)
    {
        if (Attribute == FName(TEXT("Speed")))
        {
            OutValue = Player.Speed;
        }
        else if (Attribute == FName(TEXT("Agility")))
        {
            OutValue = Player.Agility;
        }
        else if (Attribute == FName(TEXT("Strength")))
        {
            OutValue = Player.Strength;
        }
        else if (Attribute == FName(TEXT("Acceleration")))
        {
            OutValue = Player.Acceleration;
        }
        else if (Attribute == FName(TEXT("Awareness")))
        {
            OutValue = Player.Awareness;
        }
        else if (Attribute == FName(TEXT("Stamina")))
        {
            OutValue = Player.Stamina;
        }
        else
        {
            return false;
        }
        return true;
    }

    const TCHAR* DescribeRole(EPSCoachRole Role)
    {
        switch (Role)
        {
        case EPSCoachRole::OffensiveCoordinator: return TEXT("offensive coordinator");
        case EPSCoachRole::DefensiveCoordinator: return TEXT("defensive coordinator");
        default:                                 return TEXT("head coach");
        }
    }
}

namespace PSStaffManagerPrivate
{
    /** A candidate's standing in a job search: his two ratings' average. */
    float CandidateRating(const FPSCoachDef& Coach)
    {
        return (Coach.PlayCalling + Coach.Development) * 0.5f;
    }

    /** The better candidate: the higher rating, then the earlier ID (so the carousel is stable). */
    bool IsBetterCandidate(const FPSCoachDef& Coach, float Rating, const FPSCoachDef* Best, float BestRating)
    {
        return !Best || Rating > BestRating || (Rating == BestRating && Coach.CoachId.Compare(Best->CoachId) < 0);
    }

    FName& Job(FPSTeamStaffDef& Staff, EPSCoachRole Role)
    {
        switch (Role)
        {
        case EPSCoachRole::OffensiveCoordinator: return Staff.OffensiveCoordinatorId;
        case EPSCoachRole::DefensiveCoordinator: return Staff.DefensiveCoordinatorId;
        default:                                 return Staff.HeadCoachId;
        }
    }

    FName JobOf(const FPSTeamStaffDef& Staff, EPSCoachRole Role)
    {
        switch (Role)
        {
        case EPSCoachRole::OffensiveCoordinator: return Staff.OffensiveCoordinatorId;
        case EPSCoachRole::DefensiveCoordinator: return Staff.DefensiveCoordinatorId;
        default:                                 return Staff.HeadCoachId;
        }
    }

    /** "2-15", or "8-8-1" with a tie. */
    FString DescribeRecord(const FPSTeamStanding& Standing)
    {
        return Standing.Ties > 0
            ? FString::Printf(TEXT("%d-%d-%d"), Standing.Wins, Standing.Losses, Standing.Ties)
            : FString::Printf(TEXT("%d-%d"), Standing.Wins, Standing.Losses);
    }

    /** Worst record first; then the worse point margin, then the team ID. */
    bool IsWorseRecord(const FPSTeamStanding& A, const FPSTeamStanding& B)
    {
        const float WinsA = A.GetWinPercentage();
        const float WinsB = B.GetWinPercentage();
        if (!FMath::IsNearlyEqual(WinsA, WinsB))
        {
            return WinsA < WinsB;
        }
        const int32 MarginA = A.PointsFor - A.PointsAgainst;
        const int32 MarginB = B.PointsFor - B.PointsAgainst;
        if (MarginA != MarginB)
        {
            return MarginA < MarginB;
        }
        return A.TeamId.Compare(B.TeamId) < 0;
    }
}

FString UPSStaffManager::GetDefaultDataPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/coaching_staffs.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSStaffManager::LoadFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSCoachingLeague Loaded;
    if (!Ingestion->LoadCoachingLeagueFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSStaffManager: Could not load the coaching staffs from %s."), *JsonFilePath);
        return false;
    }
    League = MoveTemp(Loaded);
    return true;
}

const FPSSchemeDef* UPSStaffManager::FindScheme(FName SchemeId) const
{
    if (SchemeId.IsNone())
    {
        return nullptr;
    }
    return League.Schemes.FindByPredicate([SchemeId](const FPSSchemeDef& Scheme) { return Scheme.SchemeId == SchemeId; });
}

const FPSCoachDef* UPSStaffManager::FindCoach(FName CoachId) const
{
    if (CoachId.IsNone())
    {
        return nullptr;
    }
    return League.Coaches.FindByPredicate([CoachId](const FPSCoachDef& Coach) { return Coach.CoachId == CoachId; });
}

FPSCoachDef* UPSStaffManager::FindMutableCoach(FName CoachId)
{
    if (CoachId.IsNone())
    {
        return nullptr;
    }
    return League.Coaches.FindByPredicate([CoachId](const FPSCoachDef& Coach) { return Coach.CoachId == CoachId; });
}

const FPSTeamStaffDef* UPSStaffManager::FindStaff(FName TeamId) const
{
    if (TeamId.IsNone())
    {
        return nullptr;
    }
    return League.Staffs.FindByPredicate([TeamId](const FPSTeamStaffDef& Staff) { return Staff.TeamId == TeamId; });
}

FPSTeamStaffDef* UPSStaffManager::FindMutableStaff(FName TeamId)
{
    if (TeamId.IsNone())
    {
        return nullptr;
    }
    return League.Staffs.FindByPredicate([TeamId](const FPSTeamStaffDef& Staff) { return Staff.TeamId == TeamId; });
}

const FPSTeamStaffDef* UPSStaffManager::FindEmployer(FName CoachId) const
{
    if (CoachId.IsNone())
    {
        return nullptr;
    }
    return League.Staffs.FindByPredicate([CoachId](const FPSTeamStaffDef& Staff)
    {
        return Staff.HeadCoachId == CoachId || Staff.OffensiveCoordinatorId == CoachId || Staff.DefensiveCoordinatorId == CoachId;
    });
}

const FPSCoachDef* UPSStaffManager::FindTeamCoach(FName TeamId, EPSCoachRole Role) const
{
    const FPSTeamStaffDef* Staff = FindStaff(TeamId);
    return Staff ? FindCoach(PSStaffManagerPrivate::JobOf(*Staff, Role)) : nullptr;
}

FName UPSStaffManager::GetTeamScheme(FName TeamId, bool bOffense) const
{
    const FPSCoachDef* Coordinator = FindTeamCoach(TeamId, PSStaff::CoordinatorRole(bOffense));
    return Coordinator ? Coordinator->SchemeId : NAME_None;
}

float UPSStaffManager::GetSchemeAdherence(const FPSCoachDef& Coordinator) const
{
    return FMath::Lerp(League.Tuning.MinSchemeAdherence, League.Tuning.MaxSchemeAdherence, FMath::Clamp(Coordinator.PlayCalling / 100.f, 0.f, 1.f));
}

FPSTendencyProfile UPSStaffManager::BuildTendency(FName TeamId, bool bOffense) const
{
    FPSTendencyProfile Tendency;
    Tendency.TeamId = TeamId;
    if (const FPSCoachDef* HeadCoach = FindTeamCoach(TeamId, EPSCoachRole::HeadCoach))
    {
        Tendency.AggressionScore = FMath::Clamp(HeadCoach->Aggression, 0.f, 1.f);
    }

    const FPSCoachDef* Coordinator = FindTeamCoach(TeamId, PSStaff::CoordinatorRole(bOffense));
    const FPSSchemeDef* Scheme = Coordinator ? FindScheme(Coordinator->SchemeId) : nullptr;
    if (!Scheme)
    {
        return Tendency;
    }

    // The scheme's lean, as far as the coordinator's play calling carries it.
    const float Adherence = GetSchemeAdherence(*Coordinator);
    for (const TPair<FString, float>& Entry : Scheme->CategoryWeights)
    {
        Tendency.CategoryWeights.Add(Entry.Key, FMath::Max(1.f + (Entry.Value - 1.f) * Adherence, 0.f));
    }
    Tendency.Label = Scheme->Label;
    return Tendency;
}

TArray<FName> UPSStaffManager::BuildPlaybook(FName TeamId, const TArray<FPSPlayDefinition>& Plays) const
{
    const FPSSchemeDef* OffenseScheme = FindScheme(GetTeamScheme(TeamId, true));
    const FPSSchemeDef* DefenseScheme = FindScheme(GetTeamScheme(TeamId, false));

    TArray<FName> PlayIds;
    for (const FPSPlayDefinition& Play : Plays)
    {
        // Every team keeps its kicks, returns and clock plays.
        const bool bEveryTeam = PSSpecialTeams::FromCategory(Play.PlayCategory) != EPSSpecialTeamsPlay::None
            || PSSituation::ClockPlayFromCategory(Play.PlayCategory) != EPSClockPlay::None;
        const FPSSchemeDef* Scheme = Play.bIsOffensivePlay ? OffenseScheme : DefenseScheme;
        if (bEveryTeam || !Scheme || Scheme->Formations.Contains(Play.Formation))
        {
            PlayIds.Add(Play.PlayId);
        }
    }
    return PlayIds;
}

FPSTeamPlan UPSStaffManager::BuildTeamPlan(FName TeamId, const TArray<FPSPlayDefinition>& Plays) const
{
    FPSTeamPlan Plan;
    Plan.TeamId = TeamId;
    Plan.OffenseTendency = BuildTendency(TeamId, true);
    Plan.DefenseTendency = BuildTendency(TeamId, false);
    Plan.PlayIds = BuildPlaybook(TeamId, Plays);
    return Plan;
}

bool UPSStaffManager::ApplyToPlayCall(UPSPlayCallSubsystem* PlayCall, FName HomeTeamId, FName AwayTeamId) const
{
    if (!PlayCall)
    {
        return false;
    }
    const TArray<FPSPlayDefinition>& Playbook = PlayCall->GetPlaybook();
    const bool bHomeStaff = FindStaff(HomeTeamId) != nullptr;
    const bool bAwayStaff = FindStaff(AwayTeamId) != nullptr;
    PlayCall->SetTeamPlan(true, bHomeStaff ? BuildTeamPlan(HomeTeamId, Playbook) : FPSTeamPlan());
    PlayCall->SetTeamPlan(false, bAwayStaff ? BuildTeamPlan(AwayTeamId, Playbook) : FPSTeamPlan());
    return bHomeStaff && bAwayStaff;
}

bool UPSStaffManager::GetFitComposite(const FPSSchemeDef& Scheme, const FPlayerAttributes& Player, float& OutComposite) const
{
    float Weighted = 0.f;
    float TotalWeight = 0.f;
    for (const FPSSchemeFitWeight& Entry : Scheme.FitWeights)
    {
        float Value = 0.f;
        if (Entry.Role == Player.Role && Entry.Weight > 0.f && PSStaff::GetAttribute(Player, Entry.Attribute, Value))
        {
            Weighted += Entry.Weight * Value;
            TotalWeight += Entry.Weight;
        }
    }
    if (TotalWeight <= 0.f)
    {
        return false;
    }
    OutComposite = Weighted / TotalWeight;
    return true;
}

FPSSchemeFitResult UPSStaffManager::GetSchemeFitIn(FName SchemeId, const FPlayerAttributes& Player, float Development) const
{
    FPSSchemeFitResult Result;
    Result.PlayerId = Player.PlayerId;
    Result.SchemeId = SchemeId;

    const FPSSchemeDef* Scheme = FindScheme(SchemeId);
    float Composite = 0.f;
    if (!Scheme || Scheme->bOffense != PSStaff::IsOffensiveRole(Player.Role) || !GetFitComposite(*Scheme, Player, Composite))
    {
        return Result;
    }

    // Against his average across the side's schemes that ask something of his position: what
    // suits him here rather than how good he is.
    float Sum = 0.f;
    int32 Count = 0;
    for (const FPSSchemeDef& Other : League.Schemes)
    {
        float OtherComposite = 0.f;
        if (Other.bOffense == Scheme->bOffense && GetFitComposite(Other, Player, OtherComposite))
        {
            Sum += OtherComposite;
            ++Count;
        }
    }
    const float Average = Count > 0 ? Sum / Count : Composite;

    const FPSStaffTuning& Tune = League.Tuning;
    Result.Fit = Tune.FitSpan > 0.f ? FMath::Clamp((Composite - Average) / Tune.FitSpan, -1.f, 1.f) : 0.f;
    if (Result.Fit >= 0.f)
    {
        Result.Multiplier = 1.f + Result.Fit * (Tune.BestFitMultiplier - 1.f);
    }
    else
    {
        // A coordinator who develops players teaches part of the misfit away.
        const float Relief = FMath::Clamp(Tune.DevelopmentMisfitRelief * FMath::Clamp(Development, 0.f, 100.f) / 100.f, 0.f, 1.f);
        Result.Multiplier = 1.f + Result.Fit * (1.f - Tune.WorstFitMultiplier) * (1.f - Relief);
    }

    if (Result.Fit >= Tune.FitLabelThreshold)
    {
        Result.Description = FString::Printf(TEXT("Fits the %s"), *Scheme->Label);
    }
    else if (Result.Fit <= -Tune.FitLabelThreshold)
    {
        Result.Description = FString::Printf(TEXT("Misfit in the %s"), *Scheme->Label);
    }
    return Result;
}

FPSSchemeFitResult UPSStaffManager::GetSchemeFit(FName TeamId, const FPlayerAttributes& Player) const
{
    const FPSCoachDef* Coordinator = FindTeamCoach(TeamId, PSStaff::CoordinatorRole(PSStaff::IsOffensiveRole(Player.Role)));
    if (!Coordinator)
    {
        FPSSchemeFitResult Neutral;
        Neutral.PlayerId = Player.PlayerId;
        return Neutral;
    }
    return GetSchemeFitIn(Coordinator->SchemeId, Player, Coordinator->Development);
}

FPlayerAttributes UPSStaffManager::ApplySchemeFit(FName TeamId, const FPlayerAttributes& Player) const
{
    const float Multiplier = GetSchemeFit(TeamId, Player).Multiplier;
    FPlayerAttributes Fitted = Player;
    Fitted.Speed *= Multiplier;
    Fitted.Agility *= Multiplier;
    Fitted.Strength *= Multiplier;
    Fitted.Acceleration *= Multiplier;
    Fitted.Awareness *= Multiplier;
    return Fitted;
}

TArray<FPSCarouselEvent> UPSStaffManager::RunCarousel(const TArray<FPSTeamStanding>& Standings)
{
    using namespace PSStaffManagerPrivate;
    const FPSStaffTuning& Tune = League.Tuning;
    TArray<FPSCarouselEvent> Events;

    // The teams with a staff, worst record first: the order they hire in.
    TArray<FPSTeamStanding> Order = Standings.FilterByPredicate([this](const FPSTeamStanding& Standing) { return FindStaff(Standing.TeamId) != nullptr; });
    Order.Sort([](const FPSTeamStanding& A, const FPSTeamStanding& B) { return IsWorseRecord(A, B); });

    const auto WinPercentageOf = [&Order](FName TeamId)
    {
        const FPSTeamStanding* Found = Order.FindByPredicate([TeamId](const FPSTeamStanding& Standing) { return Standing.TeamId == TeamId; });
        return Found ? Found->GetWinPercentage() : 0.f;
    };

    // What each job ran before the carousel: a hire's previous scheme.
    TMap<FName, FName> PreviousHeadCoachScheme;
    TMap<FName, FName> PreviousOffenseScheme;
    TMap<FName, FName> PreviousDefenseScheme;
    for (const FPSTeamStanding& Standing : Order)
    {
        const FPSCoachDef* HeadCoach = FindTeamCoach(Standing.TeamId, EPSCoachRole::HeadCoach);
        PreviousHeadCoachScheme.Add(Standing.TeamId, HeadCoach ? HeadCoach->SchemeId : NAME_None);
        PreviousOffenseScheme.Add(Standing.TeamId, GetTeamScheme(Standing.TeamId, true));
        PreviousDefenseScheme.Add(Standing.TeamId, GetTeamScheme(Standing.TeamId, false));
    }

    // Coaches let go this off-season, by the team that let them go: it doesn't hire them back.
    TMap<FName, FName> ReleasedBy;
    const auto LetGo = [this, &Events, &ReleasedBy](FPSTeamStaffDef& Staff, EPSCoachRole Role, const FString& Description)
    {
        FName& Held = Job(Staff, Role);
        const FPSCoachDef* Coach = FindCoach(Held);
        FPSCarouselEvent Event;
        Event.Action = EPSCarouselAction::Fired;
        Event.TeamId = Staff.TeamId;
        Event.CoachId = Held;
        Event.Role = Role;
        Event.PreviousSchemeId = Coach ? Coach->SchemeId : NAME_None;
        Event.Description = Description;
        Events.Add(Event);
        ReleasedBy.Add(Held, Staff.TeamId);
        Held = NAME_None;
    };
    const auto NameOf = [this](FName CoachId)
    {
        const FPSCoachDef* Coach = FindCoach(CoachId);
        return Coach ? Coach->DisplayName : CoachId.ToString();
    };
    const auto SchemeLabel = [this](FName SchemeId)
    {
        const FPSSchemeDef* Scheme = FindScheme(SchemeId);
        return Scheme ? Scheme->Label : SchemeId.ToString();
    };

    // Another season for every head coach.
    for (FPSTeamStaffDef& Staff : League.Staffs)
    {
        if (!Staff.HeadCoachId.IsNone())
        {
            ++Staff.HeadCoachSeasons;
        }
    }

    // 1. Losing head coaches past their grace seasons are fired.
    for (const FPSTeamStanding& Standing : Order)
    {
        FPSTeamStaffDef* Staff = FindMutableStaff(Standing.TeamId);
        if (FindCoach(Staff->HeadCoachId) && Standing.GetWinPercentage() < Tune.FireWinPercentage && Staff->HeadCoachSeasons > Tune.GraceSeasons)
        {
            LetGo(*Staff, EPSCoachRole::HeadCoach, FString::Printf(TEXT("The %s fire head coach %s after a %s season"),
                *Standing.TeamId.ToString(), *NameOf(Staff->HeadCoachId), *DescribeRecord(Standing)));
        }
    }

    // 2. The coordinators of the worst units, on teams keeping their head coach, unless they won.
    for (const bool bOffense : { true, false })
    {
        TArray<FPSTeamStanding> Units = Order.FilterByPredicate([this](const FPSTeamStanding& Standing) { return !FindStaff(Standing.TeamId)->HeadCoachId.IsNone(); });
        Units.StableSort([bOffense](const FPSTeamStanding& A, const FPSTeamStanding& B)
        {
            return bOffense ? A.PointsFor < B.PointsFor : A.PointsAgainst > B.PointsAgainst;
        });
        const EPSCoachRole Role = PSStaff::CoordinatorRole(bOffense);
        for (int32 Index = 0; Index < FMath::Min(Tune.CoordinatorFiresPerSide, Units.Num()); ++Index)
        {
            const FPSTeamStanding& Unit = Units[Index];
            FPSTeamStaffDef* Staff = FindMutableStaff(Unit.TeamId);
            if (FindCoach(Job(*Staff, Role)) && Unit.GetWinPercentage() < Tune.CoordinatorSafeWinPercentage)
            {
                const FString Why = bOffense
                    ? FString::Printf(TEXT("the fewest points scored (%d)"), Unit.PointsFor)
                    : FString::Printf(TEXT("the most points allowed (%d)"), Unit.PointsAgainst);
                LetGo(*Staff, Role, FString::Printf(TEXT("The %s fire %s %s: %s"), *Unit.TeamId.ToString(), PSStaff::DescribeRole(Role), *NameOf(Job(*Staff, Role)), *Why));
            }
        }
    }

    // 3. Head coaching vacancies, worst team first: the best free head coach, or a winning team's
    //    coordinator hired away.
    TSet<FName> NewHeadCoachTeams;
    for (const FPSTeamStanding& Standing : Order)
    {
        FPSTeamStaffDef* Staff = FindMutableStaff(Standing.TeamId);
        if (!Staff->HeadCoachId.IsNone())
        {
            continue;
        }

        const FPSCoachDef* Best = nullptr;
        float BestRating = 0.f;
        bool bBestPromoted = false;
        for (const FPSCoachDef& Coach : League.Coaches)
        {
            if (ReleasedBy.FindRef(Coach.CoachId) == Standing.TeamId)
            {
                continue;
            }
            const FPSTeamStaffDef* Employer = FindEmployer(Coach.CoachId);
            const bool bPromotion = Employer != nullptr;
            if (bPromotion && (Coach.Role == EPSCoachRole::HeadCoach || Employer->TeamId == Standing.TeamId || WinPercentageOf(Employer->TeamId) < Tune.PromoteWinPercentage))
            {
                continue;
            }
            if (!bPromotion && Coach.Role != EPSCoachRole::HeadCoach)
            {
                continue;
            }
            const float Rating = CandidateRating(Coach) + (bPromotion ? Tune.PromotionBonus : 0.f);
            if (IsBetterCandidate(Coach, Rating, Best, BestRating))
            {
                Best = &Coach;
                BestRating = Rating;
                bBestPromoted = bPromotion;
            }
        }
        if (!Best)
        {
            continue;
        }

        const FName HireId = Best->CoachId;
        FPSCarouselEvent Event;
        Event.TeamId = Standing.TeamId;
        Event.CoachId = HireId;
        Event.Role = EPSCoachRole::HeadCoach;
        Event.PreviousSchemeId = PreviousHeadCoachScheme.FindRef(Standing.TeamId);
        Event.NewSchemeId = Best->SchemeId;
        if (bBestPromoted)
        {
            FPSTeamStaffDef* Former = FindMutableStaff(FindEmployer(HireId)->TeamId);
            const EPSCoachRole FormerRole = Best->Role;
            Event.Action = EPSCarouselAction::Promoted;
            Event.Description = FString::Printf(TEXT("The %s hire %s %s %s as head coach (%s)"), *Standing.TeamId.ToString(), *Former->TeamId.ToString(),
                PSStaff::DescribeRole(FormerRole), *Best->DisplayName, *SchemeLabel(Best->SchemeId));
            Job(*Former, FormerRole) = NAME_None;
        }
        else
        {
            Event.Action = EPSCarouselAction::Hired;
            Event.Description = FString::Printf(TEXT("The %s hire head coach %s (%s)"), *Standing.TeamId.ToString(), *Best->DisplayName, *SchemeLabel(Best->SchemeId));
        }
        Events.Add(Event);

        FindMutableCoach(HireId)->Role = EPSCoachRole::HeadCoach;
        Staff->HeadCoachId = HireId;
        Staff->HeadCoachSeasons = 0;
        NewHeadCoachTeams.Add(Standing.TeamId);
    }

    // 4. Scheme churn: a new head coach brings his scheme, and the coordinator running another
    //    on that side goes.
    for (const FPSTeamStanding& Standing : Order)
    {
        if (!NewHeadCoachTeams.Contains(Standing.TeamId))
        {
            continue;
        }
        FPSTeamStaffDef* Staff = FindMutableStaff(Standing.TeamId);
        const FPSCoachDef* HeadCoach = FindCoach(Staff->HeadCoachId);
        const FPSSchemeDef* Scheme = HeadCoach ? FindScheme(HeadCoach->SchemeId) : nullptr;
        if (!Scheme)
        {
            continue;
        }
        const EPSCoachRole Role = PSStaff::CoordinatorRole(Scheme->bOffense);
        const FPSCoachDef* Coordinator = FindCoach(Job(*Staff, Role));
        if (Coordinator && Coordinator->SchemeId != Scheme->SchemeId)
        {
            LetGo(*Staff, Role, FString::Printf(TEXT("The %s let %s %s go: new head coach %s brings the %s"), *Standing.TeamId.ToString(),
                PSStaff::DescribeRole(Role), *Coordinator->DisplayName, *HeadCoach->DisplayName, *Scheme->Label));
        }
    }

    // 5. Coordinator vacancies, worst team first: the best free coordinator for the job, a head
    //    coach's own scheme first.
    for (const FPSTeamStanding& Standing : Order)
    {
        FPSTeamStaffDef* Staff = FindMutableStaff(Standing.TeamId);
        const FPSCoachDef* HeadCoach = FindCoach(Staff->HeadCoachId);
        for (const bool bOffense : { true, false })
        {
            const EPSCoachRole Role = PSStaff::CoordinatorRole(bOffense);
            if (!Job(*Staff, Role).IsNone())
            {
                continue;
            }

            const FPSCoachDef* Best = nullptr;
            float BestRating = 0.f;
            for (const FPSCoachDef& Coach : League.Coaches)
            {
                if (Coach.Role != Role || FindEmployer(Coach.CoachId) || ReleasedBy.FindRef(Coach.CoachId) == Standing.TeamId)
                {
                    continue;
                }
                const float Rating = CandidateRating(Coach) + (HeadCoach && Coach.SchemeId == HeadCoach->SchemeId ? Tune.SchemeMatchBonus : 0.f);
                if (IsBetterCandidate(Coach, Rating, Best, BestRating))
                {
                    Best = &Coach;
                    BestRating = Rating;
                }
            }
            if (!Best)
            {
                continue;
            }

            FPSCarouselEvent Event;
            Event.Action = EPSCarouselAction::Hired;
            Event.TeamId = Standing.TeamId;
            Event.CoachId = Best->CoachId;
            Event.Role = Role;
            Event.PreviousSchemeId = (bOffense ? PreviousOffenseScheme : PreviousDefenseScheme).FindRef(Standing.TeamId);
            Event.NewSchemeId = Best->SchemeId;
            Event.Description = Event.PreviousSchemeId.IsNone() || Event.PreviousSchemeId == Event.NewSchemeId
                ? FString::Printf(TEXT("The %s hire %s %s (%s)"), *Standing.TeamId.ToString(), PSStaff::DescribeRole(Role), *Best->DisplayName, *SchemeLabel(Best->SchemeId))
                : FString::Printf(TEXT("The %s hire %s %s: the %s replaces the %s"), *Standing.TeamId.ToString(), PSStaff::DescribeRole(Role), *Best->DisplayName,
                    *SchemeLabel(Event.NewSchemeId), *SchemeLabel(Event.PreviousSchemeId));
            Events.Add(Event);
            Job(*Staff, Role) = Best->CoachId;
        }
    }

    for (const FPSCarouselEvent& Event : Events)
    {
        UE_LOG(LogTemp, Display, TEXT("UPSStaffManager: %s."), *Event.Description);
    }
    return Events;
}

void UPSStaffManager::SaveTo(UPSFranchiseSaveGame* Save) const
{
    if (Save)
    {
        Save->Coaches = League.Coaches;
        Save->Staffs = League.Staffs;
    }
}

bool UPSStaffManager::LoadFrom(const UPSFranchiseSaveGame* Save)
{
    if (!Save || Save->Coaches.Num() == 0)
    {
        return false;
    }
    League.Coaches = Save->Coaches;
    League.Staffs = Save->Staffs;
    return true;
}

TArray<FString> UPSStaffManager::Validate(const FPSCoachingLeague& InLeague)
{
    TArray<FString> Problems;

    TSet<FName> SchemeIds;
    for (int32 Index = 0; Index < InLeague.Schemes.Num(); ++Index)
    {
        const FPSSchemeDef& Scheme = InLeague.Schemes[Index];
        if (Scheme.SchemeId.IsNone() || SchemeIds.Contains(Scheme.SchemeId))
        {
            Problems.Add(FString::Printf(TEXT("Scheme %d: empty or duplicate SchemeId '%s'"), Index, *Scheme.SchemeId.ToString()));
        }
        SchemeIds.Add(Scheme.SchemeId);
        if (Scheme.Label.IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("Scheme %s: no Label"), *Scheme.SchemeId.ToString()));
        }
        if (Scheme.Formations.Num() == 0)
        {
            Problems.Add(FString::Printf(TEXT("Scheme %s: no Formations"), *Scheme.SchemeId.ToString()));
        }
        for (const TPair<FString, float>& Entry : Scheme.CategoryWeights)
        {
            if (Entry.Value < 0.f)
            {
                Problems.Add(FString::Printf(TEXT("Scheme %s: category %s has a negative weight"), *Scheme.SchemeId.ToString(), *Entry.Key));
            }
        }
        for (const FPSSchemeFitWeight& Entry : Scheme.FitWeights)
        {
            float Unused = 0.f;
            if (!PSStaff::GetAttribute(FPlayerAttributes(), Entry.Attribute, Unused))
            {
                Problems.Add(FString::Printf(TEXT("Scheme %s: '%s' is not a player rating"), *Scheme.SchemeId.ToString(), *Entry.Attribute.ToString()));
            }
            if (Entry.Weight <= 0.f)
            {
                Problems.Add(FString::Printf(TEXT("Scheme %s: fit weight for %s must be above 0"), *Scheme.SchemeId.ToString(), *Entry.Attribute.ToString()));
            }
            if (PSStaff::IsOffensiveRole(Entry.Role) != Scheme.bOffense)
            {
                Problems.Add(FString::Printf(TEXT("Scheme %s: %s plays the other side"), *Scheme.SchemeId.ToString(), *UEnum::GetValueAsString(Entry.Role)));
            }
        }
    }

    TSet<FName> CoachIds;
    for (int32 Index = 0; Index < InLeague.Coaches.Num(); ++Index)
    {
        const FPSCoachDef& Coach = InLeague.Coaches[Index];
        const FString Id = Coach.CoachId.ToString();
        if (Coach.CoachId.IsNone() || CoachIds.Contains(Coach.CoachId))
        {
            Problems.Add(FString::Printf(TEXT("Coach %d: empty or duplicate CoachId '%s'"), Index, *Id));
        }
        CoachIds.Add(Coach.CoachId);
        const FPSSchemeDef* Scheme = InLeague.Schemes.FindByPredicate([&Coach](const FPSSchemeDef& Candidate) { return Candidate.SchemeId == Coach.SchemeId; });
        if (!Scheme)
        {
            Problems.Add(FString::Printf(TEXT("Coach %s: unknown scheme '%s'"), *Id, *Coach.SchemeId.ToString()));
        }
        else if (Coach.Role != EPSCoachRole::HeadCoach && Scheme->bOffense != (Coach.Role == EPSCoachRole::OffensiveCoordinator))
        {
            Problems.Add(FString::Printf(TEXT("Coach %s: a %s cannot run the %s"), *Id, PSStaff::DescribeRole(Coach.Role), *Scheme->Label));
        }
        if (Coach.PlayCalling < 0.f || Coach.PlayCalling > 100.f || Coach.Development < 0.f || Coach.Development > 100.f)
        {
            Problems.Add(FString::Printf(TEXT("Coach %s: ratings must be 0-100"), *Id));
        }
        if (Coach.Aggression < 0.f || Coach.Aggression > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("Coach %s: Aggression must be 0-1"), *Id));
        }
    }

    TSet<FName> TeamIds;
    TSet<FName> Employed;
    for (const FPSTeamStaffDef& Staff : InLeague.Staffs)
    {
        const FString Team = Staff.TeamId.ToString();
        if (Staff.TeamId.IsNone() || TeamIds.Contains(Staff.TeamId))
        {
            Problems.Add(FString::Printf(TEXT("Staff: empty or duplicate TeamId '%s'"), *Team));
        }
        TeamIds.Add(Staff.TeamId);
        for (const EPSCoachRole Role : { EPSCoachRole::HeadCoach, EPSCoachRole::OffensiveCoordinator, EPSCoachRole::DefensiveCoordinator })
        {
            const FName CoachId = PSStaffManagerPrivate::JobOf(Staff, Role);
            if (CoachId.IsNone())
            {
                continue;
            }
            const FPSCoachDef* Coach = InLeague.Coaches.FindByPredicate([CoachId](const FPSCoachDef& Candidate) { return Candidate.CoachId == CoachId; });
            if (!Coach)
            {
                Problems.Add(FString::Printf(TEXT("Staff %s: unknown %s '%s'"), *Team, PSStaff::DescribeRole(Role), *CoachId.ToString()));
            }
            else if (Coach->Role != Role)
            {
                Problems.Add(FString::Printf(TEXT("Staff %s: %s is not a %s"), *Team, *CoachId.ToString(), PSStaff::DescribeRole(Role)));
            }
            if (Employed.Contains(CoachId))
            {
                Problems.Add(FString::Printf(TEXT("Staff %s: %s is on two staffs"), *Team, *CoachId.ToString()));
            }
            Employed.Add(CoachId);
        }
        if (Staff.HeadCoachSeasons < 0)
        {
            Problems.Add(FString::Printf(TEXT("Staff %s: HeadCoachSeasons must be 0 or more"), *Team));
        }
    }
    return Problems;
}
