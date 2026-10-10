// PSCharacterLookTests.cpp -- Epic 147.2: each player wears the colours of the team he plays for
//
// Tests covered:
//   1. Data/character_look.json loads through UPSDataIngestion, equals FPSCharacterLookStyle's
//      defaults and validates; an unsound style is caught.
//   2. Without a character mesh, a pawn's look is the fallback body: the capsule's size, no
//      collision, neutral until the match is known. Given the match, an offense pawn plays for the
//      home team (it has the ball first) and a defense pawn for the away team, each in its team's
//      primary colour from the team identity data. When the ball changes hands the colours stay
//      until the sides line up again; then each pawn wears the other team's.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PSCharacterLook.h"
#include "PSDataIngestion.h"
#include "PSMatchSetup.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSUITeamCatalog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSCharacterLookTests
{
    /** The colour the fallback body's material shows. */
    static bool BodyColor(APSPlayerPawn* Pawn, const FPSCharacterLookStyle& Style, FLinearColor& OutColor)
    {
        UStaticMeshComponent* Body = Pawn ? Pawn->GetBodyMesh() : nullptr;
        const UMaterialInstanceDynamic* Look = Body ? Cast<UMaterialInstanceDynamic>(Body->GetMaterial(0)) : nullptr;
        return Look && Look->GetVectorParameterValue(FHashedMaterialParameterInfo(Style.FallbackColorParameter), OutColor);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCharacterLookDataTest,
    "PlaySports.Look.CharacterLookData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCharacterLookDataTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSCharacterLookStyle Loaded;
    if (!TestTrue(TEXT("Data/character_look.json loads"), Ingestion->LoadCharacterLookStyleFromJson(UPSCharacterLookComponent::GetDefaultStylePath(), Loaded)))
    {
        return false;
    }
    TestEqual(TEXT("...and validates"), UPSCharacterLookComponent::ValidateStyle(Loaded).Num(), 0);

    const FPSCharacterLookStyle Defaults;
    TestEqual(TEXT("SkeletalMeshPath"), Loaded.SkeletalMeshPath, Defaults.SkeletalMeshPath);
    TestEqual(TEXT("MeshYawDegrees"), Loaded.MeshYawDegrees, Defaults.MeshYawDegrees);
    TestEqual(TEXT("TeamColorParameter"), Loaded.TeamColorParameter, Defaults.TeamColorParameter);
    TestTrue(TEXT("PrimarySlots"), Loaded.PrimarySlots == Defaults.PrimarySlots);
    TestTrue(TEXT("SecondarySlots"), Loaded.SecondarySlots == Defaults.SecondarySlots);
    TestEqual(TEXT("FallbackMeshPath"), Loaded.FallbackMeshPath, Defaults.FallbackMeshPath);
    TestEqual(TEXT("FallbackMeshSizeCm"), Loaded.FallbackMeshSizeCm, Defaults.FallbackMeshSizeCm);
    TestEqual(TEXT("FallbackMaterialPath"), Loaded.FallbackMaterialPath, Defaults.FallbackMaterialPath);
    TestEqual(TEXT("FallbackColorParameter"), Loaded.FallbackColorParameter, Defaults.FallbackColorParameter);
    TestEqual(TEXT("NeutralColor"), Loaded.NeutralColor, Defaults.NeutralColor);

    FPSCharacterLookStyle Broken;
    Broken.SkeletalMeshPath = TEXT("Characters/Standin");
    Broken.NeutralColor = TEXT("grey");
    Broken.FallbackMeshSizeCm = 0.f;
    TestEqual(TEXT("An unsound style is caught, one problem each"), UPSCharacterLookComponent::ValidateStyle(Broken).Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCharacterLookTeamColorsTest,
    "PlaySports.Look.TeamColorsFollowTheSides",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCharacterLookTeamColorsTest::RunTest(const FString& Parameters)
{
    using namespace PSCharacterLookTests;

    const TArray<FName> TeamIds = UPSMatchSetup::LoadLeagueTeamIds(UPSUITeamCatalog::GetDefaultTeamsPath());
    if (!TestTrue(TEXT("The league has two teams to play"), TeamIds.Num() >= 2))
    {
        return false;
    }
    FLinearColor HomePrimary;
    FLinearColor HomeSecondary;
    FLinearColor AwayPrimary;
    FLinearColor AwaySecondary;
    TestTrue(TEXT("The home team has its colours in the team data"), UPSCharacterLookComponent::FindTeamColors(UPSUITeamCatalog::GetDefaultTeamsPath(), TeamIds[0], HomePrimary, HomeSecondary));
    TestTrue(TEXT("...and the away team"), UPSCharacterLookComponent::FindTeamColors(UPSUITeamCatalog::GetDefaultTeamsPath(), TeamIds[1], AwayPrimary, AwaySecondary));
    FLinearColor Unused;
    TestFalse(TEXT("A team that isn't there has none"), UPSCharacterLookComponent::FindTeamColors(UPSUITeamCatalog::GetDefaultTeamsPath(), TEXT("NoSuchTeam"), Unused, Unused));

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("World"), World))
    {
        return false;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);
    UPSTelemetryBus* Bus = World->GetSubsystem<UPSTelemetryBus>();

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSPlayerPawn* Offense = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator, SpawnParams);
    APSPlayerPawn* Defense = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), FVector(300.f, 0.f, 100.f), FRotator::ZeroRotator, SpawnParams);
    UPSCharacterLookComponent* OffenseLook = Offense ? Offense->GetCharacterLookComponent() : nullptr;
    UPSCharacterLookComponent* DefenseLook = Defense ? Defense->GetCharacterLookComponent() : nullptr;
    if (!TestNotNull(TEXT("Bus"), Bus) || !TestNotNull(TEXT("The offense pawn has a look"), OffenseLook) || !TestNotNull(TEXT("...and the defense pawn"), DefenseLook))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }
    Offense->TeamSide = EPSTeamSide::Offense;
    Defense->TeamSide = EPSTeamSide::Defense;

    // The fallback body: no character mesh named.
    FPSCharacterLookStyle Style = UPSCharacterLookComponent::LoadStyle(UPSCharacterLookComponent::GetDefaultStylePath());
    Style.SkeletalMeshPath.Empty();
    FLinearColor Neutral;
    UPSUITeamCatalog::ParseHexColor(Style.NeutralColor, Neutral);
    for (UPSCharacterLookComponent* Look : { OffenseLook, DefenseLook })
    {
        Look->SetStyle(Style);
        TestTrue(TEXT("The look applies"), Look->ApplyLook());
        TestFalse(TEXT("...as the fallback body"), Look->IsUsingCharacterMesh());
        Look->BindToBus(Bus);
    }
    UStaticMeshComponent* Body = Offense->GetBodyMesh();
    const UCapsuleComponent* Capsule = Offense->FindComponentByClass<UCapsuleComponent>();
    if (TestNotNull(TEXT("The body"), Body) && TestNotNull(TEXT("The capsule"), Capsule))
    {
        const UStaticMesh* Shape = Body->GetStaticMesh();
        TestNotNull(TEXT("The body has its mesh"), Shape);
        const FVector Scale = Body->GetRelativeScale3D();
        TestEqual(TEXT("...as wide as the capsule"), static_cast<double>(Scale.X * Style.FallbackMeshSizeCm), static_cast<double>(2.f * Capsule->GetScaledCapsuleRadius()), 0.01);
        TestEqual(TEXT("...as tall"), static_cast<double>(Scale.Z * Style.FallbackMeshSizeCm), static_cast<double>(2.f * Capsule->GetScaledCapsuleHalfHeight()), 0.01);
        TestTrue(TEXT("...and doesn't collide"), Body->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    }
    FLinearColor Shown;
    TestTrue(TEXT("Before the match is known: neutral"), BodyColor(Offense, Style, Shown) && Shown.Equals(Neutral, 0.001f));

    // The match: the home team has the ball first.
    UPSMatchSetup* Match = NewObject<UPSMatchSetup>();
    TestTrue(TEXT("The match has its two teams"), Match->SetTeams(TeamIds[0], TeamIds[1]));
    OffenseLook->SetMatchSetup(Match);
    DefenseLook->SetMatchSetup(Match);
    TestEqual(TEXT("The offense plays for the home team"), OffenseLook->GetTeamId(), TeamIds[0]);
    TestEqual(TEXT("The defense plays for the away team"), DefenseLook->GetTeamId(), TeamIds[1]);
    TestTrue(TEXT("...the offense in the home team's primary colour"), BodyColor(Offense, Style, Shown) && Shown.Equals(HomePrimary, 0.001f));
    TestTrue(TEXT("...the defense in the away team's"), BodyColor(Defense, Style, Shown) && Shown.Equals(AwayPrimary, 0.001f));

    // A turnover: nobody changes shirts until the sides line up again.
    FPSTelemetryGameStateEvent Turnover;
    Turnover.bHomeHasPossession = false;
    Bus->PublishGameState(Turnover);
    TestEqual(TEXT("The ball changes hands: the offense pawn's team holds until the lineup"), OffenseLook->GetTeamId(), TeamIds[0]);
    FPSTelemetryLineupEvent Lineup;
    Bus->PublishLineup(Lineup);
    TestEqual(TEXT("At the lineup the offense pawn plays for the away team"), OffenseLook->GetTeamId(), TeamIds[1]);
    TestEqual(TEXT("...and the defense pawn for the home team"), DefenseLook->GetTeamId(), TeamIds[0]);
    TestTrue(TEXT("...each in its new team's colour"), BodyColor(Offense, Style, Shown) && Shown.Equals(AwayPrimary, 0.001f)
        && BodyColor(Defense, Style, Shown) && Shown.Equals(HomePrimary, 0.001f));

    for (UPSCharacterLookComponent* Look : { OffenseLook, DefenseLook })
    {
        Look->UnbindFromBus();
    }
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
