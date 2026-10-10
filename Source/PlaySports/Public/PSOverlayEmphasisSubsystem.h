// PSOverlayEmphasisSubsystem.h - Epic 36: emphasizing players for callouts, mismatches and replay focus
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "PSOverlayEmphasisTypes.h"
#include "PSPlatformTiers.h"
#include "PSOverlayEmphasisSubsystem.generated.h"

/**
 * UPSOverlayEmphasisSubsystem is the one place players get visually emphasized (Epic 36):
 * commentary naming a key player (Track H), a replay's focus and isolation (Track B), a coaching
 * tip or a mismatch alert all ask here instead of touching a pawn's rendering themselves.
 *
 *  - Emphasize(Pawn, Kind, Source, Seconds) asks for one look on one player: a Highlight, a
 *    Mismatch or a Focus, for some seconds or until cleared. It returns a handle;
 *    ClearEmphasis(Handle), ClearSource(Source) and ClearAll take requests back.
 *  - Spotlight(Pawn, Source, Seconds) is a Focus that dims everyone else: the isolation replay.
 *  - Of several requests on one player the kind with the highest priority wins (the newest on a
 *    tie). At most MaxEmphasized players are drawn at once, highest first, since each costs
 *    custom-depth draws.
 *
 * Drawing is the emphasis post-process material's: this marks each player's meshes for the
 * custom-depth pass with the stencil value of his look (data), and the material outlines, glows
 * or dims by stencil (Specs/Player_Emphasis_Spec.md). On a tier whose OverlayDetail is Minimal
 * nothing is drawn; the requests are still kept, and show again on a richer tier.
 *
 * The rules are Data/player_emphasis.json. It ticks with its world to run requests out; headless
 * tests call AdvanceTime.
 */
UCLASS()
class PLAYSPORTS_API UPSOverlayEmphasisSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultStylePath();

    /** Replaces the style with JsonFilePath's, read through UPSDataIngestion. A style that fails
     *  ValidateStyle is refused and the current one kept. */
    bool LoadStyleFromJson(const FString& JsonFilePath);

    void SetStyle(const FPSEmphasisStyle& InStyle);

    const FPSEmphasisStyle& GetStyle() const { return Style; }

    /** Problems with a style, one line each (empty when sound). */
    static TArray<FString> ValidateStyle(const FPSEmphasisStyle& InStyle);

    /** Asks for Kind's look on Pawn for Seconds (0 or less: until cleared). Returns the
     *  request's handle, or 0 for no pawn. */
    UFUNCTION(BlueprintCallable, Category = "Overlay")
    int32 Emphasize(APSPlayerPawn* Pawn, EPSEmphasisKind Kind, FName Source, float Seconds = 0.f);

    /** Puts Pawn in the spotlight -- a Focus that dims everyone else -- for Seconds (0 or
     *  less: until cleared). Returns the request's handle, or 0 for no pawn. */
    UFUNCTION(BlueprintCallable, Category = "Overlay")
    int32 Spotlight(APSPlayerPawn* Pawn, FName Source, float Seconds = 0.f);

    /** Takes back one request. False when no such request is active. */
    UFUNCTION(BlueprintCallable, Category = "Overlay")
    bool ClearEmphasis(int32 Handle);

    /** Takes back every request from Source; returns how many. */
    UFUNCTION(BlueprintCallable, Category = "Overlay")
    int32 ClearSource(FName Source);

    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void ClearAll();

    /** How Pawn is drawn now. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    FPSPawnEmphasis GetEmphasis(const APSPlayerPawn* Pawn) const;

    /** True while a spotlight request is active. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    bool IsSpotlightActive() const;

    /** The active requests, oldest first. */
    const TArray<FPSEmphasisRequest>& GetRequests() const { return Requests; }

    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void SetOverlayDetail(EPSOverlayDetail InDetail);

    UFUNCTION(BlueprintPure, Category = "Overlay")
    EPSOverlayDetail GetOverlayDetail() const { return OverlayDetail; }

    /** Runs timed requests out. The tick calls this; headless tests call it directly. */
    void AdvanceTime(float DeltaSeconds);

    /** Works out every player's look and marks his meshes. Every change to the requests does
     *  this; call it after pawns are spawned for them to pick up a spotlight's dimming. */
    void Refresh();

private:
    int32 AddRequest(APSPlayerPawn* Pawn, EPSEmphasisKind Kind, FName Source, float Seconds, bool bSpotlight);
    int32 PriorityOf(EPSEmphasisKind Kind) const;
    int32 StencilOf(EPSEmphasisKind Kind) const;
    static EPSEmphasisLook LookOf(EPSEmphasisKind Kind);
    static void MarkMeshes(APSPlayerPawn& Pawn, int32 Stencil);

    FPSEmphasisStyle Style;
    EPSOverlayDetail OverlayDetail = EPSOverlayDetail::Full;

    UPROPERTY(Transient)
    TArray<FPSEmphasisRequest> Requests;

    /** Each player's look from the last Refresh. */
    TMap<FObjectKey, FPSPawnEmphasis> Looks;

    int32 NextHandle = 1;
};
