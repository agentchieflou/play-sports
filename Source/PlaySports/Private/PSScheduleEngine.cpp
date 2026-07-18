#include "PSScheduleEngine.h"

TArray<FSeasonWeek> UPScheduleEngine::GenerateSeasonSchedule(const FDateTime& SeasonStart, int32 NumWeeks, const TArray<int32>& ByeWeekNumbers)
{
    TArray<FSeasonWeek> Schedule;
    for (int32 Week = 1; Week <= NumWeeks; ++Week)
    {
        FSeasonWeek SeasonWeek;
        SeasonWeek.WeekNumber = Week;
        SeasonWeek.StartDate = SeasonStart + FTimespan::FromDays((Week - 1) * 7);
        SeasonWeek.bByeWeek = ByeWeekNumbers.Contains(Week);
        Schedule.Add(SeasonWeek);
    }
    return Schedule;
}
