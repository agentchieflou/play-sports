// PSStaffData.h - Epic 89: coaches, schemes and the coaching carousel's data
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSStaffData.generated.h"

/** The job a coach holds (or last held). */
UENUM(BlueprintType)
enum class EPSCoachRole : uint8
{
    HeadCoach,
    OffensiveCoordinator,
    DefensiveCoordinator
};

/** What happened to a coach between seasons. */
UENUM(BlueprintType)
enum class EPSCarouselAction : uint8
{
    Fired,
    Hired,
    /** A winning team's coordinator hired away as another team's head coach. */
    Promoted
};

/** One attribute a scheme asks of a position, and how much it counts. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSchemeFitWeight
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    EPlayerRole Role = EPlayerRole::OffensiveLineman;

    /** An FPlayerAttributes rating: Speed, Agility, Strength, Acceleration, Awareness or Stamina. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FName Attribute;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    float Weight = 1.f;
};

/**
 * A scheme: what a coordinator runs. Its formations pick the side's plays out of the playbook
 * (Data/sample_playbook.json), its category weights lean the play calling (the coaching AI's
 * tendency, by PlayCategory: "Run", "ShortPass", ... or "Base", "Blitz", "Prevent"), and its fit
 * weights say which players it suits: a power scheme wants strong linemen, a zone scheme agile ones.
 */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSchemeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FName SchemeId;

    /** "West Coast" -- shown on the call screen and in the play-call reasons. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FString Label;

    /** An offensive scheme (an offensive coordinator's); else a defensive one. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    bool bOffense = true;

    /** The playbook formations the scheme runs, on its side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    TArray<FString> Formations;

    /** PlayCategory -> weight (1 = neutral) before the coordinator's play calling scales it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    TMap<FString, float> CategoryWeights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    TArray<FPSSchemeFitWeight> FitWeights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FString Description;
};

/** A coach. Head coaches set a team's aggression and bring their scheme (either side's) to the
 *  coordinator they hire; coordinators run theirs. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSCoachDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FName CoachId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    EPSCoachRole Role = EPSCoachRole::HeadCoach;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FName SchemeId;

    /** 0-100: how faithfully a coordinator's calls follow the scheme (its category weights). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff", meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float PlayCalling = 50.f;

    /** 0-100: how well a coordinator teaches the scheme to the players who don't fit it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff", meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float Development = 50.f;

    /** 0-1: a head coach's appetite for risk (the coaching AI's AggressionScore). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Aggression = 0.5f;
};

/** A team's staff. A vacant job is None. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTeamStaffDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FName HeadCoachId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FName OffensiveCoordinatorId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FName DefensiveCoordinatorId;

    /** Seasons completed under the current head coach. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    int32 HeadCoachSeasons = 0;
};

/** Scheme fit and carousel tuning. The defaults equal Data/coaching_staffs.json's. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSStaffTuning
{
    GENERATED_BODY()

    /** A coordinator's scheme adherence at play calling 0 and at 100: how far the scheme's
     *  category weights move from 1 (1 + (Weight - 1) x adherence). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Scheme")
    float MinSchemeAdherence = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Scheme")
    float MaxSchemeAdherence = 1.25f;

    /** Rating points a player's fit in a scheme must sit above (or below) his average fit across
     *  the side's schemes to fit it fully (or misfit it fully). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Fit")
    float FitSpan = 10.f;

    /** The rating multiplier at a full fit and at a full misfit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Fit")
    float BestFitMultiplier = 1.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Fit")
    float WorstFitMultiplier = 0.9f;

    /** The share of a misfit's penalty a coordinator with development 100 teaches away. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Fit")
    float DevelopmentMisfitRelief = 0.5f;

    /** The fit (-1..1) beyond which a player is called a fit or a misfit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Fit")
    float FitLabelThreshold = 0.25f;

    /** A head coach under this win percentage is fired once past his grace seasons. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Carousel")
    float FireWinPercentage = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Carousel")
    int32 GraceSeasons = 1;

    /** The coordinators of the league's worst offenses (fewest points) and defenses (most points
     *  allowed) are fired, this many a side, unless their team won at least
     *  CoordinatorSafeWinPercentage of its games. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Carousel")
    int32 CoordinatorFiresPerSide = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Carousel")
    float CoordinatorSafeWinPercentage = 0.5f;

    /** A coordinator whose team won at least this share can be hired away as a head coach. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Carousel")
    float PromoteWinPercentage = 0.65f;

    /** Added to a winning coordinator's rating as a head-coach candidate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Carousel")
    float PromotionBonus = 5.f;

    /** Added to a coordinator candidate who runs the head coach's scheme. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff|Carousel")
    float SchemeMatchBonus = 10.f;
};

/** Data/coaching_staffs.json: the schemes, every coach (employed or free), each team's staff
 *  and the tuning. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSCoachingLeague
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    TArray<FPSSchemeDef> Schemes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    TArray<FPSCoachDef> Coaches;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    TArray<FPSTeamStaffDef> Staffs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Staff")
    FPSStaffTuning Tuning;
};

/** How a player fits the scheme his side's coordinator runs. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSchemeFitResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    FName PlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    FName SchemeId;

    /** -1 (a full misfit) .. 1 (a full fit); 0 when the scheme asks nothing of his position. */
    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    float Fit = 0.f;

    /** What his ratings are multiplied by in the scheme. */
    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    float Multiplier = 1.f;

    /** "Misfit in the Power Run", "Fits the Zone Run", or empty. */
    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    FString Description;
};

/** One move in the coaching carousel. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSCarouselEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    EPSCarouselAction Action = EPSCarouselAction::Fired;

    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    FName TeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    FName CoachId;

    /** The job: the one lost, or the one taken. */
    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    EPSCoachRole Role = EPSCoachRole::HeadCoach;

    /** For a hire: the scheme the job ran before the carousel, and the one it runs now. */
    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    FName PreviousSchemeId;

    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    FName NewSchemeId;

    UPROPERTY(BlueprintReadOnly, Category = "Staff")
    FString Description;
};

namespace PSStaff
{
    /** Quarterbacks to linemen on offense; the rest play defense. */
    PLAYSPORTS_API bool IsOffensiveRole(EPlayerRole Role);

    /** The coordinator job on a side. */
    PLAYSPORTS_API EPSCoachRole CoordinatorRole(bool bOffense);

    /** A player's rating by name (see FPSSchemeFitWeight::Attribute); false for another name. */
    PLAYSPORTS_API bool GetAttribute(const FPlayerAttributes& Player, FName Attribute, float& OutValue);

    /** "head coach", "offensive coordinator", "defensive coordinator". */
    PLAYSPORTS_API const TCHAR* DescribeRole(EPSCoachRole Role);
}
