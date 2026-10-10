// PSSituationData.h - Epic 76: the game's leverage moments as data (tempo, two-minute,
// four-minute, victory formation)
#pragma once

#include "CoreMinimal.h"
#include "PSSituationData.generated.h"

/** How fast the offense gets to the line and snaps. */
UENUM(BlueprintType)
enum class EPSTempo : uint8
{
    /** Huddle, then snap with time to spare on the play clock. */
    Huddle,
    /** Call it at the line and snap early. */
    NoHuddle,
    /** Snap as soon as the ball is set; a human's last play is run again. */
    HurryUp,
    /** Run the play clock down before the snap to burn game clock. */
    MilkClock
};

/** Which leverage moment the offense is in, read from its side of the score. */
UENUM(BlueprintType)
enum class EPSGameSituation : uint8
{
    Normal,
    /** End of a half with points to chase: hurry, stop the clock, use the sideline. */
    TwoMinuteDrill,
    /** Leading late: run, stay in bounds, make the defense use its timeouts. */
    FourMinuteOffense,
    /** Leading with few enough seconds left that kneeling ends the half or the game. */
    VictoryFormation
};

/** A play whose only purpose is the clock. The playbook marks one by its PlayCategory
 *  ("Spike", "Kneel"); UPSPlaySimulation resolves it at the snap. */
UENUM(BlueprintType)
enum class EPSClockPlay : uint8
{
    None,
    /** Throw the ball into the ground: an incompletion that stops the clock and costs a down. */
    Spike,
    /** Take a knee: down where he stands, the clock keeps running. */
    Kneel
};

/** What the ball carrier does about the sideline, from the call's situation. */
UENUM(BlueprintType)
enum class EPSBoundaryIntent : uint8
{
    None,
    /** Two-minute drill: get out of bounds to stop the clock. */
    GetOutOfBounds,
    /** Four-minute offense: stay in the field so the clock keeps running. */
    StayInbounds
};

/** One tempo's pacing. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSTempoDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    EPSTempo Tempo = EPSTempo::Huddle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    FString Label;

    /** The play-clock reading the snap comes at. With the game clock running, the clock
     *  authority runs the game clock down by whatever is left above this when the offense
     *  snaps sooner (an accelerated clock: the CPU snaps after a short real-time delay). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    float SnapAtPlayClockSeconds = 12.f;

    /** A human offense in this tempo gets no call screen: its last called play is run again. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    bool bRerunLastCall = false;
};

/** The tempo a situation calls for, with the clock running or stopped. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSituationTempoDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    EPSGameSituation Situation = EPSGameSituation::Normal;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    EPSTempo ClockRunningTempo = EPSTempo::Huddle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    EPSTempo ClockStoppedTempo = EPSTempo::Huddle;
};

/** A play category's weight change in a situation, for the side that calls it, and why. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSituationCategoryWeight
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    EPSGameSituation Situation = EPSGameSituation::Normal;

    /** True for the offense's call, false for the defense's (both read the offense's situation). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    bool bOffense = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    FString Category;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    float Delta = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation")
    FString Reason;
};

/**
 * Situational football tuning (Data/situational_tuning.json; Architecture rule 4). The
 * defaults equal the file. Times are game-clock seconds left in the quarter; yard lines
 * count from the offense's own goal line (0) to the opponent's (100).
 */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSituationalTuning
{
    GENERATED_BODY()

    FPSSituationalTuning();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|Tempo")
    TArray<FPSTempoDef> Tempos;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|Tempo")
    TArray<FPSSituationTempoDef> SituationTempos;

    /** The tempos a human cycles through with the Tempo action, in order. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|Tempo")
    TArray<EPSTempo> HumanTempoCycle;

    /** A spike snaps at this tempo whatever the offense's tempo is. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|Tempo")
    EPSTempo SpikeTempo = EPSTempo::HurryUp;

    /** A kneel snaps at this tempo whatever the offense's tempo is. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|Tempo")
    EPSTempo KneelTempo = EPSTempo::MilkClock;

    /** The end-of-half window: a trailing or tied offense in the 4th quarter, and any
     *  offense in the 2nd, runs the two-minute drill from here. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|TwoMinute")
    float TwoMinuteWindowSeconds = 120.f;

    /** A 4th-quarter offense trailing by more than one score starts hurrying from here. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|TwoMinute")
    float TwoScoreWindowSeconds = 300.f;

    /** The most points one possession can score (touchdown plus two-point conversion). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|TwoMinute")
    int32 OneScorePoints = 8;

    /** In the two-minute drill a running clock under this is stopped: a timeout if one is
     *  left, else a spike. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|TwoMinute")
    float ClockUrgencySeconds = 60.f;

    /** The latest down a spike is spent on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|TwoMinute")
    int32 MaxSpikeDown = 2;

    /** Below this there is no time for a spike and another play: run the play. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|TwoMinute")
    float SpikeMinSeconds = 3.f;

    /** A leading 4th-quarter offense plays four-minute football from here. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|FourMinute")
    float FourMinuteWindowSeconds = 240.f;

    /** The defense facing a four-minute offense calls timeouts on a running clock from here,
     *  when it trails by no more than MaxDeficitToChase. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|FourMinute")
    float DefenseTimeoutWindowSeconds = 180.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|FourMinute")
    int32 MaxDeficitToChase = 16;

    /** Game clock one kneel takes from snap to whistle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|Kneel")
    float KneelPlaySeconds = 2.f;

    /** Game clock that runs between kneels when the defense can't stop it (a milked play
     *  clock). The defense stops one gap per timeout it has left. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|Kneel")
    float KneelPreSnapSeconds = 38.f;

    /** At the end of the 1st half, an offense this deep in its own end with this little
     *  time left kneels rather than risk a turnover. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|Kneel")
    float EndOfHalfKneelSeconds = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|Kneel")
    int32 EndOfHalfKneelMaxYardLine = 30;

    /** A spike or kneel play's weight when the clock calls for it (plays otherwise score 1
     *  plus their situational deltas). When it doesn't, its weight is 0: never called. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|PlayCalling")
    float ClockPlayWeight = 5.f;

    /** Routes that finish toward the sideline, where a catch can step out of bounds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|PlayCalling")
    TArray<FName> SidelineRouteIds;

    /** Routes that finish inside the numbers, where the tackle keeps the clock running. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|PlayCalling")
    TArray<FName> MiddleRouteIds;

    /** Two-minute drill weight change for a play with a sideline route ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|PlayCalling")
    float SidelinePlayDelta = 1.f;

    /** ... and for a pass play whose routes all finish in the middle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|PlayCalling")
    float MiddlePlayDelta = -0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Situation|PlayCalling")
    TArray<FPSSituationCategoryWeight> CategoryWeights;
};

namespace PSSituation
{
    /** The PlayCategory that marks a playbook play as a spike / a kneel. */
    inline const TCHAR* SpikeCategory() { return TEXT("Spike"); }
    inline const TCHAR* KneelCategory() { return TEXT("Kneel"); }

    inline EPSClockPlay ClockPlayFromCategory(const FString& Category)
    {
        if (Category == SpikeCategory())
        {
            return EPSClockPlay::Spike;
        }
        if (Category == KneelCategory())
        {
            return EPSClockPlay::Kneel;
        }
        return EPSClockPlay::None;
    }
}
