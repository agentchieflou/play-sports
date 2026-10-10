// PSFormations.h - where each player lines up for the call: formations, fronts and shells from data
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSPlayRecognitionTypes.h"
#include "PSFormations.generated.h"

/**
 * One player's spot in a formation, front or shell (Data/formations.json). All distances are
 * yards on the field's one frame (PSField); the side is the offense's strength or the other.
 *
 * The spot's reference across the field is, in order: the offensive line at Technique (a
 * defender's alignment technique, e.g. "5": the outside shade of the tackle), the OverReceiver-th
 * receiver from the outside on Side, or else the ball. LateralYardOffset moves him from that
 * reference toward Side (negative: back across it).
 */
USTRUCT(BlueprintType)
struct FPSFormationSpawnPoint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    EPlayerRole Role = EPlayerRole::Quarterback;

    // Offset in yards relative to the line of scrimmage (positive values mean in the play direction, negative behind)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float ScrimmageYardOffset = 0.0f;

    // Yards across the field from the spot's reference (the ball when it has none), toward Side
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float LateralYardOffset = 0.0f;

    /** "Strong" (the offense's strength; the default) or "Weak". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FName Side;

    /** A defender's alignment technique over the offensive line on Side (one of the catalog's
     *  Techniques); None when the spot doesn't key on the line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FName Technique;

    /** A defender lines up over the receiver this many from the outside on Side (1 is the
     *  widest); 0 when he doesn't key on a receiver. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    int32 OverReceiver = 0;
};

/** A defensive alignment technique: where it is on the offensive line. */
USTRUCT(BlueprintType)
struct FPSTechniqueDef
{
    GENERATED_BODY()

    /** Its name, e.g. "0", "2i", "5", "9". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FName Technique;

    /** The lineman it keys on, counted from the center: 0 the center, 1 the guard, 2 the
     *  tackle, 3 the end man's spot one spacing outside the tackle (where an inline tight end
     *  stands). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    int32 Lineman = 0;

    /** -1 his inside shade, 0 head up, 1 his outside shade (ShadeYards). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    int32 Shade = 0;
};

/** An offensive formation, named as the plays name it (FPSPlayDefinition::Formation). */
USTRUCT(BlueprintType)
struct FPSOffenseFormationDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FString Formation;

    /** "Right" or "Left": the side its strength (Side "Strong") lines up on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FName Strength = FName(TEXT("Right"));

    /** What the defense reads the quarterback's depth as (Epic 80's names: "UnderCenter",
     *  "Pistol", "Shotgun"). The slots decide where he stands; this is the read they must give. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FName QBAlignment;

    /** The backfield set the defense reads ("Empty", "Single", "Offset", "I", "Split", "Full"):
     *  likewise the read the slots must give. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FName Backfield;

    /** Everyone but the line (which the catalog's line spacing sets), role by role: the Nth
     *  player of a role takes the Nth slot of that role. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    TArray<FPSFormationSpawnPoint> Slots;
};

/** A defensive front's alignment, named as the plays name it (FPSPlayDefinition::Front, the
 *  fronts of Data/run_fits.json). */
USTRUCT(BlueprintType)
struct FPSFrontAlignmentDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FString Front;

    /** The line and linebackers (and the backs, for a front no shell completes), role by role. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    TArray<FPSFormationSpawnPoint> Slots;
};

/** A coverage shell's alignment of the defensive backs, named as the plays and
 *  Data/coverage_matchups.json name it (FPSPlayDefinition::CoverageShell). */
USTRUCT(BlueprintType)
struct FPSShellAlignmentDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FString Shell;

    /** The backs in order: the Nth back takes the Nth slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    TArray<FPSFormationSpawnPoint> Slots;
};

/** Every formation, front and shell's alignment (Data/formations.json; Architecture rule 4).
 *  Scalar defaults equal the file. */
USTRUCT(BlueprintType)
struct FPSFormationCatalog
{
    GENERATED_BODY()

    /** The offensive line: centred on the ball, this far apart ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float LinemanSpacingYards = 1.5f;

    /** ... this far behind the line of scrimmage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float LineSetbackYards = 0.5f;

    /** A shade technique is this far inside or outside head up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float ShadeYards = 0.5f;

    /** Nobody lines up closer than this to an end line or a sideline. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float BoundaryMarginYards = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    TArray<FPSTechniqueDef> Techniques;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    TArray<FPSOffenseFormationDef> OffenseFormations;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    TArray<FPSFrontAlignmentDef> FrontAlignments;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    TArray<FPSShellAlignmentDef> ShellAlignments;
};

/** What the sides line up in: the offense's formation, the defense's front and shell. An empty
 *  name lines that part up by role (APSFieldGrid's role lineup). */
USTRUCT(BlueprintType)
struct FPSLineupCall
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FString OffenseFormation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FString DefenseFront;

    /** A coverage shell; empty or "None" for none (the front places the backs). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    FString DefenseShell;
};

/** A lineup, and how much of it came from the data. */
struct PLAYSPORTS_API FPSLineupResult
{
    /** One world location per role asked for, in order. */
    TArray<FVector> Spots;

    bool bFormationFound = false;
    bool bFrontFound = false;
    bool bShellFound = false;

    /** +1 when the offense's strength is right of the ball (+Y), -1 left: the formation's, or
     *  for the defense the strength it read. */
    int32 StrongSide = 1;

    /** Players a known formation, front or shell had no slot for, lined up by role instead. */
    int32 RoleFallbacks = 0;
};

/**
 * PSFormations is the one authority on where each player lines up for a call
 * (APSFieldGrid::ComputeLineup delegates here):
 *
 *  - The offense lines up in its formation: the line centred on the ball, everyone else on his
 *    role's slot, mirrored when the formation's strength is left.
 *  - The defense lines up in its front and shell against the offense as it stands: it reads the
 *    offense's strength as Epic 80's classifier reads it (PSPlayRecognition::ClassifyFormation),
 *    puts its linemen and linebackers on their techniques over the offensive line, and its backs
 *    over the receivers they key on, counted from the outside. A back whose side runs out of
 *    receivers takes the other side's widest receiver nobody has yet (the nickel travelling to
 *    trips), or, with none, the end man's spot.
 *  - A formation, front or shell the catalog doesn't know lines that part up by role, as before,
 *    with a warning (once per name). Nobody lines up inside BoundaryMarginYards of an end line
 *    or a sideline.
 *
 * Epic 67's UPSDefenderPreSnapSubsystem then moves the safeties into the called structure and
 * walks blitzers up from these spots, and Epic 69's press corners walk up from them.
 */
namespace PSFormations
{
    PLAYSPORTS_API FString GetDefaultDataPath();

    /** The catalog in use: Data/formations.json, read once through UPSDataIngestion; empty
     *  (everyone lined up by role) when it is missing or unsound. */
    PLAYSPORTS_API const FPSFormationCatalog& GetCatalog();

    /** Problems with Catalog, one line each (empty when sound). */
    PLAYSPORTS_API TArray<FString> Validate(const FPSFormationCatalog& Catalog);

    /** The formation, front or shell of that name (case-insensitive, like the plays'), or null. */
    PLAYSPORTS_API const FPSOffenseFormationDef* FindFormation(const FPSFormationCatalog& Catalog, const FString& Formation);
    PLAYSPORTS_API const FPSFrontAlignmentDef* FindFront(const FPSFormationCatalog& Catalog, const FString& Front);
    PLAYSPORTS_API const FPSShellAlignmentDef* FindShell(const FPSFormationCatalog& Catalog, const FString& Shell);

    /** True for a shell that places nobody: empty or "None". */
    PLAYSPORTS_API bool IsNoShell(const FString& Shell);

    /**
     * Lines up players of Roles (either side or both) for Call with the ball at Ball (the offense
     * attacks +X). The defense reads Offense when given, otherwise the offensive players lined up
     * in this same call; Recognition is the tuning it reads the strength with.
     */
    PLAYSPORTS_API FPSLineupResult LineUp(const FPSFormationCatalog& Catalog, const TArray<EPlayerRole>& Roles, const FVector& Ball,
        const FPSLineupCall& Call, const TArray<FPSAlignedPlayer>* Offense, const FPSPlayRecognitionTuning& Recognition);
}
