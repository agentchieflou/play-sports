// PSCommentaryHookTests.cpp -- Epic 23.5 (commentary hooks)
//
// Plays are published on the telemetry bus in a headless world, the way the live game announces
// them. UPSCommentaryEventModel reads them and publishes each moment back as a structured
// Commentary event; with Epic 82's bridge online it offers moments to outside models.
//
// Tests covered:
//   1. Play descriptions: Data/commentary_hooks.json loads and validates, bad tunings are refused;
//      a game's moments -- its start, the snap, a deep pass, a catch, the play's result with its
//      players, an interception, a goal-line crossing and the touchdown it scores, a flag, a big
//      hit, the two-minute warning, halftime and the final whistle -- each with what happened, who
//      and the situation, tied to the bus event it describes; the facts as a model reads them,
//      within their budget; the moments kept.
//   2. The bridge gate: offline, nothing is asked; online, the offered moments become Commentary
//      requests routed as narration with the moment's facts, others don't, and an answer comes
//      back as the moment's model line.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"
#include "PSCommentaryEventModel.h"
#include "PSGameIntelligenceSubsystem.h"
#include "PSTelemetryBus.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSCommentaryHookTests
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

    /** What AgenticLink registers while its MCP server serves (Epic 82's gate). */
    struct FPSFakeBoothBridge : public IModularFeature
    {
    };

    struct FScopedBoothBridge
    {
        FPSFakeBoothBridge Feature;

        FScopedBoothBridge()
        {
            IModularFeatures::Get().RegisterModularFeature(UPSGameIntelligenceSubsystem::GetBridgeFeatureName(), &Feature);
        }

        ~FScopedBoothBridge()
        {
            IModularFeatures::Get().UnregisterModularFeature(UPSGameIntelligenceSubsystem::GetBridgeFeatureName(), &Feature);
        }
    };

    TSharedPtr<FJsonObject> ParseObject(const FString& Json)
    {
        TSharedPtr<FJsonObject> Object;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
    }

    /** The commentary hooks of a fresh world, bound to its bus, recording every moment published. */
    struct FBoothFixture
    {
        UWorld* World = nullptr;
        UPSTelemetryBus* Bus = nullptr;
        UPSCommentaryEventModel* Model = nullptr;
        UPSGameIntelligenceSubsystem* Intelligence = nullptr;
        TArray<FPSTelemetryCommentaryEvent> Published;
        FDelegateHandle Listening;

        bool Setup()
        {
            World = CreateTestWorld();
            Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
            Model = World ? World->GetSubsystem<UPSCommentaryEventModel>() : nullptr;
            Intelligence = World ? World->GetSubsystem<UPSGameIntelligenceSubsystem>() : nullptr;
            if (!Bus || !Model || !Intelligence)
            {
                return false;
            }
            Listening = Bus->OnCommentaryMC.AddLambda([this](const FPSTelemetryCommentaryEvent& Event) { Published.Add(Event); });
            Model->BindToBus(Bus);
            Model->SetIntelligence(Intelligence);
            return true;
        }

        ~FBoothFixture()
        {
            if (Bus)
            {
                Bus->OnCommentaryMC.Remove(Listening);
            }
            DestroyTestWorld(World);
        }

        /** The latest moment of Kind published, or null. */
        const FPSTelemetryCommentaryEvent* Latest(EPSCommentaryMoment Kind) const
        {
            for (int32 Index = Published.Num() - 1; Index >= 0; --Index)
            {
                if (Published[Index].Moment == Kind)
                {
                    return &Published[Index];
                }
            }
            return nullptr;
        }

        int32 Count(EPSCommentaryMoment Kind) const
        {
            int32 Found = 0;
            for (const FPSTelemetryCommentaryEvent& Event : Published)
            {
                Found += Event.Moment == Kind ? 1 : 0;
            }
            return Found;
        }

        void PublishState(int32 Quarter, float Clock, bool bHomeBall, int32 HomeScore, int32 AwayScore)
        {
            FPSTelemetryGameStateEvent State;
            State.Phase = TEXT("PreSnap");
            State.Quarter = Quarter;
            State.GameClockSeconds = Clock;
            State.bHomeHasPossession = bHomeBall;
            State.HomeScore = HomeScore;
            State.AwayScore = AwayScore;
            State.Down = 3;
            State.Distance = 7;
            State.YardLine = 35;
            Bus->PublishGameState(State);
        }

        void PublishSnap()
        {
            FPSTelemetrySnapEvent Snap;
            Snap.Down = 3;
            Snap.Distance = 7;
            Snap.YardLine = 35;
            Snap.GameClockSeconds = 600.f;
            Bus->PublishSnap(Snap);
        }
    };
}

// ---------------------------------------------------------------------------
// Test 1 -- Play descriptions
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCommentaryPlayDescriptionTest,
    "PlaySports.Commentary.PlayDescriptions",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCommentaryPlayDescriptionTest::RunTest(const FString& Parameters)
{
    PSCommentaryHookTests::FBoothFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus and the commentary hooks"), Fixture.Setup()))
    {
        return false;
    }
    UPSCommentaryEventModel* Model = Fixture.Model;
    UPSTelemetryBus* Bus = Fixture.Bus;

    const FPSCommentaryHookTuning& Tuning = Model->GetTuning();
    TestEqual(TEXT("Data/commentary_hooks.json validates"), UPSCommentaryEventModel::ValidateTuning(Tuning).Num(), 0);
    TestTrue(TEXT("...and offers plays to models"), Tuning.ModelMoments.Contains(EPSCommentaryMoment::PlayResult));
    {
        FPSCommentaryHookTuning Bad = Tuning;
        Bad.ModelContextChars = 100;
        TestFalse(TEXT("A context under 512 characters is refused"), Model->SetTuning(Bad));
        Bad = Tuning;
        Bad.ModelMoments.Add(EPSCommentaryMoment::PlayResult);
        TestFalse(TEXT("A moment offered twice is refused"), Model->SetTuning(Bad));
        Bad = Tuning;
        Bad.ModelTask.Reset();
        TestFalse(TEXT("No task is refused"), Model->SetTuning(Bad));
    }

    // The game starts.
    Fixture.PublishState(1, 900.f, true, 0, 0);
    TestEqual(TEXT("The first game state starts the game"), Fixture.Count(EPSCommentaryMoment::GameStart), 1);

    // Play 1: third and 7, a deep completion for a first down.
    Fixture.PublishSnap();
    const FPSTelemetryCommentaryEvent* Snapped = Fixture.Latest(EPSCommentaryMoment::Snap);
    if (TestNotNull(TEXT("The snap is described"), Snapped))
    {
        TestEqual(TEXT("...as the game's first play"), Snapped->PlayNumber, 1);
        TestEqual(TEXT("...on third down"), Snapped->Down, 3);
        TestEqual(TEXT("...and 7"), Snapped->Distance, 7);
        TestTrue(TEXT("...the home team with the ball"), Snapped->bHomeOffense);
    }
    FPSTelemetryThrowEvent Throw;
    Throw.PasserName = TEXT("Hawks QB");
    Throw.TargetReceiverName = TEXT("Hawks WR");
    Throw.StartLocation = FVector(3000.f, 0.f, 0.f);
    Throw.TargetLocation = FVector(3000.f + Tuning.DeepPassCm + 200.f, 0.f, 0.f);
    Bus->PublishThrow(Throw);
    const FPSTelemetryCommentaryEvent* Pass = Fixture.Latest(EPSCommentaryMoment::Pass);
    if (TestNotNull(TEXT("The throw is described"), Pass))
    {
        TestEqual(TEXT("...as deep"), Pass->Detail, FName(TEXT("Deep")));
        TestEqual(TEXT("...by the passer"), Pass->PrimaryName, FString(TEXT("Hawks QB")));
        TestEqual(TEXT("...to his target"), Pass->SecondaryName, FString(TEXT("Hawks WR")));
        TestTrue(TEXT("...its air yards"), FMath::IsNearlyEqual(Pass->Magnitude, (Tuning.DeepPassCm + 200.f) / 91.44f, 0.01f));
    }
    FPSTelemetryCatchEvent Catch;
    Catch.ReceiverName = TEXT("Hawks WR");
    Catch.YardsGained = 31;
    Bus->PublishCatch(Catch);
    const FPSTelemetryCommentaryEvent* Caught = Fixture.Latest(EPSCommentaryMoment::Catch);
    if (TestNotNull(TEXT("The catch is described"), Caught))
    {
        TestEqual(TEXT("...by the receiver"), Caught->PrimaryName, FString(TEXT("Hawks WR")));
        TestEqual(TEXT("...from the passer"), Caught->SecondaryName, FString(TEXT("Hawks QB")));
        TestEqual(TEXT("...for its yards"), Caught->Yards, 31);
        FPSTelemetryEvent Source;
        TestTrue(TEXT("...tied to the catch on the bus"), Bus->FindLatestEventOfType(EPSTelemetryEventType::Catch, Source) && Caught->SourceSequence == Source.Sequence);
    }
    FPSTelemetryTackleEvent Tackle;
    Tackle.TacklerName = TEXT("Wolves CB");
    Tackle.BallCarrierName = TEXT("Hawks WR");
    const int32 BeforeTackle = Fixture.Published.Num();
    Bus->PublishTackle(Tackle);
    TestEqual(TEXT("A plain tackle waits for the play's result"), Fixture.Published.Num(), BeforeTackle);
    FPSTelemetryPlayResultEvent Completion;
    Completion.PlayNumber = 1;
    Completion.bHomeOffense = true;
    Completion.Quarter = 1;
    Completion.Down = 3;
    Completion.Distance = 7;
    Completion.YardLine = 35;
    Completion.Result = TEXT("Tackle");
    Completion.YardsGained = 31;
    Completion.bPass = true;
    Completion.bComplete = true;
    Completion.bFirstDown = true;
    Completion.PasserId = TEXT("HAW_QB_001");
    Completion.ReceiverId = TEXT("HAW_WR_001");
    Completion.TacklerId = TEXT("WOL_CB_001");
    Bus->PublishPlayResult(Completion);
    const FPSTelemetryCommentaryEvent* Result = Fixture.Latest(EPSCommentaryMoment::PlayResult);
    if (TestNotNull(TEXT("The play's result is described"), Result))
    {
        TestEqual(TEXT("...the catch by the receiver"), Result->PrimaryName, FString(TEXT("Hawks WR")));
        TestEqual(TEXT("...with his id"), Result->PrimaryId, FName(TEXT("HAW_WR_001")));
        TestEqual(TEXT("...from the passer"), Result->SecondaryId, FName(TEXT("HAW_QB_001")));
        TestEqual(TEXT("...for 31"), Result->Yards, 31);
        TestTrue(TEXT("...a first down, the home team's way"), Result->bFirstDown && Result->bHomeFavoured && !Result->bTurnover);
        TestEqual(TEXT("...in the situation it was snapped in"), Result->Down, 3);
    }
    TestEqual(TEXT("No score, no Score moment"), Fixture.Count(EPSCommentaryMoment::Score), 0);

    // Play 2: an interception.
    Fixture.PublishSnap();
    Bus->PublishThrow(Throw);
    FPSTelemetryCatchEvent Pick;
    Pick.ReceiverName = TEXT("Wolves S");
    Pick.bIsInterception = true;
    Bus->PublishCatch(Pick);
    const FPSTelemetryCommentaryEvent* Interception = Fixture.Latest(EPSCommentaryMoment::Interception);
    if (TestNotNull(TEXT("The interception is described"), Interception))
    {
        TestEqual(TEXT("...by the defender"), Interception->PrimaryName, FString(TEXT("Wolves S")));
        TestEqual(TEXT("...off the passer"), Interception->SecondaryName, FString(TEXT("Hawks QB")));
        TestTrue(TEXT("...a turnover, the visitors' way"), Interception->bTurnover && !Interception->bHomeFavoured);
        TestEqual(TEXT("...the game's second play"), Interception->PlayNumber, 2);
    }
    FPSTelemetryPlayResultEvent Picked;
    Picked.bHomeOffense = true;
    Picked.Result = TEXT("Interception");
    Picked.bPass = true;
    Picked.bInterception = true;
    Picked.InterceptorId = TEXT("WOL_S_001");
    Bus->PublishPlayResult(Picked);
    Result = Fixture.Latest(EPSCommentaryMoment::PlayResult);
    if (TestNotNull(TEXT("The interception's result"), Result))
    {
        TestEqual(TEXT("...is the interceptor's"), Result->PrimaryId, FName(TEXT("WOL_S_001")));
        TestTrue(TEXT("...a turnover"), Result->bTurnover && !Result->bHomeFavoured);
    }

    // Play 3: the visitors run it in.
    Fixture.PublishState(1, 540.f, false, 0, 0);
    Fixture.PublishSnap();
    FPSTelemetryBoundaryCrossedEvent Crossing;
    Crossing.CarrierName = TEXT("Wolves RB");
    Crossing.YardLine = 100;
    Crossing.bEndZone = true;
    Bus->PublishBoundaryCrossed(Crossing);
    const FPSTelemetryCommentaryEvent* GoalLine = Fixture.Latest(EPSCommentaryMoment::GoalLine);
    if (TestNotNull(TEXT("The carrier crossing the goal line is described"), GoalLine))
    {
        TestEqual(TEXT("...by name"), GoalLine->PrimaryName, FString(TEXT("Wolves RB")));
        TestFalse(TEXT("...the visitors' way"), GoalLine->bHomeFavoured);
    }
    FPSTelemetryTackleEvent InTheEndZone;
    InTheEndZone.BallCarrierName = TEXT("Wolves RB");
    Bus->PublishTackle(InTheEndZone);
    FPSTelemetryPlayResultEvent Touchdown;
    Touchdown.bHomeOffense = false;
    Touchdown.Result = TEXT("Touchdown");
    Touchdown.YardsGained = 12;
    Touchdown.AwayPoints = 7;
    Touchdown.RusherId = TEXT("WOL_RB_001");
    Bus->PublishPlayResult(Touchdown);
    const FPSTelemetryCommentaryEvent* Score = Fixture.Latest(EPSCommentaryMoment::Score);
    if (TestNotNull(TEXT("The touchdown is a Score moment"), Score))
    {
        TestEqual(TEXT("...a touchdown"), Score->Detail, FName(TEXT("Touchdown")));
        TestEqual(TEXT("...by the runner"), Score->PrimaryId, FName(TEXT("WOL_RB_001")));
        TestEqual(TEXT("...seven points"), Score->Points, 7);
        TestEqual(TEXT("...the score after it"), Score->AwayScore, 7);
        TestFalse(TEXT("...the visitors' way"), Score->bHomeFavoured);
    }

    // A flag, a big hit and a small one.
    FPSTelemetryPenaltyEvent Flag;
    Flag.Kind = EPSPenaltyEventKind::Flag;
    Flag.Penalty = TEXT("Holding");
    Flag.PlayerName = TEXT("Hawks LG");
    Flag.bHomeTeam = true;
    Bus->PublishPenalty(Flag);
    const FPSTelemetryCommentaryEvent* Flagged = Fixture.Latest(EPSCommentaryMoment::Flag);
    if (TestNotNull(TEXT("A flag is described"), Flagged))
    {
        TestEqual(TEXT("...its foul"), Flagged->Detail, FName(TEXT("Holding")));
        TestEqual(TEXT("...its player"), Flagged->PrimaryName, FString(TEXT("Hawks LG")));
        TestFalse(TEXT("...against the home team"), Flagged->bHomeFavoured);
    }
    FPSTelemetryDamageEvent Hit;
    Hit.TargetName = TEXT("Hawks RB");
    Hit.Amount = Tuning.BigHitDamage * 0.5f;
    Bus->PublishDamage(Hit);
    TestEqual(TEXT("A small hit isn't a moment"), Fixture.Count(EPSCommentaryMoment::BigHit), 0);
    Hit.Amount = Tuning.BigHitDamage;
    Bus->PublishDamage(Hit);
    TestEqual(TEXT("A big hit is"), Fixture.Count(EPSCommentaryMoment::BigHit), 1);

    // The clock: the two-minute warning, halftime, the final whistle.
    Fixture.PublishState(2, Tuning.TwoMinuteWarningSeconds + 10.f, true, 0, 7);
    TestEqual(TEXT("The first quarter's end"), Fixture.Count(EPSCommentaryMoment::QuarterEnd), 1);
    Fixture.PublishState(2, Tuning.TwoMinuteWarningSeconds - 5.f, true, 0, 7);
    TestEqual(TEXT("The two-minute warning"), Fixture.Count(EPSCommentaryMoment::TwoMinuteWarning), 1);
    Fixture.PublishState(2, Tuning.TwoMinuteWarningSeconds - 30.f, true, 0, 7);
    TestEqual(TEXT("...only once"), Fixture.Count(EPSCommentaryMoment::TwoMinuteWarning), 1);
    Fixture.PublishState(3, 900.f, false, 0, 7);
    const FPSTelemetryCommentaryEvent* Half = Fixture.Latest(EPSCommentaryMoment::QuarterEnd);
    if (TestNotNull(TEXT("Halftime"), Half))
    {
        TestEqual(TEXT("...the second quarter's end"), Half->Quarter, 2);
        TestEqual(TEXT("...is the half"), Half->Detail, FName(TEXT("Halftime")));
    }
    Fixture.PublishState(5, 0.f, false, 3, 7);
    const FPSTelemetryCommentaryEvent* Final = Fixture.Latest(EPSCommentaryMoment::GameEnd);
    if (TestNotNull(TEXT("The final whistle"), Final))
    {
        TestFalse(TEXT("...the visitors won"), Final->bHomeFavoured);
        TestEqual(TEXT("...the final score"), Final->AwayScore, 7);
    }

    // The facts as a model reads them: compact JSON within the budget.
    if (const FPSTelemetryCommentaryEvent* LatestCatch = Fixture.Latest(EPSCommentaryMoment::Catch))
    {
        const FPSTelemetryCommentaryEvent CatchMoment = *LatestCatch;
        const FString Described = UPSCommentaryEventModel::DescribeForModel(CatchMoment, 512);
        const TSharedPtr<FJsonObject> Object = PSCommentaryHookTests::ParseObject(Described);
        if (TestTrue(TEXT("A moment's facts are JSON"), Object.IsValid()))
        {
            TestEqual(TEXT("...naming the moment"), Object->GetStringField(TEXT("moment")), FString(TEXT("Catch")));
            TestEqual(TEXT("...and the player"), Object->GetStringField(TEXT("player")), FString(TEXT("Hawks WR")));
        }
        TestTrue(TEXT("...within the budget"), Described.Len() <= 512);
        FPSTelemetryCommentaryEvent LongNames = CatchMoment;
        LongNames.PrimaryName = FString::ChrN(400, TEXT('A'));
        LongNames.SecondaryName = FString::ChrN(400, TEXT('B'));
        const FString Trimmed = UPSCommentaryEventModel::DescribeForModel(LongNames, 512);
        TestTrue(TEXT("Long names are shortened to fit"), Trimmed.Len() <= 512 && PSCommentaryHookTests::ParseObject(Trimmed).IsValid());
    }

    // The moments kept.
    TestTrue(TEXT("Every moment is kept up to MaxMomentsKept"), Model->GetMoments().Num() == FMath::Min(Fixture.Published.Num(), Tuning.MaxMomentsKept));
    FPSCommentaryHookTuning Short = Tuning;
    Short.MaxMomentsKept = 2;
    Model->SetTuning(Short);
    Fixture.PublishSnap();
    TestEqual(TEXT("...and no more"), Model->GetMoments().Num(), 2);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The bridge gate
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCommentaryBridgeGateTest,
    "PlaySports.Commentary.BridgeGate",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCommentaryBridgeGateTest::RunTest(const FString& Parameters)
{
    PSCommentaryHookTests::FBoothFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus, the commentary hooks and Epic 82's hooks"), Fixture.Setup()))
    {
        return false;
    }
    UPSCommentaryEventModel* Model = Fixture.Model;
    UPSGameIntelligenceSubsystem* Intelligence = Fixture.Intelligence;
    UPSTelemetryBus* Bus = Fixture.Bus;

    const auto CountCommentaryRequests = [Intelligence]()
    {
        int32 Count = 0;
        for (const FPSIntelRequest& Request : Intelligence->GetRequests())
        {
            Count += Request.Kind == EPSIntelRequestKind::Commentary ? 1 : 0;
        }
        return Count;
    };

    FPSTelemetryPlayResultEvent Play;
    Play.bHomeOffense = true;
    Play.Result = TEXT("Tackle");
    Play.YardsGained = 6;

    // Offline: the moments go on the bus, and no model is asked.
    Fixture.PublishState(1, 900.f, true, 0, 0);
    if (!UPSGameIntelligenceSubsystem::IsBridgeOnline())
    {
        Fixture.PublishSnap();
        Bus->PublishPlayResult(Play);
        TestEqual(TEXT("The play is described offline"), Fixture.Count(EPSCommentaryMoment::PlayResult), 1);
        TestEqual(TEXT("...and no model is asked"), CountCommentaryRequests(), 0);
    }

    // Online: the offered moments become requests; the others don't.
    {
        PSCommentaryHookTests::FScopedBoothBridge Bridge;
        const int32 Before = CountCommentaryRequests();
        Fixture.PublishSnap();
        TestEqual(TEXT("A snap isn't offered"), CountCommentaryRequests(), Before);
        Bus->PublishPlayResult(Play);
        TestEqual(TEXT("A play's result is"), CountCommentaryRequests(), Before + 1);
        const FPSIntelRequest* Asked = Intelligence->GetRequests().FindByPredicate([](const FPSIntelRequest& Request)
        {
            return Request.Kind == EPSIntelRequestKind::Commentary && Request.IsOpen();
        });
        if (TestNotNull(TEXT("The commentary request"), Asked))
        {
            TestEqual(TEXT("...routed as the hooks' task"), Asked->Task, Model->GetTuning().ModelTask);
            TestEqual(TEXT("...with their instructions"), Asked->Instructions, Model->GetTuning().ModelInstructions);
            const TSharedPtr<FJsonObject> Facts = PSCommentaryHookTests::ParseObject(Asked->Context);
            TestTrue(TEXT("...and the play's facts"), Facts.IsValid() && Facts->GetStringField(TEXT("moment")) == TEXT("PlayResult"));
            TestTrue(TEXT("...within the budget"), Asked->Context.Len() <= Model->GetTuning().ModelContextChars);

            // The answer comes back as the moment's model line.
            FString Heard;
            const FDelegateHandle Hearing = Model->OnModelLineMC.AddLambda([&Heard](const FPSTelemetryCommentaryEvent& Moment, const FString& Text) { Heard = Text; });
            FString Reason;
            const int32 AskedId = Asked->RequestId;
            TestTrue(TEXT("A model answers"), Intelligence->AnswerRequest(AskedId, TEXT("Six yards up the middle."), Reason));
            TestEqual(TEXT("...and the hooks hear it"), Heard, FString(TEXT("Six yards up the middle.")));
            if (TestEqual(TEXT("...keeping it"), Model->GetModelLines().Num(), 1))
            {
                TestEqual(TEXT("...for the play's result"), Model->GetModelLines()[0].Moment.Moment, EPSCommentaryMoment::PlayResult);
                TestEqual(TEXT("...from its request"), Model->GetModelLines()[0].RequestId, AskedId);
            }
            Model->OnModelLineMC.Remove(Hearing);
        }

        // Switched off in the data, nothing is offered even online.
        FPSCommentaryHookTuning Quiet = Model->GetTuning();
        Quiet.bOfferToModels = false;
        Model->SetTuning(Quiet);
        const int32 Offered = CountCommentaryRequests();
        Bus->PublishPlayResult(Play);
        TestEqual(TEXT("With offers off, no model is asked"), CountCommentaryRequests(), Offered);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
