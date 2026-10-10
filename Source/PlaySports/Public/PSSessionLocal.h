// PSSessionLocal.h - Epic 108.5: the in-process session service (no network)
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "UObject/Object.h"
#include "PSSessionService.h"
#include "PSSessionLocal.generated.h"

class UPSLocalSessionService;

/**
 * UPSLocalSessionRegistry is the stand-in for an online backend, in one process: the matchmaking
 * queue, the listed sessions and the matches made, shared by every UPSLocalSessionService
 * connected to it. Two services on one registry are two machines finding each other, which is
 * how the tests play both sides, and how two clients on one PC can.
 *
 * Matchmaking is first come, first served: on every AdvanceTime (and whenever a ticket joins),
 * each ticket in the order it queued is matched with the first later ticket it is compatible
 * with (UPSSessionService::CheckCompatible, the longer wait of the two widening the skill window
 * and relaxing the region). A ticket that waits longer than MaxWaitSeconds fails with Timeout.
 * In a match the older ticket sits in seat 0, the host is UPSSessionService::ChooseHostSeat's, and
 * the match seed and home seat come from the registry's own stream (SetSeed for tests; the clock
 * otherwise). A listed session's host sits in seat 0 and hosts.
 *
 * The owner advances the registry's clock (AdvanceTime), once a frame or as a test steps.
 */
UCLASS()
class PLAYSPORTS_API UPSLocalSessionRegistry : public UObject
{
    GENERATED_BODY()

public:
    UPSLocalSessionRegistry();

    /** The rules this registry matches by (by default Data/session_matchmaking.json's). False
     *  when UPSSessionService::ValidateTuning finds problems. */
    bool SetTuning(const FPSSessionMatchmakingTuning& InTuning);
    const FPSSessionMatchmakingTuning& GetTuning();

    /** Seeds the match seeds and home seats the registry hands out. */
    void SetSeed(int32 Seed);

    /** Waits every ticket on by DeltaSeconds, fails those past MaxWaitSeconds, and matches. */
    void AdvanceTime(float DeltaSeconds);

    /** Tickets waiting, and sessions listed and still open. */
    int32 GetQueuedCount() const { return Tickets.Num(); }
    int32 GetListedCount() const { return Listed.Num(); }

private:
    friend class UPSLocalSessionService;

    struct FTicket
    {
        TWeakObjectPtr<UPSLocalSessionService> Service;
        FPSMatchRequest Request;
        float WaitedSeconds = 0.f;
    };

    struct FListedSession
    {
        FString SessionId;
        TWeakObjectPtr<UPSLocalSessionService> Host;
        FPSMatchRequest Request;
    };

    /** A made match and its two services, by seat, so leaving can tell the other. */
    struct FMadeMatch
    {
        FString SessionId;
        TWeakObjectPtr<UPSLocalSessionService> Seats[2];
    };

    void AddTicket(UPSLocalSessionService* Service, const FPSMatchRequest& Request);
    FString AddListedSession(UPSLocalSessionService* Host, const FPSMatchRequest& Request);
    TArray<FPSSessionListing> FindSessions(const FPSMatchRequest& Request) const;
    EPSSessionFailure Join(UPSLocalSessionService* Joiner, const FString& SessionId, const FPSMatchRequest& Request);

    /** Takes Service out of the queue, its listed session off the list, and it out of any match
     *  (the other seat fails with OpponentLeft). */
    void Remove(UPSLocalSessionService* Service);

    /** Pairs compatible tickets, oldest first. */
    void MatchTickets();

    /** Makes the match between the two seats and hands each service its copy. */
    void MakeMatch(const FString& SessionId, UPSLocalSessionService* First, const FPSMatchRequest& FirstRequest,
        UPSLocalSessionService* Second, const FPSMatchRequest& SecondRequest, int32 HostSeat);

    FString NextSessionId();

    TArray<FTicket> Tickets;
    TArray<FListedSession> Listed;
    TArray<FMadeMatch> Made;
    FPSSessionMatchmakingTuning Tuning;
    bool bTuningSet = false;
    FRandomStream Seeds;
    int32 SessionCount = 0;
};

/**
 * UPSLocalSessionService is the session service with no network (Epic 108.5): it matches with the
 * other services connected to the same UPSLocalSessionRegistry. With no other service there it
 * searches until matchmaking times out, as an online service with nobody else playing would.
 * Results arrive through the service's delegates when the registry makes them.
 */
UCLASS()
class PLAYSPORTS_API UPSLocalSessionService : public UPSSessionService
{
    GENERATED_BODY()

public:
    /** Connects to the registry other local services share. Until then requests are refused
     *  (Busy). */
    void Connect(UPSLocalSessionRegistry* InRegistry);

    UPSLocalSessionRegistry* GetRegistry() const { return Registry; }

    virtual bool StartMatchmaking(const FPSMatchRequest& Request) override;
    virtual void CancelMatchmaking() override;
    virtual bool HostSession(const FPSMatchRequest& Request) override;
    virtual TArray<FPSSessionListing> FindSessions(const FPSMatchRequest& Request) override;
    virtual bool JoinSession(const FString& SessionId, const FPSMatchRequest& Request) override;
    virtual void LeaveSession() override;

private:
    friend class UPSLocalSessionRegistry;

    /** Idle or Failed: free to make a new request. */
    bool IsFree() const;

    UPROPERTY(Transient)
    TObjectPtr<UPSLocalSessionRegistry> Registry;
};
