// PSPlayCallTypes.h - Epic 102: who called which play, and the play-call tuning
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlaybookData.h"
#include "PSSituationData.h"
#include "PSPlayCallTypes.generated.h"

/** Who made a side's call. */
UENUM(BlueprintType)
enum class EPSPlayCaller : uint8
{
    None,
    Human,
    CPU,
    /** Called for a human who ran low on play clock: the top suggestion (Epic 102.5). */
    QuickCall
};

/** One side's call for the coming snap. */
USTRUCT(BlueprintType)
struct FPSPlayCall
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    FName PlayId;

    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    EPSPlayCaller Caller = EPSPlayCaller::None;

    /** The offense's tempo for the snap (Epic 76); always Huddle on the defense's call. */
    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    EPSTempo Tempo = EPSTempo::Huddle;

    bool IsSet() const { return Caller != EPSPlayCaller::None; }
};

/** Play-call timing (Data/play_call.json), so the pace of the game is data (rule 4). */
USTRUCT(BlueprintType)
struct FPlayCallTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** Seconds a CPU offense waits after both calls are in before it snaps, so the call
     *  can be seen and the field settles. A human offense snaps when its player hikes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    float CpuSnapDelaySeconds = 2.f;

    /** When the play clock reaches this with a human's side still uncalled, the top
     *  suggestion is called for them (it then snaps like a CPU call, after the delay above). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    float QuickCallPlayClockSeconds = 5.f;

    /** How many recent calls the Recent plays screen lists. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    int32 RecentPlaysShown = 5;
};

/** A play a human called and ran, kept for the recent list and the tendency readout. */
USTRUCT(BlueprintType)
struct FPSPlayCallRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    FName PlayId;

    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    FString PlayCategory;

    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    bool bOffense = true;

    /** The team that called it: the home team's when its side had the ball (for the
     *  offense's call) or didn't (the defense's). In a head-to-head game each player's recent
     *  list and tendencies are their own team's (Epic 107). */
    UPROPERTY(BlueprintReadOnly, Category = "PlayCall")
    bool bHomeTeam = true;
};

/** A pre-snap defensive adjustment (102.4): every defender of Role plays Kind on top of the
 *  called play, e.g. "Send the linebacker" turns the LBs' assignment into a blitz. */
USTRUCT(BlueprintType)
struct FPSDefensiveAdjustmentDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    FName AdjustmentId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    FString Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    FString Description;

    /** A defensive role: DefensiveLineman, Linebacker or DefensiveBack. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    EPlayerRole Role = EPlayerRole::Linebacker;

    /** A defensive assignment: PassRush, Blitz, RunFit, ManCoverage or ZoneCoverage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    EPSAssignmentKind Kind = EPSAssignmentKind::Blitz;
};

/** Top-level shape of Data/defensive_adjustments.json. */
USTRUCT(BlueprintType)
struct FPSDefensiveAdjustmentCatalog
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayCall")
    TArray<FPSDefensiveAdjustmentDef> Adjustments;
};

