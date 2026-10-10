// PSSessionServiceTests.cpp -- Epic 108.5: the session and matchmaking service
//
// Tests covered:
//   1. The rules every backend shares: the tuning from Data/session_matchmaking.json and its
//      checks; which requests match (mode, protocol, cross-play, region, the skill window as the
//      wait grows); who hosts; the default cross-play policy by controls; the travel options a
//      match travels with, read back by Epic 107's versus URL helpers.
//   2. Matchmaking on the local service: a PC player and an iPhone player queue, are matched once
//      the skill window has widened enough, get the same match with their own seats (the PC
//      hosts), and the match seeds both machines' random streams alike; leaving tells the other.
//      A touch player who didn't opt in to cross-play never meets a keyboard player, and both
//      time out. A busy service refuses a second request and carries on.
//   3. Hosting and joining: a listed session is found only by players its cross-play policy
//      accepts; joining makes the match (the lobby's host hosts); a full, unknown or other-mode
//      session is refused with the reason.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "PSNetRandomStreams.h"
#include "PSSessionLocal.h"
#include "PSSessionService.h"
#include "PSVersusSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSSessionServiceTests
{
    UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    FPSSessionPlayer MakePlayer(const TCHAR* AccountId, EPSSessionPlatform Platform, EPSSessionInput Input, int32 Rating,
        bool bOnBattery = false, bool bOnMeteredNetwork = false)
    {
        FPSSessionPlayer Player;
        Player.AccountId = AccountId;
        Player.DisplayName = AccountId;
        Player.Platform = Platform;
        Player.Input = Input;
        Player.SkillRating = Rating;
        Player.bOnBattery = bOnBattery;
        Player.bOnMeteredNetwork = bOnMeteredNetwork;
        return Player;
    }

    FPSMatchRequest MakeRequest(const FPSSessionPlayer& Player, EPSCrossPlayPolicy CrossPlay, int32 ProtocolVersion = 1)
    {
        FPSMatchRequest Request;
        Request.Player = Player;
        Request.CrossPlay = CrossPlay;
        Request.ProtocolVersion = ProtocolVersion;
        return Request;
    }

    /** A local service on Registry that counts what it hears. */
    struct FPSSessionProbe
    {
        UPSLocalSessionService* Service = nullptr;
        int32 Matches = 0;
        TArray<EPSSessionFailure> Failures;

        void Connect(UPSLocalSessionRegistry* Registry)
        {
            Service = NewObject<UPSLocalSessionService>();
            Service->Connect(Registry);
            Service->OnMatched.AddLambda([this](const FPSSessionMatch&) { ++Matches; });
            Service->OnFailed.AddLambda([this](EPSSessionFailure Failure) { Failures.Add(Failure); });
        }
    };
}

// ---------------------------------------------------------------------------
// 1. The shared rules
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSessionRulesTest,
    "PlaySports.Session.Rules",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSessionRulesTest::RunTest(const FString& Parameters)
{
    using namespace PSSessionServiceTests;

    UPSLocalSessionService* Service = NewObject<UPSLocalSessionService>();
    TestTrue(TEXT("Data/session_matchmaking.json loads and is sound"), Service->LoadTuningFromJson(UPSSessionService::GetDefaultTuningPath()));
    const FPSSessionMatchmakingTuning Tuning = Service->GetTuning();
    const FPSSessionMatchmakingTuning Defaults;
    TestEqual(TEXT("The file and the struct's defaults agree: protocol"), Tuning.ProtocolVersion, Defaults.ProtocolVersion);
    TestEqual(TEXT("...the skill window"), Tuning.InitialSkillWindow, Defaults.InitialSkillWindow);
    TestEqual(TEXT("...touch players' default"), Tuning.TouchDefaultCrossPlay, Defaults.TouchDefaultCrossPlay);

    FPSSessionMatchmakingTuning Broken = Tuning;
    Broken.ProtocolVersion = 0;
    Broken.MaxSkillWindow = Tuning.InitialSkillWindow - 1.f;
    Broken.MaxWaitSeconds = 0.f;
    TestEqual(TEXT("Bad tuning is reported line by line"), UPSSessionService::ValidateTuning(Broken).Num(), 3);
    TestFalse(TEXT("...and refused"), Service->SetTuning(Broken));

    // Compatibility.
    const FPSSessionPlayer Pc = MakePlayer(TEXT("pc"), EPSSessionPlatform::Windows, EPSSessionInput::Gamepad, 1000);
    const FPSSessionPlayer Phone = MakePlayer(TEXT("phone"), EPSSessionPlatform::IOS, EPSSessionInput::Touch, 1000, true, true);
    const FPSMatchRequest PcAnyone = MakeRequest(Pc, EPSCrossPlayPolicy::Anyone);
    const FPSMatchRequest PhoneAnyone = MakeRequest(Phone, EPSCrossPlayPolicy::Anyone);
    TestEqual(TEXT("A PC and an iPhone that both accept anyone match"), UPSSessionService::CheckCompatible(PcAnyone, PhoneAnyone, 0.f, Tuning), EPSSessionFailure::None);
    TestEqual(TEXT("A touch player keeping to touch refuses a gamepad"),
        UPSSessionService::CheckCompatible(PcAnyone, MakeRequest(Phone, EPSCrossPlayPolicy::SameInput), 0.f, Tuning), EPSSessionFailure::CrossPlayRefused);
    TestEqual(TEXT("...and a player keeping to his platform refuses another"),
        UPSSessionService::CheckCompatible(MakeRequest(Pc, EPSCrossPlayPolicy::SamePlatform), PhoneAnyone, 0.f, Tuning), EPSSessionFailure::CrossPlayRefused);
    FPSMatchRequest OtherMode = PhoneAnyone;
    OtherMode.Mode = TEXT("Franchise");
    TestEqual(TEXT("Different modes never match"), UPSSessionService::CheckCompatible(PcAnyone, OtherMode, 999.f, Tuning), EPSSessionFailure::ModeMismatch);
    TestEqual(TEXT("Different protocols never match"),
        UPSSessionService::CheckCompatible(PcAnyone, MakeRequest(Phone, EPSCrossPlayPolicy::Anyone, 2), 999.f, Tuning), EPSSessionFailure::ProtocolMismatch);

    FPSMatchRequest Europe = PcAnyone;
    Europe.Region = TEXT("eu");
    FPSMatchRequest America = PhoneAnyone;
    America.Region = TEXT("us-west");
    TestEqual(TEXT("Different regions wait ..."), UPSSessionService::CheckCompatible(Europe, America, 0.f, Tuning), EPSSessionFailure::RegionMismatch);
    TestEqual(TEXT("... until RegionRelaxSeconds"), UPSSessionService::CheckCompatible(Europe, America, Tuning.RegionRelaxSeconds, Tuning), EPSSessionFailure::None);
    TestEqual(TEXT("A friend's invitation ignores the region"), UPSSessionService::CheckCompatible(Europe, America, 0.f, Tuning, false), EPSSessionFailure::None);

    const FPSMatchRequest Stronger = MakeRequest(MakePlayer(TEXT("pro"), EPSSessionPlatform::Windows, EPSSessionInput::Gamepad, 1300), EPSCrossPlayPolicy::Anyone);
    const float GapSeconds = (300.f - Tuning.InitialSkillWindow) / Tuning.SkillWindowGrowthPerSecond;
    TestEqual(TEXT("Ratings 300 apart wait ..."), UPSSessionService::CheckCompatible(PcAnyone, Stronger, 0.f, Tuning), EPSSessionFailure::SkillGap);
    TestEqual(TEXT("... until the window has widened to 300"), UPSSessionService::CheckCompatible(PcAnyone, Stronger, GapSeconds, Tuning), EPSSessionFailure::None);
    TestEqual(TEXT("The window starts at InitialSkillWindow"), UPSSessionService::GetSkillWindow(0.f, Tuning), Tuning.InitialSkillWindow);
    TestEqual(TEXT("...and stops at MaxSkillWindow"), UPSSessionService::GetSkillWindow(1.0e6f, Tuning), Tuning.MaxSkillWindow);

    // Who hosts.
    TestEqual(TEXT("A PC on mains and Wi-Fi scores every host point"), UPSSessionService::GetHostScore(Pc, Tuning),
        Tuning.HostScoreDesktop + Tuning.HostScoreOnPower + Tuning.HostScoreUnmetered);
    TestEqual(TEXT("An iPhone on battery and cellular scores none"), UPSSessionService::GetHostScore(Phone, Tuning), 0.f);
    TestEqual(TEXT("The PC hosts, whichever seat it has"), UPSSessionService::ChooseHostSeat({ Phone, Pc }, Tuning), 1);
    const FPSSessionPlayer PluggedPhone = MakePlayer(TEXT("plugged"), EPSSessionPlatform::IOS, EPSSessionInput::Touch, 1000);
    TestEqual(TEXT("Between two phones, the one on power and Wi-Fi hosts"), UPSSessionService::ChooseHostSeat({ Phone, PluggedPhone }, Tuning), 1);
    TestEqual(TEXT("A tie goes to the first seat"), UPSSessionService::ChooseHostSeat({ Pc, Pc }, Tuning), 0);
    TestEqual(TEXT("Nobody, no host"), UPSSessionService::ChooseHostSeat({}, Tuning), static_cast<int32>(INDEX_NONE));
    TestTrue(TEXT("Windows, Mac and Linux are desktops"), UPSSessionService::IsDesktop(EPSSessionPlatform::Mac) && !UPSSessionService::IsDesktop(EPSSessionPlatform::IOS));

    // Default requests.
    const FPSMatchRequest TouchDefault = Service->MakeRequest(Phone);
    TestEqual(TEXT("A touch player's request keeps to touch by default"), TouchDefault.CrossPlay, Tuning.TouchDefaultCrossPlay);
    TestEqual(TEXT("...a gamepad player's accepts the tuning's default"), Service->MakeRequest(Pc).CrossPlay, Tuning.DefaultCrossPlay);
    TestEqual(TEXT("...with this build's protocol"), TouchDefault.ProtocolVersion, Tuning.ProtocolVersion);
    TestEqual(TEXT("...for head-to-head"), TouchDefault.Mode, FName(TEXT("Versus")));

    // The travel options, read back by the versus subsystem's URL helpers (Epic 107).
    FPSSessionMatch Match;
    Match.SessionId = TEXT("S");
    Match.Mode = TEXT("Versus");
    Match.Players = { Pc, Phone };
    Match.HostSeat = 0;
    Match.HomeSeat = 1;
    Match.LocalSeat = 1;
    Match.MatchSeed = 424242;
    TestTrue(TEXT("A full match is valid"), Match.IsValid());
    TestFalse(TEXT("...and the phone's copy is not the host"), Match.IsLocalHost());
    const FString Options = UPSSessionService::GetTravelOptions(Match);
    TestEqual(TEXT("The travel options"), Options, FString(TEXT("mode=Versus?homeseat=1?matchseed=424242")));
    FURL Url;
    TArray<FString> Parts;
    Options.ParseIntoArray(Parts, TEXT("?"));
    for (const FString& Part : Parts)
    {
        Url.AddOption(*Part);
    }
    TestTrue(TEXT("...are a versus game to the versus subsystem"), UPSVersusSubsystem::IsVersusURL(Url));
    TestEqual(TEXT("...with the match's home seat"), UPSVersusSubsystem::GetHomeSeatFromURL(Url), 1);
    TestEqual(TEXT("...and the match seed"), FCString::Atoi(Url.GetOption(TEXT("matchseed="), TEXT("0"))), 424242);
    FPSSessionMatch Unseeded = Match;
    Unseeded.MatchSeed = 0;
    TestFalse(TEXT("A match without a seed is not valid"), Unseeded.IsValid());
    TestTrue(TEXT("...and travels nowhere"), UPSSessionService::GetTravelOptions(Unseeded).IsEmpty());
    return true;
}

// ---------------------------------------------------------------------------
// 2. Matchmaking on the local service
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSessionLocalMatchmakingTest,
    "PlaySports.Session.LocalMatchmaking",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSessionLocalMatchmakingTest::RunTest(const FString& Parameters)
{
    using namespace PSSessionServiceTests;

    UPSLocalSessionRegistry* Registry = NewObject<UPSLocalSessionRegistry>();
    Registry->SetSeed(108);
    const FPSSessionMatchmakingTuning Tuning = Registry->GetTuning();

    FPSSessionProbe Desk;
    FPSSessionProbe Phone;
    Desk.Connect(Registry);
    Phone.Connect(Registry);

    // The PC player queues alone: nobody to meet.
    const FPSSessionPlayer DeskPlayer = MakePlayer(TEXT("desk"), EPSSessionPlatform::Windows, EPSSessionInput::KeyboardMouse, 1000);
    TestTrue(TEXT("The PC player queues"), Desk.Service->StartMatchmaking(MakeRequest(DeskPlayer, EPSCrossPlayPolicy::Anyone, 0)));
    TestEqual(TEXT("...and searches"), Desk.Service->GetState(), EPSSessionState::Searching);
    TestFalse(TEXT("A second request while searching is refused"), Desk.Service->HostSession(MakeRequest(DeskPlayer, EPSCrossPlayPolicy::Anyone)));
    TestEqual(TEXT("...as busy"), Desk.Service->GetLastFailure(), EPSSessionFailure::Busy);
    TestEqual(TEXT("...and the search carries on"), Desk.Service->GetState(), EPSSessionState::Searching);
    TestFalse(TEXT("So is a second search"), Desk.Service->StartMatchmaking(MakeRequest(DeskPlayer, EPSCrossPlayPolicy::Anyone, 0)));
    TestEqual(TEXT("He is queued once"), Registry->GetQueuedCount(), 1);
    TestEqual(TEXT("Nothing listed"), Registry->GetListedCount(), 0);
    TestEqual(TEXT("A refusal is not a failure: nothing announced"), Desk.Failures.Num(), 0);

    // The iPhone player, 250 points stronger, opted in to cross-play.
    const FPSSessionPlayer PhonePlayer = MakePlayer(TEXT("phone"), EPSSessionPlatform::IOS, EPSSessionInput::Touch, 1250, true, true);
    TestTrue(TEXT("The iPhone player queues"), Phone.Service->StartMatchmaking(MakeRequest(PhonePlayer, EPSCrossPlayPolicy::Anyone, 0)));
    TestEqual(TEXT("250 points apart: no match yet"), Registry->GetQueuedCount(), 2);
    const float WidensAt = (250.f - Tuning.InitialSkillWindow) / Tuning.SkillWindowGrowthPerSecond;
    Registry->AdvanceTime(WidensAt * 0.5f);
    TestEqual(TEXT("Half way there: still none"), Desk.Matches + Phone.Matches, 0);
    Registry->AdvanceTime(WidensAt * 0.5f + 0.01f);
    if (!TestEqual(TEXT("Once the window is 250 wide they are matched, each told once"), Desk.Matches * 10 + Phone.Matches, 11))
    {
        return false;
    }
    TestEqual(TEXT("The queue is empty"), Registry->GetQueuedCount(), 0);

    const FPSSessionMatch& DeskMatch = Desk.Service->GetMatch();
    const FPSSessionMatch& PhoneMatch = Phone.Service->GetMatch();
    TestEqual(TEXT("Both are matched"), Phone.Service->GetState(), EPSSessionState::Matched);
    TestTrue(TEXT("Both matches are valid"), DeskMatch.IsValid() && PhoneMatch.IsValid());
    TestEqual(TEXT("The same session"), PhoneMatch.SessionId, DeskMatch.SessionId);
    TestEqual(TEXT("The same seed"), PhoneMatch.MatchSeed, DeskMatch.MatchSeed);
    TestEqual(TEXT("The same home seat"), PhoneMatch.HomeSeat, DeskMatch.HomeSeat);
    TestEqual(TEXT("The first to queue sits in seat 0"), DeskMatch.Players[0].AccountId, FString(TEXT("desk")));
    TestEqual(TEXT("Each machine knows its own seat: the PC's"), DeskMatch.LocalSeat, 0);
    TestEqual(TEXT("...and the phone's"), PhoneMatch.LocalSeat, 1);
    TestEqual(TEXT("The PC hosts"), DeskMatch.HostSeat, 0);
    TestTrue(TEXT("...so the PC's copy is the host's"), DeskMatch.IsLocalHost() && !PhoneMatch.IsLocalHost());
    TestEqual(TEXT("The protocol is the tuning's"), DeskMatch.ProtocolVersion, Tuning.ProtocolVersion);

    // The match seeds both machines' streams alike.
    UWorld* DeskWorld = CreateTestWorld();
    UWorld* PhoneWorld = CreateTestWorld();
    TestTrue(TEXT("The PC's world takes the match"), UPSSessionService::ApplyMatchToWorld(DeskMatch, DeskWorld));
    TestTrue(TEXT("...and the phone's"), UPSSessionService::ApplyMatchToWorld(PhoneMatch, PhoneWorld));
    UPSNetRandomStreams* DeskStreams = DeskWorld ? DeskWorld->GetSubsystem<UPSNetRandomStreams>() : nullptr;
    UPSNetRandomStreams* PhoneStreams = PhoneWorld ? PhoneWorld->GetSubsystem<UPSNetRandomStreams>() : nullptr;
    if (TestTrue(TEXT("Both worlds have their streams"), DeskStreams && PhoneStreams))
    {
        TestEqual(TEXT("The PC's streams have the match seed"), DeskStreams->GetMatchSeed(), DeskMatch.MatchSeed);
        TestEqual(TEXT("...as do the phone's"), PhoneStreams->GetMatchSeed(), DeskMatch.MatchSeed);
    }
    TestFalse(TEXT("No match, nothing to apply"), UPSSessionService::ApplyMatchToWorld(FPSSessionMatch(), DeskWorld));
    if (DeskWorld)
    {
        DestroyTestWorld(DeskWorld);
    }
    if (PhoneWorld)
    {
        DestroyTestWorld(PhoneWorld);
    }

    // The same registry seed makes the same match.
    UPSLocalSessionRegistry* Again = NewObject<UPSLocalSessionRegistry>();
    Again->SetSeed(108);
    FPSSessionProbe DeskAgain;
    FPSSessionProbe PhoneAgain;
    DeskAgain.Connect(Again);
    PhoneAgain.Connect(Again);
    DeskAgain.Service->StartMatchmaking(MakeRequest(DeskPlayer, EPSCrossPlayPolicy::Anyone, 0));
    PhoneAgain.Service->StartMatchmaking(MakeRequest(PhonePlayer, EPSCrossPlayPolicy::Anyone, 0));
    Again->AdvanceTime(WidensAt + 0.01f);
    TestEqual(TEXT("A registry seeded alike hands out the same match seed"), DeskAgain.Service->GetMatch().MatchSeed, DeskMatch.MatchSeed);

    // Leaving tells the other machine.
    Phone.Service->LeaveSession();
    TestEqual(TEXT("The leaver is idle"), Phone.Service->GetState(), EPSSessionState::Idle);
    TestEqual(TEXT("The other is told his opponent left"), Desk.Service->GetLastFailure(), EPSSessionFailure::OpponentLeft);
    TestEqual(TEXT("...and is no longer matched"), Desk.Service->GetState(), EPSSessionState::Failed);

    // A touch player keeping to touch never meets a keyboard player; both give up in the end.
    FPSSessionProbe Toucher;
    FPSSessionProbe Typist;
    Toucher.Connect(Registry);
    Typist.Connect(Registry);
    Toucher.Service->StartMatchmaking(Toucher.Service->MakeRequest(MakePlayer(TEXT("toucher"), EPSSessionPlatform::IOS, EPSSessionInput::Touch, 1000)));
    Typist.Service->StartMatchmaking(Typist.Service->MakeRequest(MakePlayer(TEXT("typist"), EPSSessionPlatform::Windows, EPSSessionInput::KeyboardMouse, 1000)));
    Registry->AdvanceTime(Tuning.MaxWaitSeconds * 0.5f);
    TestEqual(TEXT("Different controls, touch keeping to touch: no match however long"), Toucher.Matches + Typist.Matches, 0);
    Registry->AdvanceTime(Tuning.MaxWaitSeconds * 0.5f + 1.f);
    TestEqual(TEXT("Matchmaking gives up on the touch player"), Toucher.Service->GetLastFailure(), EPSSessionFailure::Timeout);
    TestEqual(TEXT("...and on the keyboard player"), Typist.Service->GetLastFailure(), EPSSessionFailure::Timeout);
    TestEqual(TEXT("...emptying the queue"), Registry->GetQueuedCount(), 0);

    // Cancelling leaves the queue.
    Toucher.Service->StartMatchmaking(Toucher.Service->MakeRequest(MakePlayer(TEXT("toucher"), EPSSessionPlatform::IOS, EPSSessionInput::Touch, 1000)));
    Toucher.Service->CancelMatchmaking();
    TestEqual(TEXT("A cancelled search is idle"), Toucher.Service->GetState(), EPSSessionState::Idle);
    TestEqual(TEXT("...and out of the queue"), Registry->GetQueuedCount(), 0);

    UPSLocalSessionService* Unconnected = NewObject<UPSLocalSessionService>();
    TestFalse(TEXT("A service with no backend refuses to search"), Unconnected->StartMatchmaking(MakeRequest(DeskPlayer, EPSCrossPlayPolicy::Anyone)));
    return true;
}

// ---------------------------------------------------------------------------
// 3. Hosting and joining a listed session
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSSessionHostAndJoinTest,
    "PlaySports.Session.HostAndJoin",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSessionHostAndJoinTest::RunTest(const FString& Parameters)
{
    using namespace PSSessionServiceTests;

    UPSLocalSessionRegistry* Registry = NewObject<UPSLocalSessionRegistry>();
    Registry->SetSeed(5);
    FPSSessionProbe Host;
    FPSSessionProbe Guest;
    FPSSessionProbe Latecomer;
    Host.Connect(Registry);
    Guest.Connect(Registry);
    Latecomer.Connect(Registry);

    const FPSSessionPlayer HostPlayer = MakePlayer(TEXT("host"), EPSSessionPlatform::IOS, EPSSessionInput::Touch, 900, true, true);
    const FPSSessionPlayer GuestPlayer = MakePlayer(TEXT("guest"), EPSSessionPlatform::Windows, EPSSessionInput::Gamepad, 1500);
    const FPSMatchRequest GuestRequest = MakeRequest(GuestPlayer, EPSCrossPlayPolicy::Anyone);

    // Hosted keeping to touch: the gamepad player can't see it.
    TestTrue(TEXT("The iPhone player hosts, keeping to touch"), Host.Service->HostSession(MakeRequest(HostPlayer, EPSCrossPlayPolicy::SameInput)));
    TestEqual(TEXT("...and is hosting"), Host.Service->GetState(), EPSSessionState::Hosting);
    TestEqual(TEXT("A gamepad player finds no session he may join"), Guest.Service->FindSessions(GuestRequest).Num(), 0);

    // Hosted open to anyone: listed for him, whatever their ratings.
    Host.Service->LeaveSession();
    TestEqual(TEXT("Leaving takes the session off the list"), Registry->GetListedCount(), 0);
    TestTrue(TEXT("The iPhone player hosts again, open to anyone"), Host.Service->HostSession(MakeRequest(HostPlayer, EPSCrossPlayPolicy::Anyone)));
    const TArray<FPSSessionListing> Found = Guest.Service->FindSessions(GuestRequest);
    if (!TestEqual(TEXT("The gamepad player finds it, 600 rating points away"), Found.Num(), 1))
    {
        return false;
    }
    TestEqual(TEXT("...hosted by the iPhone player"), Found[0].Host.AccountId, FString(TEXT("host")));
    TestEqual(TEXT("...with a seat open"), Found[0].OpenSeats, 1);
    FPSMatchRequest Franchise = GuestRequest;
    Franchise.Mode = TEXT("Franchise");
    TestEqual(TEXT("A search for another mode doesn't list it"), Guest.Service->FindSessions(Franchise).Num(), 0);

    TestTrue(TEXT("He joins"), Guest.Service->JoinSession(Found[0].SessionId, GuestRequest));
    TestEqual(TEXT("Both are matched"), Host.Matches * 10 + Guest.Matches, 11);
    const FPSSessionMatch& Match = Guest.Service->GetMatch();
    TestEqual(TEXT("The session is the listed one"), Match.SessionId, Found[0].SessionId);
    TestEqual(TEXT("The lobby's host sits in seat 0 ..."), Match.Players[0].AccountId, FString(TEXT("host")));
    TestEqual(TEXT("... and hosts the match"), Match.HostSeat, 0);
    TestTrue(TEXT("The iPhone's copy is the host's"), Host.Service->GetMatch().IsLocalHost());
    TestEqual(TEXT("The session is off the list"), Registry->GetListedCount(), 0);

    // Refusals, with the reason.
    Latecomer.Service->JoinSession(Found[0].SessionId, MakeRequest(MakePlayer(TEXT("late"), EPSSessionPlatform::Windows, EPSSessionInput::Gamepad, 1000), EPSCrossPlayPolicy::Anyone));
    TestEqual(TEXT("A made match is full"), Latecomer.Service->GetLastFailure(), EPSSessionFailure::SessionFull);
    Latecomer.Service->JoinSession(TEXT("LOCAL-9999"), GuestRequest);
    TestEqual(TEXT("An unknown session isn't found"), Latecomer.Service->GetLastFailure(), EPSSessionFailure::SessionNotFound);

    FPSSessionProbe Other;
    Other.Connect(Registry);
    Other.Service->HostSession(MakeRequest(MakePlayer(TEXT("other"), EPSSessionPlatform::Windows, EPSSessionInput::Gamepad, 1000), EPSCrossPlayPolicy::Anyone));
    FString OtherId;
    for (const FPSSessionListing& Listing : Latecomer.Service->FindSessions(GuestRequest))
    {
        OtherId = Listing.SessionId;
    }
    Latecomer.Service->JoinSession(OtherId, Franchise);
    TestEqual(TEXT("Joining for another mode is refused"), Latecomer.Service->GetLastFailure(), EPSSessionFailure::ModeMismatch);
    TestEqual(TEXT("...and the session stays listed"), Registry->GetListedCount(), 1);
    return true;
}

#endif
