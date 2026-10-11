// PSBallLook.cpp - Epic 147.4: what the ball looks like, from Data/ball_look.json
#include "PSBallLook.h"
#include "PSDataIngestion.h"
#include "PSUITeamCatalog.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

FString PSBallLook::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/ball_look.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

FPSBallLookStyle PSBallLook::LoadStyle(const FString& Path)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSBallLookStyle Loaded;
    if (!Ingestion->LoadBallLookStyleFromJson(Path, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("PSBallLook: Could not load %s; using the default look."), *Path);
        return FPSBallLookStyle();
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("PSBallLook: %s: %s"), *Path, *Problem);
    }
    return Problems.Num() == 0 ? Loaded : FPSBallLookStyle();
}

TArray<FString> PSBallLook::ValidateStyle(const FPSBallLookStyle& Style)
{
    TArray<FString> Problems;
    if (Style.MeshPath.IsEmpty() || Style.MaterialPath.IsEmpty())
    {
        Problems.Add(TEXT("MeshPath and MaterialPath must name assets"));
    }
    if (Style.ColorParameter.IsNone())
    {
        Problems.Add(TEXT("ColorParameter must name the material's color parameter"));
    }
    FLinearColor Parsed;
    if (!UPSUITeamCatalog::ParseHexColor(Style.BallColor, Parsed))
    {
        Problems.Add(FString::Printf(TEXT("BallColor '%s' is not #RRGGBB"), *Style.BallColor));
    }
    for (const TPair<const TCHAR*, float>& Positive : {
        TPair<const TCHAR*, float>(TEXT("MeshSizeCm"), Style.MeshSizeCm),
        TPair<const TCHAR*, float>(TEXT("LengthCm"), Style.LengthCm),
        TPair<const TCHAR*, float>(TEXT("WidthCm"), Style.WidthCm) })
    {
        if (!(Positive.Value > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be above 0"), Positive.Key));
        }
    }
    return Problems;
}

bool PSBallLook::Apply(UStaticMeshComponent* Mesh, const FPSBallLookStyle& Style, UObject* Outer)
{
    if (!Mesh)
    {
        return false;
    }
    UStaticMesh* Shape = Style.MeshPath.IsEmpty() ? nullptr : Cast<UStaticMesh>(FSoftObjectPath(Style.MeshPath).TryLoad());
    UMaterialInterface* Material = Style.MaterialPath.IsEmpty() ? nullptr : Cast<UMaterialInterface>(FSoftObjectPath(Style.MaterialPath).TryLoad());
    if (!Shape || !Material)
    {
        UE_LOG(LogTemp, Warning, TEXT("PSBallLook: Could not load the ball's mesh '%s' or material '%s'."), *Style.MeshPath, *Style.MaterialPath);
        return false;
    }

    const float MeshSize = FMath::Max(Style.MeshSizeCm, 1.f);
    Mesh->SetStaticMesh(Shape);
    Mesh->SetRelativeScale3D(FVector(Style.LengthCm / MeshSize, Style.WidthCm / MeshSize, Style.WidthCm / MeshSize));
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    FLinearColor Color = FLinearColor::White;
    UPSUITeamCatalog::ParseHexColor(Style.BallColor, Color);
    UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, Outer ? Outer : Mesh);
    Instance->SetVectorParameterValue(Style.ColorParameter, Color);
    Mesh->SetMaterial(0, Instance);
    return true;
}
