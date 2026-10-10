// PSHUDBannerTests.cpp -- the play result banner reads the play's own result
//
// Tests covered:
//   1. UPSPlayResultWidget's banner comes from the bus's PlayResult (Epic 92), not from the phase
//      a play ended in: each result's banner (yards, a sack's loss, an incompletion, a score, an
//      interception and a return of one for a touchdown, a missed field goal, a turnover on
//      downs; none for a kick), and, through a play simulation on a bus, a run stopped at once
//      after the snap (the whistle in the pass-rush phase) shows its yards, not "incomplete".

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSHUDWidget.h"
#include "PSLocalization.h"
#include "PSPlaySimulation.h"
#include "PSPlayerAttributes.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSHUDBannerTests
{
    static UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    static void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    static FPSTelemetryPlayResultEvent MakeResult(const TCHAR* Result, int32 Yards = 0, bool bPass = false)
    {
        FPSTelemetryPlayResultEvent Play;
        Play.Result = Result;
        Play.YardsGained = Yards;
        Play.bPass = bPass;
        return Play;
    }

    /** The banner text for Play, or "(none)". */
    static FString BannerFor(const FPSTelemetryPlayResultEvent& Play)
    {
        FText Banner;
        return UPSPlayResultWidget::MakePlayResultBanner(Play, Banner) ? Banner.ToString() : FString(TEXT("(none)"));
    }

    static FPlayerAttributes MakePlayer(const TCHAR* PlayerId, const TCHAR* DisplayName, EPlayerRole Role)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(PlayerId);
        Player.DisplayName = DisplayName;
        Player.Role = Role;
        return Player;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayResultBannerTest,
    "PlaySports.UI.PlayResultBannerReadsThePlay",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayResultBannerTest::RunTest(const FString& Parameters)
{
    using namespace PSHUDBannerTests;

    // Each result's banner.
    const FString Touchdown = UPSPlayResultWidget::MakeScoreBanner(TEXT("Touchdown")).ToString();
    TestEqual(TEXT("A run tackled for 3"), BannerFor(MakeResult(TEXT("Tackle"), 3)), UPSPlayResultWidget::MakeYardsBanner(3).ToString());
    TestEqual(TEXT("A run stopped for no gain"), BannerFor(MakeResult(TEXT("Tackle"), 0)), UPSPlayResultWidget::MakeYardsBanner(0).ToString());
    FPSTelemetryPlayResultEvent Sack = MakeResult(TEXT("Tackle"), -7, true);
    Sack.bSack = true;
    TestEqual(TEXT("A sack for 7"), BannerFor(Sack), UPSPlayResultWidget::MakeYardsBanner(-7).ToString());
    TestEqual(TEXT("A completion tackled after 12"), BannerFor(MakeResult(TEXT("Tackle"), 12, true)), UPSPlayResultWidget::MakeYardsBanner(12).ToString());
    TestEqual(TEXT("An incompletion"), BannerFor(MakeResult(TEXT("Incomplete"), 0, true)), UPSPlayResultWidget::MakeIncompletePassBanner().ToString());
    TestEqual(TEXT("A touchdown"), BannerFor(MakeResult(TEXT("Touchdown"), 100)), Touchdown);
    TestEqual(TEXT("A safety"), BannerFor(MakeResult(TEXT("Safety"), -20)), UPSPlayResultWidget::MakeScoreBanner(TEXT("Safety")).ToString());
    TestEqual(TEXT("A field goal"), BannerFor(MakeResult(TEXT("FieldGoalGood"))), UPSPlayResultWidget::MakeScoreBanner(TEXT("FieldGoal")).ToString());
    TestEqual(TEXT("A miss"), BannerFor(MakeResult(TEXT("FieldGoalMissed"))), UPSLocalization::GetText(TEXT("HUD.FieldGoalMissed")).ToString());
    FPSTelemetryPlayResultEvent Pick = MakeResult(TEXT("Interception"), 0, true);
    Pick.bInterception = true;
    TestEqual(TEXT("An interception"), BannerFor(Pick), UPSLocalization::GetText(TEXT("HUD.Interception")).ToString());
    Pick.AwayPoints = 7;
    TestEqual(TEXT("An interception returned for a touchdown"), BannerFor(Pick), Touchdown);
    FPSTelemetryPlayResultEvent Stopped = MakeResult(TEXT("Tackle"), 1);
    Stopped.bTurnoverOnDowns = true;
    TestEqual(TEXT("Stopped on 4th down"), BannerFor(Stopped), UPSLocalization::GetText(TEXT("HUD.TurnoverOnDowns")).ToString());
    TestEqual(TEXT("No banner for a kickoff"), BannerFor(MakeResult(TEXT("KickoffResult"), 30)), FString(TEXT("(none)")));
    TestEqual(TEXT("...or a punt"), BannerFor(MakeResult(TEXT("PuntResult"), 40)), FString(TEXT("(none)")));
    for (const TCHAR* Key : { TEXT("HUD.Interception"), TEXT("HUD.FieldGoalMissed"), TEXT("HUD.TurnoverOnDowns") })
    {
        TestTrue(*FString::Printf(TEXT("%s is in the string table"), Key), UPSLocalization::HasText(Key));
    }

    // Through the simulation: a run stopped at once after the snap, its whistle in the pass-rush
    // phase, shows its yards (the old banner guessed "incomplete" from that phase change).
    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    TArray<FString> Banners;
    Bus->OnPlayResultMC.AddLambda([&Banners](const FPSTelemetryPlayResultEvent& Event) { Banners.Add(BannerFor(Event)); });
    const TArray<FPlayerAttributes> Offense = { MakePlayer(TEXT("HOME_RB"), TEXT("Home Runner"), EPlayerRole::RunningBack) };
    const TArray<FPlayerAttributes> Defense = { MakePlayer(TEXT("AWAY_LB"), TEXT("Away Backer"), EPlayerRole::Linebacker) };
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->InitializePlay(Offense, Defense);
    Sim->InitializeWithWorld(World);
    Sim->TriggerSnap();
    Sim->ActivePenalty = EPSPenaltyType::None;
    Sim->SetPlayPhase(EPlayPhase::PassRush);

    FPSTelemetryTackleEvent Tackle;
    Tackle.TacklerName = Defense[0].DisplayName;
    Tackle.BallCarrierName = Offense[0].DisplayName;
    Tackle.YardLine = 23;
    Tackle.YardsGained = 3;
    Bus->PublishTackle(Tackle);
    FPSTelemetryPhaseChangeEvent Whistle;
    Whistle.OldPhase = TEXT("PassRush");
    Whistle.NewPhase = TEXT("Scoring");
    Bus->PublishPhaseChange(Whistle);
    Sim->EndPlayAndPrepareNext();
    if (TestEqual(TEXT("One banner"), Banners.Num(), 1))
    {
        TestEqual(TEXT("...the run's 3 yards"), Banners[0], UPSPlayResultWidget::MakeYardsBanner(3).ToString());
    }

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
