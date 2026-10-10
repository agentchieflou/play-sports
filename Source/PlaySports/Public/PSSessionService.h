// PSSessionService.h - Epic 108.5: the platform-agnostic session and matchmaking service
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSSessionServiceTypes.h"
#include "PSSessionService.generated.h"

class UWorld;

DECLARE_MULTICAST_DELEGATE_OneParam(FPSSessionStateChangedMC, EPSSessionState);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSSessionMatchedMC, const FPSSessionMatch&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSSessionFailedMC, EPSSessionFailure);

/**
 * UPSSessionService is how the game finds an online opponent (Epic 108.5), whatever platform
 * service does the finding. The game codes against this class; each online backend (Epic Online
 * Services, a console's service, ...) is a subclass, chosen when online play is built (Epic 109).
 * UPSLocalSessionService is the one that exists now: an in-process stand-in for tests and for two
 * clients on one machine.
 *
 * A player either queues for matchmaking (StartMatchmaking) or hosts a listed session
 * (HostSession) that another finds (FindSessions) and joins (JoinSession). Both end in a match:
 * the two players by seat, the host (the machine that simulates the match: an authoritative
 * host, per Specs/ADR_Online_Architecture.md), the home seat and the match's seed. Both
 * machines get the same match, each with its own LocalSeat.
 *
 * Results arrive through OnMatched, OnFailed and OnStateChanged, as online services answer
 * later. The rules every backend shares are static and pure here: which requests can be matched
 * (CheckCompatible), how far apart ratings may be after a wait (GetSkillWindow), and who hosts
 * (ChooseHostSeat). The tuning is Data/session_matchmaking.json.
 *
 * The service lives above any one world (it is busy before the match's world exists). Its owner
 * (the front end's flow, Epic 109) hands the match to the match's world with ApplyMatchToWorld and
 * travels with GetTravelOptions.
 */
UCLASS(Abstract)
class PLAYSPORTS_API UPSSessionService : public UObject
{
    GENERATED_BODY()

public:
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion; tuning with
     *  problems is refused. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** The tuning in use, loaded from the default path on first use. */
    const FPSSessionMatchmakingTuning& GetTuning();

    /** Replaces the tuning (tests, a remote config); refused when ValidateTuning finds problems. */
    bool SetTuning(const FPSSessionMatchmakingTuning& InTuning);

    /** Problems with Candidate, one line each (empty when sound). */
    static TArray<FString> ValidateTuning(const FPSSessionMatchmakingTuning& Candidate);

    // --- What every backend implements -------------------------------------------------------

    /** Queues Request for a match. The match (OnMatched) or the failure (OnFailed) comes later.
     *  False when the service is already searching, hosting or matched (or has no backend):
     *  GetLastFailure is then Busy and what it was doing carries on. */
    virtual bool StartMatchmaking(const FPSMatchRequest& Request) PURE_VIRTUAL(UPSSessionService::StartMatchmaking, return false;);

    /** Leaves the matchmaking queue: back to Idle. */
    virtual void CancelMatchmaking() PURE_VIRTUAL(UPSSessionService::CancelMatchmaking, );

    /** Lists a session for Request's mode that others can find and join. False when busy, as
     *  for StartMatchmaking. */
    virtual bool HostSession(const FPSMatchRequest& Request) PURE_VIRTUAL(UPSSessionService::HostSession, return false;);

    /** The listed sessions with an open seat that Request could join: same mode and protocol,
     *  and cross-play policies that accept each other. */
    virtual TArray<FPSSessionListing> FindSessions(const FPSMatchRequest& Request) PURE_VIRTUAL(UPSSessionService::FindSessions, return TArray<FPSSessionListing>(););

    /** Joins a listed session. The match (or the failure: SessionNotFound, SessionFull, a mode,
     *  protocol or cross-play refusal) comes later, as for matchmaking. False when busy, as for
     *  StartMatchmaking. */
    virtual bool JoinSession(const FString& SessionId, const FPSMatchRequest& Request) PURE_VIRTUAL(UPSSessionService::JoinSession, return false;);

    /** Leaves whatever the service is in: the queue, a hosted session, or a match (the other
     *  player fails with OpponentLeft). Back to Idle. */
    virtual void LeaveSession() PURE_VIRTUAL(UPSSessionService::LeaveSession, );

    // --- State ---------------------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Session")
    EPSSessionState GetState() const { return State; }

    /** Why the last request failed; None after a success. */
    UFUNCTION(BlueprintPure, Category = "Session")
    EPSSessionFailure GetLastFailure() const { return LastFailure; }

    /** The match, once Matched; empty before. */
    const FPSSessionMatch& GetMatch() const { return Match; }

    /** A request for Player with this build's protocol, Mode, and the tuning's default cross-play
     *  policy for his controls (touch players meet touch players unless they opt in). */
    FPSMatchRequest MakeRequest(const FPSSessionPlayer& Player, FName Mode = TEXT("Versus"));

    // --- The rules every backend shares ---------------------------------------------------------

    /** Whether A and B can be matched now, the longer of their waits being LongestWaitSeconds:
     *  None when they can, else the first reason they can't. Mode, protocol and cross-play never
     *  change with waiting; the region (RegionRelaxSeconds) and the skill window do. With
     *  bForMatchmaking false (joining a listed session: a friend's invitation), region and
     *  skill are not checked. */
    static EPSSessionFailure CheckCompatible(const FPSMatchRequest& A, const FPSMatchRequest& B, float LongestWaitSeconds,
        const FPSSessionMatchmakingTuning& InTuning, bool bForMatchmaking = true);

    /** How far apart ratings may be after WaitedSeconds of matchmaking. */
    static float GetSkillWindow(float WaitedSeconds, const FPSSessionMatchmakingTuning& InTuning);

    /** How good a host Player's machine is: desktop, mains power, unmetered network. */
    static float GetHostScore(const FPSSessionPlayer& Player, const FPSSessionMatchmakingTuning& InTuning);

    /** The seat that hosts: the highest host score, the lower seat on a tie; INDEX_NONE when
     *  Players is empty. */
    static int32 ChooseHostSeat(const TArray<FPSSessionPlayer>& Players, const FPSSessionMatchmakingTuning& InTuning);

    /** A PC, Mac or Linux machine. */
    static bool IsDesktop(EPSSessionPlatform Platform);

    /** The platform this build runs on. */
    static EPSSessionPlatform GetCurrentPlatform();

    /** Hands Match to the match's World: its seeded random streams take the match seed
     *  (UPSNetRandomStreams). Call it before the game mode starts play, which seeds the play
     *  simulation from them. False for an invalid match or a world without the streams. */
    static bool ApplyMatchToWorld(const FPSSessionMatch& InMatch, UWorld* World);

    /** The travel options a matched machine opens the match's map with: Epic 107's versus options
     *  ("mode=Versus?homeseat=1", read by UPSVersusSubsystem) and the match seed
     *  ("matchseed=..."). Empty for an invalid match. */
    static FString GetTravelOptions(const FPSSessionMatch& InMatch);

    FPSSessionStateChangedMC OnStateChanged;
    FPSSessionMatchedMC OnMatched;
    FPSSessionFailedMC OnFailed;

protected:
    /** Moves to NewState and announces it (OnStateChanged) when it changed. */
    void SetState(EPSSessionState NewState);

    /** The match is made: kept, Matched, announced (OnMatched). */
    void CompleteMatch(const FPSSessionMatch& InMatch);

    /** The request failed: Failed, announced (OnFailed). */
    void Fail(EPSSessionFailure Failure);

    /** A request refused at once while the service is busy: GetLastFailure says why, and the
     *  state and the match are left as they were. */
    void Refuse(EPSSessionFailure Failure);

    /** Back to Idle with no match and no failure. */
    void ResetToIdle();

    /** A request with its protocol filled in from the tuning when it left it at 0. */
    FPSMatchRequest Resolve(const FPSMatchRequest& Request);

private:
    EPSSessionState State = EPSSessionState::Idle;
    EPSSessionFailure LastFailure = EPSSessionFailure::None;
    FPSSessionMatch Match;
    FPSSessionMatchmakingTuning Tuning;
    bool bTuningLoaded = false;
};
