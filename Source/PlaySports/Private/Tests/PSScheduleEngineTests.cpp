#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSScheduleEngine.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSScheduleEngineGeneratesWeeksTest,
    "PlaySports.ScheduleEngine.GeneratesConfiguredWeeks",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSScheduleEngineGeneratesWeeksTest::RunTest(const FString& Parameters)
{
    UPScheduleEngine* Engine = NewObject<UPScheduleEngine>();
    const FDateTime SeasonStart(2026, 9, 10);
    TArray<int32> ByeWeeks;
    ByeWeeks.Add(5);
    ByeWeeks.Add(9);

    TArray<FSeasonWeek> Schedule = Engine->GenerateSeasonSchedule(SeasonStart, 18, ByeWeeks);

    TestEqual(TEXT("Schedule has the configured number of weeks"), Schedule.Num(), 18);
    if (Schedule.Num() == 18)
    {
        TestEqual(TEXT("Week 1 starts at season start"), Schedule[0].StartDate, SeasonStart);
        TestEqual(TEXT("Week 2 starts seven days later"), Schedule[1].StartDate, SeasonStart + FTimespan::FromDays(7));
        TestTrue(TEXT("Week 5 is a bye week"), Schedule[4].bByeWeek);
        TestTrue(TEXT("Week 9 is a bye week"), Schedule[8].bByeWeek);
        TestFalse(TEXT("Week 6 is not a bye week"), Schedule[5].bByeWeek);
        TestEqual(TEXT("Week numbers are sequential from 1"), Schedule[17].WeekNumber, 18);
    }
    return true;
}

#endif
