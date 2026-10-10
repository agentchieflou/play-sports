// PSVersusTypes.h - Epic 107: local head-to-head seats, phases and the versus rules (data)
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSSituationData.h"
#include "PSVersusTypes.generated.h"

class APSPlayerController;

/** The team a seat plays for. Home is the team the play simulation calls home. */
UENUM(BlueprintType)
enum class EPSVersusTeam : uint8
{
    None,
    Home,
    Away
};

/** Where a head-to-head session is. */
UENUM(BlueprintType)
enum class EPSVersusPhase : uint8
{
    /** No session: one human against the CPU, or nobody. */
    Inactive,
    /** Seats are being claimed and sides picked. */
    SideSelect,
    Playing,
    Paused,
    /** A seat quit (a forfeit) or the session was closed. */
    Ended
};

/** How the two players see the game. */
UENUM(BlueprintType)
enum class EPSVersusScreen : uint8
{
    /** One view both players watch: anything drawn is seen by both. */
    Shared,
    /** A view per player: an overlay can be drawn in one player's view only. */
    Split
};

/** The pre-snap overlays that give away a side's call (Epics 27 and 31). */
UENUM(BlueprintType)
enum class EPSVersusOverlay : uint8
{
    /** The offense's route ribbons and endpoint rings (Epic 27). */
    RouteArt,
    /** The defense's zone stars, man lines and blitz arrows (Epic 31). */
    DefensiveIcons
};

/** Who may see a side's overlay in a head-to-head game. */
UENUM(BlueprintType)
enum class EPSVersusAudience : uint8
{
    Everyone,
    /** Only the side it belongs to: on a split screen, that player's view; on a shared screen,
     *  nobody, because the opponent watches the same view. */
    OwnerOnly,
    Nobody
};

/** What a seat can know about a side's call for the coming snap: never the play itself. */
UENUM(BlueprintType)
enum class EPSVersusCallState : uint8
{
    /** No call window is open. */
    Closed,
    /** The window is open and the side has not called. */
    Choosing,
    /** The side has called. */
    Called
};

/** The head-to-head rules (Data/versus_rules.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSVersusRules
{
    GENERATED_BODY()

    /** The player each seat controls at the start of a down on offense ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    EPlayerRole OffenseControlRole = EPlayerRole::Quarterback;

    /** ... and on defense. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    EPlayerRole DefenseControlRole = EPlayerRole::Linebacker;

    /** Every new down puts both players back on their side's control role; off, a player keeps
     *  whoever they controlled at the whistle while their side is unchanged. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    bool bResetControlEachDown = true;

    /** The defending player may switch players after the snap. Off, they keep the defender they
     *  picked before it until the whistle, unless their side takes the ball away. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    bool bDefenseSwitchDuringPlay = true;

    /** The defending player may use the pre-snap pick buttons. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    bool bDefensePreSnapPicks = true;

    /** One shared view or a view per player. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    EPSVersusScreen Screen = EPSVersusScreen::Shared;

    /** Who sees the offense's route art before the snap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    EPSVersusAudience RouteArtAudience = EPSVersusAudience::OwnerOnly;

    /** Who sees the defense's assignment icons before the snap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    EPSVersusAudience DefensiveIconsAudience = EPSVersusAudience::OwnerOnly;

    /** Pauses each player may call per half; -1 for no limit. A disconnect's pause is not
     *  counted. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    int32 PausesPerHalf = 3;

    /** A player may pause only between plays (before the snap). A disconnect pauses anyway. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    bool bPauseOnlyBetweenPlays = true;

    /** Play resumes only when both players have said they are ready; off, the player who
     *  paused resumes alone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    bool bResumeNeedsBoth = true;

    /** Seconds counted down, still paused, between everyone being ready and play resuming. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    float ResumeCountdownSeconds = 3.f;

    /** A player's controller disconnecting pauses the game, and it cannot resume until that
     *  controller is back. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    bool bPauseOnDisconnect = true;

    /** Quitting a head-to-head game forfeits it to the other player. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Versus")
    bool bQuitForfeits = true;
};

/** One of the two places at the machine (Epic 107). */
USTRUCT(BlueprintType)
struct FPSVersusSeat
{
    GENERATED_BODY()

    /** The player controller seated here; each seat has its own, with its own contexts. */
    UPROPERTY(Transient)
    TWeakObjectPtr<APSPlayerController> Controller;

    /** The Slate / platform user whose devices drive this seat (the local player's controller
     *  ID). */
    UPROPERTY(BlueprintReadOnly, Category = "Versus")
    int32 UserIndex = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category = "Versus")
    EPSVersusTeam Team = EPSVersusTeam::None;

    UPROPERTY(BlueprintReadOnly, Category = "Versus")
    bool bReady = false;

    /** Pauses called this half. */
    UPROPERTY(BlueprintReadOnly, Category = "Versus")
    int32 PausesUsed = 0;

    /** Ready to play on while the game is paused. */
    UPROPERTY(BlueprintReadOnly, Category = "Versus")
    bool bResumeConfirmed = false;

    /** The seat's controller is disconnected. */
    UPROPERTY(BlueprintReadOnly, Category = "Versus")
    bool bDisconnected = false;

    /** The tempo this seat's offense plays at, kept while it defends (Epic 76). */
    UPROPERTY(BlueprintReadOnly, Category = "Versus")
    EPSTempo Tempo = EPSTempo::Huddle;

    bool IsClaimed() const { return Controller.IsValid(); }
};
