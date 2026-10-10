#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PSRulesConfig.generated.h"

UCLASS(Blueprintable)
class PLAYSPORTS_API UPSRulesConfig : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
    int32 TouchdownPoints = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
    int32 SafetyPoints = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
    int32 FieldGoalPoints = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
    float PATSuccessChance = 0.94f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring")
    float TwoPointSuccessChance = 0.45f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penalties")
    int32 OffsidesYardage = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penalties")
    int32 HoldingYardage = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
    int32 MaxTimeoutsPerHalf = 3;

    /** A carrier out of bounds stops the clock until the snap inside the last this-many
     *  seconds of the 2nd quarter (Epic 76); earlier it restarts like a tackle in bounds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
    float OutOfBoundsStopsClockFirstHalfSeconds = 120.f;

    /** The same for the 4th quarter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
    float OutOfBoundsStopsClockSecondHalfSeconds = 300.f;

    /** Game clock a tackle in bounds runs off in quick-sim mode, which has no pre-snap time;
     *  a played game runs the real play clock instead, at the call's tempo (Epic 76). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
    float QuickSimTackleRunoffSeconds = 30.f;

    /** Yards a kneel loses. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
    int32 KneelYardage = -1;

    /** Game clock a kneel takes from snap to whistle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
    float KneelPlaySeconds = 2.f;

    /** Game clock a spike takes from snap to whistle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
    float SpikePlaySeconds = 1.f;

    /** No punting is allowed in this ruleset (Epic 140): 4th down is always go-for-it
     *  or a field-goal attempt. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules|Combat")
    bool bAllowPunting = false;

    /** The defense fields extra defenders on 4th down (Epic 140), since there is no
     *  punt/change-of-possession safety valve to discourage 4th-down attempts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules|Combat")
    bool bFourthDownDefensiveOverload = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules|Combat")
    int32 NumExtraDefendersOnFourthDown = 1;
};

/** How many defenders should be on the field for the given down. 11 on downs 1-3;
 *   11 + NumExtraDefendersOnFourthDown on 4th down when bFourthDownDefensiveOverload
 *  is set (Epic 140) -- there is no punt safety valve to discourage 4th-down
 *  attempts, so the defense gets reinforcements instead. RulesConfig may be null
 *  (defaults apply). */
PLAYSPORTS_API int32 GetDefensivePersonnelCount(int32 Down, const UPSRulesConfig* RulesConfig);

/** Whether a carrier going out of bounds keeps the clock stopped until the snap: inside the
 *  last OutOfBoundsStopsClock*Seconds of the 2nd or 4th quarter. RulesConfig may be null
 *  (defaults apply). */
PLAYSPORTS_API bool DoesOutOfBoundsStopClock(int32 Quarter, float GameClockSeconds, const UPSRulesConfig* RulesConfig);
