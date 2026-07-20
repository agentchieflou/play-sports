// PSCombatRulesModelTests.cpp - Epic 139: deterministic tackle-damage resolution
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSCombatRulesModel.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Same seed and inputs produce the same damage roll (replay-safe)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCombatRulesDeterminismTest,
    "PlaySports.Combat.RulesModelDeterminism",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCombatRulesDeterminismTest::RunTest(const FString& Parameters)
{
    FPlayerAttributes Carrier;
    Carrier.Role = EPlayerRole::RunningBack;
    Carrier.Strength = 70.f;
    Carrier.Agility = 80.f;

    FPlayerAttributes Tackler;
    Tackler.Role = EPlayerRole::Linebacker;
    Tackler.Strength = 85.f;

    FPSArchetypeTuning Tuning;

    UPSCombatRulesModel* ModelA = NewObject<UPSCombatRulesModel>();
    ModelA->SeedDeterminism(42);
    const float DamageA = ModelA->ResolveTackleDamage(Carrier, Tackler, Tuning);

    UPSCombatRulesModel* ModelB = NewObject<UPSCombatRulesModel>();
    ModelB->SeedDeterminism(42);
    const float DamageB = ModelB->ResolveTackleDamage(Carrier, Tackler, Tuning);

    TestEqual(TEXT("Same seed and inputs produce the same damage roll"), DamageA, DamageB);
    TestTrue(TEXT("Damage is always positive"), DamageA > 0.f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Linemen take less damage per hit than skill players (tankier archetype)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCombatRulesArchetypeScalingTest,
    "PlaySports.Combat.RulesModelArchetypeScaling",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCombatRulesArchetypeScalingTest::RunTest(const FString& Parameters)
{
    FPlayerAttributes SkillCarrier;
    SkillCarrier.Role = EPlayerRole::WideReceiver;
    SkillCarrier.Strength = 60.f;
    SkillCarrier.Agility = 85.f;

    FPlayerAttributes LinemanCarrier;
    LinemanCarrier.Role = EPlayerRole::OffensiveLineman;
    LinemanCarrier.Strength = 60.f;
    LinemanCarrier.Agility = 85.f;

    FPlayerAttributes Tackler;
    Tackler.Role = EPlayerRole::DefensiveLineman;
    Tackler.Strength = 80.f;

    FPSArchetypeTuning Tuning;

    UPSCombatRulesModel* SkillModel = NewObject<UPSCombatRulesModel>();
    SkillModel->SeedDeterminism(7);
    const float SkillDamage = SkillModel->ResolveTackleDamage(SkillCarrier, Tackler, Tuning);

    UPSCombatRulesModel* LinemanModel = NewObject<UPSCombatRulesModel>();
    LinemanModel->SeedDeterminism(7);
    const float LinemanDamage = LinemanModel->ResolveTackleDamage(LinemanCarrier, Tackler, Tuning);

    TestTrue(TEXT("Linemen take less tackle damage than offense skill players with identical attributes"), LinemanDamage < SkillDamage);

    return true;
}

#endif
