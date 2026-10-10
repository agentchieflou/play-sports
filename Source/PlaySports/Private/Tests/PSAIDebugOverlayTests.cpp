// PSAIDebugOverlayTests.cpp -- Epic 85.2 (the on-field AI debug overlay, drawn with Track A's badge rules)
//
// Tests covered:
//   1. The cards' layout for a camera: text wrapped and measured; a card over the player's head,
//      larger when nearer (the badges' distance scale) and sized for the display's DPI; none
//      behind the camera or off the screen; side-by-side cards nudged apart as badges are, and
//      one with no room dimmed where it was rather than hidden; the line to a target.
//   2. The decision log's overlay: a card for every player on the field with a decision this
//      play, saying what DescribeForOverlay says, by side; none while the overlay is off; the
//      overlay turns logging on; the world's debug text stands down while a drawing layer is up.
//   3. Data/ai_debug.json's overlay fields load with the defaults and validate; unsound ones are
//      reported.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSAIDebugOverlay.h"
#include "PSAIDecisionLog.h"
#include "PSDataIngestion.h"
#include "PSOverlayBadgeLayout.h"
#include "PSPlayerPawn.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSAIDebugOverlayTests
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

    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (Pawn)
        {
            FPlayerAttributes Attributes;
            Attributes.PlayerId = FName(PlayerId);
            Attributes.DisplayName = PlayerId;
            Attributes.Role = Role;
            Pawn->InitializePlayer(Attributes);
        }
        return Pawn;
    }

    /** A sideline-high camera 30 m behind the ball, looking upfield at a 1080p screen. */
    static FPSBadgeView BehindTheOffense()
    {
        FPSBadgeView View;
        View.CameraLocation = FVector(-3000.0, 0.0, 300.0);
        View.CameraRotation = FRotator::ZeroRotator;
        View.FOVDegrees = 90.f;
        View.ViewportSize = FVector2D(1920.0, 1080.0);
        return View;
    }

    static FPSAIDebugCardSource Source(const TCHAR* PlayerId, const FVector& Location, const TCHAR* Text, bool bOffense = true)
    {
        FPSAIDebugCardSource Entry;
        Entry.PlayerId = FName(PlayerId);
        Entry.Location = Location;
        Entry.Text = Text;
        Entry.bOffense = bOffense;
        return Entry;
    }

    static const FPSAIDebugCard* FindCard(const TArray<FPSAIDebugCard>& Cards, const TCHAR* PlayerId)
    {
        const FName Wanted(PlayerId);
        return Cards.FindByPredicate([Wanted](const FPSAIDebugCard& Card) { return Card.PlayerId == Wanted; });
    }
}

// ---------------------------------------------------------------------------
// 1. The cards' layout
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAIDebugOverlayLayoutTest,
    "PlaySports.AIDebug.OverlayLayout",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAIDebugOverlayLayoutTest::RunTest(const FString& Parameters)
{
    using namespace PSAIDebugOverlayTests;

    // Text: wrapped at a space, a word longer than a line cut, its own breaks kept.
    TestEqual(TEXT("A long line breaks at its last space"), PSAIDebugOverlay::WrapText(TEXT("abc def ghi"), 7), FString(TEXT("abc def\nghi")));
    TestEqual(TEXT("A word longer than a line is cut"), PSAIDebugOverlay::WrapText(TEXT("abcdefghij"), 4), FString(TEXT("abcd\nefgh\nij")));
    TestEqual(TEXT("The text's own breaks stay"), PSAIDebugOverlay::WrapText(TEXT("ab\ncd"), 10), FString(TEXT("ab\ncd")));
    TestEqual(TEXT("No limit, no wrap"), PSAIDebugOverlay::WrapText(TEXT("abc def"), 0), FString(TEXT("abc def")));

    FPSAIDebugTuning Tuning;
    const FVector2D Measured = PSAIDebugOverlay::MeasureText(TEXT("abcd\nab"), 10, Tuning);
    TestEqual(TEXT("A card is its longest line wide, plus padding"), static_cast<float>(Measured.X), 4.f * Tuning.OverlayCharWidth * 10.f + 2.f * Tuning.OverlayPadding, 0.001f);
    TestEqual(TEXT("...and its lines tall, plus padding"), static_cast<float>(Measured.Y), 2.f * Tuning.OverlayLineHeight * 10.f + 2.f * Tuning.OverlayPadding, 0.001f);

    const FPSOverlayBadgeStyle BadgeStyle;
    const FPSBadgeView View = BehindTheOffense();
    const TArray<FPSAIDebugCardSource> Sources = {
        Source(TEXT("WR_1"), FVector(0.0, 0.0, 0.0), TEXT("WR_1 [Route] RunRoute")),
        Source(TEXT("RB_1"), FVector(-1500.0, 300.0, 0.0), TEXT("RB_1 [Route] RunRoute")),
        Source(TEXT("BEHIND"), FVector(-4000.0, 0.0, 0.0), TEXT("Behind the camera")),
        Source(TEXT("WIDE"), FVector(0.0, 5000.0, 0.0), TEXT("Off the screen"), false) };
    const TArray<FPSAIDebugCard> Cards = PSAIDebugOverlay::LayoutCards(Sources, View, BadgeStyle, Tuning, 1.f);
    TestEqual(TEXT("A card per source"), Cards.Num(), Sources.Num());
    const FPSAIDebugCard* Far = FindCard(Cards, TEXT("WR_1"));
    const FPSAIDebugCard* Near = FindCard(Cards, TEXT("RB_1"));
    if (TestTrue(TEXT("Both players in view have cards"), Far && Near && Far->bVisible && Near->bVisible))
    {
        FVector2D Head;
        float Depth = 0.f;
        PSOverlayBadgeLayout::ProjectToScreen(View, FVector(0.0, 0.0, Tuning.OverlayHeightCm), Head, Depth);
        TestTrue(TEXT("A card sits OverlayHeightCm over its player"), Far->ScreenPosition.Equals(Head, 0.01));
        FVector2D Feet;
        PSOverlayBadgeLayout::ProjectToScreen(View, FVector::ZeroVector, Feet, Depth);
        TestTrue(TEXT("...above him on the screen"), Far->ScreenPosition.Y < Feet.Y);
        TestTrue(TEXT("The nearer player's card is larger, as a badge is"), Near->Scale > Far->Scale && Near->FontSize > Far->FontSize);
        TestEqual(TEXT("...its type the overlay's size at its scale"), Far->FontSize,
            FMath::Max(1, FMath::RoundToInt(Tuning.OverlayFontSize * Tuning.OverlayFontScale * PSOverlayBadgeLayout::ScaleForDistance(BadgeStyle,
                static_cast<float>(FVector::Dist(View.CameraLocation, FVector(0.0, 0.0, Tuning.OverlayHeightCm)))))));
        TestTrue(TEXT("...and its card sized for its text"), Far->Size.Equals(PSAIDebugOverlay::MeasureText(Far->Text, Far->FontSize, Tuning), 0.01));
        TestTrue(TEXT("The card's colors follow its side"), Far->bOffense);
    }
    const FPSAIDebugCard* Behind = FindCard(Cards, TEXT("BEHIND"));
    TestTrue(TEXT("A player behind the camera has no card on the screen"), Behind && !Behind->bVisible);
    const FPSAIDebugCard* Wide = FindCard(Cards, TEXT("WIDE"));
    TestTrue(TEXT("Nor does one off the side of the screen"), Wide && !Wide->bVisible && !Wide->bOffense);

    // The display's DPI: a phone's cards are as big to the eye, so more pixels.
    const TArray<FPSAIDebugCard> Dense = PSAIDebugOverlay::LayoutCards({ Sources[0] }, View, BadgeStyle, Tuning, 3.f);
    TestTrue(TEXT("At three pixels a unit a card is three times the pixels"), Dense.Num() == 1 && Far && Dense[0].Size.Equals(Far->Size * 3.0, 0.01)
        && Dense[0].FontSize == Far->FontSize);

    // Side by side: the farther card is nudged up clear of the nearer, as badges are.
    const TArray<FPSAIDebugCardSource> Pair = {
        Source(TEXT("A_NEAR"), FVector(0.0, 0.0, 0.0), TEXT("A_NEAR [Route] RunRoute")),
        Source(TEXT("B_FAR"), FVector(20.0, 30.0, 0.0), TEXT("B_FAR [Route] RunRoute")) };
    const TArray<FPSAIDebugCard> Nudged = PSAIDebugOverlay::LayoutCards(Pair, View, BadgeStyle, Tuning, 1.f);
    if (TestEqual(TEXT("Two cards"), Nudged.Num(), 2))
    {
        FVector2D FarHead;
        float FarDepth = 0.f;
        PSOverlayBadgeLayout::ProjectToScreen(View, FVector(20.0, 30.0, Tuning.OverlayHeightCm), FarHead, FarDepth);
        TestTrue(TEXT("The nearer card keeps its place"), !Nudged[0].bCrowdedOut && Nudged[0].bVisible);
        TestTrue(TEXT("The farther one moves up clear of it"), !Nudged[1].bCrowdedOut && Nudged[1].bVisible && Nudged[1].ScreenPosition.Y < FarHead.Y - 1.0);
    }
    FPSAIDebugTuning NoNudges = Tuning;
    NoNudges.OverlayMaxNudges = 0;
    const TArray<FPSAIDebugCard> Crowded = PSAIDebugOverlay::LayoutCards(Pair, View, BadgeStyle, NoNudges, 1.f);
    if (TestEqual(TEXT("Two cards again"), Crowded.Num(), 2))
    {
        FVector2D FarHead;
        float FarDepth = 0.f;
        PSOverlayBadgeLayout::ProjectToScreen(View, FVector(20.0, 30.0, Tuning.OverlayHeightCm), FarHead, FarDepth);
        TestTrue(TEXT("With no room, a card is crowded out but still shown where it was: a debug view hides nothing"),
            Crowded[1].bCrowdedOut && Crowded[1].bVisible && Crowded[1].ScreenPosition.Equals(FarHead, 0.01));
    }

    // The line to a target: both ends on the screen, or none.
    FPSAIDebugCardSource Rusher = Source(TEXT("DE_1"), FVector(500.0, -200.0, 0.0), TEXT("DE_1 [PassRush] Rush -> QB"), false);
    Rusher.bHasTarget = true;
    Rusher.TargetLocation = FVector(-200.0, 0.0, 0.0);
    FPSAIDebugCardSource Lost = Rusher;
    Lost.PlayerId = TEXT("DE_2");
    Lost.TargetLocation = FVector(-5000.0, 0.0, 0.0);
    const TArray<FPSAIDebugCard> Targets = PSAIDebugOverlay::LayoutCards({ Rusher, Lost }, View, BadgeStyle, Tuning, 1.f);
    if (TestEqual(TEXT("Two rushers"), Targets.Num(), 2))
    {
        FVector2D PlayerScreen;
        FVector2D TargetScreen;
        float Unused = 0.f;
        PSOverlayBadgeLayout::ProjectToScreen(View, Rusher.Location, PlayerScreen, Unused);
        PSOverlayBadgeLayout::ProjectToScreen(View, Rusher.TargetLocation, TargetScreen, Unused);
        TestTrue(TEXT("A rusher's line runs from him to his target on the screen"), Targets[0].bHasTarget
            && Targets[0].PlayerScreen.Equals(PlayerScreen, 0.01) && Targets[0].TargetScreen.Equals(TargetScreen, 0.01));
        TestFalse(TEXT("A target behind the camera draws no line"), Targets[1].bHasTarget);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 2. The decision log's overlay
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAIDebugOverlayCardsTest,
    "PlaySports.AIDebug.OverlayCards",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAIDebugOverlayCardsTest::RunTest(const FString& Parameters)
{
    using namespace PSAIDebugOverlayTests;

    UWorld* World = CreateTestWorld();
    UPSAIDecisionLog* Log = UPSAIDecisionLog::Get(World);
    if (!TestNotNull(TEXT("Game worlds have the decision log"), Log))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    APSPlayerPawn* Receiver = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_1"), FVector(0.0, 300.0, 90.0));
    APSPlayerPawn* Corner = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_1"), FVector(600.0, 300.0, 90.0));
    APSPlayerPawn* Quiet = SpawnPlayer(World, EPlayerRole::OffensiveLineman, TEXT("OL_1"), FVector(0.0, -200.0, 90.0));
    if (!TestTrue(TEXT("Three players"), Receiver && Corner && Quiet))
    {
        DestroyTestWorld(World);
        return false;
    }

    const FPSBadgeView View = BehindTheOffense();
    const FPSOverlayBadgeStyle BadgeStyle;
    Log->SetOverlayEnabled(false);
    TestFalse(TEXT("Off, the overlay is off"), Log->IsOverlayOn());
    TestEqual(TEXT("...and lays out nothing"), Log->LayoutOverlay(View, BadgeStyle, 1.f).Num(), 0);

    Log->SetOverlayEnabled(true);
    TestTrue(TEXT("On, it is on"), Log->IsOverlayOn());
    TestTrue(TEXT("...and the AI's decisions are recorded for it"), Log->IsLogging());

    FPSAIDecisionRecord Route;
    Route.AgentId = TEXT("WR_1");
    Route.System = TEXT("SkillAI");
    Route.Assignment = TEXT("Route");
    Route.Action = TEXT("RunRoute");
    Route.Reason = TEXT("on his stem, the corner playing off");
    Log->Record(Route);
    FPSAIDecisionRecord Cover;
    Cover.AgentId = TEXT("CB_1");
    Cover.System = TEXT("DefenderAI");
    Cover.Assignment = TEXT("ManCoverage");
    Cover.Action = TEXT("Cover");
    Cover.Target = TEXT("WR_1");
    Cover.TargetLocation = FVector(0.0, 300.0, 90.0);
    Cover.Reason = TEXT("trailing his man");
    Log->Record(Cover);

    const TArray<FPSAIDebugCard> Cards = Log->LayoutOverlay(View, BadgeStyle, 1.f);
    TestEqual(TEXT("A card for each player with a decision"), Cards.Num(), 2);
    const FPSAIDebugCard* RouteCard = FindCard(Cards, TEXT("WR_1"));
    const FPSAIDebugCard* CoverCard = FindCard(Cards, TEXT("CB_1"));
    TestNull(TEXT("None for a player without one"), FindCard(Cards, TEXT("OL_1")));
    if (TestNotNull(TEXT("The receiver's card"), RouteCard) && TestNotNull(TEXT("The corner's card"), CoverCard))
    {
        const FPSAIDebugTuning& Tuning = Log->GetTuning();
        TestEqual(TEXT("It says what the decision log's overlay text says"), RouteCard->Text,
            PSAIDebugOverlay::WrapText(UPSAIDecisionLog::DescribeForOverlay(Route), Tuning.OverlayMaxLineChars));
        TestTrue(TEXT("...on the offense's color"), RouteCard->bOffense);
        TestFalse(TEXT("The corner's is the defense's"), CoverCard->bOffense);
        TestFalse(TEXT("A decision with no place draws no line"), RouteCard->bHasTarget);
        TestTrue(TEXT("The corner's runs to his man"), CoverCard->bHasTarget);
        TestTrue(TEXT("Both are on the screen"), RouteCard->bVisible && CoverCard->bVisible);
    }

    // Who draws it: the world's debug text, until a drawing layer is up.
    TestTrue(TEXT("With no layer up, the world's debug text draws the overlay"), Log->ShouldDrawWorldText());
    Log->RegisterOverlayLayer();
    TestFalse(TEXT("A layer up: the world's text stands down"), Log->ShouldDrawWorldText());
    Log->UnregisterOverlayLayer();
    Log->UnregisterOverlayLayer();
    TestTrue(TEXT("The layer gone (and gone again): the world's text is back"), Log->ShouldDrawWorldText());
    Log->SetOverlayEnabled(false);
    TestFalse(TEXT("The overlay off: nothing draws"), Log->ShouldDrawWorldText());

    DestroyTestWorld(World);
    return true;
}

// ---------------------------------------------------------------------------
// 3. The overlay's settings
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSAIDebugOverlayTuningTest,
    "PlaySports.AIDebug.OverlayTuning",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSAIDebugOverlayTuningTest::RunTest(const FString& Parameters)
{
    FPSAIDebugTuning Loaded;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (!TestTrue(TEXT("Data/ai_debug.json loads"), Ingestion->LoadAIDebugTuningFromJson(UPSAIDecisionLog::GetDefaultTuningPath(), Loaded)))
    {
        return false;
    }
    for (const FString& Problem : PSAIDebugOverlay::ValidateTuning(Loaded))
    {
        AddError(FString::Printf(TEXT("ai_debug.json: %s"), *Problem));
    }
    const FPSAIDebugTuning Defaults;
    TestEqual(TEXT("Defaults equal the file: the type size"), Loaded.OverlayFontSize, Defaults.OverlayFontSize);
    TestEqual(TEXT("... the character width"), Loaded.OverlayCharWidth, Defaults.OverlayCharWidth);
    TestEqual(TEXT("... the wrap"), Loaded.OverlayMaxLineChars, Defaults.OverlayMaxLineChars);
    TestEqual(TEXT("... the offense's color"), Loaded.OverlayOffenseColor, Defaults.OverlayOffenseColor);
    TestEqual(TEXT("... the crowded opacity"), Loaded.OverlayCrowdedOpacity, Defaults.OverlayCrowdedOpacity);
    TestEqual(TEXT("... the nudges"), Loaded.OverlayMaxNudges, Defaults.OverlayMaxNudges);
    TestEqual(TEXT("... the line"), Loaded.OverlayTargetLineWidth, Defaults.OverlayTargetLineWidth);

    FPSAIDebugTuning Bad = Loaded;
    Bad.OverlayFontSize = 0;
    Bad.OverlayCharWidth = 0.f;
    Bad.OverlayNudgeStep = -1.f;
    Bad.OverlayMaxLineChars = 2;
    Bad.OverlayOpacity = 1.5f;
    Bad.OverlayDefenseColor = TEXT("orange");
    TestEqual(TEXT("Each unsound overlay setting is reported"), PSAIDebugOverlay::ValidateTuning(Bad).Num(), 6);
    return true;
}

#endif
