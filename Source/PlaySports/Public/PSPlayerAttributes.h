#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.generated.h"

UENUM(BlueprintType)
enum class EPlayerRole : uint8
{
    Quarterback UMETA(DisplayName = "Quarterback"),
    RunningBack UMETA(DisplayName = "Running Back"),
    WideReceiver UMETA(DisplayName = "Wide Receiver"),
    TightEnd UMETA(DisplayName = "Tight End"),
    OffensiveLineman UMETA(DisplayName = "Offensive Lineman"),
    DefensiveLineman UMETA(DisplayName = "Defensive Lineman"),
    Linebacker UMETA(DisplayName = "Linebacker"),
    DefensiveBack UMETA(DisplayName = "Defensive Back")
};

/** Broad character-archetype grouping used for hitpoint/combat tuning (Epic 139):
 *  offensive skill players, defensive skill players, and linemen play by different
 *  combat rules even though they share the finer-grained EPlayerRole. */
UENUM(BlueprintType)
enum class EPlayerArchetypeClass : uint8
{
    OffenseSkill UMETA(DisplayName = "Offense Skill Player"),
    DefenseSkill UMETA(DisplayName = "Defense Skill Player"),
    Lineman UMETA(DisplayName = "Lineman")
};

/** A player's style (Epic 79): how he plays, apart from how well. Each axis runs from -1 to 1
 *  between two styles, 0 being neither. Data/player_dna.json says which roles each axis applies
 *  to, which AI tuning it scales (PSPlayerDNA), and the scouting trait at each end; an axis that
 *  doesn't apply to a player's role is ignored. A roster that gives no DNA plays neutral. */
USTRUCT(BlueprintType)
struct FPSPlayerDNA
{
    GENERATED_BODY()

    /** Quarterback: -1 a pocket passer who stands in and throws, +1 a scrambler who leaves the
     *  pocket early and runs rather than force it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float Mobility = 0.f;

    /** Quarterback: -1 a game manager who throws only to the open man, +1 a gunslinger who fits
     *  it into tight windows and throws before the break. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float Gunslinger = 0.f;

    /** Running back: -1 elusive (veers away from tacklers early), +1 power (runs through them). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float RunPower = 0.f;

    /** Receivers and tight ends: -1 a route technician (sharp breaks, sold fakes), +1 a vertical
     *  threat (rounds his breaks, hurries his fakes). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float RouteStyle = 0.f;

    /** Pass rushers: -1 finesse (swim, rip, spin), +1 power (bull rush, club). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float RushPower = 0.f;

    /** Coverage: -1 blanket (tight on his man, rarely bites), +1 ball hawk (sits off, jumps
     *  throws from further away, bites harder on fakes). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float BallHawk = 0.f;

    /** True when every axis is 0: the player plays every tuning as loaded. */
    bool IsNeutral() const
    {
        return Mobility == 0.f && Gunslinger == 0.f && RunPower == 0.f && RouteStyle == 0.f && RushPower == 0.f && BallHawk == 0.f;
    }
};

USTRUCT(BlueprintType)
struct FPlayerAttributes : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayerRole Role = EPlayerRole::Quarterback;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WeightKg = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HeightCm = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Speed = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Agility = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Strength = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Acceleration = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Awareness = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Stamina = 0.0f;

    /** His age in years (Epic 122); optional in a roster file. 0 means unknown: the contract
     *  manager then uses its tuning's DefaultPlayerAge. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Age = 0;

    /** The number on his jersey, 1-99; optional in a roster file. 0 means he has none yet. A
     *  number is his team's alone. Viewers and overlays label him with it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 JerseyNumber = 0;

    /** His style (Epic 79): optional in a roster file ("DNA": { "Mobility": 0.6 }); missing
     *  axes are 0. tools/player_dna.py generates it from the ratings. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FPSPlayerDNA DNA;
};

USTRUCT(BlueprintType)
struct FMovementTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseMaxSpeedMin = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseMaxSpeedMax = 900.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseAccelerationMin = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseAccelerationMax = 2000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseTurnRateMin = 180.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseTurnRateMaxMultiplier = 6.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WeightMin = 50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WeightTurnRateReference = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DecelerationMultiplier = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CutDivergenceThreshold = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CutSpeedMinMultiplierBase = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CutSpeedAgilityMultiplier = 0.003f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CutSpeedWeightMultiplier = 0.001f;
};
