// PSBallLookTests.cpp -- Epic 147.4: the ball's look comes from data and never changes how it flies
//
// Tests covered:
//   1. Data/ball_look.json loads through UPSDataIngestion, equals FPSBallLookStyle's defaults and
//      validates; an unsound style is caught.
//   2. Applied to a ball: its mesh is the style's, stretched to a football's length along X and its
//      width across, in the style's colour; the mesh has no collision, and the ball's collision is
//      still its sphere.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PSBall.h"
#include "PSBallLook.h"
#include "PSDataIngestion.h"
#include "PSUITeamCatalog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSBallLookDataTest,
    "PlaySports.Ball.LookData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSBallLookDataTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSBallLookStyle Loaded;
    if (!TestTrue(TEXT("Data/ball_look.json loads"), Ingestion->LoadBallLookStyleFromJson(PSBallLook::GetDefaultStylePath(), Loaded)))
    {
        return false;
    }
    TestEqual(TEXT("...and validates"), PSBallLook::ValidateStyle(Loaded).Num(), 0);

    const FPSBallLookStyle Defaults;
    TestEqual(TEXT("MeshPath"), Loaded.MeshPath, Defaults.MeshPath);
    TestEqual(TEXT("MeshSizeCm"), Loaded.MeshSizeCm, Defaults.MeshSizeCm);
    TestEqual(TEXT("LengthCm"), Loaded.LengthCm, Defaults.LengthCm);
    TestEqual(TEXT("WidthCm"), Loaded.WidthCm, Defaults.WidthCm);
    TestEqual(TEXT("MaterialPath"), Loaded.MaterialPath, Defaults.MaterialPath);
    TestEqual(TEXT("ColorParameter"), Loaded.ColorParameter, Defaults.ColorParameter);
    TestEqual(TEXT("BallColor"), Loaded.BallColor, Defaults.BallColor);

    FPSBallLookStyle Broken;
    Broken.BallColor = TEXT("brown");
    Broken.WidthCm = 0.f;
    TestEqual(TEXT("An unsound style is caught, one problem each"), PSBallLook::ValidateStyle(Broken).Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSBallLookAppliedTest,
    "PlaySports.Ball.LookApplied",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSBallLookAppliedTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("World"), World))
    {
        return false;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSBall* Ball = World->SpawnActor<APSBall>(APSBall::StaticClass(), FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator, SpawnParams);
    UStaticMeshComponent* Mesh = Ball ? Ball->FindComponentByClass<UStaticMeshComponent>() : nullptr;
    if (!TestNotNull(TEXT("The ball has its mesh component"), Mesh))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }

    const FPSBallLookStyle Style = PSBallLook::LoadStyle(PSBallLook::GetDefaultStylePath());
    TestTrue(TEXT("The look applies"), PSBallLook::Apply(Mesh, Style, Ball));
    // GetStaticMesh may hand back a TObjectPtr; a raw pointer is what TestNotNull takes.
    const UStaticMesh* Shape = Mesh->GetStaticMesh();
    TestNotNull(TEXT("...with the style's mesh"), Shape);
    const FVector Scale = Mesh->GetRelativeScale3D();
    TestEqual(TEXT("...a football's length along X"), static_cast<double>(Scale.X * Style.MeshSizeCm), static_cast<double>(Style.LengthCm), 0.01);
    TestEqual(TEXT("...and its width across"), static_cast<double>(Scale.Y * Style.MeshSizeCm), static_cast<double>(Style.WidthCm), 0.01);
    TestEqual(TEXT("...both ways"), static_cast<double>(Scale.Z * Style.MeshSizeCm), static_cast<double>(Style.WidthCm), 0.01);

    UMaterialInstanceDynamic* Look = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
    FLinearColor Expected;
    UPSUITeamCatalog::ParseHexColor(Style.BallColor, Expected);
    FLinearColor Shown = FLinearColor::Black;
    if (TestNotNull(TEXT("...in a dynamic material"), Look))
    {
        TestTrue(TEXT("...that has the colour parameter"), Look->GetVectorParameterValue(FHashedMaterialParameterInfo(Style.ColorParameter), Shown));
        TestTrue(TEXT("...set to the ball's colour"), Shown.Equals(Expected, 0.001f));
    }

    TestTrue(TEXT("The mesh doesn't collide"), Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    const USphereComponent* Sphere = Ball->FindComponentByClass<USphereComponent>();
    TestTrue(TEXT("The ball still collides as its sphere"), Sphere && Sphere->GetCollisionEnabled() != ECollisionEnabled::NoCollision);

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
