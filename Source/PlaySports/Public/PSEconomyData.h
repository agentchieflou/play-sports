// PSEconomyData.h - Epic 95: owner mode, revenue, budgets and fan satisfaction
#pragma once

#include "CoreMinimal.h"
#include "PSEconomyData.generated.h"

// Team money is in thousands of dollars (int32), as the salary cap's is (Epic 87); ticket prices
// and concession spending are in dollars.

/** Where an owner's budget goes: the departments other epics read their funding from. */
UENUM(BlueprintType)
enum class EPSBudgetDepartment : uint8
{
    /** The draft's scouting (Epic 86). */
    Scouting,
    /** Weekly training (Epic 90). */
    Training,
    /** The coaching staff (Epic 89). */
    Staff
};

/** The share of revenue a team spends on each department. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTeamBudget
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    float ScoutingFraction = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    float TrainingFraction = 0.03f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    float StaffFraction = 0.03f;

    float GetFraction(EPSBudgetDepartment Department) const
    {
        return Department == EPSBudgetDepartment::Scouting ? ScoutingFraction : Department == EPSBudgetDepartment::Training ? TrainingFraction : StaffFraction;
    }

    float GetTotal() const { return ScoutingFraction + TrainingFraction + StaffFraction; }
};

/** The league's economics (Data/owner_economics.json, Epic 95). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSEconomyTuning
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Gate")
    int32 StadiumCapacity = 68000;

    /** Ticket prices, in dollars: the league's base, and the range an owner may set. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Gate")
    float BaseTicketPrice = 110.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Gate")
    float MinTicketPrice = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Gate")
    float MaxTicketPrice = 300.f;

    /** The stadium fills to BaseFillRate at the base price for a .500 team its fans are neutral
     *  on; plus WinFillWeight times (win percentage - 0.5), plus SatisfactionFillWeight times
     *  (fan satisfaction - 0.5), less PriceElasticity times (price / base price - 1); never under
     *  MinFillRate nor over capacity. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Gate")
    float BaseFillRate = 0.85f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Gate")
    float WinFillWeight = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Gate")
    float SatisfactionFillWeight = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Gate")
    float PriceElasticity = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Gate")
    float MinFillRate = 0.25f;

    /** What each fan spends inside, in dollars. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Gate")
    float ConcessionsPerFan = 35.f;

    /** Each team's share of the league's media money, a season. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Media")
    int32 MediaRevenuePerTeam = 300000;

    /** Fan satisfaction, 0-1: where a new franchise's fans start, what a win and a loss do, what
     *  each home game at a price above the base costs (times price / base - 1), and a winning or
     *  losing season's swing at its end. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Fans")
    float StartingSatisfaction = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Fans")
    float WinSatisfactionGain = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Fans")
    float LossSatisfactionLoss = 0.03f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Fans")
    float PriceSatisfactionLoss = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Fans")
    float WinningSeasonSatisfactionGain = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Fans")
    float LosingSeasonSatisfactionLoss = 0.08f;

    /** Relocation pressure builds on a team with this many losing seasons in a row and fans
     *  below RelocationSatisfactionThreshold. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Fans")
    float RelocationSatisfactionThreshold = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Fans")
    int32 RelocationLosingSeasons = 3;

    /** The most of its revenue a team may budget, all departments together. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Budget")
    float MaxBudgetFraction = 0.15f;

    /** A new franchise's budget. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Budget")
    FPSTeamBudget DefaultBudget;
};

/** One team's business: its prices, fans, budget and this season's takings. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTeamEconomy
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    FName TeamId;

    /** In dollars. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    float TicketPrice = 0.f;

    /** 0 (furious) to 1 (devoted). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    float FanSatisfaction = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    FPSTeamBudget Budget;

    /** This season so far. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    int32 HomeGames = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    int32 Attendance = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    int32 GateRevenue = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    int32 ConcessionRevenue = 0;

    /** The last finished season's revenue: what this season's budget is a share of. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    int32 LastSeasonRevenue = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    int32 ConsecutiveLosingSeasons = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    bool bRelocationPressure = false;
};

/** Every team's business, as the franchise save keeps it (UPSFranchiseSaveGame). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSLeagueEconomy
{
    GENERATED_BODY()

    /** Seasons finished. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    int32 SeasonsCompleted = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
    TArray<FPSTeamEconomy> Teams;
};

/** One home game's gate. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSGameGate
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    FName HomeTeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    int32 Attendance = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    int32 GateRevenue = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    int32 ConcessionRevenue = 0;
};

/** A team's season on the books. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSEconomySeasonReport
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    FName TeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    int32 GateRevenue = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    int32 ConcessionRevenue = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    int32 MediaRevenue = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    int32 Revenue = 0;

    /** The cap the team's contracts and dead money used this league year (Epic 87). */
    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    int32 Payroll = 0;

    /** Its budget's share of the revenue. */
    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    int32 BudgetSpend = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    int32 Profit = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    int32 AverageAttendance = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    float FanSatisfaction = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    bool bRelocationPressure = false;

    /** The pressure began this season. */
    UPROPERTY(BlueprintReadOnly, Category = "Economy")
    bool bRelocationPressureStarted = false;
};
