// PSRenderCaptureTests.cpp -- Lane V1: the render capture's logic, with no GPU
//
// The capture itself renders on the CI runner's GPU (.github/workflows/render.yml). What it decides
// is tested here, headless:
//
// Tests covered:
//   1. The shipped views file (Data/render_views.json) loads and is sound, has the starting
//      views, and every Field view's camera can be placed.
//   2. Views parse from JSON: a Player view's anchor, role, index, points and field of view.
//   3. Validation catches each kind of mistake.
//   4. Camera placement: a Field view in PSField's frame (yards to world), a Player view from the
//      ground under its player, and a camera on its own target refused.
//   5. The anchor player: a role's players counted from the -Y sideline, ties by X, out of range.
//   6. The switches: -PSRenderCapture and its Out, Views, Only and Commit values.
//   7. The report: pass only with every view written and no failure, exit codes 0 and 1, and
//      index.json's fields.
//   8. -PSRenderCaptureOnly keeps the file's order.
//   9. Only the pre-snap phase counts as lined up (bCaptureOnlyPreSnap's test of PhaseChange).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "PSDataIngestion.h"
#include "PSFieldDimensions.h"
#include "PSPlaySimulation.h"
#include "PSRenderCapture.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSRenderCaptureTests
{
    /** A sound settings object with one Field view, for the tests to break. */
    FPSRenderCaptureSettings MakeSettings()
    {
        FPSRenderCaptureSettings Made;
        FPSRenderView View;
        View.ViewId = TEXT("wide");
        View.CameraYards = FVector(20.f, -70.f, 25.f);
        View.TargetYards = FVector(20.f, 0.f, 0.f);
        View.FieldOfViewDegrees = 40.f;
        Made.RenderViews.Add(View);
        return Made;
    }

    bool HasProblemContaining(const TArray<FString>& Problems, const TCHAR* Text)
    {
        return Problems.ContainsByPredicate([Text](const FString& Problem) { return Problem.Contains(Text); });
    }

    FPSRenderShot MakeShot(const TCHAR* Id, bool bWritten)
    {
        FPSRenderShot Shot;
        Shot.ViewId = Id;
        Shot.FileName = FString(Id) + TEXT(".png");
        Shot.bWritten = bWritten;
        return Shot;
    }
}

// ---------------------------------------------------------------------------
// 1. The shipped views file
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRenderShippedViewsTest,
    "PlaySports.Render.Views.ShippedFileIsSound",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRenderShippedViewsTest::RunTest(const FString& Parameters)
{
    FPSRenderCaptureSettings Loaded;
    TArray<FString> Problems;
    const bool bLoaded = UPSRenderCapture::LoadSettings(UPSRenderCapture::GetDefaultViewsPath(), Loaded, Problems);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    if (!TestTrue(TEXT("Data/render_views.json loads and is sound"), bLoaded))
    {
        return false;
    }

    const TCHAR* const Required[] = { TEXT("broadcast_wide"), TEXT("all22_endzone"), TEXT("sideline_field_level"),
        TEXT("player_closeup"), TEXT("helmet_closeup"), TEXT("stadium_aerial"), TEXT("turf_low") };
    for (const TCHAR* Id : Required)
    {
        TestTrue(FString::Printf(TEXT("The views include %s"), Id),
            Loaded.RenderViews.ContainsByPredicate([Id](const FPSRenderView& View) { return View.ViewId == FName(Id); }));
    }
    TestEqual(TEXT("Rendered 1920 wide"), Loaded.ResolutionX, 1920);
    TestEqual(TEXT("...and 1080 high"), Loaded.ResolutionY, 1080);
    TestEqual(TEXT("Play Now lines up 22"), Loaded.ExpectedPlayers, 22);
    TestTrue(TEXT("Views are taken with the players lined up"), Loaded.bCaptureOnlyPreSnap);

    for (const FPSRenderView& View : Loaded.RenderViews)
    {
        FVector Location;
        FRotator Rotation;
        TestTrue(FString::Printf(TEXT("%s's camera can be placed"), *View.ViewId.ToString()),
            UPSRenderCapture::ComputeCameraPose(View, FVector(1500.f, -2000.f, 0.f), Location, Rotation));
        TestTrue(FString::Printf(TEXT("%s's camera is above the ground"), *View.ViewId.ToString()), Location.Z > 0.f);
        if (View.ViewId.ToString().EndsWith(TEXT("closeup")))
        {
            TestEqual(FString::Printf(TEXT("%s frames a player"), *View.ViewId.ToString()), View.Anchor, EPSRenderViewAnchor::Player);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 2. Views parse from JSON
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRenderViewParsingTest,
    "PlaySports.Render.Views.ParseFromJson",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRenderViewParsingTest::RunTest(const FString& Parameters)
{
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Tests") / TEXT("RenderViews");
    IFileManager::Get().MakeDirectory(*Dir, true);
    const FString Path = Dir / TEXT("render_views.json");
    const FString Json = TEXT(R"json({
        "ResolutionX": 1280, "ResolutionY": 720, "ExpectedPlayers": 0,
        "MatchTimeoutSeconds": 10.0, "CompileTimeoutSeconds": 20.0, "TotalTimeoutSeconds": 30.0,
        "WarmupFrames": 4, "WarmupSeconds": 0.5, "SettleFrames": 3, "SettleSeconds": 0.25,
        "MeasureFrames": 2, "ScreenshotTimeoutFrames": 9, "bCaptureOnlyPreSnap": false, "StreamingWaitSeconds": 0.0,
        "RenderViews": [
            { "ViewId": "close", "Description": "a close-up", "Anchor": "Player", "PlayerRole": "Linebacker", "PlayerIndex": 2,
              "CameraYards": { "X": 3.0, "Y": -1.0, "Z": 1.5 }, "TargetYards": { "X": 0.0, "Y": 0.0, "Z": 1.4 },
              "FieldOfViewDegrees": 22.5 }
        ]
    })json");
    if (!TestTrue(TEXT("The scratch views file is written"), FFileHelper::SaveStringToFile(Json, *Path)))
    {
        return false;
    }

    FPSRenderCaptureSettings Loaded;
    TArray<FString> Problems;
    TestTrue(TEXT("It loads and is sound"), UPSRenderCapture::LoadSettings(Path, Loaded, Problems));
    TestEqual(TEXT("No problems"), Problems.Num(), 0);
    TestEqual(TEXT("ResolutionX"), Loaded.ResolutionX, 1280);
    TestEqual(TEXT("ResolutionY"), Loaded.ResolutionY, 720);
    TestEqual(TEXT("ExpectedPlayers"), Loaded.ExpectedPlayers, 0);
    TestEqual(TEXT("WarmupFrames"), Loaded.WarmupFrames, 4);
    TestEqual(TEXT("SettleSeconds"), Loaded.SettleSeconds, 0.25f);
    TestEqual(TEXT("MeasureFrames"), Loaded.MeasureFrames, 2);
    TestEqual(TEXT("ScreenshotTimeoutFrames"), Loaded.ScreenshotTimeoutFrames, 9);
    TestFalse(TEXT("bCaptureOnlyPreSnap"), Loaded.bCaptureOnlyPreSnap);
    TestEqual(TEXT("StreamingWaitSeconds"), Loaded.StreamingWaitSeconds, 0.f);
    if (!TestEqual(TEXT("One view"), Loaded.RenderViews.Num(), 1))
    {
        return false;
    }
    const FPSRenderView& View = Loaded.RenderViews[0];
    TestEqual(TEXT("ViewId"), View.ViewId, FName(TEXT("close")));
    TestEqual(TEXT("Description"), View.Description, FString(TEXT("a close-up")));
    TestEqual(TEXT("Anchor"), View.Anchor, EPSRenderViewAnchor::Player);
    TestEqual(TEXT("PlayerRole"), View.PlayerRole, EPlayerRole::Linebacker);
    TestEqual(TEXT("PlayerIndex"), View.PlayerIndex, 2);
    TestEqual(TEXT("CameraYards"), View.CameraYards, FVector(3.f, -1.f, 1.5f));
    TestEqual(TEXT("TargetYards"), View.TargetYards, FVector(0.f, 0.f, 1.4f));
    TestEqual(TEXT("FieldOfViewDegrees"), View.FieldOfViewDegrees, 22.5f);

    FPSRenderCaptureSettings Missing;
    TestFalse(TEXT("A missing file doesn't load"), UPSRenderCapture::LoadSettings(Dir / TEXT("no_such_file.json"), Missing, Problems));
    TestEqual(TEXT("...and says so"), Problems.Num(), 1);

    IFileManager::Get().Delete(*Path);
    return true;
}

// ---------------------------------------------------------------------------
// 3. Validation
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRenderValidationTest,
    "PlaySports.Render.Views.ValidationCatchesMistakes",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRenderValidationTest::RunTest(const FString& Parameters)
{
    using namespace PSRenderCaptureTests;

    TestEqual(TEXT("A sound settings object has no problems"), UPSRenderCapture::ValidateSettings(MakeSettings()).Num(), 0);

    FPSRenderCaptureSettings Broken = MakeSettings();
    Broken.ResolutionX = 10;
    TestTrue(TEXT("A tiny resolution"), HasProblemContaining(UPSRenderCapture::ValidateSettings(Broken), TEXT("Resolution")));

    Broken = MakeSettings();
    Broken.MeasureFrames = Broken.SettleFrames + 1;
    TestTrue(TEXT("Measuring more frames than settle"), HasProblemContaining(UPSRenderCapture::ValidateSettings(Broken), TEXT("MeasureFrames")));

    Broken = MakeSettings();
    Broken.StreamingWaitSeconds = -1.f;
    TestTrue(TEXT("A negative streaming wait"), HasProblemContaining(UPSRenderCapture::ValidateSettings(Broken), TEXT("StreamingWaitSeconds")));

    Broken = MakeSettings();
    Broken.CompileTimeoutSeconds = 0.f;
    TestTrue(TEXT("A zero limit"), HasProblemContaining(UPSRenderCapture::ValidateSettings(Broken), TEXT("CompileTimeoutSeconds")));

    Broken = MakeSettings();
    Broken.RenderViews.Reset();
    TestTrue(TEXT("No views"), HasProblemContaining(UPSRenderCapture::ValidateSettings(Broken), TEXT("at least one view")));

    Broken = MakeSettings();
    // A copy first: adding an array's own element to it asserts.
    FPSRenderView Twin = Broken.RenderViews[0];
    Twin.ViewId = TEXT("WIDE");
    Broken.RenderViews.Add(Twin);
    TestTrue(TEXT("A ViewId used twice (names ignore case)"), HasProblemContaining(UPSRenderCapture::ValidateSettings(Broken), TEXT("used twice")));

    Broken = MakeSettings();
    Broken.RenderViews[0].ViewId = TEXT("has space");
    TestTrue(TEXT("A ViewId that can't name a file"), HasProblemContaining(UPSRenderCapture::ValidateSettings(Broken), TEXT("letters, digits")));

    Broken = MakeSettings();
    Broken.RenderViews[0].ViewId = NAME_None;
    TestTrue(TEXT("An empty ViewId"), HasProblemContaining(UPSRenderCapture::ValidateSettings(Broken), TEXT("must not be empty")));

    Broken = MakeSettings();
    Broken.RenderViews[0].FieldOfViewDegrees = 179.f;
    TestTrue(TEXT("A field of view past 170"), HasProblemContaining(UPSRenderCapture::ValidateSettings(Broken), TEXT("FieldOfViewDegrees")));

    Broken = MakeSettings();
    Broken.RenderViews[0].TargetYards = Broken.RenderViews[0].CameraYards;
    TestTrue(TEXT("A camera on its own target"), HasProblemContaining(UPSRenderCapture::ValidateSettings(Broken), TEXT("must differ")));

    Broken = MakeSettings();
    Broken.RenderViews[0].Anchor = EPSRenderViewAnchor::Player;
    Broken.RenderViews[0].PlayerIndex = -1;
    TestTrue(TEXT("A negative player index"), HasProblemContaining(UPSRenderCapture::ValidateSettings(Broken), TEXT("PlayerIndex")));
    return true;
}

// ---------------------------------------------------------------------------
// 4. Camera placement
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRenderCameraPoseTest,
    "PlaySports.Render.CameraPose",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRenderCameraPoseTest::RunTest(const FString& Parameters)
{
    const float Yard = PSField::YardsToCentimetres(1.f);

    // A Field view: high on the -Y sideline at the 20, looking across at the middle of the field.
    FPSRenderView Wide;
    Wide.ViewId = TEXT("wide");
    Wide.CameraYards = FVector(20.f, -70.f, 25.f);
    Wide.TargetYards = FVector(20.f, 0.f, 0.f);
    FVector Location;
    FRotator Rotation;
    TestTrue(TEXT("A Field view is placed"), UPSRenderCapture::ComputeCameraPose(Wide, FVector(999.f, 999.f, 999.f), Location, Rotation));
    TestEqual(TEXT("...in PSField's frame (the anchor is ignored)"), Location, FVector(20.f * Yard, -70.f * Yard, 25.f * Yard));
    TestEqual(TEXT("...on the yard line PSField puts the 20 on"), Location.X, PSField::YardLineToWorld(20.f).X);
    TestEqual(TEXT("...looking across the field (+Y)"), Rotation.Yaw, 90.0, 0.01);
    TestEqual(TEXT("...and down at the middle"), Rotation.Pitch, -FMath::RadiansToDegrees(FMath::Atan2(25.0, 70.0)), 0.01);
    TestEqual(TEXT("...level (no roll)"), Rotation.Roll, 0.0, 0.01);

    // A Player view: 3.5 yards in front of (downfield of) the player, looking back at his chest.
    FPSRenderView Close;
    Close.ViewId = TEXT("close");
    Close.Anchor = EPSRenderViewAnchor::Player;
    Close.CameraYards = FVector(3.5f, 0.f, 1.5f);
    Close.TargetYards = FVector(0.f, 0.f, 1.5f);
    const FVector Ground(1500.f, -2000.f, 0.f);
    TestTrue(TEXT("A Player view is placed"), UPSRenderCapture::ComputeCameraPose(Close, Ground, Location, Rotation));
    TestEqual(TEXT("...from the ground under its player"), Location, Ground + FVector(3.5f * Yard, 0.f, 1.5f * Yard));
    TestEqual(TEXT("...looking back at him (-X)"), FMath::Abs(Rotation.Yaw), 180.0, 0.01);
    TestEqual(TEXT("...level"), Rotation.Pitch, 0.0, 0.01);

    // The forward vector points from the camera at the target, for any view.
    FPSRenderView Diagonal;
    Diagonal.ViewId = TEXT("diagonal");
    Diagonal.CameraYards = FVector(8.f, -12.f, 0.15f);
    Diagonal.TargetYards = FVector(20.f, 4.f, 0.4f);
    TestTrue(TEXT("A diagonal view is placed"), UPSRenderCapture::ComputeCameraPose(Diagonal, FVector::ZeroVector, Location, Rotation));
    const FVector Expected = (FVector(20.f, 4.f, 0.4f) - FVector(8.f, -12.f, 0.15f)).GetSafeNormal();
    TestTrue(TEXT("...looking at its target"), Rotation.Vector().Equals(Expected, 0.001));

    FPSRenderView Degenerate = Wide;
    Degenerate.TargetYards = Degenerate.CameraYards;
    TestFalse(TEXT("A camera on its own target is refused"), UPSRenderCapture::ComputeCameraPose(Degenerate, FVector::ZeroVector, Location, Rotation));
    return true;
}

// ---------------------------------------------------------------------------
// 5. The anchor player
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRenderAnchorPlayerTest,
    "PlaySports.Render.AnchorPlayer",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRenderAnchorPlayerTest::RunTest(const FString& Parameters)
{
    TArray<TPair<EPlayerRole, FVector>> Players;
    Players.Emplace(EPlayerRole::WideReceiver, FVector(1900.f, 2000.f, 88.f));   // 0: far side
    Players.Emplace(EPlayerRole::Quarterback, FVector(1500.f, -3000.f, 88.f));   // 1: not a receiver
    Players.Emplace(EPlayerRole::WideReceiver, FVector(1900.f, -2200.f, 88.f));  // 2: nearest the -Y sideline
    Players.Emplace(EPlayerRole::WideReceiver, FVector(1800.f, 2000.f, 88.f));   // 3: same Y as 0, lower X

    TestEqual(TEXT("Index 0 is the receiver with the lowest Y"), UPSRenderCapture::PickAnchorPlayer(Players, EPlayerRole::WideReceiver, 0), 2);
    TestEqual(TEXT("A tie on Y goes to the lower X"), UPSRenderCapture::PickAnchorPlayer(Players, EPlayerRole::WideReceiver, 1), 3);
    TestEqual(TEXT("...then the other"), UPSRenderCapture::PickAnchorPlayer(Players, EPlayerRole::WideReceiver, 2), 0);
    TestEqual(TEXT("Past the last receiver: none"), UPSRenderCapture::PickAnchorPlayer(Players, EPlayerRole::WideReceiver, 3), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("A role with nobody on the field: none"), UPSRenderCapture::PickAnchorPlayer(Players, EPlayerRole::Linebacker, 0), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("A negative index: none"), UPSRenderCapture::PickAnchorPlayer(Players, EPlayerRole::WideReceiver, -1), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("The one quarterback"), UPSRenderCapture::PickAnchorPlayer(Players, EPlayerRole::Quarterback, 0), 1);
    return true;
}

// ---------------------------------------------------------------------------
// 6. The switches
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRenderCommandLineTest,
    "PlaySports.Render.CommandLine",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRenderCommandLineTest::RunTest(const FString& Parameters)
{
    FPSRenderCaptureOptions Parsed;
    TestFalse(TEXT("Without -PSRenderCapture there is no capture"),
        UPSRenderCapture::ParseCommandLine(TEXT("/Game/Maps/GameMap -game -PSRenderCaptureOut=C:/Renders"), Parsed));
    TestFalse(TEXT("...nor without a command line"), UPSRenderCapture::ParseCommandLine(nullptr, Parsed));

    TestTrue(TEXT("-PSRenderCapture alone asks for a capture"), UPSRenderCapture::ParseCommandLine(TEXT("/Game/Maps/GameMap -game -PSRenderCapture -log"), Parsed));
    TestEqual(TEXT("...to Saved/Renders"), Parsed.OutputDir, UPSRenderCapture::GetDefaultOutputDir());
    TestEqual(TEXT("...of Data/render_views.json"), Parsed.ViewsPath, UPSRenderCapture::GetDefaultViewsPath());
    TestEqual(TEXT("...every view"), Parsed.OnlyViews.Num(), 0);
    TestTrue(TEXT("...naming no commit"), Parsed.Commit.IsEmpty());

    TestTrue(TEXT("With every switch"), UPSRenderCapture::ParseCommandLine(
        TEXT("-game -PSRenderCapture -PSRenderCaptureOut=C:/Work/Renders -PSRenderCaptureViews=C:/Work/views.json -PSRenderCaptureOnly=turf_low+broadcast_wide -PSRenderCaptureCommit=abc123"), Parsed));
    TestEqual(TEXT("...the output folder"), Parsed.OutputDir, FPaths::ConvertRelativePathToFull(TEXT("C:/Work/Renders")));
    TestEqual(TEXT("...the views file"), Parsed.ViewsPath, FPaths::ConvertRelativePathToFull(TEXT("C:/Work/views.json")));
    TestEqual(TEXT("...the commit"), Parsed.Commit, FString(TEXT("abc123")));
    if (TestEqual(TEXT("...two views"), Parsed.OnlyViews.Num(), 2))
    {
        TestEqual(TEXT("...the first"), Parsed.OnlyViews[0], FName(TEXT("turf_low")));
        TestEqual(TEXT("...the second"), Parsed.OnlyViews[1], FName(TEXT("broadcast_wide")));
    }
    return true;
}

// ---------------------------------------------------------------------------
// 7. The report and the exit code
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRenderReportTest,
    "PlaySports.Render.ReportAndExitCode",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRenderReportTest::RunTest(const FString& Parameters)
{
    using namespace PSRenderCaptureTests;

    FPSRenderCaptureReport Passing;
    Passing.Commit = TEXT("abc123");
    Passing.RHIName = TEXT("D3D12");
    Passing.Shots.Add(MakeShot(TEXT("broadcast_wide"), true));
    Passing.Shots.Add(MakeShot(TEXT("turf_low"), true));
    Passing.Shots[0].ResolutionX = 1920;
    Passing.Shots[0].ResolutionY = 1080;
    Passing.Shots[0].GpuMs = 8.25f;
    Passing.Shots[1].Restarts = 2;
    UPSRenderCapture::FinalizeReport(Passing, 2);
    TestTrue(TEXT("Every view written, nothing failed: a pass"), Passing.bPassed);
    TestEqual(TEXT("...exit code 0"), UPSRenderCapture::GetExitCode(Passing), 0);

    FPSRenderCaptureReport Unwritten = Passing;
    Unwritten.Shots[1].bWritten = false;
    UPSRenderCapture::FinalizeReport(Unwritten, 2);
    TestFalse(TEXT("A view without its PNG fails"), Unwritten.bPassed);
    TestEqual(TEXT("...exit code 1"), UPSRenderCapture::GetExitCode(Unwritten), 1);

    FPSRenderCaptureReport Short = Passing;
    UPSRenderCapture::FinalizeReport(Short, 3);
    TestFalse(TEXT("Fewer shots than views fails"), Short.bPassed);

    FPSRenderCaptureReport Failed = Passing;
    Failed.Failures.Add(TEXT("the match was not ready"));
    UPSRenderCapture::FinalizeReport(Failed, 2);
    TestFalse(TEXT("Any failure fails"), Failed.bPassed);

    FPSRenderCaptureReport Empty;
    UPSRenderCapture::FinalizeReport(Empty, 0);
    TestFalse(TEXT("Nothing to render fails"), Empty.bPassed);
    TestEqual(TEXT("...exit code 1"), UPSRenderCapture::GetExitCode(Empty), 1);

    TSharedPtr<FJsonObject> Index;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(UPSRenderCapture::BuildIndexJson(Passing));
    if (!TestTrue(TEXT("index.json is JSON"), FJsonSerializer::Deserialize(Reader, Index) && Index.IsValid()))
    {
        return false;
    }
    TestTrue(TEXT("Passed"), Index->GetBoolField(TEXT("Passed")));
    TestEqual(TEXT("Commit"), Index->GetStringField(TEXT("Commit")), FString(TEXT("abc123")));
    TestEqual(TEXT("Rhi"), Index->GetStringField(TEXT("Rhi")), FString(TEXT("D3D12")));
    const TArray<TSharedPtr<FJsonValue>>& ViewValues = Index->GetArrayField(TEXT("Views"));
    if (TestEqual(TEXT("One entry per view"), ViewValues.Num(), 2))
    {
        const TSharedPtr<FJsonObject> First = ViewValues[0]->AsObject();
        TestEqual(TEXT("View"), First->GetStringField(TEXT("View")), FString(TEXT("broadcast_wide")));
        TestEqual(TEXT("File"), First->GetStringField(TEXT("File")), FString(TEXT("broadcast_wide.png")));
        TestTrue(TEXT("Written"), First->GetBoolField(TEXT("Written")));
        TestEqual(TEXT("ResolutionX"), static_cast<int32>(First->GetNumberField(TEXT("ResolutionX"))), 1920);
        TestEqual(TEXT("ResolutionY"), static_cast<int32>(First->GetNumberField(TEXT("ResolutionY"))), 1080);
        TestEqual(TEXT("GpuMs"), First->GetNumberField(TEXT("GpuMs")), 8.25, 0.001);
        TestEqual(TEXT("CameraLocation is x, y, z"), First->GetArrayField(TEXT("CameraLocation")).Num(), 3);
        TestEqual(TEXT("Restarts"), static_cast<int32>(ViewValues[1]->AsObject()->GetNumberField(TEXT("Restarts"))), 2);
    }
    TestEqual(TEXT("No failures"), Index->GetArrayField(TEXT("Failures")).Num(), 0);
    return true;
}

// ---------------------------------------------------------------------------
// 8. Picking views
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRenderSelectViewsTest,
    "PlaySports.Render.SelectViews",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRenderSelectViewsTest::RunTest(const FString& Parameters)
{
    using namespace PSRenderCaptureTests;

    FPSRenderCaptureSettings Made = MakeSettings();
    for (const TCHAR* Id : { TEXT("aerial"), TEXT("turf") })
    {
        FPSRenderView View = Made.RenderViews[0];
        View.ViewId = Id;
        Made.RenderViews.Add(View);
    }

    TestEqual(TEXT("No filter: every view"), UPSRenderCapture::SelectViews(Made, {}).Num(), 3);
    const TArray<FPSRenderView> Picked = UPSRenderCapture::SelectViews(Made, { FName(TEXT("turf")), FName(TEXT("wide")) });
    if (TestEqual(TEXT("A filter keeps the views it names"), Picked.Num(), 2))
    {
        TestEqual(TEXT("...in the file's order"), Picked[0].ViewId, FName(TEXT("wide")));
        TestEqual(TEXT("...both"), Picked[1].ViewId, FName(TEXT("turf")));
    }
    TestEqual(TEXT("A filter naming nothing known keeps none"), UPSRenderCapture::SelectViews(Made, { FName(TEXT("nope")) }).Num(), 0);
    TestEqual(TEXT("The PNG is named after its view"), UPSRenderCapture::GetShotFileName(Made.RenderViews[1]), FString(TEXT("aerial.png")));
    return true;
}

// ---------------------------------------------------------------------------
// 9. Lined up: the pre-snap phase only
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSRenderLinedUpPhaseTest,
    "PlaySports.Render.LinedUpPhase",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSRenderLinedUpPhaseTest::RunTest(const FString& Parameters)
{
    const UEnum* PhaseEnum = StaticEnum<EPlayPhase>();
    if (!TestNotNull(TEXT("EPlayPhase is reflected"), PhaseEnum))
    {
        return false;
    }
    // The names the game mode's PhaseChange events carry (PSGameStateEvents::MakePhaseChange).
    TestTrue(TEXT("PreSnap is lined up"), UPSRenderCapture::IsLinedUpPhase(PhaseEnum->GetNameStringByValue(static_cast<int64>(EPlayPhase::PreSnap))));
    TestFalse(TEXT("Snap is not"), UPSRenderCapture::IsLinedUpPhase(PhaseEnum->GetNameStringByValue(static_cast<int64>(EPlayPhase::Snap))));
    TestFalse(TEXT("BallCarrierMovement is not"), UPSRenderCapture::IsLinedUpPhase(PhaseEnum->GetNameStringByValue(static_cast<int64>(EPlayPhase::BallCarrierMovement))));
    TestFalse(TEXT("Scoring is not"), UPSRenderCapture::IsLinedUpPhase(PhaseEnum->GetNameStringByValue(static_cast<int64>(EPlayPhase::Scoring))));
    TestFalse(TEXT("An empty name is not"), UPSRenderCapture::IsLinedUpPhase(FString()));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
