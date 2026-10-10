// PSAIFieldSnapshot.h - Epic 17.5: one read of the field per frame, shared by every AI player
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Stats/Stats.h"
#include "PSPlayerAttributes.h"
#include "PSPlayerPawn.h"
#include "PSAIFieldSnapshot.generated.h"

/** The AI's per-frame work, for `stat PSAI` in a console (Specs/Platform_Audit.md section 5). */
DECLARE_STATS_GROUP(TEXT("PlaySports AI"), STATGROUP_PSAI, STATCAT_Advanced);

/**
 * UPSAIFieldSnapshot is the field as the AI reads it this frame (Epic 17.5): the pawns on it
 * and their roles, gathered with one actor scan per frame and shared by every AI player
 * (UPSDefenderAIComponent, UPSSkillPlayerAIComponent, UPSRushMoveComponent,
 * UPSDefenderGapSubsystem). Before it, each of 22 AI players scanned every actor several times
 * per decision and copied each pawn's attribute row to read its role.
 *
 * The scan holds pawn pointers and roles only; positions, velocities and possession are read
 * live, so a pawn that moves or takes the ball between two decisions in the same frame is
 * seen as it is. A new frame, a pawn spawning, or a pawn on the list being destroyed starts a
 * new scan. Headless tests, whose frame doesn't advance, call Invalidate to stand in for one.
 */
UCLASS()
class PLAYSPORTS_API UPSAIFieldSnapshot : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** The pawns on the field this frame. */
    const TArray<APSPlayerPawn*>& GetPawns();

    /** Their roles as of this frame's scan, parallel to GetPawns. */
    const TArray<EPlayerRole>& GetRoles();

    /** The first pawn on Side with Role (other than Except), or null. */
    APSPlayerPawn* FindPawn(EPSTeamSide Side, EPlayerRole Role, const APSPlayerPawn* Except = nullptr);

    /** The pawn holding the ball, or null. */
    APSPlayerPawn* FindBallCarrier();

    /** Starts a new scan on the next read, as a new frame does. */
    void Invalidate() { bDirty = true; }

    /** How many times the field has been scanned: once a frame however many players decide. */
    int32 GetScanCount() const { return ScanCount; }

    /** World's snapshot pawns; empty without a world or snapshot. The AI reads the field
     *  through this. */
    static const TArray<APSPlayerPawn*>& GetFieldPawns(const UWorld* World);

private:
    void EnsureScanned();
    void HandleActorSpawned(AActor* Actor);

    UPROPERTY(Transient)
    TArray<APSPlayerPawn*> Pawns;

    TArray<EPlayerRole> Roles;
    FDelegateHandle SpawnHandle;
    uint64 ScannedFrame = 0;
    int32 ScanCount = 0;
    bool bDirty = true;
};
