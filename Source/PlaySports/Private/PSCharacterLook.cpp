// PSCharacterLook.cpp - Epic 147.2: what a player looks like on the field, in the colours of the team he plays for
#include "PSCharacterLook.h"
#include "PSDataIngestion.h"
#include "PSLeagueData.h"
#include "PSMatchSetup.h"
#include "PSPlayerPawn.h"
#include "PSTelemetryBus.h"
#include "PSUITeamCatalog.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

namespace PSCharacterLookPrivate
{
    FLinearColor ParseOr(const FString& Hex, const FLinearColor& Fallback)
    {
        FLinearColor Parsed;
        return UPSUITeamCatalog::ParseHexColor(Hex, Parsed) ? Parsed : Fallback;
    }

    /** The team identity data's colours, read once per file: they don't change during a run. */
    struct FTeamColors
    {
        FLinearColor Primary = FLinearColor::Gray;
        FLinearColor Secondary = FLinearColor::Gray;
    };

    const TMap<FName, FTeamColors>& TeamColorsIn(const FString& TeamsJsonPath)
    {
        static TMap<FString, TMap<FName, FTeamColors>> ByFile;
        if (const TMap<FName, FTeamColors>* Known = ByFile.Find(TeamsJsonPath))
        {
            return *Known;
        }
        TMap<FName, FTeamColors>& Colors = ByFile.Add(TeamsJsonPath);
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        UDataTable* Teams = NewObject<UDataTable>();
        Teams->RowStruct = FPSTeamInfo::StaticStruct();
        if (Ingestion->LoadTeamsFromJson(TeamsJsonPath, Teams))
        {
            TArray<FPSTeamInfo*> Rows;
            Teams->GetAllRows<FPSTeamInfo>(TEXT("UPSCharacterLookComponent"), Rows);
            for (const FPSTeamInfo* Row : Rows)
            {
                FLinearColor Primary;
                FLinearColor Secondary;
                if (Row && UPSUITeamCatalog::ParseHexColor(Row->PrimaryColor, Primary) && UPSUITeamCatalog::ParseHexColor(Row->SecondaryColor, Secondary))
                {
                    FTeamColors& Entry = Colors.Add(Row->TeamId);
                    Entry.Primary = Primary;
                    Entry.Secondary = Secondary;
                }
            }
        }
        return Colors;
    }
}

UPSCharacterLookComponent::UPSCharacterLookComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FString UPSCharacterLookComponent::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/character_look.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

FPSCharacterLookStyle UPSCharacterLookComponent::LoadStyle(const FString& Path)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSCharacterLookStyle Loaded;
    if (!Ingestion->LoadCharacterLookStyleFromJson(Path, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCharacterLookComponent: Could not load %s; using the default look."), *Path);
        return FPSCharacterLookStyle();
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCharacterLookComponent: %s: %s"), *Path, *Problem);
    }
    return Problems.Num() == 0 ? Loaded : FPSCharacterLookStyle();
}

TArray<FString> UPSCharacterLookComponent::ValidateStyle(const FPSCharacterLookStyle& InStyle)
{
    TArray<FString> Problems;
    if (!InStyle.SkeletalMeshPath.IsEmpty() && !InStyle.SkeletalMeshPath.StartsWith(TEXT("/")))
    {
        Problems.Add(TEXT("SkeletalMeshPath must be empty or an asset path"));
    }
    if (InStyle.FallbackMeshPath.IsEmpty() || InStyle.FallbackMaterialPath.IsEmpty())
    {
        Problems.Add(TEXT("FallbackMeshPath and FallbackMaterialPath must name assets"));
    }
    if (InStyle.TeamColorParameter.IsNone() || InStyle.FallbackColorParameter.IsNone())
    {
        Problems.Add(TEXT("TeamColorParameter and FallbackColorParameter must name colour parameters"));
    }
    if (!(InStyle.FallbackMeshSizeCm > 0.f))
    {
        Problems.Add(TEXT("FallbackMeshSizeCm must be above 0"));
    }
    FLinearColor Parsed;
    if (!UPSUITeamCatalog::ParseHexColor(InStyle.NeutralColor, Parsed))
    {
        Problems.Add(FString::Printf(TEXT("NeutralColor '%s' is not #RRGGBB"), *InStyle.NeutralColor));
    }
    return Problems;
}

bool UPSCharacterLookComponent::FindTeamColors(const FString& TeamsJsonPath, FName InTeamId, FLinearColor& OutPrimary, FLinearColor& OutSecondary)
{
    const PSCharacterLookPrivate::FTeamColors* Found = PSCharacterLookPrivate::TeamColorsIn(TeamsJsonPath).Find(InTeamId);
    if (!Found)
    {
        return false;
    }
    OutPrimary = Found->Primary;
    OutSecondary = Found->Secondary;
    return true;
}

void UPSCharacterLookComponent::BeginPlay()
{
    Super::BeginPlay();
    Style = LoadStyle(GetDefaultStylePath());
    ApplyLook();
    if (UWorld* World = GetWorld())
    {
        BindToBus(World->GetSubsystem<UPSTelemetryBus>());
    }
}

void UPSCharacterLookComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromBus();
    Super::EndPlay(EndPlayReason);
}

bool UPSCharacterLookComponent::ApplyLook()
{
    APSPlayerPawn* Pawn = Cast<APSPlayerPawn>(GetOwner());
    UStaticMeshComponent* Body = Pawn ? Pawn->GetBodyMesh() : nullptr;
    const UCapsuleComponent* Capsule = Pawn ? Pawn->FindComponentByClass<UCapsuleComponent>() : nullptr;
    if (!Pawn || !Body || !Capsule)
    {
        return false;
    }
    PrimaryMaterials.Reset();
    SecondaryMaterials.Reset();

    // The character, when the data names one that loads.
    USkeletalMesh* Character = Style.SkeletalMeshPath.IsEmpty() ? nullptr : Cast<USkeletalMesh>(FSoftObjectPath(Style.SkeletalMeshPath).TryLoad());
    if (!Character && !Style.SkeletalMeshPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCharacterLookComponent: Could not load %s; dressing the fallback body."), *Style.SkeletalMeshPath);
    }
    if (Character)
    {
        if (!CharacterMesh)
        {
            CharacterMesh = NewObject<USkeletalMeshComponent>(Pawn, TEXT("CharacterMesh"));
            CharacterMesh->SetupAttachment(Pawn->GetRootComponent());
            CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            CharacterMesh->SetGenerateOverlapEvents(false);
            CharacterMesh->SetCanEverAffectNavigation(false);
            CharacterMesh->RegisterComponent();
        }
        CharacterMesh->SetSkeletalMeshAsset(Character);
        // His feet on the ground: the bottom of the capsule.
        CharacterMesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -Capsule->GetScaledCapsuleHalfHeight()), FRotator(0.f, Style.MeshYawDegrees, 0.f));
        const TArray<FName> Slots = CharacterMesh->GetMaterialSlotNames();
        for (int32 Index = 0; Index < Slots.Num(); ++Index)
        {
            const bool bPrimary = Style.PrimarySlots.Contains(Slots[Index]);
            if (bPrimary || Style.SecondarySlots.Contains(Slots[Index]))
            {
                UMaterialInstanceDynamic* Instance = CharacterMesh->CreateDynamicMaterialInstance(Index);
                (bPrimary ? PrimaryMaterials : SecondaryMaterials).Add(Instance);
            }
        }
        Body->SetVisibility(false);
        RefreshColors();
        return true;
    }

    // The fallback body: the capsule's size, in the team's primary colour.
    UStaticMesh* Shape = Cast<UStaticMesh>(FSoftObjectPath(Style.FallbackMeshPath).TryLoad());
    UMaterialInterface* Material = Cast<UMaterialInterface>(FSoftObjectPath(Style.FallbackMaterialPath).TryLoad());
    if (!Shape || !Material)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCharacterLookComponent: Could not load the fallback body '%s' or its material '%s'."), *Style.FallbackMeshPath, *Style.FallbackMaterialPath);
        return false;
    }
    if (CharacterMesh)
    {
        CharacterMesh->DestroyComponent();
        CharacterMesh = nullptr;
    }
    const float MeshSize = FMath::Max(Style.FallbackMeshSizeCm, 1.f);
    const float Across = 2.f * Capsule->GetScaledCapsuleRadius() / MeshSize;
    Body->SetStaticMesh(Shape);
    Body->SetRelativeLocation(FVector::ZeroVector);
    Body->SetRelativeScale3D(FVector(Across, Across, 2.f * Capsule->GetScaledCapsuleHalfHeight() / MeshSize));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetVisibility(true);
    UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
    Body->SetMaterial(0, Instance);
    PrimaryMaterials.Add(Instance);
    RefreshColors();
    return true;
}

void UPSCharacterLookComponent::SetMatchSetup(const UPSMatchSetup* InMatchSetup)
{
    MatchSetup = InMatchSetup;
    RefreshTeam();
}

void UPSCharacterLookComponent::BindToBus(UPSTelemetryBus* Bus)
{
    UnbindFromBus();
    if (!Bus)
    {
        return;
    }
    BoundBus = Bus;
    GameStateHandle = Bus->OnGameStateMC.AddUObject(this, &UPSCharacterLookComponent::HandleGameState);
    LineupHandle = Bus->OnLineupMC.AddUObject(this, &UPSCharacterLookComponent::HandleLineup);
}

void UPSCharacterLookComponent::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnGameStateMC.Remove(GameStateHandle);
        Bus->OnLineupMC.Remove(LineupHandle);
    }
    GameStateHandle.Reset();
    LineupHandle.Reset();
    BoundBus.Reset();
}

void UPSCharacterLookComponent::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    // Who has the ball now; the new team comes on at the next lineup.
    bHomeHasPossession = Event.bHomeHasPossession;
}

void UPSCharacterLookComponent::HandleLineup(const FPSTelemetryLineupEvent& Event)
{
    RefreshTeam();
}

void UPSCharacterLookComponent::RefreshTeam()
{
    const UPSMatchSetup* Match = MatchSetup.Get();
    const APSPlayerPawn* Pawn = Cast<APSPlayerPawn>(GetOwner());
    if (!Match || !Match->HasTeams() || !Pawn)
    {
        TeamId = NAME_None;
        PrimaryColor = SecondaryColor = PSCharacterLookPrivate::ParseOr(Style.NeutralColor, FLinearColor::Gray);
        RefreshColors();
        return;
    }
    // The team with the ball plays offense.
    const bool bHome = (Pawn->TeamSide == EPSTeamSide::Offense) == bHomeHasPossession;
    TeamId = Match->GetTeamId(bHome);
    if (!FindTeamColors(UPSUITeamCatalog::GetDefaultTeamsPath(), TeamId, PrimaryColor, SecondaryColor))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCharacterLookComponent: %s has no colours in the team data."), *TeamId.ToString());
        PrimaryColor = SecondaryColor = PSCharacterLookPrivate::ParseOr(Style.NeutralColor, FLinearColor::Gray);
    }
    RefreshColors();
}

void UPSCharacterLookComponent::RefreshColors()
{
    if (TeamId.IsNone())
    {
        PrimaryColor = SecondaryColor = PSCharacterLookPrivate::ParseOr(Style.NeutralColor, FLinearColor::Gray);
    }
    const FName Parameter = CharacterMesh ? Style.TeamColorParameter : Style.FallbackColorParameter;
    for (UMaterialInstanceDynamic* Instance : PrimaryMaterials)
    {
        if (Instance)
        {
            Instance->SetVectorParameterValue(Parameter, PrimaryColor);
        }
    }
    for (UMaterialInstanceDynamic* Instance : SecondaryMaterials)
    {
        if (Instance)
        {
            Instance->SetVectorParameterValue(Parameter, SecondaryColor);
        }
    }
}
