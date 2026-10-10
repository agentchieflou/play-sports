// PSTradeMarket.cpp - Epic 88: trade logic and the league market
#include "PSTradeMarket.h"
#include "PSContractData.h"
#include "PSContractManager.h"
#include "PSContractNegotiation.h"
#include "PSDataIngestion.h"
#include "PSDraft.h"
#include "PSDraftData.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSLeagueData.h"
#include "PSLocalization.h"
#include "PSLockerRoom.h"
#include "PSLockerRoomData.h"
#include "PSPlayerAging.h"
#include "PSPlayerProgression.h"
#include "PSRoster.h"
#include "PSTelemetryBus.h"
#include "Math/RandomStream.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"

namespace PSTradeMarketPrivate
{
    template <typename EnumType>
    FString EnumName(EnumType Value)
    {
        return StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value));
    }

    /** A seed for one of the market's weekly draws: the same in every run. */
    int32 MarketSeed(int32 Seed, int32 LeagueYear, int32 Week, FName TeamId)
    {
        return static_cast<int32>(FCrc::StrCrc32(*FString::Printf(TEXT("%d:%d:%d:%s"), Seed, LeagueYear, Week, *TeamId.ToString())));
    }

    /** The contract market's tuning: the manager's, else the defaults. */
    const FPSContractTuning& MarketTuning(const UPSContractManager* Contracts)
    {
        static const FPSContractTuning Defaults;
        return Contracts ? Contracts->GetTuning() : Defaults;
    }

    bool IsFraction(float Value)
    {
        return Value >= 0.f && Value <= 1.f;
    }

    /** A stable key for ordering assets the same way every run. */
    FString AssetKey(const FPSTradeAsset& Asset)
    {
        return Asset.Kind == EPSTradeAssetKind::Player ? Asset.PlayerId.ToString()
            : FString::Printf(TEXT("~%d.%d.%s"), Asset.DraftYear, Asset.Round, *Asset.OriginalTeamId.ToString());
    }

    /** True when Got for Gave clears Ratio: Got - Gave at least (Ratio - 1) of what is given
     *  (which may be negative: a contract a team is glad to shed). */
    bool Clears(float Got, float Gave, float Ratio)
    {
        return Got - Gave >= (Ratio - 1.f) * FMath::Abs(Gave);
    }

    /** A reason about value, not about the rules: the player's team may accept a trade the CPU
     *  would not. */
    bool IsValueReason(EPSTradeReason Reason)
    {
        return Reason == EPSTradeReason::GoodValue || Reason == EPSTradeReason::NotEnoughValue
            || Reason == EPSTradeReason::CounterAddAsset || Reason == EPSTradeReason::CounterKeepAsset
            || Reason == EPSTradeReason::UserTeamDecides;
    }

    FText RoleText(EPlayerRole Role)
    {
        return UPSLocalization::GetText(FString::Printf(TEXT("PlayCall.Role.%s"), *EnumName(Role)));
    }
}

// --- Tuning ----------------------------------------------------------------------------------

FString UPSTradeMarket::GetDefaultTuningPath()
{
    return FPaths::ProjectDir() / TEXT("Data/trades.json");
}

bool UPSTradeMarket::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSTradeTuning Loaded;
    if (!Ingestion->LoadTradeTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSTradeMarket: Could not load the trade tuning from %s."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSTradeMarket: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSTradeMarket::ValidateTuning(const FPSTradeTuning& InTuning)
{
    using namespace PSTradeMarketPrivate;

    TArray<FString> Problems;
    if (InTuning.MaxPlayerValue <= 0.f || InTuning.TalentCurveExponent <= 0.f)
    {
        Problems.Add(TEXT("MaxPlayerValue and TalentCurveExponent must be above 0"));
    }
    if (InTuning.RoleWeightExponent < 0.f || InTuning.SurplusValuePerCap < 0.f || InTuning.NeedValueWeight < 0.f || InTuning.LastPickValue < 0.f
        || InTuning.LopsidedMinGap < 0.f || InTuning.DeadlineBuyerPremium < 0.f || InTuning.MinTargetGain < 0.f)
    {
        Problems.Add(TEXT("RoleWeightExponent, SurplusValuePerCap, NeedValueWeight, LastPickValue, LopsidedMinGap, DeadlineBuyerPremium and MinTargetGain must be 0 or more"));
    }
    if (InTuning.ValueHorizonYears < 1 || InTuning.TradablePickYears < 1 || InTuning.MaxAssetsPerSide < 1 || InTuning.MaxTradesPerTeamPerSeason < 1
        || InTuning.DeadlineRampWeeks < 1 || InTuning.TargetsPerAttempt < 1)
    {
        Problems.Add(TEXT("ValueHorizonYears, TradablePickYears, MaxAssetsPerSide, MaxTradesPerTeamPerSeason, DeadlineRampWeeks and TargetsPerAttempt must be at least 1"));
    }
    if (InTuning.MinGamesForStance < 0 || InTuning.RetradeCooldownWeeks < 0 || InTuning.MinPlayersAtRole < 0 || InTuning.MaxOffersToUserPerWeek < 0)
    {
        Problems.Add(TEXT("MinGamesForStance, RetradeCooldownWeeks, MinPlayersAtRole and MaxOffersToUserPerWeek must be 0 or more"));
    }
    for (const float Fraction : { InTuning.UncontrolledYearWeight, InTuning.TradeRequestDiscount, InTuning.ContenderWinPercentage, InTuning.RebuilderWinPercentage,
        InTuning.FuturePickDiscount, InTuning.MaxValueImbalance, InTuning.DeadlineFraction, InTuning.BaseTradeChance, InTuning.DeadlineTradeChance,
        InTuning.DeadlineSellerDiscount })
    {
        if (!IsFraction(Fraction))
        {
            Problems.Add(TEXT("UncontrolledYearWeight, TradeRequestDiscount, the win percentages, FuturePickDiscount, MaxValueImbalance, DeadlineFraction, the trade chances and DeadlineSellerDiscount are 0-1"));
            break;
        }
    }
    if (InTuning.DeadlineFraction <= 0.f)
    {
        Problems.Add(TEXT("DeadlineFraction must be above 0"));
    }
    if (InTuning.RebuilderWinPercentage >= InTuning.ContenderWinPercentage)
    {
        Problems.Add(TEXT("RebuilderWinPercentage must be under ContenderWinPercentage"));
    }
    if (InTuning.CounterRatio <= 0.f || InTuning.CounterRatio > InTuning.AcceptRatio)
    {
        Problems.Add(TEXT("Answers must run 0 < CounterRatio <= AcceptRatio"));
    }

    const UEnum* StanceEnum = StaticEnum<EPSTradeStance>();
    for (int32 Index = 0; StanceEnum && Index < StanceEnum->NumEnums() - 1; ++Index)
    {
        const EPSTradeStance Stance = static_cast<EPSTradeStance>(StanceEnum->GetValueByIndex(Index));
        const int32 Count = InTuning.Stances.FilterByPredicate([Stance](const FPSTradeStanceTuning& Entry) { return Entry.Stance == Stance; }).Num();
        if (Count != 1)
        {
            Problems.Add(FString::Printf(TEXT("Stances: %s has %d entries; it needs one"), *StanceEnum->GetNameStringByIndex(Index), Count));
        }
    }
    for (const FPSTradeStanceTuning& Entry : InTuning.Stances)
    {
        if (!IsFraction(Entry.FutureYearWeight) || Entry.PickMultiplier <= 0.f)
        {
            Problems.Add(FString::Printf(TEXT("Stances: %s needs a FutureYearWeight of 0-1 and a PickMultiplier above 0"), *EnumName(Entry.Stance)));
        }
    }

    if (InTuning.PickRoundValues.Num() == 0)
    {
        Problems.Add(TEXT("PickRoundValues needs the first round's value at least"));
    }
    for (int32 Index = 0; Index < InTuning.PickRoundValues.Num(); ++Index)
    {
        const float Value = InTuning.PickRoundValues[Index];
        if (Value <= 0.f || (Index > 0 && Value > InTuning.PickRoundValues[Index - 1]))
        {
            Problems.Add(TEXT("PickRoundValues must be above 0 and never more for a later round"));
            break;
        }
    }
    if (InTuning.PickRoundValues.Num() > 0 && InTuning.LastPickValue > InTuning.PickRoundValues.Last())
    {
        Problems.Add(TEXT("LastPickValue must be no more than the last round's first pick"));
    }
    return Problems;
}

// --- The league --------------------------------------------------------------------------------

void UPSTradeMarket::Connect(UPSFranchiseSeason* InSeason, UPSContractManager* InContracts, UPSDraft* InDraft, FName InUserTeamId)
{
    if (InSeason != Season)
    {
        // A new season: the off-season is over.
        bOffseason = false;
    }
    Season = InSeason;
    Contracts = InContracts;
    Draft = InDraft;
    UserTeamId = InUserTeamId;
}

void UPSTradeMarket::RegisterTeam(FName TeamId, UPSRoster* Roster)
{
    if (TeamId.IsNone())
    {
        return;
    }
    if (Roster)
    {
        Rosters.Add(TeamId, Roster);
    }
    else
    {
        Rosters.Remove(TeamId);
    }
}

void UPSTradeMarket::SetLockerRoom(const UPSLockerRoom* InLockerRoom)
{
    LockerRoom = InLockerRoom;
}

void UPSTradeMarket::SetPlayerAging(const UPSPlayerAging* InAging)
{
    Aging = InAging;
}

void UPSTradeMarket::BindToBus(UPSTelemetryBus* Bus)
{
    BoundBus = Bus;
}

void UPSTradeMarket::UnbindFromBus()
{
    BoundBus.Reset();
}

void UPSTradeMarket::BeginOffseason()
{
    bOffseason = true;
    State.IncomingOffers.Reset();
}

UPSRoster* UPSTradeMarket::FindRoster(FName TeamId) const
{
    UPSRoster* const* Found = Rosters.Find(TeamId);
    return Found ? *Found : nullptr;
}

const FPlayerAttributes* UPSTradeMarket::FindRosteredPlayer(FName PlayerId, FName& OutTeamId) const
{
    for (const TPair<FName, UPSRoster*>& Team : Rosters)
    {
        if (const FPlayerAttributes* Found = Team.Value ? Team.Value->FindPlayerPtr(PlayerId) : nullptr)
        {
            OutTeamId = Team.Key;
            return Found;
        }
    }
    OutTeamId = NAME_None;
    return nullptr;
}

// --- The calendar ------------------------------------------------------------------------------

int32 UPSTradeMarket::GetFinalWeek() const
{
    int32 FinalWeek = 0;
    if (Season)
    {
        for (const FPSWeekMatchup& Matchup : Season->GetAllMatchups())
        {
            FinalWeek = FMath::Max(FinalWeek, Matchup.WeekNumber);
        }
    }
    return FinalWeek;
}

int32 UPSTradeMarket::GetDeadlineWeek() const
{
    const int32 FinalWeek = GetFinalWeek();
    return FinalWeek > 0 ? FMath::Clamp(FMath::RoundToInt(FinalWeek * Tuning.DeadlineFraction), 1, FinalWeek) : 0;
}

bool UPSTradeMarket::IsOffseason() const
{
    const int32 FinalWeek = GetFinalWeek();
    return bOffseason || (Season && FinalWeek > 0 && Season->GetCurrentWeek() > FinalWeek);
}

bool UPSTradeMarket::IsWindowOpen() const
{
    return !Season || GetFinalWeek() == 0 || IsOffseason() || Season->GetCurrentWeek() <= GetDeadlineWeek();
}

float UPSTradeMarket::GetDeadlineUrgency() const
{
    if (!Season || GetFinalWeek() == 0 || IsOffseason())
    {
        return 0.f;
    }
    const int32 Week = Season->GetCurrentWeek();
    const int32 Deadline = GetDeadlineWeek();
    if (Week > Deadline)
    {
        return 0.f;
    }
    const float Ramp = static_cast<float>(FMath::Max(1, Tuning.DeadlineRampWeeks));
    return FMath::Clamp(1.f - (Deadline - Week) / Ramp, 0.f, 1.f);
}

int32 UPSTradeMarket::GetMarketWeek() const
{
    return !Season || IsOffseason() ? 0 : Season->GetCurrentWeek();
}

int32 UPSTradeMarket::GetLeagueYear() const
{
    return Contracts ? Contracts->GetLeagueYear() : 0;
}

int32 UPSTradeMarket::GetNextDraftYear() const
{
    const int32 LeagueYear = GetLeagueYear();
    // In the season the coming draft is next league year's; once it is over (the league year has
    // rolled over), this one's.
    int32 Next = IsOffseason() ? LeagueYear : LeagueYear + 1;
    if (Draft)
    {
        const FPSDraftState& DraftState = Draft->GetState();
        if (DraftState.DraftYear > 0 && !DraftState.bComplete)
        {
            return DraftState.DraftYear;
        }
        if (DraftState.bComplete && DraftState.DraftYear >= Next)
        {
            Next = DraftState.DraftYear + 1;
        }
    }
    return Next;
}

EPSTradeStance UPSTradeMarket::GetTeamStance(FName TeamId) const
{
    if (!Season || TeamId.IsNone())
    {
        return EPSTradeStance::Balanced;
    }
    const TArray<FPSTeamStanding> Standings = Season->GetStandings();
    const FPSTeamStanding* Standing = Standings.FindByPredicate([TeamId](const FPSTeamStanding& Row) { return Row.TeamId == TeamId; });
    if (!Standing || Standing->Wins + Standing->Losses + Standing->Ties < FMath::Max(1, Tuning.MinGamesForStance))
    {
        return EPSTradeStance::Balanced;
    }
    const float WinPercentage = Standing->GetWinPercentage();
    if (WinPercentage >= Tuning.ContenderWinPercentage)
    {
        return EPSTradeStance::Contender;
    }
    return WinPercentage <= Tuning.RebuilderWinPercentage ? EPSTradeStance::Rebuilder : EPSTradeStance::Balanced;
}

// --- Value -------------------------------------------------------------------------------------

float UPSTradeMarket::SeasonTalent(float Rating, EPlayerRole Role) const
{
    using namespace PSTradeMarketPrivate;

    const FPSContractTuning& Market = MarketTuning(Contracts);
    const float Span = Market.EliteRating - Market.ReplacementRating;
    const float Quality = Span > 0.f ? FMath::Clamp((Rating - Market.ReplacementRating) / Span, 0.f, 1.f) : 0.f;
    float TopShare = 0.f;
    for (const FPSPositionMarket& Entry : Market.PositionMarkets)
    {
        TopShare = FMath::Max(TopShare, Entry.TopCapFraction);
    }
    float RoleWeight = 1.f;
    const FPSPositionMarket* RoleMarket = Market.FindMarket(Role);
    if (RoleMarket && TopShare > 0.f)
    {
        RoleWeight = FMath::Pow(FMath::Clamp(RoleMarket->TopCapFraction / TopShare, 0.f, 1.f), FMath::Max(0.f, Tuning.RoleWeightExponent));
    }
    return Tuning.MaxPlayerValue * FMath::Pow(Quality, FMath::Max(0.01f, Tuning.TalentCurveExponent)) * RoleWeight;
}

float UPSTradeMarket::GetNeed(FName TeamId, EPlayerRole Role, int32 Delta) const
{
    using namespace PSTradeMarketPrivate;

    const UPSRoster* Roster = FindRoster(TeamId);
    if (!Roster)
    {
        return 0.f;
    }
    int32 Count = Delta;
    for (const FPlayerAttributes& Player : Roster->GetFullRoster())
    {
        Count += Player.Role == Role ? 1 : 0;
    }
    return MarketTuning(Contracts).GetRoleShortfall(Role, Count);
}

float UPSTradeMarket::ValuePlayerFor(const FPlayerAttributes& Player, FName OwnerId, FName ViewerId) const
{
    using namespace PSTradeMarketPrivate;

    const bool bNeutral = ViewerId.IsNone();
    const EPSTradeStance Stance = bNeutral ? EPSTradeStance::Balanced : GetTeamStance(ViewerId);
    const FPSTradeStanceTuning* StanceTuning = Tuning.FindStance(Stance);
    const float FutureWeight = StanceTuning ? FMath::Clamp(StanceTuning->FutureYearWeight, 0.f, 1.f) : 0.f;
    // A rebuilder weighs this season less as the deadline nears: it is selling it.
    const float NowWeight = !bNeutral && Stance == EPSTradeStance::Rebuilder
        ? FMath::Max(0.f, 1.f - Tuning.DeadlineSellerDiscount * GetDeadlineUrgency()) : 1.f;

    const FPSContractTuning& Market = MarketTuning(Contracts);
    const int32 Age = Contracts ? Contracts->GetPlayerAge(Player) : (Player.Age > 0 ? Player.Age : Market.DefaultPlayerAge);
    const FPSProgressionTuning Curve = Aging.IsValid() ? Aging->GetCurve(Player.Role) : FPSProgressionTuning();
    const UPSPlayerProgression* Progression = GetDefault<UPSPlayerProgression>();
    const FPSContract* Contract = Contracts ? Contracts->FindContract(Player.PlayerId) : nullptr;
    const int32 LeagueYear = GetLeagueYear();
    const float Cap = Contracts ? static_cast<float>(Contracts->GetSalaryCap()) : 0.f;

    // His seasons ahead, as he will be then (his role's aging curve), each weighed by the stance.
    FPlayerAttributes Projected = Player;
    float OnField = 0.f;
    float WeightSum = 0.f;
    float Surplus = 0.f;
    for (int32 Year = 0; Year < FMath::Max(1, Tuning.ValueHorizonYears); ++Year)
    {
        const float YearWeight = Year == 0 ? NowWeight : FMath::Pow(FutureWeight, static_cast<float>(Year));
        const float Rating = UPSContractNegotiation::RatePlayer(Projected);
        const FPSContractYear* Signed = Contract ? Contract->FindYear(LeagueYear + Year) : nullptr;
        const float Control = Year == 0 || Signed ? 1.f : Tuning.UncontrolledYearWeight;
        OnField += YearWeight * Control * SeasonTalent(Rating, Player.Role);
        WeightSum += YearWeight;
        if (Signed && Cap > 0.f)
        {
            // What the market would pay him that season (the contract market's demand, Epic 87),
            // against the base salary his team pays.
            FPSNegotiationContext Context;
            Context.Role = Player.Role;
            Context.Rating = Rating;
            Context.Age = Age + Year;
            Context.CurrentTeamId = OwnerId;
            Context.MarketCapSpaceFraction = Market.NeutralCapSpaceFraction;
            const float Worth = static_cast<float>(UPSContractNegotiation::ComputeDemand(Market, Contracts->GetSalaryCap(), Context).AnnualValue);
            const float YearCap = static_cast<float>(FMath::Max(1, Contracts->GetProjectedSalaryCap(LeagueYear + Year)));
            Surplus += YearWeight * (Worth / Cap - Signed->BaseSalary / YearCap);
        }
        Progression->ApplyOffseasonProgression(Projected, Age + Year, 1.f, Curve);
    }

    float Value = WeightSum > 0.f ? OnField / WeightSum : 0.f;
    if (!bNeutral)
    {
        // A team short at his position values him more; his own team, one who asked out, less.
        const bool bOwnTeam = ViewerId == OwnerId;
        Value *= 1.f + Tuning.NeedValueWeight * GetNeed(ViewerId, Player.Role, bOwnTeam ? -1 : 0);
        FPSPlayerMorale Morale;
        if (bOwnTeam && LockerRoom.IsValid() && LockerRoom->GetPlayerMorale(Player.PlayerId, Morale) && Morale.bTradeRequested)
        {
            Value *= Tuning.TradeRequestDiscount;
        }
    }
    return Value + Tuning.SurplusValuePerCap * Surplus;
}

float UPSTradeMarket::ValuePlayer(FName TeamId, FName PlayerId) const
{
    FName OwnerId;
    const FPlayerAttributes* Player = FindRosteredPlayer(PlayerId, OwnerId);
    return Player ? ValuePlayerFor(*Player, OwnerId, TeamId) : 0.f;
}

float UPSTradeMarket::GetPickChartValue(int32 Round, float RoundPosition) const
{
    const TArray<float>& Chart = Tuning.PickRoundValues;
    if (Round < 1 || Chart.Num() == 0)
    {
        return 0.f;
    }
    const float Start = Chart.IsValidIndex(Round - 1) ? Chart[Round - 1] : Tuning.LastPickValue;
    const float Next = Chart.IsValidIndex(Round) ? Chart[Round] : Tuning.LastPickValue;
    const float Position = FMath::Clamp(RoundPosition, 0.f, 1.f);
    if (Start <= 0.f || Next <= 0.f)
    {
        return FMath::Max(0.f, FMath::Lerp(Start, Next, Position));
    }
    return Start * FMath::Pow(Next / Start, Position);
}

float UPSTradeMarket::GetProjectedRoundPosition(int32 DraftYear, int32 Round, FName OriginalTeamId) const
{
    // Its draft's order is set: its own place.
    if (Draft)
    {
        const int32 Slot = Draft->GetPickSlot(DraftYear, Round, OriginalTeamId);
        const int32 RoundSize = Draft->GetRoundSize();
        if (Slot > 0 && RoundSize > 0)
        {
            return static_cast<float>((Slot - 1) % RoundSize) / RoundSize;
        }
    }
    const TArray<FPSTeamStanding> Standings = Season ? Season->GetSortedStandings() : TArray<FPSTeamStanding>();
    const int32 NumTeams = FMath::Max(1, Standings.Num() > 0 ? Standings.Num() : Rosters.Num());
    const float Middle = static_cast<float>(NumTeams - 1) / (2.f * NumTeams);
    const int32 Rank = Standings.IndexOfByPredicate([OriginalTeamId](const FPSTeamStanding& Row) { return Row.TeamId == OriginalTeamId; });
    if (!Season || DraftYear != GetNextDraftYear() || Rank == INDEX_NONE)
    {
        return Middle;
    }
    // The coming draft: the worst team picks first, and the standings say more as the season goes.
    const float ByRecord = static_cast<float>(NumTeams - 1 - Rank) / NumTeams;
    int32 Played = 0;
    const TArray<FPSWeekMatchup>& Matchups = Season->GetAllMatchups();
    for (const FPSWeekMatchup& Matchup : Matchups)
    {
        Played += Matchup.bPlayed ? 1 : 0;
    }
    const float Certainty = Matchups.Num() > 0 ? static_cast<float>(Played) / Matchups.Num() : 0.f;
    return FMath::Lerp(Middle, ByRecord, Certainty);
}

float UPSTradeMarket::ValuePickFor(int32 DraftYear, int32 Round, FName OriginalTeamId, FName ViewerId) const
{
    float Value = GetPickChartValue(Round, GetProjectedRoundPosition(DraftYear, Round, OriginalTeamId));
    Value *= FMath::Pow(FMath::Clamp(Tuning.FuturePickDiscount, 0.f, 1.f), static_cast<float>(FMath::Max(0, DraftYear - GetNextDraftYear())));
    if (!ViewerId.IsNone())
    {
        const FPSTradeStanceTuning* StanceTuning = Tuning.FindStance(GetTeamStance(ViewerId));
        Value *= StanceTuning ? StanceTuning->PickMultiplier : 1.f;
    }
    return Value;
}

float UPSTradeMarket::ValuePick(FName TeamId, int32 DraftYear, int32 Round, FName OriginalTeamId) const
{
    return ValuePickFor(DraftYear, Round, OriginalTeamId, TeamId);
}

FPSTradeAssetValue UPSTradeMarket::ValueAsset(const FPSTradeAsset& Asset, FName GiverTeamId, FName ReceiverTeamId) const
{
    FPSTradeAssetValue Result;
    Result.Asset = Asset;
    Result.Label = DescribeAsset(Asset);
    if (Asset.Kind == EPSTradeAssetKind::Player)
    {
        FName OwnerId;
        const FPlayerAttributes* Player = FindRosteredPlayer(Asset.PlayerId, OwnerId);
        if (Player)
        {
            Result.ValueToGiver = ValuePlayerFor(*Player, OwnerId, GiverTeamId);
            Result.ValueToReceiver = ValuePlayerFor(*Player, OwnerId, ReceiverTeamId);
            Result.NeutralValue = ValuePlayerFor(*Player, OwnerId, NAME_None);
        }
        return Result;
    }
    Result.ValueToGiver = ValuePickFor(Asset.DraftYear, Asset.Round, Asset.OriginalTeamId, GiverTeamId);
    Result.ValueToReceiver = ValuePickFor(Asset.DraftYear, Asset.Round, Asset.OriginalTeamId, ReceiverTeamId);
    Result.NeutralValue = ValuePickFor(Asset.DraftYear, Asset.Round, Asset.OriginalTeamId, NAME_None);
    return Result;
}

TArray<FPSTradeAsset> UPSTradeMarket::GetTeamAssets(FName TeamId) const
{
    TArray<FPSTradeAsset> Assets;
    if (const UPSRoster* Roster = FindRoster(TeamId))
    {
        for (const FPlayerAttributes& Player : Roster->GetFullRoster())
        {
            Assets.Add(FPSTradeAsset::MakePlayer(Player.PlayerId));
        }
    }
    if (Draft && Rosters.Contains(TeamId))
    {
        const int32 First = GetNextDraftYear();
        for (int32 Year = First; Year < First + FMath::Max(1, Tuning.TradablePickYears); ++Year)
        {
            for (const FPSDraftPickRight& Right : Draft->GetTeamPicks(TeamId, Year))
            {
                if (Rosters.Contains(Right.OriginalTeamId))
                {
                    Assets.Add(FPSTradeAsset::MakePick(Right.DraftYear, Right.Round, Right.OriginalTeamId));
                }
            }
        }
    }
    return Assets;
}

FString UPSTradeMarket::DescribeAsset(const FPSTradeAsset& Asset) const
{
    using namespace PSTradeMarketPrivate;

    FFormatNamedArguments Arguments;
    if (Asset.Kind == EPSTradeAssetKind::Player)
    {
        FName OwnerId;
        const FPlayerAttributes* Player = FindRosteredPlayer(Asset.PlayerId, OwnerId);
        if (!Player)
        {
            return Asset.PlayerId.ToString();
        }
        Arguments.Add(TEXT("Player"), UPSLocalization::Verbatim(Player->PlayerId.ToString()));
        Arguments.Add(TEXT("Role"), RoleText(Player->Role));
        Arguments.Add(TEXT("Age"), UPSLocalization::FormatNumber(static_cast<float>(Contracts ? Contracts->GetPlayerAge(*Player) : Player->Age), 0));
        return UPSLocalization::Format(TEXT("Trade.Asset.Player"), Arguments).ToString();
    }
    Arguments.Add(TEXT("Year"), UPSLocalization::Verbatim(FString::FromInt(Asset.DraftYear)));
    Arguments.Add(TEXT("Round"), UPSLocalization::FormatNumber(static_cast<float>(Asset.Round), 0));
    Arguments.Add(TEXT("Team"), UPSLocalization::Verbatim(Asset.OriginalTeamId.ToString()));
    return UPSLocalization::Format(TEXT("Trade.Asset.Pick"), Arguments).ToString();
}

// --- Rules -------------------------------------------------------------------------------------

bool UPSTradeMarket::HoldsAsset(FName TeamId, const FPSTradeAsset& Asset) const
{
    if (Asset.Kind == EPSTradeAssetKind::Player)
    {
        const UPSRoster* Roster = FindRoster(TeamId);
        return Roster && !Asset.PlayerId.IsNone() && Roster->FindPlayerPtr(Asset.PlayerId) != nullptr;
    }
    const int32 First = GetNextDraftYear();
    return Draft && Rosters.Contains(Asset.OriginalTeamId)
        && Asset.DraftYear >= First && Asset.DraftYear < First + FMath::Max(1, Tuning.TradablePickYears)
        && Draft->IsPickAvailable(Asset.DraftYear, Asset.Round, Asset.OriginalTeamId)
        && Draft->GetPickOwner(Asset.DraftYear, Asset.Round, Asset.OriginalTeamId) == TeamId;
}

bool UPSTradeMarket::IsCoolingDown(FName PlayerId) const
{
    if (Tuning.RetradeCooldownWeeks <= 0)
    {
        return false;
    }
    const int32 LeagueYear = GetLeagueYear();
    const int32 Week = GetMarketWeek();
    for (const FPSTradeRecord& Trade : State.History)
    {
        if (Trade.LeagueYear != LeagueYear || Week - Trade.Week >= Tuning.RetradeCooldownWeeks)
        {
            continue;
        }
        auto IsHim = [PlayerId](const FPSTradeAsset& Asset) { return Asset.Kind == EPSTradeAssetKind::Player && Asset.PlayerId == PlayerId; };
        if (Trade.Proposal.FromAssets.ContainsByPredicate(IsHim) || Trade.Proposal.ToAssets.ContainsByPredicate(IsHim))
        {
            return true;
        }
    }
    return false;
}

int32 UPSTradeMarket::CountTeamTrades(FName TeamId) const
{
    const int32 LeagueYear = GetLeagueYear();
    int32 Count = 0;
    for (const FPSTradeRecord& Trade : State.History)
    {
        Count += Trade.LeagueYear == LeagueYear && Trade.Involves(TeamId) ? 1 : 0;
    }
    return Count;
}

float UPSTradeMarket::GetAcceptRatio(FName TeamId) const
{
    // A contender pays a premium as the deadline nears.
    const float Premium = GetTeamStance(TeamId) == EPSTradeStance::Contender ? Tuning.DeadlineBuyerPremium * GetDeadlineUrgency() : 0.f;
    return Tuning.AcceptRatio - Premium;
}

void UPSTradeMarket::AddReason(FPSTradeEvaluation& Evaluation, EPSTradeReason Reason, FName TeamId, const FString& Detail) const
{
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Team"), UPSLocalization::Verbatim(TeamId.ToString()));
    Arguments.Add(TEXT("Detail"), UPSLocalization::Verbatim(Detail));
    Arguments.Add(TEXT("In"), UPSLocalization::FormatNumber(Evaluation.ValueIn, 0));
    Arguments.Add(TEXT("Out"), UPSLocalization::FormatNumber(Evaluation.ValueOut, 0));
    Arguments.Add(TEXT("Percent"), UPSLocalization::FormatPercent(Evaluation.Imbalance));
    Arguments.Add(TEXT("Week"), UPSLocalization::FormatNumber(static_cast<float>(GetDeadlineWeek()), 0));

    FText Text;
    switch (Reason)
    {
    case EPSTradeReason::GoodValue:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.GoodValue"), Arguments);
        break;
    case EPSTradeReason::NotEnoughValue:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.NotEnoughValue"), Arguments);
        break;
    case EPSTradeReason::CounterAddAsset:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.CounterAddAsset"), Arguments);
        break;
    case EPSTradeReason::CounterKeepAsset:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.CounterKeepAsset"), Arguments);
        break;
    case EPSTradeReason::Lopsided:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.Lopsided"), Arguments);
        break;
    case EPSTradeReason::WindowClosed:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.WindowClosed"), Arguments);
        break;
    case EPSTradeReason::UnknownTeam:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.UnknownTeam"), Arguments);
        break;
    case EPSTradeReason::SameTeam:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.SameTeam"), Arguments);
        break;
    case EPSTradeReason::EmptySide:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.EmptySide"), Arguments);
        break;
    case EPSTradeReason::TooManyAssets:
        Arguments.Add(TEXT("Count"), UPSLocalization::FormatNumber(static_cast<float>(Tuning.MaxAssetsPerSide), 0));
        Text = UPSLocalization::Format(TEXT("Trade.Reason.TooManyAssets"), Arguments);
        break;
    case EPSTradeReason::DuplicateAsset:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.DuplicateAsset"), Arguments);
        break;
    case EPSTradeReason::AssetNotOwned:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.AssetNotOwned"), Arguments);
        break;
    case EPSTradeReason::OverCap:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.OverCap"), Arguments);
        break;
    case EPSTradeReason::RosterTooThin:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.RosterTooThin"), Arguments);
        break;
    case EPSTradeReason::TradeLimit:
        Arguments.Add(TEXT("Count"), UPSLocalization::FormatNumber(static_cast<float>(Tuning.MaxTradesPerTeamPerSeason), 0));
        Text = UPSLocalization::Format(TEXT("Trade.Reason.TradeLimit"), Arguments);
        break;
    case EPSTradeReason::Cooldown:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.Cooldown"), Arguments);
        break;
    case EPSTradeReason::UserTeamDecides:
        Text = UPSLocalization::Format(TEXT("Trade.Reason.UserTeamDecides"), Arguments);
        break;
    default:
        break;
    }
    Evaluation.Reasons.Add(Reason);
    Evaluation.ReasonTexts.Add(Text.ToString());
}

// --- Proposals ---------------------------------------------------------------------------------

FPSTradeEvaluation UPSTradeMarket::EvaluateTrade(const FPSTradeProposal& Proposal) const
{
    return EvaluateInternal(Proposal, true, false);
}

FPSTradeEvaluation UPSTradeMarket::EvaluateInternal(const FPSTradeProposal& Proposal, bool bAllowCounter, bool bAnswerForUser) const
{
    using namespace PSTradeMarketPrivate;

    FPSTradeEvaluation Evaluation;
    const FName FromId = Proposal.FromTeamId;
    const FName ToId = Proposal.ToTeamId;

    // The teams and the shape of the trade.
    if (!FindRoster(FromId) || !FindRoster(ToId))
    {
        AddReason(Evaluation, EPSTradeReason::UnknownTeam, FindRoster(FromId) ? ToId : FromId, FString());
        return Evaluation;
    }
    if (FromId == ToId)
    {
        AddReason(Evaluation, EPSTradeReason::SameTeam, ToId, FString());
        return Evaluation;
    }
    if (!IsWindowOpen())
    {
        AddReason(Evaluation, EPSTradeReason::WindowClosed, ToId, FString());
        return Evaluation;
    }
    if (Proposal.FromAssets.Num() == 0 || Proposal.ToAssets.Num() == 0)
    {
        AddReason(Evaluation, EPSTradeReason::EmptySide, ToId, FString());
        return Evaluation;
    }
    if (Proposal.FromAssets.Num() > Tuning.MaxAssetsPerSide || Proposal.ToAssets.Num() > Tuning.MaxAssetsPerSide)
    {
        AddReason(Evaluation, EPSTradeReason::TooManyAssets, ToId, FString());
        return Evaluation;
    }
    TArray<FPSTradeAsset> Everything = Proposal.FromAssets;
    Everything.Append(Proposal.ToAssets);
    for (int32 First = 0; First < Everything.Num(); ++First)
    {
        for (int32 Second = First + 1; Second < Everything.Num(); ++Second)
        {
            if (Everything[First].IsSameAs(Everything[Second]))
            {
                AddReason(Evaluation, EPSTradeReason::DuplicateAsset, ToId, DescribeAsset(Everything[First]));
                return Evaluation;
            }
        }
    }

    // Each side holds what it gives.
    for (const FPSTradeAsset& Asset : Proposal.FromAssets)
    {
        if (!HoldsAsset(FromId, Asset))
        {
            AddReason(Evaluation, EPSTradeReason::AssetNotOwned, FromId, DescribeAsset(Asset));
        }
    }
    for (const FPSTradeAsset& Asset : Proposal.ToAssets)
    {
        if (!HoldsAsset(ToId, Asset))
        {
            AddReason(Evaluation, EPSTradeReason::AssetNotOwned, ToId, DescribeAsset(Asset));
        }
    }
    if (Evaluation.Reasons.Num() > 0)
    {
        return Evaluation;
    }

    // The rules: trades a season, cooldowns, depth at each position, the cap.
    for (const FName TeamId : { FromId, ToId })
    {
        if (CountTeamTrades(TeamId) >= Tuning.MaxTradesPerTeamPerSeason)
        {
            AddReason(Evaluation, EPSTradeReason::TradeLimit, TeamId, FString());
        }
    }
    TArray<FPSContractTransfer> Transfers;
    TMap<FName, TMap<EPlayerRole, int32>> RoleChanges;
    for (const bool bFromSide : { true, false })
    {
        const FName GiverId = bFromSide ? FromId : ToId;
        const FName ReceiverId = bFromSide ? ToId : FromId;
        for (const FPSTradeAsset& Asset : bFromSide ? Proposal.FromAssets : Proposal.ToAssets)
        {
            if (Asset.Kind != EPSTradeAssetKind::Player)
            {
                continue;
            }
            if (IsCoolingDown(Asset.PlayerId))
            {
                AddReason(Evaluation, EPSTradeReason::Cooldown, GiverId, DescribeAsset(Asset));
            }
            FName OwnerId;
            if (const FPlayerAttributes* Player = FindRosteredPlayer(Asset.PlayerId, OwnerId))
            {
                RoleChanges.FindOrAdd(GiverId).FindOrAdd(Player->Role) -= 1;
                RoleChanges.FindOrAdd(ReceiverId).FindOrAdd(Player->Role) += 1;
            }
            FPSContractTransfer& Transfer = Transfers.AddDefaulted_GetRef();
            Transfer.PlayerId = Asset.PlayerId;
            Transfer.ToTeamId = ReceiverId;
        }
    }
    for (const TPair<FName, TMap<EPlayerRole, int32>>& Team : RoleChanges)
    {
        const UPSRoster* Roster = FindRoster(Team.Key);
        for (const TPair<EPlayerRole, int32>& Change : Team.Value)
        {
            if (Change.Value >= 0 || !Roster)
            {
                continue;
            }
            int32 Had = 0;
            for (const FPlayerAttributes& Player : Roster->GetFullRoster())
            {
                Had += Player.Role == Change.Key ? 1 : 0;
            }
            if (Had + Change.Value < Tuning.MinPlayersAtRole)
            {
                AddReason(Evaluation, EPSTradeReason::RosterTooThin, Team.Key, RoleText(Change.Key).ToString());
            }
        }
    }
    if (Contracts && Transfers.Num() > 0)
    {
        const FPSTradeCapCheck Cap = Contracts->PreviewTrade(Transfers);
        if (!Cap.bValid)
        {
            AddReason(Evaluation, EPSTradeReason::OverCap, Cap.OverCapTeamId.IsNone() ? FromId : Cap.OverCapTeamId, Cap.Problem);
        }
    }
    if (Evaluation.Reasons.Num() > 0)
    {
        return Evaluation;
    }

    // The values: each side's own, and the league's.
    for (const FPSTradeAsset& Asset : Proposal.FromAssets)
    {
        const FPSTradeAssetValue Value = ValueAsset(Asset, FromId, ToId);
        Evaluation.ValueIn += Value.ValueToReceiver;
        Evaluation.ProposerValueOut += Value.ValueToGiver;
        Evaluation.NeutralFrom += Value.NeutralValue;
        Evaluation.FromAssets.Add(Value);
    }
    for (const FPSTradeAsset& Asset : Proposal.ToAssets)
    {
        const FPSTradeAssetValue Value = ValueAsset(Asset, ToId, FromId);
        Evaluation.ValueOut += Value.ValueToGiver;
        Evaluation.ProposerValueIn += Value.ValueToReceiver;
        Evaluation.NeutralTo += Value.NeutralValue;
        Evaluation.ToAssets.Add(Value);
    }
    const float Gap = FMath::Abs(Evaluation.NeutralFrom - Evaluation.NeutralTo);
    Evaluation.Imbalance = Gap / FMath::Max3(FMath::Abs(Evaluation.NeutralFrom), FMath::Abs(Evaluation.NeutralTo), 1.f);

    // The guardrail: no lopsided trade, whoever would gain.
    if (Gap >= Tuning.LopsidedMinGap && Evaluation.Imbalance > Tuning.MaxValueImbalance)
    {
        AddReason(Evaluation, EPSTradeReason::Lopsided, ToId, FString());
        return Evaluation;
    }
    if (ToId == UserTeamId && !bAnswerForUser)
    {
        AddReason(Evaluation, EPSTradeReason::UserTeamDecides, ToId, FString());
        return Evaluation;
    }

    // The answer: what it gets against what it gives.
    if (Clears(Evaluation.ValueIn, Evaluation.ValueOut, GetAcceptRatio(ToId)))
    {
        Evaluation.Response = EPSTradeResponse::Accept;
        AddReason(Evaluation, EPSTradeReason::GoodValue, ToId, FString());
        return Evaluation;
    }
    FPSTradeProposal Counter;
    EPSTradeReason CounterReason = EPSTradeReason::CounterAddAsset;
    FString CounterDetail;
    if (bAllowCounter && Clears(Evaluation.ValueIn, Evaluation.ValueOut, Tuning.CounterRatio)
        && FindCounter(Proposal, Evaluation, bAnswerForUser, Counter, CounterReason, CounterDetail))
    {
        Evaluation.Response = EPSTradeResponse::Counter;
        Evaluation.bHasCounter = true;
        Evaluation.Counter = Counter;
        AddReason(Evaluation, CounterReason, ToId, CounterDetail);
        return Evaluation;
    }
    AddReason(Evaluation, EPSTradeReason::NotEnoughValue, ToId, FString());
    return Evaluation;
}

bool UPSTradeMarket::FindCounter(const FPSTradeProposal& Proposal, const FPSTradeEvaluation& Evaluation, bool bAnswerForUser,
    FPSTradeProposal& OutCounter, EPSTradeReason& OutReason, FString& OutDetail) const
{
    using namespace PSTradeMarketPrivate;

    struct FCounterOption
    {
        FPSTradeProposal Trade;
        EPSTradeReason Reason = EPSTradeReason::CounterAddAsset;
        FString Detail;
        float Overshoot = 0.f;
        FString Key;
    };
    const FName FromId = Proposal.FromTeamId;
    const FName ToId = Proposal.ToTeamId;
    // How much more the answering team needs to clear its ratio.
    const float Deficit = (GetAcceptRatio(ToId) - 1.f) * FMath::Abs(Evaluation.ValueOut) - (Evaluation.ValueIn - Evaluation.ValueOut);
    TArray<FCounterOption> Options;

    // One more of the proposer's assets ...
    if (Proposal.FromAssets.Num() < Tuning.MaxAssetsPerSide)
    {
        for (const FPSTradeAsset& Asset : GetTeamAssets(FromId))
        {
            if (Proposal.FromAssets.ContainsByPredicate([&Asset](const FPSTradeAsset& Listed) { return Listed.IsSameAs(Asset); }))
            {
                continue;
            }
            const FPSTradeAssetValue Value = ValueAsset(Asset, FromId, ToId);
            if (Value.ValueToReceiver >= Deficit)
            {
                FCounterOption& Option = Options.AddDefaulted_GetRef();
                Option.Trade = Proposal;
                Option.Trade.FromAssets.Add(Asset);
                Option.Reason = EPSTradeReason::CounterAddAsset;
                Option.Detail = Value.Label;
                Option.Overshoot = Value.ValueToReceiver - Deficit;
                Option.Key = AssetKey(Asset);
            }
        }
    }
    // ... or keeping one of its own.
    if (Proposal.ToAssets.Num() > 1)
    {
        for (int32 Index = 0; Index < Proposal.ToAssets.Num() && Evaluation.ToAssets.IsValidIndex(Index); ++Index)
        {
            const float Kept = Evaluation.ToAssets[Index].ValueToGiver;
            if (Kept >= Deficit)
            {
                FCounterOption& Option = Options.AddDefaulted_GetRef();
                Option.Trade = Proposal;
                Option.Trade.ToAssets.RemoveAt(Index);
                Option.Reason = EPSTradeReason::CounterKeepAsset;
                Option.Detail = Evaluation.ToAssets[Index].Label;
                Option.Overshoot = Kept - Deficit;
                Option.Key = AssetKey(Proposal.ToAssets[Index]);
            }
        }
    }

    // The smallest change that it would accept.
    Options.Sort([](const FCounterOption& A, const FCounterOption& B) { return A.Overshoot != B.Overshoot ? A.Overshoot < B.Overshoot : A.Key < B.Key; });
    const int32 Tries = FMath::Min(Options.Num(), FMath::Max(1, Tuning.TargetsPerAttempt));
    for (int32 Index = 0; Index < Tries; ++Index)
    {
        if (EvaluateInternal(Options[Index].Trade, false, bAnswerForUser).Response == EPSTradeResponse::Accept)
        {
            OutCounter = Options[Index].Trade;
            OutReason = Options[Index].Reason;
            OutDetail = Options[Index].Detail;
            return true;
        }
    }
    return false;
}

bool UPSTradeMarket::BuildPackage(FName BuyerTeamId, FName SellerTeamId, const FPSTradeAsset& Target, FPSTradeProposal& OutProposal) const
{
    using namespace PSTradeMarketPrivate;

    const FPSTradeAssetValue TargetValue = ValueAsset(Target, SellerTeamId, BuyerTeamId);
    const float BuyerGets = TargetValue.ValueToReceiver;
    const float SellerLoses = TargetValue.ValueToGiver;
    if (BuyerGets <= 0.f)
    {
        return false;
    }
    // What the seller must get to clear its ratio, and what the buyer may pay to clear its own.
    const float Required = SellerLoses + (GetAcceptRatio(SellerTeamId) - 1.f) * FMath::Abs(SellerLoses);
    const float BuyerRatio = GetAcceptRatio(BuyerTeamId);

    // The buyer's assets the seller values, never one the buyer values above the target.
    struct FPackagePiece
    {
        FPSTradeAsset Asset;
        float ToSeller = 0.f;
        float ToBuyer = 0.f;
        float Neutral = 0.f;
        FString Key;
    };
    TArray<FPackagePiece> Pieces;
    for (const FPSTradeAsset& Asset : GetTeamAssets(BuyerTeamId))
    {
        if (Asset.Kind == EPSTradeAssetKind::Player && IsCoolingDown(Asset.PlayerId))
        {
            continue;
        }
        const FPSTradeAssetValue Value = ValueAsset(Asset, BuyerTeamId, SellerTeamId);
        if (Value.ValueToReceiver <= 0.f || Value.ValueToGiver >= BuyerGets)
        {
            continue;
        }
        FPackagePiece& Piece = Pieces.AddDefaulted_GetRef();
        Piece.Asset = Asset;
        Piece.ToSeller = Value.ValueToReceiver;
        Piece.ToBuyer = Value.ValueToGiver;
        Piece.Neutral = Value.NeutralValue;
        Piece.Key = AssetKey(Asset);
    }

    // The pool: the pieces the seller values most for what they cost the buyer, and the pieces it
    // values most, TargetsPerAttempt of each.
    TArray<FPackagePiece> Pool;
    const int32 PoolSide = FMath::Max(1, Tuning.TargetsPerAttempt);
    TArray<FPackagePiece> Ranked = Pieces;
    Ranked.Sort([](const FPackagePiece& A, const FPackagePiece& B)
    {
        const float RatioA = A.ToSeller / FMath::Max(A.ToBuyer, 1.f);
        const float RatioB = B.ToSeller / FMath::Max(B.ToBuyer, 1.f);
        return RatioA != RatioB ? RatioA > RatioB : A.Key < B.Key;
    });
    for (int32 Index = 0; Index < FMath::Min(PoolSide, Ranked.Num()); ++Index)
    {
        Pool.Add(Ranked[Index]);
    }
    Ranked.Sort([](const FPackagePiece& A, const FPackagePiece& B) { return A.ToSeller != B.ToSeller ? A.ToSeller > B.ToSeller : A.Key < B.Key; });
    for (int32 Index = 0; Index < FMath::Min(PoolSide, Ranked.Num()); ++Index)
    {
        const FString& RankedKey = Ranked[Index].Key;
        if (!Pool.ContainsByPredicate([&RankedKey](const FPackagePiece& Pooled) { return Pooled.Key == RankedKey; }))
        {
            Pool.Add(Ranked[Index]);
        }
    }

    // Every package of up to MaxAssetsPerSide pieces from the pool: the cheapest to the buyer that
    // the seller would accept, the buyer would pay and the guardrail allows.
    TArray<int32> Best;
    float BestCost = 0.f;
    float BestTotal = 0.f;
    const float TargetNeutral = TargetValue.NeutralValue;
    for (int32 Size = 1; Size <= FMath::Min(FMath::Max(1, Tuning.MaxAssetsPerSide), Pool.Num()); ++Size)
    {
        TArray<int32> Combo;
        for (int32 Index = 0; Index < Size; ++Index)
        {
            Combo.Add(Index);
        }
        while (true)
        {
            float Total = 0.f;
            float Cost = 0.f;
            float Neutral = 0.f;
            for (const int32 Index : Combo)
            {
                Total += Pool[Index].ToSeller;
                Cost += Pool[Index].ToBuyer;
                Neutral += Pool[Index].Neutral;
            }
            const float Gap = FMath::Abs(Neutral - TargetNeutral);
            const bool bLopsided = Gap >= Tuning.LopsidedMinGap && Gap / FMath::Max3(FMath::Abs(Neutral), FMath::Abs(TargetNeutral), 1.f) > Tuning.MaxValueImbalance;
            const bool bBetter = Best.Num() == 0 || Cost < BestCost || (Cost == BestCost && Total < BestTotal);
            if (Total >= Required && !bLopsided && Clears(BuyerGets, Cost, BuyerRatio) && bBetter)
            {
                Best = Combo;
                BestCost = Cost;
                BestTotal = Total;
            }
            // The next combination of this size.
            int32 Position = Size - 1;
            while (Position >= 0 && Combo[Position] == Pool.Num() - Size + Position)
            {
                --Position;
            }
            if (Position < 0)
            {
                break;
            }
            ++Combo[Position];
            for (int32 Next = Position + 1; Next < Size; ++Next)
            {
                Combo[Next] = Combo[Next - 1] + 1;
            }
        }
    }
    if (Best.Num() == 0)
    {
        return false;
    }

    OutProposal = FPSTradeProposal();
    OutProposal.FromTeamId = BuyerTeamId;
    OutProposal.ToTeamId = SellerTeamId;
    for (const int32 Index : Best)
    {
        OutProposal.FromAssets.Add(Pool[Index].Asset);
    }
    OutProposal.ToAssets.Add(Target);
    return EvaluateInternal(OutProposal, false, true).Response == EPSTradeResponse::Accept;
}

TArray<FPSTradeProposal> UPSTradeMarket::GenerateProposals(FName BuyerTeamId, int32 Max, bool bToUserTeam) const
{
    using namespace PSTradeMarketPrivate;

    TArray<FPSTradeProposal> Proposals;
    if (!FindRoster(BuyerTeamId) || Max <= 0 || !IsWindowOpen())
    {
        return Proposals;
    }

    // Every other team's player, by how much more he is worth to the buyer than to his team.
    struct FTargetCandidate
    {
        FName PlayerId;
        FName SellerId;
        float Gain = 0.f;
    };
    TArray<FTargetCandidate> Candidates;
    for (const TPair<FName, UPSRoster*>& Team : Rosters)
    {
        const bool bUserSeller = !UserTeamId.IsNone() && Team.Key == UserTeamId;
        if (Team.Key == BuyerTeamId || !Team.Value || bUserSeller != bToUserTeam)
        {
            continue;
        }
        for (const FPlayerAttributes& Player : Team.Value->GetFullRoster())
        {
            if (IsCoolingDown(Player.PlayerId))
            {
                continue;
            }
            const float ToBuyer = ValuePlayerFor(Player, Team.Key, BuyerTeamId);
            const float Gain = ToBuyer - ValuePlayerFor(Player, Team.Key, Team.Key);
            if (ToBuyer > 0.f && Gain > 0.f && Gain >= Tuning.MinTargetGain)
            {
                FTargetCandidate& Candidate = Candidates.AddDefaulted_GetRef();
                Candidate.PlayerId = Player.PlayerId;
                Candidate.SellerId = Team.Key;
                Candidate.Gain = Gain;
            }
        }
    }
    Candidates.Sort([](const FTargetCandidate& A, const FTargetCandidate& B)
    {
        return A.Gain != B.Gain ? A.Gain > B.Gain : A.PlayerId.LexicalLess(B.PlayerId);
    });
    if (Candidates.Num() > Tuning.TargetsPerAttempt)
    {
        Candidates.SetNum(Tuning.TargetsPerAttempt);
    }

    // For each, the package his team would accept and the buyer would pay.
    for (const FTargetCandidate& Candidate : Candidates)
    {
        FPSTradeProposal Proposal;
        if (BuildPackage(BuyerTeamId, Candidate.SellerId, FPSTradeAsset::MakePlayer(Candidate.PlayerId), Proposal))
        {
            Proposals.Add(Proposal);
            if (Proposals.Num() >= Max)
            {
                break;
            }
        }
    }
    return Proposals;
}

FPSTradeEvaluation UPSTradeMarket::ProposeTrade(const FPSTradeProposal& Proposal)
{
    FPSTradeEvaluation Answer = EvaluateInternal(Proposal, true, false);
    if (Answer.Response == EPSTradeResponse::Accept)
    {
        Answer.TradeId = ExecuteTrade(Proposal, Answer).TradeId;
        if (Answer.TradeId == 0)
        {
            // The cap engine refused it after all.
            Answer.Response = EPSTradeResponse::Reject;
        }
    }
    CountAnswer(Answer);
    return Answer;
}

FPSTradeEvaluation UPSTradeMarket::AcceptOffer(int32 OfferId)
{
    using namespace PSTradeMarketPrivate;

    const int32 Index = State.IncomingOffers.IndexOfByPredicate([OfferId](const FPSTradeOffer& Open) { return Open.OfferId == OfferId; });
    if (Index == INDEX_NONE)
    {
        return FPSTradeEvaluation();
    }
    const FPSTradeOffer Offer = State.IncomingOffers[Index];
    State.IncomingOffers.RemoveAt(Index);

    // The rules and the guardrail still hold, and the CPU team still wants it; the player's team
    // may take less than the CPU would.
    FPSTradeEvaluation Check = EvaluateInternal(Offer.Proposal, false, true);
    if (Check.Reasons.ContainsByPredicate([](EPSTradeReason Reason) { return !IsValueReason(Reason); }))
    {
        CountAnswer(Check);
        return Check;
    }
    if (!Clears(Check.ProposerValueIn, Check.ProposerValueOut, GetAcceptRatio(Offer.Proposal.FromTeamId)))
    {
        Check.Response = EPSTradeResponse::Reject;
        Check.Reasons.Reset();
        Check.ReasonTexts.Reset();
        AddReason(Check, EPSTradeReason::NotEnoughValue, Offer.Proposal.FromTeamId, FString());
        CountAnswer(Check);
        return Check;
    }
    Check.Response = EPSTradeResponse::Accept;
    Check.TradeId = ExecuteTrade(Offer.Proposal, Check).TradeId;
    if (Check.TradeId == 0)
    {
        Check.Response = EPSTradeResponse::Reject;
    }
    CountAnswer(Check);
    return Check;
}

bool UPSTradeMarket::DeclineOffer(int32 OfferId)
{
    return State.IncomingOffers.RemoveAll([OfferId](const FPSTradeOffer& Open) { return Open.OfferId == OfferId; }) > 0;
}

// --- The week ----------------------------------------------------------------------------------

TArray<FPSTradeRecord> UPSTradeMarket::RunWeek()
{
    using namespace PSTradeMarketPrivate;

    TArray<FPSTradeRecord> Made;
    const int32 LeagueYear = GetLeagueYear();
    const int32 Week = GetMarketWeek();
    // Last week's offers expire.
    State.IncomingOffers.RemoveAll([LeagueYear, Week](const FPSTradeOffer& Open) { return Open.LeagueYear != LeagueYear || Open.Week != Week; });
    if (!Season || IsOffseason() || !IsWindowOpen())
    {
        return Made;
    }

    const float Urgency = GetDeadlineUrgency();
    TArray<FName> TeamIds;
    Rosters.GenerateKeyArray(TeamIds);
    TeamIds.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
    int32 Offers = State.IncomingOffers.Num();
    for (const FName& BuyerId : TeamIds)
    {
        if (BuyerId == UserTeamId)
        {
            continue;
        }
        // Contenders and rebuilders look harder as the deadline nears.
        const EPSTradeStance Stance = GetTeamStance(BuyerId);
        const float Chance = Stance == EPSTradeStance::Balanced ? Tuning.BaseTradeChance : FMath::Lerp(Tuning.BaseTradeChance, Tuning.DeadlineTradeChance, Urgency);
        FRandomStream Stream(MarketSeed(Tuning.RandomSeed, LeagueYear, Week, BuyerId));
        if (Stream.GetFraction() >= Chance || CountTeamTrades(BuyerId) >= Tuning.MaxTradesPerTeamPerSeason)
        {
            continue;
        }

        // Its best trade with another CPU team.
        for (const FPSTradeProposal& Proposal : GenerateProposals(BuyerId, 1, false))
        {
            const FPSTradeEvaluation Answer = EvaluateInternal(Proposal, true, true);
            CountAnswer(Answer);
            if (Answer.Response == EPSTradeResponse::Accept)
            {
                const FPSTradeRecord Trade = ExecuteTrade(Proposal, Answer);
                if (Trade.TradeId != 0)
                {
                    Made.Add(Trade);
                }
            }
            else if (Answer.Response == EPSTradeResponse::Counter && Answer.bHasCounter)
            {
                // The buyer takes the counter when it still likes the trade.
                const FPSTradeEvaluation Countered = EvaluateInternal(Answer.Counter, false, true);
                if (Countered.Response == EPSTradeResponse::Accept && Clears(Countered.ProposerValueIn, Countered.ProposerValueOut, GetAcceptRatio(BuyerId)))
                {
                    const FPSTradeRecord Trade = ExecuteTrade(Answer.Counter, Countered);
                    if (Trade.TradeId != 0)
                    {
                        Made.Add(Trade);
                    }
                }
            }
        }

        // An offer to the player's team.
        if (!UserTeamId.IsNone() && FindRoster(UserTeamId) && Offers < Tuning.MaxOffersToUserPerWeek)
        {
            for (const FPSTradeProposal& Proposal : GenerateProposals(BuyerId, 1, true))
            {
                FPSTradeOffer& Offer = State.IncomingOffers.AddDefaulted_GetRef();
                Offer.OfferId = State.NextOfferId++;
                Offer.LeagueYear = LeagueYear;
                Offer.Week = Week;
                Offer.Proposal = Proposal;
                Offer.Evaluation = EvaluateInternal(Proposal, false, true);
                ++Offers;
                ++State.Telemetry.OffersToUser;
            }
        }
    }
    return Made;
}

FPSTradeRecord UPSTradeMarket::ExecuteTrade(const FPSTradeProposal& Proposal, const FPSTradeEvaluation& Evaluation)
{
    using namespace PSTradeMarketPrivate;

    FPSTradeRecord Record;
    const FName FromId = Proposal.FromTeamId;
    const FName ToId = Proposal.ToTeamId;

    // The contracts move first: the cap engine has the last word.
    TArray<FPSContractTransfer> Transfers;
    for (const bool bFromSide : { true, false })
    {
        for (const FPSTradeAsset& Asset : bFromSide ? Proposal.FromAssets : Proposal.ToAssets)
        {
            if (Asset.Kind == EPSTradeAssetKind::Player)
            {
                FPSContractTransfer& Transfer = Transfers.AddDefaulted_GetRef();
                Transfer.PlayerId = Asset.PlayerId;
                Transfer.ToTeamId = bFromSide ? ToId : FromId;
            }
        }
    }
    if (Contracts && Transfers.Num() > 0)
    {
        const FPSTradeCapCheck Cap = Contracts->TradeContracts(Transfers);
        if (!Cap.bValid)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSTradeMarket: %s-%s trade not made: %s"), *FromId.ToString(), *ToId.ToString(), *Cap.Problem);
            return Record;
        }
    }

    Record.TradeId = State.NextTradeId++;
    Record.LeagueYear = GetLeagueYear();
    Record.Week = GetMarketWeek();
    Record.DeadlineUrgency = GetDeadlineUrgency();
    Record.Proposal = Proposal;
    Record.FromStance = GetTeamStance(FromId);
    Record.ToStance = GetTeamStance(ToId);
    Record.NeutralFrom = Evaluation.NeutralFrom;
    Record.NeutralTo = Evaluation.NeutralTo;
    Record.Imbalance = Evaluation.Imbalance;
    Record.FromValueIn = Evaluation.ProposerValueIn;
    Record.FromValueOut = Evaluation.ProposerValueOut;
    Record.ToValueIn = Evaluation.ValueIn;
    Record.ToValueOut = Evaluation.ValueOut;
    Record.bUserTeam = Record.Involves(UserTeamId);

    // The headline: the most valuable player in it, and the team that got him. The players' and
    // the picks' neutral values each side got, for the telemetry.
    float Headline = 0.f;
    float PlayersTo[2] = { 0.f, 0.f };
    float PicksTo[2] = { 0.f, 0.f };
    int32 PlayersMoved = 0;
    int32 PicksMoved = 0;
    for (const bool bFromSide : { true, false })
    {
        const int32 ReceiverSide = bFromSide ? 1 : 0;
        for (const FPSTradeAssetValue& Value : bFromSide ? Evaluation.FromAssets : Evaluation.ToAssets)
        {
            if (Value.Asset.Kind == EPSTradeAssetKind::Player)
            {
                ++PlayersMoved;
                PlayersTo[ReceiverSide] += Value.NeutralValue;
                if (Record.HeadlinePlayerId.IsNone() || Value.NeutralValue > Headline)
                {
                    Headline = Value.NeutralValue;
                    Record.HeadlinePlayerId = Value.Asset.PlayerId;
                    Record.HeadlineTeamId = bFromSide ? ToId : FromId;
                }
            }
            else
            {
                ++PicksMoved;
                PicksTo[ReceiverSide] += Value.NeutralValue;
            }
        }
    }

    // The players change rosters (a CPU team slots him on its depth chart by his rating); the picks
    // change hands in the draft.
    for (const bool bFromSide : { true, false })
    {
        const FName GiverId = bFromSide ? FromId : ToId;
        const FName ReceiverId = bFromSide ? ToId : FromId;
        UPSRoster* Giver = FindRoster(GiverId);
        UPSRoster* Receiver = FindRoster(ReceiverId);
        for (const FPSTradeAsset& Asset : bFromSide ? Proposal.FromAssets : Proposal.ToAssets)
        {
            if (Asset.Kind == EPSTradeAssetKind::DraftPick)
            {
                if (!Draft || !Draft->TransferPick(Asset.DraftYear, Asset.Round, Asset.OriginalTeamId, ReceiverId))
                {
                    UE_LOG(LogTemp, Warning, TEXT("UPSTradeMarket: %s could not change hands."), *DescribeAsset(Asset));
                }
                continue;
            }
            FPlayerAttributes Row;
            if (!Giver || !Receiver || !Giver->RemovePlayer(Asset.PlayerId, Row))
            {
                continue;
            }
            Receiver->AddPlayer(Row);
            if (ReceiverId != UserTeamId)
            {
                TArray<FName> Chart = Receiver->GetDepthChartForRole(Row.Role);
                Chart.Remove(Row.PlayerId);
                const float Rating = UPSContractNegotiation::RatePlayer(Row);
                int32 Place = Chart.Num();
                for (int32 Index = 0; Index < Chart.Num(); ++Index)
                {
                    const FPlayerAttributes* Other = Receiver->FindPlayerPtr(Chart[Index]);
                    if (Other && UPSContractNegotiation::RatePlayer(*Other) < Rating)
                    {
                        Place = Index;
                        break;
                    }
                }
                Chart.Insert(Row.PlayerId, Place);
                Receiver->SetDepthChartOrder(Row.Role, Chart);
            }
        }
    }

    // "Hawks get ...; Bears get ...", from the string table.
    const FText Separator = UPSLocalization::GetText(TEXT("Common.ListSeparator"));
    auto ListOf = [&Separator](const TArray<FPSTradeAssetValue>& Values)
    {
        FString Joined;
        for (const FPSTradeAssetValue& Value : Values)
        {
            Joined += (Joined.IsEmpty() ? FString() : Separator.ToString()) + Value.Label;
        }
        return Joined;
    };
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("From"), UPSLocalization::Verbatim(FromId.ToString()));
    Arguments.Add(TEXT("To"), UPSLocalization::Verbatim(ToId.ToString()));
    Arguments.Add(TEXT("FromGets"), UPSLocalization::Verbatim(ListOf(Evaluation.ToAssets)));
    Arguments.Add(TEXT("ToGets"), UPSLocalization::Verbatim(ListOf(Evaluation.FromAssets)));
    Record.Description = UPSLocalization::Format(TEXT("Trade.Description"), Arguments).ToString();
    State.History.Add(Record);

    // The league's telemetry.
    FPSTradeTelemetry& Counts = State.Telemetry;
    ++Counts.Trades;
    Counts.PlayersMoved += PlayersMoved;
    Counts.PicksMoved += PicksMoved;
    Counts.DeadlineTrades += Record.DeadlineUrgency > 0.f ? 1 : 0;
    Counts.SumImbalance += Record.Imbalance;
    Counts.MaxImbalance = FMath::Max(Counts.MaxImbalance, Record.Imbalance);
    // Side 0 is the proposer (it got ToAssets), side 1 the team that answered.
    const EPSTradeStance Stances[2] = { Record.FromStance, Record.ToStance };
    bool bContenderBought = false;
    bool bRebuilderSold = false;
    for (int32 Side = 0; Side < 2; ++Side)
    {
        bContenderBought |= Stances[Side] == EPSTradeStance::Contender && PlayersTo[Side] > PlayersTo[1 - Side];
        bRebuilderSold |= Stances[Side] == EPSTradeStance::Rebuilder && PicksTo[Side] > PicksTo[1 - Side];
    }
    Counts.ContenderBuys += bContenderBought ? 1 : 0;
    Counts.RebuilderSells += bRebuilderSold ? 1 : 0;

    UE_LOG(LogTemp, Display, TEXT("UPSTradeMarket: Trade %d (league year %d, week %d): %s"), Record.TradeId, Record.LeagueYear, Record.Week, *Record.Description);
    OnTradeCompleted.Broadcast(Record);
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        FPSTelemetryTradeEvent Event;
        Event.TradeId = Record.TradeId;
        Event.LeagueYear = Record.LeagueYear;
        Event.Week = Record.Week;
        Event.FromTeamId = FromId;
        Event.ToTeamId = ToId;
        Event.FromStance = EnumName(Record.FromStance);
        Event.ToStance = EnumName(Record.ToStance);
        Event.PlayersMoved = PlayersMoved;
        Event.PicksMoved = PicksMoved;
        Event.NeutralFrom = Record.NeutralFrom;
        Event.NeutralTo = Record.NeutralTo;
        Event.Imbalance = Record.Imbalance;
        Event.DeadlineUrgency = Record.DeadlineUrgency;
        Event.bUserTeam = Record.bUserTeam;
        Event.Description = Record.Description;
        Bus->PublishTrade(Event);
    }
    return Record;
}

void UPSTradeMarket::CountAnswer(const FPSTradeEvaluation& Evaluation)
{
    FPSTradeTelemetry& Counts = State.Telemetry;
    ++Counts.Proposals;
    switch (Evaluation.Response)
    {
    case EPSTradeResponse::Accept:
        ++Counts.Accepted;
        return;
    case EPSTradeResponse::Counter:
        ++Counts.Countered;
        return;
    default:
        break;
    }
    ++Counts.Rejected;
    const int32 NumReasons = StaticEnum<EPSTradeReason>()->NumEnums() - 1;
    if (Counts.RejectionsByReason.Num() < NumReasons)
    {
        Counts.RejectionsByReason.SetNumZeroed(NumReasons);
    }
    for (const EPSTradeReason Reason : Evaluation.Reasons)
    {
        const int32 Index = static_cast<int32>(Reason);
        if (Counts.RejectionsByReason.IsValidIndex(Index))
        {
            ++Counts.RejectionsByReason[Index];
        }
        Counts.GuardrailBlocks += Reason == EPSTradeReason::Lopsided ? 1 : 0;
    }
}

TArray<FString> UPSTradeMarket::DescribeTelemetry() const
{
    using namespace PSTradeMarketPrivate;

    const FPSTradeTelemetry& Counts = State.Telemetry;
    TArray<FString> Lines;
    Lines.Add(FString::Printf(TEXT("Trades: %d (%d players, %d picks moved; %d near the deadline)"), Counts.Trades, Counts.PlayersMoved, Counts.PicksMoved, Counts.DeadlineTrades));
    Lines.Add(FString::Printf(TEXT("Answers: %d proposals, %d accepted, %d countered, %d rejected (%d by the lopsided guardrail)"),
        Counts.Proposals, Counts.Accepted, Counts.Countered, Counts.Rejected, Counts.GuardrailBlocks));
    Lines.Add(FString::Printf(TEXT("Contenders bought in %d, rebuilders sold in %d; %d offers to the player's team"), Counts.ContenderBuys, Counts.RebuilderSells, Counts.OffersToUser));
    Lines.Add(FString::Printf(TEXT("Neutral imbalance: mean %.2f, largest %.2f"), Counts.Trades > 0 ? Counts.SumImbalance / Counts.Trades : 0.f, Counts.MaxImbalance));
    for (int32 Index = 0; Index < Counts.RejectionsByReason.Num(); ++Index)
    {
        if (Counts.RejectionsByReason[Index] > 0)
        {
            Lines.Add(FString::Printf(TEXT("Rejected for %s: %d"), *EnumName(static_cast<EPSTradeReason>(Index)), Counts.RejectionsByReason[Index]));
        }
    }
    return Lines;
}

// --- Persistence -------------------------------------------------------------------------------

void UPSTradeMarket::SaveTo(UPSFranchiseSaveGame* Save) const
{
    if (Save)
    {
        Save->TradeMarket = State;
    }
}

bool UPSTradeMarket::LoadFrom(const UPSFranchiseSaveGame* Save)
{
    if (!Save || (Save->TradeMarket.History.Num() == 0 && Save->TradeMarket.IncomingOffers.Num() == 0 && Save->TradeMarket.Telemetry.Proposals == 0))
    {
        return false;
    }
    State = Save->TradeMarket;
    return true;
}
