// PSDefenderGapOverlayTypes.h - Epic 81: the live run-gap integrity overlay's data
#pragma once

#include "CoreMinimal.h"
#include "PSDefenderGapSubsystem.h"
#include "PSOverlayEmphasisTypes.h"
#include "PSDefenderGapOverlayTypes.generated.h"

class APSPlayerPawn;

/** What a gap marker says about its gap. */
UENUM(BlueprintType)
enum class EPSGapMarkerState : uint8
{
    /** Its owner is in it. */
    Filled,
    /** Its owner is in it but engaged with a blocker, who may wash him out of it. */
    Blocked,
    /** Its owner is somewhere else. */
    Open,
    /** Nobody owns it: the call took its defender out of the fit. */
    Unowned
};

/** How the gap overlay looks (Data/gap_overlay.json; Architecture rule 4). Colors are "#RRGGBB". */
USTRUCT(BlueprintType)
struct FPSGapOverlayStyle
{
    GENERATED_BODY()

    /** Shown from the start; otherwise the console's ps.Overlay.GapIntegrity 1 or SetEnabled
     *  turns it on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    bool bEnabledByDefault = false;

    /** How often the markers follow the line as it moves (seconds). Their states change with
     *  each GapIntegrity event. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float RefreshSeconds = 0.1f;

    /** A marker sits this far above its gap's spot (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float MarkerHeight = 5.f;

    /** A marker's radius (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float MarkerRadius = 35.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString FilledColor = TEXT("#3FB950");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString BlockedColor = TEXT("#D29922");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString OpenColor = TEXT("#F85149");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString UnownedColor = TEXT("#A371F7");

    /** The owner of an open gap is emphasized (UPSOverlayEmphasisSubsystem) with this look. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    bool bEmphasizeOpenOwners = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    EPSEmphasisKind OpenOwnerEmphasis = EPSEmphasisKind::Mismatch;

    /** Draws the markers as debug shapes (development builds) until the editor-made marker
     *  exists (Specs/Gap_Integrity_Overlay_Spec.md). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    bool bDrawDebug = true;
};

/** One gap as the overlay shows it. */
USTRUCT(BlueprintType)
struct FPSGapMarker
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    EPSRunGap Gap = EPSRunGap::None;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    EPSGapMarkerState State = EPSGapMarkerState::Unowned;

    /** Where the marker sits: the gap's spot on the line, raised MarkerHeight. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FVector Location = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FLinearColor Color = FLinearColor::White;

    /** The defender who owns the gap; empty when nobody does. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FString OwnerName;

    TWeakObjectPtr<APSPlayerPawn> Owner;
};
