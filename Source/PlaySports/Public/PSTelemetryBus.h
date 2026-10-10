#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSSituationData.h"
#include "PSTelemetryBus.generated.h"

UENUM(BlueprintType)
enum class EPSTelemetryEventType : uint8
{
    Snap,
    Throw,
    Catch,
    Tackle,
    Fumble,
    Score,
    PhaseChange,
    Damage,
    Death,
    Respawn,
    InputDeviceChange,
    ControlChange,
    PlayCall,
    PumpFake,
    PassRushMove,
    PreSnap,
    Timeout,
    GapIntegrity,
    RouteRunning,
    Kick,
    JumpSnap,
    GameState,
    BlownCoverage,
    Personnel,
    Speech,
    Pocket,
    DefensivePreSnap,
    OpponentAdjustment,
    Versus,
    PlayResult,
    RecordBroken
};

/** What a statistic counts (Epic 92). Player categories first, then team ones. */
UENUM(BlueprintType)
enum class EPSStatCategory : uint8
{
    PassingYards,
    PassingTouchdowns,
    Completions,
    InterceptionsThrown,
    RushingYards,
    RushingTouchdowns,
    Receptions,
    ReceivingYards,
    ReceivingTouchdowns,
    Tackles,
    Sacks,
    Interceptions,
    /** A team's points in a game, or over a season or its history. */
    TeamPoints,
    TeamTotalYards
};

/** How far a statistic reaches: one game, one season, or a career (a team's history). */
UENUM(BlueprintType)
enum class EPSStatScope : uint8
{
    Game,
    Season,
    Career
};

/** Why a player was downed/killed (Epic 139/140). */
UENUM(BlueprintType)
enum class EPSDeathCause : uint8
{
    TackleDamage,
    InterceptionPunishment
};

/** What an offense changed before the snap (Epic 66). */
UENUM(BlueprintType)
enum class EPSPreSnapAction : uint8
{
    Audible,
    HotRoute,
    Motion,
    Protection
};

/** A route-running contest a receiver had (Epic 68). */
UENUM(BlueprintType)
enum class EPSRouteEventKind : uint8
{
    /** Getting off the line against press. */
    Release,
    /** A double move's fake, and whether the defender bit. */
    DoubleMove,
    /** An option route's read of the coverage. */
    OptionRead
};

/** Something the quarterback did with the pocket breaking down (Epic 71). */
UENUM(BlueprintType)
enum class EPSPocketEventKind : uint8
{
    /** He left the pocket: receivers go into the scramble drill. */
    Escape,
    /** He threw the ball away legally. */
    Throwaway,
    /** He threw it away with no receiver near it, inside the tackle box: a grounding risk for
     *  the rules to flag. */
    IntentionalGrounding,
    /** A rusher tried to strip the ball as he sacked him. */
    StripAttempt,
    /** Running past the line, he slid to protect himself. */
    Slide
};

/** What happened in a local head-to-head session (Epic 107). UPSVersusSubsystem publishes it. */
UENUM(BlueprintType)
enum class EPSVersusEventKind : uint8
{
    /** Both seats have their teams and play begins. */
    Started,
    /** The ball changed hands: the seats swapped offense and defense. */
    SidesChanged,
    Paused,
    /** A pause the etiquette refused; Reason says why. */
    PauseRefused,
    /** A seat is ready to play on. */
    ResumeConfirmed,
    /** Everyone is ready and the countdown to play is running. */
    ResumeCountdown,
    Resumed,
    /** A seat's controller disconnected (the game pauses) ... */
    Disconnected,
    /** ... or came back. */
    Reconnected,
    /** A seat quit: the other seat wins. */
    Forfeit
};

/** Which kind of hardware the human player last used (Epic 127; Touch is Epic 130's
 *  touch screen). */
UENUM(BlueprintType)
enum class EPSInputDevice : uint8
{
    KeyboardMouse,
    Gamepad,
    Touch
};

USTRUCT(BlueprintType)
struct FPSTelemetrySnapEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardLine = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Down = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Distance = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float GameClockSeconds = 0.f;

    /** World-space spot of the ball at the snap, from the game mode that placed the
     *  pawns; the origin play assignments resolve against (Epic 102). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FVector LineOfScrimmage = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FPSTelemetryThrowEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PasserName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString TargetReceiverName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FVector StartLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FVector TargetLocation = FVector::ZeroVector;

    /** Where the ball will actually come down: TargetLocation plus the passer's inaccuracy.
     *  Receivers converge on this (Epic 14). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FVector LandingLocation = FVector::ZeroVector;

    /** How hard it was thrown (cm/s): a bullet at the passer's full arm, a touch pass less
     *  (Epic 104). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float LaunchSpeed = 0.f;
};

USTRUCT(BlueprintType)
struct FPSTelemetryCatchEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString ReceiverName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FVector CatchLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardsGained = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bIsInterception = false;
};

USTRUCT(BlueprintType)
struct FPSTelemetryTackleEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString TacklerName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString BallCarrierName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardLine = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardsGained = 0;

    /** The carrier was the quarterback, down behind the line before throwing. The publisher
     *  (the play-outcome authority) decides; subscribers such as rumble only read it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bIsSack = false;
};

USTRUCT(BlueprintType)
struct FPSTelemetryFumbleEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString FumblerName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString RecoveryName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardLine = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bIsTurnover = false;
};

USTRUCT(BlueprintType)
struct FPSTelemetryScoreEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString ScoreType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bHomeScored = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Points = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 HomeScore = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 AwayScore = 0;
};

USTRUCT(BlueprintType)
struct FPSTelemetryPhaseChangeEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString OldPhase;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString NewPhase;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float GameClockSeconds = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float PlayClockSeconds = 0.f;
};

USTRUCT(BlueprintType)
struct FPSTelemetryDamageEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString TargetName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Amount = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float RemainingHitPoints = 0.f;
};

USTRUCT(BlueprintType)
struct FPSTelemetryDeathEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PlayerName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSDeathCause Cause = EPSDeathCause::TackleDamage;
};

USTRUCT(BlueprintType)
struct FPSTelemetryRespawnEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PlayerName;
};

/** The human player's active device changed, or a gamepad connected/disconnected
 *  (Epic 127). HUD glyphs (UPSInputGlyphs) and rumble (UPSForceFeedbackComponent, Epic 128)
 *  follow this instead of asking the controller. */
USTRUCT(BlueprintType)
struct FPSTelemetryInputDeviceEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSInputDevice ActiveDevice = EPSInputDevice::KeyboardMouse;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSInputDevice PreviousDevice = EPSInputDevice::KeyboardMouse;

    /** True when a connect/disconnect caused this event rather than the last-input heuristic. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bFromConnectionChange = false;

    /** For connection changes: whether the gamepad connected (true) or disconnected. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bConnected = true;

    /** Which human's device this is (APSPlayerController::HumanIndex): 0 for the first local
     *  player, 1 for the second in a head-to-head game (Epic 107). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 HumanIndex = 0;
};

/** A human took or released control of a pawn (Epic 127). */
USTRUCT(BlueprintType)
struct FPSTelemetryControlChangeEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PlayerName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bHumanControlled = false;

    /** Which human took or released the pawn (APSPlayerController::HumanIndex), so each of two
     *  local players follows only their own (Epic 107). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 HumanIndex = 0;
};

/** A side called its play for the coming snap (Epic 102). The call itself lives in
 *  UPSPlayCallSubsystem; this announces it. A HUD shows only its own side's call. */
USTRUCT(BlueprintType)
struct FPSTelemetryPlayCallEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName PlayId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Formation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PlayCategory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bOffense = true;

    /** True when a person chose it; false for the CPU's call. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bHumanCall = false;

    /** The offense's tempo for this snap (Epic 76); the defense's call carries Huddle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSTempo Tempo = EPSTempo::Huddle;

    /** The play-clock reading the offense snaps at (its tempo's); negative for the defense.
     *  UPSPlaySimulation runs a running game clock down to it at the snap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float SnapAtPlayClockSeconds = -1.f;

    /** What the offense's ball carrier does about the sideline on this call. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSBoundaryIntent BoundaryIntent = EPSBoundaryIntent::None;
};

/** The passer sold a throw he didn't make (Epic 104): coverage that bites freezes. */
USTRUCT(BlueprintType)
struct FPSTelemetryPumpFakeEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PasserName;

    /** The receiver the fake was aimed at, if any. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString TargetReceiverName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FVector PasserLocation = FVector::ZeroVector;
};

/** A pass rusher's move against his blocker was resolved (Epic 70): won, or stopped by the
 *  blocker's response. UPSRushMoveComponent publishes it. */
USTRUCT(BlueprintType)
struct FPSTelemetryPassRushEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString RusherName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString BlockerName;

    /** The EPSRushMove, by name (Bull, Swim, ...). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Move;

    /** The EPSBlockResponse that stopped it, by name; empty when the move won. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Response;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float WinChance = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bWon = false;

    /** Two blockers were on the rusher. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bDoubleTeamed = false;
};

/** The offense changed its call before the snap (Epic 66): an audible, a hot route, a man in
 *  motion, or a protection call. UPSPreSnapSubsystem is the authority on these; this announces
 *  them. A motion also says how the defense answered it. */
USTRUCT(BlueprintType)
struct FPSTelemetryPreSnapEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSPreSnapAction Action = EPSPreSnapAction::Audible;

    /** The player changed; empty for an audible or a slide. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PlayerName;

    /** What he changed to: the audible's PlayId, the hot route's RouteId, "Block", "Release"
     *  or "AsCalled", "SlideLeft", "SlideRight" or "NoSlide"; "Motion" for a motion. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName Detail;

    /** True when a person made the change; false for the CPU's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bHumanCall = false;

    /** A motion: the defender who travelled across with him, if one did. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString DefenderName;

    /** A motion: a defender travelled with him, the tell of man coverage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bManIndicator = false;
};

/** A side calls a timeout (Epic 76). UPSPlaySimulation, the clock's authority, charges it
 *  and stops the clock, or refuses it when the side has none left. */
USTRUCT(BlueprintType)
struct FPSTelemetryTimeoutEvent
{
    GENERATED_BODY()

    /** True for the possessing team, false for the defense. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bOffense = true;

    /** True when a person called it; false for the CPU. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bHumanCall = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float GameClockSeconds = 0.f;
};

/** The run defense's gap integrity changed (Epic 81): which gaps are open -- no owner, or an
 *  owner not lined up on it. UPSDefenderGapSubsystem publishes it. */
USTRUCT(BlueprintType)
struct FPSTelemetryGapIntegrityEvent
{
    GENERATED_BODY()

    /** The front the gaps were assigned from. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Front;

    /** The open gaps' EPSRunGap names, comma-separated, D left to D right. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString OpenGaps;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 OpenGapCount = 0;

    /** A ball carrier other than the passer had the ball. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bRunRead = false;

    /** Scrape exchanges so far this play. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 ScrapeExchanges = 0;
};

/** A receiver's route-running contest was resolved (Epic 68): his release against press, a
 *  double move's fake, or an option route's read. UPSRouteRunnerComponent decides it (the one
 *  authority); a defender who bit freezes for Seconds. */
USTRUCT(BlueprintType)
struct FPSTelemetryRouteEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSRouteEventKind Kind = EPSRouteEventKind::Release;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString ReceiverName;

    /** The presser, the defender the fake worked on, or (man) the defender read; may be empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString DefenderName;

    /** Release: "Win", "Delay" or "Reroute"; double move: "Bit" or "Stayed"; option read: "Man"
     *  or "Zone". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName Outcome;

    /** How long it holds: a jammed receiver's hold, a bitten defender's freeze. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Seconds = 0.f;
};

/** A human kicker lines up for a kick, or kicks (Epic 104.5). UPSKickMeterComponent publishes
 *  it; UPSPlaySimulation, the authority on the kick's result, waits for the kick while one is
 *  lined up and then takes its Roll in place of a random one. */
USTRUCT(BlueprintType)
struct FPSTelemetryKickEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString KickerName;

    /** The play phase kicking: Kickoff, Punt or FieldGoal. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString KickType;

    /** True when the kicker has only lined up: the play waits up to HoldSeconds into the phase
     *  for the kick. False when this is the kick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bLiningUp = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float HoldSeconds = 0.f;

    /** The meter's power, 0..1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Power = 0.f;

    /** Where the accuracy needle stopped, -1 (hooked left) .. 1 (pushed right); 0 is straight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Accuracy = 0.f;

    /** The kick's quality as a roll, 0 (perfect) .. 1 (the worst): the result compares it
     *  where the CPU kicker compares a random number. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Roll = 1.f;
};

/** A defender's jump at the snap was timed (Epic 104.5). UPSDefenseInputComponent publishes it at
 *  the snap; an offside jump is a flag for UPSPlaySimulation. */
USTRUCT(BlueprintType)
struct FPSTelemetryJumpSnapEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString DefenderName;

    /** How long before the snap he moved. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float LeadSeconds = 0.f;

    /** He moved too early: offside. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bOffside = false;
};

/** What the broadcast shows of the game (Epic 33). UPSPlaySimulation, the authority, publishes
 *  it whenever any of it changes other than the running clocks; between events the clocks run
 *  on from GameClockSeconds and PlayClockSeconds while their bRunning flags say so. */
USTRUCT(BlueprintType)
struct FPSTelemetryGameStateEvent
{
    GENERATED_BODY()

    /** The EPlayPhase by name (PreSnap, Snap, ...). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Phase;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Quarter = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float GameClockSeconds = 900.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bGameClockRunning = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float PlayClockSeconds = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bPlayClockRunning = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Down = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Distance = 10;

    /** From the offense's own goal line (0) to the opponent's (100). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardLine = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardLineToGain = 30;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bHomeHasPossession = true;

    /** The next snap is a kickoff (Epic 75): the possessing team kicks, from before its call. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bKickoff = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 HomeScore = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 AwayScore = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 HomeTimeoutsRemaining = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 AwayTimeoutsRemaining = 3;

    /** Timeouts each side gets per half. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 MaxTimeouts = 3;

    /** Drives finished so far; one more each time possession changes hands. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 CompletedDrives = 0;

    /** The most recently finished drive. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 LastDrivePlays = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 LastDriveYards = 0;

    /** "Touchdown", "Safety", "Turnover on Downs", ... or empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString LastDriveResult;

    /** True when everything but the clock readings matches Other. */
    bool HasSameStateAs(const FPSTelemetryGameStateEvent& Other) const
    {
        return Phase == Other.Phase && Quarter == Other.Quarter
            && bGameClockRunning == Other.bGameClockRunning && bPlayClockRunning == Other.bPlayClockRunning
            && Down == Other.Down && Distance == Other.Distance && YardLine == Other.YardLine && YardLineToGain == Other.YardLineToGain
            && bHomeHasPossession == Other.bHomeHasPossession && bKickoff == Other.bKickoff && HomeScore == Other.HomeScore && AwayScore == Other.AwayScore
            && HomeTimeoutsRemaining == Other.HomeTimeoutsRemaining && AwayTimeoutsRemaining == Other.AwayTimeoutsRemaining
            && MaxTimeouts == Other.MaxTimeouts && CompletedDrives == Other.CompletedDrives;
    }
};

/** A receiver is running free downfield, every defender far from him (Epic 17's broken-play
 *  reactions). UPSBlownCoverageSubsystem spots it once per receiver per play and names the
 *  defender who leaves his zone to pick him up (empty when nobody can). */
USTRUCT(BlueprintType)
struct FPSTelemetryBlownCoverageEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString ReceiverName;

    /** The defender who rotates to him; empty when no zone defender was free to. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString HelperName;

    /** How far the nearest defender was from him (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Separation = 0.f;

    /** Where the receiver was. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FVector Location = FVector::ZeroVector;
};

/** A side's personnel changed (Epic 19.5): a new package came on, or players were
 *  substituted within one (a sit-out, a tired player). UPSPersonnelManager publishes it; a
 *  personnel panel (Track A, Epic 29) reads it. */
USTRUCT(BlueprintType)
struct FPSTelemetryPersonnelEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bOffense = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName PackageId;

    /** The package's display name, e.g. "12 Personnel". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PackageName;

    /** PlayerIds who came on, in the same order as the PlayersOut they replaced. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    TArray<FName> PlayersIn;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    TArray<FName> PlayersOut;
};

/** Someone said something the player should be able to read (Epic 103.3): the commentary booth
 *  (Epic 96), the PA, a referee. UPSUIAccessibilitySubsystem captions it when captions are on. */
USTRUCT(BlueprintType)
struct FPSTelemetrySpeechEvent
{
    GENERATED_BODY()

    /** Who is speaking, as the caption names them; empty for none. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Speaker;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Text;

    /** Where it comes from: Commentary, PA, Referee... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName Channel;

    /** How long it is spoken; 0 lets the caption time it from its length. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float DurationSeconds = 0.f;
};

/** The quarterback's pocket play (Epic 71): an escape (the scramble drill's trigger), a
 *  throwaway, a grounding risk, a strip attempt or a protective slide. UPSPocketComponent
 *  decides these; the grounding call itself is the rules' (Epic 73). */
USTRUCT(BlueprintType)
struct FPSTelemetryPocketEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSPocketEventKind Kind = EPSPocketEventKind::Escape;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PasserName;

    /** The rusher involved (the one he escaped, the stripper, the tackler he slid from). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString DefenderName;

    /** Where the quarterback was. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FVector Location = FVector::ZeroVector;

    /** An escape: the side he scrambles to across the field (-1 left, +1 right). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float ScrambleSide = 0.f;

    /** A strip attempt: whether the ball came out. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bSuccess = false;
};

/** The defense changed its look or its call before the snap (Epic 67). UPSDefenderPreSnapSubsystem
 *  is the authority on these; this announces them. */
USTRUCT(BlueprintType)
struct FPSTelemetryDefensivePreSnapEvent
{
    GENERATED_BODY()

    /** "Audible", "DisguiseShell", "ShowBlitz", "Creep" or "Shadow". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName Action;

    /** The defender a shadow assigns; empty for a team call. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PlayerName;

    /** The audible's PlayId, the shadowed receiver's name, or "On"/"Off" for a disguise. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName Detail;

    /** True when a person made the change; false for the CPU's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bHumanCall = false;
};

/** The CPU adjusted to the human's play-calling (Epic 78): its halftime adjustments, when it
 *  starts leaning on what it has seen harder. UPSOpponentModel announces it. */
USTRUCT(BlueprintType)
struct FPSTelemetryOpponentAdjustmentEvent
{
    GENERATED_BODY()

    /** The side the CPU adjusted: true for its offense (against the human's defense). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bCpuOffense = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Quarter = 0;

    /** How hard it leaned on its read before, and from now on (0-1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float PreviousStrength = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Strength = 0.f;

    /** What it saw the human call most in this situation, and how often. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString TopCategory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float TopShare = 0.f;

    /** How many of his calls the read stands on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Samples = 0.f;
};

/** A local head-to-head session changed (Epic 107): pauses and their etiquette, the seats
 *  swapping sides, a disconnect, a forfeit. The HUD shows it; the session itself is
 *  UPSVersusSubsystem's. It never carries either side's call. */
USTRUCT(BlueprintType)
struct FPSTelemetryVersusEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSVersusEventKind Kind = EPSVersusEventKind::Started;

    /** The seat it concerns (0 is player 1, 1 is player 2); -1 for both. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Seat = -1;

    /** Whether the home team has the ball, so the HUD knows which seat is on offense. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bHomeOnOffense = true;

    /** Paused: the pausing seat's pauses left this half; -1 without a limit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 PausesLeft = -1;

    /** ResumeCountdown: seconds until play resumes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float CountdownSeconds = 0.f;

    /** PauseRefused: the ui_text key that says why ("Versus.Refused.PlayLive"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Reason;
};

/** A play is over, as UPSPlaySimulation (the outcome authority) resolved it (Epic 92): the
 *  situation at the snap, the result, the points it scored and who threw, caught, ran and
 *  tackled. The statistics engine (UPSStatsEngine) records every play from it. Quick-sim games,
 *  which have no world, announce it through UPSPlaySimulation::OnPlayResolved instead. */
USTRUCT(BlueprintType)
struct FPSTelemetryPlayResultEvent
{
    GENERATED_BODY()

    /** 1 for the game's first play, kicks included. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 PlayNumber = 0;

    /** The home team had the ball (on a kick: kicked). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bHomeOffense = true;

    /** The situation at the snap. YardLine runs from the offense's own goal line (0) to the
     *  opponent's (100). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Quarter = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float GameClockSeconds = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Down = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Distance = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardLine = 20;

    /** The EPlayResultType by name: Incomplete, Tackle, Touchdown, Safety, FieldGoalGood,
     *  FieldGoalMissed, KickoffResult, PuntResult or Interception. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Result;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 YardsGained = 0;

    /** A pass was thrown (a sack counts: the quarterback dropped back to throw). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bPass = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bComplete = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bInterception = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bSack = false;

    /** The offense reached the line to gain, or scored. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bFirstDown = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bTurnoverOnDowns = false;

    /** Points each team scored on the play (a touchdown with its try). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 HomePoints = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 AwayPoints = 0;

    /** Who did what, by PlayerId; None when nobody did or the simulation doesn't know. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName PasserId;

    /** The pass's target, or its catcher. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName ReceiverId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName RusherId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName TacklerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName InterceptorId;
};

/** A record in the record book fell (Epic 92): UPSStatsEngine announces it as the game that broke
 *  it is finished. Epic 93's storylines and Track H's commentary take it from here. */
USTRUCT(BlueprintType)
struct FPSTelemetryRecordBrokenEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSStatCategory Category = EPSStatCategory::PassingYards;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSStatScope Scope = EPSStatScope::Game;

    /** A team record (TeamPoints, TeamTotalYards); otherwise a player's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    bool bTeamRecord = false;

    /** The new holder: a PlayerId, or a TeamId for a team record. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName HolderId;

    /** The holder's team. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Value = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FName PreviousHolderId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 PreviousValue = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Season = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Week = 0;

    /** "Game PassingYards record: HAW_QB_001 412 (was 398 by BER_QB_001)". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Description;
};

USTRUCT(BlueprintType)
struct FPSTelemetryEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    EPSTelemetryEventType EventType = EPSTelemetryEventType::Snap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    float Timestamp = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    FString PayloadJson;

    /** Where this event falls in the bus's publish order: 1 for the first event the bus
     *  publishes, one more for each after, never reused (ClearHistory keeps counting).
     *  Snapshot frames carry the sequence they follow, which is how the sampled stream
     *  joins this one (UPSTelemetrySamplingSubsystem, Epic 26). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telemetry")
    int32 Sequence = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetrySnapSignature, const FPSTelemetrySnapEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryThrowSignature, const FPSTelemetryThrowEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryCatchSignature, const FPSTelemetryCatchEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryTackleSignature, const FPSTelemetryTackleEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryFumbleSignature, const FPSTelemetryFumbleEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryScoreSignature, const FPSTelemetryScoreEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryPhaseChangeSignature, const FPSTelemetryPhaseChangeEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryDamageSignature, const FPSTelemetryDamageEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryDeathSignature, const FPSTelemetryDeathEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryRespawnSignature, const FPSTelemetryRespawnEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryInputDeviceSignature, const FPSTelemetryInputDeviceEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryControlChangeSignature, const FPSTelemetryControlChangeEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryPlayCallSignature, const FPSTelemetryPlayCallEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryPumpFakeSignature, const FPSTelemetryPumpFakeEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryPassRushSignature, const FPSTelemetryPassRushEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryPreSnapSignature, const FPSTelemetryPreSnapEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryTimeoutSignature, const FPSTelemetryTimeoutEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryGapIntegritySignature, const FPSTelemetryGapIntegrityEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryRouteSignature, const FPSTelemetryRouteEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryKickSignature, const FPSTelemetryKickEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryJumpSnapSignature, const FPSTelemetryJumpSnapEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryGameStateSignature, const FPSTelemetryGameStateEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryBlownCoverageSignature, const FPSTelemetryBlownCoverageEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryPersonnelSignature, const FPSTelemetryPersonnelEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetrySpeechSignature, const FPSTelemetrySpeechEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryPocketSignature, const FPSTelemetryPocketEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryDefensivePreSnapSignature, const FPSTelemetryDefensivePreSnapEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryOpponentAdjustmentSignature, const FPSTelemetryOpponentAdjustmentEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryVersusSignature, const FPSTelemetryVersusEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryPlayResultSignature, const FPSTelemetryPlayResultEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSTelemetryRecordBrokenSignature, const FPSTelemetryRecordBrokenEvent&, Event);

DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetrySnapMC, const FPSTelemetrySnapEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryThrowMC, const FPSTelemetryThrowEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryCatchMC, const FPSTelemetryCatchEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryTackleMC, const FPSTelemetryTackleEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryFumbleMC, const FPSTelemetryFumbleEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryScoreMC, const FPSTelemetryScoreEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryPhaseChangeMC, const FPSTelemetryPhaseChangeEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryDamageMC, const FPSTelemetryDamageEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryDeathMC, const FPSTelemetryDeathEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryRespawnMC, const FPSTelemetryRespawnEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryInputDeviceMC, const FPSTelemetryInputDeviceEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryControlChangeMC, const FPSTelemetryControlChangeEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryPlayCallMC, const FPSTelemetryPlayCallEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryPumpFakeMC, const FPSTelemetryPumpFakeEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryPassRushMC, const FPSTelemetryPassRushEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryPreSnapMC, const FPSTelemetryPreSnapEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryTimeoutMC, const FPSTelemetryTimeoutEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryKickMC, const FPSTelemetryKickEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryJumpSnapMC, const FPSTelemetryJumpSnapEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryGameStateMC, const FPSTelemetryGameStateEvent&);

/** Any event, once it is in the history and before its typed delegates fire (Epic 26). */
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryEventRecordedMC, const FPSTelemetryEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryGapIntegrityMC, const FPSTelemetryGapIntegrityEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryRouteMC, const FPSTelemetryRouteEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryBlownCoverageMC, const FPSTelemetryBlownCoverageEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryPersonnelMC, const FPSTelemetryPersonnelEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetrySpeechMC, const FPSTelemetrySpeechEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryPocketMC, const FPSTelemetryPocketEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryDefensivePreSnapMC, const FPSTelemetryDefensivePreSnapEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryOpponentAdjustmentMC, const FPSTelemetryOpponentAdjustmentEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryVersusMC, const FPSTelemetryVersusEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryPlayResultMC, const FPSTelemetryPlayResultEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSTelemetryRecordBrokenMC, const FPSTelemetryRecordBrokenEvent&);

UCLASS(BlueprintType, Blueprintable)
class PLAYSPORTS_API UPSTelemetryBus : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishSnap(const FPSTelemetrySnapEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishThrow(const FPSTelemetryThrowEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishCatch(const FPSTelemetryCatchEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishTackle(const FPSTelemetryTackleEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishFumble(const FPSTelemetryFumbleEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishScore(const FPSTelemetryScoreEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishPhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishDamage(const FPSTelemetryDamageEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishDeath(const FPSTelemetryDeathEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishRespawn(const FPSTelemetryRespawnEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishInputDeviceChange(const FPSTelemetryInputDeviceEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishControlChange(const FPSTelemetryControlChangeEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishPlayCall(const FPSTelemetryPlayCallEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishPumpFake(const FPSTelemetryPumpFakeEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishPassRushMove(const FPSTelemetryPassRushEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishPreSnap(const FPSTelemetryPreSnapEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishTimeout(const FPSTelemetryTimeoutEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishGapIntegrity(const FPSTelemetryGapIntegrityEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishRouteRunning(const FPSTelemetryRouteEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishKick(const FPSTelemetryKickEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishJumpSnap(const FPSTelemetryJumpSnapEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishGameState(const FPSTelemetryGameStateEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishBlownCoverage(const FPSTelemetryBlownCoverageEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishPersonnel(const FPSTelemetryPersonnelEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishSpeech(const FPSTelemetrySpeechEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishPocket(const FPSTelemetryPocketEvent& Event);

    /** A play's result, from UPSPlaySimulation (Epic 92). */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishPlayResult(const FPSTelemetryPlayResultEvent& Event);

    /** A broken record, from UPSStatsEngine (Epic 92). */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishRecordBroken(const FPSTelemetryRecordBrokenEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishDefensivePreSnap(const FPSTelemetryDefensivePreSnapEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishOpponentAdjustment(const FPSTelemetryOpponentAdjustmentEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void PublishVersus(const FPSTelemetryVersusEvent& Event);

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    TArray<FPSTelemetryEvent> GetEventHistory() const { return EventHistory; }

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void ClearHistory();

    /** The Sequence of the most recently published event; 0 before the first. */
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    int32 GetLastEventSequence() const { return LastEventSequence; }

    /** The Sequence of the oldest event still in the history; 0 when it is empty. */
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    int32 GetOldestEventSequence() const;

    /** How many events the history keeps before dropping the oldest. */
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    int32 GetMaxHistorySize() const { return MaxHistorySize; }

    /** The event with this Sequence, while it is still in the history. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    bool FindEventBySequence(int32 Sequence, FPSTelemetryEvent& OutEvent) const;

    /** The most recent event of EventType still in the history. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    bool FindLatestEventOfType(EPSTelemetryEventType EventType, FPSTelemetryEvent& OutEvent) const;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetrySnapSignature OnSnap;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryThrowSignature OnThrow;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryCatchSignature OnCatch;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryTackleSignature OnTackle;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryFumbleSignature OnFumble;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryScoreSignature OnScore;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryPhaseChangeSignature OnPhaseChange;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryDamageSignature OnDamage;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryDeathSignature OnDeath;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryRespawnSignature OnRespawn;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryInputDeviceSignature OnInputDeviceChange;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryControlChangeSignature OnControlChange;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryPlayCallSignature OnPlayCall;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryPumpFakeSignature OnPumpFake;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryPassRushSignature OnPassRushMove;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryPreSnapSignature OnPreSnap;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryTimeoutSignature OnTimeout;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryGapIntegritySignature OnGapIntegrity;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryRouteSignature OnRouteRunning;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryKickSignature OnKick;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryJumpSnapSignature OnJumpSnap;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryGameStateSignature OnGameState;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryBlownCoverageSignature OnBlownCoverage;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryPersonnelSignature OnPersonnel;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetrySpeechSignature OnSpeech;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryPocketSignature OnPocket;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryDefensivePreSnapSignature OnDefensivePreSnap;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryOpponentAdjustmentSignature OnOpponentAdjustment;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryVersusSignature OnVersus;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryPlayResultSignature OnPlayResult;

    UPROPERTY(BlueprintAssignable, Category = "Telemetry")
    FPSTelemetryRecordBrokenSignature OnRecordBroken;

    FPSTelemetrySnapMC OnSnapMC;
    FPSTelemetryThrowMC OnThrowMC;
    FPSTelemetryCatchMC OnCatchMC;
    FPSTelemetryTackleMC OnTackleMC;
    FPSTelemetryFumbleMC OnFumbleMC;
    FPSTelemetryScoreMC OnScoreMC;
    FPSTelemetryPhaseChangeMC OnPhaseChangeMC;
    FPSTelemetryDamageMC OnDamageMC;
    FPSTelemetryDeathMC OnDeathMC;
    FPSTelemetryRespawnMC OnRespawnMC;
    FPSTelemetryInputDeviceMC OnInputDeviceChangeMC;
    FPSTelemetryControlChangeMC OnControlChangeMC;
    FPSTelemetryPlayCallMC OnPlayCallMC;
    FPSTelemetryPumpFakeMC OnPumpFakeMC;
    FPSTelemetryPassRushMC OnPassRushMoveMC;
    FPSTelemetryPreSnapMC OnPreSnapMC;
    FPSTelemetryTimeoutMC OnTimeoutMC;
    FPSTelemetryKickMC OnKickMC;
    FPSTelemetryJumpSnapMC OnJumpSnapMC;
    FPSTelemetryGameStateMC OnGameStateMC;

    /** Fires for every event as it is recorded, before its typed delegates, so a listener
     *  sees the world exactly as it was when the event happened (Epic 26's keyframes). */
    FPSTelemetryEventRecordedMC OnEventRecordedMC;
    FPSTelemetryGapIntegrityMC OnGapIntegrityMC;
    FPSTelemetryRouteMC OnRouteRunningMC;
    FPSTelemetryBlownCoverageMC OnBlownCoverageMC;
    FPSTelemetryPersonnelMC OnPersonnelMC;
    FPSTelemetrySpeechMC OnSpeechMC;
    FPSTelemetryPocketMC OnPocketMC;
    FPSTelemetryDefensivePreSnapMC OnDefensivePreSnapMC;
    FPSTelemetryOpponentAdjustmentMC OnOpponentAdjustmentMC;
    FPSTelemetryVersusMC OnVersusMC;
    FPSTelemetryPlayResultMC OnPlayResultMC;
    FPSTelemetryRecordBrokenMC OnRecordBrokenMC;

private:
    UPROPERTY(Transient)
    TArray<FPSTelemetryEvent> EventHistory;

    const int32 MaxHistorySize = 100;

    int32 LastEventSequence = 0;

    void RecordHistory(EPSTelemetryEventType EventType, const FString& Description, const FString& JsonPayload);
};
