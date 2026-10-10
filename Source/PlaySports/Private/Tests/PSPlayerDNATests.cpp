// PSPlayerDNATests.cpp -- player DNA and individual tendency profiles (Epic 79)
//
// Tests covered:
//   1. The catalog: the shipped one loads and is sound, has the roadmap's three style axes, and
//      its mistakes are reported; a binding scales its field by the axis (neutral changes
//      nothing, an axis of another role changes nothing); a roster's DNA survives ingestion.
//   2. Scrambler and pocket passer, rated alike: with a rusher closing, the scrambler leaves the
//      pocket while the pocket passer stands in; out of time with a man half open, the pocket
//      passer throws and the scrambler runs.
//   3. Power and finesse rushers, rated alike, against the same blocker pick a power move and a
//      finesse move.
//   4. Ball hawk and blanket cover, rated alike: the ball hawk bites longer on a pump fake and
//      breaks on a throw that the blanket corner, further from it than his radius, stays off.
//   5. Power and elusive backs, rated alike: with a tackler closing, the elusive back cuts away
//      and the power back keeps going upfield.
//   6. Scouting traits: the traits a scout sees, named through the string tables, strongest
//      first, only past the threshold and only for the player's own role.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSOffenseController.h"
#include "PSPlayerDNA.h"
#include "PSPlayerPawn.h"
#include "PSPocketComponent.h"
#include "PSRouteRunnerComponent.h"
#include "PSRushMoveComponent.h"
#include "PSSkillPlayerAIComponent.h"
#include "PSTelemetryBus.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSPlayerDNATests
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

    /** A player rated Rating at everything, with full stamina and the given DNA. */
    static FPlayerAttributes MakePlayer(EPlayerRole Role, const TCHAR* PlayerId, const FPSPlayerDNA& DNA = FPSPlayerDNA(), float Rating = 70.f)
    {
        FPlayerAttributes Attributes;
        Attributes.PlayerId = FName(PlayerId);
        Attributes.DisplayName = PlayerId;
        Attributes.Role = Role;
        Attributes.Speed = Rating;
        Attributes.Agility = Rating;
        Attributes.Strength = Rating;
        Attributes.Acceleration = Rating;
        Attributes.Awareness = Rating;
        Attributes.Stamina = 100.f;
        Attributes.DNA = DNA;
        return Attributes;
    }

    static FPSPlayerDNA WithAxis(FName Axis, float Value)
    {
        FPSPlayerDNA DNA;
        if (FFloatProperty* Property = FindFProperty<FFloatProperty>(FPSPlayerDNA::StaticStruct(), Axis))
        {
            Property->SetPropertyValue_InContainer(&DNA, Value);
        }
        return DNA;
    }

    /** A pawn at Location under its side's AI controller, bound to the bus. */
    static APSPlayerPawn* SpawnPlayer(UWorld* World, const FPlayerAttributes& Attributes, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (!Pawn)
        {
            return nullptr;
        }
        Pawn->InitializePlayer(Attributes);
        if (Pawn->TeamSide == EPSTeamSide::Defense)
        {
            if (APSDefenseController* AI = World->SpawnActor<APSDefenseController>(APSDefenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
            {
                AI->Possess(Pawn);
                AI->GetDefenderAI()->BindToBus();
            }
        }
        else if (APSOffenseController* AI = World->SpawnActor<APSOffenseController>(APSOffenseController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
        {
            AI->Possess(Pawn);
            AI->GetSkillAI()->BindToBus();
        }
        return Pawn;
    }

    static APSOffenseController* OffenseOf(const APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSOffenseController>(Pawn->GetController()) : nullptr;
    }

    static APSDefenseController* DefenseOf(const APSPlayerPawn* Pawn)
    {
        return Pawn ? Cast<APSDefenseController>(Pawn->GetController()) : nullptr;
    }

    static APSBall* GiveBall(UWorld* World, APSPlayerPawn* Carrier)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), Carrier->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);
        if (Ball)
        {
            Ball->AttachToCarrier(Carrier, TEXT("HandSocket"));
            Carrier->GainPossession();
        }
        return Ball;
    }

    /** The snap at the origin (the play-call subsystem hands out a CPU play on it), every
     *  offensive AI's route cleared so the test sets its own, and a pass call. */
    static void StartPassPlay(UWorld* World, UPSTelemetryBus* Bus)
    {
        FPSTelemetrySnapEvent Snap;
        Bus->PublishSnap(Snap);
        for (TActorIterator<APSOffenseController> It(World); It; ++It)
        {
            It->SetAssignedRoute(TArray<FVector>());
            It->GetRouteRunner()->ClearRoutePlan();
        }
        FPSTelemetryPlayCallEvent Call;
        Call.bOffense = true;
        Call.PlayCategory = TEXT("ShortPass");
        Bus->PublishPlayCall(Call);
    }

    static FPSPlayerDNACatalog LoadShippedCatalog()
    {
        FPSPlayerDNACatalog Catalog;
        NewObject<UPSDataIngestion>()->LoadPlayerDNACatalogFromJson(UPSPlayerDNASubsystem::GetDefaultCatalogPath(), Catalog);
        return Catalog;
    }

    /** The shipped tuning Row with Player's DNA applied, as his component will play it. */
    template <typename TRow>
    static TRow WithDNA(const FPSPlayerDNACatalog& Catalog, const TRow& Row, const FPlayerAttributes& Player, const TCHAR* Target)
    {
        TRow Scaled = Row;
        PSPlayerDNA::ApplyBindings(Catalog, Player, FName(Target), TRow::StaticStruct(), &Scaled);
        return Scaled;
    }

    static const FPSDNARushMoveLean* FindLean(const FPSPlayerDNACatalog& Catalog, EPSRushMove Move)
    {
        return Catalog.RushMoveLeans.FindByPredicate([Move](const FPSDNARushMoveLean& Lean) { return Lean.Move == Move; });
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The catalog and the bindings
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayerDNACatalogTest,
    "PlaySports.AI.DNA.CatalogAndBindings",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayerDNACatalogTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayerDNATests;

    FPSPlayerDNACatalog Catalog;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (!TestTrue(TEXT("The shipped catalog loads"), Ingestion->LoadPlayerDNACatalogFromJson(UPSPlayerDNASubsystem::GetDefaultCatalogPath(), Catalog)))
    {
        return false;
    }
    const TArray<FString> Problems = PSPlayerDNA::ValidateCatalog(Catalog);
    TestEqual(TEXT("...and is sound"), Problems.Num(), 0);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }

    // The roadmap's three: scrambler vs. statue QB, finesse vs. power rusher, ball hawk vs. blanket DB.
    const FPSDNAAxisDef* Mobility = PSPlayerDNA::FindAxis(Catalog, TEXT("Mobility"));
    const FPSDNAAxisDef* RushPower = PSPlayerDNA::FindAxis(Catalog, TEXT("RushPower"));
    const FPSDNAAxisDef* BallHawk = PSPlayerDNA::FindAxis(Catalog, TEXT("BallHawk"));
    TestTrue(TEXT("Quarterbacks run from pocket passer to scrambler"), Mobility && Mobility->Roles.Contains(EPlayerRole::Quarterback)
        && Mobility->LowTrait == TEXT("PocketPasser") && Mobility->HighTrait == TEXT("Scrambler"));
    TestTrue(TEXT("Pass rushers run from finesse to power"), RushPower && RushPower->Roles.Contains(EPlayerRole::DefensiveLineman)
        && RushPower->LowTrait == TEXT("FinesseRusher") && RushPower->HighTrait == TEXT("PowerRusher"));
    TestTrue(TEXT("Defensive backs run from blanket cover to ball hawk"), BallHawk && BallHawk->Roles.Contains(EPlayerRole::DefensiveBack)
        && BallHawk->LowTrait == TEXT("BlanketCover") && BallHawk->HighTrait == TEXT("BallHawk"));

    // A binding's scale: 1 at neutral, its ends at the axis's ends, linear between.
    FPSDNABinding Binding;
    Binding.AtLow = 0.5f;
    Binding.AtHigh = 2.f;
    TestEqual(TEXT("Neutral scales by 1"), PSPlayerDNA::BindingScale(Binding, 0.f), 1.f);
    TestEqual(TEXT("+1 scales by AtHigh"), PSPlayerDNA::BindingScale(Binding, 1.f), 2.f);
    TestEqual(TEXT("-1 scales by AtLow"), PSPlayerDNA::BindingScale(Binding, -1.f), 0.5f);
    TestEqual(TEXT("Half way, half the change"), PSPlayerDNA::BindingScale(Binding, 0.5f), 1.5f);
    TestEqual(TEXT("Past the end is the end"), PSPlayerDNA::BindingScale(Binding, 3.f), 2.f);

    // Applying: a neutral player and an axis of another role change nothing.
    FPocketTuningRow Pocket;
    Ingestion->LoadPocketTuningFromJson(UPSPocketComponent::GetDefaultTuningPath(), Pocket);
    const FPSDNABinding* Collapse = Catalog.Bindings.FindByPredicate([](const FPSDNABinding& Entry)
    {
        return Entry.Axis == TEXT("Mobility") && Entry.Target == TEXT("Pocket") && Entry.Field == TEXT("CollapsePressure");
    });
    if (TestNotNull(TEXT("Mobility binds the pocket's collapse"), Collapse))
    {
        FPocketTuningRow Scaled = Pocket;
        TestEqual(TEXT("A neutral quarterback changes nothing"),
            PSPlayerDNA::ApplyBindings(Catalog, MakePlayer(EPlayerRole::Quarterback, TEXT("QB")), TEXT("Pocket"), FPocketTuningRow::StaticStruct(), &Scaled), 0);
        TestEqual(TEXT("...his collapse as loaded"), Scaled.CollapsePressure, Pocket.CollapsePressure);
        TestEqual(TEXT("Mobility means nothing to a defensive back"),
            PSPlayerDNA::ApplyBindings(Catalog, MakePlayer(EPlayerRole::DefensiveBack, TEXT("DB"), WithAxis(TEXT("Mobility"), 1.f)), TEXT("Pocket"), FPocketTuningRow::StaticStruct(), &Scaled), 0);
        const int32 Applied = PSPlayerDNA::ApplyBindings(Catalog, MakePlayer(EPlayerRole::Quarterback, TEXT("QB"), WithAxis(TEXT("Mobility"), 1.f)),
            TEXT("Pocket"), FPocketTuningRow::StaticStruct(), &Scaled);
        TestTrue(TEXT("A scrambler's pocket is rescaled"), Applied > 0);
        TestTrue(TEXT("...his collapse scaled by the binding's high end"), FMath::IsNearlyEqual(Scaled.CollapsePressure, Pocket.CollapsePressure * Collapse->AtHigh, 0.001f));
    }

    // Mistakes are reported.
    FPSPlayerDNACatalog Broken = Catalog;
    Broken.Axes[0].Axis = TEXT("Clutch");
    Broken.Axes[1].LowTrait = Broken.Axes[2].HighTrait;
    FPSDNABinding Unknown;
    Unknown.Axis = TEXT("Mobility");
    Unknown.Target = TEXT("Kicking");
    Unknown.Field = TEXT("Power");
    Broken.Bindings.Add(Unknown);
    Broken.Bindings[0].Field = TEXT("NotAField");
    Broken.Bindings[1].AtLow = 0.f;
    Broken.RushStyleWeight = 1.f;
    const FString Reported = FString::Join(PSPlayerDNA::ValidateCatalog(Broken), TEXT("\n"));
    TestTrue(TEXT("An axis FPSPlayerDNA doesn't have"), Reported.Contains(TEXT("'Clutch': not an FPSPlayerDNA axis")));
    TestTrue(TEXT("A trait used twice"), Reported.Contains(TEXT("is used twice")));
    TestTrue(TEXT("An unknown target"), Reported.Contains(TEXT("unknown target")));
    TestTrue(TEXT("A field that isn't a float of its target"), Reported.Contains(TEXT("NotAField: not a float field")));
    TestTrue(TEXT("A multiplier not above 0"), Reported.Contains(TEXT("AtLow and AtHigh must be above 0")));
    TestTrue(TEXT("A rush style weight of 1"), Reported.Contains(TEXT("RushStyleWeight")));

    // The in-game roster's DNA survives ingestion, axis by axis.
    const FString RosterPath = FPaths::ProjectDir() / TEXT("Data/sample_players.json");
    UDataTable* Roster = NewObject<UDataTable>();
    Roster->RowStruct = FPlayerAttributes::StaticStruct();
    FString Json;
    TSharedPtr<FJsonObject> Parsed;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (TestTrue(TEXT("The in-game roster loads"), Ingestion->LoadPlayerAttributesFromJson(RosterPath, Roster))
        && FFileHelper::LoadFileToString(Json, *RosterPath) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Parsed) && Parsed.IsValid()
        && Parsed->TryGetArrayField(TEXT("Players"), Rows))
    {
        int32 Compared = 0;
        for (const TSharedPtr<FJsonValue>& Row : *Rows)
        {
            const TSharedPtr<FJsonObject> Object = Row->AsObject();
            const TSharedPtr<FJsonObject>* DNA = nullptr;
            if (!Object.IsValid() || !Object->TryGetObjectField(TEXT("DNA"), DNA))
            {
                continue;
            }
            const FPlayerAttributes* Loaded = Roster->FindRow<FPlayerAttributes>(FName(*Object->GetStringField(TEXT("PlayerId"))), TEXT("DNA test"), false);
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Axis : (*DNA)->Values)
            {
                ++Compared;
                TestTrue(FString::Printf(TEXT("%s keeps %s"), *Object->GetStringField(TEXT("PlayerId")), *Axis.Key),
                    Loaded && FMath::IsNearlyEqual(PSPlayerDNA::GetAxis(Loaded->DNA, FName(*Axis.Key)), static_cast<float>(Axis.Value->AsNumber()), 0.0001f));
            }
        }
        TestTrue(TEXT("The in-game roster carries DNA (tools/player_dna.py --write)"), Compared > 0);
    }

    // The world's DNA subsystem reads the same catalog.
    UWorld* World = CreateTestWorld();
    UPSPlayerDNASubsystem* Subsystem = UPSPlayerDNASubsystem::Get(World);
    if (TestNotNull(TEXT("A game world has the DNA subsystem"), Subsystem))
    {
        TestEqual(TEXT("...with the shipped catalog"), Subsystem->GetCatalog().Axes.Num(), Catalog.Axes.Num());
    }
    if (World)
    {
        DestroyTestWorld(World);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Scrambler and pocket passer
// ---------------------------------------------------------------------------
namespace PSPlayerDNATests
{
    struct FPocketOutcome
    {
        EPSSkillPlayerAction Action = EPSSkillPlayerAction::Idle;
        bool bScrambling = false;
        int32 Throws = 0;
        float EscapeRadius = 0.f;
    };

    /** One quarterback with the ball at (-300, 0), a free rusher RusherDistance in front of him
     *  and, when ReceiverSeparation is above 0, a receiver that far from his corner. One decision. */
    static FPocketOutcome RunPocket(FAutomationTestBase& Test, const FPSPlayerDNA& DNA, float RusherDistance, float ReceiverSeparation)
    {
        FPocketOutcome Outcome;
        UWorld* World = CreateTestWorld();
        UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
        if (!Test.TestNotNull(TEXT("Bus"), Bus))
        {
            if (World)
            {
                DestroyTestWorld(World);
            }
            return Outcome;
        }
        const FVector Passer(-300.f, 0.f, 100.f);
        APSPlayerPawn* QB = SpawnPlayer(World, MakePlayer(EPlayerRole::Quarterback, TEXT("QB"), DNA), Passer);
        SpawnPlayer(World, MakePlayer(EPlayerRole::DefensiveLineman, TEXT("DL")), Passer + FVector(RusherDistance, 0.f, 0.f));
        if (ReceiverSeparation > 0.f)
        {
            SpawnPlayer(World, MakePlayer(EPlayerRole::WideReceiver, TEXT("WR")), FVector(500.f, 1500.f, 100.f));
            SpawnPlayer(World, MakePlayer(EPlayerRole::DefensiveBack, TEXT("CB")), FVector(500.f, 1500.f + ReceiverSeparation, 100.f));
        }
        if (!Test.TestNotNull(TEXT("QB"), QB) || !Test.TestNotNull(TEXT("Ball"), GiveBall(World, QB)))
        {
            DestroyTestWorld(World);
            return Outcome;
        }
        const FDelegateHandle Handle = Bus->OnThrowMC.AddLambda([&Outcome](const FPSTelemetryThrowEvent&) { ++Outcome.Throws; });

        StartPassPlay(World, Bus);
        UPSSkillPlayerAIComponent* Brain = OffenseOf(QB)->GetSkillAI();
        Brain->TickAI(0.1f);
        Outcome.Action = Brain->GetAction();
        Outcome.bScrambling = OffenseOf(QB)->GetPocket()->IsScrambling();
        Outcome.EscapeRadius = OffenseOf(QB)->GetPocket()->GetTuning().EscapeRadius;

        Bus->OnThrowMC.Remove(Handle);
        DestroyTestWorld(World);
        return Outcome;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayerDNAScramblerTest,
    "PlaySports.AI.DNA.ScramblerAndPocketPasser",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayerDNAScramblerTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayerDNATests;

    const FPSPlayerDNACatalog Catalog = LoadShippedCatalog();
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPocketTuningRow BasePocket;
    FSkillPlayerAITuningRow BaseSkill;
    Ingestion->LoadPocketTuningFromJson(UPSPocketComponent::GetDefaultTuningPath(), BasePocket);
    Ingestion->LoadSkillPlayerAITuningFromJson(UPSSkillPlayerAIComponent::GetDefaultTuningPath(), BaseSkill);

    // Two quarterbacks rated alike, at the two ends of Mobility.
    const FPSPlayerDNA ScramblerDNA = WithAxis(TEXT("Mobility"), 1.f);
    const FPSPlayerDNA PocketPasserDNA = WithAxis(TEXT("Mobility"), -1.f);
    const FPlayerAttributes Scrambler = MakePlayer(EPlayerRole::Quarterback, TEXT("QB"), ScramblerDNA);
    const FPlayerAttributes PocketPasser = MakePlayer(EPlayerRole::Quarterback, TEXT("QB"), PocketPasserDNA);
    const FPocketTuningRow ScramblerPocket = WithDNA(Catalog, BasePocket, Scrambler, TEXT("Pocket"));
    const FPocketTuningRow PasserPocket = WithDNA(Catalog, BasePocket, PocketPasser, TEXT("Pocket"));
    const FSkillPlayerAITuningRow ScramblerSkill = WithDNA(Catalog, BaseSkill, Scrambler, TEXT("SkillAI"));
    const FSkillPlayerAITuningRow PasserSkill = WithDNA(Catalog, BaseSkill, PocketPasser, TEXT("SkillAI"));

    // 1. A rusher closing, between the two men's escape radii and outside the pressure radius.
    const float Closing = 0.5f * (PasserPocket.EscapeRadius + ScramblerPocket.EscapeRadius);
    TestTrue(TEXT("(The scrambler feels the rush from further away)"), PasserPocket.EscapeRadius < Closing && Closing < ScramblerPocket.EscapeRadius);
    TestTrue(TEXT("(...from outside the pressure radius)"), Closing > BaseSkill.PressureRadius && Closing < BasePocket.PocketRadius);
    TestTrue(TEXT("(...not enough pressure to collapse the pocket passer's pocket)"), 1.f - Closing / BasePocket.PocketRadius < PasserPocket.CollapsePressure);

    const FPocketOutcome ScramblerLeaves = RunPocket(*this, ScramblerDNA, Closing, 0.f);
    TestTrue(TEXT("The scrambler leaves the pocket"), ScramblerLeaves.Action == EPSSkillPlayerAction::CarryBall && ScramblerLeaves.bScrambling);
    TestEqual(TEXT("...his pocket played with his DNA"), ScramblerLeaves.EscapeRadius, ScramblerPocket.EscapeRadius);
    const FPocketOutcome PasserStays = RunPocket(*this, PocketPasserDNA, Closing, 0.f);
    TestTrue(TEXT("The pocket passer stands in and reads"), PasserStays.Action == EPSSkillPlayerAction::ReadDefense && !PasserStays.bScrambling);
    TestEqual(TEXT("...his pocket played with his DNA"), PasserStays.EscapeRadius, PasserPocket.EscapeRadius);

    // 2. Pressured, the only receiver half open: between the two men's pressured-throw windows.
    const float Pressing = 0.5f * (FMath::Max(PasserPocket.EscapeRadius, BasePocket.SackImminentRadius) + BaseSkill.PressureRadius);
    const float HalfOpen = 0.5f * (PasserSkill.PressuredThrowSeparation + ScramblerSkill.PressuredThrowSeparation);
    TestTrue(TEXT("(The rusher presses without a sack or the pocket passer's escape)"), Pressing > PasserPocket.EscapeRadius && Pressing > BasePocket.SackImminentRadius
        && Pressing <= BaseSkill.PressureRadius);
    TestTrue(TEXT("(The receiver is open enough for the pocket passer under pressure, not the scrambler)"),
        PasserSkill.PressuredThrowSeparation <= HalfOpen && HalfOpen < ScramblerSkill.PressuredThrowSeparation);
    TestTrue(TEXT("(...and not plainly open)"), HalfOpen < BaseSkill.OpenSeparation);

    const FPocketOutcome PasserThrows = RunPocket(*this, PocketPasserDNA, Pressing, HalfOpen);
    TestEqual(TEXT("Under pressure the pocket passer throws to the half-open man"), PasserThrows.Throws, 1);
    TestFalse(TEXT("...without leaving the pocket"), PasserThrows.bScrambling);
    const FPocketOutcome ScramblerRuns = RunPocket(*this, ScramblerDNA, Pressing, HalfOpen);
    TestEqual(TEXT("The scrambler doesn't"), ScramblerRuns.Throws, 0);
    TestTrue(TEXT("...he runs"), ScramblerRuns.Action == EPSSkillPlayerAction::CarryBall && ScramblerRuns.bScrambling);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Power and finesse rushers
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayerDNARushTest,
    "PlaySports.AI.DNA.PowerAndFinesseRushers",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayerDNARushTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayerDNATests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    // Rated alike (80, the blockers 70), at the two ends of RushPower, each on his own blocker.
    APSPlayerPawn* Power = SpawnPlayer(World, MakePlayer(EPlayerRole::DefensiveLineman, TEXT("DL_POWER"), WithAxis(TEXT("RushPower"), 1.f), 80.f), FVector(0.f, 0.f, 100.f));
    APSPlayerPawn* Finesse = SpawnPlayer(World, MakePlayer(EPlayerRole::DefensiveLineman, TEXT("DL_FINESSE"), WithAxis(TEXT("RushPower"), -1.f), 80.f), FVector(0.f, 3000.f, 100.f));
    APSPlayerPawn* Neutral = SpawnPlayer(World, MakePlayer(EPlayerRole::DefensiveLineman, TEXT("DL_NEUTRAL"), FPSPlayerDNA(), 80.f), FVector(0.f, -3000.f, 100.f));
    APSPlayerPawn* BlockerA = SpawnPlayer(World, MakePlayer(EPlayerRole::OffensiveLineman, TEXT("OL_A")), FVector(-100.f, 0.f, 100.f));
    APSPlayerPawn* BlockerB = SpawnPlayer(World, MakePlayer(EPlayerRole::OffensiveLineman, TEXT("OL_B")), FVector(-100.f, 3000.f, 100.f));
    APSPlayerPawn* BlockerC = SpawnPlayer(World, MakePlayer(EPlayerRole::OffensiveLineman, TEXT("OL_C")), FVector(-100.f, -3000.f, 100.f));
    SpawnPlayer(World, MakePlayer(EPlayerRole::Quarterback, TEXT("QB")), FVector(-500.f, 0.f, 100.f));
    if (!TestNotNull(TEXT("Power"), Power) || !TestNotNull(TEXT("Finesse"), Finesse) || !TestNotNull(TEXT("Neutral"), Neutral)
        || !TestNotNull(TEXT("OL_A"), BlockerA) || !TestNotNull(TEXT("OL_B"), BlockerB) || !TestNotNull(TEXT("OL_C"), BlockerC))
    {
        DestroyTestWorld(World);
        return false;
    }

    FPSTelemetrySnapEvent Snap;
    Bus->PublishSnap(Snap);
    APSPlayerPawn* const Rushers[] = { Power, Finesse, Neutral };
    APSPlayerPawn* const Blockers[] = { BlockerA, BlockerB, BlockerC };
    for (int32 Index = 0; Index < 3; ++Index)
    {
        DefenseOf(Rushers[Index])->SetAssignment(EPSDefensiveAssignmentType::PassRush);
        DefenseOf(Rushers[Index])->GetDefenderAI()->TickAI(0.05f);
        Rushers[Index]->bIsEngaged = true;
        Rushers[Index]->EngagedOpponent = Blockers[Index];
        Blockers[Index]->bIsEngaged = true;
        Blockers[Index]->EngagedOpponent = Rushers[Index];
    }

    const FPSPlayerDNACatalog Catalog = LoadShippedCatalog();
    const EPSRushMove PowerMove = DefenseOf(Power)->GetRushMoves()->ChooseMove();
    const EPSRushMove FinesseMove = DefenseOf(Finesse)->GetRushMoves()->ChooseMove();
    const EPSRushMove NeutralMove = DefenseOf(Neutral)->GetRushMoves()->ChooseMove();
    const FPSDNARushMoveLean* PowerLean = FindLean(Catalog, PowerMove);
    const FPSDNARushMoveLean* FinesseLean = FindLean(Catalog, FinesseMove);
    TestTrue(TEXT("The power rusher picks a power move"), PowerLean && PowerLean->Lean > 0.f);
    TestTrue(TEXT("The finesse rusher, rated the same, picks a finesse move"), FinesseLean && FinesseLean->Lean < 0.f);
    TestTrue(TEXT("Neutral, the plan picks by chance alone"), NeutralMove != EPSRushMove::None);

    // Style moves the choice, not the odds: the move he starts carries its plain chance.
    UPSRushMoveComponent* FinesseRush = DefenseOf(Finesse)->GetRushMoves();
    FinesseRush->TickRush(0.05f);
    FinesseRush->TickRush(FinesseRush->GetCatalog().FirstMoveSeconds);
    TestEqual(TEXT("The finesse rusher works his move"), FinesseRush->GetActiveMove(), FinesseMove);
    const FPSRushMoveDef* Def = FinesseRush->GetCatalog().RushMoves.FindByPredicate([FinesseMove](const FPSRushMoveDef& Entry) { return Entry.Move == FinesseMove; });
    if (TestNotNull(TEXT("...a move in the library"), Def))
    {
        TestEqual(TEXT("...at its plain chance"), FinesseRush->GetActiveWinChance(),
            PSRushMoves::ComputeWinChance(*Def, Finesse->GetAttributes(), BlockerB->GetAttributes(), EPSBlockResponse::None, false, FinesseRush->GetCatalog()));
    }
    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- Ball hawk and blanket cover
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayerDNACoverageTest,
    "PlaySports.AI.DNA.BallHawkAndBlanketCover",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayerDNACoverageTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayerDNATests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    const FPSPlayerDNACatalog Catalog = LoadShippedCatalog();
    FDefenderAITuningRow Base;
    NewObject<UPSDataIngestion>()->LoadDefenderAITuningFromJson(UPSDefenderAIComponent::GetDefaultTuningPath(), Base);
    const FPlayerAttributes HawkPlayer = MakePlayer(EPlayerRole::DefensiveBack, TEXT("DB_HAWK"), WithAxis(TEXT("BallHawk"), 1.f));
    const FPlayerAttributes BlanketPlayer = MakePlayer(EPlayerRole::DefensiveBack, TEXT("DB_BLANKET"), WithAxis(TEXT("BallHawk"), -1.f));
    const FDefenderAITuningRow HawkTuning = WithDNA(Catalog, Base, HawkPlayer, TEXT("DefenderAI"));
    const FDefenderAITuningRow BlanketTuning = WithDNA(Catalog, Base, BlanketPlayer, TEXT("DefenderAI"));

    // The throw comes down between the two men's radii, each the same distance from it.
    const FVector Landing(1500.f, 0.f, 100.f);
    const float Reach = 0.5f * (HawkTuning.BallHawkRadius + BlanketTuning.BallHawkRadius);
    TestTrue(TEXT("(The ball hawk breaks on throws from further away)"), BlanketTuning.BallHawkRadius < Reach && Reach < HawkTuning.BallHawkRadius);
    APSPlayerPawn* Hawk = SpawnPlayer(World, HawkPlayer, Landing + FVector(0.f, Reach, 0.f));
    APSPlayerPawn* Blanket = SpawnPlayer(World, BlanketPlayer, Landing - FVector(0.f, Reach, 0.f));
    APSPlayerPawn* HawkMan = SpawnPlayer(World, MakePlayer(EPlayerRole::WideReceiver, TEXT("WR_A")), Landing + FVector(-100.f, Reach, 0.f));
    APSPlayerPawn* BlanketMan = SpawnPlayer(World, MakePlayer(EPlayerRole::WideReceiver, TEXT("WR_B")), Landing - FVector(100.f, Reach, 0.f));
    if (!TestNotNull(TEXT("Hawk"), Hawk) || !TestNotNull(TEXT("Blanket"), Blanket) || !TestNotNull(TEXT("WR_A"), HawkMan) || !TestNotNull(TEXT("WR_B"), BlanketMan))
    {
        DestroyTestWorld(World);
        return false;
    }
    UPSDefenderAIComponent* HawkAI = DefenseOf(Hawk)->GetDefenderAI();
    UPSDefenderAIComponent* BlanketAI = DefenseOf(Blanket)->GetDefenderAI();

    FPSTelemetrySnapEvent Snap;
    Bus->PublishSnap(Snap);
    DefenseOf(Hawk)->SetAssignment(EPSDefensiveAssignmentType::ManCoverage, HawkMan);
    DefenseOf(Blanket)->SetAssignment(EPSDefensiveAssignmentType::ManCoverage, BlanketMan);
    HawkAI->TickAI(0.05f);
    BlanketAI->TickAI(0.05f);
    TestTrue(TEXT("Both cover their man"), HawkAI->GetAction() == EPSDefenderAction::Cover && BlanketAI->GetAction() == EPSDefenderAction::Cover);
    TestEqual(TEXT("The ball hawk plays with his DNA"), HawkAI->GetTuning().BallHawkRadius, HawkTuning.BallHawkRadius);
    TestEqual(TEXT("...the blanket corner with his"), BlanketAI->GetTuning().BallHawkRadius, BlanketTuning.BallHawkRadius);
    TestTrue(TEXT("The ball hawk sits further off his man"), HawkAI->GetTuning().ManCushion > BlanketAI->GetTuning().ManCushion);

    // A pump fake: the ball hawk bites longer (both are rated 70 Awareness).
    const float Unaware = 1.f - 70.f / 100.f;
    const float HawkFreeze = HawkTuning.PumpFakeFreezeSeconds * Unaware;
    const float BlanketFreeze = BlanketTuning.PumpFakeFreezeSeconds * Unaware;
    TestTrue(TEXT("(The ball hawk's bite is the longer)"), BlanketFreeze < HawkFreeze);
    Bus->PublishPumpFake(FPSTelemetryPumpFakeEvent());
    const float AfterFake = 0.5f * (HawkFreeze + BlanketFreeze);
    HawkAI->TickAI(AfterFake);
    BlanketAI->TickAI(AfterFake);
    TestTrue(TEXT("The ball hawk is still frozen on the fake"), HawkAI->IsFrozen());
    TestFalse(TEXT("...the blanket corner is not"), BlanketAI->IsFrozen());

    // The throw: the ball hawk breaks on it, the blanket corner stays on his man.
    FPSTelemetryThrowEvent Throw;
    Throw.PasserName = TEXT("QB");
    Throw.TargetLocation = Landing;
    Throw.LandingLocation = Landing;
    Bus->PublishThrow(Throw);
    const float React = HawkAI->GetReactionSeconds() + HawkFreeze + 0.05f;
    HawkAI->TickAI(React);
    BlanketAI->TickAI(React);
    TestTrue(TEXT("The ball hawk breaks on the ball"), HawkAI->GetAction() == EPSDefenderAction::BallHawk);
    TestTrue(TEXT("The blanket corner, rated the same, stays on his man"), BlanketAI->GetAction() == EPSDefenderAction::Cover);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- Power and elusive backs
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayerDNABacksTest,
    "PlaySports.AI.DNA.PowerAndElusiveBacks",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayerDNABacksTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayerDNATests;

    UWorld* World = CreateTestWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    // Two backs rated alike with the ball, each with a tackler closing from upfield and his left.
    const FVector Tackler(200.f, -150.f, 0.f);
    APSPlayerPawn* PowerBack = SpawnPlayer(World, MakePlayer(EPlayerRole::RunningBack, TEXT("RB_POWER"), WithAxis(TEXT("RunPower"), 1.f)), FVector(0.f, 0.f, 100.f));
    APSPlayerPawn* ElusiveBack = SpawnPlayer(World, MakePlayer(EPlayerRole::RunningBack, TEXT("RB_ELUSIVE"), WithAxis(TEXT("RunPower"), -1.f)), FVector(0.f, 1500.f, 100.f));
    SpawnPlayer(World, MakePlayer(EPlayerRole::Linebacker, TEXT("LB_A")), FVector(0.f, 0.f, 100.f) + Tackler);
    SpawnPlayer(World, MakePlayer(EPlayerRole::Linebacker, TEXT("LB_B")), FVector(0.f, 1500.f, 100.f) + Tackler);
    if (!TestNotNull(TEXT("Power back"), PowerBack) || !TestNotNull(TEXT("Elusive back"), ElusiveBack)
        || !TestNotNull(TEXT("Ball A"), GiveBall(World, PowerBack)) || !TestNotNull(TEXT("Ball B"), GiveBall(World, ElusiveBack)))
    {
        DestroyTestWorld(World);
        return false;
    }

    StartPassPlay(World, Bus);
    UPSSkillPlayerAIComponent* PowerBrain = OffenseOf(PowerBack)->GetSkillAI();
    UPSSkillPlayerAIComponent* ElusiveBrain = OffenseOf(ElusiveBack)->GetSkillAI();
    PowerBrain->TickAI(0.1f);
    ElusiveBrain->TickAI(0.1f);
    TestTrue(TEXT("Both carry the ball"), PowerBrain->GetAction() == EPSSkillPlayerAction::CarryBall && ElusiveBrain->GetAction() == EPSSkillPlayerAction::CarryBall);
    const FVector PowerHeading = PowerBrain->GetDesiredDirection();
    const FVector ElusiveHeading = ElusiveBrain->GetDesiredDirection();
    TestTrue(TEXT("The elusive back cuts away from the tackler"), ElusiveHeading.Y > PowerHeading.Y && ElusiveHeading.Y > 0.f);
    TestTrue(TEXT("The power back keeps going upfield"), PowerHeading.X > ElusiveHeading.X);
    TestTrue(TEXT("...his carrier tuning scaled by his DNA"), PowerBrain->GetTuning().CarrierAvoidWeight < ElusiveBrain->GetTuning().CarrierAvoidWeight);

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- What a scout sees
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSPlayerDNAScoutingTest,
    "PlaySports.AI.DNA.ScoutingTraits",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSPlayerDNAScoutingTest::RunTest(const FString& Parameters)
{
    using namespace PSPlayerDNATests;

    const FPSPlayerDNACatalog Catalog = LoadShippedCatalog();
    const FPSDNAAxisDef* Mobility = PSPlayerDNA::FindAxis(Catalog, TEXT("Mobility"));
    if (!TestNotNull(TEXT("Mobility"), Mobility) || !TestTrue(TEXT("(A trait threshold)"), Catalog.TraitThreshold > 0.f && Catalog.TraitThreshold < 0.8f))
    {
        return false;
    }

    FPSPlayerDNA Scrambler = WithAxis(TEXT("Mobility"), 0.8f);
    Scrambler.Gunslinger = Catalog.TraitThreshold * 0.5f;
    TArray<FPSScoutingTrait> Traits = PSPlayerDNA::GetScoutingTraits(Catalog, MakePlayer(EPlayerRole::Quarterback, TEXT("QB"), Scrambler));
    if (TestEqual(TEXT("A strong scrambler shows one trait; a slight lean shows none"), Traits.Num(), 1))
    {
        TestEqual(TEXT("...Scrambler"), Traits[0].TraitId, FName(TEXT("Scrambler")));
        TestTrue(TEXT("...as strong as his DNA"), FMath::IsNearlyEqual(Traits[0].Strength, 0.8f));
        TestEqual(TEXT("...named through the string table"), Traits[0].Label.ToString(), Mobility->HighLabel);
        TestEqual(TEXT("...described"), Traits[0].Description.ToString(), Mobility->HighDescription);
    }

    FPSPlayerDNA Gunslinger = WithAxis(TEXT("Mobility"), -0.5f);
    Gunslinger.Gunslinger = 0.9f;
    Traits = PSPlayerDNA::GetScoutingTraits(Catalog, MakePlayer(EPlayerRole::Quarterback, TEXT("QB"), Gunslinger));
    if (TestEqual(TEXT("Two pronounced axes show two traits"), Traits.Num(), 2))
    {
        TestEqual(TEXT("...the strongest first"), Traits[0].TraitId, FName(TEXT("Gunslinger")));
        TestEqual(TEXT("...then the low end of Mobility"), Traits[1].TraitId, FName(TEXT("PocketPasser")));
    }

    FPSPlayerDNA Corner = WithAxis(TEXT("BallHawk"), -0.6f);
    Corner.Mobility = 1.f;
    Traits = PSPlayerDNA::GetScoutingTraits(Catalog, MakePlayer(EPlayerRole::DefensiveBack, TEXT("DB"), Corner));
    if (TestEqual(TEXT("A corner shows only his own role's traits"), Traits.Num(), 1))
    {
        TestEqual(TEXT("...BlanketCover"), Traits[0].TraitId, FName(TEXT("BlanketCover")));
    }
    TestEqual(TEXT("A neutral player shows none"), PSPlayerDNA::GetScoutingTraits(Catalog, MakePlayer(EPlayerRole::Linebacker, TEXT("LB"))).Num(), 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
