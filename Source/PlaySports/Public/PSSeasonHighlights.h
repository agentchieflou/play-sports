// PSSeasonHighlights.h - Epic 42: a franchise season's highlights, as the save keeps them
#pragma once

#include "CoreMinimal.h"
#include "PSSeasonHighlights.generated.h"

/** Why a play made the highlights. */
UENUM(BlueprintType)
enum class EPSHighlightKind : uint8
{
    /** Points went up. */
    Score,
    /** The ball changed hands in the play. */
    Turnover,
    /** Neither, but enough yards, broken tackles or a swing in who wins. */
    BigPlay
};

/** A highlight kept for a franchise season (Track G): the facts, and where its clip is saved. */
USTRUCT(BlueprintType)
struct FPSSeasonHighlight
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    int32 Week = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    FName HomeTeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    FName AwayTeamId;

    /** The play's place in its game, from 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    int32 PlayNumber = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    EPSHighlightKind Kind = EPSHighlightKind::BigPlay;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float Importance = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    int32 Quarter = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    float GameClockSeconds = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    int32 Yards = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    int32 Points = 0;

    /** The saved clip (UPSReplaySubsystem::SaveClip, Epic 41); empty when it couldn't be
     *  written. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Highlights")
    FString ClipFile;
};
