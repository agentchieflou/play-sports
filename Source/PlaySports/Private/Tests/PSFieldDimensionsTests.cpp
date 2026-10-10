// PSFieldDimensionsTests.cpp -- the field has one frame (PSField), and everything uses it
//
// Tests covered:
//   1. Data/field_dimensions.json loads through UPSDataIngestion, equals the struct's defaults and
//      validates; unsound dimensions are caught. Yard lines and world X convert both ways on the
//      game mode's line of scrimmage (PSGameStateEvents::LineOfScrimmageFor), spot for spot.
//   2. The field grid's volumes sit on the game mode's yard lines: each end zone's goal line is
//      the game mode's 0 and 100, its end line the end zone's depth beyond, the sidelines at
//      half the field's width; the grid's own coordinates agree with the game mode's. A carrier
//      crossing a sideline volume is reported at the game mode's yard line, and one in the far
//      end zone at its goal line. The broadcast camera's scrimmage shot is on the same line.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "PSBroadcastCamera.h"
#include "PSDataIngestion.h"
#include "PSEndZoneVolume.h"
#include "PSFieldDimensions.h"
#include "PSFieldGrid.h"
#include "PSGameStateEvents.h"
#include "PSOutOfBoundsVolume.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSFieldDimensionsTests
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

    /** A volume's box: its world centre and half-size. */
    static bool GetBox(const AActor* Volume, FVector& OutCenter, FVector& OutExtent)
    {
        const UBoxComponent* Box = Volume ? Volume->FindComponentByClass<UBoxComponent>() : nullptr;
        if (!Box)
        {
            return false;
        }
        OutCenter = Box->GetComponentLocation();
        OutExtent = Box->GetScaledBoxExtent();
        return true;
    }

    static AActor* FindVolume(const TArray<AActor*>& Volumes, FName Tag, TFunctionRef<bool(const FVector&)> Where)
    {
        for (AActor* Volume : Volumes)
        {
            FVector Center;
            FVector Extent;
            if (Volume && Volume->Tags.Contains(Tag) && GetBox(Volume, Center, Extent) && Where(Center))
            {
                return Volume;
            }
        }
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The dimensions, and yards to world space both ways
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFieldDimensionsDataTest,
    "PlaySports.Field.DimensionsAndYardLines",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFieldDimensionsDataTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSFieldDimensions Loaded;
    if (!TestTrue(TEXT("Data/field_dimensions.json loads"), Ingestion->LoadFieldDimensionsFromJson(PSField::GetDefaultDataPath(), Loaded)))
    {
        return false;
    }
    const FPSFieldDimensions Defaults;
    TestEqual(TEXT("The file's yard is the struct's"), Loaded.CentimetresPerYard, Defaults.CentimetresPerYard);
    TestEqual(TEXT("...its length"), Loaded.FieldLengthYards, Defaults.FieldLengthYards);
    TestEqual(TEXT("...its end zones"), Loaded.EndZoneDepthYards, Defaults.EndZoneDepthYards);
    TestEqual(TEXT("...its width"), Loaded.FieldWidthYards, Defaults.FieldWidthYards, 0.0001f);
    TestEqual(TEXT("...the boundaries' depth"), Loaded.OutOfBoundsDepthYards, Defaults.OutOfBoundsDepthYards);
    TestEqual(TEXT("...and height"), Loaded.BoundaryHeightCm, Defaults.BoundaryHeightCm);
    TestEqual(TEXT("The file is sound"), PSField::Validate(Loaded).Num(), 0);
    TestEqual(TEXT("PSField runs on the file's field"), PSField::GetDimensions().CentimetresPerYard, Loaded.CentimetresPerYard);

    FPSFieldDimensions Bad;
    Bad.CentimetresPerYard = 0.f;
    Bad.FieldLengthYards = -100.f;
    Bad.FieldWidthYards = 0.f;
    TestEqual(TEXT("A zero yard, a negative length and no width are caught"), PSField::Validate(Bad).Num(), 3);

    // The game mode's line of scrimmage is the field's frame, spot for spot, both ways.
    bool bAllAgree = true;
    for (int32 YardLine = 0; YardLine <= 100; ++YardLine)
    {
        const FVector Line = PSGameStateEvents::LineOfScrimmageFor(YardLine);
        bAllAgree &= Line.Equals(PSField::YardLineToWorld(YardLine)) && PSField::WorldToSpot(Line) == YardLine;
    }
    TestTrue(TEXT("Every yard line is the field's, and reads back as itself"), bAllAgree);
    TestEqual(TEXT("The offense's goal line is X = 0"), static_cast<double>(PSGameStateEvents::LineOfScrimmageFor(0).X), static_cast<double>(PSField::GoalLineX(false)), 0.01);
    TestEqual(TEXT("...the far one 100 yards on"), static_cast<double>(PSGameStateEvents::LineOfScrimmageFor(100).X), static_cast<double>(PSField::GoalLineX(true)), 0.01);
    TestEqual(TEXT("The 50 is midfield"), static_cast<double>(PSGameStateEvents::LineOfScrimmageFor(50).X), static_cast<double>(PSField::MidfieldX()), 0.01);
    TestEqual(TEXT("A spot in an end zone stays on the field"), PSField::WorldToSpot(PSField::YardLineToWorld(-4.f)), 0);
    TestEqual(TEXT("...at either end"), PSField::WorldToSpot(PSField::YardLineToWorld(104.f)), 100);
    TestEqual(TEXT("Half a yard rounds to the next"), PSField::WorldToSpot(PSField::YardLineToWorld(36.5f)), 37);
    TestEqual(TEXT("Yards and centimetres convert both ways"), PSField::CentimetresToYards(PSField::YardsToCentimetres(12.f)), 12.f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The volumes' lines are the game mode's
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSFieldGridAgreesTest,
    "PlaySports.Field.VolumesSitOnTheGameModesYardLines",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSFieldGridAgreesTest::RunTest(const FString& Parameters)
{
    using namespace PSFieldDimensionsTests;

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

    // A grid where a level might put it, away from the origin: the field is the game's anyway.
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSFieldGrid* Grid = World->SpawnActor<APSFieldGrid>(APSFieldGrid::StaticClass(), FVector(1234.f, -567.f, 0.f), FRotator::ZeroRotator, SpawnParams);
    if (!TestNotNull(TEXT("Field grid"), Grid))
    {
        DestroyTestWorld(World);
        return false;
    }
    const TArray<AActor*> Volumes = Grid->SpawnBoundaryVolumes();
    TestEqual(TEXT("Two end zones and four out-of-bounds volumes"), Volumes.Num(), 6);
    TestEqual(TEXT("Spawning again makes no more"), Grid->SpawnBoundaryVolumes().Num(), 6);

    const FPSFieldDimensions& Field = PSField::GetDimensions();
    const double Tolerance = 0.01;
    const double FarGoalX = PSGameStateEvents::LineOfScrimmageFor(100).X;
    const double NearGoalX = PSGameStateEvents::LineOfScrimmageFor(0).X;
    const double EndZoneDepth = PSField::YardsToCentimetres(Field.EndZoneDepthYards);
    const double SidelineY = PSField::SidelineY();
    FVector Center;
    FVector Extent;

    // The end zones: their goal lines are the game mode's 0 and 100.
    AActor* FarEndZone = FindVolume(Volumes, TEXT("EndZoneB"), [](const FVector&) { return true; });
    AActor* NearEndZone = FindVolume(Volumes, TEXT("EndZoneA"), [](const FVector&) { return true; });
    if (TestTrue(TEXT("The far end zone"), FarEndZone && GetBox(FarEndZone, Center, Extent)))
    {
        TestEqual(TEXT("...its goal line is the game mode's 100"), static_cast<double>(Center.X - Extent.X), FarGoalX, Tolerance);
        TestEqual(TEXT("...its end line the end zone's depth beyond"), static_cast<double>(Center.X + Extent.X), FarGoalX + EndZoneDepth, Tolerance);
        TestEqual(TEXT("...sideline to sideline"), static_cast<double>(Extent.Y), SidelineY, Tolerance);
        TestFalse(TEXT("...the far end zone"), Cast<APSEndZoneVolume>(FarEndZone)->bIsEndZoneA);
    }
    if (TestTrue(TEXT("The near end zone"), NearEndZone && GetBox(NearEndZone, Center, Extent)))
    {
        TestEqual(TEXT("...its goal line is the game mode's 0"), static_cast<double>(Center.X + Extent.X), NearGoalX, Tolerance);
        TestEqual(TEXT("...its end line behind it"), static_cast<double>(Center.X - Extent.X), NearGoalX - EndZoneDepth, Tolerance);
    }

    // The sidelines and end lines: out of bounds starts where the field stops.
    AActor* LeftSideline = FindVolume(Volumes, TEXT("OutOfBounds"), [](const FVector& At) { return At.Y < -1.f; });
    AActor* RightSideline = FindVolume(Volumes, TEXT("OutOfBounds"), [](const FVector& At) { return At.Y > 1.f; });
    AActor* FarEndLine = FindVolume(Volumes, TEXT("OutOfBounds"), [FarGoalX](const FVector& At) { return At.X > FarGoalX; });
    if (TestTrue(TEXT("Both sidelines"), LeftSideline && RightSideline && GetBox(RightSideline, Center, Extent)))
    {
        TestEqual(TEXT("...out of bounds from half the field's width"), static_cast<double>(Center.Y - Extent.Y), SidelineY, Tolerance);
        TestTrue(TEXT("...along the whole field, end zones included"), Center.X - Extent.X <= PSField::EndLineX(false) && Center.X + Extent.X >= PSField::EndLineX(true));
    }
    if (TestTrue(TEXT("The far end line"), FarEndLine && GetBox(FarEndLine, Center, Extent)))
    {
        TestEqual(TEXT("...out of bounds from the end line"), static_cast<double>(Center.X - Extent.X), FarGoalX + EndZoneDepth, Tolerance);
    }

    // The grid's own coordinates are the game mode's.
    const float MiddleLateral = Field.FieldWidthYards * 0.5f;
    TestTrue(TEXT("The grid's 35 is the game mode's 35"), Grid->GetWorldPositionFromFieldCoordinate(35.f, MiddleLateral).Equals(PSGameStateEvents::LineOfScrimmageFor(35), Tolerance));
    float YardLine = 0.f;
    float Lateral = 0.f;
    Grid->GetFieldCoordinateFromWorldPosition(PSGameStateEvents::LineOfScrimmageFor(62), YardLine, Lateral);
    TestTrue(TEXT("...and reads back as the 62, mid-field"), FMath::IsNearlyEqual(YardLine, 62.f, 0.01f) && FMath::IsNearlyEqual(Lateral, MiddleLateral, 0.01f));
    bool bEndZoneA = false;
    TestTrue(TEXT("Behind the offense's goal line is End Zone A"), Grid->IsLocationInEndZone(PSField::YardLineToWorld(-5.f), bEndZoneA) && bEndZoneA);
    TestTrue(TEXT("Past the far goal line is End Zone B"), Grid->IsLocationInEndZone(PSField::YardLineToWorld(105.f), bEndZoneA) && !bEndZoneA);
    TestFalse(TEXT("The 99 is on the field"), Grid->IsLocationInEndZone(PSGameStateEvents::LineOfScrimmageFor(99), bEndZoneA));
    TestTrue(TEXT("Past the end line is out"), Grid->IsLocationOutOfBounds(PSField::YardLineToWorld(111.f)));
    TestTrue(TEXT("...and past a sideline"), Grid->IsLocationOutOfBounds(PSField::YardLineToWorld(40.f, MiddleLateral + 1.f)));
    TestEqual(TEXT("From the 30 the far goal line is 70 yards"), static_cast<double>(Grid->GetDistanceToGoalLine(PSGameStateEvents::LineOfScrimmageFor(30), true)), 70.0, Tolerance);

    // A carrier crossing them is reported on the game mode's yard lines.
    TArray<FPSTelemetryBoundaryCrossedEvent> Crossings;
    Bus->OnBoundaryCrossedMC.AddLambda([&Crossings](const FPSTelemetryBoundaryCrossedEvent& Event) { Crossings.Add(Event); });
    FPlayerAttributes Runner;
    Runner.PlayerId = TEXT("RB_FIELD");
    Runner.DisplayName = TEXT("Field Runner");
    Runner.Role = EPlayerRole::RunningBack;
    APSPlayerPawn* Carrier = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), PSField::YardLineToWorld(37.f, MiddleLateral + 0.5f) + FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator, SpawnParams);
    APSOutOfBoundsVolume* Sideline = Cast<APSOutOfBoundsVolume>(RightSideline);
    APSEndZoneVolume* EndZone = Cast<APSEndZoneVolume>(FarEndZone);
    if (TestTrue(TEXT("A carrier and the volumes"), Carrier && Sideline && EndZone))
    {
        Carrier->InitializePlayer(Runner);
        Carrier->GainPossession();
        TestTrue(TEXT("Over the sideline at the 37"), Sideline->ReportCrossing(Carrier));
        Carrier->SetActorLocation(PSField::YardLineToWorld(100.5f) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
        TestTrue(TEXT("Into the far end zone"), EndZone->ReportCrossing(Carrier));
        if (TestEqual(TEXT("Two crossings"), Crossings.Num(), 2))
        {
            TestTrue(TEXT("...out of bounds at the game mode's 37"), !Crossings[0].bEndZone && Crossings[0].YardLine == 37);
            TestTrue(TEXT("...in the end zone at the game mode's goal line"), Crossings[1].bEndZone && Crossings[1].YardLine == 100);
        }
    }

    // The broadcast camera's scrimmage shot is on the same line.
    APSBroadcastCamera* Camera = World->SpawnActor<APSBroadcastCamera>(APSBroadcastCamera::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (TestNotNull(TEXT("Broadcast camera"), Camera))
    {
        Camera->SnapToScrimmage(80.f);
        TestEqual(TEXT("The camera shoots the 80 where the game mode lines up"), static_cast<double>(Camera->GetActorLocation().X), static_cast<double>(PSGameStateEvents::LineOfScrimmageFor(80).X), Tolerance);
        TestTrue(TEXT("...and follows the whole field"), Camera->MinX <= PSField::EndLineX(false) + Tolerance && Camera->MaxX >= PSField::EndLineX(true) - Tolerance);
    }

    DestroyTestWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
