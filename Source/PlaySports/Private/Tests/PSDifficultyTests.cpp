// PSDifficultyTests.cpp -- difficulty, assists and fairness (Epic 84)
//
// Tests covered:
//   1. Tiers are AI capability dials: Data/difficulty.json is sound and its tiers are the
//      Difficulty setting's choices in order. With no settings the AI plays as tuned. Each tier
//      scales the CPU's defenders' and quarterback's recognition (slowest at Rookie, fastest at
//      Legend), sets the opponent model's adaptation dial and the CPU passer's scatter. The
//      human's own players stay as tuned, and no rating moves.
//   2. Assists: pass lead (the human's throw leads a crossing receiver; off, it goes at him),
//      auto-slide (his quarterback slides ahead of a tackler past the line, only while it is on
//      and the play is live) and the suggested play's highlight on the play-call screens, drawn
//      in the player's color vision setting.
//   3. No rubber band: a tier plays the same whatever the score.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSCarrierMoveComponent.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSDifficultySubsystem.h"
#include "PSFieldReads.h"
#include "PSOffenseController.h"
#include "PSOpponentModel.h"
#include "PSPassingComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSSettingsSubsystem.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "PSUIAccessibilitySubsystem.h"
#include "PSUIColorAccessibility.h"
#include "PSUITeamCatalog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSDifficultyTests
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

    /** A pawn at Location under its side's AI controller. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location, float Awareness = 60.f)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        FPlayerAttributes Attributes;
        Attributes.PlayerId = FName(PlayerId);
        Attributes.DisplayName = PlayerId;
        Attributes.Role = Role;
        Attributes.Awareness = Awareness;
        Attributes.Speed = 70.f;
        Attributes.Strength = 50.f;
        Pawn->InitializePlayer(Attributes);
        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            if (APSDefenseController* AI = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
            {
                AI->Possess(Pawn);
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
        }
        return Pawn;
    }

    static APSPlayerController* SpawnPlayerController(UWorld* World)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<APSPlayerController>(APSPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    }

    /** The player's settings, as a game instance would hold them (nothing saved). */
    static UPSSettingsSubsystem* MakeSettings()
    {
        return NewObject<UPSSettingsSubsystem>(NewObject<UGameInstance>());
    }

    static void GiveBall(UWorld* World, APSPlayerPawn* Carrier)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), Carrier->GetActorLocation(), FRotator::ZeroRotator, SpawnParams))
        {
            Ball->AttachToCarrier(Carrier, TEXT("HandSocket"));
            Carrier->GainPossession();
        }
    }

    static bool SameTuning(const UScriptStruct* Struct, const void* A, const void* B)
    {
        return Struct->CompareScriptStruct(A, B, PPF_None);
    }

    static bool SameRatings(const FPlayerAttributes& A, const FPlayerAttributes& B)
    {
        return A.Speed == B.Speed && A.Agility == B.Agility && A.Strength == B.Strength && A.Acceleration == B.Acceleration
            && A.Awareness == B.Awareness;
    }

    static int32 CountAccented(const TArray<FPSMenuOptionDef>& Options, const FLinearColor& Accent, FName& OutPayload)
    {
        int32 Count = 0;
        for (const FPSMenuOptionDef& Option : Options)
        {
            if (Option.AccentColor.A > 0.f)
            {
                ++Count;
                OutPayload = Option.AccentColor.Equals(Accent) ? Option.Payload : NAME_None;
            }
        }
        return Count;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- Tiers are AI capability dials
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDifficultyTiersTest,
    "PlaySports.AI.Difficulty.TiersAreCapabilityDials",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDifficultyTiersTest::RunTest(const FString& Parameters)
{
    using namespace PSDifficultyTests;

    UWorld* World = CreateTestWorld();
    UPSDifficultySubsystem* Difficulty = UPSDifficultySubsystem::Get(World);
    UPSOpponentModel* Model = World ? World->GetSubsystem<UPSOpponentModel>() : nullptr;
    APSPlayerController* Human = World ? SpawnPlayerController(World) : nullptr;
    APSPlayerPawn* QB = World ? SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f)) : nullptr;
    APSPlayerPawn* WR = World ? SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(500.f, 900.f, 100.f)) : nullptr;
    APSPlayerPawn* CB = World ? SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB"), FVector(700.f, 900.f, 100.f)) : nullptr;
    APSOffenseController* QBAI = QB ? Cast<APSOffenseController>(QB->GetController()) : nullptr;
    APSOffenseController* WRAI = WR ? Cast<APSOffenseController>(WR->GetController()) : nullptr;
    APSDefenseController* CBAI = CB ? Cast<APSDefenseController>(CB->GetController()) : nullptr;
    if (!TestNotNull(TEXT("Difficulty"), Difficulty) || !TestNotNull(TEXT("Opponent model"), Model) || !TestNotNull(TEXT("Controller"), Human)
        || !TestNotNull(TEXT("QB AI"), QBAI) || !TestNotNull(TEXT("WR AI"), WRAI) || !TestNotNull(TEXT("CB AI"), CBAI))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }

    // The data: sound, and one tier per choice of the Difficulty setting, in order.
    const FPSDifficultyCatalog& Catalog = Difficulty->GetCatalog();
    for (const FString& Problem : PSDifficulty::ValidateCatalog(Catalog))
    {
        AddError(FString::Printf(TEXT("difficulty.json: %s"), *Problem));
    }
    UPSSettingsSubsystem* Settings = MakeSettings();
    const FPSSettingDef* Setting = Settings->GetCatalog().FindSetting(Catalog.DifficultySetting);
    if (!TestNotNull(TEXT("The Difficulty setting"), Setting) || !TestEqual(TEXT("One tier per choice"), Catalog.DifficultyTiers.Num(), Setting->Choices.Num())
        || !TestTrue(TEXT("At least two tiers"), Catalog.DifficultyTiers.Num() >= 2))
    {
        DestroyTestWorld(World);
        return false;
    }
    for (int32 Index = 0; Index < Setting->Choices.Num(); ++Index)
    {
        TestEqual(*FString::Printf(TEXT("Tier %d is the setting's choice %d"), Index, Index), Catalog.DifficultyTiers[Index].Label, Setting->Choices[Index]);
    }

    UPSSkillPlayerAIComponent* QuarterbackAI = QBAI->GetSkillAI();
    UPSSkillPlayerAIComponent* ReceiverAI = WRAI->GetSkillAI();
    UPSDefenderAIComponent* CornerAI = CBAI->GetDefenderAI();
    const FSkillPlayerAITuningRow BaseSkill = ReceiverAI->GetTuning();
    const FDefenderAITuningRow BaseDefense = CornerAI->GetTuning();
    const FPlayerAttributes CornerRatings = CB->GetAttributes();
    const FPlayerAttributes PasserRatings = QB->GetAttributes();

    // The human has the offense; the CPU defends.
    TestTrue(TEXT("The human takes the QB"), Human->TakeControlOf(QB));
    TestTrue(TEXT("The corner plays for the CPU"), Difficulty->IsCpuPlayer(CB));
    TestFalse(TEXT("...the human's own receiver doesn't"), Difficulty->IsCpuPlayer(WR));

    // No settings (a headless world): no tier, the AI as tuned.
    TestNull(TEXT("No settings, no tier"), Difficulty->GetActiveTier());
    CornerAI->ApplyPlayTuning(CB);
    TestTrue(TEXT("...the CPU's corner plays as tuned"), SameTuning(FDefenderAITuningRow::StaticStruct(), &CornerAI->GetTuning(), &BaseDefense));
    TestTrue(TEXT("...the model adapts at its own default"),
        FMath::IsNearlyEqual(Model->GetAdaptationDial(), FMath::Clamp(Model->GetTuning().DefaultAdaptationDial, 0.f, 1.f)));

    // The player's settings: the default tier first.
    Difficulty->SetSettings(Settings);
    const FPSDifficultyTier* Default = Difficulty->GetActiveTier();
    TestTrue(TEXT("With settings, the setting's default tier"), Default && Default->Label == Setting->Choices[FMath::RoundToInt(Setting->Default)]);

    TArray<float> Reactions, Anticipations, Dials, Scatters;
    for (int32 Index = 0; Index < Catalog.DifficultyTiers.Num(); ++Index)
    {
        const FPSDifficultyTier& Expected = Catalog.DifficultyTiers[Index];
        Settings->SetValue(Catalog.DifficultySetting, static_cast<float>(Index));
        const FPSDifficultyTier* Tier = Difficulty->GetActiveTier();
        if (!TestTrue(*FString::Printf(TEXT("Choice %d picks %s"), Index, *Expected.Label), Tier && Tier->TierId == Expected.TierId))
        {
            continue;
        }

        // The CPU's corner: his recognition, scaled by the tier's dials and nothing else.
        CornerAI->ApplyPlayTuning(CB);
        FDefenderAITuningRow ScaledDefense = BaseDefense;
        PSDifficulty::ApplyScales(*Tier, TEXT("DefenderAI"), FDefenderAITuningRow::StaticStruct(), &ScaledDefense);
        TestTrue(*FString::Printf(TEXT("%s: the CPU's corner plays the tier's dials"), *Tier->Label),
            SameTuning(FDefenderAITuningRow::StaticStruct(), &CornerAI->GetTuning(), &ScaledDefense));
        Reactions.Add(CornerAI->GetTuning().MaxReactionSeconds);

        // The human's receiver: as tuned at every tier.
        ReceiverAI->ApplyPlayTuning(WR);
        TestTrue(*FString::Printf(TEXT("%s: the human's own receiver plays as tuned"), *Tier->Label),
            SameTuning(FSkillPlayerAITuningRow::StaticStruct(), &ReceiverAI->GetTuning(), &BaseSkill));
        TestEqual(*FString::Printf(TEXT("%s: the human passer's throws scatter by his Awareness alone"), *Tier->Label), Difficulty->GetThrowScatterScale(QB), 1.f);

        // Adaptation: the tier's dial, unless something set the model's own.
        TestTrue(*FString::Printf(TEXT("%s: the model adapts at the tier's dial"), *Tier->Label), FMath::IsNearlyEqual(Model->GetAdaptationDial(), Tier->AdaptationDial));
        Dials.Add(Model->GetAdaptationDial());
    }
    Model->SetAdaptationDial(0.25f);
    TestTrue(TEXT("A dial set on the model outranks the tier's"), FMath::IsNearlyEqual(Model->GetAdaptationDial(), 0.25f));

    // The human switches to the corner: the CPU now has the offense, and its quarterback reads
    // at the tier while the human's corner plays as tuned.
    TestTrue(TEXT("The human takes the corner"), Human->TakeControlOf(CB));
    TestTrue(TEXT("The QB is the CPU's again"), Difficulty->IsCpuPlayer(QB) && !Difficulty->IsCpuPlayer(CB));
    for (int32 Index = 0; Index < Catalog.DifficultyTiers.Num(); ++Index)
    {
        Settings->SetValue(Catalog.DifficultySetting, static_cast<float>(Index));
        const FPSDifficultyTier* Tier = Difficulty->GetActiveTier();
        if (!Tier)
        {
            continue;
        }
        QuarterbackAI->ApplyPlayTuning(QB);
        FSkillPlayerAITuningRow ScaledSkill = BaseSkill;
        PSDifficulty::ApplyScales(*Tier, TEXT("SkillAI"), FSkillPlayerAITuningRow::StaticStruct(), &ScaledSkill);
        TestTrue(*FString::Printf(TEXT("%s: the CPU's quarterback plays the tier's dials"), *Tier->Label),
            SameTuning(FSkillPlayerAITuningRow::StaticStruct(), &QuarterbackAI->GetTuning(), &ScaledSkill));
        Anticipations.Add(QuarterbackAI->GetTuning().MaxAnticipationSeconds);
        TestEqual(*FString::Printf(TEXT("%s: the CPU passer's scatter is the tier's"), *Tier->Label), Difficulty->GetThrowScatterScale(QB), Tier->ThrowScatterScale);
        Scatters.Add(Difficulty->GetThrowScatterScale(QB));
        CornerAI->ApplyPlayTuning(CB);
        TestTrue(*FString::Printf(TEXT("%s: the human's corner plays as tuned"), *Tier->Label),
            SameTuning(FDefenderAITuningRow::StaticStruct(), &CornerAI->GetTuning(), &BaseDefense));
    }

    // Harder tiers recognise faster, adapt more and throw truer: never easier than the one before.
    if (TestEqual(TEXT("Every tier was read"), Reactions.Num() + Anticipations.Num() + Dials.Num() + Scatters.Num(), 4 * Catalog.DifficultyTiers.Num()))
    {
        bool bOrdered = true;
        for (int32 Index = 1; Index < Reactions.Num(); ++Index)
        {
            bOrdered &= Reactions[Index] <= Reactions[Index - 1] && Anticipations[Index] >= Anticipations[Index - 1]
                && Dials[Index] >= Dials[Index - 1] && Scatters[Index] <= Scatters[Index - 1];
        }
        TestTrue(TEXT("Each tier is at least as sharp as the one before"), bOrdered);
        TestTrue(TEXT("...and the hardest reacts faster than the easiest"), Reactions.Last() < Reactions[0]);
        TestTrue(TEXT("...anticipates further"), Anticipations.Last() > Anticipations[0]);
        TestTrue(TEXT("...adapts more"), Dials.Last() > Dials[0]);
        TestTrue(TEXT("...and throws truer"), Scatters.Last() < Scatters[0]);
    }

    // Not stat inflation: no rating moved at any tier.
    TestTrue(TEXT("The corner's ratings never moved"), SameRatings(CB->GetAttributes(), CornerRatings));
    TestTrue(TEXT("...nor the quarterback's"), SameRatings(QB->GetAttributes(), PasserRatings));

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Assists: pass lead, auto-slide, the suggested play
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDifficultyAssistsTest,
    "PlaySports.AI.Difficulty.Assists",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDifficultyAssistsTest::RunTest(const FString& Parameters)
{
    using namespace PSDifficultyTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSDifficultySubsystem* Difficulty = UPSDifficultySubsystem::Get(World);
    APSPlayerController* Human = World ? SpawnPlayerController(World) : nullptr;
    // An accurate passer (Awareness 100: no scatter) and a receiver crossing the field.
    APSPlayerPawn* QB = World ? SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f), 100.f) : nullptr;
    APSPlayerPawn* WR = World ? SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR"), FVector(1000.f, 0.f, 100.f)) : nullptr;
    APSPlayerPawn* LB = World ? SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB"), FVector(3000.f, 3000.f, 100.f)) : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Play-call subsystem"), PlayCall) || !TestNotNull(TEXT("Difficulty"), Difficulty)
        || !TestNotNull(TEXT("Controller"), Human) || !TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("WR"), WR) || !TestNotNull(TEXT("LB"), LB))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    const FPSDifficultyCatalog& Catalog = Difficulty->GetCatalog();
    UPSSettingsSubsystem* Settings = MakeSettings();
    TestTrue(TEXT("The human takes the QB"), Human->TakeControlOf(QB));

    // With no settings each assist is at its setting's default.
    auto DefaultOn = [Settings](FName SettingId)
    {
        const FPSSettingDef* Def = Settings->GetCatalog().FindSetting(SettingId);
        return Def && Def->Default >= 0.5f;
    };
    for (const FName SettingId : { Catalog.PassLeadSetting, Catalog.AutoSlideSetting, Catalog.SuggestedPlaySetting })
    {
        TestNotNull(*FString::Printf(TEXT("%s is a setting"), *SettingId.ToString()), Settings->GetCatalog().FindSetting(SettingId));
    }
    TestTrue(TEXT("No settings: pass lead at its default"), Difficulty->IsPassLeadOn() == DefaultOn(Catalog.PassLeadSetting));
    TestTrue(TEXT("...auto-slide at its"), Difficulty->IsAutoSlideOn() == DefaultOn(Catalog.AutoSlideSetting));
    TestTrue(TEXT("...the highlight at its"), Difficulty->IsSuggestedPlayHighlightOn() == DefaultOn(Catalog.SuggestedPlaySetting));
    Difficulty->SetSettings(Settings);

    // --- Pass lead ---
    TArray<FPSTelemetryThrowEvent> Throws;
    const FDelegateHandle ThrowHandle = Bus->OnThrowMC.AddLambda([&Throws](const FPSTelemetryThrowEvent& Event) { Throws.Add(Event); });
    UPSPassingComponent* Passing = Human->GetPassingComponent();
    WR->GetFloatingMovementComponent()->Velocity = FVector(0.f, 600.f, 0.f);
    const FVector Lead = PSFieldReads::LeadPoint(QB->GetActorLocation(), WR, Passing->GetTuning().LeadSpeed);
    TestTrue(TEXT("The crossing receiver's lead point is ahead of him"), Lead.Y > WR->GetActorLocation().Y + 10.f);

    Settings->SetValue(Catalog.PassLeadSetting, 1.f);
    GiveBall(World, QB);
    TestTrue(TEXT("A throw"), Passing->ThrowToSlot(0, 0.f, FVector2D::ZeroVector));
    Settings->SetValue(Catalog.PassLeadSetting, 0.f);
    GiveBall(World, QB);
    TestTrue(TEXT("Another throw, the assist off"), Passing->ThrowToSlot(0, 0.f, FVector2D::ZeroVector));
    Settings->SetValue(Catalog.PassLeadSetting, 1.f);
    GiveBall(World, QB);
    TestTrue(TEXT("A third, the assist on"), Passing->ThrowToSlot(0, 0.f, FVector2D::ZeroVector));
    if (TestEqual(TEXT("Three throws"), Throws.Num(), 3))
    {
        TestTrue(TEXT("The assist leads the receiver"), Throws[0].TargetLocation.Equals(Lead, 1.f));
        TestTrue(TEXT("...off, the ball goes at him and the stick does the leading"), Throws[1].TargetLocation.Equals(WR->GetActorLocation(), 1.f));
        TestTrue(TEXT("...on again, it leads him"), Throws[2].TargetLocation.Equals(Lead, 1.f));
    }
    Bus->OnThrowMC.Remove(ThrowHandle);

    // --- Auto-slide ---
    TArray<FPSTelemetryPocketEvent> Slides;
    const FDelegateHandle PocketHandle = Bus->OnPocketMC.AddLambda([&Slides](const FPSTelemetryPocketEvent& Event)
    {
        if (Event.Kind == EPSPocketEventKind::Slide)
        {
            Slides.Add(Event);
        }
    });
    // The CPU quarterback's slide read: past the line by SlideMinGain, a tackler ahead within
    // SlideTriggerRadius.
    FPocketTuningRow Pocket;
    TestTrue(TEXT("The pocket tuning loads"), NewObject<UPSDataIngestion>()->LoadPocketTuningFromJson(UPSPocketComponent::GetDefaultTuningPath(), Pocket));
    const float Gained = Pocket.SlideMinGain + 200.f;
    const float Close = Pocket.SlideTriggerRadius * 0.5f;
    GiveBall(World, QB);
    QB->SetActorLocation(FVector(Gained, 0.f, 100.f));
    LB->SetActorLocation(FVector(Gained + Close, 0.f, 100.f));
    Settings->SetValue(Catalog.AutoSlideSetting, 1.f);
    TestFalse(TEXT("No slide before the snap"), Difficulty->UpdateAutoSlide());

    FPSTelemetrySnapEvent Snap;
    Snap.LineOfScrimmage = FVector::ZeroVector;
    Bus->PublishSnap(Snap);
    Settings->SetValue(Catalog.AutoSlideSetting, 0.f);
    TestFalse(TEXT("Auto-slide off: he doesn't slide"), Difficulty->UpdateAutoSlide());
    Settings->SetValue(Catalog.AutoSlideSetting, 1.f);
    QB->SetActorLocation(FVector(-100.f, 0.f, 100.f));
    LB->SetActorLocation(FVector(-100.f + Close, 0.f, 100.f));
    TestFalse(TEXT("On, behind the line he doesn't"), Difficulty->UpdateAutoSlide());
    QB->SetActorLocation(FVector(Gained, 0.f, 100.f));
    LB->SetActorLocation(FVector(Gained - Close, 0.f, 100.f));
    TestFalse(TEXT("...nor past it with the tackler behind him"), Difficulty->UpdateAutoSlide());
    LB->SetActorLocation(FVector(Gained + Close, 0.f, 100.f));
    TestTrue(TEXT("...past it with a tackler closing ahead, he slides"), Difficulty->UpdateAutoSlide());
    if (TestEqual(TEXT("One slide on the bus"), Slides.Num(), 1))
    {
        TestEqual(TEXT("...by the human's QB"), Slides[0].PasserName, FString(TEXT("QB")));
        TestEqual(TEXT("...ahead of the linebacker"), Slides[0].DefenderName, FString(TEXT("LB")));
    }
    TestTrue(TEXT("The slide gives himself up"), QB->GetCarrierMoveComponent() && QB->GetCarrierMoveComponent()->HasGivenUp());
    TestFalse(TEXT("...once"), Difficulty->UpdateAutoSlide());
    Bus->OnPocketMC.Remove(PocketHandle);

    // --- The suggested play ---
    FPSSituationContext Situation;
    Situation.Down = 3;
    Situation.Distance = 2;
    Situation.YardLine = 45;
    PlayCall->OpenPlayCall(Situation);
    const TArray<FPSPlaySuggestion> Ranked = PlayCall->RankPlays(true);
    FPSPlayDefinition Suggested;
    if (!TestTrue(TEXT("The coach suggests a play"), Ranked.Num() > 0 && PlayCall->FindPlay(Ranked[0].PlayId, Suggested)))
    {
        DestroyTestWorld(World);
        return false;
    }
    Settings->SetValue(Catalog.SuggestedPlaySetting, 1.f);
    const FLinearColor Accent = Difficulty->GetSuggestedPlayAccent();
    TestTrue(TEXT("The highlight has a color"), Accent.A > 0.f);
    FName Payload;
    TestEqual(TEXT("One play is highlighted in its formation"), CountAccented(PlayCall->BuildPlayOptions(Suggested.Formation, true), Accent, Payload), 1);
    TestEqual(TEXT("...the suggested one"), Payload, Suggested.PlayId);
    TestEqual(TEXT("One formation is highlighted"), CountAccented(PlayCall->BuildFormationOptions(true, NAME_None), Accent, Payload), 1);
    TestEqual(TEXT("...the suggested play's"), Payload, FName(*Suggested.Formation));
    for (const FString& Formation : PlayCall->GetFormations(true))
    {
        if (Formation != Suggested.Formation)
        {
            TestEqual(TEXT("No play in another formation is highlighted"), CountAccented(PlayCall->BuildPlayOptions(Formation, true), Accent, Payload), 0);
            break;
        }
    }

    // In the player's color vision.
    const FName ColorblindSetting = GetDefault<UPSUIAccessibilitySubsystem>()->ColorblindSettingId;
    Settings->SetValue(ColorblindSetting, static_cast<float>(static_cast<uint8>(EPSColorblindMode::Deuteranopia)));
    const EPSColorblindMode Mode = UPSUIAccessibilitySubsystem::GetColorblindMode(Settings);
    FLinearColor Base;
    TestTrue(TEXT("The accent parses"), UPSUITeamCatalog::ParseHexColor(Catalog.SuggestedPlayAccent, Base));
    TestTrue(TEXT("Color vision is set"), Mode == EPSColorblindMode::Deuteranopia);
    const FLinearColor Resolved = Difficulty->GetSuggestedPlayAccent();
    TestTrue(TEXT("The highlight is drawn for it"), Resolved.Equals(UPSUIColorLibrary::ResolveColor(Base, Mode)));
    TestEqual(TEXT("...on the play-call screen too"), CountAccented(PlayCall->BuildPlayOptions(Suggested.Formation, true), Resolved, Payload), 1);
    TestEqual(TEXT("...the suggested play"), Payload, Suggested.PlayId);

    Settings->SetValue(Catalog.SuggestedPlaySetting, 0.f);
    TestEqual(TEXT("Highlight off: no color"), Difficulty->GetSuggestedPlayAccent().A, 0.f);
    TestEqual(TEXT("...and nothing highlighted"), CountAccented(PlayCall->BuildPlayOptions(Suggested.Formation, true), Accent, Payload), 0);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- No rubber band
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSDifficultyNoRubberBandTest,
    "PlaySports.AI.Difficulty.NoRubberBand",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSDifficultyNoRubberBandTest::RunTest(const FString& Parameters)
{
    using namespace PSDifficultyTests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    UPSDifficultySubsystem* Difficulty = UPSDifficultySubsystem::Get(World);
    UPSOpponentModel* Model = World ? World->GetSubsystem<UPSOpponentModel>() : nullptr;
    APSPlayerController* Human = World ? SpawnPlayerController(World) : nullptr;
    APSPlayerPawn* QB = World ? SpawnPlayer(World, EPlayerRole::Quarterback, TEXT("QB"), FVector(-300.f, 0.f, 100.f)) : nullptr;
    APSPlayerPawn* CB = World ? SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB"), FVector(700.f, 900.f, 100.f)) : nullptr;
    APSDefenseController* CBAI = CB ? Cast<APSDefenseController>(CB->GetController()) : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("Difficulty"), Difficulty) || !TestNotNull(TEXT("Opponent model"), Model)
        || !TestNotNull(TEXT("Controller"), Human) || !TestNotNull(TEXT("QB"), QB) || !TestNotNull(TEXT("CB AI"), CBAI))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    UPSSettingsSubsystem* Settings = MakeSettings();
    Difficulty->SetSettings(Settings);
    TestTrue(TEXT("The human takes the QB"), Human->TakeControlOf(QB));
    UPSDefenderAIComponent* CornerAI = CBAI->GetDefenderAI();

    // At every tier, the CPU plays the same leading or trailing by four scores.
    const FPSDifficultyCatalog& Catalog = Difficulty->GetCatalog();
    for (int32 Index = 0; Index < Catalog.DifficultyTiers.Num(); ++Index)
    {
        Settings->SetValue(Catalog.DifficultySetting, static_cast<float>(Index));
        CornerAI->ApplyPlayTuning(CB);
        const FDefenderAITuningRow Level = CornerAI->GetTuning();
        const float Strength = Model->GetAdaptationStrength(4);
        for (const TPair<int32, int32>& Score : { TPair<int32, int32>(35, 0), TPair<int32, int32>(0, 35), TPair<int32, int32>(21, 21) })
        {
            FPSTelemetryScoreEvent Event;
            Event.ScoreType = TEXT("Touchdown");
            Event.HomeScore = Score.Key;
            Event.AwayScore = Score.Value;
            Bus->PublishScore(Event);
            CornerAI->ApplyPlayTuning(CB);
            const FString Where = FString::Printf(TEXT("%s at %d-%d"), *Catalog.DifficultyTiers[Index].Label, Score.Key, Score.Value);
            TestTrue(*FString::Printf(TEXT("%s: the CPU's corner reads the same"), *Where), SameTuning(FDefenderAITuningRow::StaticStruct(), &CornerAI->GetTuning(), &Level));
            TestTrue(*FString::Printf(TEXT("%s: the CPU adapts the same"), *Where), FMath::IsNearlyEqual(Model->GetAdaptationStrength(4), Strength));
        }
    }

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
