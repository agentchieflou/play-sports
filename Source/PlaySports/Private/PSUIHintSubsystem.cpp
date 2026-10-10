#include "PSUIHintSubsystem.h"
#include "PSDataIngestion.h"
#include "PSLocalization.h"
#include "PSSettingsSubsystem.h"
#include "PSSituationAI.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSUIHints
{
    // The fourth down, the one the hint is about.
    static const int32 FourthDown = 4;
}

UPSUIHintSubsystem::UPSUIHintSubsystem()
{
    HintsSettingId = TEXT("Hints");
}

void UPSUIHintSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>())
    {
        Bus->OnSnapMC.AddUObject(this, &UPSUIHintSubsystem::HandleSnap);
        BoundBus = Bus;
    }
}

void UPSUIHintSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSUIHintSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FString UPSUIHintSubsystem::GetDefaultCatalogPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/ui_hints.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSHintCatalog& UPSUIHintSubsystem::GetCatalog()
{
    if (!bCatalogLoaded)
    {
        LoadCatalogFromJson(GetDefaultCatalogPath());
    }
    return Catalog;
}

bool UPSUIHintSubsystem::LoadCatalogFromJson(const FString& JsonFilePath)
{
    bCatalogLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSHintCatalog Loaded;
    if (!Ingestion->LoadHintCatalogFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSUIHintSubsystem: Could not load %s; no hints."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateCatalog(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSUIHintSubsystem: %s"), *Problem);
    }
    Catalog = Loaded;
    return true;
}

TArray<FString> UPSUIHintSubsystem::ValidateCatalog(const FPSHintCatalog& InCatalog)
{
    TArray<FString> Problems;
    TSet<FName> Seen;
    for (int32 Index = 0; Index < InCatalog.Hints.Num(); ++Index)
    {
        const FPSHintDef& Hint = InCatalog.Hints[Index];
        bool bAlreadySeen = false;
        Seen.Add(Hint.HintId, &bAlreadySeen);
        if (Hint.HintId.IsNone() || bAlreadySeen)
        {
            Problems.Add(FString::Printf(TEXT("Hints[%d]: HintId is empty or used twice"), Index));
        }
        if (Hint.Text.IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("Hints[%d] '%s': no Text"), Index, *Hint.HintId.ToString()));
        }
    }
    return Problems;
}

UPSSettingsSubsystem* UPSUIHintSubsystem::GetSettings() const
{
    return SettingsOverride ? SettingsOverride : UPSSettingsSubsystem::Get(this);
}

bool UPSUIHintSubsystem::Applies(EPSHintTrigger Trigger, const FPSSituationContext& Situation, bool bOffense)
{
    switch (Trigger)
    {
    case EPSHintTrigger::OffenseCall:
        return bOffense && !Situation.bKickoff;
    case EPSHintTrigger::DefenseCall:
        return !bOffense && !Situation.bKickoff;
    case EPSHintTrigger::FourthDown:
        return bOffense && !Situation.bKickoff && Situation.Down == PSUIHints::FourthDown;
    case EPSHintTrigger::TwoMinuteDrill:
    {
        if (!bOffense || Situation.bKickoff)
        {
            return false;
        }
        // The situation AI decides what a two-minute drill is (Epic 76).
        if (!SituationAI)
        {
            SituationAI = NewObject<UPSSituationAI>(this);
            SituationAI->LoadTuningFromJson(UPSSituationAI::GetDefaultTuningPath());
        }
        return SituationAI->ClassifySituation(Situation) == EPSGameSituation::TwoMinuteDrill;
    }
    case EPSHintTrigger::Kickoff:
        return bOffense && Situation.bKickoff;
    default:
        return false;
    }
}

FName UPSUIHintSubsystem::PickHint(const FPSSituationContext& Situation, bool bOffense)
{
    UPSSettingsSubsystem* Settings = GetSettings();
    const bool bHintsOn = !Settings || !Settings->GetCatalog().FindSetting(HintsSettingId) || Settings->GetBool(HintsSettingId);
    if (!bHintsOn)
    {
        return NAME_None;
    }
    for (const FPSHintDef& Hint : GetCatalog().Hints)
    {
        if ((!Settings || !Settings->HasSeenHint(Hint.HintId)) && Applies(Hint.Trigger, Situation, bOffense))
        {
            return Hint.HintId;
        }
    }
    return NAME_None;
}

FText UPSUIHintSubsystem::GetCallHint(const FPSSituationContext& Situation, bool bOffense)
{
    bool& bPicked = bOffense ? bOffensePicked : bDefensePicked;
    FName& HintId = bOffense ? OffenseHintId : DefenseHintId;
    if (!bPicked)
    {
        bPicked = true;
        HintId = PickHint(Situation, bOffense);
        if (!HintId.IsNone())
        {
            if (UPSSettingsSubsystem* Settings = GetSettings())
            {
                Settings->MarkHintSeen(HintId);
            }
            OnHintShownMC.Broadcast(HintId);
        }
    }
    const FName Shown = HintId;
    const FPSHintDef* Hint = Shown.IsNone() ? nullptr
        : GetCatalog().Hints.FindByPredicate([Shown](const FPSHintDef& Def) { return Def.HintId == Shown; });
    return Hint ? UPSLocalization::GetDataText(UPSLocalization::HintKey(Hint->HintId), Hint->Text) : FText::GetEmpty();
}

void UPSUIHintSubsystem::ResetCallWindow()
{
    OffenseHintId = NAME_None;
    DefenseHintId = NAME_None;
    bOffensePicked = false;
    bDefensePicked = false;
}

void UPSUIHintSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    ResetCallWindow();
}
