#include "PSDraft.h"
#include "PSContractData.h"
#include "PSContractManager.h"
#include "PSContractNegotiation.h"
#include "PSDataIngestion.h"
#include "PSFranchiseSaveGame.h"
#include "PSFreeAgency.h"
#include "PSLeagueGenerator.h"
#include "PSRoster.h"
#include "PSStaffData.h"
#include "Math/RandomStream.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"

namespace PSDraftPrivate
{
    /** A seed for one draw of the draft's that is the same in every run: Kind, then who and what. */
    int32 DraftSeed(int32 Seed, const TCHAR* Kind, FName First, FName Second, int32 Index)
    {
        return static_cast<int32>(FCrc::StrCrc32(*FString::Printf(TEXT("%d:%s:%s:%s:%d"), Seed, Kind, *First.ToString(), *Second.ToString(), Index)));
    }

    /** Minus or plus Amount, at even odds. */
    float Swing(FRandomStream& Stream, float Amount)
    {
        return (Stream.GetFraction() < 0.5f ? -1.f : 1.f) * Amount;
    }

    bool IsFraction(float Value)
    {
        return Value >= 0.f && Value <= 1.f;
    }

    FString RoleName(EPlayerRole Role)
    {
        return StaticEnum<EPlayerRole>()->GetNameStringByValue(static_cast<int64>(Role));
    }
}

FString UPSDraft::GetDefaultTuningPath()
{
    return FPaths::ProjectDir() / TEXT("Data/draft.json");
}

bool UPSDraft::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSDraftTuning Loaded;
    if (!Ingestion->LoadDraftTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDraft: Could not load the draft tuning from %s."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDraft: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSDraft::ValidateTuning(const FPSDraftTuning& InTuning)
{
    using namespace PSDraftPrivate;

    TArray<FString> Problems;
    if (InTuning.NumRounds < 1 || InTuning.RookieYears < 1 || InTuning.AIScoutTargets < 1)
    {
        Problems.Add(TEXT("NumRounds, RookieYears and AIScoutTargets must be at least 1"));
    }
    if (InTuning.PublicUncertainty <= 0.f || InTuning.ReportNoise <= 0.f || InTuning.ReportCost <= 0.f || InTuning.RangeSigmas <= 0.f || InTuning.RookieScaleExponent <= 0.f)
    {
        Problems.Add(TEXT("PublicUncertainty, ReportNoise, ReportCost, RangeSigmas and RookieScaleExponent must be above 0"));
    }
    if (InTuning.CombineCertainty <= 0.f || InTuning.CombineCertainty > 1.f)
    {
        Problems.Add(TEXT("CombineCertainty must be above 0 and at most 1"));
    }
    for (const float Fraction : { InTuning.ProDayShare, InTuning.BoomBustChance, InTuning.MisleadChance, InTuning.FundingFloor, InTuning.FirstPickGuarantee, InTuning.LastPickGuarantee })
    {
        if (!IsFraction(Fraction))
        {
            Problems.Add(TEXT("ProDayShare, BoomBustChance, MisleadChance, FundingFloor and the pick guarantees are 0-1"));
            break;
        }
    }
    if (InTuning.BoomBustSwing < 0.f || InTuning.MisleadSwing < 0.f || InTuning.PointsPerSeason < 0.f || InTuning.NeedWeight < 0.f || InTuning.FirstPickSalary < 0)
    {
        Problems.Add(TEXT("BoomBustSwing, MisleadSwing, PointsPerSeason, NeedWeight and FirstPickSalary must be 0 or more"));
    }
    if (InTuning.MaxFundingMultiplier < 1.f)
    {
        Problems.Add(TEXT("MaxFundingMultiplier must be at least 1"));
    }
    TSet<FName> Seen;
    FPlayerAttributes Probe;
    for (const FPSCombineDrill& Drill : InTuning.CombineDrills)
    {
        float Rating = 0.f;
        if (Drill.DrillId.IsNone() || Seen.Contains(Drill.DrillId))
        {
            Problems.Add(FString::Printf(TEXT("CombineDrills: empty or duplicate drill '%s'"), *Drill.DrillId.ToString()));
        }
        Seen.Add(Drill.DrillId);
        if (!PSStaff::GetAttribute(Probe, Drill.Attribute, Rating))
        {
            Problems.Add(FString::Printf(TEXT("CombineDrills: %s reads '%s', not a rating"), *Drill.DrillId.ToString(), *Drill.Attribute.ToString()));
        }
        if (Drill.Noise < 0.f || Drill.PerPoint == 0.f)
        {
            Problems.Add(FString::Printf(TEXT("CombineDrills: %s needs a PerPoint other than 0 and a Noise of 0 or more"), *Drill.DrillId.ToString()));
        }
    }
    return Problems;
}

bool UPSDraft::PrepareClass(const FPSDraftClass& Class, int32 Seed)
{
    using namespace PSDraftPrivate;

    if (Class.Prospects.Num() == 0)
    {
        return false;
    }
    // Picks traded for this draft or a later one keep their new owners (Epic 88).
    TArray<FPSDraftPickRight> Traded = State.TradedPicks;
    const int32 ClassYear = Class.DraftYear;
    Traded.RemoveAll([ClassYear](const FPSDraftPickRight& Right) { return Right.DraftYear < ClassYear; });
    State = FPSDraftState();
    State.Seed = Seed;
    State.DraftYear = Class.DraftYear;
    State.TradedPicks = MoveTemp(Traded);
    for (const FPlayerAttributes& Player : Class.Prospects)
    {
        FPSProspect& Prospect = State.Prospects.AddDefaulted_GetRef();
        Prospect.Player = Player;
        Prospect.TrueGrade = UPSContractNegotiation::RatePlayer(Player);

        // The combine, or a pro day: each drill reads a true rating.
        FRandomStream Stream(DraftSeed(Seed, TEXT("Prospect"), Player.PlayerId, NAME_None, 0));
        Prospect.bAttendedCombine = Stream.GetFraction() >= Tuning.ProDayShare;
        for (const FPSCombineDrill& Drill : Tuning.CombineDrills)
        {
            float Rating = 0.f;
            PSStaff::GetAttribute(Player, Drill.Attribute, Rating);
            FPSCombineResult& Result = Prospect.Measurables.AddDefaulted_GetRef();
            Result.DrillId = Drill.DrillId;
            Result.Value = Drill.Base + Drill.PerPoint * Rating + Drill.Noise * PSLeagueGenerator::StandardNormal(Stream);
        }

        // The public projection's hidden error: surer after the combine; the busts and booms.
        Prospect.PublicUncertainty = Tuning.PublicUncertainty * (Prospect.bAttendedCombine ? Tuning.CombineCertainty : 1.f);
        float Error = Prospect.PublicUncertainty * PSLeagueGenerator::StandardNormal(Stream);
        if (Stream.GetFraction() < Tuning.BoomBustChance)
        {
            Error += Swing(Stream, Tuning.BoomBustSwing);
        }
        Prospect.Projection = Prospect.TrueGrade + Error;
    }
    return true;
}

void UPSDraft::RegisterTeam(FName TeamId, UPSRoster* Roster, bool bUserControlled, float ScoutingFundingIndex)
{
    if (TeamId.IsNone())
    {
        return;
    }
    if (Roster)
    {
        Rosters.Add(TeamId, Roster);
    }
    FPSTeamScouting* Team = FindMutableTeam(TeamId);
    if (!Team)
    {
        Team = &State.Teams.AddDefaulted_GetRef();
        Team->TeamId = TeamId;
        Team->FundingIndex = ScoutingFundingIndex;
        Team->PointsTotal = Tuning.PointsPerSeason * GetFundingMultiplier(ScoutingFundingIndex);
        Team->Points = Team->PointsTotal;
    }
    Team->bUserControlled = bUserControlled;
}

float UPSDraft::GetFundingMultiplier(float FundingIndex) const
{
    return FMath::Min(Tuning.FundingFloor + (1.f - Tuning.FundingFloor) * FMath::Max(FundingIndex, 0.f), Tuning.MaxFundingMultiplier);
}

bool UPSDraft::Scout(FName TeamId, FName PlayerId)
{
    using namespace PSDraftPrivate;

    FPSTeamScouting* Team = FindMutableTeam(TeamId);
    const FPSProspect* Prospect = FindProspect(PlayerId);
    if (!Team || !Prospect || !Prospect->DraftedBy.IsNone() || State.bComplete || Tuning.ReportCost <= 0.f || Team->Points < Tuning.ReportCost)
    {
        return false;
    }
    FPSScoutingRecord* Record = Team->Records.FindByPredicate([PlayerId](const FPSScoutingRecord& Candidate) { return Candidate.PlayerId == PlayerId; });
    if (!Record)
    {
        Record = &Team->Records.AddDefaulted_GetRef();
        Record->PlayerId = PlayerId;
    }

    // A read of his true grade, and now and then a scout who has him all wrong.
    FRandomStream Stream(DraftSeed(State.Seed, TEXT("Report"), TeamId, PlayerId, Record->Reports));
    float Read = Prospect->TrueGrade + Tuning.ReportNoise * PSLeagueGenerator::StandardNormal(Stream);
    if (Stream.GetFraction() < Tuning.MisleadChance)
    {
        Read += Swing(Stream, Tuning.MisleadSwing);
    }
    ++Record->Reports;
    Record->SumReads += Read;
    Team->Points -= Tuning.ReportCost;
    return true;
}

int32 UPSDraft::AutoScout(FName TeamId)
{
    // The best-projected prospects still on the board.
    TArray<const FPSProspect*> Targets;
    for (const FPSProspect& Prospect : State.Prospects)
    {
        if (Prospect.DraftedBy.IsNone())
        {
            Targets.Add(&Prospect);
        }
    }
    Targets.StableSort([](const FPSProspect& A, const FPSProspect& B) { return A.Projection > B.Projection; });
    if (Targets.Num() > Tuning.AIScoutTargets)
    {
        Targets.SetNum(Tuning.AIScoutTargets);
    }
    TArray<FName> TargetIds;
    for (const FPSProspect* Target : Targets)
    {
        TargetIds.Add(Target->Player.PlayerId);
    }

    // A report on each in turn, until the points run out.
    int32 Filed = 0;
    bool bFiled = TargetIds.Num() > 0;
    while (bFiled)
    {
        bFiled = false;
        for (const FName& PlayerId : TargetIds)
        {
            if (Scout(TeamId, PlayerId))
            {
                ++Filed;
                bFiled = true;
            }
        }
    }
    return Filed;
}

void UPSDraft::EstimateGrade(FName TeamId, const FPSProspect& Prospect, float& OutEstimate, float& OutUncertainty) const
{
    // The projection and the reports, each weighed by how sure it is.
    const float PriorVariance = FMath::Square(FMath::Max(Prospect.PublicUncertainty, 0.01f));
    const float ReportVariance = FMath::Square(FMath::Max(Tuning.ReportNoise, 0.01f));
    float Precision = 1.f / PriorVariance;
    float Weighted = Prospect.Projection / PriorVariance;
    const FPSTeamScouting* Team = FindTeam(TeamId);
    const FName PlayerId = Prospect.Player.PlayerId;
    const FPSScoutingRecord* Record = Team ? Team->Records.FindByPredicate([PlayerId](const FPSScoutingRecord& Candidate) { return Candidate.PlayerId == PlayerId; }) : nullptr;
    if (Record && Record->Reports > 0)
    {
        Precision += Record->Reports / ReportVariance;
        Weighted += Record->SumReads / ReportVariance;
    }
    OutEstimate = Weighted / Precision;
    OutUncertainty = 1.f / FMath::Sqrt(Precision);
}

FPSProspectView UPSDraft::GetProspectView(FName TeamId, FName PlayerId) const
{
    FPSProspectView View;
    const FPSProspect* Prospect = FindProspect(PlayerId);
    if (!Prospect)
    {
        return View;
    }
    const FPlayerAttributes& Player = Prospect->Player;
    View.PlayerId = Player.PlayerId;
    View.DisplayName = Player.DisplayName;
    View.Role = Player.Role;
    View.Age = Player.Age;
    View.WeightKg = Player.WeightKg;
    View.HeightCm = Player.HeightCm;
    View.DNA = Player.DNA;
    View.Projection = Prospect->Projection;
    float Uncertainty = 0.f;
    EstimateGrade(TeamId, *Prospect, View.Estimate, Uncertainty);
    View.RangeLow = View.Estimate - Tuning.RangeSigmas * Uncertainty;
    View.RangeHigh = View.Estimate + Tuning.RangeSigmas * Uncertainty;
    const FPSTeamScouting* Team = FindTeam(TeamId);
    const FPSScoutingRecord* Record = Team ? Team->Records.FindByPredicate([PlayerId](const FPSScoutingRecord& Candidate) { return Candidate.PlayerId == PlayerId; }) : nullptr;
    View.Reports = Record ? Record->Reports : 0;
    View.bAttendedCombine = Prospect->bAttendedCombine;
    if (Prospect->bAttendedCombine || View.Reports > 0)
    {
        View.Measurables = Prospect->Measurables;
    }
    View.DraftedBy = Prospect->DraftedBy;
    View.OverallPick = Prospect->OverallPick;
    return View;
}

TArray<FPSProspectView> UPSDraft::GetBoard(FName TeamId) const
{
    TArray<FPSProspectView> Board;
    for (const FPSProspect& Prospect : State.Prospects)
    {
        if (Prospect.DraftedBy.IsNone())
        {
            Board.Add(GetProspectView(TeamId, Prospect.Player.PlayerId));
        }
    }
    Board.StableSort([](const FPSProspectView& A, const FPSProspectView& B) { return A.Estimate > B.Estimate; });
    return Board;
}

bool UPSDraft::GetTeamScouting(FName TeamId, FPSTeamScouting& OutTeam) const
{
    const FPSTeamScouting* Team = FindTeam(TeamId);
    if (Team)
    {
        OutTeam = *Team;
    }
    return Team != nullptr;
}

bool UPSDraft::BeginDraft(const TArray<FName>& FirstRoundOrder, UPSContractManager* InContracts, UPSFreeAgency* InFreeAgency)
{
    if (State.bOpen || State.bComplete || State.Prospects.Num() == 0)
    {
        return false;
    }
    TArray<FName> Round;
    for (const FName& TeamId : FirstRoundOrder)
    {
        if (FindTeam(TeamId) && !Round.Contains(TeamId))
        {
            Round.Add(TeamId);
        }
    }
    if (Round.Num() == 0)
    {
        return false;
    }

    Contracts = InContracts;
    FreeAgency = InFreeAgency;
    State.Order.Reset();
    State.OriginalOrder.Reset();
    State.Picks.Reset();
    const int32 NumPicks = FMath::Min(Tuning.NumRounds * Round.Num(), State.Prospects.Num());
    for (int32 Pick = 0; Pick < NumPicks; ++Pick)
    {
        // Each pick goes to the team holding it: its own, or the one it was traded to (Epic 88).
        const FName Original = Round[Pick % Round.Num()];
        const FName Owner = GetPickOwner(State.DraftYear, Pick / Round.Num() + 1, Original);
        State.OriginalOrder.Add(Original);
        State.Order.Add(FindTeam(Owner) ? Owner : Original);
    }
    State.bOpen = true;

    // The CPU's scouts spend what they have left.
    TArray<FName> CpuTeams;
    for (const FPSTeamScouting& Team : State.Teams)
    {
        if (!Team.bUserControlled)
        {
            CpuTeams.Add(Team.TeamId);
        }
    }
    for (const FName& TeamId : CpuTeams)
    {
        AutoScout(TeamId);
    }
    return true;
}

FName UPSDraft::GetTeamOnTheClock() const
{
    return State.bOpen && State.Order.IsValidIndex(State.Picks.Num()) ? State.Order[State.Picks.Num()] : NAME_None;
}

int32 UPSDraft::GetCurrentPick() const
{
    return State.bOpen && State.Order.IsValidIndex(State.Picks.Num()) ? State.Picks.Num() + 1 : 0;
}

float UPSDraft::GetNeed(FName TeamId, EPlayerRole Role) const
{
    const FPSPositionMarket* Market = Contracts ? Contracts->GetTuning().FindMarket(Role) : nullptr;
    if (!Market || Market->RosterTarget <= 0)
    {
        return 0.f;
    }
    UPSRoster* const* Roster = Rosters.Find(TeamId);
    int32 Count = 0;
    if (Roster && *Roster)
    {
        for (const FPlayerAttributes& Player : (*Roster)->GetFullRoster())
        {
            Count += Player.Role == Role ? 1 : 0;
        }
    }
    return FMath::Clamp(static_cast<float>(Market->RosterTarget - Count) / Market->RosterTarget, 0.f, 1.f);
}

float UPSDraft::GetPickValue(FName TeamId, FName PlayerId) const
{
    const FPSProspect* Prospect = FindProspect(PlayerId);
    if (!Prospect)
    {
        return 0.f;
    }
    float Estimate = 0.f;
    float Uncertainty = 0.f;
    EstimateGrade(TeamId, *Prospect, Estimate, Uncertainty);
    return Estimate + Tuning.NeedWeight * GetNeed(TeamId, Prospect->Player.Role);
}

FName UPSDraft::ChooseProspect(FName TeamId) const
{
    FName Best;
    float BestValue = 0.f;
    for (const FPSProspect& Prospect : State.Prospects)
    {
        if (!Prospect.DraftedBy.IsNone())
        {
            continue;
        }
        const float Value = GetPickValue(TeamId, Prospect.Player.PlayerId);
        if (Best.IsNone() || Value > BestValue)
        {
            Best = Prospect.Player.PlayerId;
            BestValue = Value;
        }
    }
    return Best;
}

bool UPSDraft::MakePick(FName PlayerId)
{
    const FPSProspect* Prospect = FindProspect(PlayerId);
    if (GetTeamOnTheClock().IsNone() || !Prospect || !Prospect->DraftedBy.IsNone())
    {
        return false;
    }
    TakePick(PlayerId);
    return true;
}

bool UPSDraft::AutoPick()
{
    const FName TeamId = GetTeamOnTheClock();
    if (TeamId.IsNone())
    {
        return false;
    }
    const FName Choice = ChooseProspect(TeamId);
    if (Choice.IsNone())
    {
        // Nobody is left to take.
        FinishDraft();
        return false;
    }
    TakePick(Choice);
    return true;
}

TArray<FPSDraftPick> UPSDraft::AdvanceDraft()
{
    const int32 Before = State.Picks.Num();
    while (State.bOpen)
    {
        const FPSTeamScouting* Team = FindTeam(GetTeamOnTheClock());
        if ((Team && Team->bUserControlled) || !AutoPick())
        {
            break;
        }
    }
    TArray<FPSDraftPick> Made;
    for (int32 Index = Before; Index < State.Picks.Num(); ++Index)
    {
        Made.Add(State.Picks[Index]);
    }
    return Made;
}

TArray<FPSDraftPick> UPSDraft::RunToEnd()
{
    const int32 Before = State.Picks.Num();
    while (State.bOpen && AutoPick())
    {
    }
    TArray<FPSDraftPick> Made;
    for (int32 Index = Before; Index < State.Picks.Num(); ++Index)
    {
        Made.Add(State.Picks[Index]);
    }
    return Made;
}

int32 UPSDraft::GetRookieSalary(int32 Overall, int32 NumPicks, int32 MinimumSalary) const
{
    const float Share = NumPicks > 1 ? FMath::Clamp(static_cast<float>(Overall - 1) / (NumPicks - 1), 0.f, 1.f) : 0.f;
    const float Scale = FMath::Pow(1.f - Share, FMath::Max(Tuning.RookieScaleExponent, KINDA_SMALL_NUMBER));
    return FMath::Max(MinimumSalary, FMath::RoundToInt(MinimumSalary + (Tuning.FirstPickSalary - MinimumSalary) * Scale));
}

float UPSDraft::GetRookieGuarantee(int32 Overall, int32 NumPicks) const
{
    const float Share = NumPicks > 1 ? FMath::Clamp(static_cast<float>(Overall - 1) / (NumPicks - 1), 0.f, 1.f) : 0.f;
    return FMath::Lerp(Tuning.FirstPickGuarantee, Tuning.LastPickGuarantee, Share);
}

void UPSDraft::TakePick(FName PlayerId)
{
    using namespace PSDraftPrivate;

    FPSProspect* Prospect = FindMutableProspect(PlayerId);
    const int32 Overall = State.Picks.Num() + 1;
    if (!Prospect || !State.Order.IsValidIndex(Overall - 1))
    {
        return;
    }
    const FName TeamId = State.Order[Overall - 1];
    const FName OriginalTeamId = State.OriginalOrder.IsValidIndex(Overall - 1) ? State.OriginalOrder[Overall - 1] : TeamId;
    const int32 RoundSize = FMath::Max(1, GetRoundSize());

    float Estimate = 0.f;
    float Uncertainty = 0.f;
    EstimateGrade(TeamId, *Prospect, Estimate, Uncertainty);
    Prospect->DraftedBy = TeamId;
    Prospect->OverallPick = Overall;

    FPSDraftPick Pick;
    Pick.Overall = Overall;
    Pick.Round = (Overall - 1) / RoundSize + 1;
    Pick.PickInRound = (Overall - 1) % RoundSize + 1;
    Pick.TeamId = TeamId;
    Pick.OriginalTeamId = OriginalTeamId;
    Pick.PlayerId = PlayerId;
    Pick.Estimate = Estimate;
    Pick.TrueGrade = Prospect->TrueGrade;

    // He joins the roster as he truly is, on a rookie-scale contract (Epic 87).
    if (UPSRoster* const* Roster = Rosters.Find(TeamId))
    {
        if (*Roster)
        {
            (*Roster)->AddPlayer(Prospect->Player);
        }
    }
    if (Contracts)
    {
        FPSContractOffer Offer;
        Offer.TeamId = TeamId;
        Offer.Years = Tuning.RookieYears;
        Offer.AnnualValue = GetRookieSalary(Overall, State.Order.Num(), Contracts->GetTuning().MinimumSalary);
        Offer.GuaranteedFraction = GetRookieGuarantee(Overall, State.Order.Num());
        Pick.bSigned = Contracts->SignContract(Contracts->MakeContract(PlayerId, Offer)) == EPSCapResult::Ok;
        Pick.AnnualValue = Offer.AnnualValue;
    }
    Pick.Description = FString::Printf(TEXT("Round %d, pick %d (%d overall): %s take %s, %s %s (graded %.0f)%s"), Pick.Round, Pick.PickInRound, Overall,
        *TeamId.ToString(), *Prospect->Player.DisplayName, *RoleName(Prospect->Player.Role), *PlayerId.ToString(), Estimate,
        Contracts && !Pick.bSigned ? TEXT(", unsigned: no cap room") : TEXT(""));
    if (OriginalTeamId != TeamId)
    {
        Pick.Description += FString::Printf(TEXT(" (%s's pick, traded)"), *OriginalTeamId.ToString());
    }
    State.Picks.Add(Pick);

    if (State.Picks.Num() >= State.Order.Num())
    {
        FinishDraft();
    }
}

void UPSDraft::FinishDraft()
{
    if (State.bComplete)
    {
        return;
    }
    State.bOpen = false;
    State.bComplete = true;
    if (FreeAgency && FreeAgency->IsOpen())
    {
        // The undrafted look for a team in free agency (Epic 87).
        for (const FPSProspect& Prospect : State.Prospects)
        {
            if (Prospect.DraftedBy.IsNone())
            {
                FreeAgency->AddFreeAgent(Prospect.Player, Contracts ? Contracts->GetPlayerAge(Prospect.Player) : Prospect.Player.Age, 0.5f, NAME_None);
            }
        }
    }
}

TArray<FString> UPSDraft::DescribeDraft() const
{
    TArray<FString> Lines;
    Lines.Add(FString::Printf(TEXT("Draft %d: %d of %d picks made%s"), State.DraftYear, State.Picks.Num(), State.Order.Num(),
        State.bComplete ? TEXT(", complete") : State.bOpen ? TEXT(", open") : TEXT(", scouting")));
    for (const FPSDraftPick& Pick : State.Picks)
    {
        Lines.Add(Pick.Description);
    }
    return Lines;
}

void UPSDraft::SaveTo(UPSFranchiseSaveGame* Save) const
{
    if (Save)
    {
        Save->Draft = State;
    }
}

bool UPSDraft::LoadFrom(const UPSFranchiseSaveGame* Save)
{
    if (!Save || (Save->Draft.Prospects.Num() == 0 && Save->Draft.TradedPicks.Num() == 0))
    {
        return false;
    }
    State = Save->Draft;
    return true;
}

FName UPSDraft::GetPickOwner(int32 DraftYear, int32 Round, FName OriginalTeamId) const
{
    const FPSDraftPickRight* Right = State.TradedPicks.FindByPredicate([DraftYear, Round, OriginalTeamId](const FPSDraftPickRight& Candidate)
    {
        return Candidate.DraftYear == DraftYear && Candidate.Round == Round && Candidate.OriginalTeamId == OriginalTeamId;
    });
    return Right ? Right->OwnerTeamId : OriginalTeamId;
}

int32 UPSDraft::GetRoundSize() const
{
    const TArray<FName>& Originals = State.OriginalOrder.Num() > 0 ? State.OriginalOrder : State.Order;
    TSet<FName> Teams;
    for (const FName& TeamId : Originals)
    {
        Teams.Add(TeamId);
    }
    return Teams.Num();
}

int32 UPSDraft::FindPickIndex(int32 DraftYear, int32 Round, FName OriginalTeamId) const
{
    if (DraftYear != State.DraftYear || State.Order.Num() == 0 || Round < 1)
    {
        return INDEX_NONE;
    }
    const TArray<FName>& Originals = State.OriginalOrder.Num() == State.Order.Num() ? State.OriginalOrder : State.Order;
    const int32 RoundSize = GetRoundSize();
    const int32 Last = FMath::Min(Round * RoundSize, Originals.Num());
    for (int32 Index = (Round - 1) * RoundSize; Index < Last; ++Index)
    {
        if (Originals[Index] == OriginalTeamId)
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

int32 UPSDraft::GetPickSlot(int32 DraftYear, int32 Round, FName OriginalTeamId) const
{
    const int32 Index = FindPickIndex(DraftYear, Round, OriginalTeamId);
    return Index == INDEX_NONE ? 0 : Index + 1;
}

bool UPSDraft::IsPickAvailable(int32 DraftYear, int32 Round, FName OriginalTeamId) const
{
    if (OriginalTeamId.IsNone() || DraftYear <= 0 || Round < 1 || Round > Tuning.NumRounds)
    {
        return false;
    }
    if (State.DraftYear > 0 && DraftYear < State.DraftYear)
    {
        // A past draft's.
        return false;
    }
    if (DraftYear == State.DraftYear && (State.bOpen || State.bComplete))
    {
        // This draft's order is set: the pick must be in it and not yet made.
        const int32 Index = FindPickIndex(DraftYear, Round, OriginalTeamId);
        return !State.bComplete && Index != INDEX_NONE && Index >= State.Picks.Num();
    }
    return true;
}

bool UPSDraft::TransferPick(int32 DraftYear, int32 Round, FName OriginalTeamId, FName NewOwnerTeamId)
{
    if (NewOwnerTeamId.IsNone() || !IsPickAvailable(DraftYear, Round, OriginalTeamId))
    {
        return false;
    }
    const int32 Index = State.bOpen ? FindPickIndex(DraftYear, Round, OriginalTeamId) : INDEX_NONE;
    if (Index != INDEX_NONE && !FindTeam(NewOwnerTeamId))
    {
        // On the open draft's clock only a team in the draft can pick.
        return false;
    }

    auto IsThisPick = [DraftYear, Round, OriginalTeamId](const FPSDraftPickRight& Candidate)
    {
        return Candidate.DraftYear == DraftYear && Candidate.Round == Round && Candidate.OriginalTeamId == OriginalTeamId;
    };
    if (NewOwnerTeamId == OriginalTeamId)
    {
        // Back with its own team: no longer a traded pick.
        State.TradedPicks.RemoveAll(IsThisPick);
    }
    else if (FPSDraftPickRight* Right = State.TradedPicks.FindByPredicate(IsThisPick))
    {
        Right->OwnerTeamId = NewOwnerTeamId;
    }
    else
    {
        FPSDraftPickRight& Added = State.TradedPicks.AddDefaulted_GetRef();
        Added.DraftYear = DraftYear;
        Added.Round = Round;
        Added.OriginalTeamId = OriginalTeamId;
        Added.OwnerTeamId = NewOwnerTeamId;
    }
    if (Index != INDEX_NONE)
    {
        State.Order[Index] = NewOwnerTeamId;
    }
    return true;
}

TArray<FPSDraftPickRight> UPSDraft::GetTeamPicks(FName TeamId, int32 DraftYear) const
{
    TArray<FPSDraftPickRight> Held;
    if (TeamId.IsNone())
    {
        return Held;
    }
    for (int32 Round = 1; Round <= Tuning.NumRounds; ++Round)
    {
        if (GetPickOwner(DraftYear, Round, TeamId) == TeamId && IsPickAvailable(DraftYear, Round, TeamId))
        {
            FPSDraftPickRight& Own = Held.AddDefaulted_GetRef();
            Own.DraftYear = DraftYear;
            Own.Round = Round;
            Own.OriginalTeamId = TeamId;
            Own.OwnerTeamId = TeamId;
        }
    }
    for (const FPSDraftPickRight& Right : State.TradedPicks)
    {
        if (Right.DraftYear == DraftYear && Right.OwnerTeamId == TeamId && Right.OriginalTeamId != TeamId
            && IsPickAvailable(Right.DraftYear, Right.Round, Right.OriginalTeamId))
        {
            Held.Add(Right);
        }
    }
    return Held;
}

FPSProspect* UPSDraft::FindMutableProspect(FName PlayerId)
{
    return State.Prospects.FindByPredicate([PlayerId](const FPSProspect& Prospect) { return Prospect.Player.PlayerId == PlayerId; });
}

const FPSProspect* UPSDraft::FindProspect(FName PlayerId) const
{
    return State.Prospects.FindByPredicate([PlayerId](const FPSProspect& Prospect) { return Prospect.Player.PlayerId == PlayerId; });
}

FPSTeamScouting* UPSDraft::FindMutableTeam(FName TeamId)
{
    return State.Teams.FindByPredicate([TeamId](const FPSTeamScouting& Team) { return Team.TeamId == TeamId; });
}

const FPSTeamScouting* UPSDraft::FindTeam(FName TeamId) const
{
    return State.Teams.FindByPredicate([TeamId](const FPSTeamScouting& Team) { return Team.TeamId == TeamId; });
}
