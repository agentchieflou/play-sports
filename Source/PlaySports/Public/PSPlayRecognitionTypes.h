// PSPlayRecognitionTypes.h - Epic 80: what the defense reads, and the pure rules it reads by
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSPlayRecognitionTypes.generated.h"

class APSPlayerPawn;

/** What a defender has diagnosed the play as (Epic 80). */
UENUM(BlueprintType)
enum class EPSPlayRead : uint8
{
    /** Still reading: no key he has seen yet. */
    None,
    Run,
    Pass
};

/** One formation class the defense knows (Data/play_recognition.json). The classifier names the
 *  offense's alignment after the first class, in file order, whose every condition it meets;
 *  a condition left at its default matches anything. */
USTRUCT(BlueprintType)
struct FPSFormationClassDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recognition")
    FName ClassId;

    /** "UnderCenter", "Pistol" or "Shotgun"; None for any. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recognition")
    FName QBAlignment;

    /** "Empty", "Single", "Offset", "I", "Split" or "Full"; None for any. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recognition")
    FName Backfield;

    /** At least this many receivers on the strong side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recognition")
    int32 MinStrongSide = 0;

    /** At most this many on the weak side; -1 for any. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recognition")
    int32 MaxWeakSide = -1;

    /** At least this many tight ends on the field. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recognition")
    int32 MinTightEnds = 0;

    /** At least this many receivers split out wide (beyond InlineWidth). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recognition")
    int32 MinSplitReceivers = 0;

    /** How likely the defense thinks a run is from this look, 0-1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recognition")
    float RunLean = 0.5f;
};

/**
 * Formation and play recognition's tuning (Data/play_recognition.json, Epic 80; Architecture
 * rule 4). Distances in cm, speeds cm/s, times seconds. The read scales are what Data/player_dna.json
 * binds (Target "Recognition") and the difficulty tiers can scale.
 */
USTRUCT(BlueprintType)
struct FPSPlayRecognitionTuning : public FTableRowBase
{
    GENERATED_BODY()

    // --- The formation ---

    /** A quarterback this close behind the line is under center ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float UnderCenterMaxDepth = 200.f;

    /** ... this close in the pistol; deeper, in the shotgun. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float PistolMaxDepth = 450.f;

    /** A player (not the quarterback or a lineman) at least this far behind the line and within
     *  BoxHalfWidth of the ball is in the backfield; anyone else is a receiver. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float BackfieldMinDepth = 250.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float BoxHalfWidth = 450.f;

    /** Two backs this close across the field are stacked (an I); further apart, split. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float StackWidth = 100.f;

    /** A lone back further than this from the ball across the field is offset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float OffsetWidth = 100.f;

    /** A receiver this close to the ball across the field is in tight (a tight end here is
     *  inline); further out he is split wide. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float InlineWidth = 500.f;

    /** The run lean of a look no class matches. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    float DefaultRunLean = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
    TArray<FPSFormationClassDef> FormationClasses;

    // --- The keys ---

    /** The drop: the quarterback, ball in hand, at least this far behind the line ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys")
    float DropKeyDepth = 250.f;

    /** ... and at least this much deeper than he lined up (a shotgun snap alone is no drop). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys")
    float DropKeyRetreat = 50.f;

    /** Backfield flow: a back in the box behind the line heading downhill (more than across)
     *  at least this fast. A run shows it; so can a fake. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys")
    float FlowMinSpeed = 200.f;

    /** The line: its linemen, on average, this far forward of their stance is a run (firing
     *  off); this far back, a pass set. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys")
    float LineKeyDistance = 150.f;

    // --- The reads ---

    /** A defender reads a pass key in his reaction (UPSDefenderAIComponent::GetReactionSeconds,
     *  his Awareness) times this ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float PassReadScale = 1.f;

    /** ... a run key in his reaction times this ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float RunReadScale = 1.f;

    /** ... breaks on a throw in his reaction times this (a ball hawk jumps it) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float ThrowReadScale = 1.f;

    /** ... and sees through a play-action fake in his reaction times this. One who hasn't by the
     *  time the fake is sold bites. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float FakeReadScale = 2.f;

    /** What he expects stretches a read against it: expecting the run fully, a pass read takes
     *  (1 + this) times as long (a run read likewise expecting the pass), and the fake read
     *  between (1 - this) and (1 + this) times. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float ExpectationWeight = 0.5f;

    /** What he expects: the formation's run lean, pulled this far (0-1) toward the share of
     *  runs in the offense's recent calls (UPSDeceptionSubsystem::GetRunShare). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float TendencyWeight = 0.5f;

    /** Each defender's fake read varies by up to this fraction either way, seeded per snap, so
     *  two players rated alike don't bite as one. 0 to below 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float LatencyJitter = 0.15f;

    /** A defender who bites holds until he sees through the fake, at most this long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float MaxBiteSeconds = 1.f;
};

/** The offense's formation as the defense reads it from the alignment (Epic 80.1). */
USTRUCT(BlueprintType)
struct FPSFormationRead
{
    GENERATED_BODY()

    /** The first matching class's ClassId; "Unknown" when none matches. */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    FName ClassId;

    /** Personnel: backs then tight ends on the field, e.g. "11", "21". */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    FString Personnel;

    /** "UnderCenter", "Pistol" or "Shotgun"; None without a quarterback. */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    FName QBAlignment;

    /** "Empty", "Single", "Offset", "I", "Split" or "Full". */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    FName Backfield;

    /** +1 when the strength (more receivers) is right of the ball, -1 left. */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    int32 StrongSide = 1;

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    int32 StrongSideReceivers = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    int32 WeakSideReceivers = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    int32 Backs = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    int32 TightEnds = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    int32 InlineTightEnds = 0;

    /** Receivers split out beyond InlineWidth. */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    int32 SplitReceivers = 0;

    /** The class's run lean (DefaultRunLean when none matches). */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    float RunLean = 0.5f;

    /** An offense with a quarterback was read. */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    bool bValid = false;
};

/** One offensive player where he lines up, for the classifier. */
USTRUCT(BlueprintType)
struct FPSAlignedPlayer
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Recognition")
    EPlayerRole Role = EPlayerRole::WideReceiver;

    UPROPERTY(BlueprintReadWrite, Category = "Recognition")
    FVector Location = FVector::ZeroVector;
};

/** What the field shows the defense's keys at one look after the snap (Epic 80.2). */
USTRUCT(BlueprintType)
struct FPSKeyLook
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Recognition")
    bool bPasserHasBall = false;

    /** How far behind the line the quarterback is. */
    UPROPERTY(BlueprintReadWrite, Category = "Recognition")
    float PasserDepth = 0.f;

    /** How much deeper than he lined up. */
    UPROPERTY(BlueprintReadWrite, Category = "Recognition")
    float PasserRetreat = 0.f;

    /** An offensive player other than the quarterback has the ball, and nobody threw it. */
    UPROPERTY(BlueprintReadWrite, Category = "Recognition")
    bool bHandedOff = false;

    /** The fastest back in the box behind the line heading more downhill than across: his speed
     *  toward the line, 0 for none. */
    UPROPERTY(BlueprintReadWrite, Category = "Recognition")
    float BackDownhillSpeed = 0.f;

    UPROPERTY(BlueprintReadWrite, Category = "Recognition")
    bool bHasLine = false;

    /** The linemen's average move along the field since the snap (+ downfield). */
    UPROPERTY(BlueprintReadWrite, Category = "Recognition")
    float LineAdvance = 0.f;
};

/** A key the field shows: what it says, and whether a fake can show it. */
USTRUCT(BlueprintType)
struct FPSKeyRead
{
    GENERATED_BODY()

    /** "Handoff", "Drop", "LineFire", "PassSet" or "Flow". */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    FName Key;

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    EPSPlayRead Read = EPSPlayRead::None;

    /** Backfield flow: a run shows it, and so can a fake. */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    bool bFakeable = false;
};

/** A key as the play showed it: when it first showed, and the player it points at. */
USTRUCT(BlueprintType)
struct FPSKeySighting
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    FPSKeyRead Key;

    /** Seconds after the snap. */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    float ShownAt = 0.f;

    /** The ball carrier on a hand-off, the back the flow showed, the quarterback on a drop. */
    UPROPERTY()
    TWeakObjectPtr<APSPlayerPawn> Player;
};

/** One defender's diagnosis of the play at a moment. */
USTRUCT(BlueprintType)
struct FPSPlayDiagnosis
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    EPSPlayRead Read = EPSPlayRead::None;

    /** The key he read it from. */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    FName Key;

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    bool bFakeable = false;

    /** When the key showed, and when he read it (seconds after the snap). */
    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    float ShownAt = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    float ReadAt = 0.f;

    /** The player the key points at (FPSKeySighting::Player). */
    UPROPERTY()
    TWeakObjectPtr<APSPlayerPawn> Player;
};

/** How long one defender takes over each read this play (seconds). */
USTRUCT(BlueprintType)
struct FPSDefenderReadTimes
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    float PassSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    float RunSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    float ThrowSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Recognition")
    float FakeSeconds = 0.f;
};

/** The pure rules of formation and play recognition (Epic 80). */
namespace PSPlayRecognition
{
    /** The ClassId of a look no formation class matches. */
    PLAYSPORTS_API FName UnknownClassId();

    /** True when Read meets every condition of Def. */
    PLAYSPORTS_API bool MatchesClass(const FPSFormationClassDef& Def, const FPSFormationRead& Read);

    /**
     * The formation of Offense, lined up against LineOfScrimmage (the offense attacks +X; the
     * ball is at LineOfScrimmage's Y). Personnel is the backs and tight ends among them. The
     * first quarterback's depth sets his alignment. Players other than the quarterback and the
     * linemen are backs when they are at least BackfieldMinDepth deep inside the box, otherwise
     * receivers, on the side of the ball they stand (on it counts right); the strong side has
     * more, then more inline tight ends, then is the right. The class is the first in Tuning's
     * FormationClasses that matches.
     */
    PLAYSPORTS_API FPSFormationRead ClassifyFormation(const TArray<FPSAlignedPlayer>& Offense, const FVector& LineOfScrimmage, const FPSPlayRecognitionTuning& Tuning);

    /**
     * The keys Look shows, true keys first: a hand-off (run); the quarterback with the ball at
     * least DropKeyDepth behind the line and DropKeyRetreat deeper than he lined up (the drop,
     * pass); the line LineKeyDistance forward (run) or back (pass set, pass); and backfield flow,
     * a back downhill at FlowMinSpeed (run, fakeable).
     */
    PLAYSPORTS_API TArray<FPSKeyRead> ReadKeys(const FPSKeyLook& Look, const FPSPlayRecognitionTuning& Tuning);

    /**
     * What a defender who reads a pass key in PassSeconds and a run key in RunSeconds has
     * diagnosed by Now, from the play's Sightings: each key once it has shown that long. A true
     * key beats backfield flow, which a fake shows; between keys of the same kind the one shown
     * last wins (a draw: the drop, then the hand-off). None while he has read nothing.
     */
    PLAYSPORTS_API FPSPlayDiagnosis Diagnose(const TArray<FPSKeySighting>& Sightings, float PassSeconds, float RunSeconds, float Now);

    /** What the defense expects: FormationRunLean pulled TendencyWeight of the way toward
     *  RunShare, 0 (pass) to 1 (run). */
    PLAYSPORTS_API float ExpectedRunShare(float FormationRunLean, float RunShare, const FPSPlayRecognitionTuning& Tuning);

    /** A defender's read times from his Reaction (seconds), the expected run share and his
     *  fake read's Jitter (-1 to 1): each read scale times his reaction; a pass read stretched by
     *  ExpectationWeight as far as he expects the run, a run read as far as he expects the pass
     *  (never shortened), the fake read stretched or shortened by it both ways and varied by
     *  LatencyJitter * Jitter. Tuning is his own (his style and difficulty applied). */
    PLAYSPORTS_API FPSDefenderReadTimes ReadTimes(float Reaction, float ExpectedRun, float Jitter, const FPSPlayRecognitionTuning& Tuning);

    /** How long a defender who sees through a fake in FakeReadSeconds holds on one that took
     *  FakeSeconds to sell: the rest of his read, at most MaxBiteSeconds; 0 when he saw
     *  through it in time (he doesn't bite). */
    PLAYSPORTS_API float BiteSeconds(float FakeReadSeconds, float FakeSeconds, const FPSPlayRecognitionTuning& Tuning);

    /** "Run", "Pass" or "None". */
    PLAYSPORTS_API FName ReadName(EPSPlayRead Read);

    /** Problems with Tuning, one line each (empty when sound): a negative number, the pistol
     *  inside under center, a lean, weight or jitter out of range, no formation classes, a class
     *  without an ID or with one used twice (or "Unknown"), an unknown alignment or backfield,
     *  a count below its floor. */
    PLAYSPORTS_API TArray<FString> ValidateTuning(const FPSPlayRecognitionTuning& Tuning);
}
