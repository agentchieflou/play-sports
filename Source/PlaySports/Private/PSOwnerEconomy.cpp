#include "PSOwnerEconomy.h"
#include "PSContractManager.h"
#include "PSDataIngestion.h"
#include "PSFranchiseSaveGame.h"
#include "Misc/Paths.h"

namespace PSOwnerEconomyPrivate
{
    /** Dollars times fans, in thousands of dollars. */
    static int32 ToThousands(float Dollars, int32 Fans)
    {
        return FMath::RoundToInt(Dollars * Fans / 1000.f);
    }
}

FString UPSOwnerEconomy::GetDefaultTuningPath()
{
    return FPaths::ProjectDir() / TEXT("Data/owner_economics.json");
}

bool UPSOwnerEconomy::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSEconomyTuning Loaded;
    if (!Ingestion->LoadEconomyTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOwnerEconomy: Could not load the economy tuning from %s."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOwnerEconomy: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSOwnerEconomy::ValidateTuning(const FPSEconomyTuning& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.StadiumCapacity <= 0)
    {
        Problems.Add(TEXT("StadiumCapacity must be positive"));
    }
    if (InTuning.MinTicketPrice <= 0.f || InTuning.MinTicketPrice > InTuning.BaseTicketPrice || InTuning.BaseTicketPrice > InTuning.MaxTicketPrice)
    {
        Problems.Add(TEXT("Ticket prices must run 0 < MinTicketPrice <= BaseTicketPrice <= MaxTicketPrice"));
    }
    if (InTuning.MinFillRate < 0.f || InTuning.MinFillRate > InTuning.BaseFillRate || InTuning.BaseFillRate > 1.f)
    {
        Problems.Add(TEXT("Fill rates must run 0 <= MinFillRate <= BaseFillRate <= 1"));
    }
    if (InTuning.MediaRevenuePerTeam < 0 || InTuning.ConcessionsPerFan < 0.f)
    {
        Problems.Add(TEXT("MediaRevenuePerTeam and ConcessionsPerFan can't be negative"));
    }
    if (InTuning.StartingSatisfaction < 0.f || InTuning.StartingSatisfaction > 1.f || InTuning.RelocationSatisfactionThreshold < 0.f || InTuning.RelocationSatisfactionThreshold > 1.f)
    {
        Problems.Add(TEXT("StartingSatisfaction and RelocationSatisfactionThreshold are 0-1"));
    }
    if (InTuning.RelocationLosingSeasons < 1)
    {
        Problems.Add(TEXT("RelocationLosingSeasons must be at least 1"));
    }
    const FPSTeamBudget& Budget = InTuning.DefaultBudget;
    if (Budget.ScoutingFraction < 0.f || Budget.TrainingFraction < 0.f || Budget.StaffFraction < 0.f
        || InTuning.MaxBudgetFraction > 1.f || Budget.GetTotal() > InTuning.MaxBudgetFraction)
    {
        Problems.Add(TEXT("DefaultBudget's shares must be 0 or more and total at most MaxBudgetFraction (at most 1)"));
    }
    return Problems;
}

FPSTeamEconomy& UPSOwnerEconomy::FindOrAddTeam(FName TeamId)
{
    if (FPSTeamEconomy* Found = League.Teams.FindByPredicate([TeamId](const FPSTeamEconomy& Team) { return Team.TeamId == TeamId; }))
    {
        return *Found;
    }
    FPSTeamEconomy& Added = League.Teams.AddDefaulted_GetRef();
    Added.TeamId = TeamId;
    Added.TicketPrice = Tuning.BaseTicketPrice;
    Added.FanSatisfaction = Tuning.StartingSatisfaction;
    Added.Budget = Tuning.DefaultBudget;
    return Added;
}

const FPSTeamEconomy* UPSOwnerEconomy::FindTeam(FName TeamId) const
{
    return League.Teams.FindByPredicate([TeamId](const FPSTeamEconomy& Team) { return Team.TeamId == TeamId; });
}

void UPSOwnerEconomy::RegisterTeam(FName TeamId)
{
    if (!TeamId.IsNone())
    {
        FindOrAddTeam(TeamId);
    }
}

bool UPSOwnerEconomy::GetTeam(FName TeamId, FPSTeamEconomy& OutTeam) const
{
    const FPSTeamEconomy* Found = FindTeam(TeamId);
    if (Found)
    {
        OutTeam = *Found;
    }
    return Found != nullptr;
}

float UPSOwnerEconomy::SetTicketPrice(FName TeamId, float Price)
{
    FPSTeamEconomy& Team = FindOrAddTeam(TeamId);
    Team.TicketPrice = FMath::Clamp(Price, Tuning.MinTicketPrice, Tuning.MaxTicketPrice);
    return Team.TicketPrice;
}

bool UPSOwnerEconomy::SetBudget(FName TeamId, const FPSTeamBudget& Budget)
{
    if (Budget.ScoutingFraction < 0.f || Budget.TrainingFraction < 0.f || Budget.StaffFraction < 0.f || Budget.GetTotal() > Tuning.MaxBudgetFraction + KINDA_SMALL_NUMBER)
    {
        return false;
    }
    FindOrAddTeam(TeamId).Budget = Budget;
    return true;
}

int32 UPSOwnerEconomy::PredictAttendance(FName TeamId, float WinPercentage) const
{
    const FPSTeamEconomy* Team = FindTeam(TeamId);
    const float Price = Team ? Team->TicketPrice : Tuning.BaseTicketPrice;
    const float Satisfaction = Team ? Team->FanSatisfaction : Tuning.StartingSatisfaction;
    const float PriceRatio = Tuning.BaseTicketPrice > 0.f ? Price / Tuning.BaseTicketPrice : 1.f;
    const float Fill = Tuning.BaseFillRate
        + Tuning.WinFillWeight * (FMath::Clamp(WinPercentage, 0.f, 1.f) - 0.5f)
        + Tuning.SatisfactionFillWeight * (Satisfaction - 0.5f)
        - Tuning.PriceElasticity * (PriceRatio - 1.f);
    return FMath::RoundToInt(Tuning.StadiumCapacity * FMath::Clamp(Fill, Tuning.MinFillRate, 1.f));
}

FPSGameGate UPSOwnerEconomy::RecordGame(FName HomeTeamId, FName AwayTeamId, int32 HomeScore, int32 AwayScore, float HomeWinPercentage)
{
    using namespace PSOwnerEconomyPrivate;

    // Both teams first, so the references below stay put.
    RegisterTeam(HomeTeamId);
    RegisterTeam(AwayTeamId);

    FPSGameGate Gate;
    Gate.HomeTeamId = HomeTeamId;
    Gate.Attendance = PredictAttendance(HomeTeamId, HomeWinPercentage);

    FPSTeamEconomy& Home = FindOrAddTeam(HomeTeamId);
    Gate.GateRevenue = ToThousands(Home.TicketPrice, Gate.Attendance);
    Gate.ConcessionRevenue = ToThousands(Tuning.ConcessionsPerFan, Gate.Attendance);
    ++Home.HomeGames;
    Home.Attendance += Gate.Attendance;
    Home.GateRevenue += Gate.GateRevenue;
    Home.ConcessionRevenue += Gate.ConcessionRevenue;

    // The fans: a win lifts them, a loss stings, and a price above the base wears on the home crowd.
    const float Overpriced = Tuning.BaseTicketPrice > 0.f ? FMath::Max(0.f, Home.TicketPrice / Tuning.BaseTicketPrice - 1.f) : 0.f;
    Home.FanSatisfaction -= Tuning.PriceSatisfactionLoss * Overpriced;
    if (HomeScore != AwayScore)
    {
        FPSTeamEconomy& Winner = FindOrAddTeam(HomeScore > AwayScore ? HomeTeamId : AwayTeamId);
        Winner.FanSatisfaction += Tuning.WinSatisfactionGain;
        FPSTeamEconomy& Loser = FindOrAddTeam(HomeScore > AwayScore ? AwayTeamId : HomeTeamId);
        Loser.FanSatisfaction -= Tuning.LossSatisfactionLoss;
    }
    for (FPSTeamEconomy& Team : League.Teams)
    {
        Team.FanSatisfaction = FMath::Clamp(Team.FanSatisfaction, 0.f, 1.f);
    }
    return Gate;
}

TArray<FPSEconomySeasonReport> UPSOwnerEconomy::EndSeason(const TArray<FPSTeamStanding>& Standings, const UPSContractManager* Contracts)
{
    TArray<FPSEconomySeasonReport> Reports;
    for (const FPSTeamStanding& Standing : Standings)
    {
        FindOrAddTeam(Standing.TeamId);
    }

    for (FPSTeamEconomy& Team : League.Teams)
    {
        FPSEconomySeasonReport& Report = Reports.AddDefaulted_GetRef();
        Report.TeamId = Team.TeamId;
        Report.GateRevenue = Team.GateRevenue;
        Report.ConcessionRevenue = Team.ConcessionRevenue;
        Report.MediaRevenue = Tuning.MediaRevenuePerTeam;
        Report.Revenue = Report.GateRevenue + Report.ConcessionRevenue + Report.MediaRevenue;
        Report.Payroll = Contracts ? Contracts->GetCapUsed(Team.TeamId, Contracts->GetLeagueYear()) : 0;
        Report.BudgetSpend = FMath::RoundToInt(Team.Budget.GetTotal() * Report.Revenue);
        Report.Profit = Report.Revenue - Report.Payroll - Report.BudgetSpend;
        Report.AverageAttendance = Team.HomeGames > 0 ? Team.Attendance / Team.HomeGames : 0;

        // A winning season lifts the fans, a losing one sinks them; losing seasons in a row
        // with fans who have given up bring relocation pressure.
        const FPSTeamStanding* Record = Standings.FindByPredicate([&Team](const FPSTeamStanding& Standing) { return Standing.TeamId == Team.TeamId; });
        if (Record && Record->Losses > Record->Wins)
        {
            Team.FanSatisfaction -= Tuning.LosingSeasonSatisfactionLoss;
            ++Team.ConsecutiveLosingSeasons;
        }
        else if (Record)
        {
            Team.FanSatisfaction += Record->Wins > Record->Losses ? Tuning.WinningSeasonSatisfactionGain : 0.f;
            Team.ConsecutiveLosingSeasons = 0;
        }
        Team.FanSatisfaction = FMath::Clamp(Team.FanSatisfaction, 0.f, 1.f);
        const bool bWasUnderPressure = Team.bRelocationPressure;
        Team.bRelocationPressure = Team.ConsecutiveLosingSeasons >= Tuning.RelocationLosingSeasons && Team.FanSatisfaction < Tuning.RelocationSatisfactionThreshold;
        Report.FanSatisfaction = Team.FanSatisfaction;
        Report.bRelocationPressure = Team.bRelocationPressure;
        Report.bRelocationPressureStarted = Team.bRelocationPressure && !bWasUnderPressure;
        if (Report.bRelocationPressureStarted)
        {
            UE_LOG(LogTemp, Display, TEXT("UPSOwnerEconomy: %s are under relocation pressure after %d losing seasons."), *Team.TeamId.ToString(), Team.ConsecutiveLosingSeasons);
        }

        Team.LastSeasonRevenue = Report.Revenue;
        Team.HomeGames = 0;
        Team.Attendance = 0;
        Team.GateRevenue = 0;
        Team.ConcessionRevenue = 0;
    }
    ++League.SeasonsCompleted;
    return Reports;
}

int32 UPSOwnerEconomy::GetDepartmentFunding(FName TeamId, EPSBudgetDepartment Department) const
{
    const FPSTeamEconomy* Team = FindTeam(TeamId);
    const FPSTeamBudget& Budget = Team ? Team->Budget : Tuning.DefaultBudget;
    const int32 Base = Team && Team->LastSeasonRevenue > 0 ? Team->LastSeasonRevenue : Tuning.MediaRevenuePerTeam;
    return FMath::RoundToInt(Budget.GetFraction(Department) * Base);
}

float UPSOwnerEconomy::GetFundingIndex(FName TeamId, EPSBudgetDepartment Department) const
{
    if (League.Teams.Num() == 0)
    {
        return 1.f;
    }
    float Sum = 0.f;
    for (const FPSTeamEconomy& Team : League.Teams)
    {
        Sum += GetDepartmentFunding(Team.TeamId, Department);
    }
    const float Average = Sum / League.Teams.Num();
    return Average > 0.f ? GetDepartmentFunding(TeamId, Department) / Average : 1.f;
}

void UPSOwnerEconomy::SaveTo(UPSFranchiseSaveGame* Save) const
{
    if (Save)
    {
        Save->Economy = League;
    }
}

bool UPSOwnerEconomy::LoadFrom(const UPSFranchiseSaveGame* Save)
{
    if (!Save || Save->Economy.Teams.Num() == 0)
    {
        return false;
    }
    League = Save->Economy;
    return true;
}
