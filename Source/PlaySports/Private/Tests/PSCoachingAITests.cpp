#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSCoachingAI.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Situational weighting favors Run on short yardage
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCoachingAIShortYardageTest,
    "PlaySports.AI.CoachingAIShortYardagePrefersRun",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCoachingAIShortYardageTest::RunTest(const FString& Parameters)
{
    UPSCoachingAI* CoachingAI = NewObject<UPSCoachingAI>();
    CoachingAI->SeedDeterminism(42);

    FPSSituationContext Situation;
    Situation.Down = 2;
    Situation.Distance = 1;
    Situation.YardLine = 50;
    Situation.Quarter = 1;
    Situation.GameClockSeconds = 800.f;
    Situation.ScoreDifferential = 0;

    FPSTendencyProfile Tendency;
    Tendency.AggressionScore = 0.5f;

    TArray<FPSPlayDefinition> Candidates;
    FPSPlayDefinition RunPlay;
    RunPlay.PlayId = FName("RunPlay");
    RunPlay.bIsOffensivePlay = true;
    RunPlay.PlayCategory = TEXT("Run");
    Candidates.Add(RunPlay);

    FPSPlayDefinition DeepPassPlay;
    DeepPassPlay.PlayId = FName("DeepPassPlay");
    DeepPassPlay.bIsOffensivePlay = true;
    DeepPassPlay.PlayCategory = TEXT("DeepPass");
    Candidates.Add(DeepPassPlay);

    // Deterministic seed + heavily-favored Run weight should select RunPlay
    // consistently across repeated selections in short-yardage situations.
    int32 RunSelections = 0;
    for (int32 i = 0; i < 20; ++i)
    {
        const FName Selected = CoachingAI->SelectOffensivePlay(Situation, Tendency, Candidates);
        if (Selected == FName("RunPlay"))
        {
            ++RunSelections;
        }
    }

    TestTrue(TEXT("Run is selected significantly more often than deep pass on 2nd-and-1"), RunSelections > 12);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- 4th down and 2-point decisions follow the situational chart
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCoachingAIDecisionLogicTest,
    "PlaySports.AI.CoachingAIDecisionLogic",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCoachingAIDecisionLogicTest::RunTest(const FString& Parameters)
{
    UPSCoachingAI* CoachingAI = NewObject<UPSCoachingAI>();

    FPSTendencyProfile Tendency;
    Tendency.AggressionScore = 0.5f;

    FPSSituationContext ShortYardageAtMidfield;
    ShortYardageAtMidfield.Down = 4;
    ShortYardageAtMidfield.Distance = 1;
    ShortYardageAtMidfield.YardLine = 55;
    ShortYardageAtMidfield.Quarter = 2;
    ShortYardageAtMidfield.GameClockSeconds = 600.f;

    TestTrue(TEXT("Go for it on 4th-and-1 past midfield"), CoachingAI->ShouldGoForItOnFourthDown(ShortYardageAtMidfield, Tendency));

    FPSSituationContext LongYardageOwnTerritory;
    LongYardageOwnTerritory.Down = 4;
    LongYardageOwnTerritory.Distance = 8;
    LongYardageOwnTerritory.YardLine = 30;
    LongYardageOwnTerritory.Quarter = 1;
    LongYardageOwnTerritory.GameClockSeconds = 800.f;

    TestFalse(TEXT("Do not go for it on 4th-and-8 in own territory, 1st quarter"), CoachingAI->ShouldGoForItOnFourthDown(LongYardageOwnTerritory, Tendency));

    TestTrue(TEXT("Chart-mandated 2-point attempt when trailing by 5 after TD"), CoachingAI->ShouldAttemptTwoPointConversion(-5, 4, Tendency));
    TestFalse(TEXT("No 2-point attempt when trailing by 6 after TD (kick the tying/go-ahead XP path), moderate aggression"), CoachingAI->ShouldAttemptTwoPointConversion(-6, 1, Tendency));

    return true;
}

#endif
