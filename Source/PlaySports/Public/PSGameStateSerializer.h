// PSGameStateSerializer.h - Epic 82: the game state an outside model reads, sized for its context
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "PSGameIntelligenceTypes.h"
#include "PSTelemetryBus.h"

class UPSOpponentModel;
class UPSPersonnelManager;
class UPSStatsEngine;

/**
 * Where the game-state contract reads each fact: from its authority (Architecture rule 6), never
 * a copy. The situation is the play simulation's GameState as the bus last announced it; the
 * personnel UPSPersonnelManager's; the tendencies UPSOpponentModel's read of the human (Epic
 * 78); the statistics UPSStatsEngine's box score of the game (Epic 92). A missing source leaves
 * its section out.
 */
struct FPSGameStateSources
{
    FPSTelemetryGameStateEvent State;

    /** Which sides a human plays: the opponent model reads only a human's tendencies. */
    bool bHumanOffense = false;
    bool bHumanDefense = false;

    const UPSPersonnelManager* Personnel = nullptr;
    UPSOpponentModel* Opponent = nullptr;
    const UPSStatsEngine* Stats = nullptr;

    /** The game's leaders listed per stat category. */
    int32 LeadersPerCategory = 1;
};

/**
 * The game-state serialization contract (Epic 82): one compact JSON object an outside model
 * reads, never longer than the budget it is given.
 *
 *   {"contract":"play-sports.game-state/1",
 *    "situation":{phase, quarter, clock, down, distance, yardLine, yardsToGoal, offense
 *                 ("home"/"away"), score, offenseMargin, timeouts, drives, lastDrive, ...},
 *    "humans":{"offense":bool,"defense":bool},
 *    "tendencies":[{side, basis, samples, top, topPct, pct{category: percent}}],
 *    "personnel":{"offense":{package, players[]},"defense":{...}},
 *    "stats":{"home":{team line},"away":{...},"leaders":[{cat, id, team, v}]},
 *    "trimmed":[what was left out to fit]}
 *
 * Numbers are whole (percentages, yards, seconds as "m:ss"); free text from the game is clipped
 * short. Over the budget, detail goes in this order until it fits: the leaders, the players on
 * the field, the tendency shares, then the statistics, the personnel and the tendencies whole;
 * "trimmed" names each. The situation always stays, and at or above MinBudgetChars the result
 * always fits.
 */
namespace PSGameStateSerializer
{
    /** The smallest budget the contract always meets. */
    constexpr int32 MinBudgetChars = 1024;

    PLAYSPORTS_API FString Serialize(const FPSGameStateSources& Sources, int32 MaxChars, TArray<FString>* OutTrimmed = nullptr);

    /**
     * A post-game analysis for a model, within MaxChars the same way:
     *   {"contract":"play-sports.post-game/1","final":{home, away, winner},
     *    "drives":[{team, quarter, plays, yards, result}],
     *    "keyPlays":[{play, kind, importance, yards, points, turnover, brokenTackles, quarter,
     *                 down, distance, yardLine, offense}],
     *    "omitted":{"drives":n,"keyPlays":n}}
     * Over the budget the oldest drives go first, then the least important key plays.
     */
    PLAYSPORTS_API FString SerializeAnalysis(const FPSGameAnalysis& Analysis, int32 MaxChars);

    /** Object as JSON text with no whitespace. */
    PLAYSPORTS_API FString ToCompactJson(const TSharedRef<FJsonObject>& Object);

    /** Text as JSON (for a context embedded in a larger object); null when it isn't an object. */
    PLAYSPORTS_API TSharedPtr<FJsonObject> ParseObject(const FString& Json);
}
