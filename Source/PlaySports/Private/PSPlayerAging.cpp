#include "PSPlayerAging.h"
#include "PSContractManager.h"
#include "PSContractNegotiation.h"
#include "PSDataIngestion.h"
#include "PSLeagueHistory.h"
#include "PSLockerRoom.h"
#include "PSRoster.h"
#include "PSStatsEngine.h"
#include "PSWeeklyPreparation.h"
#include "Math/RandomStream.h"
#include "Misc/Crc.h"

namespace PSPlayerAgingPrivate
{
    /** A player who rolled his retirement. */
    struct FRetirementCandidate
    {
        FName PlayerId;
        int32 Age = 0;
        float Chance = 0.f;
        bool bForced = false;
        FString Reason;
    };

    /** A seed for PlayerId's roll after Season that is the same in every run. */
    int32 RetirementSeed(int32 Seed, FName PlayerId, int32 Season)
    {
        return static_cast<int32>(FCrc::StrCrc32(*FString::Printf(TEXT("%d:%s:%d"), Seed, *PlayerId.ToString(), Season)));
    }
}

bool UPSPlayerAging::LoadDefaults()
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSLegacyTuning Loaded;
    const bool bTuning = Ingestion->LoadLegacyTuningFromJson(UPSLeagueHistory::GetDefaultTuningPath(), Loaded);
    if (bTuning)
    {
        for (const FString& Problem : UPSLeagueHistory::ValidateTuning(Loaded))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSPlayerAging: %s"), *Problem);
        }
        Tuning = Loaded;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayerAging: Could not load %s."), *UPSLeagueHistory::GetDefaultTuningPath());
    }
    FPSProgressionTuning Curve;
    const bool bCurve = Ingestion->LoadProgressionTuningFromJson(UPSPlayerProgression::GetDefaultTuningPath(), Curve);
    if (bCurve)
    {
        BaseCurve = Curve;
    }
    return bTuning && bCurve;
}

FPSProgressionTuning UPSPlayerAging::GetCurve(EPlayerRole Role) const
{
    const FPSRoleAgingCurve* Found = Tuning.RoleCurves.FindByPredicate([Role](const FPSRoleAgingCurve& RoleCurve) { return RoleCurve.Role == Role; });
    return Found ? Found->Curve : BaseCurve;
}

float UPSPlayerAging::GetSnapShare(const UPSRoster* Roster, const FPlayerAttributes& Player)
{
    if (!Roster)
    {
        return 0.f;
    }
    const int32 Place = Roster->GetDepthChartForRole(Player.Role).IndexOfByKey(Player.PlayerId);
    return Place == INDEX_NONE ? 0.f : 1.f / (1 + Place);
}

float UPSPlayerAging::GetRetirementChance(const FPlayerAttributes& Player, int32 Age, float Morale, bool bInjured, FString& OutReason) const
{
    const FPSRetirementTuning& Rules = Tuning.Retirement;
    OutReason = FString::Printf(TEXT("Age %d"), Age);
    if (Age >= Rules.ForcedAge)
    {
        return 1.f;
    }
    if (Age < Rules.MinAge)
    {
        OutReason.Reset();
        return 0.f;
    }

    // Each reason's share; the biggest names the decision.
    float Biggest = Rules.BaseChance + Rules.ChancePerYear * (Age - Rules.MinAge);
    float Chance = Biggest;
    const float Rating = UPSContractNegotiation::RatePlayer(Player);
    const TPair<float, FString> Others[] = {
        TPair<float, FString>(Rating < Rules.LowRating ? Rules.LowRatingChance : 0.f, FString::Printf(TEXT("Declining (rated %.0f)"), Rating)),
        TPair<float, FString>(bInjured ? Rules.InjuredChance : 0.f, TEXT("Injured")),
        TPair<float, FString>(Morale < Rules.LowMorale ? Rules.LowMoraleChance : 0.f, TEXT("Unhappy")) };
    for (const TPair<float, FString>& Other : Others)
    {
        Chance += Other.Key;
        if (Other.Key > Biggest)
        {
            Biggest = Other.Key;
            OutReason = Other.Value;
        }
    }
    return FMath::Clamp(Chance, 0.f, 1.f);
}

TArray<FPSRetirementDecision> UPSPlayerAging::RunOffseason(FName TeamId, UPSRoster* Roster, int32 Season, UPSContractManager* Contracts, const UPSStatsEngine* Stats,
    const UPSWeeklyPreparation* Preparation, const UPSLockerRoom* LockerRoom, UPSLeagueHistory* History)
{
    using namespace PSPlayerAgingPrivate;

    TArray<FPSRetirementDecision> Retired;
    if (!Roster)
    {
        return Retired;
    }
    const FPSRetirementTuning& Rules = Tuning.Retirement;
    const auto AgeOf = [Contracts](const FPlayerAttributes& Player) { return Contracts ? Contracts->GetPlayerAge(Player) : Player.Age; };

    // Who would retire: each veteran's roll against his chance.
    TArray<FRetirementCandidate> Candidates;
    for (const FPlayerAttributes& Player : Roster->GetFullRoster())
    {
        const int32 Age = AgeOf(Player);
        if (Age <= 0 || Player.PlayerId.IsNone())
        {
            continue;
        }
        FString Reason;
        const float Chance = GetRetirementChance(Player, Age, LockerRoom ? LockerRoom->GetMorale(Player.PlayerId) : 0.5f,
            Preparation && Preparation->IsInjured(Player.PlayerId), Reason);
        FRandomStream Stream(RetirementSeed(Rules.RandomSeed, Player.PlayerId, Season));
        if (Chance > 0.f && Stream.GetFraction() < Chance)
        {
            FRetirementCandidate& Candidate = Candidates.AddDefaulted_GetRef();
            Candidate.PlayerId = Player.PlayerId;
            Candidate.Age = Age;
            Candidate.Chance = Chance;
            Candidate.bForced = Age >= Rules.ForcedAge;
            Candidate.Reason = Reason;
        }
    }

    // Roster churn: the forced always go; the rest up to MaxRetirementShare of the roster, the likeliest first.
    Candidates.StableSort([](const FRetirementCandidate& A, const FRetirementCandidate& B)
    {
        return A.bForced != B.bForced ? A.bForced : A.Chance > B.Chance;
    });
    const int32 Allowed = FMath::FloorToInt(Rules.MaxRetirementShare * Roster->GetFullRoster().Num());
    for (const FRetirementCandidate& Candidate : Candidates)
    {
        if (!Candidate.bForced && Retired.Num() >= Allowed)
        {
            continue;
        }
        FPlayerAttributes Removed;
        if (!Roster->RemovePlayer(Candidate.PlayerId, Removed))
        {
            continue;
        }
        Removed.Age = Candidate.Age;
        if (Contracts && Contracts->FindContract(Candidate.PlayerId))
        {
            Contracts->CutPlayer(Candidate.PlayerId, false);
        }
        if (History)
        {
            History->RecordRetirement(Removed, TeamId, Season, Candidate.Reason, Stats);
        }
        FPSRetirementDecision& Decision = Retired.AddDefaulted_GetRef();
        Decision.PlayerId = Candidate.PlayerId;
        Decision.TeamId = TeamId;
        Decision.Age = Candidate.Age;
        Decision.Chance = Candidate.Chance;
        Decision.Reason = Candidate.Reason;
    }

    // Everyone else is a year older, along his role's curve.
    if (!Progression)
    {
        Progression = NewObject<UPSPlayerProgression>(this);
    }
    for (FPlayerAttributes& Player : Roster->GetMutableFullRoster())
    {
        const int32 Age = AgeOf(Player);
        if (Age <= 0)
        {
            continue;
        }
        Progression->ApplyOffseasonProgression(Player, Age, GetSnapShare(Roster, Player), GetCurve(Player.Role));
        Player.Age = Age + 1;
    }
    return Retired;
}
