#include "PSPlaybookGenerator.h"
#include "PSDataIngestion.h"
#include "PSCoverageMatchupSubsystem.h"
#include "PSFieldGrid.h"
#include "PSJsonWriting.h"
#include "PSOverlayPlayArtSubsystem.h"
#include "PSPersonnelManager.h"
#include "PSPlayArt.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayResolution.h"
#include "PSPlayerPawn.h"
#include "PSPlaybookIngestion.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"

namespace PSPlaybookGeneratorPrivate
{
    /** The scrimmage categories the coaching AI weighs, per side (FPSPlayDefinition::PlayCategory). */
    static const TArray<FString>& Categories(bool bOffense)
    {
        static const TArray<FString> Offense = { TEXT("Run"), TEXT("ShortPass"), TEXT("DeepPass"), TEXT("PlayAction"), TEXT("Screen") };
        static const TArray<FString> Defense = { TEXT("Base"), TEXT("Blitz"), TEXT("Prevent") };
        return bOffense ? Offense : Defense;
    }

    static bool IsOffenseRole(EPlayerRole Role)
    {
        return Role == EPlayerRole::Quarterback || Role == EPlayerRole::RunningBack || Role == EPlayerRole::WideReceiver
            || Role == EPlayerRole::TightEnd || Role == EPlayerRole::OffensiveLineman;
    }

    static bool IsOffenseKind(EPSAssignmentKind Kind)
    {
        return Kind == EPSAssignmentKind::Route || Kind == EPSAssignmentKind::PassBlock || Kind == EPSAssignmentKind::RunBlock;
    }

    static bool IsRunOption(EPSDeception Deception)
    {
        return Deception == EPSDeception::RPO || Deception == EPSDeception::ZoneRead || Deception == EPSDeception::TripleOption;
    }

    static FString RoleName(EPlayerRole Role)
    {
        return StaticEnum<EPlayerRole>()->GetNameStringByValue(static_cast<int64>(Role));
    }

    static FString KindName(EPSAssignmentKind Kind)
    {
        return StaticEnum<EPSAssignmentKind>()->GetNameStringByValue(static_cast<int64>(Kind));
    }

    static FString DeceptionName(EPSDeception Deception)
    {
        return StaticEnum<EPSDeception>()->GetNameStringByValue(static_cast<int64>(Deception));
    }

    /** An EPlayerRole by its name, or false. */
    static bool ParseRole(const FString& Name, EPlayerRole& OutRole)
    {
        const UEnum* RoleEnum = StaticEnum<EPlayerRole>();
        const int32 Index = RoleEnum->GetIndexByNameString(Name);
        if (Index == INDEX_NONE || Index >= RoleEnum->NumEnums() - 1)
        {
            return false;
        }
        OutRole = static_cast<EPlayerRole>(RoleEnum->GetValueByIndex(Index));
        return true;
    }

    static FPSPlayAssignment MakeAssignment(EPlayerRole Role, EPSAssignmentKind Kind, FName RouteId = NAME_None, const FVector& FormationOffset = FVector::ZeroVector, const FVector& ZoneOffset = FVector::ZeroVector)
    {
        FPSPlayAssignment Assignment;
        Assignment.Role = Role;
        Assignment.Kind = Kind;
        Assignment.RouteId = RouteId;
        Assignment.FormationOffset = FormationOffset;
        Assignment.ZoneOffset = ZoneOffset;
        return Assignment;
    }

    static const TCHAR* DeceptionSuffix(EPSDeception Deception)
    {
        switch (Deception)
        {
        case EPSDeception::PlayAction:
            return TEXT("_PA");
        case EPSDeception::RPO:
            return TEXT("_RPO");
        case EPSDeception::ZoneRead:
            return TEXT("_ZR");
        case EPSDeception::TripleOption:
            return TEXT("_TO");
        default:
            return TEXT("");
        }
    }

    static float MapWeight(const TMap<FString, float>* Weights, const FString& Key)
    {
        const float* Found = Weights ? Weights->Find(Key) : nullptr;
        return Found ? FMath::Max(0.f, *Found) : 1.f;
    }

    /** Count shared out in proportion to Weights, one at a time to the entry furthest below its
     *  due share, each capped at its Capacity (what a full entry can't take goes to the others);
     *  every entry with weight and capacity gets at least one while Count lasts. */
    static TArray<int32> ShareOut(const TArray<float>& Weights, const TArray<int32>& Capacity, int32 Count)
    {
        const int32 Num = Weights.Num();
        TArray<int32> Shares;
        Shares.Init(0, Num);
        int32 Left = Count;
        // One each first, in weight order, so no category with weight goes without.
        TArray<int32> Order;
        for (int32 Index = 0; Index < Num; ++Index)
        {
            Order.Add(Index);
        }
        Order.StableSort([&Weights](int32 A, int32 B) { return Weights[A] > Weights[B]; });
        for (const int32 Index : Order)
        {
            if (Left > 0 && Weights[Index] > 0.f && Capacity[Index] > 0)
            {
                Shares[Index] = 1;
                --Left;
            }
        }
        // Then the rest by weight, a full share's part going to the others.
        while (Left > 0)
        {
            float Open = 0.f;
            for (int32 Index = 0; Index < Num; ++Index)
            {
                Open += Shares[Index] < Capacity[Index] ? Weights[Index] : 0.f;
            }
            if (Open <= 0.f)
            {
                break;
            }
            // The entry furthest below its due share takes the next play.
            int32 Best = INDEX_NONE;
            float BestGap = -MAX_flt;
            for (const int32 Index : Order)
            {
                if (Shares[Index] >= Capacity[Index] || Weights[Index] <= 0.f)
                {
                    continue;
                }
                const float Due = static_cast<float>(Count) * Weights[Index] / Open;
                const float Gap = Due - Shares[Index];
                if (Gap > BestGap)
                {
                    BestGap = Gap;
                    Best = Index;
                }
            }
            if (Best == INDEX_NONE)
            {
                break;
            }
            ++Shares[Best];
            --Left;
        }
        return Shares;
    }
}

FString PSPlaybookGenerator::Code(const FString& Text)
{
    FString Out;
    for (const TCHAR Char : Text)
    {
        if (FChar::IsAlnum(Char))
        {
            Out.AppendChar(Char);
        }
    }
    return Out;
}

TMap<EPlayerRole, int32> PSPlaybookGenerator::GetFormationRoles(const FPSPersonnelCatalog& InPersonnel, const FString& Formation, bool bOffense)
{
    TMap<EPlayerRole, int32> Roles;
    UPSPersonnelManager* Manager = NewObject<UPSPersonnelManager>();
    Manager->SetCatalog(InPersonnel);
    const FPSPersonnelPackage* Package = Manager->FindPackage(Manager->GetPackageForFormation(Formation, bOffense));
    // The manager falls back to the side's default package; a formation must be listed.
    if (!Package || Package->bOffense != bOffense || !Package->Formations.Contains(Formation))
    {
        return Roles;
    }
    for (const TPair<FString, int32>& Count : Package->RoleCounts)
    {
        EPlayerRole Role = EPlayerRole::Quarterback;
        if (Count.Value > 0 && PSPlaybookGeneratorPrivate::ParseRole(Count.Key, Role))
        {
            Roles.FindOrAdd(Role) += Count.Value;
        }
    }
    return Roles;
}

TArray<FPSPlayDefinition> PSPlaybookGenerator::BuildConceptPlays(const FPSPlaybookGeneratorTuning& InTuning, const FPSPlayConcept& Concept, const FString& Formation, const TMap<EPlayerRole, int32>& Roles)
{
    using namespace PSPlaybookGeneratorPrivate;

    TArray<FPSPlayDefinition> Plays;
    if (Concept.ConceptId.IsNone() || Roles.FindRef(EPlayerRole::Quarterback) < 1
        || (Concept.Formations.Num() > 0 && !Concept.Formations.Contains(Formation)))
    {
        return Plays;
    }

    // A run's ball carrier is the first back; then each slot goes to the first receiver of its
    // roles the formation still has.
    const bool bRun = Concept.Category == TEXT("Run");
    TMap<EPlayerRole, int32> Open = Roles;
    if (bRun)
    {
        if (Open.FindRef(EPlayerRole::RunningBack) < 1)
        {
            return Plays;
        }
        Open.FindOrAdd(EPlayerRole::RunningBack) -= 1;
    }
    TArray<EPlayerRole> SlotRoles;
    int32 Variants = 1;
    for (const FPSConceptSlot& Slot : Concept.Slots)
    {
        const EPlayerRole* Taken = Slot.Roles.FindByPredicate([&Open](EPlayerRole Role)
        {
            return Role != EPlayerRole::Quarterback && Role != EPlayerRole::OffensiveLineman && IsOffenseRole(Role) && Open.FindRef(Role) > 0;
        });
        if (!Taken || Slot.Routes.Num() == 0)
        {
            return Plays;
        }
        SlotRoles.Add(*Taken);
        Open.FindOrAdd(*Taken) -= 1;
        Variants *= Slot.Routes.Num();
    }

    TArray<EPSDeception> Deceptions = Concept.Deceptions;
    if (Deceptions.Num() == 0)
    {
        Deceptions.Add(EPSDeception::None);
    }
    const EPlayerRole Receivers[] = { EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::TightEnd };
    for (const EPSDeception Deception : Deceptions)
    {
        const bool bPlayAction = Deception == EPSDeception::PlayAction;
        for (int32 Variant = 0; Variant < Variants; ++Variant)
        {
            // This variant's route for each slot: the variant number read digit by digit.
            TArray<FName> Routes;
            TArray<FString> Picked;
            int32 Rest = Variant;
            for (const FPSConceptSlot& Slot : Concept.Slots)
            {
                Routes.Add(Slot.Routes[Rest % Slot.Routes.Num()]);
                if (Slot.Routes.Num() > 1)
                {
                    Picked.Add(Routes.Last().ToString());
                }
                Rest /= Slot.Routes.Num();
            }

            FPSPlayDefinition Play;
            Play.PlayId = FName(*FString::Printf(TEXT("%s_%s_%d%s"), *Concept.ConceptId.ToString(), *Code(Formation), Variant + 1, DeceptionSuffix(Deception)));
            Play.DisplayName = FString::Printf(TEXT("%s%s%s%s"), bPlayAction ? TEXT("PA ") : TEXT(""), *Concept.Label,
                Deception == EPSDeception::ZoneRead ? TEXT(" Read") : TEXT(""),
                Picked.Num() > 0 ? *FString::Printf(TEXT(" (%s)"), *FString::Join(Picked, TEXT(", "))) : TEXT(""));
            Play.Formation = Formation;
            Play.bIsOffensivePlay = true;
            Play.PlayCategory = bPlayAction ? FString(TEXT("PlayAction")) : Concept.Category;
            Play.Deception.Type = Deception;

            const FVector Drop(bPlayAction ? InTuning.PlayActionDrop : Concept.QBDrop, 0.f, 0.f);
            for (int32 Index = 0; Index < Roles.FindRef(EPlayerRole::Quarterback); ++Index)
            {
                Play.Assignments.Add(MakeAssignment(EPlayerRole::Quarterback, EPSAssignmentKind::Route, NAME_None, Drop));
            }
            for (const EPlayerRole Role : Receivers)
            {
                int32 Given = 0;
                if (bRun && Role == EPlayerRole::RunningBack)
                {
                    Play.Assignments.Add(MakeAssignment(Role, EPSAssignmentKind::Route, NAME_None, Concept.BackSpot));
                    ++Given;
                }
                for (int32 SlotIndex = 0; SlotIndex < SlotRoles.Num(); ++SlotIndex)
                {
                    if (SlotRoles[SlotIndex] == Role)
                    {
                        // The slots are the quarterback's progression, so the art ranks them (Epic 27).
                        FPSPlayAssignment& Slotted = Play.Assignments.Add_GetRef(MakeAssignment(Role, EPSAssignmentKind::Route, Routes[SlotIndex]));
                        Slotted.ReadOrder = SlotIndex + 1;
                        ++Given;
                    }
                }
                for (; Given < Roles.FindRef(Role); ++Given)
                {
                    const bool bBackside = Role == EPlayerRole::WideReceiver && !Concept.BacksideRoute.IsNone();
                    Play.Assignments.Add(bBackside ? MakeAssignment(Role, EPSAssignmentKind::Route, Concept.BacksideRoute) : MakeAssignment(Role, Concept.LineKind));
                }
            }
            for (int32 Index = 0; Index < Roles.FindRef(EPlayerRole::OffensiveLineman); ++Index)
            {
                Play.Assignments.Add(MakeAssignment(EPlayerRole::OffensiveLineman, Concept.LineKind));
            }
            Plays.Add(Play);
        }
    }
    return Plays;
}

TArray<FPSPlayDefinition> PSPlaybookGenerator::BuildDefensiveCalls(const FPSPlaybookGeneratorTuning& InTuning, const FPSDefensiveFrontDef& Front, const TMap<EPlayerRole, int32>& Roles)
{
    using namespace PSPlaybookGeneratorPrivate;

    TArray<FPSPlayDefinition> Plays;
    const EPlayerRole Defenders[] = { EPlayerRole::DefensiveLineman, EPlayerRole::Linebacker, EPlayerRole::DefensiveBack };
    for (const FPSCoverageTemplate& Coverage : InTuning.Coverages)
    {
        for (const FPSPressureDef& Pressure : InTuning.Pressures)
        {
            // The pressure has to fit the coverage and the front's personnel.
            TMap<EPlayerRole, int32> Blitzing;
            int32 Blitzers = 0;
            bool bFits = true;
            for (const TPair<FString, int32>& Entry : Pressure.Blitzers)
            {
                EPlayerRole Role = EPlayerRole::Quarterback;
                if (!ParseRole(Entry.Key, Role) || Entry.Value < 0 || Roles.FindRef(Role) < Entry.Value)
                {
                    bFits = false;
                    break;
                }
                Blitzing.FindOrAdd(Role) += Entry.Value;
                Blitzers += Entry.Value;
            }
            if (!bFits || Blitzers > Coverage.MaxBlitzers)
            {
                continue;
            }

            FPSPlayDefinition Play;
            Play.PlayId = FName(*FString::Printf(TEXT("Def_%s_%s_%s"), *Code(Front.Formation), *Code(Coverage.Shell), *Pressure.PressureId.ToString()));
            Play.DisplayName = Pressure.Label.IsEmpty()
                ? FString::Printf(TEXT("%s %s"), *Front.Front, *Coverage.Label)
                : FString::Printf(TEXT("%s %s %s"), *Front.Front, *Coverage.Label, *Pressure.Label);
            Play.Formation = Front.Formation;
            Play.bIsOffensivePlay = false;
            Play.PlayCategory = Blitzers > 0 ? FString(TEXT("Blitz")) : Coverage.Category;
            Play.Front = Front.Front;
            Play.CoverageShell = Coverage.Shell;
            for (const EPlayerRole Role : Defenders)
            {
                const int32 Count = Roles.FindRef(Role);
                const int32 Sent = FMath::Min(Blitzing.FindRef(Role), Count);
                for (int32 Index = 0; Index < Sent; ++Index)
                {
                    Play.Assignments.Add(MakeAssignment(Role, EPSAssignmentKind::Blitz));
                }
                TArray<const FPSCoverageSlotDef*> Jobs;
                for (const FPSCoverageSlotDef& Job : Coverage.Slots)
                {
                    if (Job.Role == Role)
                    {
                        Jobs.Add(&Job);
                    }
                }
                for (int32 Index = 0; Index < Count - Sent; ++Index)
                {
                    if (Jobs.Num() == 0)
                    {
                        // No coverage job for the role: a lineman plays the front, anyone else man.
                        Play.Assignments.Add(MakeAssignment(Role, Role == EPlayerRole::DefensiveLineman ? Front.LineKind : EPSAssignmentKind::ManCoverage));
                        continue;
                    }
                    const FPSCoverageSlotDef& Job = *Jobs[FMath::Min(Index, Jobs.Num() - 1)];
                    const FVector Zone = Job.Kind == EPSAssignmentKind::ZoneCoverage ? Job.Zone : FVector::ZeroVector;
                    Play.Assignments.Add(MakeAssignment(Role, Job.Kind, NAME_None, FVector::ZeroVector, Zone));
                }
            }
            Plays.Add(Play);
        }
    }
    return Plays;
}

TArray<FString> PSPlaybookGenerator::ValidatePlay(const FPSPlayDefinition& Play, const UDataTable* InRouteLibrary, const FPSPersonnelCatalog& InPersonnel)
{
    using namespace PSPlaybookGeneratorPrivate;

    TArray<FString> Problems;
    const bool bOffense = Play.bIsOffensivePlay;
    const TCHAR* Side = bOffense ? TEXT("offensive") : TEXT("defensive");
    if (Play.PlayId.IsNone() || Play.DisplayName.TrimStartAndEnd().IsEmpty())
    {
        Problems.Add(TEXT("needs a PlayId and a DisplayName"));
    }
    if (!Categories(bOffense).Contains(Play.PlayCategory))
    {
        Problems.Add(FString::Printf(TEXT("PlayCategory '%s' is not a %s scrimmage category (%s)"), *Play.PlayCategory, Side, *FString::Join(Categories(bOffense), TEXT(", "))));
    }
    if (bOffense && (!Play.Front.IsEmpty() || !Play.CoverageShell.IsEmpty()))
    {
        Problems.Add(TEXT("Front and CoverageShell are for defensive plays only"));
    }
    if (!bOffense && (Play.Front.IsEmpty() || Play.CoverageShell.IsEmpty()))
    {
        Problems.Add(TEXT("a defensive call needs a Front and a CoverageShell"));
    }

    TMap<EPlayerRole, int32> Slots;
    TArray<int32> Reads;
    for (int32 Index = 0; Index < Play.Assignments.Num(); ++Index)
    {
        const FPSPlayAssignment& Assignment = Play.Assignments[Index];
        const FString Where = FString::Printf(TEXT("Assignments[%d] %s"), Index, *RoleName(Assignment.Role));
        if (IsOffenseRole(Assignment.Role) != bOffense)
        {
            Problems.Add(FString::Printf(TEXT("%s: not a %s role"), *Where, Side));
        }
        if (IsOffenseKind(Assignment.Kind) != bOffense)
        {
            Problems.Add(FString::Printf(TEXT("%s: %s is not a %s assignment"), *Where, *KindName(Assignment.Kind), Side));
        }
        if (!Assignment.RouteId.IsNone())
        {
            if (Assignment.Kind != EPSAssignmentKind::Route)
            {
                Problems.Add(FString::Printf(TEXT("%s: only a Route runs a RouteId"), *Where));
            }
            else if (InRouteLibrary && !InRouteLibrary->FindRowUnchecked(Assignment.RouteId))
            {
                Problems.Add(FString::Printf(TEXT("%s: route '%s' is not in the route library"), *Where, *Assignment.RouteId.ToString()));
            }
        }
        if (Assignment.ReadOrder < 0 || (Assignment.ReadOrder > 0 && (Assignment.Kind != EPSAssignmentKind::Route || Assignment.RouteId.IsNone())))
        {
            Problems.Add(FString::Printf(TEXT("%s: only a route with a RouteId is read, from 1"), *Where));
        }
        else if (Assignment.ReadOrder > 0)
        {
            Reads.Add(Assignment.ReadOrder);
        }
        Slots.FindOrAdd(Assignment.Role) += 1;
    }
    Reads.Sort();
    for (int32 Index = 0; Index < Reads.Num(); ++Index)
    {
        if (Reads[Index] != Index + 1)
        {
            Problems.Add(TEXT("ReadOrder values must run 1, 2, 3, ... with no gap or repeat"));
            break;
        }
    }

    // Every player the formation puts on the field has exactly one job.
    const TMap<EPlayerRole, int32> Roles = GetFormationRoles(InPersonnel, Play.Formation, bOffense);
    if (Roles.Num() == 0)
    {
        Problems.Add(FString::Printf(TEXT("Formation '%s' is in no %s personnel package"), *Play.Formation, Side));
    }
    else
    {
        for (const TPair<EPlayerRole, int32>& Count : Roles)
        {
            if (Slots.FindRef(Count.Key) != Count.Value)
            {
                Problems.Add(FString::Printf(TEXT("%d %s assignment(s) for the formation's %d"), Slots.FindRef(Count.Key), *RoleName(Count.Key), Count.Value));
            }
        }
        for (const TPair<EPlayerRole, int32>& Count : Slots)
        {
            if (!Roles.Contains(Count.Key))
            {
                Problems.Add(FString::Printf(TEXT("the formation has no %s for its assignment"), *RoleName(Count.Key)));
            }
        }
    }

    auto HasRoute = [&Play](EPlayerRole Role, bool bNeedsRouteId)
    {
        return Play.Assignments.ContainsByPredicate([Role, bNeedsRouteId](const FPSPlayAssignment& Assignment)
        {
            return Assignment.Role == Role && Assignment.Kind == EPSAssignmentKind::Route && (!bNeedsRouteId || !Assignment.RouteId.IsNone());
        });
    };
    if (bOffense)
    {
        const bool bRun = Play.PlayCategory == TEXT("Run");
        if (bRun && !HasRoute(EPlayerRole::RunningBack, false))
        {
            Problems.Add(TEXT("a run needs a RunningBack on a Route to carry it"));
        }
        const bool bAnyRoute = Play.Assignments.ContainsByPredicate([](const FPSPlayAssignment& Assignment) { return !Assignment.RouteId.IsNone(); });
        if (!bRun && !bAnyRoute)
        {
            Problems.Add(TEXT("a pass needs a receiver on a route"));
        }
    }

    // Epic 72's deception rules (content_contracts.validate_deception).
    const FPSDeceptionDef& Deception = Play.Deception;
    if (Deception.Type != EPSDeception::None)
    {
        const FString Type = DeceptionName(Deception.Type);
        if (!bOffense)
        {
            Problems.Add(TEXT("only offensive plays deceive"));
        }
        if (IsRunOption(Deception.Type) && Play.PlayCategory != TEXT("Run"))
        {
            Problems.Add(FString::Printf(TEXT("%s is a run option, so the play is a Run"), *Type));
        }
        if (Deception.Type == EPSDeception::PlayAction && (Play.PlayCategory == TEXT("Run") || Play.PlayCategory == TEXT("Screen")))
        {
            Problems.Add(TEXT("play-action fakes a run to pass, not on a Run or a Screen"));
        }
        if (Deception.PlaySide != 1 && Deception.PlaySide != -1)
        {
            Problems.Add(TEXT("Deception.PlaySide: 1 (right) or -1 (left)"));
        }
        if (IsRunOption(Deception.Type) && !HasRoute(EPlayerRole::RunningBack, false))
        {
            Problems.Add(TEXT("a run option needs a RunningBack on a Route (to the mesh)"));
        }
        if (Deception.Type == EPSDeception::RPO && !HasRoute(Deception.PassRole, true))
        {
            Problems.Add(FString::Printf(TEXT("the RPO's pass option, a %s, runs no route"), *RoleName(Deception.PassRole)));
        }
        if (Deception.Type == EPSDeception::TripleOption
            && (Deception.PitchRole == EPlayerRole::Quarterback || Deception.PitchRole == EPlayerRole::RunningBack || !Slots.Contains(Deception.PitchRole)))
        {
            Problems.Add(TEXT("the triple option's pitch man is on the field and neither the quarterback nor the back"));
        }
    }
    return Problems;
}

TArray<FString> PSPlaybookGenerator::ValidateTuning(const FPSPlaybookGeneratorTuning& InTuning, const UDataTable* InRouteLibrary, const FPSPersonnelCatalog& InPersonnel)
{
    using namespace PSPlaybookGeneratorPrivate;

    TArray<FString> Problems;
    auto CheckRoute = [&Problems, InRouteLibrary](const FString& Where, FName RouteId)
    {
        if (RouteId.IsNone() || (InRouteLibrary && !InRouteLibrary->FindRowUnchecked(RouteId)))
        {
            Problems.Add(FString::Printf(TEXT("%s: route '%s' is not in the route library"), *Where, *RouteId.ToString()));
        }
    };
    auto IsId = [](const FString& Id)
    {
        return !Id.IsEmpty() && Code(Id) == Id;
    };

    if (InTuning.OffenseFormations.Num() == 0)
    {
        Problems.Add(TEXT("OffenseFormations: at least one"));
    }
    for (const FString& Formation : InTuning.OffenseFormations)
    {
        if (GetFormationRoles(InPersonnel, Formation, true).Num() == 0)
        {
            Problems.Add(FString::Printf(TEXT("OffenseFormations: '%s' is in no offensive personnel package"), *Formation));
        }
    }
    if (InTuning.OffensePlaybookSize < 1 || InTuning.DefensePlaybookSize < 1 || InTuning.CategoryEmphasis < 0.f)
    {
        Problems.Add(TEXT("OffensePlaybookSize and DefensePlaybookSize 1 or more, CategoryEmphasis 0 or more"));
    }
    if (InTuning.PlayActionDrop >= 0.f)
    {
        Problems.Add(TEXT("PlayActionDrop: behind the line (below 0)"));
    }

    TSet<FName> ConceptIds;
    for (const FPSPlayConcept& Concept : InTuning.Concepts)
    {
        const FString Where = FString::Printf(TEXT("Concepts '%s'"), *Concept.ConceptId.ToString());
        if (!IsId(Concept.ConceptId.ToString()) || ConceptIds.Contains(Concept.ConceptId))
        {
            Problems.Add(FString::Printf(TEXT("%s: the ConceptId is empty, not letters and digits, or used twice"), *Where));
        }
        ConceptIds.Add(Concept.ConceptId);
        if (Concept.Label.TrimStartAndEnd().IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("%s: needs a Label"), *Where));
        }
        const bool bRun = Concept.Category == TEXT("Run");
        const bool bPass = Concept.Category == TEXT("ShortPass") || Concept.Category == TEXT("DeepPass");
        if (!bRun && !bPass && Concept.Category != TEXT("Screen"))
        {
            Problems.Add(FString::Printf(TEXT("%s: Category '%s' is not Run, ShortPass, DeepPass or Screen"), *Where, *Concept.Category));
        }
        if (Concept.QBDrop >= 0.f)
        {
            Problems.Add(FString::Printf(TEXT("%s: QBDrop is behind the line (below 0)"), *Where));
        }
        if (Concept.LineKind != EPSAssignmentKind::PassBlock && Concept.LineKind != EPSAssignmentKind::RunBlock)
        {
            Problems.Add(FString::Printf(TEXT("%s: LineKind is PassBlock or RunBlock"), *Where));
        }
        for (const FString& Formation : Concept.Formations)
        {
            if (!InTuning.OffenseFormations.Contains(Formation))
            {
                Problems.Add(FString::Printf(TEXT("%s: formation '%s' is not in OffenseFormations"), *Where, *Formation));
            }
        }
        int32 Variants = 1;
        for (int32 Index = 0; Index < Concept.Slots.Num(); ++Index)
        {
            const FPSConceptSlot& Slot = Concept.Slots[Index];
            const FString SlotWhere = FString::Printf(TEXT("%s.Slots[%d]"), *Where, Index);
            if (Slot.Roles.Num() == 0 || Slot.Routes.Num() == 0)
            {
                Problems.Add(FString::Printf(TEXT("%s: needs Roles and Routes"), *SlotWhere));
            }
            for (const EPlayerRole Role : Slot.Roles)
            {
                if (Role != EPlayerRole::RunningBack && Role != EPlayerRole::WideReceiver && Role != EPlayerRole::TightEnd)
                {
                    Problems.Add(FString::Printf(TEXT("%s: %s is not a receiver"), *SlotWhere, *RoleName(Role)));
                }
            }
            for (const FName& RouteId : Slot.Routes)
            {
                CheckRoute(SlotWhere, RouteId);
            }
            Variants *= FMath::Max(1, Slot.Routes.Num());
        }
        if (Variants > 64)
        {
            Problems.Add(FString::Printf(TEXT("%s: %d route variants; at most 64"), *Where, Variants));
        }
        if (!Concept.BacksideRoute.IsNone())
        {
            CheckRoute(Where + TEXT(".BacksideRoute"), Concept.BacksideRoute);
        }
        for (const EPSDeception Deception : Concept.Deceptions)
        {
            const bool bFits = Deception == EPSDeception::None
                || (Deception == EPSDeception::PlayAction && bPass)
                || ((Deception == EPSDeception::ZoneRead || Deception == EPSDeception::RPO) && bRun);
            if (!bFits)
            {
                Problems.Add(FString::Printf(TEXT("%s: %s doesn't go with a %s concept (play-action on a pass, ZoneRead or RPO on a run)"), *Where, *DeceptionName(Deception), *Concept.Category));
            }
            if (Deception == EPSDeception::RPO && Concept.BacksideRoute.IsNone())
            {
                Problems.Add(FString::Printf(TEXT("%s: an RPO needs a BacksideRoute for its pass option"), *Where));
            }
        }
        bool bFitsAFormation = false;
        for (const FString& Formation : InTuning.OffenseFormations)
        {
            bFitsAFormation |= BuildConceptPlays(InTuning, Concept, Formation, GetFormationRoles(InPersonnel, Formation, true)).Num() > 0;
        }
        if (!bFitsAFormation)
        {
            Problems.Add(FString::Printf(TEXT("%s: its slots fit none of its formations"), *Where));
        }
    }

    for (const FPSDefensiveFrontDef& Front : InTuning.DefensiveFronts)
    {
        const FString Where = FString::Printf(TEXT("DefensiveFronts '%s'"), *Front.Formation);
        if (GetFormationRoles(InPersonnel, Front.Formation, false).Num() == 0)
        {
            Problems.Add(FString::Printf(TEXT("%s: in no defensive personnel package"), *Where));
        }
        if (Front.Front.TrimStartAndEnd().IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("%s: needs a Front"), *Where));
        }
        if (Front.LineKind != EPSAssignmentKind::PassRush && Front.LineKind != EPSAssignmentKind::RunFit)
        {
            Problems.Add(FString::Printf(TEXT("%s: LineKind is PassRush or RunFit"), *Where));
        }
    }
    TSet<FString> Shells;
    for (const FPSCoverageTemplate& Coverage : InTuning.Coverages)
    {
        const FString Where = FString::Printf(TEXT("Coverages '%s'"), *Coverage.Shell);
        if (!IsId(Coverage.Shell) || Shells.Contains(Coverage.Shell))
        {
            Problems.Add(FString::Printf(TEXT("%s: the Shell is empty, not letters and digits, or used twice"), *Where));
        }
        Shells.Add(Coverage.Shell);
        if (Coverage.Category != TEXT("Base") && Coverage.Category != TEXT("Prevent"))
        {
            Problems.Add(FString::Printf(TEXT("%s: Category is Base or Prevent"), *Where));
        }
        if (Coverage.MaxBlitzers < 0 || Coverage.Label.TrimStartAndEnd().IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("%s: needs a Label, and MaxBlitzers 0 or more"), *Where));
        }
        for (const EPlayerRole Role : { EPlayerRole::Linebacker, EPlayerRole::DefensiveBack })
        {
            if (!Coverage.Slots.ContainsByPredicate([Role](const FPSCoverageSlotDef& Job) { return Job.Role == Role; }))
            {
                Problems.Add(FString::Printf(TEXT("%s: no job for a %s"), *Where, *RoleName(Role)));
            }
        }
        for (const FPSCoverageSlotDef& Job : Coverage.Slots)
        {
            if (IsOffenseRole(Job.Role) || (Job.Kind != EPSAssignmentKind::ZoneCoverage && Job.Kind != EPSAssignmentKind::ManCoverage))
            {
                Problems.Add(FString::Printf(TEXT("%s: a job is a defender's ZoneCoverage or ManCoverage"), *Where));
            }
        }
    }
    TSet<FName> PressureIds;
    bool bAnyBase = false;
    for (const FPSPressureDef& Pressure : InTuning.Pressures)
    {
        const FString Where = FString::Printf(TEXT("Pressures '%s'"), *Pressure.PressureId.ToString());
        if (!IsId(Pressure.PressureId.ToString()) || PressureIds.Contains(Pressure.PressureId))
        {
            Problems.Add(FString::Printf(TEXT("%s: the PressureId is empty, not letters and digits, or used twice"), *Where));
        }
        PressureIds.Add(Pressure.PressureId);
        int32 Blitzers = 0;
        for (const TPair<FString, int32>& Entry : Pressure.Blitzers)
        {
            EPlayerRole Role = EPlayerRole::Quarterback;
            if (!ParseRole(Entry.Key, Role) || IsOffenseRole(Role) || Entry.Value < 0)
            {
                Problems.Add(FString::Printf(TEXT("%s: '%s' is not a defensive role, or its count is below 0"), *Where, *Entry.Key));
            }
            Blitzers += Entry.Value;
        }
        bAnyBase |= Blitzers == 0;
    }
    if (!bAnyBase)
    {
        Problems.Add(TEXT("Pressures: one must send nobody, so coverages have a base call"));
    }

    TSet<FName> Flavors;
    for (const FPSSchemeFlavor& Flavor : InTuning.SchemeFlavors)
    {
        const FString Where = FString::Printf(TEXT("SchemeFlavors '%s'"), *Flavor.SchemeId.ToString());
        if (Flavor.SchemeId.IsNone() || Flavors.Contains(Flavor.SchemeId))
        {
            Problems.Add(FString::Printf(TEXT("%s: the SchemeId is empty or used twice"), *Where));
        }
        Flavors.Add(Flavor.SchemeId);
        for (const TPair<FString, float>& Weight : Flavor.ConceptWeights)
        {
            if (!ConceptIds.Contains(FName(*Weight.Key)) || Weight.Value < 0.f)
            {
                Problems.Add(FString::Printf(TEXT("%s.ConceptWeights: '%s' is no concept, or its weight is below 0"), *Where, *Weight.Key));
            }
        }
        for (const TPair<FString, float>& Weight : Flavor.ShellWeights)
        {
            if (!Shells.Contains(Weight.Key) || Weight.Value < 0.f)
            {
                Problems.Add(FString::Printf(TEXT("%s.ShellWeights: '%s' is no coverage, or its weight is below 0"), *Where, *Weight.Key));
            }
        }
        for (const TPair<FString, float>& Weight : Flavor.PressureWeights)
        {
            if (!PressureIds.Contains(FName(*Weight.Key)) || Weight.Value < 0.f)
            {
                Problems.Add(FString::Printf(TEXT("%s.PressureWeights: '%s' is no pressure, or its weight is below 0"), *Where, *Weight.Key));
            }
        }
    }
    return Problems;
}

FString PSPlaybookGenerator::PlayJson(const FPSPlayDefinition& Play)
{
    using namespace PSPlaybookGeneratorPrivate;
    using PSJsonWriting::Quote;

    TArray<FString> Assignments;
    for (const FPSPlayAssignment& Assignment : Play.Assignments)
    {
        FString Row = FString::Printf(TEXT("{ \"Role\": %s, \"Kind\": %s"), *Quote(RoleName(Assignment.Role)), *Quote(KindName(Assignment.Kind)));
        if (!Assignment.RouteId.IsNone())
        {
            Row += FString::Printf(TEXT(", \"RouteId\": %s"), *Quote(Assignment.RouteId.ToString()));
        }
        if (!Assignment.ZoneOffset.IsZero())
        {
            Row += FString::Printf(TEXT(", \"ZoneOffset\": %s"), *PSJsonWriting::Vector(Assignment.ZoneOffset));
        }
        if (!Assignment.FormationOffset.IsZero())
        {
            Row += FString::Printf(TEXT(", \"FormationOffset\": %s"), *PSJsonWriting::Vector(Assignment.FormationOffset));
        }
        if (Assignment.ReadOrder > 0)
        {
            Row += FString::Printf(TEXT(", \"ReadOrder\": %d"), Assignment.ReadOrder);
        }
        Assignments.Add(Row + TEXT(" }"));
    }

    FString Json = FString::Printf(TEXT("{ \"PlayId\": %s, \"DisplayName\": %s, \"Formation\": %s, \"bIsOffensivePlay\": %s, \"PlayCategory\": %s"),
        *Quote(Play.PlayId.ToString()), *Quote(Play.DisplayName), *Quote(Play.Formation), Play.bIsOffensivePlay ? TEXT("true") : TEXT("false"), *Quote(Play.PlayCategory));
    if (!Play.bIsOffensivePlay)
    {
        Json += FString::Printf(TEXT(", \"Front\": %s, \"CoverageShell\": %s"), *Quote(Play.Front), *Quote(Play.CoverageShell));
    }
    Json += FString::Printf(TEXT(", \"Assignments\": [ %s ]"), *FString::Join(Assignments, TEXT(", ")));
    const FPSDeceptionDef& Deception = Play.Deception;
    if (Deception.Type != EPSDeception::None)
    {
        Json += FString::Printf(TEXT(", \"Deception\": { \"Type\": %s, \"PlaySide\": %d"), *Quote(DeceptionName(Deception.Type)), Deception.PlaySide);
        if (Deception.Type == EPSDeception::RPO)
        {
            Json += FString::Printf(TEXT(", \"PassRole\": %s"), *Quote(RoleName(Deception.PassRole)));
        }
        if (Deception.Type == EPSDeception::TripleOption)
        {
            Json += FString::Printf(TEXT(", \"PitchRole\": %s"), *Quote(RoleName(Deception.PitchRole)));
        }
        Json += TEXT(" }");
    }
    return Json + TEXT(" }");
}

FString UPSPlaybookGenerator::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/playbook_generator.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSPlaybookGenerator::LoadTuningFromJson(const FString& JsonFilePath)
{
    FPSPlaybookGeneratorTuning Loaded;
    if (!NewObject<UPSDataIngestion>(this)->LoadPlaybookGeneratorTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlaybookGenerator: Could not load the generator tuning from %s."), *JsonFilePath);
        return false;
    }
    SetTuning(Loaded);
    return true;
}

const FPSPlaybookGeneratorTuning& UPSPlaybookGenerator::GetTuning()
{
    EnsureLoaded();
    return Tuning;
}

void UPSPlaybookGenerator::SetTuning(const FPSPlaybookGeneratorTuning& InTuning)
{
    Tuning = InTuning;
    bTuningLoaded = true;
}

void UPSPlaybookGenerator::SetPersonnel(const FPSPersonnelCatalog& InPersonnel)
{
    Personnel = InPersonnel;
    bPersonnelSet = true;
}

const FPSPersonnelCatalog& UPSPlaybookGenerator::GetPersonnel()
{
    EnsureLoaded();
    return Personnel;
}

void UPSPlaybookGenerator::SetRouteLibrary(UDataTable* InRoutes)
{
    RouteLibrary = InRoutes;
}

UDataTable* UPSPlaybookGenerator::GetRouteLibrary()
{
    EnsureLoaded();
    return RouteLibrary;
}

void UPSPlaybookGenerator::EnsureLoaded()
{
    if (!bTuningLoaded)
    {
        bTuningLoaded = true;
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    if (!bPersonnelSet)
    {
        bPersonnelSet = true;
        if (!NewObject<UPSDataIngestion>(this)->LoadPersonnelCatalogFromJson(UPSPersonnelManager::GetDefaultCatalogPath(), Personnel))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSPlaybookGenerator: Could not load the personnel packages; no formation fits."));
        }
    }
    if (!RouteLibrary)
    {
        RouteLibrary = NewObject<UDataTable>(this);
        RouteLibrary->RowStruct = FPSRoute::StaticStruct();
        if (!NewObject<UPSPlaybookIngestion>(this)->LoadRoutesFromJson(UPSPlayCallSubsystem::GetDefaultRoutesPath(), RouteLibrary))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSPlaybookGenerator: Could not load the route library."));
        }
    }
}

TArray<PSPlaybookGenerator::FPSLibraryPlay> UPSPlaybookGenerator::BuildEntries(bool bOffense, const TArray<FString>& Formations)
{
    EnsureLoaded();
    TArray<PSPlaybookGenerator::FPSLibraryPlay> Entries;
    if (bOffense)
    {
        for (const FString& Formation : Tuning.OffenseFormations)
        {
            if (Formations.Num() > 0 && !Formations.Contains(Formation))
            {
                continue;
            }
            const TMap<EPlayerRole, int32> Roles = PSPlaybookGenerator::GetFormationRoles(Personnel, Formation, true);
            for (const FPSPlayConcept& Concept : Tuning.Concepts)
            {
                for (const FPSPlayDefinition& Play : PSPlaybookGenerator::BuildConceptPlays(Tuning, Concept, Formation, Roles))
                {
                    PSPlaybookGenerator::FPSLibraryPlay& Entry = Entries.AddDefaulted_GetRef();
                    Entry.Play = Play;
                    Entry.ConceptId = Concept.ConceptId.ToString();
                }
            }
        }
        return Entries;
    }
    for (const FPSDefensiveFrontDef& Front : Tuning.DefensiveFronts)
    {
        if (Formations.Num() > 0 && !Formations.Contains(Front.Formation))
        {
            continue;
        }
        const TMap<EPlayerRole, int32> Roles = PSPlaybookGenerator::GetFormationRoles(Personnel, Front.Formation, false);
        for (const FPSPlayDefinition& Play : PSPlaybookGenerator::BuildDefensiveCalls(Tuning, Front, Roles))
        {
            PSPlaybookGenerator::FPSLibraryPlay& Entry = Entries.AddDefaulted_GetRef();
            Entry.Play = Play;
            Entry.Shell = Play.CoverageShell;
            // Def_<Formation>_<Shell>_<PressureId>: the PressureId is the last part.
            FString Head;
            Play.PlayId.ToString().Split(TEXT("_"), &Head, &Entry.PressureId, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        }
    }
    return Entries;
}

TArray<FPSPlayDefinition> UPSPlaybookGenerator::BuildLibrary(bool bOffense, const TArray<FString>& Formations)
{
    TArray<FPSPlayDefinition> Plays;
    for (const PSPlaybookGenerator::FPSLibraryPlay& Entry : BuildEntries(bOffense, Formations))
    {
        Plays.Add(Entry.Play);
    }
    return Plays;
}

FPSGeneratedPlaybook UPSPlaybookGenerator::GeneratePlaybook(int32 Seed, const FPSSchemeDef& Scheme)
{
    using namespace PSPlaybookGeneratorPrivate;

    FPSGeneratedPlaybook Book;
    Book.SchemeId = Scheme.SchemeId;
    Book.bOffense = Scheme.bOffense;
    EnsureLoaded();
    const TArray<FString> Problems = PSPlaybookGenerator::ValidateTuning(Tuning, RouteLibrary, Personnel);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlaybookGenerator: %s"), *Problem);
    }
    if (Problems.Num() > 0)
    {
        return Book;
    }

    const TArray<PSPlaybookGenerator::FPSLibraryPlay> Entries = BuildEntries(Scheme.bOffense, Scheme.Formations);
    const FPSSchemeFlavor* Flavor = Tuning.FindFlavor(Scheme.SchemeId);
    const TArray<FString>& SideCategories = Categories(Scheme.bOffense);

    // The book shared out by the scheme's category weights, raised to CategoryEmphasis.
    TArray<float> Weights;
    TArray<int32> Capacity;
    for (const FString& Category : SideCategories)
    {
        Weights.Add(FMath::Pow(MapWeight(&Scheme.CategoryWeights, Category), FMath::Max(0.f, Tuning.CategoryEmphasis)));
        Capacity.Add(Entries.FilterByPredicate([&Category](const PSPlaybookGenerator::FPSLibraryPlay& Entry) { return Entry.Play.PlayCategory == Category; }).Num());
    }
    const TArray<int32> Shares = ShareOut(Weights, Capacity, Scheme.bOffense ? Tuning.OffensePlaybookSize : Tuning.DefensePlaybookSize);

    // Inside a category, the flavor's concepts (or shells and pressures), each sharing its weight
    // among its plays, drawn without replacement.
    FRandomStream Stream(Seed ^ static_cast<int32>(FCrc::StrCrc32(*Scheme.SchemeId.ToString())));
    for (int32 CategoryIndex = 0; CategoryIndex < SideCategories.Num(); ++CategoryIndex)
    {
        TArray<int32> Candidates;
        TMap<FString, int32> GroupSizes;
        for (int32 Index = 0; Index < Entries.Num(); ++Index)
        {
            const PSPlaybookGenerator::FPSLibraryPlay& Entry = Entries[Index];
            if (Entry.Play.PlayCategory == SideCategories[CategoryIndex])
            {
                Candidates.Add(Index);
                GroupSizes.FindOrAdd(Entry.ConceptId + TEXT("/") + Entry.Shell + TEXT("/") + Entry.PressureId) += 1;
            }
        }
        TArray<float> CandidateWeights;
        for (const int32 Index : Candidates)
        {
            const PSPlaybookGenerator::FPSLibraryPlay& Entry = Entries[Index];
            const float Liking = Scheme.bOffense
                ? MapWeight(Flavor ? &Flavor->ConceptWeights : nullptr, Entry.ConceptId)
                : MapWeight(Flavor ? &Flavor->ShellWeights : nullptr, Entry.Shell) * MapWeight(Flavor ? &Flavor->PressureWeights : nullptr, Entry.PressureId);
            CandidateWeights.Add(Liking / FMath::Max(1, GroupSizes.FindRef(Entry.ConceptId + TEXT("/") + Entry.Shell + TEXT("/") + Entry.PressureId)));
        }
        for (int32 Pick = 0; Pick < Shares[CategoryIndex]; ++Pick)
        {
            float Total = 0.f;
            for (const float Weight : CandidateWeights)
            {
                Total += Weight;
            }
            if (Total <= 0.f)
            {
                break;
            }
            float Roll = Stream.FRand() * Total;
            int32 Chosen = CandidateWeights.Num() - 1;
            for (int32 Index = 0; Index < CandidateWeights.Num(); ++Index)
            {
                Roll -= CandidateWeights[Index];
                if (Roll < 0.f && CandidateWeights[Index] > 0.f)
                {
                    Chosen = Index;
                    break;
                }
            }
            while (Chosen > 0 && CandidateWeights[Chosen] <= 0.f)
            {
                --Chosen;
            }
            FPSPlayDefinition Play = Entries[Candidates[Chosen]].Play;
            Play.PlayId = FName(*FString::Printf(TEXT("%s_%s"), *Scheme.SchemeId.ToString(), *Play.PlayId.ToString()));
            Book.Plays.Add(Play);
            CandidateWeights[Chosen] = 0.f;
        }
    }
    return Book;
}

TArray<FString> UPSPlaybookGenerator::ValidatePlays(const TArray<FPSPlayDefinition>& Plays)
{
    EnsureLoaded();
    TArray<FString> Problems;
    for (const FPSPlayDefinition& Play : Plays)
    {
        for (const FString& Problem : PSPlaybookGenerator::ValidatePlay(Play, RouteLibrary, Personnel))
        {
            Problems.Add(FString::Printf(TEXT("%s: %s"), *Play.PlayId.ToString(), *Problem));
        }
    }
    return Problems;
}

TArray<FString> UPSPlaybookGenerator::ValidatePlayArt(UWorld* World, const TArray<FPSPlayDefinition>& Plays)
{
    TArray<FString> Problems;
    UPSOverlayPlayArtSubsystem* Overlay = World ? World->GetSubsystem<UPSOverlayPlayArtSubsystem>() : nullptr;
    if (!Overlay)
    {
        Problems.Add(TEXT("no play-art overlay in the world to check the art with"));
        return Problems;
    }
    EnsureLoaded();
    const UPSCoverageMatchupSubsystem* Matchups = World->GetSubsystem<UPSCoverageMatchupSubsystem>();
    const FPSPlayArtStyle& Style = Overlay->GetStyle();
    const float BreakAngle = Overlay->GetBreakMinAngleDegrees();
    const FVector Line = FVector::ZeroVector;

    // A defense lines up against the default offensive package's first formation.
    UPSPersonnelManager* PersonnelLookup = NewObject<UPSPersonnelManager>(this);
    PersonnelLookup->SetCatalog(Personnel);
    const FPSPersonnelPackage* BaseOffense = PersonnelLookup->FindPackage(Personnel.DefaultOffensePackage);
    const FString OpposingFormation = BaseOffense && BaseOffense->Formations.Num() > 0 ? BaseOffense->Formations[0] : FString();

    // Each formation's players, lined up once (in role order, as the game mode lines them up).
    TArray<APSPlayerPawn*> Spawned;
    TMap<FString, TArray<APSPlayerPawn*>> Lineups;
    auto LineUp = [this, World, &Spawned, &Lineups, &Line](const FString& Formation, bool bOffense)
    {
        const FString Key = FString::Printf(TEXT("%s/%s"), bOffense ? TEXT("O") : TEXT("D"), *Formation);
        if (const TArray<APSPlayerPawn*>* Known = Lineups.Find(Key))
        {
            return *Known;
        }
        const TMap<EPlayerRole, int32> Counts = PSPlaybookGenerator::GetFormationRoles(Personnel, Formation, bOffense);
        TArray<EPlayerRole> Roles;
        const UEnum* RoleEnum = StaticEnum<EPlayerRole>();
        for (int32 Index = 0; Index < RoleEnum->NumEnums() - 1; ++Index)
        {
            const EPlayerRole Role = static_cast<EPlayerRole>(RoleEnum->GetValueByIndex(Index));
            for (int32 Count = 0; Count < Counts.FindRef(Role); ++Count)
            {
                Roles.Add(Role);
            }
        }
        const TArray<FVector> Spots = APSFieldGrid::ComputeLineup(Roles, static_cast<float>(Line.X));
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        TArray<APSPlayerPawn*> Players;
        for (int32 Index = 0; Index < Roles.Num() && Index < Spots.Num(); ++Index)
        {
            APSPlayerPawn* Pawn = World->SpawnActor<APSPlayerPawn>(APSPlayerPawn::StaticClass(), Spots[Index], FRotator::ZeroRotator, SpawnParams);
            if (!Pawn)
            {
                continue;
            }
            FPlayerAttributes Attributes;
            Attributes.PlayerId = FName(*FString::Printf(TEXT("ArtCheck_%s_%d"), *PSPlaybookGenerator::Code(Key), Index));
            Attributes.DisplayName = Attributes.PlayerId.ToString();
            Attributes.Role = Roles[Index];
            Pawn->InitializePlayer(Attributes);
            Players.Add(Pawn);
            Spawned.Add(Pawn);
        }
        Lineups.Add(Key, Players);
        return Players;
    };

    for (const FPSPlayDefinition& Play : Plays)
    {
        const FString Name = Play.PlayId.ToString();
        TArray<APSPlayerPawn*> Field = LineUp(Play.Formation, Play.bIsOffensivePlay);
        if (Field.Num() == 0)
        {
            Problems.Add(FString::Printf(TEXT("%s: formation '%s' lines nobody up"), *Name, *Play.Formation));
            continue;
        }
        if (!Play.bIsOffensivePlay)
        {
            Field.Append(LineUp(OpposingFormation, true));
        }
        TArray<EPlayerRole> Roles;
        for (const APSPlayerPawn* Pawn : Field)
        {
            Roles.Add(Pawn->GetAttributes().Role);
        }

        // Resolved and compiled as the snap and the overlay do.
        TArray<FPSResolvedAssignment> Resolved = PSPlayResolution::ResolvePlay(Play, Field, RouteLibrary, Line, false);
        if (!Play.bIsOffensivePlay)
        {
            PSPlayResolution::ResolveManMatchups(Resolved, Field, Roles, Matchups);
        }
        const TArray<FPSPlayArtPrimitive> Art = PSPlayArt::CompilePlayArt(Play, Resolved, RouteLibrary, Style, BreakAngle, Line, Matchups);
        for (const FString& Problem : PSPlayArt::ValidatePlayArt(Play, Resolved, Art, RouteLibrary, Style))
        {
            Problems.Add(FString::Printf(TEXT("%s: %s"), *Name, *Problem));
        }
        if (Art.Num() == 0 && !PSPlayArt::DrawsNoArt(Play, Style))
        {
            Problems.Add(FString::Printf(TEXT("%s: a %s play draws no art"), *Name, *Play.PlayCategory));
        }
    }

    for (APSPlayerPawn* Pawn : Spawned)
    {
        if (AController* Controller = Pawn ? Pawn->GetController() : nullptr)
        {
            Controller->Destroy();
        }
        if (Pawn)
        {
            Pawn->Destroy();
        }
    }
    return Problems;
}

bool UPSPlaybookGenerator::WritePlaybook(const TArray<FPSPlayDefinition>& Plays, const FString& FilePath)
{
    TArray<FString> Rows;
    for (const FPSPlayDefinition& Play : Plays)
    {
        Rows.Add(PSPlaybookGenerator::PlayJson(Play));
    }
    return PSJsonWriting::SaveText(FilePath, PSJsonWriting::ArrayFile(TEXT("Plays"), Rows));
}
