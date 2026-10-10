// PSPenaltyRateTests.cpp -- the snap's flags fly at per-play rates from data, quick or live
//
// Tests covered:
//   1. Data/penalties.json loads through UPSDataIngestion, equals the struct's defaults and
//      validates; chances outside 0..1 are refused. Holding is a scrimmage play's, once at its
//      snap: never on a kickoff, never later in a live play however long it runs.
//   2. Quick sim and a live game agree: over a seeded quick-sim season (6-second steps) holding is
//      called on HoldingChancePerPlay of the scrimmage plays (less the snaps an offside takes), as
//      on a live game's snaps -- not the ~45% a per-second chance gave quick sim's long steps.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "PSDataIngestion.h"
#include "PSPenaltyModel.h"
#include "PSPlaySimulation.h"
#include "PSPlayerAttributes.h"
#include "PSQuickSimRunner.h"
#include "PSTelemetryBus.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPenaltyRateTests
{
    struct FRoleCount
    {
        EPlayerRole Role;
        int32 Players;
    };

    /** A team of 22, both sides of the ball, every rating Rating. */
    static TArray<FPlayerAttributes> MakeTeam(const TCHAR* Prefix, float Rating)
    {
        static const FRoleCount Counts[] = {
            { EPlayerRole::Quarterback, 1 }, { EPlayerRole::RunningBack, 1 }, { EPlayerRole::TightEnd, 1 },
            { EPlayerRole::WideReceiver, 3 }, { EPlayerRole::OffensiveLineman, 5 },
            { EPlayerRole::DefensiveLineman, 4 }, { EPlayerRole::Linebacker, 3 }, { EPlayerRole::DefensiveBack, 4 } };
        TArray<FPlayerAttributes> Team;
        for (const FRoleCount& Count : Counts)
        {
            for (int32 Number = 1; Number <= Count.Players; ++Number)
            {
                FPlayerAttributes& Player = Team.AddDefaulted_GetRef();
                Player.PlayerId = FName(*FString::Printf(TEXT("%s_%s_%d"), Prefix, *UEnum::GetValueAsString(Count.Role).RightChop(13), Number));
                Player.DisplayName = Player.PlayerId.ToString();
                Player.Role = Count.Role;
                Player.Speed = Rating;
                Player.Agility = Rating;
                Player.Strength = Rating;
                Player.Acceleration = Rating;
                Player.Awareness = Rating;
                Player.Stamina = Rating;
            }
        }
        return Team;
    }

    static bool IsKickResult(const FString& Result)
    {
        return Result == TEXT("KickoffResult") || Result == TEXT("PuntResult") || Result == TEXT("FieldGoalGood") || Result == TEXT("FieldGoalMissed");
    }

    static void UnseedRandom()
    {
        FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The rates are data; holding is the scrimmage snap's
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPenaltyTuningTest,
    "PlaySports.Penalties.HoldingIsTheScrimmageSnaps",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPenaltyTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSPenaltyRateTests;

    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSPenaltyTuning Loaded;
    if (!TestTrue(TEXT("Data/penalties.json loads"), Ingestion->LoadPenaltyTuningFromJson(UPSPenaltyModel::GetDefaultTuningPath(), Loaded)))
    {
        return false;
    }
    const FPSPenaltyTuning Defaults;
    TestEqual(TEXT("Holding's rate is the struct's default"), Loaded.HoldingChancePerPlay, Defaults.HoldingChancePerPlay);
    TestEqual(TEXT("...and offside's"), Loaded.OffsidesChancePerSnap, Defaults.OffsidesChancePerSnap);
    TestEqual(TEXT("The file is sound"), UPSPenaltyModel::ValidateTuning(Loaded).Num(), 0);
    FPSPenaltyTuning Bad;
    Bad.HoldingChancePerPlay = 1.5f;
    Bad.OffsidesChancePerSnap = -0.1f;
    TestEqual(TEXT("Chances outside 0..1 are caught"), UPSPenaltyModel::ValidateTuning(Bad).Num(), 2);

    UPSPlaySimulation* Sim = NewObject<UPSPlaySimulation>();
    Sim->InitializePlay(MakeTeam(TEXT("HOME"), 75.f), MakeTeam(TEXT("AWAY"), 75.f));
    TestEqual(TEXT("The simulation runs on the file's rates"), Sim->GetPenalties()->GetTuning().HoldingChancePerPlay, Loaded.HoldingChancePerPlay);
    TestFalse(TEXT("Unsound rates are refused"), Sim->GetPenalties()->SetTuning(Bad));

    // Holding certain, offside never: every scrimmage snap draws it, at the snap.
    FPSPenaltyTuning Always;
    Always.HoldingChancePerPlay = 1.f;
    Always.OffsidesChancePerSnap = 0.f;
    TestTrue(TEXT("Certain holding"), Sim->GetPenalties()->SetTuning(Always));
    Sim->TriggerSnap();
    TestEqual(TEXT("A scrimmage snap draws holding at once"), Sim->ActivePenalty, EPSPenaltyType::Holding);

    // Holding never: a live play runs its whole course without a flag, however small its steps.
    FPSPenaltyTuning Never;
    Never.HoldingChancePerPlay = 0.f;
    Never.OffsidesChancePerSnap = 0.f;
    TestTrue(TEXT("No flags"), Sim->GetPenalties()->SetTuning(Never));
    Sim->ActivePenalty = EPSPenaltyType::None;
    Sim->SetPlayPhase(EPlayPhase::PreSnap);
    Sim->TriggerSnap();
    for (int32 Step = 0; Step < 200 && Sim->GetPlayState().Phase != EPlayPhase::Scoring; ++Step)
    {
        Sim->AdvancePlay(0.1f);
    }
    TestEqual(TEXT("The play reaches the whistle"), Sim->GetPlayState().Phase, EPlayPhase::Scoring);
    TestEqual(TEXT("...without a flag: nothing is rolled while it runs"), Sim->ActivePenalty, EPSPenaltyType::None);

    // A kickoff is no scrimmage play.
    TestTrue(TEXT("Certain holding again"), Sim->GetPenalties()->SetTuning(Always));
    Sim->RecordTouchdown();
    Sim->EndPlayAndPrepareNext();
    TestTrue(TEXT("A touchdown: the kickoff is next"), Sim->GetPlayState().bKickoff && Sim->GetPlayState().Phase == EPlayPhase::PreSnap);
    Sim->TriggerSnap();
    TestEqual(TEXT("The kickoff snaps into the kick"), Sim->GetPlayState().Phase, EPlayPhase::Kickoff);
    TestEqual(TEXT("...with no holding"), Sim->ActivePenalty, EPSPenaltyType::None);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Quick sim and a live game call holding at the same rate
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSHoldingRateTest,
    "PlaySports.Penalties.QuickSimHoldingRateMatchesLive",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSHoldingRateTest::RunTest(const FString& Parameters)
{
    using namespace PSPenaltyRateTests;

    const TArray<FPlayerAttributes> Home = MakeTeam(TEXT("HOME"), 75.f);
    const TArray<FPlayerAttributes> Away = MakeTeam(TEXT("AWAY"), 70.f);
    UPSPenaltyModel* Rates = NewObject<UPSPenaltyModel>();
    Rates->LoadTuningFromJson(UPSPenaltyModel::GetDefaultTuningPath());
    const FPSPenaltyTuning& Tuning = Rates->GetTuning();
    // Holding is rolled when no offside flew at the same snap.
    const float Expected = Tuning.HoldingChancePerPlay * (1.f - Tuning.OffsidesChancePerSnap);

    // A seeded quick-sim season: 17 games at quick sim's 6-second steps.
    UPSQuickSimRunner* Runner = NewObject<UPSQuickSimRunner>();
    int32 ScrimmagePlays = 0;
    int32 Holdings = 0;
    Runner->OnPlayResolved.AddLambda([&ScrimmagePlays](const FPSTelemetryPlayResultEvent& Play)
    {
        ScrimmagePlays += IsKickResult(Play.Result) ? 0 : 1;
    });
    Runner->OnPenaltyRuled.AddLambda([&Holdings](const FPSTelemetryPenaltyEvent& Ruling)
    {
        Holdings += Ruling.Penalty == TEXT("Holding") ? 1 : 0;
    });
    FMath::RandInit(20261010);
    for (int32 Game = 0; Game < 17; ++Game)
    {
        Runner->SimulateGame(Home, Away);
    }
    const float QuickRate = ScrimmagePlays > 0 ? static_cast<float>(Holdings) / ScrimmagePlays : 0.f;
    AddInfo(FString::Printf(TEXT("Quick sim: holding on %d of %d scrimmage plays (%.3f; expected %.3f)"), Holdings, ScrimmagePlays, QuickRate, Expected));
    TestTrue(TEXT("A season's worth of scrimmage plays"), ScrimmagePlays >= 500);
    TestTrue(TEXT("Quick sim calls holding at the per-play rate"), FMath::Abs(QuickRate - Expected) <= 0.04f);
    TestTrue(TEXT("...far from the 45% a per-second chance gave its long steps"), QuickRate < 0.25f);

    // A live game's snaps, seeded too: the same rate.
    UPSPlaySimulation* Live = NewObject<UPSPlaySimulation>();
    Live->InitializePlay(Home, Away);
    int32 LiveHoldings = 0;
    const int32 LiveSnaps = 1000;
    for (int32 Snap = 0; Snap < LiveSnaps; ++Snap)
    {
        Live->TriggerSnap();
        LiveHoldings += Live->ActivePenalty == EPSPenaltyType::Holding ? 1 : 0;
        Live->ActivePenalty = EPSPenaltyType::None;
        Live->SetPlayPhase(EPlayPhase::PreSnap);
    }
    UnseedRandom();
    const float LiveRate = static_cast<float>(LiveHoldings) / LiveSnaps;
    AddInfo(FString::Printf(TEXT("Live: holding on %d of %d snaps (%.3f)"), LiveHoldings, LiveSnaps, LiveRate));
    TestTrue(TEXT("A live game calls holding at the per-play rate"), FMath::Abs(LiveRate - Expected) <= 0.04f);
    TestTrue(TEXT("...so quick and live games agree"), FMath::Abs(QuickRate - LiveRate) <= 0.06f);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
