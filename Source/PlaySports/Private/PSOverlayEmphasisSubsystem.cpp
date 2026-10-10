#include "PSOverlayEmphasisSubsystem.h"
#include "PSDataIngestion.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

namespace PSOverlayEmphasisPrivate
{
    /** Every kind needs a style entry. */
    const EPSEmphasisKind AllKinds[] = { EPSEmphasisKind::Highlight, EPSEmphasisKind::Mismatch, EPSEmphasisKind::Focus };

    /** A custom-depth stencil is one byte; 0 means none. */
    constexpr int32 MaxStencil = 255;
}

void UPSOverlayEmphasisSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadStyleFromJson(GetDefaultStylePath());
    OverlayDetail = PSPlatformTiers::GetActiveTier().OverlayDetail;
}

bool UPSOverlayEmphasisSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSOverlayEmphasisSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AdvanceTime(DeltaTime);
}

TStatId UPSOverlayEmphasisSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSOverlayEmphasisSubsystem, STATGROUP_Tickables);
}

FString UPSOverlayEmphasisSubsystem::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/player_emphasis.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSOverlayEmphasisSubsystem::LoadStyleFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSEmphasisStyle Loaded;
    if (!Ingestion->LoadEmphasisStyleFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOverlayEmphasisSubsystem: Could not load the emphasis style from %s; keeping the current one."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSOverlayEmphasisSubsystem: %s: %s"), *JsonFilePath, *Problem);
        }
        return false;
    }
    SetStyle(Loaded);
    return true;
}

void UPSOverlayEmphasisSubsystem::SetStyle(const FPSEmphasisStyle& InStyle)
{
    Style = InStyle;
    Refresh();
}

TArray<FString> UPSOverlayEmphasisSubsystem::ValidateStyle(const FPSEmphasisStyle& InStyle)
{
    using namespace PSOverlayEmphasisPrivate;

    TArray<FString> Problems;
    TArray<int32> Stencils;
    for (const EPSEmphasisKind Kind : AllKinds)
    {
        const FString KindName = UEnum::GetDisplayValueAsText(Kind).ToString();
        int32 Count = 0;
        for (const FPSEmphasisKindStyle& Entry : InStyle.Kinds)
        {
            if (Entry.Kind != Kind)
            {
                continue;
            }
            ++Count;
            Stencils.Add(Entry.Stencil);
            if (Entry.Stencil < 1 || Entry.Stencil > MaxStencil)
            {
                Problems.Add(FString::Printf(TEXT("Kinds %s: Stencil %d must be 1-%d"), *KindName, Entry.Stencil, MaxStencil));
            }
        }
        if (Count != 1)
        {
            Problems.Add(FString::Printf(TEXT("Kinds: %s is listed %d times; it needs exactly one entry"), *KindName, Count));
        }
    }
    Stencils.Add(InStyle.DimStencil);
    if (InStyle.DimStencil < 1 || InStyle.DimStencil > MaxStencil)
    {
        Problems.Add(FString::Printf(TEXT("DimStencil: %d must be 1-%d"), InStyle.DimStencil, MaxStencil));
    }
    TSet<int32> Distinct;
    for (const int32 Stencil : Stencils)
    {
        Distinct.Add(Stencil);
    }
    if (Distinct.Num() != Stencils.Num())
    {
        Problems.Add(TEXT("Stencil values must differ: the material tells the looks apart by them"));
    }
    if (InStyle.MaxEmphasized < 1)
    {
        Problems.Add(FString::Printf(TEXT("MaxEmphasized: %d must be 1 or more"), InStyle.MaxEmphasized));
    }
    return Problems;
}

int32 UPSOverlayEmphasisSubsystem::Emphasize(APSPlayerPawn* Pawn, EPSEmphasisKind Kind, FName Source, float Seconds)
{
    return AddRequest(Pawn, Kind, Source, Seconds, false);
}

int32 UPSOverlayEmphasisSubsystem::Spotlight(APSPlayerPawn* Pawn, FName Source, float Seconds)
{
    return AddRequest(Pawn, EPSEmphasisKind::Focus, Source, Seconds, true);
}

int32 UPSOverlayEmphasisSubsystem::AddRequest(APSPlayerPawn* Pawn, EPSEmphasisKind Kind, FName Source, float Seconds, bool bSpotlight)
{
    if (!IsValid(Pawn))
    {
        return 0;
    }
    FPSEmphasisRequest Request;
    Request.Handle = NextHandle++;
    Request.Pawn = Pawn;
    Request.Kind = Kind;
    Request.Source = Source;
    Request.bSpotlight = bSpotlight;
    Request.bTimed = Seconds > 0.f;
    Request.RemainingSeconds = FMath::Max(Seconds, 0.f);
    Requests.Add(Request);
    Refresh();
    return Request.Handle;
}

bool UPSOverlayEmphasisSubsystem::ClearEmphasis(int32 Handle)
{
    if (Requests.RemoveAll([Handle](const FPSEmphasisRequest& Request) { return Request.Handle == Handle; }) == 0)
    {
        return false;
    }
    Refresh();
    return true;
}

int32 UPSOverlayEmphasisSubsystem::ClearSource(FName Source)
{
    const int32 Removed = Requests.RemoveAll([Source](const FPSEmphasisRequest& Request) { return Request.Source == Source; });
    if (Removed > 0)
    {
        Refresh();
    }
    return Removed;
}

void UPSOverlayEmphasisSubsystem::ClearAll()
{
    Requests.Reset();
    Refresh();
}

bool UPSOverlayEmphasisSubsystem::IsSpotlightActive() const
{
    return Requests.ContainsByPredicate([](const FPSEmphasisRequest& Request) { return Request.bSpotlight && Request.Pawn.IsValid(); });
}

FPSPawnEmphasis UPSOverlayEmphasisSubsystem::GetEmphasis(const APSPlayerPawn* Pawn) const
{
    const FPSPawnEmphasis* Found = Pawn ? Looks.Find(FObjectKey(Pawn)) : nullptr;
    return Found ? *Found : FPSPawnEmphasis();
}

void UPSOverlayEmphasisSubsystem::SetOverlayDetail(EPSOverlayDetail InDetail)
{
    OverlayDetail = InDetail;
    Refresh();
}

void UPSOverlayEmphasisSubsystem::AdvanceTime(float DeltaSeconds)
{
    const float Step = FMath::Max(DeltaSeconds, 0.f);
    bool bChanged = false;
    for (int32 Index = Requests.Num() - 1; Index >= 0; --Index)
    {
        FPSEmphasisRequest& Request = Requests[Index];
        if (Request.bTimed)
        {
            Request.RemainingSeconds -= Step;
        }
        if (!Request.Pawn.IsValid() || (Request.bTimed && Request.RemainingSeconds <= 0.f))
        {
            Requests.RemoveAt(Index);
            bChanged = true;
        }
    }
    if (bChanged)
    {
        Refresh();
    }
}

int32 UPSOverlayEmphasisSubsystem::PriorityOf(EPSEmphasisKind Kind) const
{
    const FPSEmphasisKindStyle* Entry = Style.FindKind(Kind);
    return Entry ? Entry->Priority : 0;
}

int32 UPSOverlayEmphasisSubsystem::StencilOf(EPSEmphasisKind Kind) const
{
    const FPSEmphasisKindStyle* Entry = Style.FindKind(Kind);
    return Entry ? Entry->Stencil : 0;
}

EPSEmphasisLook UPSOverlayEmphasisSubsystem::LookOf(EPSEmphasisKind Kind)
{
    switch (Kind)
    {
    case EPSEmphasisKind::Mismatch: return EPSEmphasisLook::Mismatch;
    case EPSEmphasisKind::Focus:    return EPSEmphasisLook::Focus;
    default:                        return EPSEmphasisLook::Highlight;
    }
}

void UPSOverlayEmphasisSubsystem::MarkMeshes(APSPlayerPawn& Pawn, int32 Stencil)
{
    TInlineComponentArray<UMeshComponent*> Meshes(&Pawn);
    for (UMeshComponent* Mesh : Meshes)
    {
        Mesh->SetRenderCustomDepth(Stencil > 0);
        Mesh->SetCustomDepthStencilValue(FMath::Max(Stencil, 0));
    }
}

void UPSOverlayEmphasisSubsystem::Refresh()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // Each player's winning request: the highest priority, the newest on a tie (requests are
    // kept oldest first).
    TMap<APSPlayerPawn*, const FPSEmphasisRequest*> Winning;
    TSet<APSPlayerPawn*> Spotlit;
    for (const FPSEmphasisRequest& Request : Requests)
    {
        APSPlayerPawn* Pawn = Request.Pawn.Get();
        if (!IsValid(Pawn))
        {
            continue;
        }
        if (Request.bSpotlight)
        {
            Spotlit.Add(Pawn);
        }
        const FPSEmphasisRequest** Current = Winning.Find(Pawn);
        if (!Current || PriorityOf(Request.Kind) >= PriorityOf((*Current)->Kind))
        {
            Winning.Add(Pawn, &Request);
        }
    }

    // Under the budget, the highest priority players are drawn first, newest first on a tie.
    struct FRanked
    {
        APSPlayerPawn* Pawn;
        const FPSEmphasisRequest* Request;
    };
    TArray<FRanked> Ranked;
    for (const TPair<APSPlayerPawn*, const FPSEmphasisRequest*>& Entry : Winning)
    {
        FRanked Item;
        Item.Pawn = Entry.Key;
        Item.Request = Entry.Value;
        Ranked.Add(Item);
    }
    Ranked.Sort([this](const FRanked& A, const FRanked& B)
    {
        const int32 PriorityA = PriorityOf(A.Request->Kind);
        const int32 PriorityB = PriorityOf(B.Request->Kind);
        return PriorityA != PriorityB ? PriorityA > PriorityB : A.Request->Handle > B.Request->Handle;
    });

    const bool bDraw = OverlayDetail != EPSOverlayDetail::Minimal;
    TMap<FObjectKey, FPSPawnEmphasis> NewLooks;
    int32 Drawn = 0;
    for (const FRanked& Item : Ranked)
    {
        FPSPawnEmphasis Look;
        Look.Look = LookOf(Item.Request->Kind);
        Look.Source = Item.Request->Source;
        Look.bRendered = bDraw && Drawn < Style.MaxEmphasized;
        if (Look.bRendered)
        {
            ++Drawn;
            Look.Stencil = StencilOf(Item.Request->Kind);
        }
        NewLooks.Add(FObjectKey(Item.Pawn), Look);
    }

    // In a spotlight everyone else recedes: players with no drawn look of their own, and the
    // emphasized ones too when the style says so.
    if (Spotlit.Num() > 0)
    {
        for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
        {
            APSPlayerPawn* Pawn = *It;
            if (!IsValid(Pawn) || Spotlit.Contains(Pawn))
            {
                continue;
            }
            const FPSPawnEmphasis* Own = NewLooks.Find(FObjectKey(Pawn));
            if (Own && Own->bRendered && !Style.bSpotlightDimsEmphasized)
            {
                continue;
            }
            FPSPawnEmphasis Dim;
            Dim.Look = EPSEmphasisLook::Dimmed;
            Dim.bRendered = bDraw;
            Dim.Stencil = bDraw ? Style.DimStencil : 0;
            NewLooks.Add(FObjectKey(Pawn), Dim);
        }
    }

    // Mark the meshes that change.
    for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
    {
        APSPlayerPawn* Pawn = *It;
        if (!IsValid(Pawn))
        {
            continue;
        }
        const FObjectKey Key(Pawn);
        const FPSPawnEmphasis* Now = NewLooks.Find(Key);
        const FPSPawnEmphasis* Before = Looks.Find(Key);
        const int32 NewStencil = Now && Now->bRendered ? Now->Stencil : 0;
        const int32 OldStencil = Before && Before->bRendered ? Before->Stencil : 0;
        if (NewStencil != OldStencil)
        {
            MarkMeshes(*Pawn, NewStencil);
        }
    }
    Looks = MoveTemp(NewLooks);
}
