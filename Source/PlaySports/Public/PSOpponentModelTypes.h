// PSOpponentModelTypes.h - Epic 78: the human's observed play-calling and how the CPU counters it
#pragma once

#include "CoreMinimal.h"
#include "PSOpponentModelTypes.generated.h"

/** One cell of the human's play-calling history: how many times he called Category in one
 *  situation. Kept for this game and, in the profile save, for his earlier games. */
USTRUCT(BlueprintType)
struct FPSTendencyCell
{
    GENERATED_BODY()

    /** True for his calls with the ball, false for his calls on defense. */
    UPROPERTY(BlueprintReadWrite, Category = "OpponentModel")
    bool bOffense = true;

    UPROPERTY(BlueprintReadWrite, Category = "OpponentModel")
    int32 Down = 1;

    /** Which of the tuning's distance buckets the yards to go fell in (0 = the shortest). */
    UPROPERTY(BlueprintReadWrite, Category = "OpponentModel")
    int32 DistanceBucket = 0;

    /** The offense's personnel package on the field (NAME_None when not known). */
    UPROPERTY(BlueprintReadWrite, Category = "OpponentModel")
    FName Personnel;

    /** The play category he called: Run, ShortPass, ... or Base, Blitz, Prevent. */
    UPROPERTY(BlueprintReadWrite, Category = "OpponentModel")
    FString Category;

    UPROPERTY(BlueprintReadWrite, Category = "OpponentModel")
    int32 Count = 0;
};

/** How specific a tendency read is: the narrowest situation with enough calls behind it. */
UENUM(BlueprintType)
enum class EPSTendencyBasis : uint8
{
    /** Too few calls anywhere: no read. */
    None,
    /** This down, distance and personnel. */
    Exact,
    /** This down and distance, any personnel. */
    DownDistance,
    /** This down, any distance. */
    Down,
    /** Every call on that side. */
    Overall
};

/** The human's tendency in a situation, from what he has called and run. */
USTRUCT(BlueprintType)
struct FPSTendencyRead
{
    GENERATED_BODY()

    /** Each tracked category's share of his calls, summing to 1; empty without a read. */
    UPROPERTY(BlueprintReadOnly, Category = "OpponentModel")
    TMap<FString, float> Shares;

    /** How many calls the read stands on (an earlier game's count at PriorGameWeight). */
    UPROPERTY(BlueprintReadOnly, Category = "OpponentModel")
    float Samples = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "OpponentModel")
    EPSTendencyBasis Basis = EPSTendencyBasis::None;

    /** His most called category in the read, and its share. */
    UPROPERTY(BlueprintReadOnly, Category = "OpponentModel")
    FString TopCategory;

    UPROPERTY(BlueprintReadOnly, Category = "OpponentModel")
    float TopShare = 0.f;
};

/** One counter (Data/opponent_model.json): what the CPU leans to when the human over-calls a
 *  category. */
USTRUCT(BlueprintType)
struct FPSOpponentCounterDef
{
    GENERATED_BODY()

    /** The human's side when he called Observed: true with the ball. The CPU counters on the
     *  other side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    bool bOffense = true;

    /** The category he calls (tracked only when some counter names it). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    FString Observed;

    /** The CPU's category that answers it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    FString Counter;

    /** How much: positive favours Counter the more he over-calls Observed, negative avoids it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    float Weight = 0.f;
};

/** The opponent model's tuning (Data/opponent_model.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSOpponentModelTuning
{
    GENERATED_BODY()

    /** The longest yards to go of each distance bucket but the last, ascending: [3, 7] makes
     *  short (up to 3), medium (up to 7) and long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    TArray<int32> DistanceBuckets;

    /** A read needs at least this many calls behind it; with fewer the CPU doesn't adapt. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    float MinSamples = 4.f;

    /** A call from an earlier game counts this much as one from this game (0 forgets them). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    float PriorGameWeight = 0.5f;

    /** How hard the CPU leans on its read before the half, and after its halftime adjustments
     *  (each times the adaptation dial). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    float FirstHalfStrength = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    float SecondHalfStrength = 1.f;

    /** The quarter the halftime adjustments take effect in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    int32 HalftimeQuarter = 3;

    /** The adaptation dial before the difficulty sets one (0 never adapts, 1 fully). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    float DefaultAdaptationDial = 1.f;

    /** A counter never scales a category's weight outside these. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    float MinMultiplier = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    float MaxMultiplier = 2.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpponentModel")
    TArray<FPSOpponentCounterDef> Counters;
};
