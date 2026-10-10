// PSCoverageMatchupTypes.h - Epic 69: the coverage matchup engine's data
#pragma once

#include "CoreMinimal.h"
#include "PSCoverageMatchupTypes.generated.h"

/** Which side of his man a cover defender plays: inside (toward the ball) or outside (toward
 *  the receiver's sideline). */
UENUM(BlueprintType)
enum class EPSLeverage : uint8
{
    Inside,
    Outside
};

/** The job of a man-coverage defender left over when the defense has more cover men than the
 *  offense has receivers. */
UENUM(BlueprintType)
enum class EPSFreeRole : uint8
{
    None,
    /** Over the top in the deep middle. */
    DeepMiddle,
    /** In the intermediate middle, jumping the routes that cross it. */
    Robber
};

/** How a coverage shell (FPSPlayDefinition::CoverageShell) plays its man defenders. */
USTRUCT(BlueprintType)
struct FPSCoverageShellRule
{
    GENERATED_BODY()

    /** The shell this rule is for ("Cover1", "ManFree", ...). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coverage")
    FString Shell;

    /** The side of his man each man defender plays. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coverage")
    EPSLeverage Leverage = EPSLeverage::Inside;

    /** Its man defenders may press a receiver at the line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coverage")
    bool bPress = false;

    /** The roles its left-over man defenders take, in turn; the rest play where they stand. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coverage")
    TArray<EPSFreeRole> FreeRoles;
};

/** The coverage matchup engine's tuning (Data/coverage_matchups.json; Architecture rule 4).
 *  Distances in cm, chances 0-1. */
USTRUCT(BlueprintType)
struct FPSCoverageMatchupTuning
{
    GENERATED_BODY()

    // --- Press ---

    /** A pressing defender lines up this far in front of his receiver ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Press")
    float PressDepth = 120.f;

    /** ... and this far to his leverage side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Press")
    float PressShade = 60.f;

    /** A back lined up within this distance across the field of a receiver is the one over him. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Press")
    float PressAlignWidth = 400.f;

    /** The CPU presses only when its defender's chance to win the jam (one minus the receiver's
     *  release chance, Epic 68) is at least this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Press")
    float PressMinJamChance = 0.4f;

    /** Walking up to press, a defender this close to his spot is there. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Press")
    float PreSnapArrivalRadius = 40.f;

    /** A defender who won his jam trails his man this close (in place of the defense AI's
     *  ManCushion). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Press")
    float PressCushion = 60.f;

    /** A presser the receiver beats at the line is out of phase this long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Press")
    float PressBeatenSeconds = 0.4f;

    // --- Leverage ---

    /** A man defender plays this far to his leverage side of his man. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leverage")
    float LeverageShade = 75.f;

    /** The receiver this far across the defender's face, to the leverage side, has taken the
     *  leverage ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leverage")
    float LeverageLostMargin = 50.f;

    /** ... and the defender this far back on his side has it again. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leverage")
    float LeverageRegainMargin = 25.f;

    /** A double move's fake toward the defender's leverage side bites this much more often (he
     *  is sitting on that break); one away from it this much less. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leverage")
    float LeverageBiteBonus = 0.15f;

    /** A break across the field turns this much (0-1, the leg's sideways share) or more. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leverage")
    float BreakMinLateral = 0.3f;

    /** A break into the defender's leverage keeps this share of its separation (he is sitting
     *  on it) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leverage")
    float IntoLeverageSeparationScale = 0.f;

    /** ... and one away from it gains this much more. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leverage")
    float AwayFromLeverageBonus = 40.f;

    /** A defender makes up lost separation this fast: the separation a break gains over this is
     *  how long he is out of phase ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leverage")
    float SeparationRecoverySpeed = 500.f;

    /** ... up to this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leverage")
    float MaxOutOfPhaseSeconds = 0.6f;

    // --- Zones ---

    /** A zone defender keeps the receiver he carries until he is this far outside the zone
     *  (the defense AI's ZoneRadius) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zones")
    float CarryMargin = 150.f;

    /** ... playing this far downfield of him. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zones")
    float ZoneCarryCushion = 150.f;

    /** A receiver this far past a zone defender's spot, leaving the zone, is going vertical:
     *  carried on until a deeper zone takes him. One leaving it any other way is passed off. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zones")
    float VerticalCarryDepth = 100.f;

    // --- Safety help ---

    /** A zone this far past the line is a deep zone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safeties")
    float DeepZoneDepth = 1100.f;

    /** A deep defender stays this far deeper than the deepest receiver in his area ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safeties")
    float OverTopCushion = 300.f;

    /** ... which reaches this far either side of his spot ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safeties")
    float DeepHelpWidth = 1200.f;

    /** ... and shades this share of the way across to that receiver. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safeties")
    float DeepShadeWeight = 0.5f;

    /** The field's width: the deep defenders left after one leaves the deep zones split it
     *  evenly between them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safeties")
    float FieldWidth = 4877.f;

    /** A free deep-middle defender plays this far past the line, in the middle of the field. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safeties")
    float FreeDeepDepth = 1500.f;

    /** A robber sits this far past the line, in the middle of the field ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safeties")
    float RobberDepth = 800.f;

    /** ... reads the receivers within this distance of his spot ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safeties")
    float RobberRadius = 700.f;

    /** ... and jumps the nearest of them this share of the way. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safeties")
    float RobberJumpWeight = 0.8f;

    // --- Pass interference ---

    /** A defender this close to the targeted receiver while the ball is in the air is in
     *  contact with him ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interference")
    float ContactRadius = 90.f;

    /** ... and playing through him, not the ball, when this much further from where it comes
     *  down than the receiver is. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interference")
    float TrailMargin = 50.f;

    /** The officials flag interference this often. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interference")
    float FlagChance = 0.85f;

    // --- Shells ---

    /** How each coverage shell plays its man defenders. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shells")
    TArray<FPSCoverageShellRule> Shells;

    /** The rule for a shell not listed (and a defense with no call). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shells")
    FPSCoverageShellRule DefaultShell;
};
