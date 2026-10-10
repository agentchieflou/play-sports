// PSUITeamCatalog.h - Epic 101: teams as the team-select screen shows them
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSUITeamCatalog.generated.h"

/** One team ready to display: identity from Data/sample_teams.json, ratings from its roster. */
USTRUCT(BlueprintType)
struct FPSTeamSummary
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Team")
    FName TeamId;

    UPROPERTY(BlueprintReadOnly, Category = "Team")
    FString DisplayName;

    UPROPERTY(BlueprintReadOnly, Category = "Team")
    FString Abbreviation;

    UPROPERTY(BlueprintReadOnly, Category = "Team")
    FString Division;

    UPROPERTY(BlueprintReadOnly, Category = "Team")
    FLinearColor PrimaryColor = FLinearColor::White;

    UPROPERTY(BlueprintReadOnly, Category = "Team")
    FLinearColor SecondaryColor = FLinearColor::Black;

    UPROPERTY(BlueprintReadOnly, Category = "Team")
    FString LogoPath;

    /** Ratings 0-100: the mean of each player's average skill attribute (Speed, Agility,
     *  Strength, Acceleration, Awareness), over the whole roster and each side of the ball. */
    UPROPERTY(BlueprintReadOnly, Category = "Team")
    int32 Overall = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Team")
    int32 Offense = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Team")
    int32 Defense = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Team")
    int32 PlayerCount = 0;

    /** The roster's mean FPlayerAttributes::WeightKg and HeightCm (0 without a roster); team
     *  select shows them in the player's units (Epic 106). */
    UPROPERTY(BlueprintReadOnly, Category = "Team")
    float AverageWeightKg = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Team")
    float AverageHeightCm = 0.f;
};

/**
 * UPSUITeamCatalog turns the league's team data into what team select shows. Teams and their
 * rosters load through UPSDataIngestion (rule 4: no second parser); ratings are derived from
 * the roster attributes, so they move when a roster changes rather than being typed in.
 */
UCLASS(BlueprintType)
class PLAYSPORTS_API UPSUITeamCatalog : public UObject
{
    GENERATED_BODY()

public:
    /** Absolute path of the league's team file: <ProjectDir>/Data/sample_teams.json. */
    static FString GetDefaultTeamsPath();

    /** Loads every team in TeamsJsonPath and its roster. Problems (unreadable roster, bad
     *  color) go to OutErrors; a team with a bad color keeps the default color. */
    static bool BuildSummaries(const FString& TeamsJsonPath, TArray<FPSTeamSummary>& OutSummaries, TArray<FString>& OutErrors);

    /** Parses "#RRGGBB"; false (and OutColor untouched) for anything else. */
    static bool ParseHexColor(const FString& Hex, FLinearColor& OutColor);
};
