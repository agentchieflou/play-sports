// PSNetRandomStreams.h - Epic 108: the match's seeded random streams, the online foundation's dice
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/EngineTypes.h"
#include "PSTelemetryBus.h"
#include "PSNetRandomStreams.generated.h"

/**
 * UPSNetRandomStreams is where the live game's physical contests get their chance (Epic 108; the
 * determinism audit's remediation R1, Specs/Determinism_Audit.md): one master seed per match, and
 * from it a stream per play, per kind of roll (Domain: "ThrowScatter", "Tackle", ...) and per
 * player (Key: his PlayerId).
 *
 *  - The match seed is drawn when the world starts, so an unseeded match is a different game
 *    every time. Whoever needs the match reproduced sets it before kickoff with SetMatchSeed (an
 *    online session hands both machines the same one, a test its own) and reads it back with
 *    GetMatchSeed to record it.
 *  - Every Snap heard on the telemetry bus starts a play. The play's seed mixes the match seed,
 *    the play's number in the match and the snap's situation, and every stream starts over from
 *    it. The same match seed and the same snaps roll the same numbers, however long each play
 *    runs and whatever else draws from the engine's global random stream.
 *  - A stream is seeded on its first draw of the play from the play seed, its domain and its key.
 *    One player's draws, or one kind of roll's, never shift another's, so the order in which
 *    actors tick or overlap (audit findings B3 and B4) can't move numbers between them.
 *  - Seeds are mixed with integer arithmetic only, and text is hashed with FCrc after folding it
 *    to lower case, never with GetTypeHash(FName): an FName's hash is its index in the process's
 *    name table, which differs between machines. The same match seed rolls the same integers on
 *    every platform; the floats made from them are subject to the audit's float findings.
 *
 * A draw made while the Snap is still being broadcast comes from the previous play's streams (the
 * order of the bus's Snap subscribers is not fixed). Nothing rolls there today.
 */
UCLASS()
class PLAYSPORTS_API UPSNetRandomStreams : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** The streams of WorldContext's world; null without a world. */
    static UPSNetRandomStreams* Get(const UObject* WorldContext);

    /** Starts the match over from Seed: play 0, every stream begun again. */
    UFUNCTION(BlueprintCallable, Category = "Determinism")
    void SetMatchSeed(int32 Seed);

    UFUNCTION(BlueprintPure, Category = "Determinism")
    int32 GetMatchSeed() const { return MatchSeed; }

    /** The plays begun this match: 0 before the first snap. */
    UFUNCTION(BlueprintPure, Category = "Determinism")
    int32 GetPlayIndex() const { return PlayIndex; }

    /** The seed every stream of the current play is drawn from. */
    UFUNCTION(BlueprintPure, Category = "Determinism")
    int32 GetPlaySeed() const { return PlaySeed; }

    /** Begins the next play's streams from Snap's situation. The bus's Snap calls it. */
    void BeginPlayStreams(const FPSTelemetrySnapEvent& Snap);

    /** The next number in [0, 1) from Domain's stream for Key this play. */
    float Roll(const TCHAR* Domain, FName Key = NAME_None);

    /** The next number in [Min, Max) from Domain's stream for Key this play. */
    float RollRange(const TCHAR* Domain, float Min, float Max, FName Key = NAME_None);

    /** The next random unit vector (uniform over the sphere) from Domain's stream for Key. */
    FVector RollUnitVector(const TCHAR* Domain, FName Key = NAME_None);

    /** The next seed from Domain's stream for Key, for a model that keeps a stream of its own
     *  (UPSCombatRulesModel::SeedDeterminism). */
    int32 RollSeed(const TCHAR* Domain, FName Key = NAME_None);

    /** Roll from the streams of WorldContext's world; from the engine's global stream when it
     *  has none (an object outside any game world). */
    static float RollFor(const UObject* WorldContext, const TCHAR* Domain, FName Key = NAME_None);

    /** RollUnitVector from the streams of WorldContext's world; the global stream without. */
    static FVector RollUnitVectorFor(const UObject* WorldContext, const TCHAR* Domain, FName Key = NAME_None);

    /** RollSeed from the streams of WorldContext's world; the global stream without. */
    static int32 RollSeedFor(const UObject* WorldContext, const TCHAR* Domain, FName Key = NAME_None);

    /** A seed that stays the same all match, for a system that rolls on its own stream from
     *  kickoff to the final whistle (the play simulation): the match seed mixed with Domain. */
    int32 MakeMatchSeed(const TCHAR* Domain) const;

    /** Seed mixed with Salt by integer arithmetic alone (a MurmurHash3 finalizer): the same on
     *  every platform and compiler. */
    static int32 MixSeed(int32 Seed, uint32 Salt);

    /** Text as a seed salt: FCrc::StrCrc32 of the text folded to lower case (an FName key's case
     *  is whatever its first spelling in the process was). The same on every machine. */
    static uint32 HashText(const FString& Text);

    /** A snap's situation as a seed salt: its down, distance and yard line, and the game clock's
     *  bit pattern. */
    static uint32 HashSnap(const FPSTelemetrySnapEvent& Snap);

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    /** Domain's stream for Key this play, seeded on its first use. The reference is good until
     *  the next stream is made. */
    FRandomStream& FindStream(const TCHAR* Domain, FName Key);

    void HandleSnap(const FPSTelemetrySnapEvent& Event);

    /** This play's streams, by "domain" or "domain|key". */
    TMap<FString, FRandomStream> Streams;

    int32 MatchSeed = 0;
    int32 PlayIndex = 0;
    int32 PlaySeed = 0;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
};
