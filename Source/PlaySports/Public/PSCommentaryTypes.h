// PSCommentaryTypes.h - Epic 23.5: the commentary hooks' tuning and what outside models answer
#pragma once

#include "CoreMinimal.h"
#include "PSTelemetryBus.h"
#include "PSCommentaryTypes.generated.h"

/**
 * Tuning for the commentary hooks (Data/commentary_hooks.json, Epic 23.5; Architecture rule 4):
 * what makes a moment worth describing, how many are kept, and which ones are offered to outside
 * models through Epic 82's bridge. Defaults equal the JSON.
 */
USTRUCT(BlueprintType)
struct FPSCommentaryHookTuning
{
    GENERATED_BODY()

    /** A hit of this much damage (Epic 139) or more is a BigHit moment. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float BigHitDamage = 30.f;

    /** A pass thrown this far (cm, start to target) or farther is Deep; shorter is Short. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float DeepPassCm = 2500.f;

    /** The two-minute warning: the first game state of the second or fourth quarter at or under
     *  this many seconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    float TwoMinuteWarningSeconds = 120.f;

    /** Moments kept (GetMoments), the newest last; also the model lines kept. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 MaxMomentsKept = 64;

    /** Offer moments to outside models while Epic 82's bridge is online. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    bool bOfferToModels = true;

    /** The moments offered: each becomes a Commentary request with the moment as its context. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    TArray<EPSCommentaryMoment> ModelMoments;

    /** The model router's task the requests name (tools/orchestrator/routing.json). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    FString ModelTask = TEXT("narration");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    FString ModelInstructions = TEXT("You are a football play-by-play announcer. Describe this moment in one short spoken sentence, using only the facts given.");

    /** The most characters of a moment's facts a request carries. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Commentary")
    int32 ModelContextChars = 1024;
};

/** A line an outside model wrote for a moment through Epic 82's bridge. */
USTRUCT(BlueprintType)
struct FPSCommentaryModelLine
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    int32 RequestId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    FPSTelemetryCommentaryEvent Moment;

    /** The model's text, as it answered; not ours to translate. */
    UPROPERTY(BlueprintReadOnly, Category = "Commentary")
    FString Text;
};
