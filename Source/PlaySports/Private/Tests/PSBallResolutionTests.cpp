// PSBallResolutionTests.cpp -- Epic C4: Core Gameplay Test Retrofit
//
// Pure-function tests for the catch/interception/fumble-recovery formulas
// extracted from APSBall::OnBallOverlap. No World needed.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBallResolutionHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

static FPlayerAttributes MakeAttributes(float Agility, float Awareness)
{
    FPlayerAttributes Attr;
    Attr.Agility = Agility;
    Attr.Awareness = Awareness;
    return Attr;
}

// ---------------------------------------------------------------------------
// ComputeCatchChance: boundary attrs clamp into [CatchChanceMin, CatchChanceMax]
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSC4CatchChanceBoundsTest,
    "PlaySports.C4.BallResolution.CatchChanceBounds",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSC4CatchChanceBoundsTest::RunTest(const FString& Parameters)
{
    const FCatchTuningRow Tuning;

    // Deliberately out-of-range attributes so the formula's raw output falls
    // outside [Min, Max] and the clamp is what's actually under test -- at
    // realistic attributes (0-100) the base chance alone can already sit
    // inside bounds, so 0/100 don't necessarily exercise the clamp.
    const float LowChance = PSBallResolutionHelpers::ComputeCatchChance(MakeAttributes(-10000.f, -10000.f), Tuning);
    TestEqual(TEXT("Extreme-low attributes clamp to CatchChanceMin"), LowChance, Tuning.CatchChanceMin);

    const float HighChance = PSBallResolutionHelpers::ComputeCatchChance(MakeAttributes(10000.f, 10000.f), Tuning);
    TestEqual(TEXT("Extreme-high attributes clamp to CatchChanceMax"), HighChance, Tuning.CatchChanceMax);

    const float ZeroAttrChance = PSBallResolutionHelpers::ComputeCatchChance(MakeAttributes(0.f, 0.f), Tuning);
    TestEqual(TEXT("Zero-attribute catch chance is the unclamped base chance"), ZeroAttrChance, Tuning.CatchBaseChance);

    const float MidChance = PSBallResolutionHelpers::ComputeCatchChance(MakeAttributes(50.f, 50.f), Tuning);
    TestTrue(TEXT("Mid-attribute catch chance stays within bounds"), MidChance >= Tuning.CatchChanceMin && MidChance <= Tuning.CatchChanceMax);

    return true;
}

// ---------------------------------------------------------------------------
// ResolveCatch: roll=0.0 always catches, roll=1.0 always drops
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSC4ResolveCatchRollBoundsTest,
    "PlaySports.C4.BallResolution.ResolveCatchRollBounds",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSC4ResolveCatchRollBoundsTest::RunTest(const FString& Parameters)
{
    const FPlayerAttributes Attr = MakeAttributes(50.f, 50.f);

    TestTrue(TEXT("Roll 0.0 always catches"), PSBallResolutionHelpers::ResolveCatch(Attr, 0.0f));
    TestFalse(TEXT("Roll 1.0 always drops"), PSBallResolutionHelpers::ResolveCatch(Attr, 1.0f));

    return true;
}

// ---------------------------------------------------------------------------
// ComputeInterceptionChance: boundary check
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSC4InterceptionChanceBoundsTest,
    "PlaySports.C4.BallResolution.InterceptionChanceBounds",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSC4InterceptionChanceBoundsTest::RunTest(const FString& Parameters)
{
    const FCatchTuningRow Tuning;

    const float LowChance = PSBallResolutionHelpers::ComputeInterceptionChance(MakeAttributes(-10000.f, -10000.f), Tuning);
    TestEqual(TEXT("Extreme-low attributes clamp to InterceptionChanceMin"), LowChance, Tuning.InterceptionChanceMin);

    const float HighChance = PSBallResolutionHelpers::ComputeInterceptionChance(MakeAttributes(10000.f, 10000.f), Tuning);
    TestEqual(TEXT("Extreme-high attributes clamp to InterceptionChanceMax"), HighChance, Tuning.InterceptionChanceMax);

    return true;
}

// ---------------------------------------------------------------------------
// ComputeFumbleRecoveryChance: boundary check
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSC4FumbleRecoveryChanceBoundsTest,
    "PlaySports.C4.BallResolution.FumbleRecoveryChanceBounds",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSC4FumbleRecoveryChanceBoundsTest::RunTest(const FString& Parameters)
{
    const FCatchTuningRow Tuning;

    const float LowChance = PSBallResolutionHelpers::ComputeFumbleRecoveryChance(MakeAttributes(-10000.f, -10000.f), Tuning);
    TestEqual(TEXT("Extreme-low attributes clamp to FumbleRecoveryChanceMin"), LowChance, Tuning.FumbleRecoveryChanceMin);

    const float HighChance = PSBallResolutionHelpers::ComputeFumbleRecoveryChance(MakeAttributes(10000.f, 10000.f), Tuning);
    TestEqual(TEXT("Extreme-high attributes clamp to FumbleRecoveryChanceMax"), HighChance, Tuning.FumbleRecoveryChanceMax);

    return true;
}

// ---------------------------------------------------------------------------
// Custom tuning row is honored (proves the formulas are actually data-driven,
// not still reading hardcoded constants -- AGENTS.md rule 4).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSC4CustomTuningRowHonoredTest,
    "PlaySports.C4.BallResolution.CustomTuningRowHonored",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSC4CustomTuningRowHonoredTest::RunTest(const FString& Parameters)
{
    FCatchTuningRow CustomTuning;
    CustomTuning.CatchBaseChance = 0.0f;
    CustomTuning.CatchAttributeScalar = 0.0f;
    CustomTuning.CatchChanceMin = 0.0f;
    CustomTuning.CatchChanceMax = 1.0f;

    const float Chance = PSBallResolutionHelpers::ComputeCatchChance(MakeAttributes(50.f, 50.f), CustomTuning);
    TestEqual(TEXT("Custom tuning row overrides the default catch formula"), Chance, 0.0f);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
