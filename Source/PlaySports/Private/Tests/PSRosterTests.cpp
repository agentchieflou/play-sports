#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSRoster.h"
#include "PSPlayerProgression.h"
#include "PSInjuryModel.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Depth chart ordering and fatigue-driven substitution
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRosterDepthChartTest,
    "PlaySports.Roster.DepthChartAndSubstitution",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRosterDepthChartTest::RunTest(const FString& Parameters)
{
    UPSRoster* Roster = NewObject<UPSRoster>();

    TArray<FPlayerAttributes> Players;
    FPlayerAttributes Starter;
    Starter.PlayerId = FName("QB_Starter");
    Starter.Role = EPlayerRole::Quarterback;
    Players.Add(Starter);

    FPlayerAttributes Backup;
    Backup.PlayerId = FName("QB_Backup");
    Backup.Role = EPlayerRole::Quarterback;
    Players.Add(Backup);

    Roster->InitializeRoster(Players);
    Roster->BuildDefaultDepthChart();

    TestEqual(TEXT("Starter is first player added at the role"), Roster->GetStarterId(EPlayerRole::Quarterback), FName("QB_Starter"));
    TestEqual(TEXT("Next backup for starter is QB_Backup"), Roster->GetNextBackup(FName("QB_Starter")), FName("QB_Backup"));
    TestEqual(TEXT("No backup beyond the last depth chart entry"), Roster->GetNextBackup(FName("QB_Backup")), NAME_None);

    TMap<FName, float> FatigueRatios;
    FatigueRatios.Add(FName("QB_Starter"), 0.15f);
    const TMap<FName, FName> Subs = Roster->EvaluateFatigueSubstitutions(FatigueRatios, 0.3f);

    TestTrue(TEXT("Fatigued starter is flagged for substitution"), Subs.Contains(FName("QB_Starter")));
    if (const FName* SubIn = Subs.Find(FName("QB_Starter")))
    {
        TestEqual(TEXT("Substitution brings in the backup"), *SubIn, FName("QB_Backup"));
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Progression grows young players and declines aging players
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPlayerProgressionTest,
    "PlaySports.Roster.Progression",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlayerProgressionTest::RunTest(const FString& Parameters)
{
    UPSPlayerProgression* Progression = NewObject<UPSPlayerProgression>();

    FPSProgressionTuning Tuning;

    FPlayerAttributes YoungPlayer;
    YoungPlayer.Speed = 70.f;
    Progression->ApplyOffseasonProgression(YoungPlayer, 22, 0.8f, Tuning);
    TestTrue(TEXT("Young player (age 22) grows Speed"), YoungPlayer.Speed > 70.f);

    FPlayerAttributes AgingPlayer;
    AgingPlayer.Speed = 70.f;
    Progression->ApplyOffseasonProgression(AgingPlayer, 34, 0.8f, Tuning);
    TestTrue(TEXT("Aging player (age 34) declines in Speed"), AgingPlayer.Speed < 70.f);

    FPlayerAttributes PeakPlayer;
    PeakPlayer.Speed = 70.f;
    Progression->ApplyOffseasonProgression(PeakPlayer, 27, 0.8f, Tuning);
    TestEqual(TEXT("Peak-age player (27) holds steady"), PeakPlayer.Speed, 70.f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Injury roll probability scales with fatigue
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FInjuryModelFatigueTest,
    "PlaySports.Roster.InjuryFatigueScaling",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInjuryModelFatigueTest::RunTest(const FString& Parameters)
{
    FPSInjuryTuning Tuning;
    Tuning.BaseInjuryChance = 0.5f; // exaggerated for deterministic test observability
    Tuning.MaxFatigueMultiplier = 2.f;

    UPSInjuryModel* FreshModel = NewObject<UPSInjuryModel>();
    FreshModel->SeedDeterminism(7);
    int32 FreshInjuries = 0;
    for (int32 i = 0; i < 100; ++i)
    {
        if (FreshModel->RollForInjury(1.f, Tuning).bInjured)
        {
            ++FreshInjuries;
        }
    }

    UPSInjuryModel* FatiguedModel = NewObject<UPSInjuryModel>();
    FatiguedModel->SeedDeterminism(7);
    int32 FatiguedInjuries = 0;
    for (int32 i = 0; i < 100; ++i)
    {
        if (FatiguedModel->RollForInjury(0.f, Tuning).bInjured)
        {
            ++FatiguedInjuries;
        }
    }

    TestTrue(TEXT("Fully fatigued players are injured more often than fresh players over 100 rolls"), FatiguedInjuries > FreshInjuries);

    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Downed ball carrier sits out exactly the next play (Epic 139)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRosterDownedCarrierSitOutTest,
    "PlaySports.Roster.DownedCarrierSitsOutOnePlay",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRosterDownedCarrierSitOutTest::RunTest(const FString& Parameters)
{
    UPSRoster* Roster = NewObject<UPSRoster>();
    const FName CarrierId(TEXT("RB_Carrier"));
    const int32 DownedOnPlayIndex = 5;

    TestTrue(TEXT("Player is available before being downed"), Roster->IsAvailableForPlay(CarrierId, DownedOnPlayIndex));

    Roster->MarkDownedForNextPlay(CarrierId, DownedOnPlayIndex);
    TestFalse(TEXT("Downed carrier unavailable for the very next play"), Roster->IsAvailableForPlay(CarrierId, DownedOnPlayIndex + 1));
    TestTrue(TEXT("Downed carrier available again the play after that"), Roster->IsAvailableForPlay(CarrierId, DownedOnPlayIndex + 2));

    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Non-carrier death respawns fully for the very next play, no sit-out (Epic 139)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRosterNonCarrierRespawnTest,
    "PlaySports.Roster.NonCarrierRespawnsNextPlay",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRosterNonCarrierRespawnTest::RunTest(const FString& Parameters)
{
    UPSRoster* Roster = NewObject<UPSRoster>();
    const FName LinemanId(TEXT("OL_Downed"));
    const int32 CurrentPlayIndex = 3;

    Roster->MarkDownedForCurrentPlayOnly(LinemanId);
    TestTrue(TEXT("A non-carrier death has no sit-out for the next play"), Roster->IsAvailableForPlay(LinemanId, CurrentPlayIndex + 1));

    Roster->RespawnForNewPlay(LinemanId, 100.f);
    FPSPlayerLiveState State;
    TestTrue(TEXT("Live state exists after respawn"), Roster->FindLiveState(LinemanId, State));
    TestFalse(TEXT("Respawned player is no longer downed"), State.bIsDowned);
    TestEqual(TEXT("Respawned player is at full hitpoints"), State.CurrentHitPoints, 100.f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- XP accrual and level-up are tracked per player (Epic 141)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRosterXpLevelingTest,
    "PlaySports.Roster.XpLeveling",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRosterXpLevelingTest::RunTest(const FString& Parameters)
{
    UPSRoster* Roster = NewObject<UPSRoster>();
    const FName PlayerId(TEXT("WR_Leveling"));

    Roster->AwardXp(PlayerId, 50.f);
    Roster->AwardXp(PlayerId, 25.f);

    FPSPlayerLiveState State;
    TestTrue(TEXT("Live state exists after XP award"), Roster->FindLiveState(PlayerId, State));
    TestEqual(TEXT("XP accumulates across awards"), State.CurrentXp, 75.f);
    TestEqual(TEXT("Level starts at 1"), State.Level, 1);

    Roster->SetLevel(PlayerId, 2, 5.f);
    TestTrue(TEXT("Live state exists after level-up"), Roster->FindLiveState(PlayerId, State));
    TestEqual(TEXT("Level increments"), State.Level, 2);
    TestEqual(TEXT("Remaining XP carries over past the level threshold"), State.CurrentXp, 5.f);

    return true;
}

#endif
