#include "PSContractManager.h"
#include "PSContractNegotiation.h"
#include "PSDataIngestion.h"
#include "PSFranchiseSaveGame.h"
#include "PSRoster.h"
#include "Misc/Paths.h"

namespace PSContractManagerPrivate
{
    static int32 CapHitOf(const FPSContract& Contract, int32 LeagueYear)
    {
        const FPSContractYear* Year = Contract.FindYear(LeagueYear);
        return Year ? Year->GetCapHit() : 0;
    }

    static int32 CarryoverOf(const FPSContractLedger& InLedger, FName TeamId)
    {
        const FPSCapCarryover* Found = InLedger.Carryover.FindByPredicate([TeamId](const FPSCapCarryover& Entry) { return Entry.TeamId == TeamId; });
        return Found ? Found->Amount : 0;
    }

    static int32 DeadMoneyOf(const FPSContractLedger& InLedger, FName TeamId, int32 LeagueYear, FName PlayerId = NAME_None)
    {
        int32 Amount = 0;
        for (const FPSDeadMoneyEntry& Entry : InLedger.DeadMoney)
        {
            if (Entry.TeamId == TeamId && Entry.LeagueYear == LeagueYear && (PlayerId.IsNone() || Entry.PlayerId == PlayerId))
            {
                Amount += Entry.Amount;
            }
        }
        return Amount;
    }

    static int32 CapUsedOf(const FPSContractLedger& InLedger, FName TeamId, int32 LeagueYear)
    {
        int32 Used = DeadMoneyOf(InLedger, TeamId, LeagueYear);
        for (const FPSContract& Contract : InLedger.Contracts)
        {
            if (Contract.TeamId == TeamId)
            {
                Used += CapHitOf(Contract, LeagueYear);
            }
        }
        return Used;
    }

    static int32 ProjectedCapOf(const FPSContractTuning& InTuning, const FPSContractLedger& InLedger, int32 LeagueYear)
    {
        const int32 YearsAhead = FMath::Max(0, LeagueYear - InLedger.LeagueYear);
        return FMath::RoundToInt(InLedger.SalaryCap * FMath::Pow(1.f + InTuning.CapGrowthRate, static_cast<float>(YearsAhead)));
    }

    static int32 CapSpaceOf(const FPSContractTuning& InTuning, const FPSContractLedger& InLedger, FName TeamId, int32 LeagueYear)
    {
        const int32 Carryover = LeagueYear == InLedger.LeagueYear ? CarryoverOf(InLedger, TeamId) : 0;
        return ProjectedCapOf(InTuning, InLedger, LeagueYear) + Carryover - CapUsedOf(InLedger, TeamId, LeagueYear);
    }

    static FPSContract* FindMutableContract(FPSContractLedger& InOutLedger, FName PlayerId)
    {
        return InOutLedger.Contracts.FindByPredicate([PlayerId](const FPSContract& Contract) { return Contract.PlayerId == PlayerId; });
    }

    static void AddDeadMoney(FPSContractLedger& InOutLedger, FName TeamId, FName PlayerId, int32 LeagueYear, int32 Amount)
    {
        if (Amount > 0)
        {
            FPSDeadMoneyEntry& Entry = InOutLedger.DeadMoney.AddDefaulted_GetRef();
            Entry.TeamId = TeamId;
            Entry.PlayerId = PlayerId;
            Entry.LeagueYear = LeagueYear;
            Entry.Amount = Amount;
        }
    }

    /** Count new years after AfterYear at a flat base salary that totals BaseTotal (the remainder
     *  on the last), with Guaranteed of it on the earliest. */
    static void AppendYears(FPSContract& Contract, int32 FirstYear, int32 Count, int32 BaseTotal, int32 Guaranteed)
    {
        const int32 PerYear = BaseTotal / Count;
        const int32 Remainder = BaseTotal - PerYear * Count;
        int32 GuaranteeLeft = Guaranteed;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            FPSContractYear& Year = Contract.Years.AddDefaulted_GetRef();
            Year.LeagueYear = FirstYear + Index;
            Year.BaseSalary = PerYear + (Index == Count - 1 ? Remainder : 0);
            Year.GuaranteedSalary = FMath::Min(Year.BaseSalary, GuaranteeLeft);
            GuaranteeLeft -= Year.GuaranteedSalary;
        }
    }

    /** Spreads Bonus over Count years from Contract.Years[FirstIndex], the remainder on the first. */
    static void ProrateBonus(FPSContract& Contract, int32 FirstIndex, int32 Count, int32 Bonus)
    {
        if (Count <= 0 || Bonus <= 0)
        {
            return;
        }
        const int32 PerYear = Bonus / Count;
        const int32 Remainder = Bonus - PerYear * Count;
        for (int32 Offset = 0; Offset < Count && Contract.Years.IsValidIndex(FirstIndex + Offset); ++Offset)
        {
            Contract.Years[FirstIndex + Offset].ProratedBonus += PerYear + (Offset == 0 ? Remainder : 0);
        }
    }

    /** The first year of Contract in LeagueYear or later; INDEX_NONE when it has run out. */
    static int32 FirstRemainingIndex(const FPSContract& Contract, int32 LeagueYear)
    {
        return Contract.Years.IndexOfByPredicate([LeagueYear](const FPSContractYear& Year) { return Year.LeagueYear >= LeagueYear; });
    }
}

FString UPSContractManager::GetDefaultTuningPath()
{
    return FPaths::ProjectDir() / TEXT("Data/contracts.json");
}

bool UPSContractManager::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSContractTuning Loaded;
    if (!Ingestion->LoadContractTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSContractManager: Could not load the contract tuning from %s."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSContractManager: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSContractManager::ValidateTuning(const FPSContractTuning& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.SalaryCap <= 0 || InTuning.MinimumSalary <= 0 || InTuning.MinimumSalary >= InTuning.SalaryCap)
    {
        Problems.Add(TEXT("SalaryCap and MinimumSalary must be positive, the minimum below the cap"));
    }
    if (InTuning.CapGrowthRate <= -1.f)
    {
        Problems.Add(TEXT("CapGrowthRate must be above -1"));
    }
    if (InTuning.MaxContractYears < 1 || InTuning.MaxProrationYears < 1)
    {
        Problems.Add(TEXT("MaxContractYears and MaxProrationYears must be at least 1"));
    }
    if (InTuning.EliteRating <= InTuning.ReplacementRating || InTuning.DemandCurveExponent <= 0.f)
    {
        Problems.Add(TEXT("EliteRating must be above ReplacementRating, and DemandCurveExponent positive"));
    }
    if (InTuning.MinGuaranteeFraction < 0.f || InTuning.MinGuaranteeFraction > InTuning.MaxGuaranteeFraction || InTuning.MaxGuaranteeFraction > 1.f)
    {
        Problems.Add(TEXT("Guarantee fractions must run 0 <= MinGuaranteeFraction <= MaxGuaranteeFraction <= 1"));
    }
    if (InTuning.WalkAwayRatio <= 0.f || InTuning.WalkAwayRatio > InTuning.AcceptRatio || InTuning.AcceptRatio > InTuning.InstantAcceptRatio)
    {
        Problems.Add(TEXT("Offer ratios must run 0 < WalkAwayRatio <= AcceptRatio <= InstantAcceptRatio"));
    }
    if (InTuning.FreeAgencyDays < 1 || InTuning.DecisionDays < 1 || InTuning.AIOffersPerDay < 0)
    {
        Problems.Add(TEXT("FreeAgencyDays and DecisionDays must be at least 1, AIOffersPerDay at least 0"));
    }
    if (InTuning.DemandDecayPerDay < 0.f || InTuning.DemandDecayPerDay >= 1.f || InTuning.DemandFloorFraction <= 0.f || InTuning.DemandFloorFraction > 1.f)
    {
        Problems.Add(TEXT("DemandDecayPerDay must be in [0, 1) and DemandFloorFraction in (0, 1]"));
    }

    const UEnum* Roles = StaticEnum<EPlayerRole>();
    for (int32 Index = 0; Roles && Index < Roles->NumEnums() - 1; ++Index)
    {
        const EPlayerRole Role = static_cast<EPlayerRole>(Roles->GetValueByIndex(Index));
        const int32 Count = InTuning.PositionMarkets.FilterByPredicate([Role](const FPSPositionMarket& Market) { return Market.Role == Role; }).Num();
        if (Count != 1)
        {
            Problems.Add(FString::Printf(TEXT("PositionMarkets: %s has %d entries; it needs one"), *Roles->GetNameStringByIndex(Index), Count));
        }
    }
    for (const FPSPositionMarket& Market : InTuning.PositionMarkets)
    {
        if (Market.TopCapFraction <= 0.f || Market.TopCapFraction > 1.f || Market.RosterTarget < 0)
        {
            Problems.Add(FString::Printf(TEXT("PositionMarkets: %s needs a TopCapFraction in (0, 1] and a RosterTarget of 0 or more"), *UEnum::GetValueAsString(Market.Role)));
        }
    }
    return Problems;
}

void UPSContractManager::StartLeague()
{
    Ledger = FPSContractLedger();
    Ledger.LeagueYear = Tuning.FirstLeagueYear;
    Ledger.SalaryCap = Tuning.SalaryCap;
}

void UPSContractManager::RegisterTeam(FName TeamId)
{
    if (!TeamId.IsNone())
    {
        Ledger.TeamIds.AddUnique(TeamId);
    }
}

int32 UPSContractManager::GetProjectedSalaryCap(int32 LeagueYear) const
{
    return PSContractManagerPrivate::ProjectedCapOf(Tuning, Ledger, LeagueYear);
}

const FPSContract* UPSContractManager::FindContract(FName PlayerId) const
{
    return Ledger.Contracts.FindByPredicate([PlayerId](const FPSContract& Contract) { return Contract.PlayerId == PlayerId; });
}

bool UPSContractManager::GetContract(FName PlayerId, FPSContract& OutContract) const
{
    const FPSContract* Found = FindContract(PlayerId);
    if (Found)
    {
        OutContract = *Found;
    }
    return Found != nullptr;
}

TArray<FPSContract> UPSContractManager::GetTeamContracts(FName TeamId) const
{
    return Ledger.Contracts.FilterByPredicate([TeamId](const FPSContract& Contract) { return Contract.TeamId == TeamId; });
}

FPSContract UPSContractManager::MakeContract(FName PlayerId, const FPSContractOffer& Offer) const
{
    using namespace PSContractManagerPrivate;

    FPSContract Contract;
    Contract.PlayerId = PlayerId;
    Contract.TeamId = Offer.TeamId;

    const int32 Years = FMath::Clamp(Offer.Years, 1, FMath::Max(1, Tuning.MaxContractYears));
    const int32 Total = FMath::Max(0, Offer.AnnualValue) * Years;
    const int32 Bonus = FMath::Clamp(Offer.SigningBonus, 0, FMath::Max(0, Total - FMath::Max(0, Tuning.MinimumSalary) * Years));
    const int32 BaseTotal = Total - Bonus;
    const int32 Guaranteed = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Offer.GuaranteedFraction, 0.f, 1.f) * Total) - Bonus, 0, BaseTotal);

    AppendYears(Contract, Ledger.LeagueYear, Years, BaseTotal, Guaranteed);
    ProrateBonus(Contract, 0, FMath::Min(Years, FMath::Max(1, Tuning.MaxProrationYears)), Bonus);
    Contract.BonusPaid = Bonus;
    return Contract;
}

FString UPSContractManager::CheckContract(const FPSContract& Contract) const
{
    if (Contract.PlayerId.IsNone() || Contract.TeamId.IsNone())
    {
        return TEXT("A contract needs a player and a team");
    }
    if (Contract.Years.Num() < 1 || Contract.Years.Num() > Tuning.MaxContractYears)
    {
        return FString::Printf(TEXT("It runs %d years; 1 to %d are allowed"), Contract.Years.Num(), Tuning.MaxContractYears);
    }
    if (Contract.Years[0].LeagueYear < Ledger.LeagueYear)
    {
        return TEXT("It starts before this league year");
    }
    for (int32 Index = 0; Index < Contract.Years.Num(); ++Index)
    {
        const FPSContractYear& Year = Contract.Years[Index];
        if (Year.LeagueYear != Contract.Years[0].LeagueYear + Index)
        {
            return TEXT("Its years must be consecutive");
        }
        if (Year.BaseSalary < Tuning.MinimumSalary)
        {
            return FString::Printf(TEXT("%d pays less than the minimum salary"), Year.LeagueYear);
        }
        if (Year.GuaranteedSalary < 0 || Year.GuaranteedSalary > Year.BaseSalary || Year.ProratedBonus < 0)
        {
            return FString::Printf(TEXT("%d guarantees more than its base salary, or a negative bonus"), Year.LeagueYear);
        }
    }
    return FString();
}

EPSCapResult UPSContractManager::CanSign(const FPSContract& Contract) const
{
    using namespace PSContractManagerPrivate;

    if (!CheckContract(Contract).IsEmpty())
    {
        return EPSCapResult::InvalidContract;
    }
    if (FindContract(Contract.PlayerId))
    {
        return EPSCapResult::AlreadySigned;
    }
    const int32 FirstYear = Contract.Years[0].LeagueYear;
    if (CapHitOf(Contract, FirstYear) > CapSpaceOf(Tuning, Ledger, Contract.TeamId, FirstYear))
    {
        return EPSCapResult::OverCap;
    }
    return EPSCapResult::Ok;
}

EPSCapResult UPSContractManager::SignContract(const FPSContract& Contract)
{
    const EPSCapResult Result = CanSign(Contract);
    if (Result == EPSCapResult::Ok)
    {
        RegisterTeam(Contract.TeamId);
        Ledger.Contracts.Add(Contract);
    }
    return Result;
}

int32 UPSContractManager::SignRosterAtDemand(FName TeamId, const TArray<FPlayerAttributes>& Players)
{
    RegisterTeam(TeamId);
    int32 Signed = 0;
    for (int32 Index = 0; Index < Players.Num(); ++Index)
    {
        const FPlayerAttributes& Player = Players[Index];
        if (FindContract(Player.PlayerId))
        {
            continue;
        }
        const FPSContractDemand Demand = GetDemand(MakeNegotiationContext(Player, Tuning.DefaultPlayerAge, 0.5f, TeamId));
        FPSContractOffer Offer;
        Offer.TeamId = TeamId;
        Offer.Years = 1 + Index % FMath::Max(1, Demand.Years);
        Offer.AnnualValue = Demand.AnnualValue;
        Offer.GuaranteedFraction = Demand.GuaranteedFraction;
        if (SignContract(MakeContract(Player.PlayerId, Offer)) == EPSCapResult::Ok)
        {
            ++Signed;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSContractManager: %s could not sign %s at his demand; he is unsigned."), *TeamId.ToString(), *Player.PlayerId.ToString());
        }
    }
    return Signed;
}

int32 UPSContractManager::GetCapHit(FName PlayerId, int32 LeagueYear) const
{
    const FPSContract* Contract = FindContract(PlayerId);
    return Contract ? PSContractManagerPrivate::CapHitOf(*Contract, LeagueYear) : 0;
}

int32 UPSContractManager::GetDeadMoney(FName TeamId, int32 LeagueYear) const
{
    return PSContractManagerPrivate::DeadMoneyOf(Ledger, TeamId, LeagueYear);
}

int32 UPSContractManager::GetCarryover(FName TeamId) const
{
    return PSContractManagerPrivate::CarryoverOf(Ledger, TeamId);
}

int32 UPSContractManager::GetCapUsed(FName TeamId, int32 LeagueYear) const
{
    return PSContractManagerPrivate::CapUsedOf(Ledger, TeamId, LeagueYear);
}

int32 UPSContractManager::GetCapSpace(FName TeamId) const
{
    return PSContractManagerPrivate::CapSpaceOf(Tuning, Ledger, TeamId, Ledger.LeagueYear);
}

int32 UPSContractManager::GetCapSpaceInYear(FName TeamId, int32 LeagueYear) const
{
    return PSContractManagerPrivate::CapSpaceOf(Tuning, Ledger, TeamId, LeagueYear);
}

TArray<FName> UPSContractManager::GetTeamsOverCap() const
{
    return Ledger.TeamIds.FilterByPredicate([this](FName TeamId) { return !IsCompliant(TeamId); });
}

float UPSContractManager::GetLeagueCapSpaceFraction() const
{
    if (Ledger.TeamIds.Num() == 0 || Ledger.SalaryCap <= 0)
    {
        return Tuning.NeutralCapSpaceFraction;
    }
    float Sum = 0.f;
    for (const FName& TeamId : Ledger.TeamIds)
    {
        Sum += FMath::Clamp(static_cast<float>(GetCapSpace(TeamId)) / Ledger.SalaryCap, 0.f, 1.f);
    }
    return Sum / Ledger.TeamIds.Num();
}

FPSNegotiationContext UPSContractManager::MakeNegotiationContext(const FPlayerAttributes& Player, int32 Age, float Morale, FName CurrentTeamId) const
{
    FPSNegotiationContext Context;
    Context.Role = Player.Role;
    Context.Rating = UPSContractNegotiation::RatePlayer(Player);
    Context.Age = Age;
    Context.Morale = Morale;
    Context.CurrentTeamId = CurrentTeamId;
    Context.MarketCapSpaceFraction = GetLeagueCapSpaceFraction();
    return Context;
}

FPSContractDemand UPSContractManager::GetDemand(const FPSNegotiationContext& Context) const
{
    return UPSContractNegotiation::ComputeDemand(Tuning, Ledger.SalaryCap, Context);
}

FPSNegotiationResult UPSContractManager::ProposeExtension(FName PlayerId, const FPSNegotiationContext& Context, const FPSContractOffer& Offer, FPSCapPreview& OutPreview)
{
    FPSContractOffer Extension = Offer;
    if (const FPSContract* Contract = FindContract(PlayerId))
    {
        Extension.TeamId = Contract->TeamId;
    }

    FPSNegotiationResult Result = UPSContractNegotiation::EvaluateOffer(Tuning, GetDemand(Context), Context, Extension);
    OutPreview = PreviewExtension(PlayerId, Extension);
    if (!OutPreview.bValid)
    {
        Result.Response = EPSNegotiationResponse::Reject;
        Result.Reason = OutPreview.Problem;
    }
    else if (Result.Response == EPSNegotiationResponse::Accept)
    {
        OutPreview = ExtendContract(PlayerId, Extension);
    }
    return Result;
}

FPSCapPreview UPSContractManager::PreviewMove(FName PlayerId, TFunctionRef<bool(FPSContractLedger&, FString&)> Move, FPSContractLedger& OutAfter) const
{
    using namespace PSContractManagerPrivate;

    FPSCapPreview Preview;
    Preview.PlayerId = PlayerId;
    const FPSContract* Contract = FindContract(PlayerId);
    if (!Contract)
    {
        Preview.Problem = TEXT("He has no contract");
        return Preview;
    }

    const FName TeamId = Contract->TeamId;
    const int32 Year = Ledger.LeagueYear;
    Preview.TeamId = TeamId;
    Preview.PlayerCapHitBefore = CapHitOf(*Contract, Year);
    Preview.CapSpaceBefore = CapSpaceOf(Tuning, Ledger, TeamId, Year);
    Preview.NextYearCapSpaceBefore = CapSpaceOf(Tuning, Ledger, TeamId, Year + 1);

    OutAfter = Ledger;
    FString Problem;
    if (!Move(OutAfter, Problem))
    {
        Preview.Problem = Problem;
        return Preview;
    }

    Preview.bValid = true;
    Preview.DeadMoneyThisYear = DeadMoneyOf(OutAfter, TeamId, Year, PlayerId) - DeadMoneyOf(Ledger, TeamId, Year, PlayerId);
    Preview.DeadMoneyNextYear = DeadMoneyOf(OutAfter, TeamId, Year + 1, PlayerId) - DeadMoneyOf(Ledger, TeamId, Year + 1, PlayerId);
    const FPSContract* After = FindMutableContract(OutAfter, PlayerId);
    Preview.PlayerCapHitAfter = (After ? CapHitOf(*After, Year) : 0) + Preview.DeadMoneyThisYear;
    Preview.CapSpaceAfter = CapSpaceOf(Tuning, OutAfter, TeamId, Year);
    Preview.NextYearCapSpaceAfter = CapSpaceOf(Tuning, OutAfter, TeamId, Year + 1);
    Preview.bCompliantAfter = Preview.CapSpaceAfter >= 0;
    return Preview;
}

bool UPSContractManager::ApplyCut(FPSContractLedger& InOutLedger, FName PlayerId, bool bSpreadDeadMoney, FString& OutProblem) const
{
    using namespace PSContractManagerPrivate;

    const int32 Index = InOutLedger.Contracts.IndexOfByPredicate([PlayerId](const FPSContract& Contract) { return Contract.PlayerId == PlayerId; });
    if (Index == INDEX_NONE)
    {
        OutProblem = TEXT("He has no contract");
        return false;
    }

    // What is guaranteed or already paid as a bonus is owed either way: dead money.
    const FPSContract Cut = InOutLedger.Contracts[Index];
    const int32 Year = InOutLedger.LeagueYear;
    int32 ThisYear = 0;
    int32 Later = 0;
    for (const FPSContractYear& ContractYear : Cut.Years)
    {
        if (ContractYear.LeagueYear < Year)
        {
            continue;
        }
        const int32 Owed = ContractYear.GuaranteedSalary + ContractYear.ProratedBonus;
        if (ContractYear.LeagueYear == Year || !bSpreadDeadMoney)
        {
            ThisYear += Owed;
        }
        else
        {
            Later += Owed;
        }
    }

    InOutLedger.Contracts.RemoveAt(Index);
    AddDeadMoney(InOutLedger, Cut.TeamId, PlayerId, Year, ThisYear);
    AddDeadMoney(InOutLedger, Cut.TeamId, PlayerId, Year + 1, Later);
    return true;
}

bool UPSContractManager::ApplyRestructure(FPSContractLedger& InOutLedger, FName PlayerId, int32 Amount, FString& OutProblem) const
{
    using namespace PSContractManagerPrivate;

    FPSContract* Contract = FindMutableContract(InOutLedger, PlayerId);
    if (!Contract)
    {
        OutProblem = TEXT("He has no contract");
        return false;
    }
    const int32 YearIndex = Contract->Years.IndexOfByPredicate([&InOutLedger](const FPSContractYear& Year) { return Year.LeagueYear == InOutLedger.LeagueYear; });
    if (YearIndex == INDEX_NONE)
    {
        OutProblem = TEXT("He has no salary this league year");
        return false;
    }

    // Base salary above the minimum turns into a bonus, spread over his remaining years.
    FPSContractYear& Current = Contract->Years[YearIndex];
    const int32 Converted = FMath::Min(Amount, Current.BaseSalary - FMath::Max(0, Tuning.MinimumSalary));
    if (Converted <= 0)
    {
        OutProblem = Amount <= 0 ? TEXT("Convert a positive amount") : TEXT("His base salary is already the minimum");
        return false;
    }
    Current.BaseSalary -= Converted;
    Current.GuaranteedSalary = FMath::Min(Current.GuaranteedSalary, Current.BaseSalary);
    const int32 Remaining = Contract->Years.Num() - YearIndex;
    ProrateBonus(*Contract, YearIndex, FMath::Min(Remaining, FMath::Max(1, Tuning.MaxProrationYears)), Converted);
    Contract->BonusPaid += Converted;
    return true;
}

bool UPSContractManager::ApplyExtension(FPSContractLedger& InOutLedger, FName PlayerId, const FPSContractOffer& Extension, FString& OutProblem) const
{
    using namespace PSContractManagerPrivate;

    FPSContract* Contract = FindMutableContract(InOutLedger, PlayerId);
    if (!Contract)
    {
        OutProblem = TEXT("He has no contract");
        return false;
    }
    const int32 Year = InOutLedger.LeagueYear;
    const int32 FirstIndex = FirstRemainingIndex(*Contract, Year);
    if (FirstIndex == INDEX_NONE)
    {
        OutProblem = TEXT("His deal has run out");
        return false;
    }
    const int32 Added = Extension.Years;
    const int32 Remaining = Contract->Years.Num() - FirstIndex;
    if (Added < 1)
    {
        OutProblem = TEXT("An extension adds at least a year");
        return false;
    }
    if (Remaining + Added > Tuning.MaxContractYears)
    {
        OutProblem = FString::Printf(TEXT("He would be signed for %d years; %d are allowed"), Remaining + Added, Tuning.MaxContractYears);
        return false;
    }
    if (Extension.AnnualValue < Tuning.MinimumSalary)
    {
        OutProblem = TEXT("It pays less than the minimum salary");
        return false;
    }

    const FName TeamId = Contract->TeamId;
    const int32 SpaceBefore = CapSpaceOf(Tuning, InOutLedger, TeamId, Year);
    const int32 Total = Extension.AnnualValue * Added;
    const int32 Bonus = FMath::Clamp(Extension.SigningBonus, 0, Total - Tuning.MinimumSalary * Added);
    const int32 BaseTotal = Total - Bonus;
    const int32 Guaranteed = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Extension.GuaranteedFraction, 0.f, 1.f) * Total) - Bonus, 0, BaseTotal);

    // The new years go after his deal; the new bonus spreads over all his remaining years.
    AppendYears(*Contract, Contract->GetLastYear() + 1, Added, BaseTotal, Guaranteed);
    ProrateBonus(*Contract, FirstIndex, FMath::Min(Remaining + Added, FMath::Max(1, Tuning.MaxProrationYears)), Bonus);
    Contract->BonusPaid += Bonus;

    const int32 SpaceAfter = CapSpaceOf(Tuning, InOutLedger, TeamId, Year);
    if (SpaceAfter < 0 && SpaceAfter < SpaceBefore)
    {
        OutProblem = TEXT("It would take the team over the cap");
        return false;
    }
    return true;
}

FPSCapPreview UPSContractManager::PreviewCut(FName PlayerId, bool bSpreadDeadMoney) const
{
    FPSContractLedger After;
    return PreviewMove(PlayerId, [this, PlayerId, bSpreadDeadMoney](FPSContractLedger& Working, FString& Problem)
    {
        return ApplyCut(Working, PlayerId, bSpreadDeadMoney, Problem);
    }, After);
}

FPSCapPreview UPSContractManager::CutPlayer(FName PlayerId, bool bSpreadDeadMoney)
{
    FPSContractLedger After;
    const FPSCapPreview Preview = PreviewMove(PlayerId, [this, PlayerId, bSpreadDeadMoney](FPSContractLedger& Working, FString& Problem)
    {
        return ApplyCut(Working, PlayerId, bSpreadDeadMoney, Problem);
    }, After);
    if (Preview.bValid)
    {
        Ledger = MoveTemp(After);
    }
    return Preview;
}

FPSCapPreview UPSContractManager::PreviewRestructure(FName PlayerId, int32 Amount) const
{
    FPSContractLedger After;
    return PreviewMove(PlayerId, [this, PlayerId, Amount](FPSContractLedger& Working, FString& Problem)
    {
        return ApplyRestructure(Working, PlayerId, Amount, Problem);
    }, After);
}

FPSCapPreview UPSContractManager::RestructureContract(FName PlayerId, int32 Amount)
{
    FPSContractLedger After;
    const FPSCapPreview Preview = PreviewMove(PlayerId, [this, PlayerId, Amount](FPSContractLedger& Working, FString& Problem)
    {
        return ApplyRestructure(Working, PlayerId, Amount, Problem);
    }, After);
    if (Preview.bValid)
    {
        Ledger = MoveTemp(After);
    }
    return Preview;
}

FPSCapPreview UPSContractManager::PreviewExtension(FName PlayerId, const FPSContractOffer& Extension) const
{
    FPSContractLedger After;
    return PreviewMove(PlayerId, [this, PlayerId, &Extension](FPSContractLedger& Working, FString& Problem)
    {
        return ApplyExtension(Working, PlayerId, Extension, Problem);
    }, After);
}

FPSCapPreview UPSContractManager::ExtendContract(FName PlayerId, const FPSContractOffer& Extension)
{
    FPSContractLedger After;
    const FPSCapPreview Preview = PreviewMove(PlayerId, [this, PlayerId, &Extension](FPSContractLedger& Working, FString& Problem)
    {
        return ApplyExtension(Working, PlayerId, Extension, Problem);
    }, After);
    if (Preview.bValid)
    {
        Ledger = MoveTemp(After);
    }
    return Preview;
}

TArray<FName> UPSContractManager::EnforceCompliance(FName TeamId, UPSRoster* Roster, TArray<FPlayerAttributes>& OutReleased)
{
    TArray<FName> Cuts;
    while (!IsCompliant(TeamId))
    {
        // The cut that frees the most cap per rating point; a player the roster doesn't know first.
        FName Best;
        float BestScore = 0.f;
        int32 BestSavings = 0;
        for (const FPSContract& Contract : Ledger.Contracts)
        {
            if (Contract.TeamId != TeamId)
            {
                continue;
            }
            const FPSCapPreview Preview = PreviewCut(Contract.PlayerId, true);
            const int32 Savings = Preview.CapSpaceAfter - Preview.CapSpaceBefore;
            if (!Preview.bValid || Savings <= 0)
            {
                continue;
            }
            const FPlayerAttributes* Player = Roster ? Roster->FindPlayerPtr(Contract.PlayerId) : nullptr;
            const float Score = (Player ? UPSContractNegotiation::RatePlayer(*Player) : 0.f) / Savings;
            const bool bBetter = Best.IsNone() || Score < BestScore
                || (Score == BestScore && (Savings > BestSavings || (Savings == BestSavings && Contract.PlayerId.LexicalLess(Best))));
            if (bBetter)
            {
                Best = Contract.PlayerId;
                BestScore = Score;
                BestSavings = Savings;
            }
        }
        if (Best.IsNone())
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSContractManager: %s is over the cap and no cut would help."), *TeamId.ToString());
            break;
        }

        CutPlayer(Best, true);
        Cuts.Add(Best);
        FPlayerAttributes Released;
        if (Roster && Roster->RemovePlayer(Best, Released))
        {
            OutReleased.Add(Released);
        }
    }
    return Cuts;
}

FPSLeagueYearRollover UPSContractManager::RolloverLeagueYear()
{
    // Unused space carries over, up to MaxCarryoverFraction of the closing year's cap.
    const int32 MaxCarryover = FMath::RoundToInt(FMath::Max(0.f, Tuning.MaxCarryoverFraction) * Ledger.SalaryCap);
    TArray<FPSCapCarryover> NextCarryover;
    for (const FName& TeamId : Ledger.TeamIds)
    {
        const int32 Carried = FMath::Clamp(GetCapSpace(TeamId), 0, MaxCarryover);
        if (Carried > 0)
        {
            FPSCapCarryover& Entry = NextCarryover.AddDefaulted_GetRef();
            Entry.TeamId = TeamId;
            Entry.Amount = Carried;
        }
    }

    Ledger.LeagueYear += 1;
    Ledger.SalaryCap = FMath::RoundToInt(Ledger.SalaryCap * (1.f + Tuning.CapGrowthRate));
    Ledger.Carryover = MoveTemp(NextCarryover);

    FPSLeagueYearRollover Result;
    const int32 NewYear = Ledger.LeagueYear;
    for (const FPSContract& Contract : Ledger.Contracts)
    {
        if (Contract.GetLastYear() < NewYear)
        {
            FPSExpiredContract& Expired = Result.Expired.AddDefaulted_GetRef();
            Expired.PlayerId = Contract.PlayerId;
            Expired.TeamId = Contract.TeamId;
        }
    }
    Ledger.Contracts.RemoveAll([NewYear](const FPSContract& Contract) { return Contract.GetLastYear() < NewYear; });
    Ledger.DeadMoney.RemoveAll([NewYear](const FPSDeadMoneyEntry& Entry) { return Entry.LeagueYear < NewYear; });

    Result.LeagueYear = NewYear;
    Result.SalaryCap = Ledger.SalaryCap;
    Result.TeamsOverCap = GetTeamsOverCap();
    UE_LOG(LogTemp, Display, TEXT("UPSContractManager: League year %d: cap %d, %d contract(s) expired, %d team(s) over the cap."),
        NewYear, Ledger.SalaryCap, Result.Expired.Num(), Result.TeamsOverCap.Num());
    return Result;
}

void UPSContractManager::SaveTo(UPSFranchiseSaveGame* Save) const
{
    if (Save)
    {
        Save->ContractLedger = Ledger;
    }
}

bool UPSContractManager::LoadFrom(const UPSFranchiseSaveGame* Save)
{
    if (!Save || Save->ContractLedger.LeagueYear <= 0)
    {
        return false;
    }
    Ledger = Save->ContractLedger;
    return true;
}
