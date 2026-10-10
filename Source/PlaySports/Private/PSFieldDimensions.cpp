#include "PSFieldDimensions.h"
#include "PSDataIngestion.h"
#include "Misc/Paths.h"

FString PSField::GetDefaultDataPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/field_dimensions.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSFieldDimensions& PSField::GetDimensions()
{
    // Read once per run; a missing or unsound file leaves the defaults, which equal it.
    static FPSFieldDimensions Active;
    static bool bLoaded = false;
    if (!bLoaded)
    {
        bLoaded = true;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        FPSFieldDimensions Loaded;
        if (!Ingestion->LoadFieldDimensionsFromJson(GetDefaultDataPath(), Loaded))
        {
            UE_LOG(LogTemp, Warning, TEXT("PSField: Could not load %s; using the default field."), *GetDefaultDataPath());
        }
        else
        {
            const TArray<FString> Problems = Validate(Loaded);
            for (const FString& Problem : Problems)
            {
                UE_LOG(LogTemp, Warning, TEXT("PSField: %s"), *Problem);
            }
            if (Problems.Num() == 0)
            {
                Active = Loaded;
            }
        }
    }
    return Active;
}

TArray<FString> PSField::Validate(const FPSFieldDimensions& Dimensions)
{
    TArray<FString> Problems;
    if (Dimensions.CentimetresPerYard <= 0.f)
    {
        Problems.Add(TEXT("CentimetresPerYard must be above 0"));
    }
    if (Dimensions.FieldLengthYards <= 0.f)
    {
        Problems.Add(TEXT("FieldLengthYards must be above 0"));
    }
    if (Dimensions.EndZoneDepthYards <= 0.f)
    {
        Problems.Add(TEXT("EndZoneDepthYards must be above 0"));
    }
    if (Dimensions.FieldWidthYards <= 0.f)
    {
        Problems.Add(TEXT("FieldWidthYards must be above 0"));
    }
    if (Dimensions.OutOfBoundsDepthYards <= 0.f)
    {
        Problems.Add(TEXT("OutOfBoundsDepthYards must be above 0"));
    }
    if (Dimensions.BoundaryHeightCm <= 0.f)
    {
        Problems.Add(TEXT("BoundaryHeightCm must be above 0"));
    }
    return Problems;
}

float PSField::YardsToCentimetres(float Yards)
{
    return Yards * GetDimensions().CentimetresPerYard;
}

float PSField::CentimetresToYards(float Centimetres)
{
    return Centimetres / GetDimensions().CentimetresPerYard;
}

FVector PSField::YardLineToWorld(float YardLine, float LateralYards)
{
    return FVector(YardsToCentimetres(YardLine), YardsToCentimetres(LateralYards), 0.f);
}

float PSField::WorldToYardLine(const FVector& Location)
{
    return CentimetresToYards(Location.X);
}

int32 PSField::WorldToSpot(const FVector& Location)
{
    return FMath::Clamp(FMath::RoundToInt(WorldToYardLine(Location)), 0, FMath::RoundToInt(GetDimensions().FieldLengthYards));
}

float PSField::GoalLineX(bool bFar)
{
    return bFar ? YardsToCentimetres(GetDimensions().FieldLengthYards) : 0.f;
}

float PSField::MidfieldX()
{
    return YardsToCentimetres(GetDimensions().FieldLengthYards * 0.5f);
}

float PSField::EndLineX(bool bFar)
{
    const float Depth = YardsToCentimetres(GetDimensions().EndZoneDepthYards);
    return bFar ? GoalLineX(true) + Depth : GoalLineX(false) - Depth;
}

float PSField::SidelineY()
{
    return YardsToCentimetres(GetDimensions().FieldWidthYards * 0.5f);
}
