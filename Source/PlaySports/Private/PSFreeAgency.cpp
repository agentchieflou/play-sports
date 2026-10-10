#include "PSFreeAgency.h"
#include "PSContractManager.h"
#include "PSContractNegotiation.h"
#include "PSRoster.h"

void UPSFreeAgency::Initialize(UPSContractManager* InContracts)
{
    Contracts = InContracts;
    Pool.Reset();
    Signings.Reset();
    TeamOrder.Reset();
    Teams.Reset();
    Day = 0;
    bOpen = false;
}

void UPSFreeAgency::RegisterTeam(FName TeamId, UPSRoster* Roster, bool bUserControlled)
{
    if (TeamId.IsNone())
    {
        return;
    }
    TeamOrder.AddUnique(TeamId);
    FPSFreeAgencyTeam& Entry = Teams.FindOrAdd(TeamId);
    Entry.Roster = Roster;
    Entry.bUserControlled = bUserControlled;
    if (Contracts)
    {
        Contracts->RegisterTeam(TeamId);
    }
}

FPSFreeAgent* UPSFreeAgency::FindMutable(FName PlayerId)
{
    return Pool.FindByPredicate([PlayerId](const FPSFreeAgent& FreeAgent) { return FreeAgent.Player.PlayerId == PlayerId; });
}

bool UPSFreeAgency::GetFreeAgent(FName PlayerId, FPSFreeAgent& OutFreeAgent) const
{
    const FPSFreeAgent* Found = Pool.FindByPredicate([PlayerId](const FPSFreeAgent& FreeAgent) { return FreeAgent.Player.PlayerId == PlayerId; });
    if (Found)
    {
        OutFreeAgent = *Found;
    }
    return Found != nullptr;
}

FPSNegotiationContext UPSFreeAgency::MakeContext(const FPSFreeAgent& FreeAgent) const
{
    return Contracts->MakeNegotiationContext(FreeAgent.Player, FreeAgent.Age, FreeAgent.Morale, FreeAgent.PreviousTeamId);
}

bool UPSFreeAgency::AddFreeAgent(const FPlayerAttributes& Player, int32 Age, float Morale, FName PreviousTeamId)
{
    if (Player.PlayerId.IsNone() || FindMutable(Player.PlayerId) || (Contracts && Contracts->FindContract(Player.PlayerId)))
    {
        return false;
    }
    FPSFreeAgent& FreeAgent = Pool.AddDefaulted_GetRef();
    FreeAgent.Player = Player;
    FreeAgent.Age = Age;
    FreeAgent.Morale = Morale;
    FreeAgent.PreviousTeamId = PreviousTeamId;
    if (bOpen && Contracts)
    {
        FreeAgent.OpeningDemand = Contracts->GetDemand(MakeContext(FreeAgent));
        FreeAgent.Demand = FreeAgent.OpeningDemand;
    }
    return true;
}

int32 UPSFreeAgency::ReleaseUnsignedToPool(const TMap<FName, int32>& AgeByPlayerId)
{
    if (!Contracts)
    {
        return 0;
    }

    int32 Moved = 0;
    for (const FName& TeamId : TeamOrder)
    {
        const FPSFreeAgencyTeam* Entry = Teams.Find(TeamId);
        UPSRoster* Roster = Entry ? Entry->Roster.Get() : nullptr;
        if (!Roster)
        {
            continue;
        }

        TArray<FName> Unsigned;
        for (const FPlayerAttributes& Player : Roster->GetFullRoster())
        {
            if (!Contracts->FindContract(Player.PlayerId))
            {
                Unsigned.Add(Player.PlayerId);
            }
        }
        for (const FName& PlayerId : Unsigned)
        {
            FPlayerAttributes Released;
            if (Roster->RemovePlayer(PlayerId, Released))
            {
                const int32* Age = AgeByPlayerId.Find(PlayerId);
                if (AddFreeAgent(Released, Age ? *Age : Contracts->GetTuning().DefaultPlayerAge, 0.5f, TeamId))
                {
                    ++Moved;
                }
            }
        }
    }
    return Moved;
}

void UPSFreeAgency::BeginPeriod()
{
    if (!Contracts)
    {
        return;
    }
    Day = 1;
    bOpen = true;
    Signings.Reset();
    for (FPSFreeAgent& FreeAgent : Pool)
    {
        FreeAgent.OpeningDemand = Contracts->GetDemand(MakeContext(FreeAgent));
        FreeAgent.Demand = FreeAgent.OpeningDemand;
        FreeAgent.Offers.Reset();
        FreeAgent.DaysConsidering = 0;
    }
    UE_LOG(LogTemp, Display, TEXT("UPSFreeAgency: Free agency opens with %d player(s) for %d day(s)."), Pool.Num(), Contracts->GetTuning().FreeAgencyDays);
}

int32 UPSFreeAgency::FirstYearCapHit(FName PlayerId, const FPSContractOffer& Offer) const
{
    const FPSContract Contract = Contracts->MakeContract(PlayerId, Offer);
    return Contract.Years.Num() > 0 ? Contract.Years[0].GetCapHit() : 0;
}

int32 UPSFreeAgency::PendingCommitments(FName TeamId, FName ExceptPlayerId) const
{
    int32 Committed = 0;
    for (const FPSFreeAgent& FreeAgent : Pool)
    {
        if (FreeAgent.Player.PlayerId == ExceptPlayerId)
        {
            continue;
        }
        for (const FPSPendingOffer& Pending : FreeAgent.Offers)
        {
            if (Pending.Offer.TeamId == TeamId)
            {
                Committed += FirstYearCapHit(FreeAgent.Player.PlayerId, Pending.Offer);
            }
        }
    }
    return Committed;
}

FPSFreeAgencyOfferResponse UPSFreeAgency::SubmitOffer(FName PlayerId, const FPSContractOffer& Offer)
{
    FPSFreeAgencyOfferResponse Response;
    const int32 PoolIndex = Pool.IndexOfByPredicate([PlayerId](const FPSFreeAgent& FreeAgent) { return FreeAgent.Player.PlayerId == PlayerId; });
    if (!bOpen || !Contracts || PoolIndex == INDEX_NONE || !Teams.Contains(Offer.TeamId))
    {
        return Response;
    }

    FPSFreeAgent& FreeAgent = Pool[PoolIndex];
    const FPSContractTuning& Tune = Contracts->GetTuning();
    Response.Evaluation = UPSContractNegotiation::EvaluateOffer(Tune, FreeAgent.Demand, MakeContext(FreeAgent), Offer);
    if (Contracts->CanSign(Contracts->MakeContract(PlayerId, Offer)) == EPSCapResult::InvalidContract)
    {
        Response.Result = EPSFreeAgencyOfferResult::Rejected;
        Response.Evaluation.Reason = TEXT("Not a valid contract");
        return Response;
    }
    if (FirstYearCapHit(PlayerId, Offer) > Contracts->GetCapSpace(Offer.TeamId) - PendingCommitments(Offer.TeamId, PlayerId))
    {
        Response.Result = EPSFreeAgencyOfferResult::OverCap;
        return Response;
    }
    if (Response.Evaluation.Response == EPSNegotiationResponse::Reject)
    {
        Response.Result = EPSFreeAgencyOfferResult::Rejected;
        return Response;
    }

    // Far above his ask: he signs now. Otherwise he weighs it with the rest.
    if (Response.Evaluation.ValueRatio >= Tune.InstantAcceptRatio)
    {
        TArray<FPSFreeAgentSigning> Signed;
        if (Sign(PoolIndex, Offer, Signed))
        {
            Response.Result = EPSFreeAgencyOfferResult::Signed;
            return Response;
        }
    }
    const FName TeamId = Offer.TeamId;
    FreeAgent.Offers.RemoveAll([TeamId](const FPSPendingOffer& Pending) { return Pending.Offer.TeamId == TeamId; });
    FPSPendingOffer& Pending = FreeAgent.Offers.AddDefaulted_GetRef();
    Pending.Offer = Offer;
    Pending.DayMade = Day;
    Response.Result = EPSFreeAgencyOfferResult::Pending;
    return Response;
}

bool UPSFreeAgency::WithdrawOffer(FName TeamId, FName PlayerId)
{
    FPSFreeAgent* FreeAgent = FindMutable(PlayerId);
    return FreeAgent && FreeAgent->Offers.RemoveAll([TeamId](const FPSPendingOffer& Pending) { return Pending.Offer.TeamId == TeamId; }) > 0;
}

float UPSFreeAgency::GetShortfall(FName TeamId, EPlayerRole Role) const
{
    const FPSPositionMarket* Market = Contracts ? Contracts->GetTuning().FindMarket(Role) : nullptr;
    const FPSFreeAgencyTeam* Entry = Teams.Find(TeamId);
    const UPSRoster* Roster = Entry ? Entry->Roster.Get() : nullptr;
    if (!Market || Market->RosterTarget <= 0 || !Roster)
    {
        return 0.f;
    }

    int32 Count = 0;
    for (const FPlayerAttributes& Player : Roster->GetFullRoster())
    {
        Count += Player.Role == Role ? 1 : 0;
    }
    for (const FPSFreeAgent& FreeAgent : Pool)
    {
        if (FreeAgent.Player.Role == Role && FreeAgent.Offers.ContainsByPredicate([TeamId](const FPSPendingOffer& Pending) { return Pending.Offer.TeamId == TeamId; }))
        {
            ++Count;
        }
    }
    return FMath::Clamp(static_cast<float>(Market->RosterTarget - Count) / Market->RosterTarget, 0.f, 1.f);
}

TArray<EPlayerRole> UPSFreeAgency::GetTeamNeeds(FName TeamId) const
{
    TArray<TPair<EPlayerRole, float>> Short;
    if (Contracts)
    {
        for (const FPSPositionMarket& Market : Contracts->GetTuning().PositionMarkets)
        {
            const float Shortfall = GetShortfall(TeamId, Market.Role);
            if (Shortfall > 0.f)
            {
                Short.Emplace(Market.Role, Shortfall);
            }
        }
    }
    Short.StableSort([](const TPair<EPlayerRole, float>& A, const TPair<EPlayerRole, float>& B) { return A.Value > B.Value; });

    TArray<EPlayerRole> Needs;
    for (const TPair<EPlayerRole, float>& Entry : Short)
    {
        Needs.Add(Entry.Key);
    }
    return Needs;
}

bool UPSFreeAgency::Sign(int32 PoolIndex, const FPSContractOffer& Offer, TArray<FPSFreeAgentSigning>& OutSignings)
{
    const FPSFreeAgent FreeAgent = Pool[PoolIndex];
    const FName PlayerId = FreeAgent.Player.PlayerId;
    if (Contracts->SignContract(Contracts->MakeContract(PlayerId, Offer)) != EPSCapResult::Ok)
    {
        return false;
    }

    const FPSFreeAgencyTeam* Entry = Teams.Find(Offer.TeamId);
    if (UPSRoster* Roster = Entry ? Entry->Roster.Get() : nullptr)
    {
        Roster->AddPlayer(FreeAgent.Player);
    }

    FPSFreeAgentSigning Signing;
    Signing.PlayerId = PlayerId;
    Signing.TeamId = Offer.TeamId;
    Signing.Day = Day;
    Signing.Offer = Offer;
    Signings.Add(Signing);
    OutSignings.Add(Signing);
    Pool.RemoveAt(PoolIndex);
    UE_LOG(LogTemp, Display, TEXT("UPSFreeAgency: Day %d: %s signs with %s, %d years at %d a year."),
        Day, *PlayerId.ToString(), *Offer.TeamId.ToString(), Offer.Years, Offer.AnnualValue);
    return true;
}

void UPSFreeAgency::MakeCpuOffers()
{
    const FPSContractTuning& Tune = Contracts->GetTuning();

    // The best players first; the same order every day for the same pool.
    TArray<int32> ByRating;
    for (int32 Index = 0; Index < Pool.Num(); ++Index)
    {
        ByRating.Add(Index);
    }
    ByRating.Sort([this](int32 A, int32 B)
    {
        const float RatingA = UPSContractNegotiation::RatePlayer(Pool[A].Player);
        const float RatingB = UPSContractNegotiation::RatePlayer(Pool[B].Player);
        return RatingA != RatingB ? RatingA > RatingB : Pool[A].Player.PlayerId.LexicalLess(Pool[B].Player.PlayerId);
    });

    const int32 Cushion = FMath::RoundToInt(Tune.AICapCushionFraction * Contracts->GetSalaryCap());
    for (const FName& TeamId : TeamOrder)
    {
        const FPSFreeAgencyTeam* Entry = Teams.Find(TeamId);
        if (!Entry || Entry->bUserControlled || !Entry->Roster.IsValid())
        {
            continue;
        }

        int32 Budget = Contracts->GetCapSpace(TeamId) - PendingCommitments(TeamId, NAME_None) - Cushion;
        for (int32 Bid = 0; Bid < Tune.AIOffersPerDay; ++Bid)
        {
            for (const int32 PoolIndex : ByRating)
            {
                FPSFreeAgent& FreeAgent = Pool[PoolIndex];
                const bool bAlreadyBid = FreeAgent.Offers.ContainsByPredicate([TeamId](const FPSPendingOffer& Pending) { return Pending.Offer.TeamId == TeamId; });
                const float Shortfall = GetShortfall(TeamId, FreeAgent.Player.Role);
                if (bAlreadyBid || Shortfall <= 0.f)
                {
                    continue;
                }

                FPSContractOffer Offer;
                Offer.TeamId = TeamId;
                Offer.Years = FreeAgent.Demand.Years;
                Offer.AnnualValue = FMath::Max(Tune.MinimumSalary, FMath::RoundToInt(FreeAgent.Demand.AnnualValue * (Tune.AIBidRatio + Tune.AINeedPremium * Shortfall)));
                Offer.GuaranteedFraction = FreeAgent.Demand.GuaranteedFraction;
                const int32 CapHit = FirstYearCapHit(FreeAgent.Player.PlayerId, Offer);
                if (CapHit > Budget || UPSContractNegotiation::EvaluateOffer(Tune, FreeAgent.Demand, MakeContext(FreeAgent), Offer).Response == EPSNegotiationResponse::Reject)
                {
                    continue;
                }

                FPSPendingOffer& Pending = FreeAgent.Offers.AddDefaulted_GetRef();
                Pending.Offer = Offer;
                Pending.DayMade = Day;
                Budget -= CapHit;
                break;
            }
        }
    }
}

void UPSFreeAgency::DecideOffers(TArray<FPSFreeAgentSigning>& OutSignings)
{
    const FPSContractTuning& Tune = Contracts->GetTuning();
    int32 Index = 0;
    while (Index < Pool.Num())
    {
        FPSFreeAgent& FreeAgent = Pool[Index];
        const FName PlayerId = FreeAgent.Player.PlayerId;

        // An offer the team can no longer fit under the cap is off the table.
        FreeAgent.Offers.RemoveAll([this, PlayerId](const FPSPendingOffer& Pending)
        {
            return FirstYearCapHit(PlayerId, Pending.Offer) > Contracts->GetCapSpace(Pending.Offer.TeamId);
        });
        if (FreeAgent.Offers.Num() == 0)
        {
            FreeAgent.DaysConsidering = 0;
            ++Index;
            continue;
        }

        ++FreeAgent.DaysConsidering;
        const FPSNegotiationContext Context = MakeContext(FreeAgent);
        int32 BestIndex = 0;
        float BestRatio = -1.f;
        for (int32 OfferIndex = 0; OfferIndex < FreeAgent.Offers.Num(); ++OfferIndex)
        {
            const float Ratio = UPSContractNegotiation::ValueOffer(Tune, FreeAgent.Demand, Context, FreeAgent.Offers[OfferIndex].Offer);
            if (Ratio > BestRatio)
            {
                BestRatio = Ratio;
                BestIndex = OfferIndex;
            }
        }

        const bool bDecisionDay = FreeAgent.DaysConsidering >= Tune.DecisionDays;
        if (BestRatio >= Tune.InstantAcceptRatio || (bDecisionDay && BestRatio >= Tune.WalkAwayRatio))
        {
            const FPSContractOffer Chosen = FreeAgent.Offers[BestIndex].Offer;
            if (Sign(Index, Chosen, OutSignings))
            {
                continue;
            }
            Pool[Index].Offers.RemoveAt(BestIndex);
        }
        else if (bDecisionDay)
        {
            // Nothing he would take: back on the market.
            FreeAgent.Offers.Reset();
            FreeAgent.DaysConsidering = 0;
        }
        ++Index;
    }
}

TArray<FPSFreeAgentSigning> UPSFreeAgency::AdvanceDay()
{
    TArray<FPSFreeAgentSigning> DaySignings;
    if (!bOpen || !Contracts)
    {
        return DaySignings;
    }

    MakeCpuOffers();
    DecideOffers(DaySignings);

    // Unsigned players' asks cool, to a floor (never under the minimum salary).
    const FPSContractTuning& Tune = Contracts->GetTuning();
    for (FPSFreeAgent& FreeAgent : Pool)
    {
        const int32 Floor = FMath::Max(Tune.MinimumSalary, FMath::RoundToInt(FreeAgent.OpeningDemand.AnnualValue * Tune.DemandFloorFraction));
        FreeAgent.Demand.AnnualValue = FMath::Max(Floor, FMath::RoundToInt(FreeAgent.Demand.AnnualValue * (1.f - Tune.DemandDecayPerDay)));
        FreeAgent.Demand.WalkAwayValue = FMath::RoundToInt(FreeAgent.Demand.AnnualValue * Tune.WalkAwayRatio);
    }

    ++Day;
    if (Day > Tune.FreeAgencyDays)
    {
        bOpen = false;
        UE_LOG(LogTemp, Display, TEXT("UPSFreeAgency: Free agency closes: %d signing(s), %d player(s) unsigned."), Signings.Num(), Pool.Num());
    }
    return DaySignings;
}

TArray<FPSFreeAgentSigning> UPSFreeAgency::RunToEnd()
{
    TArray<FPSFreeAgentSigning> All;
    while (bOpen)
    {
        All.Append(AdvanceDay());
    }
    return All;
}
