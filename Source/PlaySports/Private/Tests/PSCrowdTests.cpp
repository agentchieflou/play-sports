// PSCrowdTests.cpp -- Epic 23.2 (the dynamic crowd)
//
// UPSCrowdExcitementSubsystem, the one authority on the crowd's excitement, follows plays published
// on the telemetry bus in a headless world, and the audio (UPSAudioSubsystem) plays the crowd's
// bed and stingers from the Crowd events it publishes.
//
// Tests covered:
//   1. The model: Data/crowd.json loads and validates, bad tunings are refused; the resting level
//      (higher late in a close game), the settling (exact for any step), the levels with their
//      hysteresis, and how a moment's fans and rivals weigh in.
//   2. Home and away: a home touchdown erupts and the bed follows; the play's result doesn't
//      repeat it; the crowd settles back; a visitors' touchdown stuns the stadium quiet; a sack
//      by the home defense roars once; a flag on the home team draws boos; a neutral stadium
//      cheers either side; the update runs at the platform tier's rate.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "PSAudioSubsystem.h"
#include "PSCrowdExcitementSubsystem.h"
#include "PSPlatformTiers.h"
#include "PSTelemetryBus.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSCrowdTests
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
        if (World)
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    }

    const FPSCrowdReactionDef* FindDef(const FPSCrowdTuning& Tuning, EPSCrowdStimulus Stimulus)
    {
        return Tuning.CrowdReactions.FindByPredicate([Stimulus](const FPSCrowdReactionDef& Def) { return Def.Stimulus == Stimulus; });
    }

    /** The bus, the audio and the crowd of a fresh world, bound to the bus, every Crowd event
     *  recorded. */
    struct FCrowdFixture
    {
        UWorld* World = nullptr;
        UPSTelemetryBus* Bus = nullptr;
        UPSAudioSubsystem* Audio = nullptr;
        UPSCrowdExcitementSubsystem* Crowd = nullptr;
        TArray<FPSTelemetryCrowdEvent> Events;
        FDelegateHandle Listening;

        bool Setup()
        {
            World = CreateTestWorld();
            Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
            Audio = World ? World->GetSubsystem<UPSAudioSubsystem>() : nullptr;
            Crowd = World ? World->GetSubsystem<UPSCrowdExcitementSubsystem>() : nullptr;
            if (!Bus || !Audio || !Crowd)
            {
                return false;
            }
            Listening = Bus->OnCrowdMC.AddLambda([this](const FPSTelemetryCrowdEvent& Event) { Events.Add(Event); });
            Audio->BindToBus(Bus);
            FPSPlatformTier EveryFrame;
            EveryFrame.AudioUpdateHz = 0.f;
            EveryFrame.AudioMaxVoices = 32;
            EveryFrame.CrowdUpdateHz = 0.f;
            Audio->ApplyPlatformTier(EveryFrame);
            Crowd->BindToBus(Bus);
            Crowd->ApplyPlatformTier(EveryFrame);
            return true;
        }

        ~FCrowdFixture()
        {
            if (Bus)
            {
                Bus->OnCrowdMC.Remove(Listening);
            }
            DestroyTestWorld(World);
        }

        int32 CountReactions(EPSCrowdReaction Reaction) const
        {
            int32 Count = 0;
            for (const FPSTelemetryCrowdEvent& Event : Events)
            {
                Count += Event.Kind == EPSCrowdEventKind::Reaction && Event.Reaction == Reaction ? 1 : 0;
            }
            return Count;
        }

        /** The game as the simulation announces it: who has the ball, the quarter and the score. */
        void PublishState(bool bHomeBall, int32 Quarter, int32 HomeScore, int32 AwayScore, int32 Drives)
        {
            FPSTelemetryGameStateEvent State;
            State.Phase = TEXT("PreSnap");
            State.bHomeHasPossession = bHomeBall;
            State.Quarter = Quarter;
            State.HomeScore = HomeScore;
            State.AwayScore = AwayScore;
            State.CompletedDrives = Drives;
            Bus->PublishGameState(State);
        }

        void PublishSnap()
        {
            FPSTelemetrySnapEvent Snap;
            Bus->PublishSnap(Snap);
        }
    };
}

// ---------------------------------------------------------------------------
// Test 1 -- The model
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCrowdModelTest,
    "PlaySports.Crowd.ExcitementModel",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCrowdModelTest::RunTest(const FString& Parameters)
{
    UWorld* World = PSCrowdTests::CreateTestWorld();
    UPSCrowdExcitementSubsystem* Crowd = World ? World->GetSubsystem<UPSCrowdExcitementSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("The crowd"), Crowd))
    {
        PSCrowdTests::DestroyTestWorld(World);
        return false;
    }

    const FPSCrowdTuning& Tuning = Crowd->GetTuning();
    TestEqual(TEXT("Data/crowd.json validates"), UPSCrowdExcitementSubsystem::ValidateTuning(Tuning).Num(), 0);
    TestEqual(TEXT("...with every level"), Tuning.Levels.Num(), StaticEnum<EPSCrowdLevel>()->NumEnums() - 1);
    TestEqual(TEXT("...and every stimulus"), Tuning.CrowdReactions.Num(), StaticEnum<EPSCrowdStimulus>()->NumEnums() - 1);
    {
        FPSCrowdTuning Bad = Tuning;
        Bad.Levels.RemoveAt(2);
        TestFalse(TEXT("A missing level is refused"), Crowd->SetTuning(Bad));
        Bad = Tuning;
        const FPSCrowdReactionDef Twice = Bad.CrowdReactions[0];
        Bad.CrowdReactions.Add(Twice);
        TestFalse(TEXT("A stimulus listed twice is refused"), Crowd->SetTuning(Bad));
        Bad = Tuning;
        Bad.Levels[3].MinExcitement = Bad.Levels[2].MinExcitement;
        TestFalse(TEXT("Thresholds that don't rise are refused"), Crowd->SetTuning(Bad));
        Bad = Tuning;
        Bad.DefaultHomeShare = 1.5f;
        TestFalse(TEXT("A home share over 1 is refused"), Crowd->SetTuning(Bad));
    }

    // The resting level: higher late in a close game, not in a blowout or early.
    const float Rest = Tuning.RestingExcitement;
    TestTrue(TEXT("Early, the crowd rests at RestingExcitement"),
        FMath::IsNearlyEqual(UPSCrowdExcitementSubsystem::ComputeRestingExcitement(Tuning, 1, 0, 0), Rest));
    TestTrue(TEXT("Late and close, higher"),
        FMath::IsNearlyEqual(UPSCrowdExcitementSubsystem::ComputeRestingExcitement(Tuning, Tuning.LateGameQuarter, 17, 17 - Tuning.CloseGameMargin), Rest + Tuning.LateCloseBonus));
    TestTrue(TEXT("Late in a blowout, not"),
        FMath::IsNearlyEqual(UPSCrowdExcitementSubsystem::ComputeRestingExcitement(Tuning, Tuning.LateGameQuarter, 35, 3), Rest));

    // Settling: half way back to rest in a half-life, and the same whatever the steps.
    const float HalfLife = Tuning.HalfLifeSeconds;
    TestTrue(TEXT("A half-life settles half the way"),
        FMath::IsNearlyEqual(UPSCrowdExcitementSubsystem::SettleToward(0.9f, 0.3f, HalfLife, HalfLife), 0.6f, 0.0001f));
    const float InSteps = UPSCrowdExcitementSubsystem::SettleToward(UPSCrowdExcitementSubsystem::SettleToward(0.9f, 0.3f, HalfLife, 0.7f), 0.3f, HalfLife, 1.3f);
    TestTrue(TEXT("...exactly the same in two steps as in one"),
        FMath::IsNearlyEqual(InSteps, UPSCrowdExcitementSubsystem::SettleToward(0.9f, 0.3f, HalfLife, 2.f), 0.0001f));

    // Levels: up as soon as a threshold is reached, down only under it by the hysteresis.
    float Buzz = 0.f;
    for (const FPSCrowdLevelThreshold& Entry : Tuning.Levels)
    {
        Buzz = Entry.Level == EPSCrowdLevel::Buzz ? Entry.MinExcitement : Buzz;
    }
    TestEqual(TEXT("Reaching Buzz's threshold is Buzz"), UPSCrowdExcitementSubsystem::RateLevel(Tuning, Buzz, EPSCrowdLevel::Murmur), EPSCrowdLevel::Buzz);
    TestEqual(TEXT("Just under it, a Buzz stays Buzz"),
        UPSCrowdExcitementSubsystem::RateLevel(Tuning, Buzz - Tuning.LevelHysteresis * 0.5f, EPSCrowdLevel::Buzz), EPSCrowdLevel::Buzz);
    TestEqual(TEXT("Clearly under it, a Buzz drops to Murmur"),
        UPSCrowdExcitementSubsystem::RateLevel(Tuning, Buzz - Tuning.LevelHysteresis * 1.5f, EPSCrowdLevel::Buzz), EPSCrowdLevel::Murmur);
    TestEqual(TEXT("Silence is a Hush"), UPSCrowdExcitementSubsystem::RateLevel(Tuning, 0.f, EPSCrowdLevel::Murmur), EPSCrowdLevel::Hush);

    // A moment: its fans and its rivals, each by their share.
    const FPSCrowdReactionDef* Touchdown = PSCrowdTests::FindDef(Tuning, EPSCrowdStimulus::Touchdown);
    if (TestNotNull(TEXT("The touchdown's reaction"), Touchdown))
    {
        const float Home = Tuning.DefaultHomeShare;
        TestTrue(TEXT("A home touchdown: the home fans' cheer outweighs the visitors'"),
            FMath::IsNearlyEqual(UPSCrowdExcitementSubsystem::ExcitementDelta(*Touchdown, Home), Home * Touchdown->FansDelta + (1.f - Home) * Touchdown->RivalsDelta));
        TestTrue(TEXT("A visitors' touchdown quiets the stadium"), UPSCrowdExcitementSubsystem::ExcitementDelta(*Touchdown, 1.f - Home) < 0.f);
        TestEqual(TEXT("The home crowd erupts for its own"), UPSCrowdExcitementSubsystem::ChooseReaction(*Touchdown, Home), EPSCrowdReaction::Eruption);
        TestEqual(TEXT("...and is stunned by the visitors'"), UPSCrowdExcitementSubsystem::ChooseReaction(*Touchdown, 1.f - Home), EPSCrowdReaction::Stunned);
    }

    PSCrowdTests::DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Home and away
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCrowdHomeAwayTest,
    "PlaySports.Crowd.HomeAndAway",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCrowdHomeAwayTest::RunTest(const FString& Parameters)
{
    PSCrowdTests::FCrowdFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus, the audio and the crowd"), Fixture.Setup()))
    {
        return false;
    }
    UPSCrowdExcitementSubsystem* Crowd = Fixture.Crowd;
    UPSAudioSubsystem* Audio = Fixture.Audio;
    UPSTelemetryBus* Bus = Fixture.Bus;
    const FPSCrowdTuning& Tuning = Crowd->GetTuning();

    // At the start the crowd rests; the game's first state announces it, and the audio starts its bed.
    TestTrue(TEXT("The crowd starts at rest"), FMath::IsNearlyEqual(Crowd->GetExcitement(), Tuning.RestingExcitement));
    TestEqual(TEXT("...a murmur"), Crowd->GetLevel(), EPSCrowdLevel::Murmur);
    TestEqual(TEXT("...not announced before the game"), Fixture.Events.Num(), 0);
    Fixture.PublishState(true, 1, 0, 0, 0);
    TestEqual(TEXT("The game's first state announces the crowd's level"), Fixture.Events.Num(), 1);
    TestEqual(TEXT("The audio plays the murmur's bed"), Audio->GetActiveLoop(TEXT("CrowdBed")), FName(TEXT("Crowd.Bed.Murmur")));
    TestTrue(TEXT("The crowd is mostly home fans"), FMath::IsNearlyEqual(Crowd->GetHomeShare(), Tuning.DefaultHomeShare));

    // A home touchdown, the moment the carrier crosses: the stadium erupts.
    Fixture.PublishSnap();
    FPSTelemetryBoundaryCrossedEvent Crossing;
    Crossing.CarrierName = TEXT("HOM_RB");
    Crossing.YardLine = 100;
    Crossing.bEndZone = true;
    Bus->PublishBoundaryCrossed(Crossing);
    TestEqual(TEXT("A home touchdown erupts"), Crowd->GetLastReaction(), EPSCrowdReaction::Eruption);
    TestEqual(TEXT("...to the top level"), Crowd->GetLevel(), EPSCrowdLevel::Eruption);
    TestEqual(TEXT("...announced on the bus"), Fixture.CountReactions(EPSCrowdReaction::Eruption), 1);
    TestEqual(TEXT("The audio plays the eruption"), Audio->CountRequests(TEXT("Crowd.Eruption")), 1);
    TestEqual(TEXT("...and the eruption's bed"), Audio->GetActiveLoop(TEXT("CrowdBed")), FName(TEXT("Crowd.Bed.Eruption")));

    // The simulation's result confirms it; the crowd doesn't erupt twice.
    FPSTelemetryPlayResultEvent Scored;
    Scored.bHomeOffense = true;
    Scored.Result = TEXT("Touchdown");
    Scored.HomePoints = 7;
    Scored.YardsGained = 30;
    Bus->PublishPlayResult(Scored);
    TestEqual(TEXT("The result doesn't repeat the eruption"), Fixture.CountReactions(EPSCrowdReaction::Eruption), 1);

    // The crowd settles back to rest.
    Fixture.PublishState(false, 1, 7, 0, 1);
    Crowd->AdvanceTime(Tuning.HalfLifeSeconds * 12.f);
    TestTrue(TEXT("The crowd settles back to rest"), FMath::IsNearlyEqual(Crowd->GetExcitement(), Tuning.RestingExcitement, 0.01f));
    TestEqual(TEXT("...a murmur again"), Crowd->GetLevel(), EPSCrowdLevel::Murmur);
    TestEqual(TEXT("...and its bed"), Audio->GetActiveLoop(TEXT("CrowdBed")), FName(TEXT("Crowd.Bed.Murmur")));

    // A visitors' touchdown (the result is the first the crowd hears of it): stunned quiet.
    Fixture.PublishSnap();
    FPSTelemetryPlayResultEvent Visitors;
    Visitors.bHomeOffense = false;
    Visitors.Result = TEXT("Touchdown");
    Visitors.AwayPoints = 7;
    Bus->PublishPlayResult(Visitors);
    TestEqual(TEXT("A visitors' touchdown stuns the home crowd"), Crowd->GetLastReaction(), EPSCrowdReaction::Stunned);
    TestTrue(TEXT("...quieter than at rest"), Crowd->GetExcitement() < Tuning.RestingExcitement);
    TestEqual(TEXT("...to a hush"), Crowd->GetLevel(), EPSCrowdLevel::Hush);
    TestEqual(TEXT("The audio plays the silence"), Audio->CountRequests(TEXT("Crowd.Stunned")), 1);

    // The home defense sacks the visitors: one roar, not two when the result says sack.
    Crowd->AdvanceTime(Tuning.HalfLifeSeconds * 12.f);
    Fixture.PublishState(false, 2, 7, 7, 2);
    Fixture.PublishSnap();
    const int32 RoarsBefore = Fixture.CountReactions(EPSCrowdReaction::Roar);
    const float BeforeSack = Crowd->GetExcitement();
    FPSTelemetryTackleEvent Sack;
    Sack.TacklerName = TEXT("HOM_DE");
    Sack.BallCarrierName = TEXT("AWY_QB");
    Sack.YardsGained = -8;
    Sack.bIsSack = true;
    Bus->PublishTackle(Sack);
    TestEqual(TEXT("A home sack roars"), Crowd->GetLastReaction(), EPSCrowdReaction::Roar);
    TestTrue(TEXT("...louder"), Crowd->GetExcitement() > BeforeSack);
    FPSTelemetryPlayResultEvent SackResult;
    SackResult.bHomeOffense = false;
    SackResult.Result = TEXT("Tackle");
    SackResult.bPass = true;
    SackResult.bSack = true;
    SackResult.YardsGained = -8;
    Bus->PublishPlayResult(SackResult);
    TestEqual(TEXT("...once"), Fixture.CountReactions(EPSCrowdReaction::Roar), RoarsBefore + 1);

    // A flag on the home team draws boos from the home crowd.
    FPSTelemetryPenaltyEvent Flag;
    Flag.Kind = EPSPenaltyEventKind::Flag;
    Flag.Penalty = TEXT("PassInterference");
    Flag.bOnDefense = true;
    Flag.bHomeTeam = true;
    Bus->PublishPenalty(Flag);
    TestEqual(TEXT("A flag on the home team is booed"), Crowd->GetLastReaction(), EPSCrowdReaction::Boo);
    TestEqual(TEXT("...out loud"), Audio->CountRequests(TEXT("Crowd.Boo")), 1);

    // A neutral stadium cheers either side's score.
    Crowd->SetMatchContext(TEXT("HOM"), TEXT("AWY"), 0.5f);
    TestEqual(TEXT("The match's teams"), Crowd->GetAwayTeamId(), FName(TEXT("AWY")));
    TestTrue(TEXT("...and its split"), FMath::IsNearlyEqual(Crowd->GetHomeShare(), 0.5f));
    TestEqual(TEXT("At a neutral site the visitors' fans erupt too"), Crowd->ApplyStimulus(EPSCrowdStimulus::Touchdown, false), EPSCrowdReaction::Eruption);
    Crowd->SetMatchContext(TEXT("HOM"), TEXT("AWY"), -1.f);
    TestTrue(TEXT("A negative share falls back to the tuning's"), FMath::IsNearlyEqual(Crowd->GetHomeShare(), Tuning.DefaultHomeShare));

    // Late in a close game the crowd rests louder.
    Fixture.PublishState(true, Tuning.LateGameQuarter, 14, 10, 6);
    Crowd->AdvanceTime(Tuning.HalfLifeSeconds * 12.f);
    TestTrue(TEXT("Late and close, the crowd rests louder"),
        FMath::IsNearlyEqual(Crowd->GetExcitement(), Tuning.RestingExcitement + Tuning.LateCloseBonus, 0.01f));

    // The update runs at the platform tier's rate; the settling is the same either way.
    FPSPlatformTier Phone;
    Phone.CrowdUpdateHz = 10.f;
    Crowd->ApplyPlatformTier(Phone);
    Crowd->ApplyStimulus(EPSCrowdStimulus::BigGain, true);
    const float Excited = Crowd->GetExcitement();
    Crowd->AdvanceTime(0.05f);
    TestTrue(TEXT("Between updates the crowd holds"), FMath::IsNearlyEqual(Crowd->GetExcitement(), Excited));
    Crowd->AdvanceTime(0.06f);
    TestTrue(TEXT("...and settles by all the time passed at the next"),
        FMath::IsNearlyEqual(Crowd->GetExcitement(), UPSCrowdExcitementSubsystem::SettleToward(Excited, Crowd->GetRestingExcitement(), Tuning.HalfLifeSeconds, 0.11f), 0.0001f));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
