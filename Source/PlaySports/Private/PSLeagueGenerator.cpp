#include "PSLeagueGenerator.h"
#include "PSContractNegotiation.h"
#include "PSDataIngestion.h"
#include "PSJsonWriting.h"
#include "PSPlayerAging.h"
#include "PSRoster.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace PSLeagueGeneratorPrivate
{
    using PSJsonWriting::Quote;
    using PSJsonWriting::Number;

    /** Where WriteLeague puts the league config (the file the game and tools/content.py read)
     *  and the teams file the generated config names. */
    const TCHAR* const LeagueConfigFile = TEXT("Data/sample_league_config.json");
    const TCHAR* const TeamsFile = TEXT("Data/league_teams.json");

    /** Ratings run 0-100; the body fields only need to be above 0. */
    static bool IsBodyField(FName Attribute)
    {
        return Attribute == GET_MEMBER_NAME_CHECKED(FPlayerAttributes, WeightKg) || Attribute == GET_MEMBER_NAME_CHECKED(FPlayerAttributes, HeightCm);
    }

    static FFloatProperty* FindAttribute(FName Attribute)
    {
        return Attribute.IsNone() ? nullptr : FindFProperty<FFloatProperty>(FPlayerAttributes::StaticStruct(), Attribute);
    }

    static bool IsHexColor(const FString& Value)
    {
        if (Value.Len() != 7 || Value[0] != TEXT('#'))
        {
            return false;
        }
        for (int32 Index = 1; Index < 7; ++Index)
        {
            if (!FChar::IsHexDigit(Value[Index]))
            {
                return false;
            }
        }
        return true;
    }

    static FString PlayerJson(const FPlayerAttributes& Player)
    {
        const UEnum* RoleEnum = StaticEnum<EPlayerRole>();
        FString Json = FString::Printf(TEXT("{ \"PlayerId\": %s, \"DisplayName\": %s, \"Role\": %s, \"Age\": %d, \"WeightKg\": %s, \"HeightCm\": %s, ")
            TEXT("\"Speed\": %s, \"Agility\": %s, \"Strength\": %s, \"Acceleration\": %s, \"Awareness\": %s, \"Stamina\": %s"),
            *Quote(Player.PlayerId.ToString()), *Quote(Player.DisplayName), *Quote(RoleEnum->GetNameStringByValue(static_cast<int64>(Player.Role))), Player.Age,
            *Number(Player.WeightKg), *Number(Player.HeightCm), *Number(Player.Speed), *Number(Player.Agility), *Number(Player.Strength),
            *Number(Player.Acceleration), *Number(Player.Awareness), *Number(Player.Stamina));

        // Only the axes he has: generated DNA leaves other roles' axes at 0, and a missing axis reads 0.
        TArray<FString> Axes;
        for (TFieldIterator<FFloatProperty> It(FPSPlayerDNA::StaticStruct()); It; ++It)
        {
            const float Value = It->GetPropertyValue_InContainer(&Player.DNA);
            if (Value != 0.f)
            {
                Axes.Add(FString::Printf(TEXT("%s: %s"), *Quote(It->GetName()), *FString::SanitizeFloat(Value)));
            }
        }
        if (Axes.Num() > 0)
        {
            Json += FString::Printf(TEXT(", \"DNA\": { %s }"), *FString::Join(Axes, TEXT(", ")));
        }
        return Json + TEXT(" }");
    }

    static FString TeamJson(const FPSTeamInfo& Team)
    {
        FString Json = FString::Printf(TEXT("{ \"TeamId\": %s, \"DisplayName\": %s, \"Division\": %s, \"RosterDataTablePath\": %s"),
            *Quote(Team.TeamId.ToString()), *Quote(Team.DisplayName), *Quote(Team.Division), *Quote(Team.RosterDataTablePath));
        // The identity fields are optional, but well-formed when present.
        if (!Team.Abbreviation.IsEmpty())
        {
            Json += FString::Printf(TEXT(", \"Abbreviation\": %s"), *Quote(Team.Abbreviation));
        }
        if (!Team.PrimaryColor.IsEmpty())
        {
            Json += FString::Printf(TEXT(", \"PrimaryColor\": %s"), *Quote(Team.PrimaryColor));
        }
        if (!Team.SecondaryColor.IsEmpty())
        {
            Json += FString::Printf(TEXT(", \"SecondaryColor\": %s"), *Quote(Team.SecondaryColor));
        }
        return Json + FString::Printf(TEXT(", \"LogoPath\": %s }"), *Quote(Team.LogoPath));
    }

    static bool WriteDataFile(const FString& RootDir, const FString& RelativePath, const FString& Text, TArray<FString>& OutWrittenFiles)
    {
        if (!PSJsonWriting::SaveText(RootDir / RelativePath, Text))
        {
            return false;
        }
        OutWrittenFiles.Add(RelativePath);
        return true;
    }

    /** Years in the league: k with chance in proportion to (1 - Attrition)^k, from 0 to MaxYears. */
    static int32 DrawExperience(float Attrition, int32 MaxYears, FRandomStream& Stream)
    {
        if (MaxYears <= 0)
        {
            return 0;
        }
        const float Keep = FMath::Clamp(1.f - Attrition, 0.f, 1.f);
        float Total = 0.f;
        float Weight = 1.f;
        for (int32 Years = 0; Years <= MaxYears; ++Years)
        {
            Total += Weight;
            Weight *= Keep;
        }
        float Roll = Stream.GetFraction() * Total;
        Weight = 1.f;
        for (int32 Years = 0; Years < MaxYears; ++Years)
        {
            if (Roll < Weight)
            {
                return Years;
            }
            Roll -= Weight;
            Weight *= Keep;
        }
        return MaxYears;
    }

    /** Each role's share of Count: in proportion to its RosterCount, the remainder to the largest
     *  fractions (the earlier profile on a tie). */
    static TArray<int32> Apportion(const TArray<FPSRoleProfile>& Profiles, int32 Count)
    {
        TArray<int32> Quotas;
        Quotas.Init(0, Profiles.Num());
        int32 RosterTotal = 0;
        for (const FPSRoleProfile& Profile : Profiles)
        {
            RosterTotal += FMath::Max(0, Profile.RosterCount);
        }
        if (RosterTotal <= 0 || Count <= 0)
        {
            return Quotas;
        }
        TArray<TPair<double, int32>> Remainders;
        int32 Given = 0;
        for (int32 Index = 0; Index < Profiles.Num(); ++Index)
        {
            const double Exact = static_cast<double>(Count) * FMath::Max(0, Profiles[Index].RosterCount) / RosterTotal;
            Quotas[Index] = static_cast<int32>(Exact); // not negative, so this is its floor
            Given += Quotas[Index];
            Remainders.Emplace(Exact - Quotas[Index], Index);
        }
        Remainders.StableSort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key > B.Key; });
        for (int32 Index = 0; Given < Count && Index < Remainders.Num(); ++Index, ++Given)
        {
            ++Quotas[Remainders[Index].Value];
        }
        return Quotas;
    }
}

FString PSLeagueGenerator::NormalizeName(const FString& Name)
{
    FString Letters;
    Letters.Reserve(Name.Len());
    for (const TCHAR Char : Name)
    {
        if (FChar::IsAlnum(Char))
        {
            Letters.AppendChar(FChar::ToLower(Char));
        }
        else if (FChar::IsWhitespace(Char) || Char == TEXT('-'))
        {
            Letters.AppendChar(TEXT(' '));
        }
    }
    TArray<FString> Words;
    Letters.ParseIntoArray(Words, TEXT(" "), true);
    return FString::Join(Words, TEXT(" "));
}

TSet<FString> PSLeagueGenerator::MakeBlockedForms(const TArray<FString>& NameBlocklist)
{
    TSet<FString> Forms;
    for (const FString& Entry : NameBlocklist)
    {
        const FString Normalized = NormalizeName(Entry);
        if (Normalized.IsEmpty())
        {
            continue;
        }
        Forms.Add(Normalized);
        int32 Space = INDEX_NONE;
        if (Normalized.FindChar(TEXT(' '), Space))
        {
            Forms.Add(Normalized.Left(1) + Normalized.Mid(Space));
        }
    }
    return Forms;
}

bool PSLeagueGenerator::IsBlockedName(const TSet<FString>& BlockedForms, const FString& Name)
{
    return BlockedForms.Contains(NormalizeName(Name));
}

float PSLeagueGenerator::StandardNormal(FRandomStream& Stream)
{
    const float Radius = FMath::Sqrt(-2.f * FMath::Loge(FMath::Max(Stream.GetFraction(), 1e-7f)));
    return Radius * static_cast<float>(FMath::Cos(UE_TWO_PI * Stream.GetFraction()));
}

FPlayerAttributes PSLeagueGenerator::ApplyCareerArc(const FPlayerAttributes& Prime, int32 Age, const FPSProgressionTuning& Progression)
{
    // The progression model, run on a probe well inside its 0-100 clamp, measures what the
    // offseasons between Age and the prime window add or take from each rating.
    const float ProbeRating = 50.f;
    const int32 MaxYears = 60;
    FPlayerAttributes Probe;
    for (TFieldIterator<FFloatProperty> It(FPlayerAttributes::StaticStruct()); It; ++It)
    {
        It->SetPropertyValue_InContainer(&Probe, ProbeRating);
    }
    const UPSPlayerProgression* Model = GetDefault<UPSPlayerProgression>();
    float Direction = 0.f;
    if (Age < Progression.PeakAgeStart)
    {
        // Still to grow: what the offseasons from now to his prime will add.
        for (int32 Year = Age; Year < Progression.PeakAgeStart && Year < Age + MaxYears; ++Year)
        {
            Model->ApplyOffseasonProgression(Probe, Year, 1.f, Progression);
        }
        Direction = -1.f;
    }
    else if (Age > Progression.PeakAgeEnd)
    {
        // Past his prime: what the offseasons since it have taken.
        for (int32 Year = Progression.PeakAgeEnd + 1; Year <= Age && Year <= Progression.PeakAgeEnd + MaxYears; ++Year)
        {
            Model->ApplyOffseasonProgression(Probe, Year, 1.f, Progression);
        }
        Direction = 1.f;
    }

    FPlayerAttributes Current = Prime;
    for (TFieldIterator<FFloatProperty> It(FPlayerAttributes::StaticStruct()); It; ++It)
    {
        const float Change = It->GetPropertyValue_InContainer(&Probe) - ProbeRating;
        It->SetPropertyValue_InContainer(&Current, It->GetPropertyValue_InContainer(&Current) + Direction * Change);
    }
    return Current;
}

TArray<FString> PSLeagueGenerator::ValidateTuning(const FPSLeagueGeneratorTuning& InTuning)
{
    using namespace PSLeagueGeneratorPrivate;

    TArray<FString> Problems;
    if (InTuning.LeagueName.TrimStartAndEnd().IsEmpty())
    {
        Problems.Add(TEXT("LeagueName: empty"));
    }
    if (InTuning.NumTeams < 2)
    {
        Problems.Add(TEXT("NumTeams: a league needs 2 or more"));
    }
    if (InTuning.NumWeeks < 1)
    {
        Problems.Add(TEXT("NumWeeks: 1 or more"));
    }
    TSet<int32> Byes;
    for (const int32 Week : InTuning.ByeWeekNumbers)
    {
        if (Week < 1 || Week > InTuning.NumWeeks || Byes.Contains(Week))
        {
            Problems.Add(FString::Printf(TEXT("ByeWeekNumbers: week %d is outside the season or listed twice"), Week));
        }
        Byes.Add(Week);
    }
    if (InTuning.NumPlayoffTeams < 2 || InTuning.NumPlayoffTeams > InTuning.NumTeams)
    {
        Problems.Add(TEXT("NumPlayoffTeams: from 2 to NumTeams"));
    }
    TSet<FString> DivisionsSeen;
    if (InTuning.Divisions.Num() == 0)
    {
        Problems.Add(TEXT("Divisions: a league needs at least one"));
    }
    for (const FString& Division : InTuning.Divisions)
    {
        if (Division.TrimStartAndEnd().IsEmpty() || DivisionsSeen.Contains(Division))
        {
            Problems.Add(FString::Printf(TEXT("Divisions: '%s' is empty or listed twice"), *Division));
        }
        DivisionsSeen.Add(Division);
    }
    if (InTuning.PlaceholderTeamName.TrimStartAndEnd().IsEmpty())
    {
        Problems.Add(TEXT("PlaceholderTeamName: empty"));
    }
    for (const FString& Color : InTuning.PlaceholderColors)
    {
        if (!IsHexColor(Color))
        {
            Problems.Add(FString::Printf(TEXT("PlaceholderColors: '%s' is not #RRGGBB"), *Color));
        }
    }
    if (InTuning.TeamTalentSpread < 0.f || InTuning.TalentPerExperienceYear < 0.f || InTuning.MaxExperienceTalent < 0.f)
    {
        Problems.Add(TEXT("TeamTalentSpread, TalentPerExperienceYear and MaxExperienceTalent: 0 or more"));
    }
    if (InTuning.EntryAgeMin < 18 || InTuning.EntryAgeMax < InTuning.EntryAgeMin)
    {
        Problems.Add(TEXT("EntryAgeMin, EntryAgeMax: 18 or more, the minimum first"));
    }

    const UEnum* RoleEnum = StaticEnum<EPlayerRole>();
    TSet<EPlayerRole> RolesSeen;
    for (int32 Index = 0; Index < InTuning.RoleProfiles.Num(); ++Index)
    {
        const FPSRoleProfile& Profile = InTuning.RoleProfiles[Index];
        const FString Where = FString::Printf(TEXT("RoleProfiles[%d] '%s'"), Index, *RoleEnum->GetNameStringByValue(static_cast<int64>(Profile.Role)));
        if (RolesSeen.Contains(Profile.Role))
        {
            Problems.Add(FString::Printf(TEXT("%s: listed twice"), *Where));
        }
        RolesSeen.Add(Profile.Role);
        if (Profile.RosterCount < 1)
        {
            Problems.Add(FString::Printf(TEXT("%s.RosterCount: 1 or more"), *Where));
        }
        bool bCodeOk = !Profile.IdCode.IsEmpty();
        for (const TCHAR Char : Profile.IdCode)
        {
            bCodeOk &= FChar::IsAlnum(Char);
        }
        if (!bCodeOk)
        {
            Problems.Add(FString::Printf(TEXT("%s.IdCode: '%s' must be letters and digits"), *Where, *Profile.IdCode));
        }
        if (Profile.Attrition <= 0.f || Profile.Attrition >= 1.f)
        {
            Problems.Add(FString::Printf(TEXT("%s.Attrition: above 0, below 1"), *Where));
        }
        if (Profile.MaxAge < InTuning.EntryAgeMax || Profile.MaxAge > 50)
        {
            Problems.Add(FString::Printf(TEXT("%s.MaxAge: from EntryAgeMax to 50"), *Where));
        }
        TSet<FName> CurvesSeen;
        for (const FPSAttributeCurve& Curve : Profile.Attributes)
        {
            const FString CurveWhere = FString::Printf(TEXT("%s.Attributes '%s'"), *Where, *Curve.Attribute.ToString());
            if (!FindAttribute(Curve.Attribute))
            {
                Problems.Add(FString::Printf(TEXT("%s: not a float field of FPlayerAttributes"), *CurveWhere));
            }
            if (CurvesSeen.Contains(Curve.Attribute))
            {
                Problems.Add(FString::Printf(TEXT("%s: listed twice"), *CurveWhere));
            }
            CurvesSeen.Add(Curve.Attribute);
            if (Curve.StdDev < 0.f || Curve.Min > Curve.Mean || Curve.Mean > Curve.Max)
            {
                Problems.Add(FString::Printf(TEXT("%s: StdDev 0 or more, and Min <= Mean <= Max"), *CurveWhere));
            }
            if (IsBodyField(Curve.Attribute) ? Curve.Min <= 0.f : (Curve.Min < 0.f || Curve.Max > 100.f))
            {
                Problems.Add(FString::Printf(TEXT("%s: a rating runs 0-100, a body measure above 0"), *CurveWhere));
            }
            if (Curve.TalentWeight < 0.f || Curve.TalentWeight > 1.f)
            {
                Problems.Add(FString::Printf(TEXT("%s.TalentWeight: 0 to 1"), *CurveWhere));
            }
        }
        for (TFieldIterator<FFloatProperty> It(FPlayerAttributes::StaticStruct()); It; ++It)
        {
            if (!CurvesSeen.Contains(It->GetFName()))
            {
                Problems.Add(FString::Printf(TEXT("%s: no curve for %s"), *Where, *It->GetName()));
            }
        }
    }
    for (int32 Value = 0; Value < RoleEnum->NumEnums() - 1; ++Value)
    {
        const EPlayerRole Role = static_cast<EPlayerRole>(RoleEnum->GetValueByIndex(Value));
        if (!RolesSeen.Contains(Role))
        {
            Problems.Add(FString::Printf(TEXT("RoleProfiles: no profile for %s, so rosters would have none"), *RoleEnum->GetNameStringByIndex(Value)));
        }
    }

    if (InTuning.NameCultures.Num() == 0)
    {
        Problems.Add(TEXT("NameCultures: at least one"));
    }
    for (const FPSNameCulture& Culture : InTuning.NameCultures)
    {
        const FString Where = FString::Printf(TEXT("NameCultures '%s'"), *Culture.Culture.ToString());
        if (Culture.Weight <= 0.f)
        {
            Problems.Add(FString::Printf(TEXT("%s.Weight: above 0"), *Where));
        }
        if (Culture.FirstNames.Num() == 0 || Culture.LastNames.Num() == 0)
        {
            Problems.Add(FString::Printf(TEXT("%s: needs first and last names"), *Where));
        }
        for (const TArray<FString>* Names : { &Culture.FirstNames, &Culture.LastNames })
        {
            for (const FString& Name : *Names)
            {
                if (PSLeagueGenerator::NormalizeName(Name).IsEmpty())
                {
                    Problems.Add(FString::Printf(TEXT("%s: '%s' has no letters"), *Where, *Name));
                }
            }
        }
    }
    for (const FString& Entry : InTuning.NameBlocklist)
    {
        if (PSLeagueGenerator::NormalizeName(Entry).IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("NameBlocklist: '%s' has no letters"), *Entry));
        }
    }
    if (InTuning.MaxNameAttempts < 1)
    {
        Problems.Add(TEXT("MaxNameAttempts: 1 or more"));
    }
    if (InTuning.DraftClass.ProspectsPerTeam < 1 || InTuning.DraftClass.TalentSpread <= 0.f)
    {
        Problems.Add(TEXT("DraftClass: ProspectsPerTeam 1 or more, TalentSpread above 0"));
    }
    return Problems;
}

FString UPSLeagueGenerator::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/league_generator.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSLeagueGenerator::LoadTuningFromJson(const FString& JsonFilePath)
{
    FPSLeagueGeneratorTuning Loaded;
    if (!NewObject<UPSDataIngestion>(this)->LoadLeagueGeneratorTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLeagueGenerator: Could not load the generator tuning from %s."), *JsonFilePath);
        return false;
    }
    SetTuning(Loaded);
    return true;
}

const FPSLeagueGeneratorTuning& UPSLeagueGenerator::GetTuning()
{
    EnsureLoaded();
    return Tuning;
}

void UPSLeagueGenerator::SetTuning(const FPSLeagueGeneratorTuning& InTuning)
{
    Tuning = InTuning;
    bTuningLoaded = true;
}

void UPSLeagueGenerator::SetDNACatalog(const FPSPlayerDNACatalog& InCatalog)
{
    DNACatalog = InCatalog;
    bDNACatalogSet = true;
}

void UPSLeagueGenerator::SetProgressionTuning(const FPSProgressionTuning& InTuning)
{
    Progression = InTuning;
    bProgressionSet = true;
    if (Aging && bOwnAging)
    {
        Aging->SetBaseCurve(InTuning);
    }
}

void UPSLeagueGenerator::SetPlayerAging(UPSPlayerAging* InAging)
{
    Aging = InAging;
    bAgingSet = true;
    bOwnAging = false;
}

FPSProgressionTuning UPSLeagueGenerator::GetCareerCurve(EPlayerRole Role)
{
    EnsureLoaded();
    return Aging ? Aging->GetCurve(Role) : Progression;
}

void UPSLeagueGenerator::EnsureLoaded()
{
    if (!bTuningLoaded)
    {
        bTuningLoaded = true;
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    if (!bDNACatalogSet)
    {
        bDNACatalogSet = true;
        if (UPSPlayerDNASubsystem* WorldDNA = UPSPlayerDNASubsystem::Get(GetWorld()))
        {
            DNACatalog = WorldDNA->GetCatalog();
        }
        else if (!NewObject<UPSDataIngestion>(this)->LoadPlayerDNACatalogFromJson(UPSPlayerDNASubsystem::GetDefaultCatalogPath(), DNACatalog))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSLeagueGenerator: No DNA catalog; generated players play neutral."));
        }
    }
    if (!bProgressionSet)
    {
        bProgressionSet = true;
        if (!NewObject<UPSDataIngestion>(this)->LoadProgressionTuningFromJson(UPSPlayerProgression::GetDefaultTuningPath(), Progression))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSLeagueGenerator: Could not load %s; using the default age curve."), *UPSPlayerProgression::GetDefaultTuningPath());
            Progression = FPSProgressionTuning();
        }
    }
    if (!bAgingSet)
    {
        // Epic 94's role curves, the ones a franchise's players go on to age along; a role they
        // don't list follows the progression tuning in use.
        bAgingSet = true;
        bOwnAging = true;
        Aging = NewObject<UPSPlayerAging>(this);
        Aging->LoadDefaults();
        Aging->SetBaseCurve(Progression);
    }
}

bool UPSLeagueGenerator::CheckTuning()
{
    EnsureLoaded();
    const TArray<FString> Problems = PSLeagueGenerator::ValidateTuning(Tuning);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLeagueGenerator: %s"), *Problem);
    }
    return Problems.Num() == 0;
}

TArray<FPSTeamInfo> UPSLeagueGenerator::MakeTeamIdentities(const TArray<FPSTeamInfo>& BaseTeams) const
{
    TArray<FPSTeamInfo> Teams;
    TSet<FName> UsedIds;
    TSet<FString> UsedNames;
    for (const FPSTeamInfo& Base : BaseTeams)
    {
        if (Teams.Num() < Tuning.NumTeams && !Base.TeamId.IsNone() && !UsedIds.Contains(Base.TeamId))
        {
            Teams.Add(Base);
            UsedIds.Add(Base.TeamId);
            UsedNames.Add(Base.DisplayName);
            UsedNames.Add(Base.Abbreviation);
        }
    }

    TMap<FString, int32> DivisionSizes;
    for (const FString& Division : Tuning.Divisions)
    {
        DivisionSizes.Add(Division, 0);
    }
    for (const FPSTeamInfo& Team : Teams)
    {
        if (int32* Size = DivisionSizes.Find(Team.Division))
        {
            ++*Size;
        }
    }
    auto SmallestDivision = [this, &DivisionSizes]()
    {
        FString Smallest;
        int32 SmallestSize = MAX_int32;
        for (const FString& Division : Tuning.Divisions)
        {
            if (DivisionSizes[Division] < SmallestSize)
            {
                Smallest = Division;
                SmallestSize = DivisionSizes[Division];
            }
        }
        return Smallest;
    };

    int32 Number = Teams.Num();
    const int32 NumColors = Tuning.PlaceholderColors.Num();
    while (Teams.Num() < Tuning.NumTeams)
    {
        ++Number;
        const FString Code = FString::Printf(TEXT("T%02d"), Number);
        const FString DisplayName = FString::Printf(TEXT("%s %02d"), *Tuning.PlaceholderTeamName, Number);
        if (UsedIds.Contains(FName(*Code)) || UsedNames.Contains(Code) || UsedNames.Contains(DisplayName))
        {
            continue;
        }
        FPSTeamInfo& Team = Teams.AddDefaulted_GetRef();
        Team.TeamId = FName(*Code);
        Team.DisplayName = DisplayName;
        Team.Abbreviation = Code;
        if (NumColors > 0)
        {
            const int32 Slot = Teams.Num() - 1;
            Team.PrimaryColor = Tuning.PlaceholderColors[Slot % NumColors];
            Team.SecondaryColor = Tuning.PlaceholderColors[(Slot + NumColors / 2) % NumColors];
        }
        UsedIds.Add(Team.TeamId);
    }

    for (FPSTeamInfo& Team : Teams)
    {
        if (Team.Division.TrimStartAndEnd().IsEmpty())
        {
            Team.Division = SmallestDivision();
            if (int32* Size = DivisionSizes.Find(Team.Division))
            {
                ++*Size;
            }
        }
        Team.RosterDataTablePath = FString::Printf(TEXT("Data/rosters/team_%s.json"), *Team.TeamId.ToString());
    }
    return Teams;
}

FString UPSLeagueGenerator::DrawName(FRandomStream& Stream, TSet<FString>& UsedNames, const TSet<FString>& BlockedForms) const
{
    auto Accept = [&UsedNames, &BlockedForms](const FString& Candidate)
    {
        const FString Normalized = PSLeagueGenerator::NormalizeName(Candidate);
        if (Normalized.IsEmpty() || BlockedForms.Contains(Normalized) || UsedNames.Contains(Normalized))
        {
            return false;
        }
        UsedNames.Add(Normalized);
        return true;
    };

    float TotalWeight = 0.f;
    for (const FPSNameCulture& Culture : Tuning.NameCultures)
    {
        TotalWeight += FMath::Max(0.f, Culture.Weight);
    }
    FString First;
    FString Last;
    for (int32 Attempt = 0; Attempt < FMath::Max(1, Tuning.MaxNameAttempts); ++Attempt)
    {
        float Roll = Stream.GetFraction() * TotalWeight;
        const FPSNameCulture* Picked = nullptr;
        for (const FPSNameCulture& Culture : Tuning.NameCultures)
        {
            Picked = &Culture;
            Roll -= FMath::Max(0.f, Culture.Weight);
            if (Roll < 0.f)
            {
                break;
            }
        }
        if (!Picked || Picked->FirstNames.Num() == 0 || Picked->LastNames.Num() == 0)
        {
            continue;
        }
        const FString& DrawnFirst = Picked->FirstNames[Stream.RandHelper(Picked->FirstNames.Num())];
        const FString& DrawnLast = Picked->LastNames[Stream.RandHelper(Picked->LastNames.Num())];
        const FString Candidate = FString::Printf(TEXT("%s %s"), *DrawnFirst, *DrawnLast);
        if (Accept(Candidate))
        {
            return Candidate;
        }
        if (First.IsEmpty() || !BlockedForms.Contains(PSLeagueGenerator::NormalizeName(Candidate)))
        {
            // Separated below if every draw collides; a blocked name is never the base.
            First = DrawnFirst;
            Last = DrawnLast;
        }
    }

    // Every draw was taken or blocked: the last one that wasn't blocked, with a middle initial,
    // then a number.
    for (TCHAR Initial = TEXT('A'); Initial <= TEXT('Z'); ++Initial)
    {
        const FString Candidate = First + TEXT(" ") + FString::Chr(Initial) + TEXT(". ") + Last;
        if (Accept(Candidate))
        {
            return Candidate;
        }
    }
    for (int32 Suffix = 2; Suffix < 100000; ++Suffix)
    {
        const FString Candidate = FString::Printf(TEXT("%s %s %d"), *First, *Last, Suffix);
        if (Accept(Candidate))
        {
            return Candidate;
        }
    }
    return FString::Printf(TEXT("%s %s"), *First, *Last);
}

FPlayerAttributes UPSLeagueGenerator::MakePlayer(const FPSRoleProfile& Profile, float TalentShift, float TalentSpread, bool bRookie, FRandomStream& Stream, TSet<FString>& UsedNames, const TSet<FString>& BlockedForms) const
{
    const int32 EntryAge = Stream.RandRange(Tuning.EntryAgeMin, Tuning.EntryAgeMax);
    const int32 Experience = bRookie ? 0 : PSLeagueGeneratorPrivate::DrawExperience(Profile.Attrition, Profile.MaxAge - EntryAge, Stream);
    const float Talent = TalentSpread * PSLeagueGenerator::StandardNormal(Stream) + TalentShift
        + FMath::Min(Experience * Tuning.TalentPerExperienceYear, Tuning.MaxExperienceTalent);

    FPlayerAttributes Prime;
    Prime.Role = Profile.Role;
    for (const FPSAttributeCurve& Curve : Profile.Attributes)
    {
        FFloatProperty* Property = PSLeagueGeneratorPrivate::FindAttribute(Curve.Attribute);
        if (!Property)
        {
            continue;
        }
        const float Weight = FMath::Clamp(Curve.TalentWeight, 0.f, 1.f);
        const float Own = PSLeagueGenerator::StandardNormal(Stream);
        const float Value = Curve.Mean + Curve.StdDev * (Weight * Talent + FMath::Sqrt(1.f - Weight * Weight) * Own);
        Property->SetPropertyValue_InContainer(&Prime, FMath::Clamp(Value, Curve.Min, Curve.Max));
    }

    const int32 PlayerAge = FMath::Min(EntryAge + Experience, FMath::Max(Profile.MaxAge, EntryAge));
    // His role's age curve (Epic 94): the one he will go on to age along in a franchise.
    FPlayerAttributes Player = PSLeagueGenerator::ApplyCareerArc(Prime, PlayerAge, Aging ? Aging->GetCurve(Profile.Role) : Progression);
    for (const FPSAttributeCurve& Curve : Profile.Attributes)
    {
        if (FFloatProperty* Property = PSLeagueGeneratorPrivate::FindAttribute(Curve.Attribute))
        {
            const float Value = FMath::Clamp(Property->GetPropertyValue_InContainer(&Player), Curve.Min, Curve.Max);
            Property->SetPropertyValue_InContainer(&Player, FMath::RoundToFloat(Value));
        }
    }
    Player.Age = PlayerAge;
    Player.DisplayName = DrawName(Stream, UsedNames, BlockedForms);
    return Player;
}

void UPSLeagueGenerator::GiveDNA(TArray<FPlayerAttributes*>& Players, const TArray<FPlayerAttributes>& CenterPool, FRandomStream& Stream) const
{
    TArray<FPlayerAttributes> Own;
    for (const FPlayerAttributes* Player : Players)
    {
        Own.Add(*Player);
    }
    TMap<EPlayerRole, TMap<FName, float>> CentersByRole;
    for (const FPSRoleProfile& Profile : Tuning.RoleProfiles)
    {
        TMap<FName, float> Centers = PSPlayerDNA::GetGeneratorCenters(DNACatalog, CenterPool, Profile.Role);
        CentersByRole.Add(Profile.Role, Centers.Num() > 0 ? Centers : PSPlayerDNA::GetGeneratorCenters(DNACatalog, Own, Profile.Role));
    }
    const TMap<FName, float> NoCenters;
    for (FPlayerAttributes* Player : Players)
    {
        const TMap<FName, float>* Centers = CentersByRole.Find(Player->Role);
        Player->DNA = PSPlayerDNA::GenerateProfile(DNACatalog, *Player, Centers ? *Centers : NoCenters, [&Stream]() { return PSLeagueGenerator::StandardNormal(Stream); });
    }
}

FPSGeneratedLeague UPSLeagueGenerator::GenerateLeague(int32 Seed, const TArray<FPSTeamInfo>& BaseTeams)
{
    FPSGeneratedLeague League;
    League.Seed = Seed;
    if (!CheckTuning())
    {
        return League;
    }

    League.Config.LeagueName = Tuning.LeagueName;
    League.Config.NumWeeks = Tuning.NumWeeks;
    League.Config.ByeWeekNumbers = Tuning.ByeWeekNumbers;
    League.Config.NumPlayoffTeams = Tuning.NumPlayoffTeams;
    League.Config.TeamsDataTablePath = PSLeagueGeneratorPrivate::TeamsFile;

    FRandomStream Stream(Seed);
    const TSet<FString> BlockedForms = PSLeagueGenerator::MakeBlockedForms(Tuning.NameBlocklist);
    TSet<FString> UsedNames;
    for (const FPSTeamInfo& Identity : MakeTeamIdentities(BaseTeams))
    {
        FPSGeneratedTeam& Team = League.Teams.AddDefaulted_GetRef();
        Team.Team = Identity;
        const float TeamTalent = Tuning.TeamTalentSpread * PSLeagueGenerator::StandardNormal(Stream);
        for (const FPSRoleProfile& Profile : Tuning.RoleProfiles)
        {
            TArray<FPlayerAttributes> RolePlayers;
            for (int32 Index = 0; Index < Profile.RosterCount; ++Index)
            {
                RolePlayers.Add(MakePlayer(Profile, TeamTalent, 1.f, false, Stream, UsedNames, BlockedForms));
            }
            // Best first, so the default depth chart starts the best.
            RolePlayers.StableSort([](const FPlayerAttributes& A, const FPlayerAttributes& B)
            {
                return UPSContractNegotiation::RatePlayer(A) > UPSContractNegotiation::RatePlayer(B);
            });
            for (int32 Index = 0; Index < RolePlayers.Num(); ++Index)
            {
                RolePlayers[Index].PlayerId = FName(*FString::Printf(TEXT("%s_%s_%03d"), *Identity.TeamId.ToString(), *Profile.IdCode, Index + 1));
            }
            Team.Players.Append(RolePlayers);
        }
    }

    // DNA last, centered on the whole league's ratings.
    TArray<FPlayerAttributes> Everyone;
    TArray<FPlayerAttributes*> ToStyle;
    for (FPSGeneratedTeam& Team : League.Teams)
    {
        Everyone.Append(Team.Players);
        for (FPlayerAttributes& Player : Team.Players)
        {
            ToStyle.Add(&Player);
        }
    }
    GiveDNA(ToStyle, Everyone, Stream);
    return League;
}

FPSDraftClass UPSLeagueGenerator::GenerateDraftClass(int32 Seed, int32 DraftYear, int32 NumTeams, const TArray<FPlayerAttributes>& LeaguePlayers)
{
    FPSDraftClass Class;
    Class.DraftYear = DraftYear;
    if (!CheckTuning())
    {
        return Class;
    }

    // One stream per (seed, year): any year's class can be made again on its own.
    const uint32 Mixed = static_cast<uint32>(Seed) * 2654435761u ^ (static_cast<uint32>(DraftYear) * 40503u + 0x9E3779B9u);
    FRandomStream Stream(static_cast<int32>(Mixed));
    const TSet<FString> BlockedForms = PSLeagueGenerator::MakeBlockedForms(Tuning.NameBlocklist);
    TSet<FString> UsedNames;
    for (const FPlayerAttributes& Player : LeaguePlayers)
    {
        UsedNames.Add(PSLeagueGenerator::NormalizeName(Player.DisplayName));
    }

    const FPSDraftClassTuning& Draft = Tuning.DraftClass;
    const TArray<int32> Quotas = PSLeagueGeneratorPrivate::Apportion(Tuning.RoleProfiles, FMath::Max(0, NumTeams) * Draft.ProspectsPerTeam);
    for (int32 ProfileIndex = 0; ProfileIndex < Tuning.RoleProfiles.Num(); ++ProfileIndex)
    {
        for (int32 Index = 0; Index < Quotas[ProfileIndex]; ++Index)
        {
            FPlayerAttributes Prospect = MakePlayer(Tuning.RoleProfiles[ProfileIndex], Draft.TalentShift, Draft.TalentSpread, true, Stream, UsedNames, BlockedForms);
            Prospect.PlayerId = FName(*FString::Printf(TEXT("DC%d_%03d"), DraftYear, Class.Prospects.Num() + 1));
            Class.Prospects.Add(Prospect);
        }
    }

    TArray<FPlayerAttributes*> ToStyle;
    for (FPlayerAttributes& Prospect : Class.Prospects)
    {
        ToStyle.Add(&Prospect);
    }
    GiveDNA(ToStyle, LeaguePlayers, Stream);
    return Class;
}

bool UPSLeagueGenerator::WriteLeague(const FPSGeneratedLeague& League, const FString& RootDir, TArray<FString>& OutWrittenFiles)
{
    using namespace PSLeagueGeneratorPrivate;

    OutWrittenFiles.Reset();
    bool bWritten = true;

    TArray<FString> Byes;
    for (const int32 Week : League.Config.ByeWeekNumbers)
    {
        Byes.Add(FString::Printf(TEXT("%d"), Week));
    }
    const FString ConfigJson = FString::Printf(TEXT("{\n  \"LeagueName\": %s,\n  \"NumWeeks\": %d,\n  \"ByeWeekNumbers\": [%s],\n  \"NumPlayoffTeams\": %d,\n  \"TeamsDataTablePath\": %s\n}\n"),
        *Quote(League.Config.LeagueName), League.Config.NumWeeks, *FString::Join(Byes, TEXT(", ")), League.Config.NumPlayoffTeams, *Quote(League.Config.TeamsDataTablePath));
    bWritten &= WriteDataFile(RootDir, LeagueConfigFile, ConfigJson, OutWrittenFiles);

    TArray<FString> TeamRows;
    for (const FPSGeneratedTeam& Team : League.Teams)
    {
        TeamRows.Add(TeamJson(Team.Team));
        TArray<FString> PlayerRows;
        for (const FPlayerAttributes& Player : Team.Players)
        {
            PlayerRows.Add(PlayerJson(Player));
        }
        bWritten &= WriteDataFile(RootDir, Team.Team.RosterDataTablePath, PSJsonWriting::ArrayFile(TEXT("Players"), PlayerRows), OutWrittenFiles);
    }
    bWritten &= WriteDataFile(RootDir, League.Config.TeamsDataTablePath, PSJsonWriting::ArrayFile(TEXT("Teams"), TeamRows), OutWrittenFiles);
    return bWritten;
}

UPSRoster* UPSLeagueGenerator::MakeRoster(const FPSGeneratedTeam& Team, UObject* Outer)
{
    UPSRoster* Roster = NewObject<UPSRoster>(Outer ? Outer : GetTransientPackage());
    Roster->InitializeRoster(Team.Players);
    Roster->BuildDefaultDepthChart();
    return Roster;
}
