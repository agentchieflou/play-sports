// PSVersusSubsystem.h - Epic 107: local head-to-head, two humans on one machine
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSVersusTypes.h"
#include "PSVersusSubsystem.generated.h"

class AController;
class APlayerController;
class APSPlayerController;
class UPSMatchSetup;
class UPSPlayCallSubsystem;

/**
 * UPSVersusSubsystem runs a local head-to-head game (Epic 107): two seats, each an
 * APSPlayerController of its own (its own catalog contexts, input device and menus), one per
 * team. It is the one authority on who sits where and which side, home or away, each seat plays
 * for; the match setup (UPSMatchSetup) owns which league teams those are (GetSeatTeamId reads
 * them from it), the play-call authority (UPSPlayCallSubsystem) the calls, the control handoff
 * (UPSControlHandoffComponent) who a switch goes to, and the menus their screens.
 *
 *  - Session flow (107.1): seats are claimed (ClaimSeat, one per user), each picks a team
 *    (SelectTeam / StepTeam; both can't have the same one), both ready up, and StartSession
 *    begins. The front end's Head to Head screen travels with "?mode=Versus?homeseat=<seat>",
 *    plus the two players' teams as "?home=" and "?away=" (UPSMatchSetup reads those);
 *    BeginFromTravel then seats the first two local players (creating the second) and starts.
 *    The seat whose team has the ball plays offense: when the ball changes hands (the bus's
 *    GameState) the seats swap sides, and every new down (PhaseChange to PreSnap) puts each
 *    back on its side's control role. Each seat keeps its own offensive tempo across the swap.
 *  - Hidden picks: both sides call at once on their own screens; the offense can't hike until
 *    both calls are in. A seat can learn only whether the other has called (GetCallState),
 *    never what; a swap closes any open call screen before control moves.
 *  - Split contexts (107.2): each seat's UPSPlayContextComponent keeps its own depth context,
 *    so the offense is in PreSnap / Passing while the defense is in DefensePreSnap / Defense.
 *    The defending seat's switching follows the rules (bDefenseSwitchDuringPlay,
 *    bDefensePreSnapPicks), and nobody ever takes the other human's player.
 *  - Competitive integrity (107.3): ShouldShowOverlay says who may see the offense's route
 *    art (Epic 27) and the defense's assignment icons (Epic 31) on a shared or split screen.
 *  - Pause etiquette (107.4): a limited number of pauses per half, between plays only; play
 *    resumes once both players are ready and a countdown has run; a disconnected controller
 *    pauses the game until it is back; quitting forfeits.
 *
 * Everything that changes is published on the bus (Versus). The rules are data
 * (Data/versus_rules.json). Headless tests seat controllers by hand and call AdvanceResume.
 */
UCLASS()
class PLAYSPORTS_API UPSVersusSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    static constexpr int32 SeatCount = 2;

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    /** The resume countdown runs while the game is paused. */
    virtual bool IsTickableWhenPaused() const override { return true; }

    static FString GetDefaultRulesPath();

    /** Replaces the rules with JsonFilePath's, read through UPSDataIngestion; rules with
     *  problems are refused. */
    bool LoadRulesFromJson(const FString& JsonFilePath);

    /** The rules in use, loaded from the default path on first use. */
    const FPSVersusRules& GetRules();

    /** Replaces the rules (a house-rules screen, tests); refused when ValidateRules finds
     *  problems. A running session picks them up at the next down, or at ApplySides. */
    bool SetRules(const FPSVersusRules& InRules);

    /** Problems with Candidate, one line each (empty when sound). */
    static TArray<FString> ValidateRules(const FPSVersusRules& Candidate);

    // --- Seats and side select (107.1) --------------------------------------------------

    /** Seats Controller for UserIndex's devices in the first free seat and returns it, or
     *  INDEX_NONE when both are taken, the controller or user already sits, or a session is
     *  under way. The controller becomes that human (HumanIndex) and its device component
     *  counts only that user's input. */
    int32 ClaimSeat(APSPlayerController* Controller, int32 UserIndex);

    /** Empties a seat before the session starts. */
    bool ReleaseSeat(int32 Seat);

    const FPSVersusSeat& GetSeat(int32 Seat) const;

    APSPlayerController* GetSeatController(int32 Seat) const;

    /** The seat Controller sits in, or INDEX_NONE. */
    int32 FindSeat(const AController* Controller) const;

    /** Puts a seat on Team (None leaves it undecided). Refused when the other seat has that
     *  team or the session has started; a seat that changes team is no longer ready. */
    bool SelectTeam(int32 Seat, EPSVersusTeam Team);

    /** Moves a seat's pick one step across the side-select screen (Away on the left, nobody
     *  in the middle, Home on the right); a step onto the other seat's team is refused.
     *  Returns the team the seat is on. */
    EPSVersusTeam StepTeam(int32 Seat, int32 Direction);

    /** A seat with a team says it is ready (or not). */
    bool SetReady(int32 Seat, bool bReady);

    /** Both seats claimed, on different teams and ready. */
    bool CanStart() const;

    /** Begins play: both seats get their side for the possession and take their player. */
    bool StartSession();

    /** The second local player for a session started from the front end: created through the
     *  game instance, with its own controller. Null without a game instance (headless). */
    APSPlayerController* CreateLocalSeatPlayer(int32 ControllerId);

    // --- The match's teams ----------------------------------------------------------------

    /** The match whose home and away teams the seats play: the game mode's UPSMatchSetup, the one
     *  authority on them, handed over at kickoff (it reads "?home=" and "?away=" from the same
     *  travel URL). Kept as a reference, never copied. */
    void SetMatchSetup(const UPSMatchSetup* InMatchSetup);

    /** The league team a seat plays for: the match's home team for the seat on Home, its away
     *  team for the seat on Away; None without a match or before the seat picks a side. */
    FName GetSeatTeamId(int32 Seat) const;

    /** Starts a session from a travel URL ("?mode=Versus?homeseat=1"): seats the first two local
     *  players, creating the second, gives Home to the named seat (0 by default) and starts.
     *  False when the URL is not a versus game or two seats can't be filled. */
    bool BeginFromTravel(const FURL& URL);

    /** Whether a travel URL asks for a head-to-head game. */
    static bool IsVersusURL(const FURL& URL);

    /** The seat a travel URL makes the home team: its "home" option, 0 or 1 (0 without one). */
    static int32 GetHomeSeatFromURL(const FURL& URL);

    // --- Sides ----------------------------------------------------------------------------

    EPSVersusPhase GetPhase() const { return Phase; }

    /** Playing or paused. */
    bool IsSessionActive() const { return Phase == EPSVersusPhase::Playing || Phase == EPSVersusPhase::Paused; }

    bool IsPaused() const { return Phase == EPSVersusPhase::Paused; }

    /** The side a seat plays at this down: offense while its team has the ball. */
    EPSTeamSide GetSeatSide(int32 Seat) const;

    /** The seat on offense (or defense) at this down, or INDEX_NONE. */
    int32 GetSeatOnSide(EPSTeamSide Side) const;

    bool IsHomeOnOffense() const { return bHomeHasPossession; }

    /** True from the snap to the whistle, as the bus said. */
    bool IsPlayLive() const { return bPlayLive; }

    /** Gives each seat its side's control role and switching rules, and, when bMoveControl,
     *  moves each onto that role's player: seats whose side changed let go first, so neither
     *  ever takes the other's player. */
    void ApplySides(bool bMoveControl);

    // --- Hidden picks ---------------------------------------------------------------------

    /** Where the call of a seat's side stands: never which play it is. */
    EPSVersusCallState GetCallState(int32 Seat) const;

    /** The same for the other seat's side: what a seat may know of its opponent's call. */
    EPSVersusCallState GetOpponentCallState(int32 Seat) const;

    // --- Competitive integrity (107.3) ----------------------------------------------------

    /** Whether Viewer's view may draw Overlay before the snap. Without a session, always
     *  (the player's settings decide); in one, by the rules' audience for it and the screen. */
    bool ShouldShowOverlay(EPSVersusOverlay Overlay, const APlayerController* Viewer) const;

    // --- Pause etiquette (107.4) ----------------------------------------------------------

    /** Whether a seat may pause now; OutReason is the ui_text key that says why not. */
    bool CanPause(int32 Seat, FString& OutReason) const;

    /** A seat pauses: counted, the game paused and the pause screen opened for both seats.
     *  Refusals are announced on the bus (PauseRefused). */
    bool RequestPause(int32 Seat);

    /** Something outside the game interrupted it (Epic 152: the platform suspending or
     *  constraining it): pauses a session in play, mid-play too, without counting it against
     *  anyone's pauses. False when no session is playing. */
    bool PauseForInterruption();

    /** A seat is ready to play on. When every seat that must be is ready (and every
     *  controller is connected), the countdown starts. */
    bool ConfirmResume(int32 Seat);

    /** Runs the resume countdown on by DeltaSeconds of real time; play resumes at zero. The
     *  tick calls it; tests call it directly. */
    void AdvanceResume(float DeltaSeconds);

    /** Seconds left on the resume countdown, or a negative number when none is running. */
    float GetResumeCountdown() const { return ResumeSecondsLeft; }

    /** The seat that paused, or INDEX_NONE. */
    int32 GetPausedBySeat() const { return PausedBySeat; }

    /** A seat's pauses left this half; -1 without a limit. */
    int32 GetPausesLeft(int32 Seat) const;

    /** A seat quits: with bQuitForfeits the other seat wins. The session ends either way. */
    bool Forfeit(int32 Seat);

    /** The seat that won by forfeit, or INDEX_NONE. */
    int32 GetWinnerSeat() const { return WinnerSeat; }

    /** The Pause button of Controller's player (UPSMenuComponent::TogglePause in a session):
     *  pauses, or while paused says the player is ready. False when Controller has no seat. */
    bool HandlePausePressed(APSPlayerController* Controller);

    /** The pause screen's Resume (or Back): true when the session took it as the player's
     *  ready, false when no versus pause is on and the menu resumes as usual. */
    bool HandleResumePressed(APSPlayerController* Controller);

    /** The pause screen's Quit: forfeits for Controller's seat when a session is on. */
    bool HandleQuitPressed(APSPlayerController* Controller);

    // --- Status for the HUD ---------------------------------------------------------------

    /** "Player 2 paused: 2 pauses left", "Waiting for Player 1", "Resuming in 3", ...;
     *  empty while nothing needs saying. */
    FText DescribeStatus() const;

    /** "Opponent is choosing a play" / "Opponent is ready" for Seat's HUD while the call window
     *  is open; empty otherwise. */
    FText DescribeOpponentCall(int32 Seat) const;

    /** "Player 1" for seat 0. */
    static FText DescribeSeat(int32 Seat);

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);
    void HandleInputDevice(const FPSTelemetryInputDeviceEvent& Event);

    bool IsValidSeat(int32 Seat) const { return Seat >= 0 && Seat < SeatCount; }
    int32 GetOtherSeat(int32 Seat) const { return Seat == 0 ? 1 : 0; }
    UPSPlayCallSubsystem* GetPlayCall() const;

    /** Pauses the game (through Seat's controller) and opens the pause screen for every seat. */
    void EnterPause(int32 Seat);
    /** Starts the countdown once everyone needed is ready and connected. */
    void TryStartCountdown();
    void FinishResume();
    void CloseCallScreens(int32 Seat);
    void Publish(EPSVersusEventKind Kind, int32 Seat, const FString& Reason = FString());

    FPSVersusRules Rules;
    bool bRulesLoaded = false;

    /** The game mode's match setup (SetMatchSetup): where the seats' teams are read. */
    TWeakObjectPtr<const UPSMatchSetup> MatchSetup;

    UPROPERTY(Transient)
    TArray<FPSVersusSeat> Seats;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    EPSVersusPhase Phase = EPSVersusPhase::Inactive;

    /** What the bus last said: who has the ball, and the quarter (for the pauses per half). */
    bool bHomeHasPossession = true;
    int32 Half = 0;
    bool bPlayLive = false;

    int32 PausedBySeat = INDEX_NONE;
    float ResumeSecondsLeft = -1.f;
    int32 WinnerSeat = INDEX_NONE;
};
