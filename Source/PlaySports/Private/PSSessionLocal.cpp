// PSSessionLocal.cpp - Epic 108.5: the in-process session service (no network)
#include "PSSessionLocal.h"
#include "HAL/PlatformTime.h"

// --- The registry --------------------------------------------------------------------------------

UPSLocalSessionRegistry::UPSLocalSessionRegistry()
{
    // Matches made without SetSeed differ from run to run, as an online service's do.
    Seeds.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
}

bool UPSLocalSessionRegistry::SetTuning(const FPSSessionMatchmakingTuning& InTuning)
{
    if (UPSSessionService::ValidateTuning(InTuning).Num() > 0)
    {
        return false;
    }
    Tuning = InTuning;
    bTuningSet = true;
    return true;
}

const FPSSessionMatchmakingTuning& UPSLocalSessionRegistry::GetTuning()
{
    if (!bTuningSet)
    {
        bTuningSet = true;
        // The rules come from the data file, read the way every service reads it.
        UPSLocalSessionService* Reader = NewObject<UPSLocalSessionService>(this);
        Tuning = Reader->GetTuning();
    }
    return Tuning;
}

void UPSLocalSessionRegistry::SetSeed(int32 Seed)
{
    Seeds.Initialize(Seed);
}

void UPSLocalSessionRegistry::AdvanceTime(float DeltaSeconds)
{
    const FPSSessionMatchmakingTuning& Active = GetTuning();
    TArray<TWeakObjectPtr<UPSLocalSessionService>> TimedOut;
    for (int32 Index = Tickets.Num() - 1; Index >= 0; --Index)
    {
        Tickets[Index].WaitedSeconds += FMath::Max(0.f, DeltaSeconds);
        if (!Tickets[Index].Service.IsValid())
        {
            Tickets.RemoveAt(Index);
        }
        else if (Tickets[Index].WaitedSeconds > Active.MaxWaitSeconds)
        {
            TimedOut.Insert(Tickets[Index].Service, 0);
            Tickets.RemoveAt(Index);
        }
    }
    for (const TWeakObjectPtr<UPSLocalSessionService>& Service : TimedOut)
    {
        if (UPSLocalSessionService* Expired = Service.Get())
        {
            Expired->Fail(EPSSessionFailure::Timeout);
        }
    }
    MatchTickets();
}

void UPSLocalSessionRegistry::AddTicket(UPSLocalSessionService* Service, const FPSMatchRequest& Request)
{
    FTicket& Ticket = Tickets.AddDefaulted_GetRef();
    Ticket.Service = Service;
    Ticket.Request = Request;
    MatchTickets();
}

FString UPSLocalSessionRegistry::AddListedSession(UPSLocalSessionService* Host, const FPSMatchRequest& Request)
{
    FListedSession& Session = Listed.AddDefaulted_GetRef();
    Session.SessionId = NextSessionId();
    Session.Host = Host;
    Session.Request = Request;
    return Session.SessionId;
}

TArray<FPSSessionListing> UPSLocalSessionRegistry::FindSessions(const FPSMatchRequest& Request) const
{
    TArray<FPSSessionListing> Found;
    for (const FListedSession& Session : Listed)
    {
        if (!Session.Host.IsValid()
            || UPSSessionService::CheckCompatible(Session.Request, Request, 0.f, Tuning, false) != EPSSessionFailure::None)
        {
            continue;
        }
        FPSSessionListing& Listing = Found.AddDefaulted_GetRef();
        Listing.SessionId = Session.SessionId;
        Listing.Mode = Session.Request.Mode;
        Listing.ProtocolVersion = Session.Request.ProtocolVersion;
        Listing.Region = Session.Request.Region;
        Listing.Host = Session.Request.Player;
        Listing.OpenSeats = 1;
    }
    return Found;
}

EPSSessionFailure UPSLocalSessionRegistry::Join(UPSLocalSessionService* Joiner, const FString& SessionId, const FPSMatchRequest& Request)
{
    const int32 Index = Listed.IndexOfByPredicate([&SessionId](const FListedSession& Session) { return Session.SessionId == SessionId; });
    if (Index == INDEX_NONE || !Listed[Index].Host.IsValid())
    {
        // A made match's session is no longer listed: it is full.
        return Made.ContainsByPredicate([&SessionId](const FMadeMatch& Match) { return Match.SessionId == SessionId; })
            ? EPSSessionFailure::SessionFull : EPSSessionFailure::SessionNotFound;
    }
    if (Listed[Index].Host.Get() == Joiner)
    {
        return EPSSessionFailure::Busy;
    }
    const EPSSessionFailure Verdict = UPSSessionService::CheckCompatible(Listed[Index].Request, Request, 0.f, GetTuning(), false);
    if (Verdict != EPSSessionFailure::None)
    {
        return Verdict;
    }

    // Copied out first: the list changes before the match is handed out.
    const FListedSession Session = Listed[Index];
    Listed.RemoveAt(Index);
    MakeMatch(Session.SessionId, Session.Host.Get(), Session.Request, Joiner, Request, 0);
    return EPSSessionFailure::None;
}

void UPSLocalSessionRegistry::Remove(UPSLocalSessionService* Service)
{
    Tickets.RemoveAll([Service](const FTicket& Ticket) { return Ticket.Service.Get() == Service; });
    Listed.RemoveAll([Service](const FListedSession& Session) { return Session.Host.Get() == Service; });

    TArray<TWeakObjectPtr<UPSLocalSessionService>> Abandoned;
    for (int32 Index = Made.Num() - 1; Index >= 0; --Index)
    {
        const FMadeMatch& Match = Made[Index];
        if (Match.Seats[0].Get() == Service || Match.Seats[1].Get() == Service)
        {
            Abandoned.Add(Match.Seats[0].Get() == Service ? Match.Seats[1] : Match.Seats[0]);
            Made.RemoveAt(Index);
        }
    }
    for (const TWeakObjectPtr<UPSLocalSessionService>& Other : Abandoned)
    {
        if (UPSLocalSessionService* Left = Other.Get())
        {
            Left->Fail(EPSSessionFailure::OpponentLeft);
        }
    }
}

void UPSLocalSessionRegistry::MatchTickets()
{
    const FPSSessionMatchmakingTuning& Active = GetTuning();
    bool bMatched = true;
    while (bMatched)
    {
        bMatched = false;
        for (int32 First = 0; First < Tickets.Num() && !bMatched; ++First)
        {
            for (int32 Second = First + 1; Second < Tickets.Num() && !bMatched; ++Second)
            {
                const FTicket& Older = Tickets[First];
                const FTicket& Newer = Tickets[Second];
                if (!Older.Service.IsValid() || !Newer.Service.IsValid())
                {
                    continue;
                }
                const float LongestWait = FMath::Max(Older.WaitedSeconds, Newer.WaitedSeconds);
                if (UPSSessionService::CheckCompatible(Older.Request, Newer.Request, LongestWait, Active) != EPSSessionFailure::None)
                {
                    continue;
                }

                // Copied out first: the queue changes before the match is handed out.
                const FTicket OlderTicket = Older;
                const FTicket NewerTicket = Newer;
                Tickets.RemoveAt(Second);
                Tickets.RemoveAt(First);
                const TArray<FPSSessionPlayer> Players = { OlderTicket.Request.Player, NewerTicket.Request.Player };
                MakeMatch(NextSessionId(), OlderTicket.Service.Get(), OlderTicket.Request, NewerTicket.Service.Get(), NewerTicket.Request,
                    UPSSessionService::ChooseHostSeat(Players, Active));
                bMatched = true;
            }
        }
    }
}

void UPSLocalSessionRegistry::MakeMatch(const FString& SessionId, UPSLocalSessionService* First, const FPSMatchRequest& FirstRequest,
    UPSLocalSessionService* Second, const FPSMatchRequest& SecondRequest, int32 HostSeat)
{
    FPSSessionMatch Match;
    Match.SessionId = SessionId;
    Match.Mode = FirstRequest.Mode;
    Match.ProtocolVersion = FirstRequest.ProtocolVersion;
    Match.Players = { FirstRequest.Player, SecondRequest.Player };
    Match.HostSeat = HostSeat;
    // The seed is drawn first, then the home seat; 0 is the replay format's "unseeded".
    const int32 Seed = static_cast<int32>(Seeds.GetUnsignedInt());
    Match.MatchSeed = Seed != 0 ? Seed : 1;
    Match.HomeSeat = Seeds.RandRange(0, 1);

    FMadeMatch& Record = Made.AddDefaulted_GetRef();
    Record.SessionId = SessionId;
    Record.Seats[0] = First;
    Record.Seats[1] = Second;

    FPSSessionMatch FirstCopy = Match;
    FirstCopy.LocalSeat = 0;
    FPSSessionMatch SecondCopy = Match;
    SecondCopy.LocalSeat = 1;
    if (First)
    {
        First->CompleteMatch(FirstCopy);
    }
    if (Second)
    {
        Second->CompleteMatch(SecondCopy);
    }
}

FString UPSLocalSessionRegistry::NextSessionId()
{
    ++SessionCount;
    return FString::Printf(TEXT("LOCAL-%04d"), SessionCount);
}

// --- The service ---------------------------------------------------------------------------------

void UPSLocalSessionService::Connect(UPSLocalSessionRegistry* InRegistry)
{
    if (Registry && Registry != InRegistry)
    {
        Registry->Remove(this);
    }
    Registry = InRegistry;
}

bool UPSLocalSessionService::IsFree() const
{
    return Registry && (GetState() == EPSSessionState::Idle || GetState() == EPSSessionState::Failed);
}

bool UPSLocalSessionService::StartMatchmaking(const FPSMatchRequest& Request)
{
    if (!IsFree())
    {
        Refuse(EPSSessionFailure::Busy);
        return false;
    }
    ResetToIdle();
    SetState(EPSSessionState::Searching);
    // The registry may match the ticket at once, before this returns.
    Registry->AddTicket(this, Resolve(Request));
    return true;
}

void UPSLocalSessionService::CancelMatchmaking()
{
    if (GetState() == EPSSessionState::Searching && Registry)
    {
        Registry->Remove(this);
        ResetToIdle();
    }
}

bool UPSLocalSessionService::HostSession(const FPSMatchRequest& Request)
{
    if (!IsFree())
    {
        Refuse(EPSSessionFailure::Busy);
        return false;
    }
    ResetToIdle();
    Registry->AddListedSession(this, Resolve(Request));
    SetState(EPSSessionState::Hosting);
    return true;
}

TArray<FPSSessionListing> UPSLocalSessionService::FindSessions(const FPSMatchRequest& Request)
{
    if (!Registry)
    {
        return TArray<FPSSessionListing>();
    }
    TArray<FPSSessionListing> Found = Registry->FindSessions(Resolve(Request));
    // Not one's own session.
    Found.RemoveAll([this, &Request](const FPSSessionListing& Listing)
    {
        return GetState() == EPSSessionState::Hosting && Listing.Host.AccountId == Request.Player.AccountId;
    });
    return Found;
}

bool UPSLocalSessionService::JoinSession(const FString& SessionId, const FPSMatchRequest& Request)
{
    if (!IsFree())
    {
        Refuse(EPSSessionFailure::Busy);
        return false;
    }
    ResetToIdle();
    const EPSSessionFailure Verdict = Registry->Join(this, SessionId, Resolve(Request));
    if (Verdict != EPSSessionFailure::None)
    {
        Fail(Verdict);
    }
    return true;
}

void UPSLocalSessionService::LeaveSession()
{
    if (Registry)
    {
        Registry->Remove(this);
    }
    ResetToIdle();
}
