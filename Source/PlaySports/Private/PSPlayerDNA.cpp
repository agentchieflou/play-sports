#include "PSPlayerDNA.h"
#include "PSDataIngestion.h"
#include "PSDefenderAIComponent.h"
#include "PSLocalization.h"
#include "PSPocketComponent.h"
#include "PSRouteRunning.h"
#include "PSSkillPlayerAIComponent.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

float PSPlayerDNA::GetAxis(const FPSPlayerDNA& DNA, FName Axis)
{
    const FFloatProperty* Property = FindFProperty<FFloatProperty>(FPSPlayerDNA::StaticStruct(), Axis);
    return Property ? FMath::Clamp(Property->GetPropertyValue_InContainer(&DNA), -1.f, 1.f) : 0.f;
}

bool PSPlayerDNA::IsAxis(FName Axis)
{
    return !Axis.IsNone() && FindFProperty<FFloatProperty>(FPSPlayerDNA::StaticStruct(), Axis) != nullptr;
}

const FPSDNAAxisDef* PSPlayerDNA::FindAxis(const FPSPlayerDNACatalog& Catalog, FName Axis)
{
    return Catalog.Axes.FindByPredicate([Axis](const FPSDNAAxisDef& Def) { return Def.Axis == Axis; });
}

float PSPlayerDNA::GetEffectiveAxis(const FPSPlayerDNACatalog& Catalog, const FPlayerAttributes& Player, FName Axis)
{
    const FPSDNAAxisDef* Def = FindAxis(Catalog, Axis);
    return Def && Def->Roles.Contains(Player.Role) ? GetAxis(Player.DNA, Axis) : 0.f;
}

float PSPlayerDNA::BindingScale(const FPSDNABinding& Binding, float AxisValue)
{
    const float Value = FMath::Clamp(AxisValue, -1.f, 1.f);
    return Value >= 0.f ? FMath::Lerp(1.f, Binding.AtHigh, Value) : FMath::Lerp(1.f, Binding.AtLow, -Value);
}

const UScriptStruct* PSPlayerDNA::FindTargetStruct(FName Target)
{
    if (Target == TEXT("SkillAI"))
    {
        return FSkillPlayerAITuningRow::StaticStruct();
    }
    if (Target == TEXT("Pocket"))
    {
        return FPocketTuningRow::StaticStruct();
    }
    if (Target == TEXT("DefenderAI"))
    {
        return FDefenderAITuningRow::StaticStruct();
    }
    if (Target == TEXT("RouteRunning"))
    {
        return FRouteRunningTuningRow::StaticStruct();
    }
    return nullptr;
}

int32 PSPlayerDNA::ApplyBindings(const FPSPlayerDNACatalog& Catalog, const FPlayerAttributes& Player, FName Target, const UScriptStruct* Struct, void* Data)
{
    if (!Struct || !Data)
    {
        return 0;
    }
    int32 Applied = 0;
    for (const FPSDNABinding& Binding : Catalog.Bindings)
    {
        if (Binding.Target != Target)
        {
            continue;
        }
        const float Value = GetEffectiveAxis(Catalog, Player, Binding.Axis);
        FFloatProperty* Property = FindFProperty<FFloatProperty>(Struct, Binding.Field);
        if (Value == 0.f || !Property)
        {
            continue;
        }
        float* Field = Property->ContainerPtrToValuePtr<float>(Data);
        *Field *= BindingScale(Binding, Value);
        ++Applied;
    }
    return Applied;
}

float PSPlayerDNA::RushMoveScale(const FPSPlayerDNACatalog& Catalog, const FPlayerAttributes& Rusher, EPSRushMove Move)
{
    const FPSDNARushMoveLean* Lean = Catalog.RushMoveLeans.FindByPredicate([Move](const FPSDNARushMoveLean& Entry) { return Entry.Move == Move; });
    if (!Lean)
    {
        return 1.f;
    }
    const float Style = GetEffectiveAxis(Catalog, Rusher, GET_MEMBER_NAME_CHECKED(FPSPlayerDNA, RushPower));
    return FMath::Max(0.f, 1.f + Catalog.RushStyleWeight * Style * FMath::Clamp(Lean->Lean, -1.f, 1.f));
}

FString PSPlayerDNA::TraitKey(FName TraitId, const FString& Field)
{
    return FString::Printf(TEXT("Trait.%s.%s"), *TraitId.ToString(), *Field);
}

TArray<FPSScoutingTrait> PSPlayerDNA::GetScoutingTraits(const FPSPlayerDNACatalog& Catalog, const FPlayerAttributes& Player)
{
    TArray<FPSScoutingTrait> Traits;
    const float Threshold = FMath::Max(Catalog.TraitThreshold, KINDA_SMALL_NUMBER);
    for (const FPSDNAAxisDef& Def : Catalog.Axes)
    {
        if (!Def.Roles.Contains(Player.Role))
        {
            continue;
        }
        const float Value = GetAxis(Player.DNA, Def.Axis);
        if (FMath::Abs(Value) < Threshold)
        {
            continue;
        }
        const bool bHigh = Value > 0.f;
        FPSScoutingTrait Trait;
        Trait.Axis = Def.Axis;
        Trait.TraitId = bHigh ? Def.HighTrait : Def.LowTrait;
        Trait.Strength = FMath::Abs(Value);
        Trait.Label = UPSLocalization::GetDataText(TraitKey(Trait.TraitId, TEXT("Label")), bHigh ? Def.HighLabel : Def.LowLabel);
        Trait.Description = UPSLocalization::GetDataText(TraitKey(Trait.TraitId, TEXT("Description")), bHigh ? Def.HighDescription : Def.LowDescription);
        Traits.Add(Trait);
    }
    Traits.StableSort([](const FPSScoutingTrait& A, const FPSScoutingTrait& B) { return A.Strength > B.Strength; });
    return Traits;
}

TArray<FString> PSPlayerDNA::ValidateCatalog(const FPSPlayerDNACatalog& Catalog)
{
    TArray<FString> Problems;
    TSet<FName> AxesSeen;
    TSet<FName> TraitsSeen;
    for (int32 Index = 0; Index < Catalog.Axes.Num(); ++Index)
    {
        const FPSDNAAxisDef& Def = Catalog.Axes[Index];
        const FString Where = FString::Printf(TEXT("Axes[%d] '%s'"), Index, *Def.Axis.ToString());
        if (!IsAxis(Def.Axis))
        {
            Problems.Add(FString::Printf(TEXT("%s: not an FPSPlayerDNA axis"), *Where));
        }
        if (AxesSeen.Contains(Def.Axis))
        {
            Problems.Add(FString::Printf(TEXT("%s: listed twice"), *Where));
        }
        AxesSeen.Add(Def.Axis);
        if (Def.Roles.Num() == 0)
        {
            Problems.Add(FString::Printf(TEXT("%s: applies to no role"), *Where));
        }
        const FName Traits[] = { Def.LowTrait, Def.HighTrait };
        const FString Labels[] = { Def.LowLabel, Def.HighLabel };
        for (int32 End = 0; End < 2; ++End)
        {
            if (Traits[End].IsNone() || Labels[End].IsEmpty())
            {
                Problems.Add(FString::Printf(TEXT("%s: the %s trait needs an ID and a label"), *Where, End == 0 ? TEXT("low") : TEXT("high")));
            }
            else if (TraitsSeen.Contains(Traits[End]))
            {
                Problems.Add(FString::Printf(TEXT("%s: trait '%s' is used twice"), *Where, *Traits[End].ToString()));
            }
            TraitsSeen.Add(Traits[End]);
        }
    }
    for (int32 Index = 0; Index < Catalog.Bindings.Num(); ++Index)
    {
        const FPSDNABinding& Binding = Catalog.Bindings[Index];
        const FString Where = FString::Printf(TEXT("Bindings[%d] %s -> %s.%s"), Index, *Binding.Axis.ToString(), *Binding.Target.ToString(), *Binding.Field.ToString());
        if (!FindAxis(Catalog, Binding.Axis))
        {
            Problems.Add(FString::Printf(TEXT("%s: the axis isn't in Axes"), *Where));
        }
        const UScriptStruct* Struct = FindTargetStruct(Binding.Target);
        if (!Struct)
        {
            Problems.Add(FString::Printf(TEXT("%s: unknown target (SkillAI, Pocket, DefenderAI or RouteRunning)"), *Where));
        }
        else if (!FindFProperty<FFloatProperty>(Struct, Binding.Field))
        {
            Problems.Add(FString::Printf(TEXT("%s: not a float field of %s"), *Where, *Struct->GetName()));
        }
        if (Binding.AtLow <= 0.f || Binding.AtHigh <= 0.f)
        {
            Problems.Add(FString::Printf(TEXT("%s: AtLow and AtHigh must be above 0"), *Where));
        }
    }
    TSet<EPSRushMove> MovesSeen;
    for (const FPSDNARushMoveLean& Lean : Catalog.RushMoveLeans)
    {
        const FString MoveName = StaticEnum<EPSRushMove>()->GetNameStringByValue(static_cast<int64>(Lean.Move));
        if (Lean.Move == EPSRushMove::None || MovesSeen.Contains(Lean.Move))
        {
            Problems.Add(FString::Printf(TEXT("RushMoveLeans: '%s' is not a move, or is listed twice"), *MoveName));
        }
        MovesSeen.Add(Lean.Move);
        if (Lean.Lean < -1.f || Lean.Lean > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("RushMoveLeans '%s': Lean runs -1 to 1"), *MoveName));
        }
    }
    if (Catalog.RushStyleWeight < 0.f || Catalog.RushStyleWeight >= 1.f)
    {
        Problems.Add(TEXT("RushStyleWeight: 0 to below 1"));
    }
    if (Catalog.TraitThreshold <= 0.f || Catalog.TraitThreshold > 1.f)
    {
        Problems.Add(TEXT("TraitThreshold: above 0, at most 1"));
    }
    return Problems;
}

FString UPSPlayerDNASubsystem::GetDefaultCatalogPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/player_dna.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

UPSPlayerDNASubsystem* UPSPlayerDNASubsystem::Get(const UWorld* World)
{
    return World ? World->GetSubsystem<UPSPlayerDNASubsystem>() : nullptr;
}

const FPSPlayerDNACatalog& UPSPlayerDNASubsystem::GetCatalog()
{
    if (!bCatalogLoaded)
    {
        LoadCatalogFromJson(GetDefaultCatalogPath());
    }
    return Catalog;
}

bool UPSPlayerDNASubsystem::LoadCatalogFromJson(const FString& JsonFilePath)
{
    bCatalogLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSPlayerDNACatalog Loaded;
    if (!Ingestion->LoadPlayerDNACatalogFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayerDNASubsystem: Could not load the DNA catalog from %s; every player plays neutral."), *JsonFilePath);
        Catalog = FPSPlayerDNACatalog();
        return false;
    }
    for (const FString& Problem : PSPlayerDNA::ValidateCatalog(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayerDNASubsystem: %s: %s"), *JsonFilePath, *Problem);
    }
    Catalog = Loaded;
    return true;
}

void UPSPlayerDNASubsystem::SetCatalog(const FPSPlayerDNACatalog& InCatalog)
{
    Catalog = InCatalog;
    bCatalogLoaded = true;
}

float UPSPlayerDNASubsystem::GetRushMoveScale(const FPlayerAttributes& Rusher, EPSRushMove Move)
{
    return Rusher.DNA.IsNeutral() ? 1.f : PSPlayerDNA::RushMoveScale(GetCatalog(), Rusher, Move);
}

TArray<FPSScoutingTrait> UPSPlayerDNASubsystem::GetScoutingTraits(const FPlayerAttributes& Player)
{
    return PSPlayerDNA::GetScoutingTraits(GetCatalog(), Player);
}

bool UPSPlayerDNASubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
