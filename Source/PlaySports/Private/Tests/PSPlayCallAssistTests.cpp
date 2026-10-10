// PSPlayCallAssistTests.cpp -- Epic 102 part 2 (suggestions, recent calls, play clock)
//
// Tests covered:
//   1. The coaching AI ranks plays for a situation and says why: short yardage puts a run
//      on top, long yardage a deep shot, a passing down a blitz; the call screen's
//      suggestion is the top-ranked play with those reasons.
//   2. The play clock: a human who runs it down to the quick-call mark gets the suggestion
//      called for them, it snaps like a CPU call, and the open call screen closes.
//   3. Recent calls and the tendency readout follow what the human called and ran, and the
//      call screen offers the suggestion, then recent plays, then the formations.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSCoachingAI.h"
#include "PSMenuComponent.h"
#include "PSOffenseController.h"
#include "PSPlayCallComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlayCallAssistTests
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

    /** A controller holding a QB, bound to the play-call authority as the game binds it. */
    static APSPlayerController* SpawnHumanQuarterback(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        APSPlayerPawn* QB = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        APSPlayerController* Controller = World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        if (!QB || !Controller)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = TEXT("QB_TEST");
        Attributes.DisplayName = TEXT("QB_TEST");
        Attributes.Role = EPlayerRole::Quarterback;
        QB->InitializePlayer(Attributes);

        Controller->GetPlayCallComponent()->BindToPlayCall();
        Controller->TakeControlOf(QB);
        return Controller;
    }

    static FPSSituationContext MakeSituation(int32 Down, int32 Distance, int32 YardLine)
    {
        FPSSituationContext Situation;
        Situation.Down = Down;
        Situation.Distance = Distance;
        Situation.YardLine = YardLine;
        return Situation;
    }

    static bool AnyContains(const TArray<FString>& Lines, const TCHAR* Fragment)
    {
        return Lines.ContainsByPredicate([Fragment](const FString& Line) { return Line.Contains(Fragment); });
    }

    /** Opens a window, has the human call PlayId, and snaps it. */
    static void RunHumanCall(UPSPlayCallSubsystem* PlayCall, UPSTelemetryBus* Bus, FName PlayId)
    {
        PlayCall->OpenPlayCall(MakeSituation(1, 10, 25));
        PlayCall->CallPlay(PlayId, EPSPlayCaller::Human);
        FPSTelemetrySnapEvent Snap;
        Snap.LineOfScrimmage = FVector(2500.f, 0.f, 0.f);
        Bus->PublishSnap(Snap);
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Ranked suggestions with reasons
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSSuggestionReasonsTest,
    "PlaySports.PlayCall.SuggestionsExplainThemselves",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSSuggestionReasonsTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayCallAssistTests;

    UWorld* World = CreateTestWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Play-call subsystem"), PlayCall))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    UPSCoachingAI* CoachingAI = NewObject<UPSCoachingAI>();
    const FPSTendencyProfile Tendency = FPSTendencyProfile();
    const TArray<FPSPlayDefinition> Offense = PlayCall->GetPlays(true);
    const TArray<FPSPlayDefinition> Defense = PlayCall->GetPlays(false);

    // 3rd and 2: run it.
    const TArray<FPSPlaySuggestion> ShortYardage = CoachingAI->RankPlays(MakeSituation(3, 2, 45), Tendency, Offense, true);
    if (TestEqual(TEXT("Every offensive play is ranked"), ShortYardage.Num(), Offense.Num()))
    {
        TestEqual(TEXT("Short yardage ranks a run first"), ShortYardage[0].Category, FString(TEXT("Run")));
        TestTrue(TEXT("...and says why"), AnyContains(ShortYardage[0].Reasons, TEXT("Short yardage")));
        bool bSorted = true;
        for (int32 Index = 1; Index < ShortYardage.Num(); ++Index)
        {
            bSorted &= ShortYardage[Index - 1].Weight >= ShortYardage[Index].Weight;
        }
        TestTrue(TEXT("Best first"), bSorted);
    }

    // 2nd and 15: take a shot.
    const TArray<FPSPlaySuggestion> LongYardage = CoachingAI->RankPlays(MakeSituation(2, 15, 45), Tendency, Offense, true);
    if (LongYardage.Num() > 0)
    {
        TestEqual(TEXT("Long yardage ranks a deep pass first"), LongYardage[0].Category, FString(TEXT("DeepPass")));
        TestTrue(TEXT("...and says why"), AnyContains(LongYardage[0].Reasons, TEXT("Long yardage")));
    }

    // 3rd and 8 on defense: bring pressure.
    const TArray<FPSPlaySuggestion> PassingDown = CoachingAI->RankPlays(MakeSituation(3, 8, 45), Tendency, Defense, false);
    if (PassingDown.Num() > 0)
    {
        TestEqual(TEXT("A passing down ranks the blitz first"), PassingDown[0].Category, FString(TEXT("Blitz")));
        TestTrue(TEXT("...and says why"), AnyContains(PassingDown[0].Reasons, TEXT("Passing down")));
    }

    // The call screen's suggestion is the top of the same ranking.
    PlayCall->OpenPlayCall(MakeSituation(3, 2, 45));
    const TArray<FPSPlaySuggestion> Ranked = PlayCall->RankPlays(true);
    const TArray<FPSMenuOptionDef> Suggestion = PlayCall->BuildSuggestionOptions(true);
    if (TestEqual(TEXT("One suggestion"), Suggestion.Num(), 1) && Ranked.Num() > 0)
    {
        TestEqual(TEXT("It calls the top-ranked play"), Suggestion[0].Payload, Ranked[0].PlayId);
        TestTrue(TEXT("...with the CallPlay command"), Suggestion[0].Command == EPSMenuCommand::CallPlay);
        TestTrue(TEXT("Its label names the play"), Suggestion[0].Label.Contains(Ranked[0].DisplayName));
        TestTrue(TEXT("Its detail gives the reasons"), Suggestion[0].Detail.Contains(TEXT("Short yardage")));
    }
    TestEqual(TEXT("The situation reads like a broadcast"), UPSPlayCallSubsystem::DescribeSituation(MakeSituation(3, 2, 45)), FString(TEXT("3rd & 2 at own 45")));
    TestEqual(TEXT("...on the other side of midfield too"), UPSPlayCallSubsystem::DescribeSituation(MakeSituation(1, 10, 80)), FString(TEXT("1st & 10 at opp 20")));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Quick-call when the play clock runs low
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSQuickCallTest,
    "PlaySports.PlayCall.QuickCallOnPlayClock",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSQuickCallTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayCallAssistTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    APSPlayerController* Controller = World ? SpawnHumanQuarterback(World) : nullptr;
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Human QB"), Controller) || !TestNotNull(TEXT("Menu"), Menu))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    const FPlayCallTuningRow& Tuning = PlayCall->GetTuning();
    PlayCall->OpenPlayCall(MakeSituation(1, 10, 25));
    TestTrue(TEXT("The call screen is open for the human"), Menu->IsPlayCallScreenOpen());

    PlayCall->SetPlayClock(Tuning.QuickCallPlayClockSeconds + 10.f);
    TestFalse(TEXT("Plenty of clock: no snap"), PlayCall->PollReadyToSnap(1.f));
    TestFalse(TEXT("...and no call is made for the human"), PlayCall->GetCall(true).IsSet());

    TArray<FPSTelemetryPlayCallEvent> Calls;
    const FDelegateHandle Handle = Bus->OnPlayCallMC.AddLambda([&Calls](const FPSTelemetryPlayCallEvent& Event) { Calls.Add(Event); });

    PlayCall->SetPlayClock(Tuning.QuickCallPlayClockSeconds);
    const TArray<FPSPlaySuggestion> Ranked = PlayCall->RankPlays(true);
    PlayCall->PollReadyToSnap(0.f);
    TestTrue(TEXT("At the quick-call mark the offense is called for the human"), PlayCall->GetCall(true).Caller == EPSPlayCaller::QuickCall);
    TestTrue(TEXT("...with the top suggestion"), Ranked.Num() > 0 && PlayCall->GetCall(true).PlayId == Ranked[0].PlayId);
    TestTrue(TEXT("...announced as not the human's own call"), Calls.ContainsByPredicate([](const FPSTelemetryPlayCallEvent& Event) { return Event.bOffense && !Event.bHumanCall; }));
    TestFalse(TEXT("The call screen closes"), Menu->IsMenuOpen());

    TestFalse(TEXT("A quick-call needs no hike, but waits out the snap delay"), PlayCall->PollReadyToSnap(0.f));
    TestTrue(TEXT("...then snaps before the clock runs out"), PlayCall->PollReadyToSnap(Tuning.CpuSnapDelaySeconds));

    Bus->OnPlayCallMC.Remove(Handle);
    Controller->GetPlayCallComponent()->UnbindFromPlayCall();
    Controller->ReleaseControl();
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Recent calls and the tendency readout
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSRecentCallsTest,
    "PlaySports.PlayCall.RecentCallsAndTendencies",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRecentCallsTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayCallAssistTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    APSPlayerController* Controller = World ? SpawnHumanQuarterback(World) : nullptr;
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Human QB"), Controller) || !TestNotNull(TEXT("Menu"), Menu))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    TestTrue(TEXT("No history, no readout"), PlayCall->DescribeTendencies(true).IsEmpty());

    RunHumanCall(PlayCall, Bus, TEXT("Offense_InsideZone"));
    RunHumanCall(PlayCall, Bus, TEXT("Offense_SlantFlat"));
    RunHumanCall(PlayCall, Bus, TEXT("Offense_InsideZone"));

    // A down the CPU calls for the human (quick-call) is not "what you've been calling".
    PlayCall->OpenPlayCall(MakeSituation(1, 10, 25));
    PlayCall->SetPlayClock(0.f);
    PlayCall->PollReadyToSnap(0.f);
    Bus->PublishSnap(FPSTelemetrySnapEvent());
    PlayCall->SetPlayClock(-1.f);

    TestEqual(TEXT("Three human calls were run"), PlayCall->GetCallHistory().Num(), 3);
    const TArray<FName> Recent = PlayCall->GetRecentCalls(true, 5);
    if (TestEqual(TEXT("Recent calls are distinct"), Recent.Num(), 2))
    {
        TestEqual(TEXT("Most recent first"), Recent[0], FName(TEXT("Offense_InsideZone")));
        TestEqual(TEXT("...then the one before"), Recent[1], FName(TEXT("Offense_SlantFlat")));
    }
    TestEqual(TEXT("The recent list respects its limit"), PlayCall->GetRecentCalls(true, 1).Num(), 1);
    TestEqual(TEXT("No defensive history"), PlayCall->GetRecentCalls(false, 5).Num(), 0);

    const FString Readout = PlayCall->DescribeTendencies(true);
    TestTrue(TEXT("The readout leads with the most-called category"), Readout.StartsWith(TEXT("Your calls: Run 67%")));
    TestTrue(TEXT("...and includes the rest"), Readout.Contains(TEXT("Short pass 33%")));

    // On the call screen: situation and readout in the body; suggestion, recent, formations.
    PlayCall->OpenPlayCall(MakeSituation(2, 4, 60));
    TestTrue(TEXT("The call screen opens"), Menu->IsPlayCallScreenOpen());
    const FPSMenuScreenDef Screen = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    TestTrue(TEXT("The body states the situation"), Screen.Body.Contains(TEXT("2nd & 4 at opp 40")));
    TestTrue(TEXT("...and the player's tendencies"), Screen.Body.Contains(TEXT("Your calls:")));
    if (TestTrue(TEXT("Suggestion, recent and formations are offered"), Screen.Options.Num() == 2 + PlayCall->GetFormations(true).Num()))
    {
        TestEqual(TEXT("The suggestion comes first"), Screen.Options[0].OptionId, FName(TEXT("Suggested")));
        TestEqual(TEXT("Then the recent plays"), Screen.Options[1].OptionId, FName(TEXT("Recent")));
    }

    Menu->ChooseOption(TEXT("Recent"));
    TestTrue(TEXT("Recent opens its own play-call screen"), Menu->IsPlayCallScreenOpen());
    const FPSMenuScreenDef RecentScreen = Menu->GetPresentedScreen(Menu->GetTopScreenId());
    if (TestEqual(TEXT("It lists the recent calls"), RecentScreen.Options.Num(), 2))
    {
        TestEqual(TEXT("...most recent first"), RecentScreen.Options[0].Payload, FName(TEXT("Offense_InsideZone")));
    }
    Menu->ChooseOption(TEXT("Offense_SlantFlat"));
    TestFalse(TEXT("Calling from the recent list closes the screens"), Menu->IsMenuOpen());
    TestTrue(TEXT("...and calls the play as the human's"),
        PlayCall->GetCall(true).Caller == EPSPlayCaller::Human && PlayCall->GetCall(true).PlayId == FName(TEXT("Offense_SlantFlat")));

    Controller->GetPlayCallComponent()->UnbindFromPlayCall();
    Controller->ReleaseControl();
    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
