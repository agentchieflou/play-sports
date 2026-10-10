#include "PSDifficultySubsystem.h"
#include "PSAIFieldSnapshot.h"
#include "PSCarrierMoveComponent.h"
#include "PSDataIngestion.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerDNA.h"
#include "PSPlayerPawn.h"
#include "PSSettingsSubsystem.h"
#include "PSUIAccessibilitySubsystem.h"
#include "PSUIColorAccessibility.h"
#include "PSUITeamCatalog.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

int32 PSDifficulty::ApplyScales(const FPSDifficultyTier& Tier, FName Target, const UScriptStruct* Struct, void* Data)
{
    if (!Struct || !Data)
    {
        return 0;
    }
    int32 Applied = 0;
    for (const FPSDifficultyScale& Scale : Tier.Scales)
    {
        FFloatProperty* Property = Scale.Target == Target ? FindFProperty<FFloatProperty>(Struct, Scale.Field) : nullptr;
        if (!Property || Scale.Scale <= 0.f)
        {
            continue;
        }
        float* Field = Property->ContainerPtrToValuePtr<float>(Data);
        *Field *= Scale.Scale;
        ++Applied;
    }
    return Applied;
}

TArray<FString> PSDifficulty::ValidateCatalog(const FPSDifficultyCatalog& Catalog)
{
    TArray<FString> Problems;
    if (Catalog.DifficultyTiers.Num() == 0)
    {
        Problems.Add(TEXT("DifficultyTiers: none"));
    }
    TSet<FName> Ids;
    for (int32 Index = 0; Index < Catalog.DifficultyTiers.Num(); ++Index)
    {
        const FPSDifficultyTier& Tier = Catalog.DifficultyTiers[Index];
        const FString Where = FString::Printf(TEXT("DifficultyTiers[%d] '%s'"), Index, *Tier.TierId.ToString());
        if (Tier.TierId.IsNone() || Tier.Label.IsEmpty() || Ids.Contains(Tier.TierId))
        {
            Problems.Add(FString::Printf(TEXT("%s: needs an ID used once and a label"), *Where));
        }
        Ids.Add(Tier.TierId);
        if (Tier.AdaptationDial < 0.f || Tier.AdaptationDial > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("%s: AdaptationDial runs 0 to 1"), *Where));
        }
        if (Tier.ThrowScatterScale <= 0.f)
        {
            Problems.Add(FString::Printf(TEXT("%s: ThrowScatterScale must be above 0"), *Where));
        }
        TSet<FString> Fields;
        for (const FPSDifficultyScale& Scale : Tier.Scales)
        {
            const FString Field = FString::Printf(TEXT("%s.%s"), *Scale.Target.ToString(), *Scale.Field.ToString());
            const UScriptStruct* Struct = PSPlayerDNA::FindTargetStruct(Scale.Target);
            if (!Struct)
            {
                Problems.Add(FString::Printf(TEXT("%s %s: unknown target (SkillAI, Pocket, DefenderAI, RouteRunning or Recognition)"), *Where, *Field));
            }
            else if (!FindFProperty<FFloatProperty>(Struct, Scale.Field))
            {
                Problems.Add(FString::Printf(TEXT("%s %s: not a float field of %s"), *Where, *Field, *Struct->GetName()));
            }
            if (Fields.Contains(Field))
            {
                Problems.Add(FString::Printf(TEXT("%s %s: scaled twice"), *Where, *Field));
            }
            Fields.Add(Field);
            if (Scale.Scale <= 0.f)
            {
                Problems.Add(FString::Printf(TEXT("%s %s: Scale must be above 0"), *Where, *Field));
            }
        }
    }
    FLinearColor Accent;
    if (!UPSUITeamCatalog::ParseHexColor(Catalog.SuggestedPlayAccent, Accent))
    {
        Problems.Add(TEXT("SuggestedPlayAccent: not \"#RRGGBB\""));
    }
    return Problems;
}

void UPSDifficultySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    BindToBus(Collection.InitializeDependency<UPSTelemetryBus>());
}

void UPSDifficultySubsystem::Deinitialize()
{
    UnbindFromBus();
    Super::Deinitialize();
}

bool UPSDifficultySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPSDifficultySubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSDifficultySubsystem, STATGROUP_Tickables);
}

void UPSDifficultySubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    UpdateAutoSlide();
}

FString UPSDifficultySubsystem::GetDefaultCatalogPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/difficulty.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

UPSDifficultySubsystem* UPSDifficultySubsystem::Get(const UWorld* World)
{
    return World ? World->GetSubsystem<UPSDifficultySubsystem>() : nullptr;
}

const FPSDifficultyCatalog& UPSDifficultySubsystem::GetCatalog()
{
    if (!bCatalogLoaded)
    {
        LoadCatalogFromJson(GetDefaultCatalogPath());
    }
    return Catalog;
}

bool UPSDifficultySubsystem::LoadCatalogFromJson(const FString& JsonFilePath)
{
    bCatalogLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSDifficultyCatalog Loaded;
    if (!Ingestion->LoadDifficultyCatalogFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDifficultySubsystem: Could not load the difficulty tiers from %s; the AI plays as tuned."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : PSDifficulty::ValidateCatalog(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDifficultySubsystem: %s: %s"), *JsonFilePath, *Problem);
    }
    Catalog = Loaded;
    return true;
}

void UPSDifficultySubsystem::SetCatalog(const FPSDifficultyCatalog& InCatalog)
{
    Catalog = InCatalog;
    bCatalogLoaded = true;
}

UPSSettingsSubsystem* UPSDifficultySubsystem::GetSettings() const
{
    return SettingsOverride ? SettingsOverride.Get() : UPSSettingsSubsystem::Get(GetWorld());
}

void UPSDifficultySubsystem::SetSettings(UPSSettingsSubsystem* InSettings)
{
    SettingsOverride = InSettings;
}

const FPSDifficultyTier* UPSDifficultySubsystem::GetActiveTier()
{
    UPSSettingsSubsystem* Settings = GetSettings();
    const FName SettingId = GetCatalog().DifficultySetting;
    if (!Settings || !Settings->GetCatalog().FindSetting(SettingId))
    {
        return nullptr;
    }
    const int32 Index = FMath::RoundToInt(Settings->GetValue(SettingId));
    return Catalog.DifficultyTiers.IsValidIndex(Index) ? &Catalog.DifficultyTiers[Index] : nullptr;
}

bool UPSDifficultySubsystem::IsCpuPlayer(const APSPlayerPawn* Player) const
{
    if (!Player)
    {
        return false;
    }
    const UWorld* World = GetWorld();
    const UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    return !PlayCall || !PlayCall->IsHumanSide(Player->TeamSide == EPSTeamSide::Offense);
}

float UPSDifficultySubsystem::GetThrowScatterScale(const APSPlayerPawn* Passer)
{
    const FPSDifficultyTier* Tier = IsCpuPlayer(Passer) ? GetActiveTier() : nullptr;
    return Tier && Tier->ThrowScatterScale > 0.f ? Tier->ThrowScatterScale : 1.f;
}

float UPSDifficultySubsystem::GetAdaptationDial()
{
    const FPSDifficultyTier* Tier = GetActiveTier();
    return Tier ? FMath::Clamp(Tier->AdaptationDial, 0.f, 1.f) : -1.f;
}

bool UPSDifficultySubsystem::IsSettingOn(FName SettingId)
{
    if (UPSSettingsSubsystem* Settings = GetSettings())
    {
        return Settings->GetBool(SettingId);
    }
    if (!bDefaultSettingsLoaded)
    {
        bDefaultSettingsLoaded = true;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
        Ingestion->LoadSettingsCatalogFromJson(UPSSettingsSubsystem::GetDefaultCatalogPath(), DefaultSettings);
    }
    const FPSSettingDef* Def = DefaultSettings.FindSetting(SettingId);
    return Def && Def->Default >= 0.5f;
}

bool UPSDifficultySubsystem::IsPassLeadOn()
{
    return IsSettingOn(GetCatalog().PassLeadSetting);
}

bool UPSDifficultySubsystem::IsAutoSlideOn()
{
    return IsSettingOn(GetCatalog().AutoSlideSetting);
}

bool UPSDifficultySubsystem::IsSuggestedPlayHighlightOn()
{
    return IsSettingOn(GetCatalog().SuggestedPlaySetting);
}

FLinearColor UPSDifficultySubsystem::GetSuggestedPlayAccent()
{
    FLinearColor Accent = FLinearColor::Transparent;
    if (!IsSuggestedPlayHighlightOn() || !UPSUITeamCatalog::ParseHexColor(GetCatalog().SuggestedPlayAccent, Accent))
    {
        return FLinearColor::Transparent;
    }
    return UPSUIColorLibrary::ResolveColor(Accent, UPSUIAccessibilitySubsystem::GetColorblindMode(GetSettings()));
}

const FPocketTuningRow& UPSDifficultySubsystem::GetPocketTuning()
{
    if (!bPocketTuningLoaded)
    {
        bPocketTuningLoaded = true;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
        FPocketTuningRow Loaded;
        if (Ingestion->LoadPocketTuningFromJson(UPSPocketComponent::GetDefaultTuningPath(), Loaded))
        {
            PocketTuning = Loaded;
        }
    }
    return PocketTuning;
}

bool UPSDifficultySubsystem::UpdateAutoSlide()
{
    UWorld* World = GetWorld();
    if (!bPlayLive || !World || !IsAutoSlideOn())
    {
        return false;
    }
    const TArray<APSPlayerPawn*>& Pawns = UPSAIFieldSnapshot::GetFieldPawns(World);
    for (APSPlayerPawn* Pawn : Pawns)
    {
        if (!Pawn || !Pawn->IsUserControlled() || !Pawn->HasPossession() || Pawn->GetAttributes().Role != EPlayerRole::Quarterback)
        {
            continue;
        }
        UPSCarrierMoveComponent* Moves = Pawn->GetCarrierMoveComponent();
        const APSPlayerPawn* Tackler = Moves && !Moves->HasGivenUp() ? UPSPocketComponent::FindSlideThreat(GetPocketTuning(), Pawn, Pawns, LineOfScrimmage) : nullptr;
        if (!Tackler || !Moves->TryMove(EPSCarrierMove::Slide, FVector2D::ZeroVector))
        {
            continue;
        }
        // The same Pocket event the CPU's quarterback slides with, so the slide is on the record.
        if (UPSTelemetryBus* Bus = BoundBus.Get())
        {
            FPSTelemetryPocketEvent Event;
            Event.Kind = EPSPocketEventKind::Slide;
            Event.PasserName = Pawn->GetAttributes().DisplayName;
            Event.DefenderName = Tackler->GetAttributes().DisplayName;
            Event.Location = Pawn->GetActorLocation();
            Event.bSuccess = true;
            Bus->PublishPocket(Event);
        }
        return true;
    }
    return false;
}

void UPSDifficultySubsystem::BindToBus(UPSTelemetryBus* Bus)
{
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnSnapMC.AddUObject(this, &UPSDifficultySubsystem::HandleSnap);
    Bus->OnPhaseChangeMC.AddUObject(this, &UPSDifficultySubsystem::HandlePhaseChange);
    BoundBus = Bus;
}

void UPSDifficultySubsystem::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSDifficultySubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    bPlayLive = true;
    LineOfScrimmage = Event.LineOfScrimmage;
}

void UPSDifficultySubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") || Event.NewPhase == TEXT("PreSnap"))
    {
        bPlayLive = false;
    }
}
