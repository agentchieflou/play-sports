// PSDraftData.h - Epic 86: the draft, scouting and the combine
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSDraftData.generated.h"

/** A combine drill (Data/draft.json): a prospect's result is Base + PerPoint x his true rating in
 *  Attribute, plus a draw of Noise. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSCombineDrill
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    FName DrillId;

    /** "40-yard dash (s)" -- shown on the prospect's card. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    FString Label;

    /** An FPlayerAttributes rating: Speed, Agility, Strength, Acceleration, Awareness or Stamina. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    FName Attribute;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    float Base = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    float PerPoint = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    float Noise = 0.f;
};

/** The draft (Data/draft.json, Epic 86). Grades are the contract market's overall rating
 *  (UPSContractNegotiation::RatePlayer, 0-100); money is in thousands of dollars, as the cap's.
 *  The defaults equal the file's, but for the drills. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSDraftTuning
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    int32 NumRounds = 7;

    /** The public projection of a prospect's grade is his true grade plus a hidden error: a draw of
     *  PublicUncertainty (grade points; CombineCertainty times it for a combine attendee), and for
     *  BoomBustChance of the class a further BoomBustSwing either way: the busts and the booms. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Prospects")
    float PublicUncertainty = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Prospects")
    float CombineCertainty = 0.6f;

    /** The share of a class that skips the combine for a pro day: their measurables are seen only
     *  by teams that scout them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Prospects")
    float ProDayShare = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Prospects")
    float BoomBustChance = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Prospects")
    float BoomBustSwing = 12.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Prospects")
    TArray<FPSCombineDrill> CombineDrills;

    /** Scouting: a team gets PointsPerSeason points at the league's average scouting funding (the
     *  owner economy's index, times FundingFloor + (1 - FundingFloor) x it, at most
     *  MaxFundingMultiplier); a report costs ReportCost and reads his true grade within a draw of
     *  ReportNoise, or, MisleadChance of the time, a further MisleadSwing off either way. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Scouting")
    float PointsPerSeason = 60.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Scouting")
    float ReportCost = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Scouting")
    float ReportNoise = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Scouting")
    float MisleadChance = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Scouting")
    float MisleadSwing = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Scouting")
    float FundingFloor = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Scouting")
    float MaxFundingMultiplier = 1.5f;

    /** A team's range on a prospect: its estimate, plus or minus this many of its uncertainty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Scouting")
    float RangeSigmas = 1.f;

    /** The CPU spreads its points over the AIScoutTargets best-projected prospects. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|AI")
    int32 AIScoutTargets = 20;

    /** A pick's value: the team's estimate of his grade, plus NeedWeight times its need at his role
     *  (0 with its roster target met, 1 with none of the role; the contract market's RosterTarget). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|AI")
    float NeedWeight = 6.f;

    /** Rookie contracts: RookieYears years; the first pick's annual value FirstPickSalary, falling
     *  to the minimum salary at the last pick as (1 - t)^RookieScaleExponent (t the pick's share of
     *  the way); guarantees from FirstPickGuarantee to LastPickGuarantee. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Rookies")
    int32 RookieYears = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Rookies")
    int32 FirstPickSalary = 9000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Rookies")
    float RookieScaleExponent = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Rookies")
    float FirstPickGuarantee = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft|Rookies")
    float LastPickGuarantee = 0.1f;
};

/** One drill's result. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSCombineResult
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    FName DrillId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    float Value = 0.f;
};

/** A prospect, as the draft keeps him. Player and TrueGrade are hidden: no team sees them until he
 *  plays (GetProspectView shows a team what it knows). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSProspect
{
    GENERATED_BODY()

    /** His true ratings: the ones he plays with once drafted. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    FPlayerAttributes Player;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    float TrueGrade = 0.f;

    /** What everyone sees: his projected grade and its uncertainty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    float Projection = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    float PublicUncertainty = 0.f;

    /** At the combine (measurables public), or a pro day (seen by teams that scout him). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    bool bAttendedCombine = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    TArray<FPSCombineResult> Measurables;

    /** Who took him, and with which pick (0 while undrafted). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    FName DraftedBy;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    int32 OverallPick = 0;
};

/** What a team's scouts have filed on a prospect. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSScoutingRecord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    int32 Reports = 0;

    /** The reports' reads of his grade, summed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    float SumReads = 0.f;
};

/** A team's scouting department for a class. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTeamScouting
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    bool bUserControlled = false;

    /** Its scouting funding against the league's average when it joined (Epic 95). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    float FundingIndex = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    float PointsTotal = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    float Points = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    TArray<FPSScoutingRecord> Records;
};

/** One pick. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSDraftPick
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    int32 Overall = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    int32 Round = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    int32 PickInRound = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    FName TeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    FName PlayerId;

    /** What the team thought he was, and what he is (shown once he is drafted). */
    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    float Estimate = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    float TrueGrade = 0.f;

    /** His rookie contract: signed with the contract manager, at this annual value. */
    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    bool bSigned = false;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    int32 AnnualValue = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    FString Description;
};

/** The draft, as the franchise save keeps it (UPSFranchiseSaveGame). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSDraftState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    int32 Seed = 0;

    /** 0 before a class is prepared. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    int32 DraftYear = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    bool bOpen = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    bool bComplete = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    TArray<FPSProspect> Prospects;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    TArray<FPSTeamScouting> Teams;

    /** The team making each pick, in order. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    TArray<FName> Order;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Draft")
    TArray<FPSDraftPick> Picks;
};

/** A prospect as one team sees him: public facts, the public projection, and its own estimate
 *  and range from its reports. Never his true ratings. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSProspectView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    FName PlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    FString DisplayName;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    EPlayerRole Role = EPlayerRole::Quarterback;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    int32 Age = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    float WeightKg = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    float HeightCm = 0.f;

    /** His style (Epic 79): what tape shows. */
    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    FPSPlayerDNA DNA;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    float Projection = 0.f;

    /** The team's estimate of his grade, and its range. */
    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    float Estimate = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    float RangeLow = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    float RangeHigh = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    int32 Reports = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    bool bAttendedCombine = true;

    /** The combine's results; a pro day's only once the team has scouted him. */
    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    TArray<FPSCombineResult> Measurables;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    FName DraftedBy;

    UPROPERTY(BlueprintReadOnly, Category = "Draft")
    int32 OverallPick = 0;
};
