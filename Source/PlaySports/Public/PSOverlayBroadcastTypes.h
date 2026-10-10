// PSOverlayBroadcastTypes.h - Epic 33: the score bug, the chyrons, and the theme that skins them
#pragma once

#include "CoreMinimal.h"
#include "PSOverlayBroadcastTypes.generated.h"

/** Where on screen the score bug sits. */
UENUM(BlueprintType)
enum class EPSScoreBugAnchor : uint8
{
    BottomCenter,
    TopCenter,
    TopLeft
};

/** What a lower-third chyron is about; its priority and time on screen come from the theme. */
UENUM(BlueprintType)
enum class EPSChyronKind : uint8
{
    /** Points on the board. */
    ScoreAlert,
    /** A finished drive: plays, yards, how it ended. */
    DriveSummary,
    /** One play's line: a catch, a run, a sack, an interception. */
    PlayStat,
    /** A player's line from a stats source (Epic 92's box score once it exists). */
    StatLine,
    /** Anything else a caller pushes. */
    Custom
};

/** How one kind of chyron is shown. */
USTRUCT(BlueprintType)
struct FPSChyronKindStyle
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    EPSChyronKind Kind = EPSChyronKind::Custom;

    /** Higher shows first, and may cut in on a lower one that has been up MinShowSeconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    int32 Priority = 0;

    /** Time on screen. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    float Seconds = 4.f;
};

/**
 * The broadcast package (Data/broadcast_overlay.json; Architecture rule 4): labels, colors,
 * type sizes and placement for the score bug and chyrons, the thresholds of their special
 * states, and the chyron rules. Track C's branding reskins the game by swapping this data.
 * Colors are "#RRGGBB".
 */
USTRUCT(BlueprintType)
struct FPSBroadcastOverlayTheme
{
    GENERATED_BODY()

    /** Shown for a side whose team isn't known. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString HomeLabel = TEXT("HOME");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString AwayLabel = TEXT("AWAY");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString HomeColor = TEXT("#1F4E9C");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString AwayColor = TEXT("#B3262E");

    /** Use each known team's own abbreviation and primary color (Data/sample_teams.json). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    bool bUseTeamColors = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString BarColor = TEXT("#101418");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString TextColor = TEXT("#FFFFFF");

    /** The down-and-distance box inside the opponent's RedZoneYardLine. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString RedZoneColor = TEXT("#D7263D");

    /** The clock inside the last TwoMinuteSeconds of a half. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString TwoMinuteColor = TEXT("#F2B705");

    /** A timeout left; a used one is drawn in TimeoutUsedColor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString TimeoutColor = TEXT("#F2B705");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString TimeoutUsedColor = TEXT("#3A4048");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString ChyronColor = TEXT("#101418");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    EPSScoreBugAnchor Anchor = EPSScoreBugAnchor::BottomCenter;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    int32 ScoreFontSize = 28;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    int32 TextFontSize = 18;

    /** The red zone starts at this yard line, counted from the offense's goal line (80 is
     *  the opponent's 20). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    int32 RedZoneYardLine = 80;

    /** The two-minute state: this little left in the 2nd or 4th quarter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    float TwoMinuteSeconds = 120.f;

    /** Chyrons waiting at most; a new one pushes out the lowest priority, oldest first. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    int32 ChyronMaxQueued = 4;

    /** A chyron is up at least this long before a higher priority one may cut in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    float ChyronMinShowSeconds = 2.f;

    /** Empty time between two chyrons. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    float ChyronGapSeconds = 0.5f;

    /** One entry per EPSChyronKind. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    TArray<FPSChyronKindStyle> ChyronKinds;

    const FPSChyronKindStyle* FindChyronKind(EPSChyronKind InKind) const
    {
        return ChyronKinds.FindByPredicate([InKind](const FPSChyronKindStyle& Entry) { return Entry.Kind == InKind; });
    }
};

/** One lower-third chyron. */
USTRUCT(BlueprintType)
struct FPSChyron
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    EPSChyronKind Kind = EPSChyronKind::Custom;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString Headline;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Broadcast")
    FString Detail;

    /** From the theme's entry for Kind when pushed. */
    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    int32 Priority = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    float Seconds = 0.f;

    /** Push order, so equal priorities show first come, first served. */
    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    int32 Order = 0;
};

/** Everything the score bug draws, ready to draw. A view of the play simulation's state
 *  (rule 6): it never feeds back. */
USTRUCT(BlueprintType)
struct FPSScoreBugState
{
    GENERATED_BODY()

    /** False until the first GameState event arrives. */
    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    bool bValid = false;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    FString HomeLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    FString AwayLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    FLinearColor HomeColor = FLinearColor::White;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    FLinearColor AwayColor = FLinearColor::White;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    int32 HomeScore = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    int32 AwayScore = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    bool bHomeHasPossession = true;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    int32 HomeTimeouts = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    int32 AwayTimeouts = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    int32 MaxTimeouts = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    int32 Quarter = 1;

    /** "1st" ... "4th", "OT". */
    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    FString QuarterText;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    float GameClockSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    bool bGameClockRunning = false;

    /** "12:05". */
    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    FString GameClockText;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    float PlayClockSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    bool bPlayClockRunning = false;

    /** Whole seconds, shown only before the snap. */
    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    FString PlayClockText;

    /** "3rd & 7 at own 35". */
    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    FString SituationText;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    bool bRedZone = false;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    bool bTwoMinute = false;

    UPROPERTY(BlueprintReadOnly, Category = "Broadcast")
    FString Phase;
};
