// PSPlaybookGenerator.h - Epic 121: playbooks from concept grammars, flavored by coaching identity
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/DataTable.h"
#include "PSPlaybookGeneratorData.h"
#include "PSRosterData.h"
#include "PSStaffData.h"
#include "PSPlaybookGenerator.generated.h"

class UWorld;

/** The pure rules of the playbook generator. */
namespace PSPlaybookGenerator
{
    /** Text with everything but letters and digits removed, for PlayIds: "Trips Right" ->
     *  "TripsRight". */
    PLAYSPORTS_API FString Code(const FString& Text);

    /** How many of each role Formation puts on the field: its personnel package's (Personnel,
     *  Data/personnel_packages.json, Epic 19.5) on that side. Empty when no package lists it. */
    PLAYSPORTS_API TMap<EPlayerRole, int32> GetFormationRoles(const FPSPersonnelCatalog& Personnel, const FString& Formation, bool bOffense);

    /**
     * Every play Concept makes in Formation, which puts Roles on the field: one per combination
     * of its slots' routes, per deception. Each slot goes to the first receiver of a role it
     * lists that the formation still has; when a slot finds none, the concept doesn't fit and
     * there are no plays. Every player gets his own assignment: the quarterback his drop, a
     * run's first back the ball at his spot, slotted receivers their routes in read order, other
     * wide receivers the backside route and everyone else LineKind. PlayIds are
     * <ConceptId>_<Formation>_<n>, with _PA, _ZR or _RPO for a deception.
     */
    PLAYSPORTS_API TArray<FPSPlayDefinition> BuildConceptPlays(const FPSPlaybookGeneratorTuning& Tuning, const FPSPlayConcept& Concept, const FString& Formation, const TMap<EPlayerRole, int32>& Roles);

    /**
     * The defensive call sheet for Front, which puts Roles on the field: one call per coverage
     * and pressure the coverage can afford (its MaxBlitzers) and the front can send. Blitzers
     * take a role's first jobs, then the coverage's jobs for that role in order (its last one
     * repeated for extra players); defensive linemen do the front's LineKind. A call with a
     * pressure is a Blitz, else its coverage's Category. PlayIds are
     * Def_<Formation>_<Shell>_<PressureId>.
     */
    PLAYSPORTS_API TArray<FPSPlayDefinition> BuildDefensiveCalls(const FPSPlaybookGeneratorTuning& Tuning, const FPSDefensiveFrontDef& Front, const TMap<EPlayerRole, int32>& Roles);

    /**
     * Problems with Play as a generated scrimmage play, one line each (empty when it is valid):
     * the playbook contract (tools/content_contracts.py's validate_playbook: a category, roles
     * and assignment kinds of the play's side, a RouteId only on a Route, a front and shell on
     * defense only, Epic 72's deception rules), each RouteId in RouteLibrary, and a formation
     * whose personnel package gives every player on the field exactly one assignment.
     */
    PLAYSPORTS_API TArray<FString> ValidatePlay(const FPSPlayDefinition& Play, const UDataTable* RouteLibrary, const FPSPersonnelCatalog& Personnel);

    /** Problems with Tuning, one line each (empty when sound), against the route library and the
     *  personnel packages. */
    PLAYSPORTS_API TArray<FString> ValidateTuning(const FPSPlaybookGeneratorTuning& Tuning, const UDataTable* RouteLibrary, const FPSPersonnelCatalog& Personnel);

    /** One play as a line of a playbook file: FPSPlayDefinition's field names, the empty and
     *  default fields left out. */
    PLAYSPORTS_API FString PlayJson(const FPSPlayDefinition& Play);

    /** A library play and what it was made from, which a scheme's flavor weighs. */
    struct FPSLibraryPlay
    {
        FPSPlayDefinition Play;
        FString ConceptId;
        FString Shell;
        FString PressureId;
    };
}

/**
 * UPSPlaybookGenerator makes playbooks (Epic 121) out of concept grammars in
 * Data/playbook_generator.json instead of hand-authoring each play.
 *
 *  - The offense: each concept (flood, mesh, dagger, smash, verticals, zone and gap runs,
 *    screens, ...) lines up in every formation its route slots fit, once per route variant and
 *    deception, so a few concepts make hundreds of plays (BuildLibrary).
 *  - The defense: fronts x coverages x pressures, as each coverage can afford.
 *  - A coaching identity (Epic 89's scheme, Data/coaching_staffs.json) gets its own playbook
 *    (GeneratePlaybook): its formations only, its categories in proportion to its
 *    CategoryWeights raised to CategoryEmphasis, and inside a category the concepts, shells and
 *    pressures its flavor likes. An air raid's book is mostly passes, a power run's mostly runs
 *    and play-action.
 *
 * Every play it makes is a plain FPSPlayDefinition: the play loader (UPSPlaybookIngestion), the
 * AI (UPSPlayOrchestrator, UPSRouteRunnerComponent) and the content contracts take it as they
 * take the hand-written playbook; PSPlaybookGenerator::ValidatePlay checks it.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSPlaybookGenerator : public UObject
{
    GENERATED_BODY()

public:
    /** Data/playbook_generator.json under the project directory. */
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. False, with the
     *  tuning unchanged, on a missing or malformed file. */
    UFUNCTION(BlueprintCallable, Category = "PlaybookGenerator")
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** The tuning in use, loaded from the default path on first use. */
    const FPSPlaybookGeneratorTuning& GetTuning();

    void SetTuning(const FPSPlaybookGeneratorTuning& InTuning);

    /** The personnel packages formations are read from; by default Data/personnel_packages.json. */
    void SetPersonnel(const FPSPersonnelCatalog& InPersonnel);

    const FPSPersonnelCatalog& GetPersonnel();

    /** The route library plays are checked against; by default the play caller's
     *  (UPSPlayCallSubsystem::GetDefaultRoutesPath). */
    void SetRouteLibrary(UDataTable* InRoutes);

    UDataTable* GetRouteLibrary();

    /** Every play the grammar makes on a side, in Formations (all of the tuning's when empty). */
    UFUNCTION(BlueprintCallable, Category = "PlaybookGenerator")
    TArray<FPSPlayDefinition> BuildLibrary(bool bOffense, const TArray<FString>& Formations);

    /** Scheme's playbook on its side, the same for the same seed. Empty when the tuning is
     *  unsound or the scheme runs none of the tuning's formations. */
    UFUNCTION(BlueprintCallable, Category = "PlaybookGenerator")
    FPSGeneratedPlaybook GeneratePlaybook(int32 Seed, const FPSSchemeDef& Scheme);

    /** PSPlaybookGenerator::ValidatePlay for each of Plays against this generator's routes and
     *  personnel, prefixed with the PlayId. */
    UFUNCTION(BlueprintCallable, Category = "PlaybookGenerator")
    TArray<FString> ValidatePlays(const TArray<FPSPlayDefinition>& Plays);

    /**
     * Epic 35's art/AI consistency check (PSPlayArt::ValidatePlayArt) for each of Plays: the play
     * lined up in World in its formation's personnel as the game mode lines players up
     * (APSFieldGrid::ComputeLineup) -- a defense against the default offensive package, so its man
     * defenders have receivers -- resolved into each player's job as the snap resolves it
     * (PSPlayResolution), its man matchups as the defense AI takes them, and compiled into art as
     * the overlay compiles it (PSPlayArt::CompilePlayArt, with World's UPSOverlayPlayArtSubsystem's
     * style and break angle). A play whose category draws art must draw some. Problems are
     * prefixed with the PlayId. World is the caller's own (a tool's or a test's): the players it
     * lines up are destroyed before it returns.
     */
    UFUNCTION(BlueprintCallable, Category = "PlaybookGenerator")
    TArray<FString> ValidatePlayArt(UWorld* World, const TArray<FPSPlayDefinition>& Plays);

    /** Writes Plays as a playbook file ({ "Plays": [...] }, UTF-8) that
     *  UPSPlaybookIngestion::LoadPlaysFromJson loads. */
    UFUNCTION(BlueprintCallable, Category = "PlaybookGenerator")
    static bool WritePlaybook(const TArray<FPSPlayDefinition>& Plays, const FString& FilePath);

private:
    TArray<PSPlaybookGenerator::FPSLibraryPlay> BuildEntries(bool bOffense, const TArray<FString>& Formations);

    /** Loads whatever has not been set. */
    void EnsureLoaded();

    UPROPERTY(Transient)
    FPSPlaybookGeneratorTuning Tuning;

    UPROPERTY(Transient)
    FPSPersonnelCatalog Personnel;

    UPROPERTY(Transient)
    UDataTable* RouteLibrary = nullptr;

    bool bTuningLoaded = false;
    bool bPersonnelSet = false;
};
