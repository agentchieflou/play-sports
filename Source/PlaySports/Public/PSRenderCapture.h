// PSRenderCapture.h - Lane V1: -PSRenderCapture renders the game scene from fixed views to PNGs, then exits
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UObject/Object.h"
#include "PSRenderCaptureTypes.h"
#include "PSRenderCapture.generated.h"

class ACameraActor;
class UWorld;
struct FPSTelemetryPhaseChangeEvent;

/** What -PSRenderCapture's switches ask for. */
struct FPSRenderCaptureOptions
{
    /** Where the PNGs and index.json go (-PSRenderCaptureOut=; Saved/Renders without it). */
    FString OutputDir;

    /** The views file (-PSRenderCaptureViews=; Data/render_views.json without it). */
    FString ViewsPath;

    /** Only these views, when given (-PSRenderCaptureOnly=a+b); every view otherwise. */
    TArray<FName> OnlyViews;

    /** The commit index.json names (-PSRenderCaptureCommit=). */
    FString Commit;
};

/**
 * UPSRenderCapture renders the real game scene with the GPU from fixed cameras and writes a PNG
 * per view, so visual work is judged on images the engine drew (lane V1; .github/workflows/
 * render.yml runs it on the CI runner). With -PSRenderCapture on a game's command line (the
 * packaged game, or the editor binary with -game), once the engine is up it:
 *   1. waits for the level and the match: the game world has begun play and ExpectedPlayers
 *      players are on the field (Play Now: both elevens lined up, the ball spotted);
 *   2. waits for shaders and assets still compiling (the editor binary compiles them on demand);
 *   3. hides the HUD and on-screen messages and renders WarmupFrames from the first view, so
 *      exposure, Lumen, virtual shadow maps and streaming settle;
 *   4. for each view in Data/render_views.json: puts its own camera there (ComputeCameraPose),
 *      renders SettleFrames, averages the GPU time of the last MeasureFrames, and saves the
 *      viewport as <ViewId>.png. The match plays on meanwhile; with bCaptureOnlyPreSnap a view
 *      is taken only while the play is pre-snap (the bus's PhaseChange events say when), and a
 *      snap during a view starts it again at the next pre-snap;
 *   5. writes index.json (BuildIndexJson) and exits 0 when every view has its PNG, 1 otherwise.
 * The editor and commandlets ignore the switch. The game keeps running while it captures; the
 * capture never touches the game mode.
 */
UCLASS()
class PLAYSPORTS_API UPSRenderCapture : public UObject
{
    GENERATED_BODY()

public:
    /** Data/render_views.json under the project directory. */
    static FString GetDefaultViewsPath();

    /** Saved/Renders under the project directory. */
    static FString GetDefaultOutputDir();

    /** Reads CommandLine: true when -PSRenderCapture is on it, with OutOptions from its other
     *  switches (defaults for those not given). */
    static bool ParseCommandLine(const TCHAR* CommandLine, FPSRenderCaptureOptions& OutOptions);

    /** Problems with Candidate, one line each (empty when sound): resolution, frame counts and
     *  limits, and each view's unique ID, distinct camera and target and field of view. */
    static TArray<FString> ValidateSettings(const FPSRenderCaptureSettings& Candidate);

    /** Settings from JsonFilePath through UPSDataIngestion, into OutSettings; false (with
     *  OutProblems) when it can't be read or isn't sound. */
    static bool LoadSettings(const FString& JsonFilePath, FPSRenderCaptureSettings& OutSettings, TArray<FString>& OutProblems);

    /** The views to render: every one, or those named in Only, in the file's order. */
    static TArray<FPSRenderView> SelectViews(const FPSRenderCaptureSettings& InSettings, const TArray<FName>& Only);

    /** Where View's camera goes and which way it looks, in world space. AnchorGround is the
     *  ground point under a Player view's player (ignored for a Field view, whose frame is
     *  PSField's). False when the camera and target coincide. */
    static bool ComputeCameraPose(const FPSRenderView& View, const FVector& AnchorGround, FVector& OutLocation, FRotator& OutRotation);

    /** The index in Players of Role's Index-th player counted from the -Y sideline (lowest Y
     *  first, then lowest X), or INDEX_NONE when there are not that many. */
    static int32 PickAnchorPlayer(const TArray<TPair<EPlayerRole, FVector>>& Players, EPlayerRole Role, int32 Index);

    /** "<ViewId>.png". */
    static FString GetShotFileName(const FPSRenderView& View);

    /** Whether a PhaseChange's NewPhase is the one with both elevens lined up: pre-snap. */
    static bool IsLinedUpPhase(const FString& PhaseName);

    /** Sets InOutReport.bPassed: no failures, ExpectedShots shots, each written. */
    static void FinalizeReport(FPSRenderCaptureReport& InOutReport, int32 ExpectedShots);

    /** The process exit code for InReport: 0 on a pass, 1 otherwise. */
    static int32 GetExitCode(const FPSRenderCaptureReport& InReport);

    /** InReport as index.json: Passed, Commit, Map, Rhi, Adapter, PlayersOnField, the wait times,
     *  Views (one object per shot: View, Description, File, Written, ResolutionX/Y, GpuMs,
     *  GpuMsMax, FrameMs, CameraLocation, CameraRotation, Error) and Failures. */
    static FString BuildIndexJson(const FPSRenderCaptureReport& InReport);

    /** When -PSRenderCapture is on the command line, starts the capture once the engine has
     *  started (FCoreDelegates::OnPostEngineInit). PlaySports' module calls it at startup. */
    static void RegisterCommandLineHook();

private:
    enum class EPhase : uint8
    {
        WaitForMatch,
        WaitForCompilation,
        Warmup,
        SettleView,
        WaitForShot,
        Done
    };

    static void StartFromCommandLine();

    /** Starts ticking: the run the switch asks for, with Options and InSettings. */
    void Begin(const FPSRenderCaptureOptions& InOptions, const FPSRenderCaptureSettings& InSettings);

    /** One engine frame of the capture; false once it has finished. */
    bool TickCapture(float DeltaSeconds);

    UWorld* FindGameWorld() const;
    int32 CountPlayersOnField(UWorld* World) const;
    bool IsMatchReady(UWorld* World) const;
    void BindToBus(UWorld* World);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);
    void PrepareScene(UWorld* World);
    /** Puts the camera at View for World's players; false with OutError when it can't be. */
    bool PlaceCamera(UWorld* World, const FPSRenderView& View, FVector& OutLocation, FRotator& OutRotation, FString& OutError);
    void BeginView(UWorld* World);
    void RestartView(UWorld* World);
    void HoldViewTarget(UWorld* World);
    void RequestShot(UWorld* World);
    void FinishShot(bool bWritten, const FString& Error);
    void Finish();

    FPSRenderCaptureOptions Options;
    FPSRenderCaptureSettings Settings;
    TArray<FPSRenderView> Views;
    FPSRenderCaptureReport Report;

    UPROPERTY(Transient)
    TObjectPtr<ACameraActor> Camera;

    FTSTicker::FDelegateHandle TickerHandle;
    EPhase Phase = EPhase::WaitForMatch;
    double StartSeconds = 0.0;
    double PhaseStartSeconds = 0.0;
    double LastLogSeconds = 0.0;
    int32 PhaseFrames = 0;
    int32 ViewIndex = 0;
    bool bSceneReady = false;
    /** Whether the play is pre-snap, from the bus; true at kickoff. */
    bool bLinedUp = true;
    /** PhaseChange events seen, and how many there had been when the current view began. */
    int32 PhaseChanges = 0;
    int32 ViewPhaseChanges = 0;
    bool bBoundToBus = false;
    FPSRenderShot CurrentShot;
    TArray<float> GpuSamples;
    TArray<float> FrameSamples;
};
