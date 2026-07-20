// PSCombatRuleVariantTests.cpp - Epic 140: no punting, INT punishment, 4th-down overload
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSPlaySimulation.h"
#include "PSRulesConfig.h"
#include "PSArchetypeTuning.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Test 1 -- Defensive personnel count is 11 on downs 1-3, 12 on 4th down when
// the overload rule is enabled, and stays 11 when it's disabled.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCombatRuleFourthDownOverloadTest,
    "PlaySports.Combat.FourthDownDefensiveOverload",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCombatRuleFourthDownOverloadTest::RunTest(const FString& Parameters)
{
    UPSRulesConfig* Rules = NewObject<UPSRulesConfig>();
    Rules->bFourthDownDefensiveOverload = true;
    Rules->NumExtraDefendersOnFourthDown = 1;

    TestEqual(TEXT("11 defenders on 1st down"), GetDefensivePersonnelCount(1, Rules), 11);
    TestEqual(TEXT("11 defenders on 3rd down"), GetDefensivePersonnelCount(3, Rules), 11);
    TestEqual(TEXT("12 defenders on 4th down with overload enabled"), GetDefensivePersonnelCount(4, Rules), 12);

    Rules->bFourthDownDefensiveOverload = false;
    TestEqual(TEXT("11 defenders on 4th down with overload disabled"), GetDefensivePersonnelCount(4, Rules), 11);

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- With punting disallowed and out of field-goal range, 4th down stays
// a normal snap attempt (PreSnap) instead of transitioning to the Punt phase.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCombatRuleNoPuntingTest,
    "PlaySports.Combat.NoPuntingGoesForIt",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCombatRuleNoPuntingTest::RunTest(const FString& Parameters)
{
    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    TestNotNull(TEXT("Sim created"), Sim);
    if (!Sim) { return false; }

    UPSRulesConfig* Rules = NewObject<UPSRulesConfig>();
    Rules->bAllowPunting = false;
    Sim->RulesConfig = Rules;

    TArray<FPlayerAttributes> Offense, Defense;
    Sim->InitializePlay(Offense, Defense);

    // Drive from 1st down to 4th down with no-gain plays, deep in own territory
    // (well under the YardLine >= 60 field-goal-range threshold).
    for (int32 i = 0; i < 3; ++i)
    {
        Sim->RecordTackle(0);
        Sim->EndPlayAndPrepareNext();
    }

    TestEqual(TEXT("Now facing 4th down"), Sim->GetPlayState().Down, 4);
    TestEqual(TEXT("Punting disallowed out of FG range keeps a normal snap phase"),
        Sim->GetPlayState().Phase, EPlayPhase::PreSnap);

    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Archetype role mapping groups positions into the three combat classes
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCombatRuleArchetypeMappingTest,
    "PlaySports.Combat.ArchetypeRoleMapping",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCombatRuleArchetypeMappingTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("QB is an offense skill player"), GetArchetypeClassForRole(EPlayerRole::Quarterback), EPlayerArchetypeClass::OffenseSkill);
    TestEqual(TEXT("WR is an offense skill player"), GetArchetypeClassForRole(EPlayerRole::WideReceiver), EPlayerArchetypeClass::OffenseSkill);
    TestEqual(TEXT("CB (DefensiveBack) is a defense skill player"), GetArchetypeClassForRole(EPlayerRole::DefensiveBack), EPlayerArchetypeClass::DefenseSkill);
    TestEqual(TEXT("Linebacker is a defense skill player"), GetArchetypeClassForRole(EPlayerRole::Linebacker), EPlayerArchetypeClass::DefenseSkill);
    TestEqual(TEXT("Offensive Lineman is a lineman"), GetArchetypeClassForRole(EPlayerRole::OffensiveLineman), EPlayerArchetypeClass::Lineman);
    TestEqual(TEXT("Defensive Lineman is a lineman"), GetArchetypeClassForRole(EPlayerRole::DefensiveLineman), EPlayerArchetypeClass::Lineman);

    return true;
}

#endif
