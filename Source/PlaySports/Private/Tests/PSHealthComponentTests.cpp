// PSHealthComponentTests.cpp - Epic 139: hitpoint tracking
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSHealthComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Initialize sets full hitpoints and clears the downed flag
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHealthComponentInitializeTest,
    "PlaySports.Combat.HealthComponentInitialize",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHealthComponentInitializeTest::RunTest(const FString& Parameters)
{
    UPSHealthComponent* Health = NewObject<UPSHealthComponent>();
    Health->Initialize(80.f);

    TestEqual(TEXT("MaxHitPoints matches Initialize argument"), Health->GetMaxHitPoints(), 80.f);
    TestEqual(TEXT("CurrentHitPoints starts full"), Health->GetCurrentHitPoints(), 80.f);
    TestFalse(TEXT("Not downed after Initialize"), Health->IsDowned());

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- ApplyDamage reduces hitpoints and only reports downed at 0
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHealthComponentApplyDamageTest,
    "PlaySports.Combat.HealthComponentApplyDamage",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHealthComponentApplyDamageTest::RunTest(const FString& Parameters)
{
    UPSHealthComponent* Health = NewObject<UPSHealthComponent>();
    Health->Initialize(50.f);

    const bool bDownedAfterFirstHit = Health->ApplyDamage(20.f);
    TestFalse(TEXT("Surviving a partial hit is not downed"), bDownedAfterFirstHit);
    TestEqual(TEXT("Hitpoints reduced by the damage amount"), Health->GetCurrentHitPoints(), 30.f);

    const bool bDownedAfterLethalHit = Health->ApplyDamage(40.f);
    TestTrue(TEXT("A lethal hit reports downed"), bDownedAfterLethalHit);
    TestTrue(TEXT("IsDowned reflects the lethal hit"), Health->IsDowned());
    TestEqual(TEXT("Hitpoints never go negative"), Health->GetCurrentHitPoints(), 0.f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Kill forces downed regardless of remaining hitpoints; Respawn heals fully
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHealthComponentKillRespawnTest,
    "PlaySports.Combat.HealthComponentKillRespawn",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHealthComponentKillRespawnTest::RunTest(const FString& Parameters)
{
    UPSHealthComponent* Health = NewObject<UPSHealthComponent>();
    Health->Initialize(100.f);

    Health->Kill();
    TestTrue(TEXT("Kill forces downed"), Health->IsDowned());
    TestEqual(TEXT("Kill zeroes hitpoints"), Health->GetCurrentHitPoints(), 0.f);

    Health->Respawn();
    TestFalse(TEXT("Respawn clears downed"), Health->IsDowned());
    TestEqual(TEXT("Respawn restores full hitpoints"), Health->GetCurrentHitPoints(), 100.f);

    return true;
}

#endif
