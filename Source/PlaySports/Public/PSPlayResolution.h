// PSPlayResolution.h - Epic 27: a play call resolved into each player's job, for the snap and the play art alike
#pragma once

#include "CoreMinimal.h"
#include "PSDefenseController.h"
#include "PSPlaybookData.h"
#include "PSPlayerAttributes.h"
#include "PSPlayResolution.generated.h"

class AActor;
class APSPlayerPawn;
class UDataTable;
class UPSCoverageMatchupSubsystem;

/** One player's job on a play, as the call resolves it for where he lines up. */
USTRUCT(BlueprintType)
struct FPSResolvedAssignment
{
    GENERATED_BODY()

    /** The player. */
    UPROPERTY()
    TWeakObjectPtr<APSPlayerPawn> Pawn;

    /** Where he stood when the play was resolved. */
    UPROPERTY(BlueprintReadOnly, Category = "Play")
    FVector PawnLocation = FVector::ZeroVector;

    /** The play has a slot for his role: the role's slot of his index, or its last when the
     *  role has more players than slots. False: the play gives his role no job. */
    UPROPERTY(BlueprintReadOnly, Category = "Play")
    bool bHasSlot = false;

    /** His slot as he runs it. On offense his pre-snap changes are applied (Epic 66): a hot
     *  route, a back kept in to block, a tight end released. */
    UPROPERTY(BlueprintReadOnly, Category = "Play")
    FPSPlayAssignment Assignment;

    /** +1 lined up right of the ball (or on it), -1 left of it. Assignments are authored for
     *  the right and run mirrored on the left. */
    UPROPERTY(BlueprintReadOnly, Category = "Play")
    float Mirror = 1.f;

    /** Offense, a Route: where it starts, the line's X and his own split plus his formation
     *  offset. */
    UPROPERTY(BlueprintReadOnly, Category = "Play")
    FVector RouteOrigin = FVector::ZeroVector;

    /** Offense, a Route: the waypoints he runs, in field space. A Route with no RouteId is his
     *  spot alone (the QB's drop, a back's mesh point). Empty for a blocker, or for a RouteId
     *  missing from the library. */
    UPROPERTY(BlueprintReadOnly, Category = "Play")
    TArray<FVector> Waypoints;

    /** Defense: the job his controller runs ... */
    UPROPERTY(BlueprintReadOnly, Category = "Play")
    EPSDefensiveAssignmentType DefensiveType = EPSDefensiveAssignmentType::RunFit;

    /** ... the receiver he shadows, when a matchup set before the snap gives him one (Epic 67) ... */
    UPROPERTY()
    TWeakObjectPtr<AActor> CoverageTarget;

    /** ... and his zone's landmark: the line plus the slot's ZoneOffset, played on his own side
     *  of the field. */
    UPROPERTY(BlueprintReadOnly, Category = "Play")
    FVector ZoneSpot = FVector::ZeroVector;

    /** Defense, man coverage: the receiver he plays, as the defense AI takes him at the snap
     *  (PSPlayResolution::ResolveManMatchups fills it before the snap). */
    UPROPERTY()
    TWeakObjectPtr<APSPlayerPawn> ManReceiver;

    /** True for an offensive player whose job is a Route (a pattern or a spot). */
    bool RunsRoute() const { return bHasSlot && Assignment.Kind == EPSAssignmentKind::Route; }

    /** Defense, a zone: where he plays it, as the defender AI takes it up at the snap -- the
     *  slot's landmark (ZoneSpot), or his own spot when the slot gives no ZoneOffset. */
    FVector GetZoneLandmark() const { return Assignment.ZoneOffset.IsZero() ? PawnLocation : ZoneSpot; }
};

/**
 * The one resolution of a play call into each player's job (Architecture rule 6). Pure: it reads
 * the play, the players where they stand, the route library and the line, plus the pre-snap
 * changes the two pre-snap authorities hold (UPSPreSnapSubsystem for the offense,
 * UPSDefenderPreSnapSubsystem for the defense). UPSPlayOrchestrator hands its result to the AI
 * at the snap; the play art (UPSOverlayPlayArtSubsystem) draws the same result before it, so
 * what is drawn is what is run.
 */
namespace PSPlayResolution
{
    /** The play's assignment for the RoleIndex-th player (0-based) of Role: the role's slot of
     *  that index, or its last slot when the role has more players than slots. Null when the
     *  play has no slot for Role. */
    PLAYSPORTS_API const FPSPlayAssignment* FindAssignmentSlot(const FPSPlayDefinition& Play, EPlayerRole Role, int32 RoleIndex);

    /** Route's waypoints placed at Origin: each is Origin plus its offset, the offset's Y
     *  mirrored by Mirror (-1 for a player left of the ball). */
    PLAYSPORTS_API TArray<FVector> PlaceRoute(const FPSRoute& Route, const FVector& Origin, float Mirror);

    /** The library's RouteId placed at Origin (PlaceRoute); empty without the library or the
     *  route. bWarnIfMissing logs a RouteId the library doesn't have. */
    PLAYSPORTS_API TArray<FVector> ResolveRouteWaypoints(FName RouteId, const UDataTable* RouteLibrary, const FVector& Origin, float Mirror, bool bWarnIfMissing = true);

    /** The defensive controller's job for an assignment kind; a blitz is a pass rush. */
    PLAYSPORTS_API EPSDefensiveAssignmentType ToDefensiveAssignmentType(EPSAssignmentKind Kind);

    /**
     * Play resolved for Pawns lined up against LineOfScrimmage: one entry per pawn, in order,
     * nulls skipped. The players of each role take the role's slots in the order given (a role
     * with more players than slots repeats its last).
     *  - Offense: the slot with the player's pre-snap changes applied. A Route starts from his
     *    own split (LineOfScrimmage's X, his Y) plus his formation offset, and runs the library's
     *    waypoints from there, mirrored for a player left of the ball.
     *  - Defense: the slot's job, overridden by a shadow matchup set before the snap, and its
     *    zone on the defender's own side of the field.
     * bWarnIfRouteMissing logs a RouteId the library doesn't have.
     */
    PLAYSPORTS_API TArray<FPSResolvedAssignment> ResolvePlay(const FPSPlayDefinition& Play, const TArray<APSPlayerPawn*>& Pawns, const UDataTable* RouteLibrary, const FVector& LineOfScrimmage, bool bWarnIfRouteMissing = true);

    /** A player the defense covers in man: a receiver, tight end or back of the offense. Role
     *  is his role as the field snapshot read it. */
    PLAYSPORTS_API bool IsCoverable(const APSPlayerPawn* Pawn, EPlayerRole Role);

    /** The receiver a man defender with nobody named takes: the coverable player nearest him on
     *  the ground that no one in Taken has. Roles is parallel to Pawns. Null when none is left.
     *  UPSDefenderAIComponent picks with it at the snap. */
    PLAYSPORTS_API APSPlayerPawn* NearestOpenReceiver(const APSPlayerPawn* Defender, const TArray<APSPlayerPawn*>& Pawns, const TArray<EPlayerRole>& Roles, const TSet<const APSPlayerPawn*>& Taken);

    /**
     * Each man defender's receiver before the snap, taken as UPSDefenderAIComponent takes them at
     * it, the defenders in Resolved's order: the receiver the play names (a shadow matchup, in
     * CoverageTarget), else the one he lined up to press (Matchups, Epic 69), else the nearest
     * open one -- open meaning no defender before him has him and no other defender lines up to
     * press him. Fills ManReceiver. Pawns and Roles are the field (UPSAIFieldSnapshot).
     */
    PLAYSPORTS_API void ResolveManMatchups(TArray<FPSResolvedAssignment>& Resolved, const TArray<APSPlayerPawn*>& Pawns, const TArray<EPlayerRole>& Roles, const UPSCoverageMatchupSubsystem* Matchups);
}
