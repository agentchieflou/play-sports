// PSTeamCoachTests.cpp -- each side's CPU calls with its own team's coaches
//
// Tests covered:
//   1. The match's staffs reach the play-call authority: each team's plan carries its own head
//      coach's aggression, and the side's plan is the team on that side this down.
//   2. Epic 75's fakes follow the kicking team's own head coach: an aggressive one (Hawks) fakes
//      some of his punts, a conservative one (Wolves) never does, read from the plan each call.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSCoachingAI.h"
#include "PSPlayCallSubsystem.h"
#include "PSSpecialTeamsAI.h"
#include "PSSpecialTeamsData.h"
#include "PSStaffData.h"
#include "PSStaffManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSTeamCoachTests
{
    static UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    static void DestroyTestWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /** Fourth and 4 at the kicking team's own 30 in the first quarter: a punt, unless the coach
     *  fakes it. */
    static FPSSituationContext FourthAndFour(bool bHomeBall)
    {
        FPSSituationContext Situation;
        Situation.Down = 4;
        Situation.Distance = 4;
        Situation.YardLine = 30;
        Situation.Quarter = 1;
        Situation.bHomeHasPossession = bHomeBall;
        return Situation;
    }

    /** How many of Count CPU calls in Situation were fake punts and real punts. */
    static void CountPunts(UPSPlayCallSubsystem* PlayCall, const FPSSituationContext& Situation, int32 Count, int32& OutFakes, int32& OutPunts)
    {
        OutFakes = 0;
        OutPunts = 0;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            PlayCall->OpenPlayCall(Situation);
            PlayCall->PollReadyToSnap(0.f);
            FPSPlayDefinition Called;
            if (PlayCall->FindPlay(PlayCall->GetCall(true).PlayId, Called))
            {
                const EPSSpecialTeamsPlay Kind = PSSpecialTeams::FromCategory(Called.PlayCategory);
                OutFakes += Kind == EPSSpecialTeamsPlay::FakePunt ? 1 : 0;
                OutPunts += Kind == EPSSpecialTeamsPlay::Punt ? 1 : 0;
            }
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSTeamCoachProfilesTest,
    "PlaySports.PlayCall.TeamCoachProfiles",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTeamCoachProfilesTest::RunTest(const FString& Parameters)
{
    using namespace PSTeamCoachTests;

    UWorld* World = CreateTestWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSStaffManager* Staffs = NewObject<UPSStaffManager>();
    if (!TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestTrue(TEXT("The staffs load"), Staffs->LoadFromJson(UPSStaffManager::GetDefaultDataPath())))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // Kickoff: the Hawks at home against the Wolves, each with its own staff.
    const FName Home(TEXT("Hawks"));
    const FName Away(TEXT("Wolves"));
    TestTrue(TEXT("Both teams' plans reach the play-call authority"), Staffs->ApplyToPlayCall(PlayCall, Home, Away));
    const FPSCoachDef* HomeCoach = Staffs->FindTeamCoach(Home, EPSCoachRole::HeadCoach);
    const FPSCoachDef* AwayCoach = Staffs->FindTeamCoach(Away, EPSCoachRole::HeadCoach);
    if (!TestNotNull(TEXT("The Hawks' head coach"), HomeCoach) || !TestNotNull(TEXT("The Wolves' head coach"), AwayCoach))
    {
        DestroyTestWorld(World);
        return false;
    }
    const FPSSpecialTeamsTuning& Kicking = PlayCall->GetCoachingAI()->GetSpecialTeamsAI()->GetTuning();
    TestTrue(TEXT("(The Hawks' coach is aggressive enough to fake, the Wolves' isn't)"),
        HomeCoach->Aggression >= Kicking.FakeMinAggression && AwayCoach->Aggression < Kicking.FakeMinAggression);

    PlayCall->OpenPlayCall(FourthAndFour(true));
    TestEqual(TEXT("With the ball, the Hawks call with their own head coach"), PlayCall->GetCallingPlan(true).OffenseTendency.AggressionScore, HomeCoach->Aggression);
    TestEqual(TEXT("...against the Wolves' defense, with theirs"), PlayCall->GetCallingPlan(false).DefenseTendency.AggressionScore, AwayCoach->Aggression);
    PlayCall->OpenPlayCall(FourthAndFour(false));
    TestEqual(TEXT("With the ball, the Wolves call with their own"), PlayCall->GetCallingPlan(true).OffenseTendency.AggressionScore, AwayCoach->Aggression);
    TestEqual(TEXT("...against the Hawks' defense"), PlayCall->GetCallingPlan(false).DefenseTendency.AggressionScore, HomeCoach->Aggression);

    // Fourth and 4 deep in their own end, forty times each: only the aggressive coach fakes.
    PlayCall->GetCoachingAI()->SeedDeterminism(78);
    int32 Fakes = 0;
    int32 Punts = 0;
    CountPunts(PlayCall, FourthAndFour(true), 40, Fakes, Punts);
    TestTrue(TEXT("The Hawks' aggressive coach fakes some punts"), Fakes > 0);
    TestTrue(TEXT("...and punts the rest"), Punts > 0 && Fakes + Punts == 40);
    CountPunts(PlayCall, FourthAndFour(false), 40, Fakes, Punts);
    TestEqual(TEXT("The Wolves' conservative coach never fakes"), Fakes, 0);
    TestEqual(TEXT("...he punts"), Punts, 40);

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
