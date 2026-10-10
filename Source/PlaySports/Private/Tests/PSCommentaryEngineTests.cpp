// PSCommentaryEngineTests.cpp -- Epic 96 (the commentary engine)
//
// The booth (UPSCommentaryEngine) hears structured moments -- from the commentary hooks
// (UPSCommentaryEventModel) on the telemetry bus, or handed to it directly -- and speaks through
// the bus's Speech event, which the captions (Epic 103.3) show. Headless: the booth's clock moves
// only by AdvanceTime.
//
// Tests covered:
//   1. The event model weighs (96.1): stakes and novelty, and a play's primary player's game total
//      from Epic 92's box score, this play included.
//   2. Line selection (96.2, 96.4's template library): the shipped library validates; conditions;
//      a line's text from the string table; a big play cuts off the line being said, a lesser one
//      waits and is dropped when stale; cooldowns; repetition rotates equal lines; the captions.
//   3. Two voices (96.3): the analyst waits for the window after the play and for the
//      play-by-play, is cut off by it, and the snap closes his window.
//   4. Outside models and storylines (96.4, 96.5): with Epic 82's bridge online a model's line for
//      the play replaces the analyst's template line, a late one is dropped; the league's
//      storylines fill the analyst's quiet windows.
//   5. Repetition (96.6): per-game and per-season caps, the report, and the season's counts through
//      the franchise save.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"
#include "PSCommentaryEngine.h"
#include "PSCommentaryEventModel.h"
#include "PSFranchiseSaveGame.h"
#include "PSGameIntelligenceSubsystem.h"
#include "PSLeagueNarrative.h"
#include "PSLocalization.h"
#include "PSStatsEngine.h"
#include "PSTelemetryBus.h"
#include "PSUIAccessibilitySubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSCommentaryEngineTests
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
    struct FPSFakeAnalystBridge : public IModularFeature
    {
    };

    struct FScopedAnalystBridge
    {
        FPSFakeAnalystBridge Feature;

        FScopedAnalystBridge()
        {
            IModularFeatures::Get().RegisterModularFeature(UPSGameIntelligenceSubsystem::GetBridgeFeatureName(), &Feature);
        }

        ~FScopedAnalystBridge()
        {
            IModularFeatures::Get().UnregisterModularFeature(UPSGameIntelligenceSubsystem::GetBridgeFeatureName(), &Feature);
        }
    };

    /** The shipped library cut down to Ids, every pick deterministic (no variety band). */
    FPSCommentaryLibrary Only(const FPSCommentaryLibrary& Shipped, std::initializer_list<const TCHAR*> Ids)
    {
        FPSCommentaryLibrary Cut = Shipped;
        Cut.Lines.Reset();
        Cut.VarietyBand = 0.f;
        for (const TCHAR* Id : Ids)
        {
            const FName LineId(Id);
            if (const FPSCommentaryLineDef* Found = Shipped.Lines.FindByPredicate([LineId](const FPSCommentaryLineDef& Line) { return Line.LineId == LineId; }))
            {
                Cut.Lines.Add(*Found);
            }
        }
        return Cut;
    }

    FPSCommentaryLineDef* FindLine(FPSCommentaryLibrary& Library, const TCHAR* Id)
    {
        const FName LineId(Id);
        return Library.Lines.FindByPredicate([LineId](const FPSCommentaryLineDef& Line) { return Line.LineId == LineId; });
    }

    FPSTelemetryCommentaryEvent MakeMoment(EPSCommentaryMoment Kind, int32 PlayNumber)
    {
        FPSTelemetryCommentaryEvent Moment;
        Moment.Moment = Kind;
        Moment.PlayNumber = PlayNumber;
        Moment.Quarter = 1;
        Moment.GameClockSeconds = 540.f;
        Moment.Down = 1;
        Moment.Distance = 10;
        Moment.YardLine = 35;
        return Moment;
    }

    /** The booth of a fresh world, bound to its bus, updating every frame, every Speech recorded. */
    struct FBoothFixture
    {
        UWorld* World = nullptr;
        UPSTelemetryBus* Bus = nullptr;
        UPSCommentaryEngine* Booth = nullptr;
        UPSCommentaryEventModel* Model = nullptr;
        UPSGameIntelligenceSubsystem* Intelligence = nullptr;
        TArray<FPSTelemetrySpeechEvent> Speeches;
        FDelegateHandle Listening;

        bool Setup()
        {
            World = CreateTestWorld();
            Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
            Booth = World ? World->GetSubsystem<UPSCommentaryEngine>() : nullptr;
            Model = World ? World->GetSubsystem<UPSCommentaryEventModel>() : nullptr;
            Intelligence = World ? World->GetSubsystem<UPSGameIntelligenceSubsystem>() : nullptr;
            if (!Bus || !Booth || !Model || !Intelligence)
            {
                return false;
            }
            Listening = Bus->OnSpeechMC.AddLambda([this](const FPSTelemetrySpeechEvent& Event) { Speeches.Add(Event); });
            Booth->BindToBus(Bus);
            FPSPlatformTier EveryFrame;
            EveryFrame.AudioUpdateHz = 0.f;
            Booth->ApplyPlatformTier(EveryFrame);
            return true;
        }

        ~FBoothFixture()
        {
            if (Bus)
            {
                Bus->OnSpeechMC.Remove(Listening);
            }
            DestroyTestWorld(World);
        }

        /** Lets the booth run Seconds in steps of a tenth. */
        void Run(float Seconds)
        {
            for (float Ran = 0.f; Ran < Seconds - KINDA_SMALL_NUMBER; Ran += 0.1f)
            {
                Booth->AdvanceTime(0.1f);
            }
        }

        FName Saying(EPSCommentaryVoice Voice) const
        {
            FPSCommentaryUtterance Line;
            return Booth->GetCurrentLine(Voice, Line) ? Line.LineId : FName();
        }

        int32 CountSpoken(EPSCommentarySource Source) const
        {
            int32 Count = 0;
            for (const FPSCommentaryUtterance& Line : Booth->GetSpoken())
            {
                Count += Line.Source == Source ? 1 : 0;
            }
            return Count;
        }
    };
}

// ---------------------------------------------------------------------------
// Test 1 -- The event model weighs (96.1)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCommentaryWeighsTest,
    "PlaySports.Commentary.EventModelWeighs",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCommentaryWeighsTest::RunTest(const FString& Parameters)
{
    PSCommentaryEngineTests::FBoothFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus, the hooks and the booth"), Fixture.Setup()))
    {
        return false;
    }
    UPSCommentaryEventModel* Model = Fixture.Model;
    UPSTelemetryBus* Bus = Fixture.Bus;
    const FPSCommentaryHookTuning& Tuning = Model->GetTuning();

    // Stakes: a first down at midfield early in a tie game is only close; late, on third down in
    // the red zone with a score, they top out.
    FPSTelemetryCommentaryEvent Early = PSCommentaryEngineTests::MakeMoment(EPSCommentaryMoment::PlayResult, 1);
    Early.YardLine = 50;
    TestTrue(TEXT("Early, only the close score counts"), FMath::IsNearlyEqual(UPSCommentaryEventModel::ComputeStakes(Early, Tuning), Tuning.CloseGameStakes));
    FPSTelemetryCommentaryEvent Late = Early;
    Late.Quarter = Tuning.LateGameQuarter;
    Late.Down = 3;
    Late.YardLine = Tuning.RedZoneYardLine + 5;
    TestTrue(TEXT("Late, on third down in the red zone, more"),
        FMath::IsNearlyEqual(UPSCommentaryEventModel::ComputeStakes(Late, Tuning),
            FMath::Min(1.f, Tuning.LateQuarterStakes + Tuning.CloseGameStakes + Tuning.CriticalDownStakes + Tuning.RedZoneStakes)));
    Late.Points = 7;
    Late.bTurnover = true;
    TestTrue(TEXT("...and capped at 1"), UPSCommentaryEventModel::ComputeStakes(Late, Tuning) <= 1.f);

    // Novelty: the first of its kind is new, it fades by the horizon, a big play and a record stand out.
    TestTrue(TEXT("The first of its kind is new"), FMath::IsNearlyEqual(UPSCommentaryEventModel::ComputeNovelty(Early, 0, Tuning), 1.f));
    TestTrue(TEXT("...half way to the horizon, half as new"),
        FMath::IsNearlyEqual(UPSCommentaryEventModel::ComputeNovelty(Early, Tuning.NoveltyHorizon / 2, Tuning), 1.f - static_cast<float>(Tuning.NoveltyHorizon / 2) / Tuning.NoveltyHorizon));
    TestTrue(TEXT("...at the horizon, not new"), FMath::IsNearlyEqual(UPSCommentaryEventModel::ComputeNovelty(Early, Tuning.NoveltyHorizon, Tuning), 0.f));
    FPSTelemetryCommentaryEvent Big = Early;
    Big.Yards = Tuning.BigPlayYards;
    TestTrue(TEXT("A big play stands out even then"), FMath::IsNearlyEqual(UPSCommentaryEventModel::ComputeNovelty(Big, Tuning.NoveltyHorizon, Tuning), Tuning.BigPlayNovelty));
    FPSTelemetryCommentaryEvent Record = PSCommentaryEngineTests::MakeMoment(EPSCommentaryMoment::RecordBroken, 1);
    TestTrue(TEXT("A record is always new"), FMath::IsNearlyEqual(UPSCommentaryEventModel::ComputeNovelty(Record, 20, Tuning), 1.f));

    // From the bus, with the match's statistics: the moments carry their stakes, their novelty
    // and the player's game total, this play included.
    UPSStatsEngine* Stats = NewObject<UPSStatsEngine>();
    Stats->BeginGame(1, TEXT("HOM"), TEXT("AWY"));
    Model->SetStats(Stats);
    Model->BindToBus(Bus);
    Stats->BindToBus(Bus);
    TArray<FPSTelemetryCommentaryEvent> Results;
    const FDelegateHandle Hearing = Bus->OnCommentaryMC.AddLambda([&Results](const FPSTelemetryCommentaryEvent& Event)
    {
        if (Event.Moment == EPSCommentaryMoment::PlayResult || Event.Moment == EPSCommentaryMoment::Score)
        {
            Results.Add(Event);
        }
    });
    FPSTelemetryGameStateEvent State;
    State.bHomeHasPossession = true;
    Bus->PublishGameState(State);

    FPSTelemetryPlayResultEvent Play;
    Play.bHomeOffense = true;
    Play.Result = TEXT("Tackle");
    Play.bPass = true;
    Play.bComplete = true;
    Play.ReceiverId = TEXT("HOM_WR_001");
    Play.PasserId = TEXT("HOM_QB_001");
    FPSTelemetryCatchEvent Catch;
    Catch.ReceiverName = TEXT("Hawks WR");
    for (const int32 Yards : { 12, 8 })
    {
        FPSTelemetrySnapEvent Snap;
        Bus->PublishSnap(Snap);
        Catch.YardsGained = Yards;
        Bus->PublishCatch(Catch);
        Play.PlayNumber += 1;
        Play.YardsGained = Yards;
        Bus->PublishPlayResult(Play);
    }
    if (TestEqual(TEXT("Both plays are described"), Results.Num(), 2))
    {
        TestEqual(TEXT("A catch adds to the receiver's receiving yards"), Results[0].PrimaryStat, FName(TEXT("ReceivingYards")));
        TestEqual(TEXT("...12 after the first, though the box score hears it after the booth"), Results[0].PrimaryGameTotal, 12);
        TestEqual(TEXT("...20 after the second"), Results[1].PrimaryGameTotal, 20);
        TestTrue(TEXT("The second result is less new than the first"), Results[1].Novelty < Results[0].Novelty);
        TestTrue(TEXT("Its stakes are the model's"), FMath::IsNearlyEqual(Results[1].Stakes, UPSCommentaryEventModel::ComputeStakes(Results[1], Tuning)));
    }
    TestEqual(TEXT("The box score has both plays"), Stats->GetCurrentGame().PlayCount, 2);

    // A run for a touchdown: the Score moment counts the runner's touchdowns.
    FPSTelemetrySnapEvent GoalSnap;
    Bus->PublishSnap(GoalSnap);
    FPSTelemetryPlayResultEvent Run;
    Run.PlayNumber = 3;
    Run.bHomeOffense = true;
    Run.Result = TEXT("Touchdown");
    Run.YardsGained = 4;
    Run.HomePoints = 7;
    Run.RusherId = TEXT("HOM_RB_001");
    Bus->PublishPlayResult(Run);
    if (TestEqual(TEXT("The run and its score"), Results.Num(), 4))
    {
        TestEqual(TEXT("The run adds to his rushing yards"), Results[2].PrimaryStat, FName(TEXT("RushingYards")));
        TestEqual(TEXT("The score counts his touchdowns"), Results[3].PrimaryStat, FName(TEXT("RushingTouchdowns")));
        TestEqual(TEXT("...one"), Results[3].PrimaryGameTotal, 1);
        TestTrue(TEXT("A score raises the stakes"), Results[3].Stakes >= Tuning.ScoreStakes);
    }
    Bus->OnCommentaryMC.Remove(Hearing);
    Stats->UnbindFromBus();
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Line selection (96.2, 96.4)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCommentaryLineSelectionTest,
    "PlaySports.Commentary.LineSelection",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCommentaryLineSelectionTest::RunTest(const FString& Parameters)
{
    using namespace PSCommentaryEngineTests;
    FBoothFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus and the booth"), Fixture.Setup()))
    {
        return false;
    }
    UPSCommentaryEngine* Booth = Fixture.Booth;

    // The shipped library: sound, every line with its text.
    const FPSCommentaryLibrary Shipped = Booth->GetLibrary();
    TestTrue(TEXT("The library has lines"), Shipped.Lines.Num() > 20);
    TestEqual(TEXT("...and validates"), UPSCommentaryEngine::ValidateLibrary(Shipped).Num(), 0);
    {
        FPSCommentaryLibrary Bad = Shipped;
        Bad.Lines[0].MinYards = 10;
        Bad.Lines[0].MaxYards = 0;
        TestFalse(TEXT("Conditions that can't hold are refused"), Booth->SetLibrary(Bad));
        Bad = Shipped;
        FPSCommentaryLineDef Mute;
        Mute.LineId = TEXT("No.Such.Line");
        Bad.Lines.Add(Mute);
        TestFalse(TEXT("A line without its text is refused"), Booth->SetLibrary(Bad));
        Bad = Shipped;
        Bad.InterruptMargin = 200;
        TestFalse(TEXT("An interrupt margin over 100 is refused"), Booth->SetLibrary(Bad));
    }

    // Conditions.
    FPSCommentaryLibrary Lines = Shipped;
    const FPSCommentaryLineDef* BigCatch = FindLine(Lines, TEXT("PbP.Catch.Big"));
    const FPSCommentaryLineDef* ThirdDown = FindLine(Lines, TEXT("PbP.Snap.Third"));
    const FPSCommentaryLineDef* Deep = FindLine(Lines, TEXT("PbP.Pass.Deep"));
    if (TestNotNull(TEXT("The big catch's line"), BigCatch) && TestNotNull(TEXT("A third down's"), ThirdDown) && TestNotNull(TEXT("A deep pass's"), Deep))
    {
        FPSTelemetryCommentaryEvent Caught = MakeMoment(EPSCommentaryMoment::Catch, 1);
        Caught.PrimaryName = TEXT("Hawks WR");
        Caught.Yards = 25;
        TestTrue(TEXT("A 25-yard catch fits the big catch's line"), UPSCommentaryEngine::LineFits(*BigCatch, Caught));
        Caught.Yards = 10;
        TestFalse(TEXT("...a 10-yard one doesn't"), UPSCommentaryEngine::LineFits(*BigCatch, Caught));
        Caught.Yards = 25;
        Caught.PrimaryName.Reset();
        TestFalse(TEXT("...nor one without the catcher's name"), UPSCommentaryEngine::LineFits(*BigCatch, Caught));
        FPSTelemetryCommentaryEvent Snapped = MakeMoment(EPSCommentaryMoment::Snap, 1);
        Snapped.Down = 3;
        TestTrue(TEXT("Third down fits the third-down line"), UPSCommentaryEngine::LineFits(*ThirdDown, Snapped));
        Snapped.Down = 2;
        TestFalse(TEXT("...second down doesn't"), UPSCommentaryEngine::LineFits(*ThirdDown, Snapped));
        FPSTelemetryCommentaryEvent Thrown = MakeMoment(EPSCommentaryMoment::Pass, 1);
        Thrown.PrimaryName = TEXT("Hawks QB");
        Thrown.Detail = TEXT("Short");
        TestFalse(TEXT("A short pass doesn't fit the deep line"), UPSCommentaryEngine::LineFits(*Deep, Thrown));

        // The text: the string table's row, the moment's facts filled in.
        Caught.PrimaryName = TEXT("Hawks WR");
        const FString Said = UPSCommentaryEngine::FormatLine(*BigCatch, Caught);
        TestTrue(TEXT("A line's text names the player"), Said.Contains(TEXT("Hawks WR")));
        TestFalse(TEXT("...with nothing left unfilled"), Said.Contains(TEXT("{")) || Said.Contains(TEXT("<Commentary")));
    }
    const FPSCommentaryLineDef* SackLine = FindLine(Lines, TEXT("PbP.Sack"));
    if (TestNotNull(TEXT("The sack's line"), SackLine))
    {
        FPSTelemetryCommentaryEvent Sacked = MakeMoment(EPSCommentaryMoment::Sack, 1);
        Sacked.PrimaryName = TEXT("Wolves DE");
        TestTrue(TEXT("A sack names the rusher"), UPSCommentaryEngine::FormatLine(*SackLine, Sacked).Contains(TEXT("Wolves DE")));
    }

    // Speaking: a cut-down library, every pick deterministic.
    FPSCommentaryLibrary Cut = Only(Shipped, { TEXT("PbP.GameStart"), TEXT("PbP.Snap.First"), TEXT("PbP.Interception"), TEXT("PbP.Catch"),
        TEXT("PbP.Pass.Short"), TEXT("PbP.Sack"), TEXT("PbP.Sack.Plain") });
    if (FPSCommentaryLineDef* Snap = FindLine(Cut, TEXT("PbP.Snap.First")))
    {
        Snap->CooldownSeconds = 0.f;
    }
    if (!TestTrue(TEXT("The cut-down library is taken"), Booth->SetLibrary(Cut)))
    {
        return false;
    }

    Booth->HandleMoment(MakeMoment(EPSCommentaryMoment::GameStart, 0));
    TestEqual(TEXT("The game's start is called"), Fixture.Saying(EPSCommentaryVoice::PlayByPlay), FName(TEXT("PbP.GameStart")));
    if (TestEqual(TEXT("...out loud, on the bus"), Fixture.Speeches.Num(), 1))
    {
        const FPSTelemetrySpeechEvent& Speech = Fixture.Speeches[0];
        TestEqual(TEXT("...on the commentary channel"), Speech.Channel, FName(TEXT("Commentary")));
        TestEqual(TEXT("...by the play-by-play voice"), Speech.Speaker, UPSLocalization::GetText(TEXT("Commentary.Voice.PlayByPlay")).ToString());
        TestEqual(TEXT("...its line's text"), Speech.Text, UPSLocalization::GetText(TEXT("Commentary.Line.PbP.GameStart")).ToString());
        TestEqual(TEXT("...and its id, for the voice-over"), Speech.LineId, FName(TEXT("PbP.GameStart")));
        TestTrue(TEXT("...timed by its words"), Speech.DurationSeconds >= Cut.MinLineSeconds && Speech.DurationSeconds <= Cut.MaxLineSeconds);
    }
    if (UPSUIAccessibilitySubsystem* Captions = Fixture.World->GetSubsystem<UPSUIAccessibilitySubsystem>())
    {
        const TArray<FPSCaptionLine> Shown = Captions->GetActiveCaptions(Fixture.World->GetTimeSeconds());
        TestTrue(TEXT("The caption shows it"), Shown.Num() >= 1 && Fixture.Speeches.Num() > 0
            && UPSUIAccessibilitySubsystem::FormatCaption(Shown.Last()).Contains(Fixture.Speeches[0].Text));
    }
    Fixture.Run(10.f);
    TestFalse(TEXT("A line ends when its words are said"), Booth->IsSpeaking(EPSCommentaryVoice::PlayByPlay));

    // A big play cuts off the line being said; a lesser one waits its turn.
    FPSTelemetryCommentaryEvent Snapped = MakeMoment(EPSCommentaryMoment::Snap, 1);
    Booth->HandleMoment(Snapped);
    TestEqual(TEXT("The snap is called"), Fixture.Saying(EPSCommentaryVoice::PlayByPlay), FName(TEXT("PbP.Snap.First")));
    FPSTelemetryCommentaryEvent Picked = MakeMoment(EPSCommentaryMoment::Interception, 1);
    Picked.PrimaryName = TEXT("Wolves S");
    Booth->HandleMoment(Picked);
    TestEqual(TEXT("An interception cuts it off"), Fixture.Saying(EPSCommentaryVoice::PlayByPlay), FName(TEXT("PbP.Interception")));
    const TArray<FPSCommentaryUtterance>& Spoken = Booth->GetSpoken();
    TestTrue(TEXT("...the snap's call marked cut off"), Spoken.Num() >= 2 && Spoken[Spoken.Num() - 2].bInterrupted && Spoken[Spoken.Num() - 2].LineId == FName(TEXT("PbP.Snap.First")));
    FPSTelemetryCommentaryEvent Caught = MakeMoment(EPSCommentaryMoment::Catch, 1);
    Caught.PrimaryName = TEXT("Wolves S");
    Caught.Yards = 4;
    Booth->HandleMoment(Caught);
    TestEqual(TEXT("A catch waits behind the interception"), Fixture.Saying(EPSCommentaryVoice::PlayByPlay), FName(TEXT("PbP.Interception")));
    TestEqual(TEXT("...in the queue"), Booth->GetQueueLength(EPSCommentaryVoice::PlayByPlay), 1);
    FPSCommentaryUtterance Current;
    Booth->GetCurrentLine(EPSCommentaryVoice::PlayByPlay, Current);
    Fixture.Run(Current.DurationSeconds + 0.1f);
    TestEqual(TEXT("...and is said once the interception's call is over"), Fixture.Saying(EPSCommentaryVoice::PlayByPlay), FName(TEXT("PbP.Catch")));

    // A line that waits too long is dropped: the moment has passed.
    Fixture.Run(10.f);
    FPSCommentaryLibrary Impatient = Cut;
    Impatient.MaxDelaySeconds = 0.5f;
    Booth->SetLibrary(Impatient);
    const int32 StaleBefore = Booth->BuildRepetitionReport().DroppedStale;
    Booth->HandleMoment(Picked);
    Booth->HandleMoment(Caught);
    Booth->GetCurrentLine(EPSCommentaryVoice::PlayByPlay, Current);
    Fixture.Run(Current.DurationSeconds + 0.1f);
    TestFalse(TEXT("A catch still waiting after MaxDelaySeconds isn't said"), Fixture.Saying(EPSCommentaryVoice::PlayByPlay) == FName(TEXT("PbP.Catch")));
    TestEqual(TEXT("...it is dropped as stale"), Booth->BuildRepetitionReport().DroppedStale, StaleBefore + 1);
    Booth->SetLibrary(Cut);

    // A line's cooldown.
    Fixture.Run(10.f);
    FPSTelemetryCommentaryEvent Thrown = MakeMoment(EPSCommentaryMoment::Pass, 2);
    Thrown.PrimaryName = TEXT("Hawks QB");
    Thrown.Detail = TEXT("Short");
    Booth->HandleMoment(Thrown);
    TestEqual(TEXT("A short pass is called"), Fixture.Saying(EPSCommentaryVoice::PlayByPlay), FName(TEXT("PbP.Pass.Short")));
    const FPSCommentaryLineDef* Short = FindLine(Cut, TEXT("PbP.Pass.Short"));
    if (TestNotNull(TEXT("The short pass's line"), Short) && TestTrue(TEXT("...has a cooldown"), Short->CooldownSeconds > 1.f))
    {
        Fixture.Run(1.f);
        TestNull(TEXT("Within its cooldown it isn't said again"), Booth->SelectLine(Thrown, EPSCommentaryVoice::PlayByPlay));
        Fixture.Run(Short->CooldownSeconds);
        TestNotNull(TEXT("...after it, it is"), Booth->SelectLine(Thrown, EPSCommentaryVoice::PlayByPlay));
    }

    // Repetition rotates equal lines: the sack's named call first, then the plain one.
    Fixture.Run(10.f);
    FPSTelemetryCommentaryEvent Sacked = MakeMoment(EPSCommentaryMoment::Sack, 3);
    Sacked.PrimaryName = TEXT("Wolves DE");
    Booth->HandleMoment(Sacked);
    TestEqual(TEXT("The first sack names the rusher"), Fixture.Saying(EPSCommentaryVoice::PlayByPlay), FName(TEXT("PbP.Sack")));
    Fixture.Run(10.f);
    Booth->HandleMoment(Sacked);
    TestEqual(TEXT("...the next, said once already, gives way to the other line"), Fixture.Saying(EPSCommentaryVoice::PlayByPlay), FName(TEXT("PbP.Sack.Plain")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Two voices (96.3)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCommentaryTwoVoicesTest,
    "PlaySports.Commentary.TwoVoices",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCommentaryTwoVoicesTest::RunTest(const FString& Parameters)
{
    using namespace PSCommentaryEngineTests;
    FBoothFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus and the booth"), Fixture.Setup()))
    {
        return false;
    }
    UPSCommentaryEngine* Booth = Fixture.Booth;
    FPSCommentaryLibrary Cut = Only(Booth->GetLibrary(), { TEXT("PbP.Result.FirstDown"), TEXT("Color.Result.FirstDown"), TEXT("PbP.Timeout"),
        TEXT("PbP.Snap.First"), TEXT("Color.Sack") });
    for (FPSCommentaryLineDef& Line : Cut.Lines)
    {
        Line.CooldownSeconds = 0.f;
    }
    if (!TestTrue(TEXT("The cut-down library is taken"), Booth->SetLibrary(Cut)))
    {
        return false;
    }

    // The play is over: the play-by-play calls it, the analyst waits for his window.
    FPSTelemetryCommentaryEvent Result = MakeMoment(EPSCommentaryMoment::PlayResult, 1);
    Result.Detail = TEXT("Tackle");
    Result.Yards = 12;
    Result.bFirstDown = true;
    Result.PrimaryName = TEXT("Hawks RB");
    Booth->HandleMoment(Result);
    TestEqual(TEXT("The play-by-play calls the play"), Fixture.Saying(EPSCommentaryVoice::PlayByPlay), FName(TEXT("PbP.Result.FirstDown")));
    TestFalse(TEXT("The analyst waits"), Booth->IsSpeaking(EPSCommentaryVoice::Color));
    TestEqual(TEXT("...his line queued"), Booth->GetQueueLength(EPSCommentaryVoice::Color), 1);
    TestFalse(TEXT("...his window not open yet"), Booth->IsColorWindowOpen());
    Fixture.Run(Cut.ColorWindowDelaySeconds + 0.1f);
    TestTrue(TEXT("After the delay the window opens"), Booth->IsColorWindowOpen());
    FPSCommentaryUtterance Call;
    if (Booth->GetCurrentLine(EPSCommentaryVoice::PlayByPlay, Call))
    {
        TestFalse(TEXT("...but he doesn't talk over the play-by-play"), Booth->IsSpeaking(EPSCommentaryVoice::Color));
        Fixture.Run(Call.DurationSeconds);
    }
    TestEqual(TEXT("Then the analyst speaks"), Fixture.Saying(EPSCommentaryVoice::Color), FName(TEXT("Color.Result.FirstDown")));
    if (TestTrue(TEXT("...out loud"), Fixture.Speeches.Num() >= 2))
    {
        TestEqual(TEXT("...as the analyst"), Fixture.Speeches.Last().Speaker, UPSLocalization::GetText(TEXT("Commentary.Voice.Color")).ToString());
    }

    // The play-by-play cuts him off.
    Booth->HandleMoment(MakeMoment(EPSCommentaryMoment::Timeout, 1));
    TestEqual(TEXT("A timeout is called"), Fixture.Saying(EPSCommentaryVoice::PlayByPlay), FName(TEXT("PbP.Timeout")));
    TestFalse(TEXT("...over the analyst, who stops"), Booth->IsSpeaking(EPSCommentaryVoice::Color));
    const FPSCommentaryUtterance* CutOff = Booth->GetSpoken().FindByPredicate([](const FPSCommentaryUtterance& Line) { return Line.Voice == EPSCommentaryVoice::Color; });
    TestTrue(TEXT("...his line marked cut off"), CutOff && CutOff->bInterrupted);

    // The snap closes his window and his queue.
    Fixture.Run(10.f);
    Result.PlayNumber = 2;
    Booth->HandleMoment(Result);
    TestEqual(TEXT("Another play: the analyst has a line waiting"), Booth->GetQueueLength(EPSCommentaryVoice::Color), 1);
    Booth->HandleMoment(MakeMoment(EPSCommentaryMoment::Snap, 3));
    TestFalse(TEXT("The snap closes his window"), Booth->IsColorWindowOpen());
    TestEqual(TEXT("...and clears his queue"), Booth->GetQueueLength(EPSCommentaryVoice::Color), 0);

    // During the play he holds his thoughts until it is over.
    FPSTelemetryCommentaryEvent Sacked = MakeMoment(EPSCommentaryMoment::Sack, 3);
    Booth->HandleMoment(Sacked);
    Fixture.Run(5.f);
    TestFalse(TEXT("A sack's analysis waits while the ball is live"), Booth->IsSpeaking(EPSCommentaryVoice::Color));
    FPSTelemetryCommentaryEvent Loss = MakeMoment(EPSCommentaryMoment::PlayResult, 3);
    Loss.Detail = TEXT("Tackle");
    Loss.Yards = -8;
    Booth->HandleMoment(Loss);
    Fixture.Run(Cut.ColorWindowDelaySeconds + 0.2f);
    TestEqual(TEXT("...and is said once the play is over"), Fixture.Saying(EPSCommentaryVoice::Color), FName(TEXT("Color.Sack")));
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Outside models and storylines (96.4, 96.5)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCommentaryModelAndStorylinesTest,
    "PlaySports.Commentary.ModelLinesAndStorylines",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCommentaryModelAndStorylinesTest::RunTest(const FString& Parameters)
{
    using namespace PSCommentaryEngineTests;
    FBoothFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus, the hooks and the booth"), Fixture.Setup()))
    {
        return false;
    }
    UPSCommentaryEngine* Booth = Fixture.Booth;
    UPSTelemetryBus* Bus = Fixture.Bus;
    UPSGameIntelligenceSubsystem* Intelligence = Fixture.Intelligence;
    Fixture.Model->BindToBus(Bus);
    Fixture.Model->SetIntelligence(Intelligence);
    Booth->SetEventModel(Fixture.Model);
    FPSCommentaryLibrary Cut = Only(Booth->GetLibrary(), { TEXT("PbP.Result.Gain"), TEXT("Color.Result.FirstDown"), TEXT("PbP.Timeout") });
    for (FPSCommentaryLineDef& Line : Cut.Lines)
    {
        Line.CooldownSeconds = 0.f;
    }
    if (!TestTrue(TEXT("The cut-down library is taken"), Booth->SetLibrary(Cut)))
    {
        return false;
    }

    FPSTelemetryGameStateEvent State;
    State.bHomeHasPossession = true;
    Bus->PublishGameState(State);
    FPSTelemetryPlayResultEvent Play;
    Play.bHomeOffense = true;
    Play.Result = TEXT("Tackle");
    Play.YardsGained = 12;
    Play.bFirstDown = true;
    Play.RusherId = TEXT("HOM_RB_001");
    FPSTelemetryTackleEvent Tackle;
    Tackle.BallCarrierName = TEXT("Hawks RB");
    Tackle.TacklerName = TEXT("Wolves LB");
    const auto PlayOne = [&Fixture, &Play, &Tackle]()
    {
        FPSTelemetrySnapEvent Snap;
        Fixture.Bus->PublishSnap(Snap);
        Fixture.Bus->PublishTackle(Tackle);
        Fixture.Bus->PublishPlayResult(Play);
    };

    // Offline: the analyst's template line.
    if (!UPSGameIntelligenceSubsystem::IsBridgeOnline())
    {
        PlayOne();
        Fixture.Run(8.f);
        TestTrue(TEXT("Offline the analyst says his template line"), Booth->GetGameUses(TEXT("Color.Result.FirstDown")) == 1);
        TestEqual(TEXT("...and no model's"), Fixture.CountSpoken(EPSCommentarySource::Model), 0);
    }

    // Online: a model's line for the play replaces the analyst's template line.
    {
        FScopedAnalystBridge Bridge;
        const int32 TemplateBefore = Booth->GetGameUses(TEXT("Color.Result.FirstDown"));
        PlayOne();
        const FPSIntelRequest* Asked = Intelligence->GetRequests().FindByPredicate([](const FPSIntelRequest& Request)
        {
            return Request.Kind == EPSIntelRequestKind::Commentary && Request.IsOpen();
        });
        if (TestNotNull(TEXT("The play is offered to a model"), Asked))
        {
            const int32 AskedId = Asked->RequestId;
            const FString Answer = TEXT("He found the crease and hit it hard.");
            FString Reason;
            TestTrue(TEXT("A model answers"), Intelligence->AnswerRequest(AskedId, Answer, Reason));
            Fixture.Run(8.f);
            const FPSCommentaryUtterance* Voiced = Booth->GetSpoken().FindByPredicate([](const FPSCommentaryUtterance& Line) { return Line.Source == EPSCommentarySource::Model; });
            if (TestNotNull(TEXT("The analyst voices the model's line"), Voiced))
            {
                TestEqual(TEXT("...as written (not ours to translate)"), Voiced->Text, UPSLocalization::Verbatim(Answer).ToString());
                TestEqual(TEXT("...as the analyst"), Voiced->Voice, EPSCommentaryVoice::Color);
            }
            TestEqual(TEXT("...in place of his template line"), Booth->GetGameUses(TEXT("Color.Result.FirstDown")), TemplateBefore);
        }

        // A model's line for a play that's over is dropped.
        PlayOne();
        const FPSIntelRequest* Late = Intelligence->GetRequests().FindByPredicate([](const FPSIntelRequest& Request)
        {
            return Request.Kind == EPSIntelRequestKind::Commentary && Request.IsOpen();
        });
        if (TestNotNull(TEXT("The next play is offered too"), Late))
        {
            const int32 LateId = Late->RequestId;
            FPSTelemetrySnapEvent NextSnap;
            Bus->PublishSnap(NextSnap);
            const int32 ModelLinesBefore = Fixture.CountSpoken(EPSCommentarySource::Model);
            FString Reason;
            Intelligence->AnswerRequest(LateId, TEXT("Too late for this one."), Reason);
            Fixture.Run(8.f);
            TestEqual(TEXT("An answer after the next snap isn't voiced"), Fixture.CountSpoken(EPSCommentarySource::Model), ModelLinesBefore);
        }
    }

    // Storylines: the league's talking points fill the analyst's quiet windows.
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    FPSStoryline Streak;
    Streak.StorylineId = TEXT("WinStreak.HOM");
    Streak.Kind = EPSStorylineKind::WinStreak;
    Streak.TeamId = TEXT("HOM");
    Streak.Value = 4;
    Streak.Weight = 1.f;
    Save->Narrative.ActiveStorylines.Add(Streak);
    UPSLeagueNarrative* Narrative = NewObject<UPSLeagueNarrative>();
    TestTrue(TEXT("The league has a storyline"), Narrative->LoadFrom(Save));
    TestEqual(TEXT("The booth takes the game's talking points"), Booth->FeedFromNarrative(Narrative, TEXT("HOM"), TEXT("AWY")), 1);
    TestEqual(TEXT("...only about these teams"), Booth->FeedFromNarrative(Narrative, TEXT("OTH"), TEXT("ERS")), 0);
    Booth->FeedFromNarrative(Narrative, TEXT("HOM"), TEXT("AWY"));
    const TArray<FPSNewsItem> Points = Narrative->GetTalkingPoints(TEXT("HOM"), TEXT("AWY"), 1);
    Fixture.Run(40.f);
    Booth->HandleMoment(MakeMoment(EPSCommentaryMoment::Timeout, 9));
    Fixture.Run(8.f);
    const FPSCommentaryUtterance* Told = Booth->GetSpoken().FindByPredicate([](const FPSCommentaryUtterance& Line) { return Line.Source == EPSCommentarySource::TalkingPoint; });
    if (TestNotNull(TEXT("In a quiet window the analyst brings up the storyline"), Told) && Points.Num() == 1)
    {
        FFormatNamedArguments Arguments;
        Arguments.Add(TEXT("Headline"), UPSLocalization::FromLocalized(Points[0].Headline));
        Arguments.Add(TEXT("Body"), UPSLocalization::FromLocalized(Points[0].Body));
        TestEqual(TEXT("...in the league news's words"), Told->Text, UPSLocalization::Format(TEXT("Commentary.Storyline"), Arguments).ToString());
    }
    Fixture.Run(40.f);
    Booth->HandleMoment(MakeMoment(EPSCommentaryMoment::Timeout, 10));
    Fixture.Run(8.f);
    TestEqual(TEXT("...each storyline once"), Booth->GetTalkingPointsSpoken(), 1);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Repetition (96.6)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCommentaryRepetitionTest,
    "PlaySports.Commentary.RepetitionCaps",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCommentaryRepetitionTest::RunTest(const FString& Parameters)
{
    using namespace PSCommentaryEngineTests;
    FBoothFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus and the booth"), Fixture.Setup()))
    {
        return false;
    }
    UPSCommentaryEngine* Booth = Fixture.Booth;
    FPSCommentaryLibrary Cut = Only(Booth->GetLibrary(), { TEXT("PbP.Snap.First"), TEXT("PbP.Snap.Second") });
    FPSCommentaryLineDef* First = FindLine(Cut, TEXT("PbP.Snap.First"));
    FPSCommentaryLineDef* Second = FindLine(Cut, TEXT("PbP.Snap.Second"));
    if (!TestNotNull(TEXT("First down's line"), First) || !TestNotNull(TEXT("Second down's line"), Second))
    {
        return false;
    }
    First->CooldownSeconds = 0.f;
    First->MaxPerGame = 2;
    Second->CooldownSeconds = 0.f;
    Second->MaxPerSeason = 3;
    if (!TestTrue(TEXT("The capped library is taken"), Booth->SetLibrary(Cut)))
    {
        return false;
    }
    Booth->BeginSeason(1);

    FPSTelemetryCommentaryEvent FirstDown = MakeMoment(EPSCommentaryMoment::Snap, 1);
    FPSTelemetryCommentaryEvent SecondDown = MakeMoment(EPSCommentaryMoment::Snap, 1);
    SecondDown.Down = 2;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        Booth->HandleMoment(FirstDown);
        Fixture.Run(5.f);
    }
    TestEqual(TEXT("A line is said up to its game cap"), Booth->GetGameUses(TEXT("PbP.Snap.First")), 2);
    FPSCommentaryRepetitionReport Report = Booth->BuildRepetitionReport();
    TestTrue(TEXT("...the third pick is held by the cap"), Report.SuppressedByCap >= 1 && Report.LinesAtCap.Contains(FName(TEXT("PbP.Snap.First"))));
    for (int32 Index = 0; Index < 2; ++Index)
    {
        Booth->HandleMoment(SecondDown);
        Fixture.Run(5.f);
    }

    // The game's repetition, measured.
    Report = Booth->BuildRepetitionReport();
    TestEqual(TEXT("Four lines said"), Report.Spoken, 4);
    TestEqual(TEXT("...two different"), Report.DistinctLines, 2);
    TestTrue(TEXT("...half of them repeats"), FMath::IsNearlyEqual(Report.RepeatShare, 0.5f));
    TestEqual(TEXT("...the most used (ties by name)"), Report.MostUsedLineId, FName(TEXT("PbP.Snap.First")));
    TestEqual(TEXT("...twice"), Report.MostUsedCount, 2);

    // A new game: the game's counts start again, the season's carry on to its cap.
    Booth->HandleMoment(MakeMoment(EPSCommentaryMoment::GameStart, 0));
    TestEqual(TEXT("A new game starts the game's counts again"), Booth->GetGameUses(TEXT("PbP.Snap.First")), 0);
    TestEqual(TEXT("...not the season's"), Booth->GetSeasonUses(TEXT("PbP.Snap.First")), 2);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        Booth->HandleMoment(SecondDown);
        Fixture.Run(5.f);
    }
    TestEqual(TEXT("The season's cap holds across games"), Booth->GetSeasonUses(TEXT("PbP.Snap.Second")), 3);
    TestNull(TEXT("...the capped line isn't picked"), Booth->SelectLine(SecondDown, EPSCommentaryVoice::PlayByPlay));

    // The season's counts persist through the franchise save.
    UPSFranchiseSaveGame* Save = NewObject<UPSFranchiseSaveGame>();
    Booth->SaveTo(Save);
    TestEqual(TEXT("The save keeps the season"), Save->CommentaryUsage.Season, 1);
    TestEqual(TEXT("...and both lines' counts"), Save->CommentaryUsage.Lines.Num(), 2);
    Booth->BeginSeason(2);
    TestEqual(TEXT("A new season starts its counts again"), Booth->GetSeasonUses(TEXT("PbP.Snap.Second")), 0);
    TestTrue(TEXT("The saved season reads back"), Booth->LoadFrom(Save));
    TestEqual(TEXT("...with its counts"), Booth->GetSeasonUses(TEXT("PbP.Snap.Second")), 3);
    TestFalse(TEXT("A save without commentary keeps the counts"), Booth->LoadFrom(NewObject<UPSFranchiseSaveGame>()));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
