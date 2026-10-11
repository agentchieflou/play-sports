// PSCharacterLook.h - Epic 147.2: what a player looks like on the field, in the colours of the team he plays for
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSCharacterLook.generated.h"

class UMaterialInstanceDynamic;
class UPSMatchSetup;
class UPSTelemetryBus;
class USkeletalMeshComponent;
struct FPSTelemetryGameStateEvent;
struct FPSTelemetryLineupEvent;

/**
 * How a player looks (Data/character_look.json; Architecture rule 4). Defaults equal the file.
 *
 *  - The character: SkeletalMeshPath, the world kit's stand-in on the engine's body bones (Epic
 *    146.5), turned MeshYawDegrees to face +X. Its PrimarySlots (the jersey) and SecondarySlots
 *    (the pants) take the team's colours through TeamColorParameter. Empty until the import's
 *    asset and its parameter are confirmed: then the fallback is the look.
 *  - The fallback: FallbackMeshPath (a cylinder, FallbackMeshSizeCm across and tall at scale 1,
 *    centred) sized to the pawn's capsule, in the team's primary colour through
 *    FallbackColorParameter. It needs no imported asset.
 *  - NeutralColor: before the match's teams are known.
 */
USTRUCT(BlueprintType)
struct FPSCharacterLookStyle
{
    GENERATED_BODY()

    FPSCharacterLookStyle()
    {
        PrimarySlots.Add(TEXT("top"));
        SecondarySlots.Add(TEXT("bottom"));
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    FString SkeletalMeshPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    float MeshYawDegrees = -90.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    FName TeamColorParameter = TEXT("BaseColorFactor");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    TArray<FName> PrimarySlots;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    TArray<FName> SecondarySlots;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    FString FallbackMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    float FallbackMeshSizeCm = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    FString FallbackMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    FName FallbackColorParameter = TEXT("Color");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    FString NeutralColor = TEXT("#9AA0A6");
};

/**
 * UPSCharacterLookComponent is the one authority on what a player looks like (Epic 147.2; Epic
 * 143's inclusive looks extend it). APSPlayerPawn owns one.
 *
 *  - At BeginPlay it dresses its pawn: the character mesh when the data names one that loads,
 *    else the fallback body sized to the pawn's capsule. Neither collides.
 *  - Its team: the game mode hands it the match (SetMatchSetup, the one authority on which teams
 *    play). Pawns keep their side, and the team with the ball plays offense, so the pawn plays for
 *    the home team when its side is offense and the home team has the ball, or its side is defense
 *    and the away team has it. The possession is the bus's GameState (the play simulation's).
 *    The colours change when the sides line up (the bus's Lineup), after the personnel manager
 *    has brought the new team on, not the moment the ball changes hands.
 *  - The colours: the team's PrimaryColor and SecondaryColor from the team identity data
 *    (Data/sample_teams.json, read through UPSDataIngestion).
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSCharacterLookComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSCharacterLookComponent();

    static FString GetDefaultStylePath();

    /** Data/character_look.json through UPSDataIngestion; the defaults when missing or unsound. */
    static FPSCharacterLookStyle LoadStyle(const FString& Path);

    /** Problems with Style, one line each (empty when sound); mirrors tools/validate_data.py. */
    static TArray<FString> ValidateStyle(const FPSCharacterLookStyle& Style);

    /** TeamId's primary and secondary colours in the team identity data at TeamsJsonPath. False
     *  when the team isn't there or its colours aren't #RRGGBB. */
    static bool FindTeamColors(const FString& TeamsJsonPath, FName TeamId, FLinearColor& OutPrimary, FLinearColor& OutSecondary);

    /** Uses Style from now on (BeginPlay loads it from data). */
    void SetStyle(const FPSCharacterLookStyle& InStyle) { Style = InStyle; }

    /** Dresses the owner: the character mesh, else the fallback body; then its colours. False
     *  when neither can be loaded. BeginPlay calls it; headless tests call it. */
    bool ApplyLook();

    /** The match whose teams the pawn plays for (the game mode's UPSMatchSetup). Applies the team
     *  it plays for now. */
    void SetMatchSetup(const UPSMatchSetup* InMatchSetup);

    /** Follows the possession and the lineups on Bus. BeginPlay binds; tests bind by hand. */
    void BindToBus(UPSTelemetryBus* Bus);
    void UnbindFromBus();

    void HandleGameState(const FPSTelemetryGameStateEvent& Event);
    void HandleLineup(const FPSTelemetryLineupEvent& Event);

    /** The team the pawn plays for now; None before the match is known. */
    FName GetTeamId() const { return TeamId; }

    FLinearColor GetPrimaryColor() const { return PrimaryColor; }
    FLinearColor GetSecondaryColor() const { return SecondaryColor; }

    /** True when the character mesh is on; false for the fallback body. */
    bool IsUsingCharacterMesh() const { return CharacterMesh != nullptr; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /** Works out the team the pawn plays for now and recolours it. */
    void RefreshTeam();
    void RefreshColors();

    FPSCharacterLookStyle Style;

    /** The game mode's match setup; the game mode owns it. */
    TWeakObjectPtr<const UPSMatchSetup> MatchSetup;

    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> CharacterMesh;

    /** The fallback body's material, or the character's jersey and pants materials. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> PrimaryMaterials;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> SecondaryMaterials;

    FName TeamId;
    FLinearColor PrimaryColor = FLinearColor::Gray;
    FLinearColor SecondaryColor = FLinearColor::Gray;

    /** The bus's last word on possession: the home team has the ball first. */
    bool bHomeHasPossession = true;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FDelegateHandle GameStateHandle;
    FDelegateHandle LineupHandle;
};
