// PSPlayerLevelingTests.cpp - Epic 141: player leveling/XP progression
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSPlayerLeveling.h"
#include "PSRoster.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- ComputeXpForPlay awards base XP, plus a touchdown bonus when scored
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPlayerLevelingComputeXpTest,
    "PlaySports.Leveling.ComputeXpForPlay",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlayerLevelingComputeXpTest::RunTest(const FString& Parameters)
{
    UPSPlayerLeveling* Leveling = NewObject<UPSPlayerLeveling>();
    FPSLevelingTuning Tuning;

    const float BaseXp = Leveling->ComputeXpForPlay(false, Tuning);
    TestEqual(TEXT("Base XP matches XpPerPlaySurvived"), BaseXp, Tuning.XpPerPlaySurvived);

    const float TouchdownXp = Leveling->ComputeXpForPlay(true, Tuning);
    TestEqual(TEXT("Touchdown Xp includes the bonus"), TouchdownXp, Tuning.XpPerPlaySurvived + Tuning.XpBonusForTouchdown);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Crossing the XP threshold levels up and grows the player's attributes
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPlayerLevelingLevelUpGrowthTest,
    "PlaySports.Leveling.LevelUpGrowsAttributes",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlayerLevelingLevelUpGrowthTest::RunTest(const FString& Parameters)
{
    UPSRoster* Roster = NewObject<UPSRoster>();
    UPSPlayerLeveling* Leveling = NewObject<UPSPlayerLeveling>();

    const FName PlayerId(TEXT("WR_Leveling"));
    FPlayerAttributes Player;
    Player.PlayerId = PlayerId;
    Player.Role = EPlayerRole::WideReceiver;
    Player.Speed = 70.f;
    Player.Agility = 70.f;

    TArray<FPlayerAttributes> Roster53;
    Roster53.Add(Player);
    Roster->InitializeRoster(Roster53);

    FPSLevelingTuning Tuning;
    Tuning.XpPerLevel = 100.f;
    Tuning.AttributeGrowthPerLevel = 2.f;

    // Not enough XP to level up yet.
    bool bLeveledUp = Leveling->AwardXpForPlay(Roster, PlayerId, 60.f, Tuning);
    TestFalse(TEXT("60 XP against a 100 XP threshold does not level up"), bLeveledUp);

    // Crosses the threshold now (60 + 60 = 120 >= 100).
    bLeveledUp = Leveling->AwardXpForPlay(Roster, PlayerId, 60.f, Tuning);
    TestTrue(TEXT("Crossing the XP threshold levels up"), bLeveledUp);

    FPSPlayerLiveState State;
    TestTrue(TEXT("Live state exists after leveling"), Roster->FindLiveState(PlayerId, State));
    TestEqual(TEXT("Level incremented to 2"), State.Level, 2);
    TestEqual(TEXT("Remaining XP carries over past the threshold"), State.CurrentXp, 20.f);

    FPlayerAttributes UpdatedPlayer;
    TestTrue(TEXT("Player found in roster after growth"), Roster->FindPlayerById(PlayerId, UpdatedPlayer));
    TestTrue(TEXT("Skill player's Speed grew on level-up"), UpdatedPlayer.Speed > 70.f);
    TestTrue(TEXT("Skill player's Agility grew on level-up"), UpdatedPlayer.Agility > 70.f);

    return true;
}

#endif
