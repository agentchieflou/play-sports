// PSAudioTests.cpp -- Epic 23.1 (audio event mapping) and the 23.3 field-sound hooks
//
// Gameplay events are published on the telemetry bus in a headless world, the way the live game
// announces them. UPSAudioSubsystem maps them to cues through Data/audio_cues.json; no sound is
// imported, so every request is recorded but none sounds, which is what these tests read.
//
// Tests covered:
//   1. The mapping: the data loads and validates, bad tunings are refused; the snap, the whistle
//      (once a play), hits (big and small, the pads scaled by force), a sack, a touchdown, a flag
//      and a quarter's end each ask for their cues; a cue's cooldown; a muted layer.
//   2. Voices and loops: the platform tier's voice limit (a higher priority takes the weakest
//      voice, an equal one gives way), voices released at the tier's update rate, one loop a
//      group, unknown cues, the shipped tiers' numbers.
//   3. The field layer's hooks (23.3's code half): linemen engaging, the cadence, an animation's
//      footstep through RequestCue.
//   4. The flag's authority: the play simulation throws the flag on an offside jump and settles it
//      as the play is scored, both on the bus; the audio blows the flag's stinger.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "PSAudioSubsystem.h"
#include "PSDataIngestion.h"
#include "PSPlatformTiers.h"
#include "PSPlaySimulation.h"
#include "PSSettingsSubsystem.h"
#include "PSTelemetryBus.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSAudioTests
{
    UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (World)
        {
            FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
            WorldContext.SetCurrentWorld(World);
        }
        return World;
    }

    void DestroyTestWorld(UWorld* World)
    {
        if (World)
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    }

    /** A tier with Voices voices, updating every frame (UpdateHz 0) or UpdateHz times a second. */
    FPSPlatformTier MakeTier(int32 Voices, float UpdateHz)
    {
        FPSPlatformTier Tier;
        Tier.TierId = TEXT("AudioTest");
        Tier.AudioMaxVoices = Voices;
        Tier.AudioUpdateHz = UpdateHz;
        return Tier;
    }

    FPSAudioCueDef MakeCue(const TCHAR* CueId, int32 Priority, float DurationSeconds)
    {
        FPSAudioCueDef Cue;
        Cue.CueId = CueId;
        Cue.Layer = EPSAudioLayer::Field;
        Cue.Priority = Priority;
        Cue.DurationSeconds = DurationSeconds;
        return Cue;
    }

    /** The audio of a fresh world, bound to its bus with every voice free. */
    struct FAudioFixture
    {
        UWorld* World = nullptr;
        UPSTelemetryBus* Bus = nullptr;
        UPSAudioSubsystem* Audio = nullptr;

        bool Setup()
        {
            World = CreateTestWorld();
            Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
            Audio = World ? World->GetSubsystem<UPSAudioSubsystem>() : nullptr;
            if (!Bus || !Audio)
            {
                return false;
            }
            Audio->BindToBus(Bus);
            Audio->ApplyPlatformTier(MakeTier(32, 0.f));
            return true;
        }

        ~FAudioFixture()
        {
            DestroyTestWorld(World);
        }
    };
}

// ---------------------------------------------------------------------------
// Test 1 -- The mapping
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAudioCueMappingTest,
    "PlaySports.Audio.CueMapping",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAudioCueMappingTest::RunTest(const FString& Parameters)
{
    PSAudioTests::FAudioFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus and the audio"), Fixture.Setup()))
    {
        return false;
    }
    UPSAudioSubsystem* Audio = Fixture.Audio;
    UPSTelemetryBus* Bus = Fixture.Bus;

    // The data: Data/audio_cues.json loads and is sound.
    const FPSAudioTuning& Tuning = Audio->GetTuning();
    TestTrue(TEXT("The cue catalog loads"), Tuning.Cues.Num() > 0 && Tuning.EventCues.Num() > 0);
    TestEqual(TEXT("...and validates"), UPSAudioSubsystem::ValidateTuning(Tuning).Num(), 0);
    TestNotNull(TEXT("The whistle is a cue"), Audio->FindCue(TEXT("Field.Whistle")));
    {
        FPSAudioTuning Bad = Tuning;
        const FPSAudioCueDef Twice = Bad.Cues[0];
        Bad.Cues.Add(Twice);
        TestFalse(TEXT("A cue id used twice is refused"), Audio->SetTuning(Bad));
        Bad = Tuning;
        FPSAudioEventCue Stray;
        Stray.Trigger = EPSAudioTrigger::Snap;
        Stray.CueId = TEXT("No.Such.Cue");
        Bad.EventCues.Add(Stray);
        TestFalse(TEXT("A rule naming an unknown cue is refused"), Audio->SetTuning(Bad));
        Bad = Tuning;
        Bad.Cues[0].bLoop = true;
        Bad.Cues[0].LoopGroup = NAME_None;
        TestFalse(TEXT("A loop without a group is refused"), Audio->SetTuning(Bad));
        Bad = Tuning;
        Bad.Cues[0].Volume = 1.5f;
        TestFalse(TEXT("A volume over 1 is refused"), Audio->SetTuning(Bad));
        TestEqual(TEXT("...and the good tuning is kept"), Audio->GetTuning().Cues.Num(), Tuning.Cues.Num());
    }

    // The snap.
    FPSTelemetrySnapEvent Snap;
    Snap.Down = 1;
    Snap.Distance = 10;
    Snap.YardLine = 25;
    Bus->PublishSnap(Snap);
    TestEqual(TEXT("The snap asks for the snap cue"), Audio->CountRequests(TEXT("Field.Snap")), 1);
    FPSAudioCueRequest Request;
    if (TestTrue(TEXT("...recorded"), Audio->FindLastRequest(TEXT("Field.Snap"), Request)))
    {
        TestEqual(TEXT("...as the snap's"), Request.Trigger, EPSAudioTrigger::Snap);
        TestFalse(TEXT("...with no sound imported yet"), Request.bSounded);
        TestTrue(TEXT("...at its cue's volume (no settings: full layers)"), FMath::IsNearlyEqual(Request.Volume, Audio->FindCue(TEXT("Field.Snap"))->Volume));
    }

    // The whistle: the play goes dead once, however many events say so.
    FPSTelemetryGameStateEvent State;
    State.Phase = TEXT("BallCarrierMovement");
    Bus->PublishGameState(State);
    TestEqual(TEXT("No whistle while the ball is live"), Audio->CountRequests(TEXT("Field.Whistle")), 0);
    State.Phase = TEXT("Scoring");
    Bus->PublishGameState(State);
    FPSTelemetryPhaseChangeEvent Dead;
    Dead.OldPhase = TEXT("BallCarrierMovement");
    Dead.NewPhase = TEXT("Scoring");
    Bus->PublishPhaseChange(Dead);
    TestEqual(TEXT("The play going dead blows the whistle once"), Audio->CountRequests(TEXT("Field.Whistle")), 1);
    State.Phase = TEXT("PreSnap");
    Bus->PublishGameState(State);
    TestEqual(TEXT("...and not again before the next snap"), Audio->CountRequests(TEXT("Field.Whistle"), true), 1);

    // Hits: the pads every time, scaled by the force; the stinger for a big one.
    const FPSAudioCueDef* Pads = Audio->FindCue(TEXT("Field.Pads.Hit"));
    if (TestNotNull(TEXT("The pads' hit cue"), Pads))
    {
        FPSTelemetryDamageEvent Hit;
        Hit.TargetName = TEXT("RB_01");
        Hit.Amount = Tuning.BigHitDamage * 0.5f;
        Bus->PublishDamage(Hit);
        TestEqual(TEXT("A small hit sounds the pads"), Audio->CountRequests(TEXT("Field.Pads.Hit")), 1);
        TestEqual(TEXT("...without the big-hit stinger"), Audio->CountRequests(TEXT("Stinger.BigHit"), true), 0);
        if (Audio->FindLastRequest(TEXT("Field.Pads.Hit"), Request))
        {
            TestTrue(TEXT("...its volume scaled by its force"), FMath::IsNearlyEqual(Request.Volume, Pads->Volume * Hit.Amount / Tuning.FullIntensityDamage, 0.001f));
        }
        Audio->AdvanceTime(Pads->CooldownSeconds + 0.01f);
        Hit.Amount = Tuning.BigHitDamage;
        Bus->PublishDamage(Hit);
        TestEqual(TEXT("A big hit sounds the pads again"), Audio->CountRequests(TEXT("Field.Pads.Hit")), 2);
        TestEqual(TEXT("...and the big-hit stinger"), Audio->CountRequests(TEXT("Stinger.BigHit")), 1);
    }

    // A sack: the tackle's pads and the sack's stinger.
    FPSTelemetryTackleEvent Sack;
    Sack.TacklerName = TEXT("DE_01");
    Sack.BallCarrierName = TEXT("QB_01");
    Sack.YardsGained = -7;
    Sack.bIsSack = true;
    Bus->PublishTackle(Sack);
    TestEqual(TEXT("A sack sounds the tackle"), Audio->CountRequests(TEXT("Field.Pads.Tackle")), 1);
    TestEqual(TEXT("...and the sack stinger"), Audio->CountRequests(TEXT("Stinger.Sack")), 1);

    // A touchdown, as the play simulation resolved it.
    FPSTelemetryPlayResultEvent Touchdown;
    Touchdown.Result = TEXT("Touchdown");
    Touchdown.HomePoints = 7;
    Touchdown.YardsGained = 25;
    Bus->PublishPlayResult(Touchdown);
    TestEqual(TEXT("A touchdown plays the touchdown stinger"), Audio->CountRequests(TEXT("Stinger.Touchdown")), 1);
    TestEqual(TEXT("...not the field goal's"), Audio->CountRequests(TEXT("Stinger.FieldGoal"), true), 0);
    FPSTelemetryPlayResultEvent FieldGoal;
    FieldGoal.Result = TEXT("FieldGoalGood");
    FieldGoal.AwayPoints = 3;
    Bus->PublishPlayResult(FieldGoal);
    TestEqual(TEXT("A field goal plays its own"), Audio->CountRequests(TEXT("Stinger.FieldGoal")), 1);

    // A flag.
    FPSTelemetryPenaltyEvent Flag;
    Flag.Kind = EPSPenaltyEventKind::Flag;
    Flag.Penalty = TEXT("Holding");
    Bus->PublishPenalty(Flag);
    TestEqual(TEXT("A flag plays the flag stinger"), Audio->CountRequests(TEXT("Stinger.Flag")), 1);
    Flag.Kind = EPSPenaltyEventKind::Accepted;
    Bus->PublishPenalty(Flag);
    TestEqual(TEXT("...not its ruling"), Audio->CountRequests(TEXT("Stinger.Flag"), true), 1);

    // A quarter's end: the horn.
    State.Quarter = 2;
    Bus->PublishGameState(State);
    TestEqual(TEXT("The quarter's end sounds the horn"), Audio->CountRequests(TEXT("Stinger.Horn")), 1);

    // The cooldown: the snap cue doesn't repeat within its CooldownSeconds.
    const FPSAudioCueDef* SnapCue = Audio->FindCue(TEXT("Field.Snap"));
    if (TestNotNull(TEXT("The snap cue"), SnapCue) && TestTrue(TEXT("...has a cooldown"), SnapCue->CooldownSeconds > 0.f))
    {
        Audio->AdvanceTime(SnapCue->CooldownSeconds + 0.01f);
        Bus->PublishSnap(Snap);
        Bus->PublishSnap(Snap);
        TestEqual(TEXT("Two snaps at once: one plays"), Audio->CountRequests(TEXT("Field.Snap")), 2);
        if (Audio->FindLastRequest(TEXT("Field.Snap"), Request))
        {
            TestEqual(TEXT("...the other is held by the cooldown"), Request.DropReason, EPSAudioDropReason::Cooldown);
        }
        Audio->AdvanceTime(SnapCue->CooldownSeconds + 0.01f);
        Bus->PublishSnap(Snap);
        TestEqual(TEXT("After the cooldown it plays again"), Audio->CountRequests(TEXT("Field.Snap")), 3);
    }

    // The mix: a muted layer drops its cues; the others play on.
    UPSSettingsSubsystem* Settings = NewObject<UPSSettingsSubsystem>(NewObject<UGameInstance>());
    Audio->SetSettings(Settings);
    Settings->SetValue(TEXT("EffectsVolume"), 50.f);
    TestTrue(TEXT("The effects layer follows its setting"), FMath::IsNearlyEqual(Audio->GetLayerVolume(EPSAudioLayer::Field), 0.5f));
    Settings->SetValue(TEXT("EffectsVolume"), 0.f);
    Audio->AdvanceTime(5.f);
    Bus->PublishSnap(Snap);
    if (TestTrue(TEXT("A snap with effects muted"), Audio->FindLastRequest(TEXT("Field.Snap"), Request)))
    {
        TestEqual(TEXT("...is dropped as muted"), Request.DropReason, EPSAudioDropReason::Muted);
    }
    TestTrue(TEXT("The commentary layer keeps its own setting"), FMath::IsNearlyEqual(Audio->GetLayerVolume(EPSAudioLayer::Commentary), 1.f));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Voices and loops
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAudioVoicesTest,
    "PlaySports.Audio.VoicesAndLoops",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAudioVoicesTest::RunTest(const FString& Parameters)
{
    PSAudioTests::FAudioFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus and the audio"), Fixture.Setup()))
    {
        return false;
    }
    UPSAudioSubsystem* Audio = Fixture.Audio;

    FPSAudioTuning Tuning;
    Tuning.Cues.Add(PSAudioTests::MakeCue(TEXT("Low"), 10, 1.f));
    Tuning.Cues.Add(PSAudioTests::MakeCue(TEXT("Mid"), 20, 1.f));
    Tuning.Cues.Add(PSAudioTests::MakeCue(TEXT("High"), 50, 1.f));
    Tuning.Cues.Add(PSAudioTests::MakeCue(TEXT("Blip"), 30, 0.02f));
    FPSAudioCueDef BedA = PSAudioTests::MakeCue(TEXT("Bed.A"), 30, 1.f);
    BedA.Layer = EPSAudioLayer::Crowd;
    BedA.bLoop = true;
    BedA.LoopGroup = TEXT("Bed");
    FPSAudioCueDef BedB = BedA;
    BedB.CueId = TEXT("Bed.B");
    Tuning.Cues.Add(BedA);
    Tuning.Cues.Add(BedB);
    if (!TestTrue(TEXT("A test catalog is taken"), Audio->SetTuning(Tuning)))
    {
        return false;
    }

    // Two voices: the third cue takes the weakest voice when it outranks it, or gives way.
    Audio->ApplyPlatformTier(PSAudioTests::MakeTier(2, 0.f));
    TestEqual(TEXT("The tier's voices"), Audio->GetMaxVoices(), 2);
    TestTrue(TEXT("The low cue plays"), Audio->RequestCue(TEXT("Low"), FVector::ZeroVector));
    TestTrue(TEXT("The mid cue plays"), Audio->RequestCue(TEXT("Mid"), FVector::ZeroVector));
    TestEqual(TEXT("Both voices are busy"), Audio->GetActiveVoiceCount(), 2);
    TestFalse(TEXT("Another low cue gives way"), Audio->RequestCue(TEXT("Low"), FVector::ZeroVector));
    FPSAudioCueRequest Request;
    if (Audio->FindLastRequest(TEXT("Low"), Request))
    {
        TestEqual(TEXT("...for want of a voice"), Request.DropReason, EPSAudioDropReason::VoiceLimit);
    }
    TestTrue(TEXT("The high cue takes a voice"), Audio->RequestCue(TEXT("High"), FVector::ZeroVector));
    TestEqual(TEXT("...still two voices"), Audio->GetActiveVoiceCount(), 2);
    TestFalse(TEXT("...the low one's: a mid cue now outranks nothing"), Audio->RequestCue(TEXT("Mid"), FVector::ZeroVector));

    // Loops sit outside the voices, one a group.
    TestTrue(TEXT("A bed loop starts"), Audio->RequestCue(TEXT("Bed.A"), FVector::ZeroVector));
    TestEqual(TEXT("...in its group"), Audio->GetActiveLoop(TEXT("Bed")), FName(TEXT("Bed.A")));
    TestEqual(TEXT("...without taking a voice"), Audio->GetActiveVoiceCount(), 2);
    TestTrue(TEXT("The next bed"), Audio->RequestCue(TEXT("Bed.B"), FVector::ZeroVector));
    TestEqual(TEXT("...replaces it"), Audio->GetActiveLoop(TEXT("Bed")), FName(TEXT("Bed.B")));
    Audio->StopLoop(TEXT("Bed"));
    TestEqual(TEXT("A stopped group is quiet"), Audio->GetActiveLoop(TEXT("Bed")), FName());

    // Voices end with their sounds.
    Audio->AdvanceTime(1.05f);
    TestEqual(TEXT("Every voice is free once its sound ends"), Audio->GetActiveVoiceCount(), 0);

    // At a tier's update rate, voices are released on the update, not every frame.
    Audio->ApplyPlatformTier(PSAudioTests::MakeTier(2, 10.f));
    Audio->AdvanceTime(0.2f);
    TestTrue(TEXT("A short cue"), Audio->RequestCue(TEXT("Blip"), FVector::ZeroVector));
    Audio->AdvanceTime(0.05f);
    TestEqual(TEXT("...still holds its voice between updates"), Audio->GetActiveVoiceCount(), 1);
    Audio->AdvanceTime(0.06f);
    TestEqual(TEXT("...and is released at the next"), Audio->GetActiveVoiceCount(), 0);

    // Unknown cues are recorded, not played.
    TestFalse(TEXT("An unknown cue"), Audio->RequestCue(TEXT("No.Such.Cue"), FVector::ZeroVector));
    if (Audio->FindLastRequest(TEXT("No.Such.Cue"), Request))
    {
        TestEqual(TEXT("...is logged as unknown"), Request.DropReason, EPSAudioDropReason::UnknownCue);
    }

    // The log keeps the latest MaxRequestsKept.
    Tuning.MaxRequestsKept = 3;
    Audio->SetTuning(Tuning);
    for (int32 Index = 0; Index < 5; ++Index)
    {
        Audio->RequestCue(TEXT("Bed.A"), FVector::ZeroVector);
    }
    TestEqual(TEXT("The log keeps MaxRequestsKept"), Audio->GetRequestLog().Num(), 3);

    // The shipped tiers: fewer voices and a slower update on a phone, no special case in code.
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSPlatformTierCatalog Catalog;
    if (TestTrue(TEXT("The platform tiers load"), Ingestion->LoadPlatformTiersFromJson(PSPlatformTiers::GetDefaultCatalogPath(), Catalog)))
    {
        const FPSPlatformTier* Desktop = PSPlatformTiers::FindTier(Catalog, TEXT("DesktopHigh"));
        const FPSPlatformTier* Phone = PSPlatformTiers::FindTier(Catalog, TEXT("MobileBaseline"));
        if (TestNotNull(TEXT("The desktop tier"), Desktop) && TestNotNull(TEXT("The iPhone tier"), Phone))
        {
            TestTrue(TEXT("The iPhone plays fewer voices"), Phone->AudioMaxVoices < Desktop->AudioMaxVoices && Phone->AudioMaxVoices >= 1);
            TestTrue(TEXT("...and updates the audio less often"), Phone->AudioUpdateHz > 0.f && (Desktop->AudioUpdateHz == 0.f || Phone->AudioUpdateHz < Desktop->AudioUpdateHz));
            Audio->ApplyPlatformTier(*Phone);
            TestEqual(TEXT("The audio takes the tier's voices"), Audio->GetMaxVoices(), Phone->AudioMaxVoices);
        }
        TestEqual(TEXT("The catalog validates"), PSPlatformTiers::ValidateCatalog(Catalog).Num(), 0);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The field layer's hooks (23.3, code half)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAudioFieldHooksTest,
    "PlaySports.Audio.FieldHooks",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAudioFieldHooksTest::RunTest(const FString& Parameters)
{
    PSAudioTests::FAudioFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus and the audio"), Fixture.Setup()))
    {
        return false;
    }
    UPSAudioSubsystem* Audio = Fixture.Audio;
    UPSTelemetryBus* Bus = Fixture.Bus;

    // Linemen engage: a pass-rush move against a blocker.
    FPSTelemetryPassRushEvent Rush;
    Rush.RusherName = TEXT("DT_01");
    Rush.BlockerName = TEXT("OG_01");
    Rush.Move = TEXT("Bull");
    Bus->PublishPassRushMove(Rush);
    TestEqual(TEXT("A rush move sounds the pads engaging"), Audio->CountRequests(TEXT("Field.Pads.Engage")), 1);

    // The cadence: a pre-snap call.
    FPSTelemetryPreSnapEvent Audible;
    Audible.Action = EPSPreSnapAction::Audible;
    Audible.bHumanCall = true;
    Bus->PublishPreSnap(Audible);
    TestEqual(TEXT("An audible is called at the line"), Audio->CountRequests(TEXT("Field.Cadence.Call")), 1);

    // A footstep, as an animation notify asks for it: placed, and scaled by the step's weight.
    const FPSAudioCueDef* Step = Audio->FindCue(TEXT("Field.Footstep.Turf"));
    if (TestNotNull(TEXT("The footstep cue"), Step))
    {
        const FVector Where(1200.f, -300.f, 0.f);
        TestTrue(TEXT("A footstep plays"), Audio->RequestCue(TEXT("Field.Footstep.Turf"), Where, 0.5f));
        FPSAudioCueRequest Request;
        if (Audio->FindLastRequest(TEXT("Field.Footstep.Turf"), Request))
        {
            TestTrue(TEXT("...where the foot landed"), Request.Location.Equals(Where));
            TestTrue(TEXT("...at the step's weight"), FMath::IsNearlyEqual(Request.Volume, Step->Volume * 0.5f));
            TestEqual(TEXT("...asked for by code, not the bus"), Request.Trigger, EPSAudioTrigger::Manual);
        }
    }

    // A catch is placed where it was made.
    FPSTelemetryCatchEvent Catch;
    Catch.ReceiverName = TEXT("WR_01");
    Catch.CatchLocation = FVector(3000.f, 500.f, 0.f);
    Bus->PublishCatch(Catch);
    FPSAudioCueRequest CatchRequest;
    if (TestTrue(TEXT("A catch sounds the ball"), Audio->FindLastRequest(TEXT("Field.Ball.Catch"), CatchRequest)))
    {
        TestTrue(TEXT("...at the catch"), CatchRequest.Location.Equals(Catch.CatchLocation));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The flag's authority
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAudioFlagAuthorityTest,
    "PlaySports.Audio.FlagFromSimulation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAudioFlagAuthorityTest::RunTest(const FString& Parameters)
{
    PSAudioTests::FAudioFixture Fixture;
    if (!TestTrue(TEXT("A world with the bus and the audio"), Fixture.Setup()))
    {
        return false;
    }
    UPSAudioSubsystem* Audio = Fixture.Audio;
    UPSTelemetryBus* Bus = Fixture.Bus;

    TArray<FPSTelemetryPenaltyEvent> Penalties;
    const FDelegateHandle Listening = Bus->OnPenaltyMC.AddLambda([&Penalties](const FPSTelemetryPenaltyEvent& Event) { Penalties.Add(Event); });

    FPlayerAttributes Quarterback;
    Quarterback.PlayerId = TEXT("QB_01");
    Quarterback.DisplayName = TEXT("QB_01");
    Quarterback.Role = EPlayerRole::Quarterback;
    FPlayerAttributes Lineman;
    Lineman.PlayerId = TEXT("DE_01");
    Lineman.DisplayName = TEXT("DE_01");
    Lineman.Role = EPlayerRole::DefensiveLineman;
    UPSPlaySimulation* Simulation = NewObject<UPSPlaySimulation>();
    Simulation->InitializePlay({ Quarterback }, { Lineman });
    Simulation->InitializeWithWorld(Fixture.World);

    // A defender jumps offside: the simulation throws the flag.
    FPSTelemetryJumpSnapEvent Jump;
    Jump.DefenderName = TEXT("DE_01");
    Jump.LeadSeconds = 0.2f;
    Jump.bOffside = true;
    Bus->PublishJumpSnap(Jump);
    if (TestEqual(TEXT("The simulation throws a flag"), Penalties.Num(), 1))
    {
        TestEqual(TEXT("...a flag"), Penalties[0].Kind, EPSPenaltyEventKind::Flag);
        TestEqual(TEXT("...for offsides"), Penalties[0].Penalty, FString(TEXT("Offsides")));
        TestTrue(TEXT("...on the defense"), Penalties[0].bOnDefense);
        TestFalse(TEXT("...the visitors' (the home team has the ball)"), Penalties[0].bHomeTeam);
        TestEqual(TEXT("...naming the jumper"), Penalties[0].PlayerName, FString(TEXT("DE_01")));
    }
    TestEqual(TEXT("The audio hears the flag"), Audio->CountRequests(TEXT("Stinger.Flag")), 1);
    Bus->PublishJumpSnap(Jump);
    TestEqual(TEXT("A second jump with the flag down throws no second flag"), Penalties.Num(), 1);

    // The play is scored: the offense takes the five yards.
    Simulation->EndPlayAndPrepareNext();
    if (TestEqual(TEXT("The flag is settled on the bus"), Penalties.Num(), 2))
    {
        TestEqual(TEXT("...accepted"), Penalties[1].Kind, EPSPenaltyEventKind::Accepted);
        TestEqual(TEXT("...five yards"), Penalties[1].Yards, 5);
    }
    TestEqual(TEXT("The ruling plays no second stinger"), Audio->CountRequests(TEXT("Stinger.Flag"), true), 1);
    FPSTelemetryEvent Recorded;
    TestTrue(TEXT("The flag is in the bus's history"), Bus->FindLatestEventOfType(EPSTelemetryEventType::Penalty, Recorded));
    Bus->OnPenaltyMC.Remove(Listening);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
