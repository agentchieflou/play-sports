// PSPlayerDNA.h - Epic 79: per-athlete style profiles that change what the AI decides
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/Function.h"
#include "PSPlayerAttributes.h"
#include "PSRushMoveComponent.h"
#include "PSPlayerDNA.generated.h"

/** How an axis is generated from a player's ratings (Epic 79.3): by tools/player_dna.py for the
 *  hand-written rosters and by PSPlayerDNA::GenerateProfile for generated leagues (Epic 122). The
 *  game reads a roster's DNA as written; only the generators use these. */
USTRUCT(BlueprintType)
struct FPSDNAGenerator
{
    GENERATED_BODY()

    /** The rating (Speed, Agility, Strength, Acceleration, Awareness or Stamina) that leans the
     *  axis toward its high end ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FName HighAttribute;

    /** ... and the one that leans it toward its low end. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FName LowAttribute;

    /** Axis points per rating point of (HighAttribute - LowAttribute), measured from the
     *  league's average for the role. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float RatingLean = 0.f;

    /** The spread (standard deviation) of each player's own seeded variation, so two players
     *  rated alike still play differently. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float Spread = 0.f;
};

/** One style axis (Data/player_dna.json): the roles it applies to and the trait a scout sees at
 *  each end. Axis names an FPSPlayerDNA field. */
USTRUCT(BlueprintType)
struct FPSDNAAxisDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FName Axis;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    TArray<EPlayerRole> Roles;

    /** The trait at -1, e.g. PocketPasser, with its player-facing name and description
     *  (string table rows Trait.<TraitId>.Label and .Description, tools/ui_text.py). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FName LowTrait;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FString LowLabel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FString LowDescription;

    /** The trait at +1, e.g. Scrambler. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FName HighTrait;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FString HighLabel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FString HighDescription;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FPSDNAGenerator Generator;
};

/** One binding (Epic 79.2): an axis scaling one float field of an AI tuning. The field is
 *  multiplied by 1 at a neutral axis, by AtHigh at +1 and by AtLow at -1, linearly between. */
USTRUCT(BlueprintType)
struct FPSDNABinding
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FName Axis;

    /** The tuning it scales: SkillAI (FSkillPlayerAITuningRow), Pocket (FPocketTuningRow),
     *  DefenderAI (FDefenderAITuningRow) or RouteRunning (FRouteRunningTuningRow). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FName Target;

    /** A float field of that tuning. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    FName Field;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float AtLow = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float AtHigh = 1.f;
};

/** How much a pass-rush move is a power move (+1) or a finesse move (-1). */
USTRUCT(BlueprintType)
struct FPSDNARushMoveLean
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    EPSRushMove Move = EPSRushMove::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float Lean = 0.f;
};

/** The style axes and what they bind (Data/player_dna.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSPlayerDNACatalog
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    TArray<FPSDNAAxisDef> Axes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    TArray<FPSDNABinding> Bindings;

    /** The rush plan's power and finesse moves (UPSRushMoveComponent::ChooseMove). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    TArray<FPSDNARushMoveLean> RushMoveLeans;

    /** A full power rusher scores a full power move (1 + this) times as high, and a full
     *  finesse move (1 - this) times; 0 to below 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float RushStyleWeight = 0.5f;

    /** A scout sees an axis's trait once the player is this far from neutral (0 to 1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DNA")
    float TraitThreshold = 0.4f;
};

/** A style trait a scout sees (Epic 79.4; Track G's scouting screens consume these). */
USTRUCT(BlueprintType)
struct FPSScoutingTrait
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "DNA")
    FName Axis;

    UPROPERTY(BlueprintReadOnly, Category = "DNA")
    FName TraitId;

    /** How pronounced it is: the axis's distance from neutral, TraitThreshold to 1. */
    UPROPERTY(BlueprintReadOnly, Category = "DNA")
    float Strength = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "DNA")
    FText Label;

    UPROPERTY(BlueprintReadOnly, Category = "DNA")
    FText Description;
};

/** The pure rules of player DNA. */
namespace PSPlayerDNA
{
    /** DNA's value on Axis, clamped to -1..1; 0 for a name FPSPlayerDNA doesn't have. */
    PLAYSPORTS_API float GetAxis(const FPSPlayerDNA& DNA, FName Axis);

    /** True when FPSPlayerDNA has an axis called Axis. */
    PLAYSPORTS_API bool IsAxis(FName Axis);

    /** Catalog's definition of Axis, or null. */
    PLAYSPORTS_API const FPSDNAAxisDef* FindAxis(const FPSPlayerDNACatalog& Catalog, FName Axis);

    /** The value Player plays Axis with: his DNA's, or 0 when the catalog doesn't list the axis
     *  or it doesn't apply to his role. */
    PLAYSPORTS_API float GetEffectiveAxis(const FPSPlayerDNACatalog& Catalog, const FPlayerAttributes& Player, FName Axis);

    /** What Binding multiplies its field by at AxisValue: 1 at 0, AtHigh at +1, AtLow at -1. */
    PLAYSPORTS_API float BindingScale(const FPSDNABinding& Binding, float AxisValue);

    /** The tuning struct a binding Target names, or null for an unknown one. */
    PLAYSPORTS_API const UScriptStruct* FindTargetStruct(FName Target);

    /** Scales Data (an instance of Struct, the tuning Target names) by every binding on Target
     *  for Player's DNA. Returns how many bindings changed a field. */
    PLAYSPORTS_API int32 ApplyBindings(const FPSPlayerDNACatalog& Catalog, const FPlayerAttributes& Player, FName Target, const UScriptStruct* Struct, void* Data);

    /** The rush plan's multiplier for Move: 1 + RushStyleWeight * the rusher's RushPower * the
     *  move's lean (1 for a move with no lean). */
    PLAYSPORTS_API float RushMoveScale(const FPSPlayerDNACatalog& Catalog, const FPlayerAttributes& Rusher, EPSRushMove Move);

    /** The traits a scout sees in Player, most pronounced first: one per axis of his role at
     *  least TraitThreshold from neutral, named through the string tables. */
    PLAYSPORTS_API TArray<FPSScoutingTrait> GetScoutingTraits(const FPSPlayerDNACatalog& Catalog, const FPlayerAttributes& Player);

    /** The string table key of a trait's text: Trait.<TraitId>.<Field> (Label or Description). */
    PLAYSPORTS_API FString TraitKey(FName TraitId, const FString& Field);

    /** The rating gap Def's generator leans on: Player's HighAttribute less his LowAttribute (a
     *  name that isn't a float field of FPlayerAttributes reads 0). */
    PLAYSPORTS_API float GetGeneratorGap(const FPSDNAAxisDef& Def, const FPlayerAttributes& Player);

    /** The average generator gap, per axis of Role, of the players of Role among Players: what
     *  GenerateProfile treats as neutral for the role. Empty when Players has none of Role. */
    PLAYSPORTS_API TMap<FName, float> GetGeneratorCenters(const FPSPlayerDNACatalog& Catalog, const TArray<FPlayerAttributes>& Players, EPlayerRole Role);

    /** A generated style for Player (Epic 79.3's rule, the one tools/player_dna.py's
     *  generate_profile follows): each axis of his role is RatingLean * (his gap - the role's
     *  center in Centers, 0 when missing) plus Spread * NextStandardNormal(), clamped to -1..1 and
     *  rounded to two places. Axes of other roles stay 0. The caller owns the randomness, so a
     *  seeded generator (UPSLeagueGenerator) gets the same DNA from the same seed. */
    PLAYSPORTS_API FPSPlayerDNA GenerateProfile(const FPSPlayerDNACatalog& Catalog, const FPlayerAttributes& Player, const TMap<FName, float>& Centers, TFunctionRef<float()> NextStandardNormal);

    /** Problems with Catalog, one line each (empty when sound): an axis FPSPlayerDNA doesn't
     *  have, listed twice or with no role; a trait without an ID or a name, or an ID used twice; a
     *  binding on an unlisted axis, an unknown target or a field that isn't one of its floats, or
     *  a multiplier not above 0; a rush move listed twice or with a lean outside -1..1; a weight
     *  or threshold out of range. */
    PLAYSPORTS_API TArray<FString> ValidateCatalog(const FPSPlayerDNACatalog& Catalog);
}

/**
 * UPSPlayerDNASubsystem holds the style catalog (Data/player_dna.json, Epic 79) for the AI. Each
 * AI component puts its player's DNA into its own tuning as a play starts: the tuning as loaded,
 * scaled by the catalog's bindings for that player (UPSSkillPlayerAIComponent and the pocket for a
 * quarterback or a back, UPSRouteRunnerComponent for a receiver, UPSDefenderAIComponent for a
 * defender), and the rush plan weights power or finesse moves by RushPower. So two players with
 * the same ratings make different decisions. A player with neutral DNA plays the tuning as
 * loaded, and the catalog isn't read for him.
 *
 * It also says what a scout sees (GetScoutingTraits).
 */
UCLASS()
class PLAYSPORTS_API UPSPlayerDNASubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    static FString GetDefaultCatalogPath();

    /** World's DNA subsystem, or null. */
    static UPSPlayerDNASubsystem* Get(const UWorld* World);

    /** The catalog in use, loaded from the default path on first use. */
    const FPSPlayerDNACatalog& GetCatalog();

    bool LoadCatalogFromJson(const FString& JsonFilePath);

    /** Replaces the catalog (tests, or a mode with its own). */
    void SetCatalog(const FPSPlayerDNACatalog& InCatalog);

    /** Scales Row (the tuning Target names) by Player's DNA; a neutral player is left alone.
     *  Returns how many bindings changed a field. */
    template <typename TRow>
    int32 ApplyTo(const FPlayerAttributes& Player, FName Target, TRow& Row)
    {
        return Player.DNA.IsNeutral() ? 0 : PSPlayerDNA::ApplyBindings(GetCatalog(), Player, Target, TRow::StaticStruct(), &Row);
    }

    /** The rush plan's multiplier for Move by Rusher's style (PSPlayerDNA::RushMoveScale). */
    float GetRushMoveScale(const FPlayerAttributes& Rusher, EPSRushMove Move);

    /** The traits a scout sees in Player (PSPlayerDNA::GetScoutingTraits). */
    UFUNCTION(BlueprintCallable, Category = "DNA")
    TArray<FPSScoutingTrait> GetScoutingTraits(const FPlayerAttributes& Player);

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    UPROPERTY(Transient)
    FPSPlayerDNACatalog Catalog;

    bool bCatalogLoaded = false;
};
