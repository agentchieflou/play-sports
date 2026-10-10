// PSOverlayPersonnelTypes.h - Epic 29: the personnel panels' style and what they show
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSOverlayPersonnelTypes.generated.h"

/** A role a panel counts, and how it is written ("RB"). */
USTRUCT(BlueprintType)
struct FPSPersonnelRoleLabel
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    EPlayerRole Role = EPlayerRole::RunningBack;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString Label;
};

/** A defensive package's name by how many defensive backs it fields ("Nickel" for 5). */
USTRUCT(BlueprintType)
struct FPSDefenseName
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    int32 DefensiveBacks = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString Name;
};

/**
 * The personnel panels' look and naming rules (Data/personnel_panel.json; Architecture rule 4).
 * A package the personnel catalog (Data/personnel_packages.json) lists by its exact role counts
 * goes by the catalog's name; any other is named by the rules here. In a format, "{Label}"
 * stands for the count of the role with that label: "{RB}{TE} Personnel" reads "13 Personnel".
 */
USTRUCT(BlueprintType)
struct FPSPersonnelPanelStyle
{
    GENERATED_BODY()

    /** The roles the offense panel counts, in order ("RB 1 | TE 3 | WR 1"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    TArray<FPSPersonnelRoleLabel> OffenseRoles;

    /** ... and the defense panel ("DL 3 | LB 4 | DB 4"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    TArray<FPSPersonnelRoleLabel> DefenseRoles;

    /** An offensive package the catalog doesn't list. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString OffenseNameFormat = TEXT("{RB}{TE} Personnel");

    /** A defensive package the catalog doesn't list, by its defensive backs ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    TArray<FPSDefenseName> DefenseNames;

    /** ... or, with a count none of those names, this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString DefenseNameFallback = TEXT("{DL}-{LB}-{DB}");

    /** The panel's fill, "#RRGGBB". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString PanelColor = TEXT("#1B1B1B");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString TextColor = TEXT("#FFFFFF");

    /** What a panel and a changed count flash toward on a substitution. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    FString FlashColor = TEXT("#F2C94C");

    /** The counts' type size. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    int32 FontSize = 16;

    /** The package name's type size. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    int32 TitleFontSize = 20;

    /** How long a substitution's flash takes to fade, on a Full tier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    float ChangeFlashSeconds = 1.2f;

    /** Keep the panels up during the play (false: before the snap only, as broadcasts do). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
    bool bShowInPlay = false;
};

/** One count on a panel. */
USTRUCT(BlueprintType)
struct FPSPersonnelRoleCount
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    EPlayerRole Role = EPlayerRole::RunningBack;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FString Label;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    int32 Count = 0;

    /** Changed by the latest substitution; flashes with the panel. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bChanged = false;
};

/** One side's panel. */
USTRUCT(BlueprintType)
struct FPSPersonnelPanel
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bOffense = true;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    bool bVisible = false;

    /** "11 Personnel", "Nickel", ... */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FString PackageName;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    TArray<FPSPersonnelRoleCount> Counts;

    /** "RB 1 | TE 3 | WR 1" */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FString CountsText;

    /** The side's team, as the score bug shows it. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FString TeamLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    FLinearColor TeamColor = FLinearColor::White;

    /** 1 at a substitution, fading to 0 over ChangeFlashSeconds; always 0 below a Full tier. */
    UPROPERTY(BlueprintReadOnly, Category = "Overlay")
    float FlashAlpha = 0.f;
};
