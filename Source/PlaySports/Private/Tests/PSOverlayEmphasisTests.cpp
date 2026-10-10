// PSOverlayEmphasisTests.cpp -- Epic 36 (player highlight and emphasis rendering)
//
// Tests covered:
//   1. The emphasis rules load and validate, and a bad set is caught.
//   2. Requests mark players' meshes for the custom-depth pass with their look's stencil: the
//      highest priority request on a player wins and a timed one runs out; a source's requests
//      clear together; past the budget the lowest priority, oldest, go undrawn; a spotlight dims
//      everyone without a drawn look of his own, and lifting it restores them; a Minimal tier
//      draws nothing but keeps the requests.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSDataIngestion.h"
#include "PSOverlayEmphasisSubsystem.h"
#include "PSPlayerPawn.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSOverlayEmphasisTests
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

    static APSPlayerPawn* SpawnPlayer(UWorld* World, EPlayerRole Role, const TCHAR* PlayerId, double Y)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector(0.0, Y, 100.0), FRotator::ZeroRotator, SpawnParams);
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

    /** The stencil every mesh of Pawn carries into the custom-depth pass: 0 when none is drawn
     *  there, -1 when its meshes disagree or it has none. */
    static int32 MarkedStencil(const APSPlayerPawn* Pawn)
    {
        TInlineComponentArray<UMeshComponent*> Meshes(Pawn);
        int32 Stencil = -2;
        for (const UMeshComponent* Mesh : Meshes)
        {
            const int32 Own = Mesh->bRenderCustomDepth ? Mesh->CustomDepthStencilValue : 0;
            if (Stencil != -2 && Own != Stencil)
            {
                return -1;
            }
            Stencil = Own;
        }
        return Stencil == -2 ? -1 : Stencil;
    }

    /** Round stencils and a budget of two. */
    static FPSEmphasisStyle RulesStyle()
    {
        FPSEmphasisStyle Style;
        auto AddKind = [&Style](EPSEmphasisKind Kind, int32 Stencil, int32 Priority)
        {
            FPSEmphasisKindStyle Entry;
            Entry.Kind = Kind;
            Entry.Stencil = Stencil;
            Entry.Priority = Priority;
            Style.Kinds.Add(Entry);
        };
        AddKind(EPSEmphasisKind::Highlight, 1, 1);
        AddKind(EPSEmphasisKind::Mismatch, 2, 2);
        AddKind(EPSEmphasisKind::Focus, 3, 3);
        Style.DimStencil = 4;
        Style.MaxEmphasized = 2;
        Style.bSpotlightDimsEmphasized = false;
        return Style;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The rules
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSEmphasisStyleTest,
    "PlaySports.Overlay.EmphasisStyleValidates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSEmphasisStyleTest::RunTest(const FString& Parameters)
{
    FPSEmphasisStyle Style;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    if (TestTrue(TEXT("player_emphasis.json loads"), Ingestion->LoadEmphasisStyleFromJson(UPSOverlayEmphasisSubsystem::GetDefaultStylePath(), Style)))
    {
        for (const FString& Problem : UPSOverlayEmphasisSubsystem::ValidateStyle(Style))
        {
            AddError(FString::Printf(TEXT("player_emphasis.json: %s"), *Problem));
        }
        const FPSEmphasisKindStyle* Focus = Style.FindKind(EPSEmphasisKind::Focus);
        const FPSEmphasisKindStyle* Highlight = Style.FindKind(EPSEmphasisKind::Highlight);
        if (TestNotNull(TEXT("Focus is styled"), Focus) && TestNotNull(TEXT("Highlight is styled"), Highlight))
        {
            TestTrue(TEXT("A replay's focus outranks a callout"), Focus->Priority > Highlight->Priority);
        }
    }

    FPSEmphasisStyle Bad = PSOverlayEmphasisTests::RulesStyle();
    Bad.Kinds.RemoveAll([](const FPSEmphasisKindStyle& Entry) { return Entry.Kind == EPSEmphasisKind::Mismatch; });
    Bad.Kinds[0].Stencil = 300;
    Bad.DimStencil = 3;
    Bad.MaxEmphasized = 0;
    TestEqual(TEXT("A bad set is caught: a kind missing, a stencil out of range, two looks sharing one, no budget"),
        UPSOverlayEmphasisSubsystem::ValidateStyle(Bad).Num(), 4);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- Marking players
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSEmphasisMarkTest,
    "PlaySports.Overlay.EmphasisMarksPlayers",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSEmphasisMarkTest::RunTest(const FString& Parameters)
{
    using namespace PSOverlayEmphasisTests;

    UWorld* World = CreateTestWorld();
    UPSOverlayEmphasisSubsystem* Emphasis = World ? World->GetSubsystem<UPSOverlayEmphasisSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Emphasis subsystem"), Emphasis))
    {
        if (World)
        {
            DestroyTestWorld(World);
        }
        return false;
    }
    Emphasis->SetStyle(RulesStyle());
    Emphasis->SetOverlayDetail(EPSOverlayDetail::Full);

    APSPlayerPawn* WR = SpawnPlayer(World, EPlayerRole::WideReceiver, TEXT("WR_1"), -900.0);
    APSPlayerPawn* CB = SpawnPlayer(World, EPlayerRole::DefensiveBack, TEXT("CB_1"), -800.0);
    APSPlayerPawn* TE = SpawnPlayer(World, EPlayerRole::TightEnd, TEXT("TE_1"), 450.0);
    APSPlayerPawn* LB = SpawnPlayer(World, EPlayerRole::Linebacker, TEXT("LB_1"), 300.0);
    if (!WR || !CB || !TE || !LB)
    {
        AddError(TEXT("Could not spawn the players"));
        DestroyTestWorld(World);
        return false;
    }
    TestEqual(TEXT("Nobody is marked to start with"), MarkedStencil(WR), 0);

    // A callout, then a mismatch on the same player for two seconds.
    const FName Commentary(TEXT("Commentary"));
    const FName Coaching(TEXT("Coaching"));
    const FName Replay(TEXT("Replay"));
    const int32 Callout = Emphasis->Emphasize(WR, EPSEmphasisKind::Highlight, Commentary);
    TestTrue(TEXT("A request has a handle"), Callout > 0);
    TestEqual(TEXT("The callout marks his meshes with its stencil"), MarkedStencil(WR), 1);
    TestEqual(TEXT("...and reads as a highlight"), Emphasis->GetEmphasis(WR).Look, EPSEmphasisLook::Highlight);
    TestEqual(TEXT("...from commentary"), Emphasis->GetEmphasis(WR).Source, Commentary);
    TestEqual(TEXT("Nobody else is marked"), MarkedStencil(CB), 0);
    Emphasis->Emphasize(WR, EPSEmphasisKind::Mismatch, Coaching, 2.f);
    TestEqual(TEXT("The mismatch outranks the callout"), MarkedStencil(WR), 2);
    Emphasis->AdvanceTime(1.f);
    TestEqual(TEXT("A second in, it holds"), MarkedStencil(WR), 2);
    Emphasis->AdvanceTime(1.1f);
    TestEqual(TEXT("Run out: back to the callout"), MarkedStencil(WR), 1);
    TestEqual(TEXT("One request left"), Emphasis->GetRequests().Num(), 1);

    // A source clears all of its own.
    TestEqual(TEXT("Commentary takes its callout back"), Emphasis->ClearSource(Commentary), 1);
    TestEqual(TEXT("...unmarked"), MarkedStencil(WR), 0);
    TestEqual(TEXT("...no look"), Emphasis->GetEmphasis(WR).Look, EPSEmphasisLook::None);
    TestFalse(TEXT("A handle already gone can't be cleared"), Emphasis->ClearEmphasis(Callout));

    // The budget is two: the mismatch, then the newest callout.
    Emphasis->Emphasize(TE, EPSEmphasisKind::Highlight, Commentary);
    Emphasis->Emphasize(CB, EPSEmphasisKind::Mismatch, Coaching);
    Emphasis->Emphasize(LB, EPSEmphasisKind::Highlight, Commentary);
    TestEqual(TEXT("The mismatch is drawn"), MarkedStencil(CB), 2);
    TestEqual(TEXT("...and the newest callout"), MarkedStencil(LB), 1);
    TestEqual(TEXT("The oldest callout goes undrawn"), MarkedStencil(TE), 0);
    TestTrue(TEXT("...though it is still asked for"), Emphasis->GetEmphasis(TE).Look == EPSEmphasisLook::Highlight && !Emphasis->GetEmphasis(TE).bRendered);

    // The isolation replay: the receiver in the spotlight, everyone without a drawn look dimmed.
    const int32 Isolation = Emphasis->Spotlight(WR, Replay);
    TestTrue(TEXT("A spotlight is on"), Emphasis->IsSpotlightActive());
    TestEqual(TEXT("The receiver is in focus"), MarkedStencil(WR), 3);
    TestEqual(TEXT("The mismatch keeps its look"), MarkedStencil(CB), 2);
    TestEqual(TEXT("The callouts past the budget are dimmed"), MarkedStencil(LB), 4);
    TestEqual(TEXT("...both of them"), MarkedStencil(TE), 4);
    TestEqual(TEXT("...and read as dimmed"), Emphasis->GetEmphasis(TE).Look, EPSEmphasisLook::Dimmed);
    TestTrue(TEXT("The spotlight is lifted"), Emphasis->ClearEmphasis(Isolation));
    TestFalse(TEXT("No spotlight"), Emphasis->IsSpotlightActive());
    TestEqual(TEXT("The receiver is unmarked"), MarkedStencil(WR), 0);
    TestEqual(TEXT("The newest callout is back"), MarkedStencil(LB), 1);
    TestEqual(TEXT("The oldest is undrawn again"), MarkedStencil(TE), 0);

    // A Minimal tier draws nothing, but keeps every request.
    Emphasis->SetOverlayDetail(EPSOverlayDetail::Minimal);
    TestEqual(TEXT("Minimal: unmarked"), MarkedStencil(CB), 0);
    TestTrue(TEXT("...still asked for"), Emphasis->GetEmphasis(CB).Look == EPSEmphasisLook::Mismatch && !Emphasis->GetEmphasis(CB).bRendered);
    Emphasis->SetOverlayDetail(EPSOverlayDetail::Full);
    TestEqual(TEXT("Back on Full: drawn again"), MarkedStencil(CB), 2);

    Emphasis->ClearAll();
    TestTrue(TEXT("Everything cleared"), MarkedStencil(CB) == 0 && MarkedStencil(LB) == 0 && Emphasis->GetRequests().Num() == 0);

    DestroyTestWorld(World);
    return true;
}

#endif
