#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSReplayFormat.h"

#if WITH_DEV_AUTOMATION_TESTS

static FPSReplayRecording BuildSampleRecording()
{
    FPlayState PlayState;
    PlayState.Phase = EPlayPhase::BallCarrierMovement;
    PlayState.Down = 3;
    PlayState.Distance = 7;
    PlayState.YardLine = 45;
    PlayState.YardLineToGain = 52;
    PlayState.Quarter = 2;
    PlayState.GameClockSeconds = 425.f;
    PlayState.PlayClockSeconds = 12.f;
    PlayState.bHomeHasPossession = false;
    PlayState.HomeScore = 14;
    PlayState.AwayScore = 10;
    PlayState.bIsClockRunning = true;

    FPlayerAttributes Quarterback;
    Quarterback.PlayerId = TEXT("QB_001");
    Quarterback.DisplayName = TEXT("Test Quarterback");
    Quarterback.Role = EPlayerRole::Quarterback;
    Quarterback.WeightKg = 102.f;
    Quarterback.Speed = 78.f;
    Quarterback.Awareness = 91.f;

    FPlayerAttributes Defender;
    Defender.PlayerId = TEXT("DB_001");
    Defender.DisplayName = TEXT("Test Defensive Back");
    Defender.Role = EPlayerRole::DefensiveBack;
    Defender.WeightKg = 88.f;
    Defender.Speed = 93.f;
    Defender.Agility = 90.f;

    TArray<FPlayerAttributes> Offense;
    Offense.Add(Quarterback);
    TArray<FPlayerAttributes> Defense;
    Defense.Add(Defender);

    FPSReplayRecording Recording = UPSReplayFormat::MakeRecording(PlayState, Offense, Defense);
    Recording.Header.GameBuildVersion = TEXT("test-build");
    Recording.Header.RecordedAtUtc = FDateTime(2026, 7, 19, 12, 0, 0);
    Recording.Header.RandomSeed = 1234;
    Recording.Header.FixedDeltaSeconds = 0.25f;

    FPSReplayEventRecord SnapEvent;
    SnapEvent.TickIndex = 3;
    SnapEvent.TimestampSeconds = 0.05f;
    SnapEvent.EventType = TEXT("Snap");
    SnapEvent.PayloadJson = TEXT("{\"YardLine\":45,\"Down\":3}");
    Recording.Events.Add(SnapEvent);

    FPSReplayEventRecord TackleEvent;
    TackleEvent.TickIndex = 240;
    TackleEvent.TimestampSeconds = 4.f;
    TackleEvent.EventType = TEXT("Tackle");
    TackleEvent.PayloadJson = TEXT("{\"YardsGained\":6}");
    Recording.Events.Add(TackleEvent);

    return Recording;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayFormatRoundTripTest,
    "PlaySports.ReplayFormat.RoundTripPreservesRecording",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayFormatRoundTripTest::RunTest(const FString& Parameters)
{
    FPSReplayRecording Original = BuildSampleRecording();
    TestEqual(TEXT("MakeRecording stamps the current format version"), Original.Header.FormatVersion, UPSReplayFormat::CurrentFormatVersion);

    FString Json = UPSReplayFormat::SerializeToJson(Original);
    TestFalse(TEXT("Serialization produces output"), Json.IsEmpty());

    FPSReplayRecording Loaded;
    TestTrue(TEXT("Deserialization succeeds"), UPSReplayFormat::DeserializeFromJson(Json, Loaded));

    TestEqual(TEXT("Format version survives"), Loaded.Header.FormatVersion, Original.Header.FormatVersion);
    TestEqual(TEXT("Build version survives"), Loaded.Header.GameBuildVersion, Original.Header.GameBuildVersion);
    TestEqual(TEXT("Recorded timestamp survives"), Loaded.Header.RecordedAtUtc, Original.Header.RecordedAtUtc);
    TestEqual(TEXT("Random seed survives"), Loaded.Header.RandomSeed, Original.Header.RandomSeed);
    TestEqual(TEXT("Fixed delta survives"), Loaded.Header.FixedDeltaSeconds, Original.Header.FixedDeltaSeconds);

    TestEqual(TEXT("Play phase survives"), Loaded.InitialState.PlayState.Phase, Original.InitialState.PlayState.Phase);
    TestEqual(TEXT("Down survives"), Loaded.InitialState.PlayState.Down, Original.InitialState.PlayState.Down);
    TestEqual(TEXT("Distance survives"), Loaded.InitialState.PlayState.Distance, Original.InitialState.PlayState.Distance);
    TestEqual(TEXT("Yard line survives"), Loaded.InitialState.PlayState.YardLine, Original.InitialState.PlayState.YardLine);
    TestEqual(TEXT("Quarter survives"), Loaded.InitialState.PlayState.Quarter, Original.InitialState.PlayState.Quarter);
    TestEqual(TEXT("Game clock survives"), Loaded.InitialState.PlayState.GameClockSeconds, Original.InitialState.PlayState.GameClockSeconds);
    TestEqual(TEXT("Possession flag survives"), Loaded.InitialState.PlayState.bHomeHasPossession, Original.InitialState.PlayState.bHomeHasPossession);
    TestEqual(TEXT("Home score survives"), Loaded.InitialState.PlayState.HomeScore, Original.InitialState.PlayState.HomeScore);
    TestEqual(TEXT("Away score survives"), Loaded.InitialState.PlayState.AwayScore, Original.InitialState.PlayState.AwayScore);

    TestEqual(TEXT("Offense roster count survives"), Loaded.InitialState.OffenseRoster.Num(), 1);
    TestEqual(TEXT("Defense roster count survives"), Loaded.InitialState.DefenseRoster.Num(), 1);
    if (Loaded.InitialState.OffenseRoster.Num() == 1 && Loaded.InitialState.DefenseRoster.Num() == 1)
    {
        TestEqual(TEXT("Offense player id survives"), Loaded.InitialState.OffenseRoster[0].PlayerId, Original.InitialState.OffenseRoster[0].PlayerId);
        TestEqual(TEXT("Offense role enum survives"), Loaded.InitialState.OffenseRoster[0].Role, Original.InitialState.OffenseRoster[0].Role);
        TestEqual(TEXT("Offense awareness survives"), Loaded.InitialState.OffenseRoster[0].Awareness, Original.InitialState.OffenseRoster[0].Awareness);
        TestEqual(TEXT("Defense display name survives"), Loaded.InitialState.DefenseRoster[0].DisplayName, Original.InitialState.DefenseRoster[0].DisplayName);
        TestEqual(TEXT("Defense role enum survives"), Loaded.InitialState.DefenseRoster[0].Role, Original.InitialState.DefenseRoster[0].Role);
    }

    TestEqual(TEXT("Event count survives"), Loaded.Events.Num(), 2);
    if (Loaded.Events.Num() == 2)
    {
        TestEqual(TEXT("Event order preserved: first event"), Loaded.Events[0].EventType, FString(TEXT("Snap")));
        TestEqual(TEXT("Event order preserved: second event"), Loaded.Events[1].EventType, FString(TEXT("Tackle")));
        TestEqual(TEXT("Tick index survives"), Loaded.Events[1].TickIndex, 240);
        TestEqual(TEXT("Event timestamp survives"), Loaded.Events[0].TimestampSeconds, 0.05f);
        TestEqual(TEXT("Nested payload JSON survives"), Loaded.Events[0].PayloadJson, Original.Events[0].PayloadJson);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayFormatRejectsNewerVersionTest,
    "PlaySports.ReplayFormat.RejectsNewerFormatVersion",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayFormatRejectsNewerVersionTest::RunTest(const FString& Parameters)
{
    FPSReplayRecording Recording = BuildSampleRecording();
    Recording.Header.FormatVersion = UPSReplayFormat::CurrentFormatVersion + 1;

    FString Json = UPSReplayFormat::SerializeToJson(Recording);
    FPSReplayRecording Loaded;
    TestFalse(TEXT("A newer format version is rejected"), UPSReplayFormat::DeserializeFromJson(Json, Loaded));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSReplayFormatRejectsInvalidDocumentsTest,
    "PlaySports.ReplayFormat.RejectsInvalidDocuments",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSReplayFormatRejectsInvalidDocumentsTest::RunTest(const FString& Parameters)
{
    FPSReplayRecording Loaded;

    TestFalse(TEXT("Malformed JSON is rejected"), UPSReplayFormat::DeserializeFromJson(TEXT("not a replay"), Loaded));

    // An empty-but-valid JSON object deserializes to defaults, whose header version is 0:
    // the unversioned gate must reject it rather than yield a blank recording.
    TestFalse(TEXT("Unversioned document is rejected"), UPSReplayFormat::DeserializeFromJson(TEXT("{}"), Loaded));

    FPSReplayRecording ZeroVersion = BuildSampleRecording();
    ZeroVersion.Header.FormatVersion = 0;
    FString Json = UPSReplayFormat::SerializeToJson(ZeroVersion);
    TestFalse(TEXT("Explicit version 0 is rejected"), UPSReplayFormat::DeserializeFromJson(Json, Loaded));

    return true;
}

#endif
