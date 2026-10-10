// PSSessionServiceTypes.h - Epic 108.5: what the session and matchmaking service deals in
#pragma once

#include "CoreMinimal.h"
#include "PSSessionServiceTypes.generated.h"

/** Where a player's session stands. */
UENUM(BlueprintType)
enum class EPSSessionState : uint8
{
    /** In no session and asking for none. */
    Idle,
    /** Queued for matchmaking. */
    Searching,
    /** Hosting a listed session, waiting for a second player. */
    Hosting,
    /** Two players, a host and a match seed: ready to travel to the match. */
    Matched,
    /** The last request failed (GetLastFailure says why). The next request starts over. */
    Failed
};

/** Why a request failed, or why two requests can't be matched (yet). */
UENUM(BlueprintType)
enum class EPSSessionFailure : uint8
{
    None,
    /** Matchmaking waited longer than the tuning's MaxWaitSeconds. */
    Timeout,
    /** The two builds speak different network protocols. */
    ProtocolMismatch,
    /** The two asked for different game modes. */
    ModeMismatch,
    /** One player's cross-play policy doesn't accept the other's platform or input. */
    CrossPlayRefused,
    /** Different regions, before either has waited RegionRelaxSeconds. Matchmaking keeps waiting. */
    RegionMismatch,
    /** Ratings further apart than the skill window allows yet. Matchmaking keeps waiting. */
    SkillGap,
    /** No listed session has that ID. */
    SessionNotFound,
    /** The session has no open seat. */
    SessionFull,
    /** The service is already searching, hosting or matched; or it has no backend. */
    Busy,
    /** The other player left a matched session. */
    OpponentLeft
};

/** The platform a player plays on: the PC and the iPhone are the ones the owner plays. */
UENUM(BlueprintType)
enum class EPSSessionPlatform : uint8
{
    Unknown,
    Windows,
    Mac,
    Linux,
    IOS,
    Android
};

/** The kind of controls a player plays with (cross-play fairness: Specs/ADR_Online_Architecture.md). */
UENUM(BlueprintType)
enum class EPSSessionInput : uint8
{
    Unknown,
    KeyboardMouse,
    Gamepad,
    Touch
};

/** Who a player agrees to be matched with. Both players' policies must accept the other. */
UENUM(BlueprintType)
enum class EPSCrossPlayPolicy : uint8
{
    /** Any platform, any controls. */
    Anyone,
    /** Only players with the same kind of controls (touch with touch). */
    SameInput,
    /** Only players on the same platform. */
    SamePlatform
};

/** One player as the service knows him. */
USTRUCT(BlueprintType)
struct FPSSessionPlayer
{
    GENERATED_BODY()

    /** The online service's ID for the account: opaque, the same on every machine. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    FString AccountId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    EPSSessionPlatform Platform = EPSSessionPlatform::Unknown;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    EPSSessionInput Input = EPSSessionInput::Unknown;

    /** Matchmaking rating: ratings within the skill window are matched. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    int32 SkillRating = 1000;

    /** Running on battery (a phone unplugged): a worse host. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    bool bOnBattery = false;

    /** On a metered (cellular) network: a worse host. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    bool bOnMeteredNetwork = false;
};

/** What a player asks the service for: a match, or a session to host or join. */
USTRUCT(BlueprintType)
struct FPSMatchRequest
{
    GENERATED_BODY()

    /** The game mode; online head-to-head is "Versus" (Epic 107's travel mode). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    FName Mode = TEXT("Versus");

    /** The build's network protocol: only equal versions are matched. 0 takes the tuning's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    int32 ProtocolVersion = 0;

    /** A region to prefer ("eu", "us-west", ...); empty for any. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    FString Region;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    EPSCrossPlayPolicy CrossPlay = EPSCrossPlayPolicy::Anyone;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    FPSSessionPlayer Player;
};

/** A hosted session as a search lists it. */
USTRUCT(BlueprintType)
struct FPSSessionListing
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Session")
    FString SessionId;

    UPROPERTY(BlueprintReadOnly, Category = "Session")
    FName Mode;

    UPROPERTY(BlueprintReadOnly, Category = "Session")
    int32 ProtocolVersion = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Session")
    FString Region;

    UPROPERTY(BlueprintReadOnly, Category = "Session")
    FPSSessionPlayer Host;

    UPROPERTY(BlueprintReadOnly, Category = "Session")
    int32 OpenSeats = 0;
};

/** A made match: what each machine needs to start it. Both machines get the same match, each
 *  with its own LocalSeat. */
USTRUCT(BlueprintType)
struct FPSSessionMatch
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Session")
    FString SessionId;

    UPROPERTY(BlueprintReadOnly, Category = "Session")
    FName Mode;

    UPROPERTY(BlueprintReadOnly, Category = "Session")
    int32 ProtocolVersion = 0;

    /** The match's master seed, never 0, for UPSNetRandomStreams::SetMatchSeed (Epic 108): the
     *  host's rolls are reproducible from it. */
    UPROPERTY(BlueprintReadOnly, Category = "Session")
    int32 MatchSeed = 0;

    /** The two players, by seat. */
    UPROPERTY(BlueprintReadOnly, Category = "Session")
    TArray<FPSSessionPlayer> Players;

    /** The seat whose machine is the authority: it simulates the match
     *  (Specs/ADR_Online_Architecture.md). */
    UPROPERTY(BlueprintReadOnly, Category = "Session")
    int32 HostSeat = INDEX_NONE;

    /** The seat that plays as the home team (UPSVersusSubsystem's home seat). */
    UPROPERTY(BlueprintReadOnly, Category = "Session")
    int32 HomeSeat = 0;

    /** This machine's seat. */
    UPROPERTY(BlueprintReadOnly, Category = "Session")
    int32 LocalSeat = INDEX_NONE;

    /** Two players, a host and a local seat among them, and a seed. */
    bool IsValid() const
    {
        return Players.Num() == 2 && Players.IsValidIndex(HostSeat) && Players.IsValidIndex(LocalSeat)
            && Players.IsValidIndex(HomeSeat) && MatchSeed != 0;
    }

    bool IsLocalHost() const { return IsValid() && LocalSeat == HostSeat; }
};

/** The matchmaking rules (Data/session_matchmaking.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSSessionMatchmakingTuning
{
    GENERATED_BODY()

    /** This build's network protocol. Bump it whenever two builds can't play each other. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    int32 ProtocolVersion = 1;

    /** Ratings this far apart are matched at once ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    float InitialSkillWindow = 100.f;

    /** ... the window widening this much a second of waiting ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    float SkillWindowGrowthPerSecond = 10.f;

    /** ... up to this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    float MaxSkillWindow = 600.f;

    /** After this long, players in different regions are matched. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    float RegionRelaxSeconds = 30.f;

    /** Matchmaking gives up (Timeout) after this long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    float MaxWaitSeconds = 120.f;

    /** Host choice: the player with the highest score hosts (the first seat on a tie). A PC or
     *  Mac scores this ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    float HostScoreDesktop = 4.f;

    /** ... a machine on mains power this ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    float HostScoreOnPower = 2.f;

    /** ... and one on an unmetered network (Wi-Fi, a cable) this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    float HostScoreUnmetered = 1.f;

    /** A new request's cross-play policy for gamepad and keyboard players ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    EPSCrossPlayPolicy DefaultCrossPlay = EPSCrossPlayPolicy::Anyone;

    /** ... and for touch players (touch meets touch unless the player opts in). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
    EPSCrossPlayPolicy TouchDefaultCrossPlay = EPSCrossPlayPolicy::SameInput;
};
