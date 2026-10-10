// PSPureLogicSpec.cpp -- Epic 24: automation specs for the pure logic
//
// Unit-level specs (BEGIN_DEFINE_SPEC) for the logic that needs no world:
//   - drive state transitions in UPSPlaySimulation: down and distance, first downs,
//     incompletions, the fourth-down decision, turnover on downs, touchdowns, safeties, and the
//     drive summary;
//   - UPSScheduleEngine's season calendar;
//   - UPSDataIngestion's schema validation of player and team files.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "PSDataIngestion.h"
#include "PSPlaySimulation.h"
#include "PSRulesConfig.h"
#include "PSScheduleEngine.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Drive state transitions
// ---------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FPSDriveStateSpec, "PlaySports.Spec.DriveState",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

    UPSPlaySimulation* Sim = nullptr;

    /** A play tackled for Yards, then the whistle: the simulation sets up the next snap. */
    void RunPlay(int32 Yards);

END_DEFINE_SPEC(FPSDriveStateSpec)

void FPSDriveStateSpec::RunPlay(int32 Yards)
{
    Sim->RecordTackle(Yards);
    Sim->EndPlayAndPrepareNext();
}

void FPSDriveStateSpec::Define()
{
    BeforeEach([this]()
    {
        // Only the extra point rolls in these transitions; seed it so every run is the same.
        FMath::RandInit(24);
        Sim = NewObject<UPSPlaySimulation>();
        Sim->bQuickSimMode = true;
        Sim->InitializePlay(TArray<FPlayerAttributes>(), TArray<FPlayerAttributes>());
    });

    AfterEach([this]()
    {
        Sim = nullptr;
        FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
    });

    Describe("A new drive", [this]()
    {
        It("starts at 1st and 10 on the 20 with the home side's ball", [this]()
        {
            const FPlayState State = Sim->GetPlayState();
            TestEqual(TEXT("Down"), State.Down, 1);
            TestEqual(TEXT("Distance"), State.Distance, 10);
            TestEqual(TEXT("Yard line"), State.YardLine, 20);
            TestEqual(TEXT("Line to gain"), State.YardLineToGain, 30);
            TestTrue(TEXT("Home ball"), State.bHomeHasPossession);
        });
    });

    Describe("A tackle short of the line to gain", [this]()
    {
        It("advances the down, shortens the distance and keeps the clock running", [this]()
        {
            RunPlay(4);
            const FPlayState State = Sim->GetPlayState();
            TestEqual(TEXT("Down"), State.Down, 2);
            TestEqual(TEXT("Distance"), State.Distance, 6);
            TestEqual(TEXT("Yard line"), State.YardLine, 24);
            TestTrue(TEXT("Clock running"), State.bIsClockRunning);
            TestEqual(TEXT("Next phase"), State.Phase, EPlayPhase::PreSnap);
        });

        It("adds the play to the drive summary", [this]()
        {
            RunPlay(4);
            RunPlay(3);
            TestEqual(TEXT("Plays"), Sim->GetDriveSummary().Plays, 2);
            TestEqual(TEXT("Yards"), Sim->GetDriveSummary().Yards, 7);
        });
    });

    Describe("A gain past the line to gain", [this]()
    {
        It("is a first down with a new line to gain", [this]()
        {
            RunPlay(4);
            RunPlay(12);
            const FPlayState State = Sim->GetPlayState();
            TestEqual(TEXT("Down"), State.Down, 1);
            TestEqual(TEXT("Distance"), State.Distance, 10);
            TestEqual(TEXT("Yard line"), State.YardLine, 36);
            TestEqual(TEXT("Line to gain"), State.YardLineToGain, 46);
        });
    });

    Describe("An incompletion", [this]()
    {
        It("costs a down, gains nothing and stops the clock", [this]()
        {
            Sim->SetPlayPhase(EPlayPhase::Scoring);
            Sim->EndPlayAndPrepareNext();
            const FPlayState State = Sim->GetPlayState();
            TestEqual(TEXT("Down"), State.Down, 2);
            TestEqual(TEXT("Distance"), State.Distance, 10);
            TestEqual(TEXT("Yard line"), State.YardLine, 20);
            TestFalse(TEXT("Clock stopped"), State.bIsClockRunning);
        });
    });

    Describe("Third down stopped short", [this]()
    {
        It("lines up to punt from the offense's own end", [this]()
        {
            RunPlay(1);
            RunPlay(1);
            RunPlay(1);
            const FPlayState State = Sim->GetPlayState();
            TestEqual(TEXT("Down"), State.Down, 4);
            TestEqual(TEXT("Phase"), State.Phase, EPlayPhase::Punt);
            TestTrue(TEXT("Still the home side's ball"), State.bHomeHasPossession);
        });

        It("lines up a field goal from the opponent's 40 or closer", [this]()
        {
            RunPlay(45);
            RunPlay(1);
            RunPlay(1);
            RunPlay(1);
            const FPlayState State = Sim->GetPlayState();
            TestEqual(TEXT("Yard line"), State.YardLine, 68);
            TestEqual(TEXT("Down"), State.Down, 4);
            TestEqual(TEXT("Phase"), State.Phase, EPlayPhase::FieldGoal);
        });

        It("goes for it when the rules forbid punting, and turns it over on downs if stopped", [this]()
        {
            Sim->RulesConfig = NewObject<UPSRulesConfig>();
            Sim->RulesConfig->bAllowPunting = false;
            RunPlay(1);
            RunPlay(1);
            RunPlay(1);
            TestEqual(TEXT("Fourth down is a snap"), Sim->GetPlayState().Phase, EPlayPhase::PreSnap);
            TestEqual(TEXT("Fourth down"), Sim->GetPlayState().Down, 4);

            RunPlay(1);
            const FPlayState State = Sim->GetPlayState();
            TestFalse(TEXT("The ball goes over"), State.bHomeHasPossession);
            TestEqual(TEXT("At the spot, seen from the other end"), State.YardLine, 76);
            TestEqual(TEXT("1st down"), State.Down, 1);
            TestEqual(TEXT("and 10"), State.Distance, 10);
            TestEqual(TEXT("A new drive"), Sim->GetDriveSummary().Plays, 0);
        });
    });

    Describe("A touchdown", [this]()
    {
        It("scores six plus the extra point and kicks off", [this]()
        {
            Sim->RecordTouchdown();
            Sim->EndPlayAndPrepareNext();
            const FPlayState State = Sim->GetPlayState();
            TestTrue(TEXT("Six, or seven with the kick"), State.HomeScore == 6 || State.HomeScore == 7);
            TestEqual(TEXT("Nothing for the defense"), State.AwayScore, 0);
            TestEqual(TEXT("Kickoff next"), State.Phase, EPlayPhase::Kickoff);
        });
    });

    Describe("A tackle behind the offense's own goal line", [this]()
    {
        It("is a safety: two points to the defense and a kickoff", [this]()
        {
            RunPlay(-25);
            const FPlayState State = Sim->GetPlayState();
            TestEqual(TEXT("Two points to the defense"), State.AwayScore, 2);
            TestEqual(TEXT("None to the offense"), State.HomeScore, 0);
            TestEqual(TEXT("Kickoff next"), State.Phase, EPlayPhase::Kickoff);
        });
    });
}

// ---------------------------------------------------------------------------
// Season calendar
// ---------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FPSScheduleEngineSpec, "PlaySports.Spec.ScheduleEngine",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FPSScheduleEngineSpec)

void FPSScheduleEngineSpec::Define()
{
    Describe("GenerateSeasonSchedule", [this]()
    {
        It("numbers the weeks from 1, each a week after the last", [this]()
        {
            const FDateTime Start(2026, 9, 10);
            const TArray<FSeasonWeek> Weeks = NewObject<UPSScheduleEngine>()->GenerateSeasonSchedule(Start, 6, TArray<int32>());
            if (TestEqual(TEXT("Six weeks"), Weeks.Num(), 6))
            {
                for (int32 Index = 0; Index < Weeks.Num(); ++Index)
                {
                    TestEqual(*FString::Printf(TEXT("Week %d's number"), Index + 1), Weeks[Index].WeekNumber, Index + 1);
                    TestTrue(*FString::Printf(TEXT("Week %d starts %d days in"), Index + 1, Index * 7), Weeks[Index].StartDate == Start + FTimespan::FromDays(Index * 7));
                }
            }
        });

        It("marks exactly the listed bye weeks inside the season", [this]()
        {
            const TArray<FSeasonWeek> Weeks = NewObject<UPSScheduleEngine>()->GenerateSeasonSchedule(FDateTime(2026, 9, 10), 6, { 2, 6, 9 });
            TArray<int32> Byes;
            for (const FSeasonWeek& Week : Weeks)
            {
                if (Week.bByeWeek)
                {
                    Byes.Add(Week.WeekNumber);
                }
            }
            TestEqual(TEXT("Two byes in a six-week season"), Byes.Num(), 2);
            TestTrue(TEXT("Weeks 2 and 6"), Byes.Contains(2) && Byes.Contains(6));
        });

        It("is empty for a season of no weeks", [this]()
        {
            TestEqual(TEXT("No weeks"), NewObject<UPSScheduleEngine>()->GenerateSeasonSchedule(FDateTime(2026, 9, 10), 0, TArray<int32>()).Num(), 0);
        });
    });
}

// ---------------------------------------------------------------------------
// Ingestion schema validation
// ---------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FPSIngestionValidationSpec, "PlaySports.Spec.IngestionValidation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

    FString Dir;
    UPSDataIngestion* Ingestion = nullptr;

    /** Writes Json to Name in the spec's scratch directory and returns its path. */
    FString WriteJson(const TCHAR* Name, const FString& Json) const;

    bool AnyErrorContains(const TArray<FString>& Errors, const TCHAR* Text) const;

END_DEFINE_SPEC(FPSIngestionValidationSpec)

FString FPSIngestionValidationSpec::WriteJson(const TCHAR* Name, const FString& Json) const
{
    const FString Path = Dir / Name;
    FFileHelper::SaveStringToFile(Json, *Path);
    return Path;
}

bool FPSIngestionValidationSpec::AnyErrorContains(const TArray<FString>& Errors, const TCHAR* Text) const
{
    return Errors.ContainsByPredicate([Text](const FString& Error) { return Error.Contains(Text); });
}

void FPSIngestionValidationSpec::Define()
{
    BeforeEach([this]()
    {
        Dir = FPaths::ProjectSavedDir() / TEXT("Tests") / TEXT("IngestionSpec");
        IFileManager::Get().MakeDirectory(*Dir, true);
        Ingestion = NewObject<UPSDataIngestion>();
    });

    AfterEach([this]()
    {
        IFileManager::Get().DeleteDirectory(*Dir, false, true);
        Ingestion = nullptr;
    });

    Describe("ValidatePlayersJson", [this]()
    {
        It("accepts a sound roster", [this]()
        {
            TArray<FString> Errors;
            const FString Path = WriteJson(TEXT("good.json"), TEXT("{ \"Players\": [ { \"PlayerId\": \"QB_1\", \"Role\": \"Quarterback\", \"Speed\": 80 } ] }"));
            TestTrue(TEXT("Valid"), Ingestion->ValidatePlayersJson(Path, Errors));
            TestEqual(TEXT("No errors"), Errors.Num(), 0);
        });

        It("points at each bad row", [this]()
        {
            TArray<FString> Errors;
            const FString Path = WriteJson(TEXT("bad.json"), TEXT("{ \"Players\": [ { \"Role\": \"Quarterback\" }, { \"PlayerId\": \"K_1\", \"Role\": \"Kicker\" }, { \"PlayerId\": \"WR_1\", \"Role\": \"WideReceiver\", \"Speed\": -3 } ] }"));
            TestFalse(TEXT("Invalid"), Ingestion->ValidatePlayersJson(Path, Errors));
            TestTrue(TEXT("Row 0 has no PlayerId"), AnyErrorContains(Errors, TEXT("Row 0: missing or empty \"PlayerId\"")));
            TestTrue(TEXT("Row 1's role is unknown"), AnyErrorContains(Errors, TEXT("Row 1: \"Role\" value \"Kicker\"")));
            TestTrue(TEXT("Row 2's speed is negative"), AnyErrorContains(Errors, TEXT("Row 2: \"Speed\" is negative")));
        });

        It("rejects a file that is not a roster", [this]()
        {
            TArray<FString> Errors;
            TestFalse(TEXT("Not JSON"), Ingestion->ValidatePlayersJson(WriteJson(TEXT("notjson.json"), TEXT("Players: none")), Errors));
            TestTrue(TEXT("Says so"), AnyErrorContains(Errors, TEXT("not valid JSON")));
            TestFalse(TEXT("No Players array"), Ingestion->ValidatePlayersJson(WriteJson(TEXT("noplayers.json"), TEXT("{ \"Teams\": [] }")), Errors));
            TestTrue(TEXT("Says so"), AnyErrorContains(Errors, TEXT("Missing top-level \"Players\" array")));
            TestFalse(TEXT("Missing file"), Ingestion->ValidatePlayersJson(Dir / TEXT("missing.json"), Errors));
            TestTrue(TEXT("Says so"), AnyErrorContains(Errors, TEXT("Could not read file")));
        });
    });

    Describe("ValidateTeamsJson", [this]()
    {
        It("catches a duplicate team and a team with no roster", [this]()
        {
            TArray<FString> Errors;
            const FString Path = WriteJson(TEXT("teams.json"), TEXT("{ \"Teams\": [ { \"TeamId\": \"Hawks\", \"RosterDataTablePath\": \"Data/rosters/a.json\" }, { \"TeamId\": \"Hawks\", \"RosterDataTablePath\": \"Data/rosters/b.json\" }, { \"TeamId\": \"Bears\" } ] }"));
            TestFalse(TEXT("Invalid"), Ingestion->ValidateTeamsJson(Path, Errors));
            TestTrue(TEXT("Row 1 repeats a TeamId"), AnyErrorContains(Errors, TEXT("Row 1: duplicate \"TeamId\" \"Hawks\"")));
            TestTrue(TEXT("Row 2 has no roster"), AnyErrorContains(Errors, TEXT("Row 2: missing or empty \"RosterDataTablePath\"")));
        });
    });
}

#endif
