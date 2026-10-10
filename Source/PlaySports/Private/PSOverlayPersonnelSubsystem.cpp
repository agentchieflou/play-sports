#include "PSOverlayPersonnelSubsystem.h"
#include "PSDataIngestion.h"
#include "PSFieldGrid.h"
#include "PSOverlayBroadcastSubsystem.h"
#include "PSPersonnelManager.h"
#include "PSPlayerPawn.h"
#include "PSUITeamCatalog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

namespace PSOverlayPersonnelPrivate
{
    const EPlayerRole AllRoles[] = { EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::TightEnd,
        EPlayerRole::OffensiveLineman, EPlayerRole::DefensiveLineman, EPlayerRole::Linebacker, EPlayerRole::DefensiveBack };

    FString RoleName(EPlayerRole Role)
    {
        const UEnum* Roles = StaticEnum<EPlayerRole>();
        return Roles ? Roles->GetNameStringByValue(static_cast<int64>(Role)) : FString();
    }

    int32 CountOf(const TMap<EPlayerRole, int32>& Counts, EPlayerRole Role)
    {
        const int32* Found = Counts.Find(Role);
        return Found ? *Found : 0;
    }

    /** True when Package fields exactly Counts, role for role. */
    bool Matches(const FPSPersonnelPackage& Package, const TMap<EPlayerRole, int32>& Counts)
    {
        for (const EPlayerRole Role : AllRoles)
        {
            const int32* Listed = Package.RoleCounts.Find(RoleName(Role));
            if ((Listed ? *Listed : 0) != CountOf(Counts, Role))
            {
                return false;
            }
        }
        return true;
    }

    /** Format with each "{Label}" replaced by that role's count. */
    FString FillCounts(const FString& Format, const TArray<FPSPersonnelRoleLabel>& Roles, const TMap<EPlayerRole, int32>& Counts)
    {
        FString Out = Format;
        for (const FPSPersonnelRoleLabel& Entry : Roles)
        {
            Out.ReplaceInline(*FString::Printf(TEXT("{%s}"), *Entry.Label), *FString::FromInt(CountOf(Counts, Entry.Role)), ESearchCase::CaseSensitive);
        }
        return Out;
    }

    bool RoleIsOnSide(EPlayerRole Role, bool bOffense)
    {
        return (APSFieldGrid::GetSideForRole(Role) == EPSTeamSide::Offense) == bOffense;
    }
}

void UPSOverlayPersonnelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    LoadStyleFromJson(GetDefaultStylePath());
    OverlayDetail = PSPlatformTiers::GetActiveTier().OverlayDetail;
    if (UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>())
    {
        Bus->OnPersonnelMC.AddUObject(this, &UPSOverlayPersonnelSubsystem::HandlePersonnel);
        Bus->OnGameStateMC.AddUObject(this, &UPSOverlayPersonnelSubsystem::HandleGameState);
        BoundBus = Bus;
    }
}

void UPSOverlayPersonnelSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnPersonnelMC.RemoveAll(this);
        Bus->OnGameStateMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSOverlayPersonnelSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSOverlayPersonnelSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AdvanceTime(DeltaTime);
}

TStatId UPSOverlayPersonnelSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSOverlayPersonnelSubsystem, STATGROUP_Tickables);
}

FString UPSOverlayPersonnelSubsystem::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/personnel_panel.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSOverlayPersonnelSubsystem::LoadStyleFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSPersonnelPanelStyle Loaded;
    if (!Ingestion->LoadPersonnelPanelStyleFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOverlayPersonnelSubsystem: Could not load the personnel panel style from %s; keeping the current one."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSOverlayPersonnelSubsystem: %s: %s"), *JsonFilePath, *Problem);
        }
        return false;
    }
    SetStyle(Loaded);
    return true;
}

void UPSOverlayPersonnelSubsystem::SetStyle(const FPSPersonnelPanelStyle& InStyle)
{
    Style = InStyle;
    Refresh();
}

TArray<FString> UPSOverlayPersonnelSubsystem::ValidateStyle(const FPSPersonnelPanelStyle& InStyle)
{
    using namespace PSOverlayPersonnelPrivate;

    TArray<FString> Problems;
    auto CheckRoles = [&Problems](const TArray<FPSPersonnelRoleLabel>& Roles, bool bOffense, const TCHAR* Field)
    {
        if (Roles.Num() == 0)
        {
            Problems.Add(FString::Printf(TEXT("%s: no roles to count"), Field));
        }
        TSet<EPlayerRole> Seen;
        for (const FPSPersonnelRoleLabel& Entry : Roles)
        {
            if (!RoleIsOnSide(Entry.Role, bOffense))
            {
                Problems.Add(FString::Printf(TEXT("%s: %s plays on the other side"), Field, *RoleName(Entry.Role)));
            }
            if (Seen.Contains(Entry.Role))
            {
                Problems.Add(FString::Printf(TEXT("%s: %s is listed twice"), Field, *RoleName(Entry.Role)));
            }
            Seen.Add(Entry.Role);
            if (Entry.Label.IsEmpty())
            {
                Problems.Add(FString::Printf(TEXT("%s: %s has no label"), Field, *RoleName(Entry.Role)));
            }
        }
    };
    CheckRoles(InStyle.OffenseRoles, true, TEXT("OffenseRoles"));
    CheckRoles(InStyle.DefenseRoles, false, TEXT("DefenseRoles"));

    TSet<int32> Backs;
    for (const FPSDefenseName& Entry : InStyle.DefenseNames)
    {
        if (Entry.DefensiveBacks < 0 || Entry.Name.IsEmpty() || Backs.Contains(Entry.DefensiveBacks))
        {
            Problems.Add(FString::Printf(TEXT("DefenseNames: %d defensive backs needs one name, and a count of 0 or more"), Entry.DefensiveBacks));
        }
        Backs.Add(Entry.DefensiveBacks);
    }
    if (InStyle.OffenseNameFormat.IsEmpty() || InStyle.DefenseNameFallback.IsEmpty())
    {
        Problems.Add(TEXT("OffenseNameFormat and DefenseNameFallback must not be empty"));
    }

    struct FNamedColor
    {
        const TCHAR* Name;
        const FString* Value;
    };
    const FNamedColor Colors[] = { { TEXT("PanelColor"), &InStyle.PanelColor }, { TEXT("TextColor"), &InStyle.TextColor }, { TEXT("FlashColor"), &InStyle.FlashColor } };
    for (const FNamedColor& Color : Colors)
    {
        FLinearColor Parsed;
        if (!UPSUITeamCatalog::ParseHexColor(*Color.Value, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("%s: '%s' must be #RRGGBB"), Color.Name, **Color.Value));
        }
    }
    if (InStyle.FontSize < 1 || InStyle.TitleFontSize < 1)
    {
        Problems.Add(TEXT("FontSize and TitleFontSize must be 1 or more"));
    }
    if (InStyle.ChangeFlashSeconds < 0.f)
    {
        Problems.Add(TEXT("ChangeFlashSeconds must be 0 or more"));
    }
    return Problems;
}

const FPSPersonnelCatalog& UPSOverlayPersonnelSubsystem::GetCatalog()
{
    if (!bCatalogLoaded)
    {
        bCatalogLoaded = true;
        FString Path = UPSPersonnelManager::GetDefaultCatalogPath();
        FPaths::CollapseRelativeDirectories(Path);
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
        FPSPersonnelCatalog Loaded;
        if (Ingestion->LoadPersonnelCatalogFromJson(Path, Loaded))
        {
            Catalog = Loaded;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSOverlayPersonnelSubsystem: Could not load the personnel catalog from %s; packages are named by rule."), *Path);
        }
    }
    return Catalog;
}

void UPSOverlayPersonnelSubsystem::SetCatalog(const FPSPersonnelCatalog& InCatalog)
{
    Catalog = InCatalog;
    bCatalogLoaded = true;
    Refresh();
}

FString UPSOverlayPersonnelSubsystem::NamePackage(const TMap<EPlayerRole, int32>& Counts, bool bOffense, const FPSPersonnelCatalog& InCatalog,
    const FPSPersonnelPanelStyle& InStyle, FName PreferredPackage)
{
    using namespace PSOverlayPersonnelPrivate;

    // The catalog's name for exactly these counts, the announced package first.
    const FPSPersonnelPackage* Named = nullptr;
    for (const FPSPersonnelPackage& Package : InCatalog.Packages)
    {
        if (Package.bOffense != bOffense || !Matches(Package, Counts))
        {
            continue;
        }
        if (!Named || Package.PackageId == PreferredPackage)
        {
            Named = &Package;
        }
    }
    if (Named && !Named->DisplayName.IsEmpty())
    {
        return Named->DisplayName;
    }

    if (bOffense)
    {
        return FillCounts(InStyle.OffenseNameFormat, InStyle.OffenseRoles, Counts);
    }
    const int32 Backs = CountOf(Counts, EPlayerRole::DefensiveBack);
    for (const FPSDefenseName& Entry : InStyle.DefenseNames)
    {
        if (Entry.DefensiveBacks == Backs)
        {
            return Entry.Name;
        }
    }
    return FillCounts(InStyle.DefenseNameFallback, InStyle.DefenseRoles, Counts);
}

TMap<EPlayerRole, int32> UPSOverlayPersonnelSubsystem::CountSide(const UWorld* World, bool bOffense)
{
    TMap<EPlayerRole, int32> Counts;
    if (!World)
    {
        return Counts;
    }
    for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
    {
        const APSPlayerPawn* Player = *It;
        if (IsValid(Player) && (Player->TeamSide == EPSTeamSide::Offense) == bOffense)
        {
            ++Counts.FindOrAdd(Player->GetAttributes().Role);
        }
    }
    return Counts;
}

void UPSOverlayPersonnelSubsystem::SetOverlayDetail(EPSOverlayDetail InDetail)
{
    OverlayDetail = InDetail;
    if (OverlayDetail != EPSOverlayDetail::Full)
    {
        OffenseFlashRemaining = 0.f;
        DefenseFlashRemaining = 0.f;
    }
    Refresh();
}

void UPSOverlayPersonnelSubsystem::HandlePersonnel(const FPSTelemetryPersonnelEvent& Event)
{
    // The counts before the change, to flag the ones it changes.
    const FPSPersonnelPanel Before = Event.bOffense ? OffensePanel : DefensePanel;
    (Event.bOffense ? OffensePackageId : DefensePackageId) = Event.PackageId;
    if (OverlayDetail == EPSOverlayDetail::Full)
    {
        (Event.bOffense ? OffenseFlashRemaining : DefenseFlashRemaining) = Style.ChangeFlashSeconds;
    }
    Refresh();

    FPSPersonnelPanel& After = Event.bOffense ? OffensePanel : DefensePanel;
    for (FPSPersonnelRoleCount& Count : After.Counts)
    {
        const FPSPersonnelRoleCount* Old = Before.Counts.FindByPredicate([&Count](const FPSPersonnelRoleCount& Candidate) { return Candidate.Role == Count.Role; });
        Count.bChanged = !Old || Old->Count != Count.Count;
    }
}

void UPSOverlayPersonnelSubsystem::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    Phase = Event.Phase;
    bHomeHasPossession = Event.bHomeHasPossession;
    bHasGameState = true;
    Refresh();
}

void UPSOverlayPersonnelSubsystem::AdvanceTime(float DeltaSeconds)
{
    const float Step = FMath::Max(DeltaSeconds, 0.f);
    if (OffenseFlashRemaining <= 0.f && DefenseFlashRemaining <= 0.f)
    {
        return;
    }
    OffenseFlashRemaining = FMath::Max(OffenseFlashRemaining - Step, 0.f);
    DefenseFlashRemaining = FMath::Max(DefenseFlashRemaining - Step, 0.f);
    const float Span = FMath::Max(Style.ChangeFlashSeconds, KINDA_SMALL_NUMBER);
    OffensePanel.FlashAlpha = OffenseFlashRemaining / Span;
    DefensePanel.FlashAlpha = DefenseFlashRemaining / Span;
    // Once a flash is over, its counts are no longer news.
    for (FPSPersonnelPanel* Panel : { &OffensePanel, &DefensePanel })
    {
        if (Panel->FlashAlpha <= 0.f)
        {
            for (FPSPersonnelRoleCount& Count : Panel->Counts)
            {
                Count.bChanged = false;
            }
        }
    }
}

void UPSOverlayPersonnelSubsystem::Refresh()
{
    BuildPanel(OffensePanel, true, Style.OffenseRoles, OffenseFlashRemaining);
    BuildPanel(DefensePanel, false, Style.DefenseRoles, DefenseFlashRemaining);
}

void UPSOverlayPersonnelSubsystem::BuildPanel(FPSPersonnelPanel& Panel, bool bOffense, const TArray<FPSPersonnelRoleLabel>& Roles, float FlashRemaining)
{
    using namespace PSOverlayPersonnelPrivate;

    const UWorld* World = GetWorld();
    const TMap<EPlayerRole, int32> Counts = CountSide(World, bOffense);

    // Changed flags survive a recount until the flash is over.
    TMap<EPlayerRole, bool> WasChanged;
    for (const FPSPersonnelRoleCount& Count : Panel.Counts)
    {
        WasChanged.Add(Count.Role, Count.bChanged && FlashRemaining > 0.f);
    }

    Panel.bOffense = bOffense;
    Panel.Counts.Reset();
    TArray<FString> Parts;
    for (const FPSPersonnelRoleLabel& Entry : Roles)
    {
        FPSPersonnelRoleCount Count;
        Count.Role = Entry.Role;
        Count.Label = Entry.Label;
        Count.Count = CountOf(Counts, Entry.Role);
        const bool* Changed = WasChanged.Find(Entry.Role);
        Count.bChanged = Changed && *Changed;
        Panel.Counts.Add(Count);
        Parts.Add(FString::Printf(TEXT("%s %d"), *Entry.Label, Count.Count));
    }
    Panel.CountsText = FString::Join(Parts, TEXT(" | "));
    Panel.PackageName = NamePackage(Counts, bOffense, GetCatalog(), Style, bOffense ? OffensePackageId : DefensePackageId);

    // The side with the ball is the offense (from the simulation's own announcement, not the
    // score bug's copy of it, which may not have heard yet); teams as the score bug shows them.
    const UPSOverlayBroadcastSubsystem* Broadcast = World ? World->GetSubsystem<UPSOverlayBroadcastSubsystem>() : nullptr;
    if (Broadcast)
    {
        const FPSScoreBugState Bug = Broadcast->GetScoreBug();
        const bool bHomeOffense = bHasGameState ? bHomeHasPossession : (Bug.bValid ? Bug.bHomeHasPossession : true);
        const bool bHomeTeam = bHomeOffense == bOffense;
        Panel.TeamLabel = bHomeTeam ? Bug.HomeLabel : Bug.AwayLabel;
        Panel.TeamColor = bHomeTeam ? Bug.HomeColor : Bug.AwayColor;
    }

    const bool bPreSnap = !bHasGameState || Phase == TEXT("PreSnap");
    Panel.bVisible = OverlayDetail != EPSOverlayDetail::Minimal && (bPreSnap || Style.bShowInPlay) && Counts.Num() > 0;
    Panel.FlashAlpha = OverlayDetail == EPSOverlayDetail::Full ? FlashRemaining / FMath::Max(Style.ChangeFlashSeconds, KINDA_SMALL_NUMBER) : 0.f;
}
