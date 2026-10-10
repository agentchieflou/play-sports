// PSPlaybookGeneratorData.h - Epic 121: the procedural playbook generator's grammar and output
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSPlaybookData.h"
#include "PSPlaybookGeneratorData.generated.h"

/** One route of a concept, in the quarterback's read order: the first eligible receiver of a
 *  role in Roles (in that order) that the formation still has runs one of Routes. */
USTRUCT(BlueprintType)
struct FPSConceptSlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<EPlayerRole> Roles;

    /** Route library IDs (Data/sample_routes.json), each a variant of the concept. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<FName> Routes;
};

/**
 * An offensive concept (Data/playbook_generator.json): a route combination, a run or a screen
 * that the generator lines up in every formation its slots fit, once per combination of its slots'
 * routes and per deception.
 */
USTRUCT(BlueprintType)
struct FPSPlayConcept
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FName ConceptId;

    /** What the play-call screen calls it, e.g. "Flood". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FString Label;

    /** The concept family, e.g. Flood, Mesh, Dagger, Zone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FName Family;

    /** Its PlayCategory: Run, ShortPass, DeepPass or Screen (a PlayAction deception makes a
     *  pass concept a PlayAction play). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FString Category;

    /** The offensive formations it lines up in; empty for all of the tuning's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<FString> Formations;

    /** The quarterback's spot (his drop, or where he hands off): FormationOffset X, cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    float QBDrop = -500.f;

    /** A run's ball carrier, the first running back: his spot (the mesh point). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FVector BackSpot = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<FPSConceptSlot> Slots;

    /** The route a wide receiver no slot claims runs; none: he blocks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FName BacksideRoute;

    /** What the line, and a tight end or back no slot claims, does: PassBlock or RunBlock. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    EPSAssignmentKind LineKind = EPSAssignmentKind::PassBlock;

    /** The deceptions it is generated with (Epic 72): None, PlayAction on a pass, ZoneRead or
     *  RPO on a run. Empty: None only. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<EPSDeception> Deceptions;
};

/** A defensive front: the formation (its personnel package says who is on the field) and the
 *  front the run fits read (Data/run_fits.json). */
USTRUCT(BlueprintType)
struct FPSDefensiveFrontDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FString Formation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FString Front;

    /** What the defensive linemen do: PassRush, or RunFit on the goal line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    EPSAssignmentKind LineKind = EPSAssignmentKind::PassRush;
};

/** One coverage job: a zone landmark (cm from the ball, played on the defender's own side) or a
 *  man assignment. */
USTRUCT(BlueprintType)
struct FPSCoverageSlotDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    EPlayerRole Role = EPlayerRole::DefensiveBack;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    EPSAssignmentKind Kind = EPSAssignmentKind::ZoneCoverage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FVector Zone = FVector::ZeroVector;
};

/** A coverage: a shell (Data/coverage_matchups.json has its rules) and each role's jobs in
 *  priority order, the deep help first, so a blitzer takes the last one. */
USTRUCT(BlueprintType)
struct FPSCoverageTemplate
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FString Shell;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FString Label;

    /** Its PlayCategory without a pressure: Base or Prevent (any pressure makes it Blitz). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FString Category = TEXT("Base");

    /** The most blitzers it can send and still cover. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    int32 MaxBlitzers = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<FPSCoverageSlotDef> Slots;
};

/** A pressure: how many of each role (an EPlayerRole name) blitz. */
USTRUCT(BlueprintType)
struct FPSPressureDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FName PressureId;

    /** Added to the call's name; empty for no pressure. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FString Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TMap<FString, int32> Blitzers;
};

/** A coaching identity's flavor (Epic 89's SchemeId): how much it likes each concept, shell and
 *  pressure (by ID; 1 when not listed), on top of its scheme's CategoryWeights. */
USTRUCT(BlueprintType)
struct FPSSchemeFlavor
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FName SchemeId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TMap<FString, float> ConceptWeights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TMap<FString, float> ShellWeights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TMap<FString, float> PressureWeights;
};

/** The playbook generator's grammar and weights (Data/playbook_generator.json, Epic 121;
 *  Architecture rule 4). PSPlaybookGenerator::ValidateTuning checks it. */
USTRUCT(BlueprintType)
struct FPSPlaybookGeneratorTuning
{
    GENERATED_BODY()

    /** The offensive formations concepts line up in (each has a personnel package). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<FString> OffenseFormations;

    /** The quarterback's spot on a play-action pass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    float PlayActionDrop = -300.f;

    /** Plays in a scheme's generated playbook, per side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    int32 OffensePlaybookSize = 40;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    int32 DefensePlaybookSize = 24;

    /** A scheme's CategoryWeights are raised to this power to share out its playbook, so an air
     *  raid's book is mostly passes and a power run's mostly runs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    float CategoryEmphasis = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<FPSPlayConcept> Concepts;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<FPSDefensiveFrontDef> DefensiveFronts;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<FPSCoverageTemplate> Coverages;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<FPSPressureDef> Pressures;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<FPSSchemeFlavor> SchemeFlavors;

    const FPSSchemeFlavor* FindFlavor(FName SchemeId) const
    {
        return SchemeFlavors.FindByPredicate([SchemeId](const FPSSchemeFlavor& Flavor) { return Flavor.SchemeId == SchemeId; });
    }
};

/** A scheme's generated playbook (UPSPlaybookGenerator::GeneratePlaybook): plays on its side,
 *  PlayIds prefixed with the SchemeId. */
USTRUCT(BlueprintType)
struct FPSGeneratedPlaybook
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    FName SchemeId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    bool bOffense = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlaybookGenerator")
    TArray<FPSPlayDefinition> Plays;
};
