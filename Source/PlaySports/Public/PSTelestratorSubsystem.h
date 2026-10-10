// PSTelestratorSubsystem.h - Epic 44: drawing on a paused replay or the film view
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSTelestratorTypes.h"
#include "PSTelestratorSubsystem.generated.h"

class APSBroadcastCamera;
class APSPlayerController;
class APSPlayerPawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSAnnotationRequestSignature, const FPSAnnotationRequest&, Request);
DECLARE_MULTICAST_DELEGATE_OneParam(FPSAnnotationRequestMC, const FPSAnnotationRequest&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSAnnotationReceivedSignature, int32, RequestId);

/**
 * UPSTelestratorSubsystem is the telestrator and analysis mode (Epic 44): draw on a paused
 * replay (Epic 41) or the all-22 film view (Epic 40) with circles, arrows and freehand lines,
 * highlight players, save the annotated still, and ask an outside annotator for coaching notes.
 *
 *  - BeginAnalysis holds the replay if it is playing and takes the frame from the broadcast
 *    camera's film-frame hook (UPSCameraAll22Component::CaptureFrame): the exact shot and every
 *    player's place in it. It needs a replay, or the film view, to draw on.
 *  - Strokes (BeginStroke, ExtendStroke, EndStroke, in the frame's normalized screen space) make
 *    marks with the current tool. Each point is also pinned to the field through the frame's
 *    shot (UPSCameraFraming::DeprojectToField). A Player tap picks the player nearest it and
 *    lights him up through UPSOverlayEmphasisSubsystem (Epic 36). Undo and ClearMarks take marks
 *    back, emphasis and all.
 *  - ExportStill writes the frame and its marks as JSON (Saved/Telestrator by default) and asks
 *    for a screenshot with the drawing layer when a game viewport exists.
 *  - Auto-annotation, gated on the Epic 25 bridge: RequestAutoAnnotation hands the frame, the
 *    play's events and its situation to whoever listens (OnAutoAnnotationRequested), or keeps it
 *    for an agent that polls GetPendingAnnotationRequests through AgenticLink's call_function.
 *    The answer comes back through SubmitAutoAnnotation: notes, and marks in field terms. With no
 *    bridge online and nobody listening, there is nothing to ask and the request is refused.
 *  - The way in: the Telestrator catalog action of whoever looks through the broadcast camera
 *    (bound as it becomes their view, like photo mode) toggles analysis.
 *
 * UPSTelestratorWidget (the drawing layer, made by APSHUD) paints the marks
 * (PSTelestratorLayer), turns the mouse, a finger or its gamepad cursor into strokes, and runs
 * the Telestrator context's buttons through RunLayerAction; this keeps the marks and the math.
 */
UCLASS()
class PLAYSPORTS_API UPSTelestratorSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    UPSTelestratorSubsystem();

    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void Deinitialize() override;

    static FString GetDefaultTuningPath();

    /** The tuning, loaded from the default path on first use. */
    const FPSTelestratorTuning& GetTuning();

    /** Replaces the tuning with JsonFilePath's (through UPSDataIngestion); a file that can't be
     *  read or fails ValidateTuning is refused and the current tuning kept. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    bool SetTuning(const FPSTelestratorTuning& NewTuning);

    /** Problems with InTuning, one line each (empty when sound): a negative spacing, a pick
     *  radius not above 0, stroke or mark limits below 2 or 1. */
    static TArray<FString> ValidateTuning(const FPSTelestratorTuning& InTuning);

    // --- Analysis mode -------------------------------------------------------------------

    /** Starts drawing on what the broadcast camera shows: a replay (held if it was playing) or
     *  the film view. False when there is neither, or no broadcast camera. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    bool BeginAnalysis();

    /** Stops drawing: the marks go, and the players they lit up go dark. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    void EndAnalysis();

    UFUNCTION(BlueprintPure, Category = "Telestrator")
    bool IsAnalysisActive() const { return bAnalysisActive; }

    /** The frame being drawn on, with its marks and notes. */
    UFUNCTION(BlueprintPure, Category = "Telestrator")
    FPSTelestration GetTelestration() const { return Telestration; }

    // --- Drawing -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    void SetTool(EPSTelestratorTool InTool) { Tool = InTool; }

    UFUNCTION(BlueprintPure, Category = "Telestrator")
    EPSTelestratorTool GetTool() const { return Tool; }

    /** A stroke with the current tool starts at Screen (normalized). */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    void BeginStroke(FVector2D Screen);

    /** The stroke goes on through Screen. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    void ExtendStroke(FVector2D Screen);

    /** The stroke ends at Screen and becomes a mark. Returns its index, or INDEX_NONE when it
     *  made none (no stroke, no analysis, a tap on nobody, too many marks). */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    int32 EndStroke(FVector2D Screen);

    /** Takes the last mark back. False when there is none. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    bool Undo();

    /** Takes every mark back. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    void ClearMarks();

    /** The player standing nearest Screen in Frame, within Radius; None when nobody is. */
    static FName PickPlayer(const FPSFilmFrame& Frame, const FVector2D& Screen, float Radius);

    /** The tool after the current one (freehand, arrow, circle, player, and round again), now
     *  the current one. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    EPSTelestratorTool CycleTool();

    /** The stroke being drawn (normalized screen points), for the layer to show as it goes;
     *  empty between strokes. */
    UFUNCTION(BlueprintPure, Category = "Telestrator")
    TArray<FVector2D> GetLiveStroke() const { return bStroking ? StrokePoints : TArray<FVector2D>(); }

    /** The frame and its marks without a copy, for the layer to paint every frame. */
    const FPSTelestration& GetTelestrationRef() const { return Telestration; }

    // --- The way in, and the drawing layer's buttons ----------------------------------------

    /** Analysis on (BeginAnalysis), or off when it is on. True when it is on after. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    bool ToggleAnalysis();

    /** One of the drawing layer's actions (the Telestrator context), while analysis is on: the
     *  next tool, undo, clear, save the still, or leave (TelestratorExit, or Telestrator again).
     *  False for any other action, or outside analysis. Drawing and the cursor are the layer's. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    bool RunLayerAction(FName ActionId);

    /** Listens to Controller's catalog actions, so its Telestrator button toggles analysis.
     *  APSBroadcastCamera binds whoever looks through it. Idempotent. */
    void BindController(APSPlayerController* Controller);

    void UnbindController(APSPlayerController* Controller);

    /** Catalog action handler (APSPlayerController::OnCatalogActionStarted). */
    UFUNCTION()
    void HandleActionStarted(FName ActionId);

    /** The catalog action that toggles analysis, in the field's and the replay's contexts and the
     *  layer's own. */
    UPROPERTY(EditDefaultsOnly, Category = "Telestrator")
    FName AnalysisActionId;

    /** The input catalog context the drawing layer reads its keys from (never pushed on
     *  Enhanced Input; the layer takes them through Slate), and its actions. */
    UPROPERTY(EditDefaultsOnly, Category = "Telestrator")
    FName LayerContextId;

    UPROPERTY(EditDefaultsOnly, Category = "Telestrator")
    FName DrawActionId;

    UPROPERTY(EditDefaultsOnly, Category = "Telestrator")
    FName CursorActionId;

    UPROPERTY(EditDefaultsOnly, Category = "Telestrator")
    FName ToolActionId;

    UPROPERTY(EditDefaultsOnly, Category = "Telestrator")
    FName UndoActionId;

    UPROPERTY(EditDefaultsOnly, Category = "Telestrator")
    FName ClearActionId;

    UPROPERTY(EditDefaultsOnly, Category = "Telestrator")
    FName SaveActionId;

    UPROPERTY(EditDefaultsOnly, Category = "Telestrator")
    FName ExitActionId;

    // --- Stills --------------------------------------------------------------------------

    /** Saved/Telestrator. */
    static FString GetDefaultExportDirectory();

    /** Writes the frame and its marks as JSON to Directory (the default when empty), and, when
     *  a game viewport exists, asks for a screenshot with the drawing layer beside it. False
     *  outside analysis or when the file can't be written. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    bool ExportStill(const FString& Directory, FString& OutFilePath);

    /** Reads an exported still. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    static bool LoadStill(const FString& FilePath, FPSTelestration& OutTelestration);

    // --- Auto-annotation (bridge-gated, Epic 25) -------------------------------------------

    /** The bridge says it is (or isn't) there to answer requests, for an agent that polls. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    void SetAutoAnnotationBridgeOnline(bool bOnline) { bBridgeOnline = bOnline; }

    /** True when something can answer: the bridge is online, or a listener is bound. */
    UFUNCTION(BlueprintPure, Category = "Telestrator")
    bool IsAutoAnnotationAvailable() const;

    /** Asks for coaching notes on the frame being drawn on. Returns the request's id, or
     *  INDEX_NONE outside analysis or with nothing to answer. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    int32 RequestAutoAnnotation();

    /** The requests not yet answered, for an agent that polls through the bridge. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    TArray<FPSAnnotationRequest> GetPendingAnnotationRequests() const { return PendingRequests; }

    /** The answer to RequestId: Notes are kept with the still, each suggestion becomes a mark
     *  (bAuto), placed on the screen through the frame's shot. False for a request that isn't
     *  pending, or one whose frame is no longer the one being drawn on. */
    UFUNCTION(BlueprintCallable, Category = "Telestrator")
    bool SubmitAutoAnnotation(int32 RequestId, const FString& Notes, const TArray<FPSAnnotationSuggestion>& Suggestions);

    /** Fires with every request, for a bridge or Blueprint that answers it. */
    UPROPERTY(BlueprintAssignable, Category = "Telestrator")
    FPSAnnotationRequestSignature OnAutoAnnotationRequested;

    FPSAnnotationRequestMC OnAutoAnnotationRequestedMC;

    /** Fires when an answer has been drawn. */
    UPROPERTY(BlueprintAssignable, Category = "Telestrator")
    FPSAnnotationReceivedSignature OnAutoAnnotationReceived;

    /** The source the telestrator's emphasis requests go under (Epic 36). */
    UPROPERTY(EditDefaultsOnly, Category = "Telestrator")
    FName EmphasisSource;

private:
    APSBroadcastCamera* FindBroadcastCamera() const;
    APSPlayerPawn* FindPawn(FName PlayerId) const;

    /** Pins Mark's screen points to the field through the frame's shot. */
    void PinToField(FPSTelestratorMark& Mark) const;

    /** Adds Mark (lighting up its player), unless the frame is full. Its index, or INDEX_NONE. */
    int32 AddMark(FPSTelestratorMark Mark);

    /** Takes the mark at Index back, emphasis and all. */
    void RemoveMark(int32 Index);

    UPROPERTY(Transient)
    FPSTelestratorTuning Tuning;

    bool bTuningLoaded = false;
    bool bAnalysisActive = false;
    EPSTelestratorTool Tool = EPSTelestratorTool::Freehand;

    UPROPERTY(Transient)
    FPSTelestration Telestration;

    /** The stroke being drawn. */
    bool bStroking = false;
    TArray<FVector2D> StrokePoints;

    /** What the replay was doing when analysis began: it plays on after, if it was playing. */
    bool bResumeReplay = false;

    bool bBridgeOnline = false;
    int32 NextRequestId = 1;

    UPROPERTY(Transient)
    TArray<FPSAnnotationRequest> PendingRequests;

    /** The controllers whose Telestrator button this listens to. */
    TArray<TWeakObjectPtr<APSPlayerController>> BoundControllers;
};
