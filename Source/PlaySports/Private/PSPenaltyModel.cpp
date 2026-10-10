#include "PSPenaltyModel.h"
#include "PSDataIngestion.h"
#include "Misc/Paths.h"

FString UPSPenaltyModel::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/penalties.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSPenaltyModel::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSPenaltyTuning Loaded;
    if (!Ingestion || !Ingestion->LoadPenaltyTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPenaltyModel: Could not load %s; keeping the default flag rates."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPenaltyModel: %s: %s"), *JsonFilePath, *Problem);
    }
    return SetTuning(Loaded);
}

bool UPSPenaltyModel::SetTuning(const FPSPenaltyTuning& InTuning)
{
    if (ValidateTuning(InTuning).Num() > 0)
    {
        return false;
    }
    Tuning = InTuning;
    return true;
}

TArray<FString> UPSPenaltyModel::ValidateTuning(const FPSPenaltyTuning& Candidate)
{
    TArray<FString> Problems;
    if (Candidate.HoldingChancePerPlay < 0.f || Candidate.HoldingChancePerPlay > 1.f)
    {
        Problems.Add(FString::Printf(TEXT("HoldingChancePerPlay (%g) must be from 0 to 1"), Candidate.HoldingChancePerPlay));
    }
    if (Candidate.OffsidesChancePerSnap < 0.f || Candidate.OffsidesChancePerSnap > 1.f)
    {
        Problems.Add(FString::Printf(TEXT("OffsidesChancePerSnap (%g) must be from 0 to 1"), Candidate.OffsidesChancePerSnap));
    }
    return Problems;
}

void UPSPenaltyModel::Seed(int32 InSeed)
{
    Stream.Initialize(InSeed);
    bSeeded = true;
}

float UPSPenaltyModel::NextRoll()
{
    return bSeeded ? Stream.FRand() : FMath::FRand();
}

EPSPenaltyType UPSPenaltyModel::RollSnapFlag(bool bScrimmagePlay)
{
    if (NextRoll() < Tuning.OffsidesChancePerSnap)
    {
        return EPSPenaltyType::Offsides;
    }
    if (bScrimmagePlay && NextRoll() < Tuning.HoldingChancePerPlay)
    {
        return EPSPenaltyType::Holding;
    }
    return EPSPenaltyType::None;
}
