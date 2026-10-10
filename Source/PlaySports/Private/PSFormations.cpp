#include "PSFormations.h"
#include "PSDataIngestion.h"
#include "PSFieldDimensions.h"
#include "PSFieldGrid.h"
#include "Misc/Paths.h"

namespace PSFormationsPrivate
{
    const FName StrongName(TEXT("Strong"));
    const FName WeakName(TEXT("Weak"));
    const FName RightName(TEXT("Right"));
    const FName LeftName(TEXT("Left"));

    bool IsOffenseRole(EPlayerRole Role)
    {
        return APSFieldGrid::GetSideForRole(Role) == EPSTeamSide::Offense;
    }

    /** The slot's side as +1 or -1 across the field, given the strength's. */
    float SideSign(const FPSFormationSpawnPoint& Slot, float StrongSide)
    {
        return Slot.Side == WeakName ? -StrongSide : StrongSide;
    }

    /** The Nth slot of Role in Slots, or null. */
    const FPSFormationSpawnPoint* FindSlot(const TArray<FPSFormationSpawnPoint>& Slots, EPlayerRole Role, int32 RoleIndex)
    {
        int32 Seen = 0;
        for (const FPSFormationSpawnPoint& Slot : Slots)
        {
            if (Slot.Role == Role)
            {
                if (Seen == RoleIndex)
                {
                    return &Slot;
                }
                ++Seen;
            }
        }
        return nullptr;
    }

    /** Warns once per run about each name the catalog doesn't know. */
    void WarnUnknown(const TCHAR* Kind, const FString& Name)
    {
        static TSet<FString> Warned;
        const FString Key = FString::Printf(TEXT("%s/%s"), Kind, *Name.ToLower());
        if (!Warned.Contains(Key))
        {
            Warned.Add(Key);
            UE_LOG(LogTemp, Warning, TEXT("PSFormations: no %s '%s' in %s; that side lines up by role."), Kind, *Name, *PSFormations::GetDefaultDataPath());
        }
    }

    /** The offensive line the defense keys its techniques on. */
    struct FLineReference
    {
        TArray<float> LinemenY;
        int32 CenterIndex = INDEX_NONE;
        float BallY = 0.f;
        float Spacing = 150.f;

        /** Lineman Number (0 the center) on Side's side; past the end of the line, one spacing
         *  further per lineman. */
        float LinemanY(int32 Number, float Side) const
        {
            if (CenterIndex == INDEX_NONE)
            {
                return BallY + Side * Number * Spacing;
            }
            const int32 Wanted = CenterIndex + (Side > 0.f ? Number : -Number);
            if (LinemenY.IsValidIndex(Wanted))
            {
                return LinemenY[Wanted];
            }
            const int32 Last = Side > 0.f ? LinemenY.Num() - 1 : 0;
            return LinemenY[Last] + Side * FMath::Abs(Wanted - Last) * Spacing;
        }
    };

    struct FReceiverSpot
    {
        float Y = 0.f;
        bool bCovered = false;
    };
}

FString PSFormations::GetDefaultDataPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/formations.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSFormationCatalog& PSFormations::GetCatalog()
{
    // Read once per run; a missing or unsound file leaves an empty catalog, which lines
    // everyone up by role.
    static FPSFormationCatalog Active;
    static bool bLoaded = false;
    if (!bLoaded)
    {
        bLoaded = true;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
        FPSFormationCatalog Loaded;
        if (!Ingestion->LoadFormationCatalogFromJson(GetDefaultDataPath(), Loaded))
        {
            UE_LOG(LogTemp, Warning, TEXT("PSFormations: Could not load %s; every side lines up by role."), *GetDefaultDataPath());
        }
        else
        {
            const TArray<FString> Problems = Validate(Loaded);
            for (const FString& Problem : Problems)
            {
                UE_LOG(LogTemp, Warning, TEXT("PSFormations: %s"), *Problem);
            }
            if (Problems.Num() == 0)
            {
                Active = Loaded;
            }
        }
    }
    return Active;
}

TArray<FString> PSFormations::Validate(const FPSFormationCatalog& Catalog)
{
    using namespace PSFormationsPrivate;
    TArray<FString> Problems;
    if (Catalog.LinemanSpacingYards <= 0.f)
    {
        Problems.Add(TEXT("LinemanSpacingYards must be above 0"));
    }
    if (Catalog.LineSetbackYards < 0.f || Catalog.ShadeYards < 0.f || Catalog.BoundaryMarginYards < 0.f)
    {
        Problems.Add(TEXT("LineSetbackYards, ShadeYards and BoundaryMarginYards must be 0 or more"));
    }

    TSet<FName> Techniques;
    for (int32 Index = 0; Index < Catalog.Techniques.Num(); ++Index)
    {
        const FPSTechniqueDef& Technique = Catalog.Techniques[Index];
        const FString Where = FString::Printf(TEXT("Techniques[%d] '%s'"), Index, *Technique.Technique.ToString());
        if (Technique.Technique.IsNone() || Techniques.Contains(Technique.Technique))
        {
            Problems.Add(Where + TEXT(": no name, or listed twice"));
        }
        Techniques.Add(Technique.Technique);
        if (Technique.Lineman < 0 || Technique.Lineman > 3 || Technique.Shade < -1 || Technique.Shade > 1)
        {
            Problems.Add(Where + TEXT(": Lineman 0-3 and Shade -1, 0 or 1"));
        }
    }

    // A slot's side, reference and depth for its side of the ball.
    auto CheckSlot = [&Problems, &Techniques](const FString& Where, const FPSFormationSpawnPoint& Slot, bool bOffense)
    {
        if (IsOffenseRole(Slot.Role) != bOffense || (bOffense && Slot.Role == EPlayerRole::OffensiveLineman))
        {
            Problems.Add(Where + FString::Printf(TEXT(": %s doesn't take a slot here"), *UEnum::GetValueAsString(Slot.Role)));
        }
        if (!Slot.Side.IsNone() && Slot.Side != StrongName && Slot.Side != WeakName)
        {
            Problems.Add(Where + TEXT(": Side is Strong or Weak"));
        }
        if (bOffense ? Slot.ScrimmageYardOffset > 0.f : Slot.ScrimmageYardOffset < 0.f)
        {
            Problems.Add(Where + (bOffense ? TEXT(": the offense lines up behind the line (ScrimmageYardOffset 0 or less)")
                : TEXT(": the defense lines up beyond the line (ScrimmageYardOffset 0 or more)")));
        }
        if (bOffense && (!Slot.Technique.IsNone() || Slot.OverReceiver != 0))
        {
            Problems.Add(Where + TEXT(": the offense keys on no technique or receiver"));
        }
        if (!Slot.Technique.IsNone() && !Techniques.Contains(Slot.Technique))
        {
            Problems.Add(Where + FString::Printf(TEXT(": unknown technique '%s'"), *Slot.Technique.ToString()));
        }
        if (Slot.OverReceiver < 0 || (Slot.OverReceiver > 0 && !Slot.Technique.IsNone()))
        {
            Problems.Add(Where + TEXT(": OverReceiver is 0 or more, and not with a technique"));
        }
    };

    TSet<FString> Names;
    const TArray<FName> QBAlignments = { TEXT("UnderCenter"), TEXT("Pistol"), TEXT("Shotgun") };
    const TArray<FName> Backfields = { TEXT("Empty"), TEXT("Single"), TEXT("Offset"), TEXT("I"), TEXT("Split"), TEXT("Full") };
    for (int32 Index = 0; Index < Catalog.OffenseFormations.Num(); ++Index)
    {
        const FPSOffenseFormationDef& Def = Catalog.OffenseFormations[Index];
        const FString Where = FString::Printf(TEXT("OffenseFormations[%d] '%s'"), Index, *Def.Formation);
        if (Def.Formation.TrimStartAndEnd().IsEmpty() || Names.Contains(Def.Formation.ToLower()))
        {
            Problems.Add(Where + TEXT(": no name, or listed twice"));
        }
        Names.Add(Def.Formation.ToLower());
        if (Def.Strength != RightName && Def.Strength != LeftName)
        {
            Problems.Add(Where + TEXT(": Strength is Right or Left"));
        }
        if (!QBAlignments.Contains(Def.QBAlignment) || !Backfields.Contains(Def.Backfield))
        {
            Problems.Add(Where + TEXT(": QBAlignment and Backfield must be Epic 80's names"));
        }
        int32 Quarterbacks = 0;
        for (int32 SlotIndex = 0; SlotIndex < Def.Slots.Num(); ++SlotIndex)
        {
            CheckSlot(FString::Printf(TEXT("%s.Slots[%d]"), *Where, SlotIndex), Def.Slots[SlotIndex], true);
            Quarterbacks += Def.Slots[SlotIndex].Role == EPlayerRole::Quarterback ? 1 : 0;
        }
        if (Quarterbacks != 1)
        {
            Problems.Add(Where + TEXT(": one Quarterback slot"));
        }
    }

    Names.Reset();
    for (int32 Index = 0; Index < Catalog.FrontAlignments.Num(); ++Index)
    {
        const FPSFrontAlignmentDef& Def = Catalog.FrontAlignments[Index];
        const FString Where = FString::Printf(TEXT("FrontAlignments[%d] '%s'"), Index, *Def.Front);
        if (Def.Front.TrimStartAndEnd().IsEmpty() || Names.Contains(Def.Front.ToLower()))
        {
            Problems.Add(Where + TEXT(": no name, or listed twice"));
        }
        Names.Add(Def.Front.ToLower());
        for (int32 SlotIndex = 0; SlotIndex < Def.Slots.Num(); ++SlotIndex)
        {
            CheckSlot(FString::Printf(TEXT("%s.Slots[%d]"), *Where, SlotIndex), Def.Slots[SlotIndex], false);
        }
    }

    Names.Reset();
    for (int32 Index = 0; Index < Catalog.ShellAlignments.Num(); ++Index)
    {
        const FPSShellAlignmentDef& Def = Catalog.ShellAlignments[Index];
        const FString Where = FString::Printf(TEXT("ShellAlignments[%d] '%s'"), Index, *Def.Shell);
        if (IsNoShell(Def.Shell) || Names.Contains(Def.Shell.ToLower()))
        {
            Problems.Add(Where + TEXT(": no name (or None), or listed twice"));
        }
        Names.Add(Def.Shell.ToLower());
        for (int32 SlotIndex = 0; SlotIndex < Def.Slots.Num(); ++SlotIndex)
        {
            const FString SlotWhere = FString::Printf(TEXT("%s.Slots[%d]"), *Where, SlotIndex);
            CheckSlot(SlotWhere, Def.Slots[SlotIndex], false);
            if (Def.Slots[SlotIndex].Role != EPlayerRole::DefensiveBack)
            {
                Problems.Add(SlotWhere + TEXT(": a shell places the defensive backs"));
            }
        }
    }
    return Problems;
}

const FPSOffenseFormationDef* PSFormations::FindFormation(const FPSFormationCatalog& Catalog, const FString& Formation)
{
    return Catalog.OffenseFormations.FindByPredicate([&Formation](const FPSOffenseFormationDef& Def) { return Def.Formation.Equals(Formation, ESearchCase::IgnoreCase); });
}

const FPSFrontAlignmentDef* PSFormations::FindFront(const FPSFormationCatalog& Catalog, const FString& Front)
{
    return Catalog.FrontAlignments.FindByPredicate([&Front](const FPSFrontAlignmentDef& Def) { return Def.Front.Equals(Front, ESearchCase::IgnoreCase); });
}

const FPSShellAlignmentDef* PSFormations::FindShell(const FPSFormationCatalog& Catalog, const FString& Shell)
{
    return Catalog.ShellAlignments.FindByPredicate([&Shell](const FPSShellAlignmentDef& Def) { return Def.Shell.Equals(Shell, ESearchCase::IgnoreCase); });
}

bool PSFormations::IsNoShell(const FString& Shell)
{
    const FString Trimmed = Shell.TrimStartAndEnd();
    return Trimmed.IsEmpty() || Trimmed.Equals(TEXT("None"), ESearchCase::IgnoreCase);
}

FPSLineupResult PSFormations::LineUp(const FPSFormationCatalog& Catalog, const TArray<EPlayerRole>& Roles, const FVector& Ball,
    const FPSLineupCall& Call, const TArray<FPSAlignedPlayer>* Offense, const FPSPlayRecognitionTuning& Recognition)
{
    using namespace PSFormationsPrivate;
    FPSLineupResult Result;

    // Everyone starts on his role's spot: what a side without a known call keeps.
    Result.Spots = APSFieldGrid::ComputeLineup(Roles, static_cast<float>(Ball.X));
    for (FVector& Spot : Result.Spots)
    {
        Spot.Y += Ball.Y;
    }
    const float Spacing = PSField::YardsToCentimetres(Catalog.LinemanSpacingYards);
    auto ToCm = [](float Yards) { return PSField::YardsToCentimetres(Yards); };

    // --- The offense, in its formation ---
    const bool bWantsFormation = !Call.OffenseFormation.TrimStartAndEnd().IsEmpty();
    const FPSOffenseFormationDef* Formation = bWantsFormation ? FindFormation(Catalog, Call.OffenseFormation) : nullptr;
    if (bWantsFormation && !Formation)
    {
        WarnUnknown(TEXT("formation"), Call.OffenseFormation);
    }
    Result.bFormationFound = Formation != nullptr;
    if (Formation)
    {
        Result.StrongSide = Formation->Strength == LeftName ? -1 : 1;
        const float StrongSide = static_cast<float>(Result.StrongSide);
        int32 Linemen = 0;
        for (const EPlayerRole Role : Roles)
        {
            Linemen += Role == EPlayerRole::OffensiveLineman ? 1 : 0;
        }
        TMap<EPlayerRole, int32> Seen;
        for (int32 Index = 0; Index < Roles.Num(); ++Index)
        {
            const EPlayerRole Role = Roles[Index];
            if (!IsOffenseRole(Role))
            {
                continue;
            }
            const int32 RoleIndex = Seen.FindOrAdd(Role)++;
            if (Role == EPlayerRole::OffensiveLineman)
            {
                // The line, centred on the ball, left to right in role order.
                Result.Spots[Index].X = Ball.X - ToCm(Catalog.LineSetbackYards);
                Result.Spots[Index].Y = Ball.Y + (RoleIndex - (Linemen - 1) * 0.5f) * Spacing;
                continue;
            }
            const FPSFormationSpawnPoint* Slot = FindSlot(Formation->Slots, Role, RoleIndex);
            if (!Slot)
            {
                ++Result.RoleFallbacks;
                continue;
            }
            Result.Spots[Index].X = Ball.X + ToCm(Slot->ScrimmageYardOffset);
            Result.Spots[Index].Y = Ball.Y + SideSign(*Slot, StrongSide) * ToCm(Slot->LateralYardOffset);
        }
    }

    // --- The defense, in its front and shell, against the offense as it stands ---
    const bool bWantsFront = !Call.DefenseFront.TrimStartAndEnd().IsEmpty();
    const bool bWantsShell = !IsNoShell(Call.DefenseShell);
    const FPSFrontAlignmentDef* Front = bWantsFront ? FindFront(Catalog, Call.DefenseFront) : nullptr;
    const FPSShellAlignmentDef* Shell = bWantsShell ? FindShell(Catalog, Call.DefenseShell) : nullptr;
    if (bWantsFront && !Front)
    {
        WarnUnknown(TEXT("front"), Call.DefenseFront);
    }
    if (bWantsShell && !Shell)
    {
        WarnUnknown(TEXT("shell"), Call.DefenseShell);
    }
    Result.bFrontFound = Front != nullptr;
    Result.bShellFound = Shell != nullptr;
    if (Front || Shell)
    {
        // What the defense reads: the offense given, or the one lined up here.
        TArray<FPSAlignedPlayer> Read;
        if (Offense)
        {
            Read = *Offense;
        }
        else
        {
            for (int32 Index = 0; Index < Roles.Num(); ++Index)
            {
                if (IsOffenseRole(Roles[Index]))
                {
                    FPSAlignedPlayer& Player = Read.AddDefaulted_GetRef();
                    Player.Role = Roles[Index];
                    Player.Location = Result.Spots[Index];
                }
            }
        }
        Result.StrongSide = PSPlayRecognition::ClassifyFormation(Read, Ball, Recognition).StrongSide >= 0 ? 1 : -1;
        const float StrongSide = static_cast<float>(Result.StrongSide);

        FLineReference Line;
        Line.BallY = Ball.Y;
        Line.Spacing = Spacing;
        TArray<FReceiverSpot> StrongReceivers;
        TArray<FReceiverSpot> WeakReceivers;
        for (const FPSAlignedPlayer& Player : Read)
        {
            if (Player.Role == EPlayerRole::OffensiveLineman)
            {
                Line.LinemenY.Add(Player.Location.Y);
            }
            else if (Player.Role == EPlayerRole::WideReceiver || Player.Role == EPlayerRole::TightEnd)
            {
                // On the ball counts right, as the classifier counts it.
                const float Across = Player.Location.Y - Ball.Y;
                FReceiverSpot Spot;
                Spot.Y = Player.Location.Y;
                ((Across >= 0.f ? 1.f : -1.f) == StrongSide ? StrongReceivers : WeakReceivers).Add(Spot);
            }
        }
        Line.LinemenY.Sort();
        float Nearest = TNumericLimits<float>::Max();
        for (int32 Index = 0; Index < Line.LinemenY.Num(); ++Index)
        {
            if (FMath::Abs(Line.LinemenY[Index] - Ball.Y) < Nearest)
            {
                Nearest = FMath::Abs(Line.LinemenY[Index] - Ball.Y);
                Line.CenterIndex = Index;
            }
        }
        auto Outside = [&Ball](const FReceiverSpot& A, const FReceiverSpot& B) { return FMath::Abs(A.Y - Ball.Y) > FMath::Abs(B.Y - Ball.Y); };
        StrongReceivers.Sort(Outside);
        WeakReceivers.Sort(Outside);
        const bool bAnyReceiver = StrongReceivers.Num() + WeakReceivers.Num() > 0;

        TMap<EPlayerRole, int32> Seen;
        for (int32 Index = 0; Index < Roles.Num(); ++Index)
        {
            const EPlayerRole Role = Roles[Index];
            if (IsOffenseRole(Role))
            {
                continue;
            }
            const int32 RoleIndex = Seen.FindOrAdd(Role)++;
            // A shell places the backs it has slots for; the front the rest.
            const FPSFormationSpawnPoint* Slot = (Shell && Role == EPlayerRole::DefensiveBack) ? FindSlot(Shell->Slots, Role, RoleIndex) : nullptr;
            if (!Slot && Front)
            {
                Slot = FindSlot(Front->Slots, Role, RoleIndex);
            }
            if (!Slot || (Slot->OverReceiver > 0 && !bAnyReceiver))
            {
                ++Result.RoleFallbacks;
                continue;
            }

            const float Side = SideSign(*Slot, StrongSide);
            float ReferenceY = Ball.Y;
            if (!Slot->Technique.IsNone())
            {
                const FPSTechniqueDef* Technique = Catalog.Techniques.FindByPredicate([Slot](const FPSTechniqueDef& Def) { return Def.Technique == Slot->Technique; });
                if (Technique)
                {
                    ReferenceY = Line.LinemanY(Technique->Lineman, Side) + Side * Technique->Shade * ToCm(Catalog.ShadeYards);
                }
            }
            else if (Slot->OverReceiver > 0)
            {
                TArray<FReceiverSpot>& Mine = Side == StrongSide ? StrongReceivers : WeakReceivers;
                TArray<FReceiverSpot>& Other = Side == StrongSide ? WeakReceivers : StrongReceivers;
                FReceiverSpot* Receiver = Mine.IsValidIndex(Slot->OverReceiver - 1) ? &Mine[Slot->OverReceiver - 1] : nullptr;
                // His side ran out: the other side's widest receiver nobody has yet.
                for (int32 OtherIndex = 0; !Receiver && OtherIndex < Other.Num(); ++OtherIndex)
                {
                    Receiver = Other[OtherIndex].bCovered ? nullptr : &Other[OtherIndex];
                }
                if (Receiver)
                {
                    Receiver->bCovered = true;
                    ReferenceY = Receiver->Y;
                }
                else
                {
                    ReferenceY = Line.LinemanY(3, Side);
                }
            }
            Result.Spots[Index].X = Ball.X + ToCm(Slot->ScrimmageYardOffset);
            Result.Spots[Index].Y = ReferenceY + Side * ToCm(Slot->LateralYardOffset);
        }
    }

    // On the field: inside the end lines and the sidelines.
    if (Formation || Front || Shell)
    {
        const float Margin = ToCm(Catalog.BoundaryMarginYards);
        const float MinX = PSField::EndLineX(false) + Margin;
        const float MaxX = PSField::EndLineX(true) - Margin;
        const float MaxY = FMath::Max(PSField::SidelineY() - Margin, 0.f);
        for (FVector& Spot : Result.Spots)
        {
            Spot.X = FMath::Clamp(Spot.X, static_cast<double>(MinX), static_cast<double>(MaxX));
            Spot.Y = FMath::Clamp(Spot.Y, static_cast<double>(-MaxY), static_cast<double>(MaxY));
        }
    }
    return Result;
}
