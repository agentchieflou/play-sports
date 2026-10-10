#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSBallResolutionHelpers.generated.h"

/**
 * Tuning row for the catch/interception/fumble-recovery probability formulas
 * extracted from APSBall::OnBallOverlap (Epic C4). Defaults match the values
 * that were previously hardcoded inline, so behavior is unchanged unless a
 * DataTable/JSON override is supplied (AGENTS.md rule 4: tuning lives in
 * DataTables, not magic numbers in code).
 */
USTRUCT(BlueprintType)
struct FCatchTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CatchBaseChance = 0.50f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CatchAttributeScalar = 0.0025f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CatchChanceMin = 0.10f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CatchChanceMax = 0.95f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InterceptionBaseChance = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InterceptionAttributeScalar = 0.001f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InterceptionChanceMin = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InterceptionChanceMax = 0.40f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float FumbleRecoveryBaseChance = 0.60f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float FumbleRecoveryAttributeScalar = 0.002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float FumbleRecoveryChanceMin = 0.20f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float FumbleRecoveryChanceMax = 0.98f;
};

/**
 * Tuning for a tackle's chance of success, extracted from
 * UPSBallActionComponent::ResolveTackle (Epic 104.2) with the defaults it had inline:
 * chance = Base + (DefenderPower - CarrierPower) * PowerScalar, clamped to [Min, Max], where
 * each side's power is its weighted Strength/Agility plus weighted speed (cm/s).
 */
USTRUCT(BlueprintType)
struct FTackleTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TackleBaseChance = 0.50f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PowerScalar = 0.005f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TackleChanceMin = 0.10f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TackleChanceMax = 0.95f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DefenderStrengthWeight = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DefenderSpeedWeight = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CarrierStrengthWeight = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CarrierAgilityWeight = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CarrierSpeedWeight = 0.05f;
};

/**
 * Pure, deterministic probability/resolution functions extracted from
 * APSBall::OnBallOverlap so the catch/interception/fumble rules are unit
 * testable without a World (Epic C4).
 */
namespace PSBallResolutionHelpers
{
    float ComputeCatchChance(const FPlayerAttributes& Attributes, const FCatchTuningRow& Tuning = FCatchTuningRow());

    float ComputeInterceptionChance(const FPlayerAttributes& Attributes, const FCatchTuningRow& Tuning = FCatchTuningRow());

    float ComputeFumbleRecoveryChance(const FPlayerAttributes& Attributes, const FCatchTuningRow& Tuning = FCatchTuningRow());

    /** Roll <= ComputeCatchChance(Attributes, Tuning). Deterministic given Roll. */
    bool ResolveCatch(const FPlayerAttributes& Attributes, float Roll, const FCatchTuningRow& Tuning = FCatchTuningRow());

    /** A tackle's chance of success. CarrierMoveMultiplier is the carrier's active move
     *  (UPSCarrierMoveComponent::GetTackleChanceMultiplier, Epic 104.2): it scales the clamped
     *  chance, so a good move can beat the floor; the result never exceeds the maximum. */
    float ComputeTackleChance(const FPlayerAttributes& Carrier, const FPlayerAttributes& Defender, float CarrierSpeed, float DefenderSpeed,
        float CarrierMoveMultiplier = 1.f, const FTackleTuningRow& Tuning = FTackleTuningRow());
}
