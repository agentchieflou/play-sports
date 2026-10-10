// PSInputGlyphs.h - Epic 128: which button picture to show for an action on the active device
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "InputCoreTypes.h"
#include "PSInputConfigTypes.h"
#include "PSTelemetryBus.h"
#include "PSInputGlyphs.generated.h"

/** The glyph for one physical key in a glyph set. */
USTRUCT(BlueprintType)
struct FPSKeyGlyphDef
{
    GENERATED_BODY()

    /** Engine key name (EKeys), e.g. "Gamepad_FaceButton_Bottom". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FName Key;

    /** Icon ID an imported texture is registered under, e.g. "Xbox_A". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FName GlyphId;

    /** Short text drawn in place of the icon until one is imported, e.g. "A", "LB", "Esc". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FString Label;
};

/** A glyph for a whole action, used instead of its first key's: Move on keyboard shows
 *  "WASD" rather than "W". */
USTRUCT(BlueprintType)
struct FPSActionGlyphDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FName ActionId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FName GlyphId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FString Label;
};

/** One family of button pictures (Xbox, keyboard and mouse, touch; later PlayStation). */
USTRUCT(BlueprintType)
struct FPSInputGlyphSetDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FName GlyphSetId;

    /** The device whose keys this set draws. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    EPSInputDevice Device = EPSInputDevice::KeyboardMouse;

    /** The set shown for Device unless something picks another (one per device). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    bool bDefaultForDevice = false;

    /** Keys missing from Keys still get a keycap labelled with the key's own name (keyboards
     *  have too many keys to list; a remapped key still shows). Gamepad sets list every key. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    bool bFallbackToKeyName = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    TArray<FPSKeyGlyphDef> Keys;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    TArray<FPSActionGlyphDef> Actions;
};

/** Top-level shape of Data/input_glyphs.json. */
USTRUCT(BlueprintType)
struct FPSInputGlyphCatalog
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    TArray<FPSInputGlyphSetDef> GlyphSets;
};

/** What a HUD, prompt or remap screen draws for a key or action. */
USTRUCT(BlueprintType)
struct FPSInputGlyph
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Input")
    FName GlyphSetId;

    UPROPERTY(BlueprintReadOnly, Category = "Input")
    FName GlyphId;

    UPROPERTY(BlueprintReadOnly, Category = "Input")
    FString Label;

    /** The key the glyph stands for; none for an action-wide glyph. */
    UPROPERTY(BlueprintReadOnly, Category = "Input")
    FKey Key;
};

/**
 * UPSInputGlyphs answers "which button picture goes next to this action?" for the device the
 * player is using. The table (Data/input_glyphs.json, read through UPSDataIngestion) maps
 * physical keys to glyphs per glyph set; which key an action uses comes from the input
 * catalog, so the catalog stays the one authority on bindings (rule 6) and a remapped action
 * (Epic 103) shows its new button without touching this table.
 *
 * The active device arrives on UPSTelemetryBus (InputDeviceChange); consumers pass it in.
 */
UCLASS(BlueprintType)
class PLAYSPORTS_API UPSInputGlyphs : public UObject
{
    GENERATED_BODY()

public:
    /** Absolute path of the authored table: <ProjectDir>/Data/input_glyphs.json. */
    static FString GetDefaultGlyphsPath();

    UFUNCTION(BlueprintCallable, Category = "Input")
    bool LoadFromJson(const FString& JsonFilePath);

    UFUNCTION(BlueprintCallable, Category = "Input")
    bool LoadDefaults() { return LoadFromJson(GetDefaultGlyphsPath()); }

    /** Problems with the table, one line each: duplicate or empty IDs, a device without
     *  exactly one default set, a key that is not an engine key or belongs to the other
     *  device, an empty glyph or label. With InCatalog, also an action glyph for an unknown
     *  action and a bound key that a default set without fallback cannot draw. */
    TArray<FString> Validate(const FPSInputCatalog* InCatalog = nullptr) const;

    /** The default glyph set for Device, or null when the table has none. */
    const FPSInputGlyphSetDef* GetDefaultSet(EPSInputDevice Device) const;

    /** The glyph for Key in Device's default set. False when the set has no entry and no
     *  fallback, or Key belongs to the other device. */
    bool GetGlyphForKey(const FKey& Key, EPSInputDevice Device, FPSInputGlyph& OutGlyph) const;

    /** The glyph for ActionId in ContextId on Device: the set's action glyph if it has one,
     *  otherwise the glyph of the first key of that device the catalog binds to the action
     *  there. False when the action has no binding for the device in that context. Touch
     *  binds no keys (its controls name actions, Epic 130), so on Touch it is the action
     *  glyph, for any action that lives in ContextId. */
    bool GetGlyphForAction(const FPSInputCatalog& InCatalog, FName ActionId, FName ContextId, EPSInputDevice Device, FPSInputGlyph& OutGlyph) const;

    /** The device a key belongs to: gamepad keys, touch keys, or keyboard and mouse. */
    static EPSInputDevice GetDeviceForKey(const FKey& Key);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    FPSInputGlyphCatalog Table;
};
