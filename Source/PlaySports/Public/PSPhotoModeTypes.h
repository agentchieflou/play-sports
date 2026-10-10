// PSPhotoModeTypes.h - Epic 45: photo mode's filters, looks, framing guides and tuning
#pragma once

#include "CoreMinimal.h"
#include "PSPhotoModeTypes.generated.h"

/** A framing aid drawn over the photo (by the photo mode's widget, which isn't built). */
UENUM(BlueprintType)
enum class EPSPhotoGuide : uint8
{
    None,
    /** Lines a third of the way in from each edge. */
    Thirds,
    /** A cross through the middle. */
    Center
};

/** One line of a framing guide, in normalized screen space ((0,0) top left, (1,1) bottom
 *  right). */
USTRUCT(BlueprintType)
struct FPSPhotoGuideLine
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    FVector2D Start = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    FVector2D End = FVector2D::ZeroVector;
};

/** One filter: a grade laid over the picture. Each field's default changes nothing, so a
 *  filter names only what it changes. */
USTRUCT(BlueprintType)
struct FPSPhotoFilter
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    FName FilterId;

    /** Colour saturation: 0 is black and white, 1 unchanged. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float Saturation = 1.f;

    /** 1 is unchanged. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float Contrast = 1.f;

    /** A colour the picture is multiplied by, "#RRGGBB"; white changes nothing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    FString Tint = TEXT("#FFFFFF");

    /** White balance (K): lower is cooler, higher warmer; 6500 changes nothing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float WhiteTemp = 6500.f;

    /** Darkened corners, 0 to 1; 0 leaves the camera's own. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float Vignette = 0.f;
};

/** A named filter stack, offered to the viewer as one choice. */
USTRUCT(BlueprintType)
struct FPSPhotoPreset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    FName PresetId;

    /** Filters by FilterId, applied in order; empty is the picture as the camera sees it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    TArray<FName> Filters;
};

/** A filter stack composed into one grade (UPSPhotoModeSubsystem::ComposeLook). A flag that is
 *  false leaves the camera's own setting. */
USTRUCT(BlueprintType)
struct FPSPhotoLook
{
    GENERATED_BODY()

    /** Saturation, contrast and tint are applied. */
    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    bool bColorGrade = false;

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    float Saturation = 1.f;

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    float Contrast = 1.f;

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    FLinearColor Tint = FLinearColor::White;

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    bool bWhiteTemp = false;

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    float WhiteTemp = 6500.f;

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    bool bVignette = false;

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    float Vignette = 0.f;
};

/** A photo asked for: where it goes and how big it is. */
USTRUCT(BlueprintType)
struct FPSPhotoCapture
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    FString FilePath;

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    int32 Width = 0;

    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    int32 Height = 0;

    /** The engine took the request: false with no game viewport (headless runs). */
    UPROPERTY(BlueprintReadOnly, Category = "PhotoMode")
    bool bRequested = false;
};

/** How photo mode's camera moves, what it offers and how big a photo is (Data/photo_mode.json;
 *  Architecture rule 4). The numbers' defaults equal the file's; the lists come from it. */
USTRUCT(BlueprintType)
struct FPSPhotoModeTuning
{
    GENERATED_BODY()

    /** How fast the camera flies on the stick (cm/s at full stick). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float MoveCmPerSecond = 800.f;

    /** How fast it rises or sinks (cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float RiseCmPerSecond = 400.f;

    /** How fast it turns while a turn button is held (degrees/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float TurnDegreesPerSecond = 60.f;

    /** How far one press (or one swipe) turns it at once (degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float TurnStepDegrees = 5.f;

    /** How far it looks up or down (degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float MaxPitchDegrees = 85.f;

    /** How far it may fly from where photo mode began (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float MaxDistanceCm = 3000.f;

    /** The lowest it goes (world height, cm): above the turf. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float MinHeightCm = 30.f;

    /** The field of view's range (degrees) and how fast zooming changes it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float MinFieldOfView = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float MaxFieldOfView = 110.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float ZoomDegreesPerSecond = 30.f;

    /** How far the camera tilts sideways either way (degrees) and how fast. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float MaxRollDegrees = 45.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float RollDegreesPerSecond = 30.f;

    /** Depth of field: the focus distance's range (cm), how fast it moves (doublings per
     *  second), and where it starts when the camera follows nobody. It starts on the followed
     *  player or ball otherwise. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float MinFocusCm = 50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float MaxFocusCm = 20000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float FocusDoublingsPerSecond = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float DefaultFocusCm = 2000.f;

    /** The apertures the aperture button steps through (f-stops, ascending); 0 first is depth
     *  of field off. The first is where photo mode starts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    TArray<float> Apertures;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    TArray<FPSPhotoFilter> Filters;

    /** The presets the filter button steps through; the first is where photo mode starts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    TArray<FPSPhotoPreset> Presets;

    /** A photo is the viewport's size times this, at most MaxCaptureDimension on its longer
     *  side (and never smaller than the viewport). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    float CaptureResolutionMultiplier = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhotoMode")
    int32 MaxCaptureDimension = 7680;
};
