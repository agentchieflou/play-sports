// PSPlayDemoTypes.h - live-play demos: real plays recorded from the running game (Track S checkpoint)
#pragma once

#include "CoreMinimal.h"
#include "PSPlayDemoTypes.generated.h"

/** One demo play: the match, the calls both sides make, and the seed it runs on. */
USTRUCT(BlueprintType)
struct FPSPlayDemoDef
{
    GENERATED_BODY()

    /** Unique; names the play's file. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    FName DemoId;

    /** What the demo sets out to show ("Inside run"). Its title says what actually happened. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    FString Intent;

    /** The home team has the ball: first and ten at its own 20, as the match kicks off. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    FName HomeTeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    FName AwayTeamId;

    /** The offense's call, from the playbook (Data/sample_playbook.json); the home team's scheme
     *  must keep it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    FName OffensePlayId;

    /** The defense's call; the away team's scheme must keep it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    FName DefensePlayId;

    /** The match seed (UPSNetRandomStreams::SetMatchSeed): every contest's roll in the play. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    int32 Seed = 1;

    /** With WantedOutcome, how many seeds (Seed, Seed + 1, ...) may be played until one ends
     *  that way. The first that does is kept, else the last. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    int32 SeedTries = 1;

    /** An outcome the demo looks for (FPSPlayDemoSummary::Outcome: Run, Completion, Incompletion,
     *  Sack, Interception, Touchdown); empty takes whatever the first seed gives. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    FString WantedOutcome;
};

/** The demo set and how it is recorded (Data/play_demos.json). Defaults equal the file. */
USTRUCT(BlueprintType)
struct FPSPlayDemoCatalog
{
    GENERATED_BODY()

    /** The world steps at this fixed rate, and every step's frame is recorded. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    float FrameRateHz = 30.f;

    /** A play that hasn't snapped this long after kickoff is a failure. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    float MaxPreSnapSeconds = 15.f;

    /** A play with no whistle this long after the snap is a failure. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    float MaxPlaySeconds = 30.f;

    /** Frames are recorded until this long after the whistle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    float PostWhistleSeconds = 1.f;

    /** The play's result must be on the bus this long after the whistle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    float MaxResultWaitSeconds = 10.f;

    /** Every player must move at least this far from where he stood at the snap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    float MinPlayerMoveCm = 50.f;

    /** Above the movement data's top speed (FMovementTuningRow::BaseMaxSpeedMax), what a player
     *  may reach in a designed burst (a carrier's truck, a blocker's push). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    float SpeedAllowanceCmPerSec = 150.f;

    /** How far below the ground a capsule's or the ball's bottom may dip before it counts as
     *  going through it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    float GroundToleranceCm = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Demo")
    TArray<FPSPlayDemoDef> PlayDemos;
};

/** One recorded play, as index.json lists it: what was called, what happened, how it was
 *  recorded, and what its sanity checks found. */
USTRUCT(BlueprintType)
struct FPSPlayDemoSummary
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName DemoId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString Intent;

    /** What happened, in words, from the play's result ("Inside Zone Run: K. Trample runs for 6
     *  yards, tackled by D. Ward"). */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString Title;

    /** The recording's file name in the index's directory. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString File;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    int32 Seed = 0;

    /** Seeds played to find WantedOutcome (1 without one). */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    int32 SeedsTried = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString WantedOutcome;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName HomeTeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName AwayTeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName OffenseTeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName DefenseTeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName OffensePlayId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString OffensePlayName;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString OffenseFormation;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString OffenseCategory;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName DefensePlayId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString DefensePlayName;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString DefenseFormation;

    /** Who called the plays: "Demo" (the demo named both calls, through the play-call
     *  subsystem as the call screens do). */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString CalledBy;

    /** The situation at the snap. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    int32 Down = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    int32 Distance = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    int32 YardLine = 0;

    /** Run, Completion, Incompletion, Sack, Interception or Touchdown; None without a result. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString Outcome;

    /** The play simulation's result (EPlayResultType), from the PlayResult event. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString Result;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    int32 YardsGained = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    bool bFirstDown = false;

    /** What blew the whistle: Tackle, BallGrounded, BoundaryCrossed or LooseBall when that event
     *  came in the steps just before it, else PhaseClock (the simulation's phase timer ended the
     *  play, not the football). */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString EndedBy;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName PasserId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName ReceiverId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName RusherId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName TacklerId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName InterceptorId;

    /** Who had the ball at the whistle, from the frames (none when it was loose or in the air). */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName BallCarrierId;

    /** Recording-clock seconds (the frames' Time) of the snap, the whistle and the result. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float SnapTime = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float WhistleTime = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float ResultTime = 0.f;

    /** Snap to whistle. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float PlaySeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    int32 FrameCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float FrameRateHz = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    int32 EventCount = 0;

    /** What the sanity checks held the play to: the top speed of the movement data plus the
     *  allowance, the ball's (its projectile's MaxSpeed), the ground's height, a capsule's half
     *  height and the ball's radius. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float PlayerSpeedLimitCmPerSec = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float BallSpeedLimitCmPerSec = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float GroundZ = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float PawnHalfHeightCm = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float BallRadiusCm = 0.f;

    /** The fastest a player went, and who. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float MaxPlayerSpeedCmPerSec = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FName FastestPlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float MaxBallSpeedCmPerSec = 0.f;

    /** The farthest the ball got from its spot at the snap. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float BallTravelCm = 0.f;

    /** Every sanity check that failed, one line each; empty for a sound play. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    TArray<FString> Problems;
};

/** index.json: every recorded play. */
USTRUCT(BlueprintType)
struct FPSPlayDemoIndex
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FDateTime GeneratedAtUtc;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString GameBuildVersion;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    float FrameRateHz = 0.f;

    /** How the plays were driven and what in them is real, in words. */
    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    FString Method;

    UPROPERTY(BlueprintReadOnly, Category = "Demo")
    TArray<FPSPlayDemoSummary> Plays;
};
