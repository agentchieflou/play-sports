// PSLeagueGeneratorTests.cpp -- the roster and league generator (Epic 122)
//
// Tests covered:
//   1. The tuning: the shipped one loads and is sound, the age curve loads, a broken tuning's
//      mistakes are reported, and the no-real-person policy matches names in full and initial
//      form, ignoring case and punctuation.
//   2. A full league: 32 teams of 53, the given teams first, unique PlayerIds and names with none
//      blocklisted, ratings inside their curves, no league of 99s, best players first on the
//      depth chart, no runaway team, balanced divisions, and the same seed the same league.
//   3. Ages and career arcs: the arc is the progression curve run backward (a young player's
//      offseasons take him to his prime) and forward (a veteran's took him down from it); the
//      league runs from rookies to veterans; the contract manager prices a player at his age.
//   4. DNA: generated through Epic 79's rule on the player's role's axes only, leaning with his
//      ratings, so scouts see distinct players.
//   5. The draft-class mode: rookies in the roster's role shares, names new to the league,
//      below the league's primes, the same class from the same seed and year.
//   6. Written content: a generated league laid out as Data/ imports cleanly through the game's
//      own loaders and round-trips; its rosters start a franchise. CI then validates the same
//      files (Saved/GeneratedLeague) with tools/content.py check --strict.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSContentReimportCommandlet.h"
#include "PSContractManager.h"
#include "PSContractNegotiation.h"
#include "PSDataIngestion.h"
#include "PSFranchiseFlow.h"
#include "PSLeagueGenerator.h"
#include "PSPlayerDNA.h"
#include "PSPlayerProgression.h"
#include "PSRoster.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSLeagueGeneratorTests
{
    static const int32 LeagueSeed = 2026;

    static FPSLeagueGeneratorTuning LoadShippedTuning()
    {
        FPSLeagueGeneratorTuning Loaded;
        NewObject<UPSDataIngestion>()->LoadLeagueGeneratorTuningFromJson(UPSLeagueGenerator::GetDefaultTuningPath(), Loaded);
        return Loaded;
    }

    static FPSProgressionTuning LoadShippedProgression()
    {
        FPSProgressionTuning Loaded;
        NewObject<UPSDataIngestion>()->LoadProgressionTuningFromJson(UPSPlayerProgression::GetDefaultTuningPath(), Loaded);
        return Loaded;
    }

    /** Data/sample_teams.json's teams, the ones a generated league keeps. */
    static TArray<FPSTeamInfo> LoadShippedTeams()
    {
        UDataTable* Table = NewObject<UDataTable>();
        Table->RowStruct = FPSTeamInfo::StaticStruct();
        NewObject<UPSDataIngestion>()->LoadTeamsFromJson(FPaths::ProjectDir() / TEXT("Data/sample_teams.json"), Table);
        TArray<FPSTeamInfo*> Rows;
        Table->GetAllRows<FPSTeamInfo>(TEXT("PSLeagueGeneratorTests"), Rows);
        TArray<FPSTeamInfo> Teams;
        for (const FPSTeamInfo* Row : Rows)
        {
            if (Row)
            {
                Teams.Add(*Row);
            }
        }
        return Teams;
    }

    static FPSGeneratedLeague GenerateShippedLeague(int32 Seed)
    {
        return NewObject<UPSLeagueGenerator>()->GenerateLeague(Seed, LoadShippedTeams());
    }

    static TArray<FPlayerAttributes> AllPlayers(const FPSGeneratedLeague& League)
    {
        TArray<FPlayerAttributes> Players;
        for (const FPSGeneratedTeam& Team : League.Teams)
        {
            Players.Append(Team.Players);
        }
        return Players;
    }

    static float Mean(const TArray<float>& Values)
    {
        float Sum = 0.f;
        for (const float Value : Values)
        {
            Sum += Value;
        }
        return Values.Num() > 0 ? Sum / Values.Num() : 0.f;
    }

    static float StdDev(const TArray<float>& Values)
    {
        const float Average = Mean(Values);
        float Sum = 0.f;
        for (const float Value : Values)
        {
            Sum += FMath::Square(Value - Average);
        }
        return Values.Num() > 0 ? FMath::Sqrt(Sum / Values.Num()) : 0.f;
    }

    static float GetFloat(const FPlayerAttributes& Player, FName Field)
    {
        const FFloatProperty* Property = FindFProperty<FFloatProperty>(FPlayerAttributes::StaticStruct(), Field);
        return Property ? Property->GetPropertyValue_InContainer(&Player) : 0.f;
    }

    /** Every generated fact about the league in one string, to compare two generations. */
    static FString Fingerprint(const TArray<FPlayerAttributes>& Players)
    {
        FString Out;
        for (const FPlayerAttributes& Player : Players)
        {
            Out += FString::Printf(TEXT("|%s,%s,%d,%d"), *Player.PlayerId.ToString(), *Player.DisplayName, static_cast<int32>(Player.Role), Player.Age);
            for (TFieldIterator<FFloatProperty> It(FPlayerAttributes::StaticStruct()); It; ++It)
            {
                Out += FString::Printf(TEXT(",%.2f"), It->GetPropertyValue_InContainer(&Player));
            }
            for (TFieldIterator<FFloatProperty> It(FPSPlayerDNA::StaticStruct()); It; ++It)
            {
                Out += FString::Printf(TEXT(",%.2f"), It->GetPropertyValue_InContainer(&Player.DNA));
            }
        }
        return Out;
    }

    static FPlayerAttributes MakeRated(EPlayerRole Role, const TCHAR* PlayerId, float Rating, int32 Age)
    {
        FPlayerAttributes Player;
        Player.PlayerId = FName(PlayerId);
        Player.DisplayName = PlayerId;
        Player.Role = Role;
        Player.WeightKg = 100.f;
        Player.HeightCm = 190.f;
        Player.Speed = Rating;
        Player.Agility = Rating;
        Player.Strength = Rating;
        Player.Acceleration = Rating;
        Player.Awareness = Rating;
        Player.Stamina = Rating;
        Player.Age = Age;
        return Player;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The tuning and the name policy
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLeagueGeneratorTuningTest,
    "PlaySports.Content.LeagueGenerator.Tuning",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLeagueGeneratorTuningTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueGeneratorTests;

    FPSLeagueGeneratorTuning Shipped;
    if (!TestTrue(TEXT("The shipped generator tuning loads"), NewObject<UPSDataIngestion>()->LoadLeagueGeneratorTuningFromJson(UPSLeagueGenerator::GetDefaultTuningPath(), Shipped)))
    {
        return false;
    }
    const TArray<FString> Problems = PSLeagueGenerator::ValidateTuning(Shipped);
    TestEqual(TEXT("...and is sound"), Problems.Num(), 0);
    for (const FString& Problem : Problems)
    {
        AddError(Problem);
    }
    TestEqual(TEXT("A league of 32 teams"), Shipped.NumTeams, 32);
    int32 RosterSize = 0;
    for (const FPSRoleProfile& Profile : Shipped.RoleProfiles)
    {
        RosterSize += Profile.RosterCount;
    }
    TestEqual(TEXT("...of 53 players"), RosterSize, 53);
    TestTrue(TEXT("Names come from several cultures"), Shipped.NameCultures.Num() >= 6);
    TestTrue(TEXT("The blocklist names real people"), Shipped.NameBlocklist.Contains(TEXT("Josh Allen")));

    FPSProgressionTuning Curve;
    TestTrue(TEXT("The age curve loads"), NewObject<UPSDataIngestion>()->LoadProgressionTuningFromJson(UPSPlayerProgression::GetDefaultTuningPath(), Curve));
    TestTrue(TEXT("...with a prime window"), Curve.PeakAgeStart >= 18 && Curve.PeakAgeStart <= Curve.PeakAgeEnd);

    // A broken tuning's mistakes, each reported.
    FPSLeagueGeneratorTuning Broken = Shipped;
    Broken.NumPlayoffTeams = Broken.NumTeams + 1;
    Broken.RoleProfiles[1].Role = Broken.RoleProfiles[0].Role;
    Broken.RoleProfiles[2].Attrition = 1.5f;
    Broken.RoleProfiles[3].Attributes.RemoveAt(0);
    Broken.RoleProfiles[4].Attributes[0].Attribute = TEXT("Charisma");
    Broken.PlaceholderColors.Add(TEXT("blue"));
    Broken.NameCultures[0].LastNames.Reset();
    auto Reports = [](const TArray<FString>& Lines, const TCHAR* Text)
    {
        return Lines.ContainsByPredicate([Text](const FString& Line) { return Line.Contains(Text); });
    };
    const TArray<FString> BrokenProblems = PSLeagueGenerator::ValidateTuning(Broken);
    TestTrue(TEXT("Too many playoff teams"), Reports(BrokenProblems, TEXT("NumPlayoffTeams")));
    TestTrue(TEXT("A role listed twice"), Reports(BrokenProblems, TEXT("listed twice")));
    TestTrue(TEXT("...so another role has no profile"), Reports(BrokenProblems, TEXT("no profile for")));
    TestTrue(TEXT("An attrition outside 0-1"), Reports(BrokenProblems, TEXT("Attrition")));
    TestTrue(TEXT("A missing curve"), Reports(BrokenProblems, TEXT("no curve for")));
    TestTrue(TEXT("An attribute FPlayerAttributes doesn't have"), Reports(BrokenProblems, TEXT("'Charisma': not a float field")));
    TestTrue(TEXT("A color that isn't #RRGGBB"), Reports(BrokenProblems, TEXT("'blue' is not #RRGGBB")));
    TestTrue(TEXT("A culture without last names"), Reports(BrokenProblems, TEXT("needs first and last names")));

    // The no-real-person policy: full and initial forms, case and punctuation ignored.
    TestEqual(TEXT("Names compare without punctuation"), PSLeagueGenerator::NormalizeName(TEXT("Ja'Marr  CHASE")), FString(TEXT("jamarr chase")));
    TestEqual(TEXT("...a hyphen read as a space"), PSLeagueGenerator::NormalizeName(TEXT("Smith-Jones")), FString(TEXT("smith jones")));
    const TSet<FString> Blocked = PSLeagueGenerator::MakeBlockedForms({ TEXT("Josh Allen"), TEXT("T.J. Watt") });
    TestTrue(TEXT("A blocklisted name is blocked"), PSLeagueGenerator::IsBlockedName(Blocked, TEXT("Josh Allen")));
    TestTrue(TEXT("...in any case"), PSLeagueGenerator::IsBlockedName(Blocked, TEXT("JOSH allen")));
    TestTrue(TEXT("...and in initial form"), PSLeagueGenerator::IsBlockedName(Blocked, TEXT("J. Allen")));
    TestTrue(TEXT("...with or without its periods"), PSLeagueGenerator::IsBlockedName(Blocked, TEXT("TJ Watt")) && PSLeagueGenerator::IsBlockedName(Blocked, TEXT("T. Watt")));
    TestFalse(TEXT("Another first name is not"), PSLeagueGenerator::IsBlockedName(Blocked, TEXT("Joshua Allen")));
    TestFalse(TEXT("Nor another surname"), PSLeagueGenerator::IsBlockedName(Blocked, TEXT("Josh Allender")));

    // A culture that can only draw a blocked name never names a player after him.
    FPSLeagueGeneratorTuning Narrow = Shipped;
    Narrow.NumTeams = 2;
    Narrow.NumPlayoffTeams = 2;
    Narrow.NameCultures.SetNum(1);
    Narrow.NameCultures[0].FirstNames = { TEXT("Josh") };
    Narrow.NameCultures[0].LastNames = { TEXT("Allen"), TEXT("Pruett") };
    Narrow.NameBlocklist = { TEXT("Josh Allen") };
    UPSLeagueGenerator* Generator = NewObject<UPSLeagueGenerator>();
    Generator->SetTuning(Narrow);
    const TArray<FPlayerAttributes> NarrowPlayers = AllPlayers(Generator->GenerateLeague(5, TArray<FPSTeamInfo>()));
    TestEqual(TEXT("The narrow league is generated"), NarrowPlayers.Num(), 2 * RosterSize);
    TSet<FString> NarrowNames;
    bool bAnyBlocked = false;
    for (const FPlayerAttributes& Player : NarrowPlayers)
    {
        bAnyBlocked |= PSLeagueGenerator::IsBlockedName(Blocked, Player.DisplayName);
        NarrowNames.Add(PSLeagueGenerator::NormalizeName(Player.DisplayName));
    }
    TestFalse(TEXT("No player is named for the blocklisted man"), bAnyBlocked);
    TestEqual(TEXT("Every name is still the league's own (a middle initial or a number separates them)"), NarrowNames.Num(), NarrowPlayers.Num());
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- A full league
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLeagueGeneratorFullLeagueTest,
    "PlaySports.Content.LeagueGenerator.FullLeague",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLeagueGeneratorFullLeagueTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueGeneratorTests;

    const FPSLeagueGeneratorTuning Shipped = LoadShippedTuning();
    const TArray<FPSTeamInfo> BaseTeams = LoadShippedTeams();
    const FPSGeneratedLeague League = GenerateShippedLeague(LeagueSeed);
    if (!TestEqual(TEXT("32 teams"), League.Teams.Num(), 32))
    {
        return false;
    }
    TestEqual(TEXT("The config is the tuning's"), League.Config.NumPlayoffTeams, Shipped.NumPlayoffTeams);
    TestEqual(TEXT("...and names the teams file it is written with"), League.Config.TeamsDataTablePath, FString(TEXT("Data/league_teams.json")));
    for (int32 Index = 0; Index < BaseTeams.Num(); ++Index)
    {
        TestEqual(TEXT("The given teams come first, as they are"), League.Teams[Index].Team.DisplayName, BaseTeams[Index].DisplayName);
    }
    TestEqual(TEXT("The rest get stand-in identities"), League.Teams[BaseTeams.Num()].Team.TeamId, FName(*FString::Printf(TEXT("T%02d"), BaseTeams.Num() + 1)));

    TSet<FName> Ids;
    TSet<FString> Names;
    const TSet<FString> Blocked = PSLeagueGenerator::MakeBlockedForms(Shipped.NameBlocklist);
    bool bAllRostersFull = true;
    bool bAllInCurves = true;
    bool bBestFirst = true;
    bool bAnyBlocked = false;
    TMap<EPlayerRole, TArray<const FPlayerAttributes*>> ByRole;
    TArray<float> TeamOveralls;
    TMap<FString, int32> DivisionSizes;
    for (const FPSGeneratedTeam& Team : League.Teams)
    {
        bAllRostersFull &= Team.Players.Num() == 53;
        DivisionSizes.FindOrAdd(Team.Team.Division)++;
        TestEqual(TEXT("Each team's roster path is its own"), Team.Team.RosterDataTablePath, FString::Printf(TEXT("Data/rosters/team_%s.json"), *Team.Team.TeamId.ToString()));
        TArray<float> Overalls;
        for (int32 Index = 0; Index < Team.Players.Num(); ++Index)
        {
            const FPlayerAttributes& Player = Team.Players[Index];
            Ids.Add(Player.PlayerId);
            Names.Add(PSLeagueGenerator::NormalizeName(Player.DisplayName));
            bAnyBlocked |= PSLeagueGenerator::IsBlockedName(Blocked, Player.DisplayName);
            ByRole.FindOrAdd(Player.Role).Add(&Player);
            Overalls.Add(UPSContractNegotiation::RatePlayer(Player));
            if (const FPSRoleProfile* Profile = Shipped.FindRole(Player.Role))
            {
                for (const FPSAttributeCurve& Curve : Profile->Attributes)
                {
                    const float Value = GetFloat(Player, Curve.Attribute);
                    bAllInCurves &= Value >= Curve.Min && Value <= Curve.Max && Value >= 0.f;
                }
            }
            if (Index > 0 && Team.Players[Index - 1].Role == Player.Role)
            {
                bBestFirst &= UPSContractNegotiation::RatePlayer(Team.Players[Index - 1]) >= UPSContractNegotiation::RatePlayer(Player);
            }
        }
        TeamOveralls.Add(Mean(Overalls));
    }
    TestTrue(TEXT("Every roster has 53 players"), bAllRostersFull);
    TestEqual(TEXT("PlayerIds are unique across the league"), Ids.Num(), 32 * 53);
    TestEqual(TEXT("So are names"), Names.Num(), 32 * 53);
    TestFalse(TEXT("No name is a blocklisted real person's"), bAnyBlocked);
    TestTrue(TEXT("Every rating and body measure is inside its role's curve"), bAllInCurves);
    TestTrue(TEXT("Each role's players are best first, so the default depth chart starts the best"), bBestFirst);

    for (const FPSRoleProfile& Profile : Shipped.RoleProfiles)
    {
        const TArray<const FPlayerAttributes*>* Players = ByRole.Find(Profile.Role);
        const FString RoleName = StaticEnum<EPlayerRole>()->GetNameStringByValue(static_cast<int64>(Profile.Role));
        if (!TestTrue(*FString::Printf(TEXT("%s: rostered"), *RoleName), Players && Players->Num() == 32 * Profile.RosterCount))
        {
            continue;
        }
        for (const TCHAR* Field : { TEXT("Speed"), TEXT("Agility"), TEXT("Strength"), TEXT("Acceleration"), TEXT("Awareness"), TEXT("Stamina") })
        {
            TArray<float> Values;
            for (const FPlayerAttributes* Player : *Players)
            {
                Values.Add(GetFloat(*Player, Field));
            }
            // tools/content.py's report warns at a mean of 90 or a spread under 2.
            TestTrue(*FString::Printf(TEXT("%s %s: no league of 99s (mean %.1f)"), *RoleName, Field, Mean(Values)), Mean(Values) < 90.f);
            TestTrue(*FString::Printf(TEXT("%s %s: ratings vary (spread %.1f)"), *RoleName, Field, StdDev(Values)), StdDev(Values) >= 2.f);
        }
    }

    const float LeagueOverall = Mean(TeamOveralls);
    float WorstGap = 0.f;
    for (const float TeamOverall : TeamOveralls)
    {
        WorstGap = FMath::Max(WorstGap, FMath::Abs(TeamOverall - LeagueOverall));
    }
    TestTrue(*FString::Printf(TEXT("No team is far from the league (worst %.1f points)"), WorstGap), WorstGap < 15.f);
    TestTrue(*FString::Printf(TEXT("...but teams differ (worst %.1f points)"), WorstGap), WorstGap > 0.5f);

    int32 Smallest = MAX_int32;
    int32 Largest = 0;
    for (const FString& Division : Shipped.Divisions)
    {
        const int32 Size = DivisionSizes.FindRef(Division);
        Smallest = FMath::Min(Smallest, Size);
        Largest = FMath::Max(Largest, Size);
    }
    TestTrue(TEXT("Stand-in teams fill the divisions evenly"), Largest - Smallest <= 1 && Smallest > 0);

    TestEqual(TEXT("The same seed makes the same league"), Fingerprint(AllPlayers(GenerateShippedLeague(LeagueSeed))), Fingerprint(AllPlayers(League)));
    TestNotEqual(TEXT("Another seed makes another"), Fingerprint(AllPlayers(GenerateShippedLeague(LeagueSeed + 1))), Fingerprint(AllPlayers(League)));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- Ages and career arcs
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLeagueGeneratorAgeTest,
    "PlaySports.Content.LeagueGenerator.AgesAndCareerArcs",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLeagueGeneratorAgeTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueGeneratorTests;

    const FPSProgressionTuning Curve = LoadShippedProgression();
    const UPSPlayerProgression* Model = GetDefault<UPSPlayerProgression>();
    const FPlayerAttributes Prime = MakeRated(EPlayerRole::WideReceiver, TEXT("Prime"), 70.f, 0);
    const TCHAR* const Grown[] = { TEXT("Speed"), TEXT("Agility"), TEXT("Strength"), TEXT("Acceleration"), TEXT("Awareness") };

    // A young player: the offseasons from his age to the prime window take him to his prime.
    const int32 YoungAge = Curve.PeakAgeStart - 4;
    FPlayerAttributes Young = PSLeagueGenerator::ApplyCareerArc(Prime, YoungAge, Curve);
    TestTrue(TEXT("A young player is below his prime"), Young.Speed < Prime.Speed && Young.Awareness < Prime.Awareness);
    TestEqual(TEXT("...his stamina and body unchanged"), Young.Stamina, Prime.Stamina);
    TestEqual(TEXT("...his weight too"), Young.WeightKg, Prime.WeightKg);
    for (int32 Year = YoungAge; Year < Curve.PeakAgeStart; ++Year)
    {
        Model->ApplyOffseasonProgression(Young, Year, 1.f, Curve);
    }
    for (const TCHAR* Field : Grown)
    {
        TestEqual(*FString::Printf(TEXT("His offseasons grow him into his prime: %s"), Field), GetFloat(Young, Field), GetFloat(Prime, Field), 0.01f);
    }

    // In the prime window he is at his prime.
    const FPlayerAttributes Peak = PSLeagueGenerator::ApplyCareerArc(Prime, Curve.PeakAgeStart, Curve);
    TestEqual(TEXT("In his prime he is at it"), Peak.Speed, Prime.Speed);

    // A veteran: what the offseasons past the window took.
    const int32 VeteranAge = Curve.PeakAgeEnd + 3;
    const FPlayerAttributes Veteran = PSLeagueGenerator::ApplyCareerArc(Prime, VeteranAge, Curve);
    FPlayerAttributes Declined = Prime;
    for (int32 Year = Curve.PeakAgeEnd + 1; Year <= VeteranAge; ++Year)
    {
        Model->ApplyOffseasonProgression(Declined, Year, 1.f, Curve);
    }
    for (const TCHAR* Field : Grown)
    {
        TestEqual(*FString::Printf(TEXT("A veteran has declined as the offseasons since his prime took him: %s"), Field), GetFloat(Veteran, Field), GetFloat(Declined, Field), 0.01f);
    }
    TestTrue(TEXT("...so he is below his prime"), Veteran.Speed < Prime.Speed);

    // The league's ages run from rookies to veterans.
    const FPSLeagueGeneratorTuning Shipped = LoadShippedTuning();
    const TArray<FPlayerAttributes> Players = AllPlayers(GenerateShippedLeague(LeagueSeed));
    TArray<float> Ages;
    int32 Rookies = 0;
    int32 Veterans = 0;
    bool bAgesInRange = true;
    TArray<float> YoungSpeed;
    TArray<float> PrimeSpeed;
    for (const FPlayerAttributes& Player : Players)
    {
        const FPSRoleProfile* Profile = Shipped.FindRole(Player.Role);
        bAgesInRange &= Profile && Player.Age >= Shipped.EntryAgeMin && Player.Age <= Profile->MaxAge;
        Ages.Add(static_cast<float>(Player.Age));
        Rookies += Player.Age <= Shipped.EntryAgeMax ? 1 : 0;
        Veterans += Player.Age >= 30 ? 1 : 0;
        if (Player.Role == EPlayerRole::WideReceiver || Player.Role == EPlayerRole::DefensiveBack)
        {
            if (Player.Age <= Shipped.EntryAgeMax)
            {
                YoungSpeed.Add(Player.Speed);
            }
            else if (Player.Age >= Curve.PeakAgeStart && Player.Age <= Curve.PeakAgeEnd)
            {
                PrimeSpeed.Add(Player.Speed);
            }
        }
    }
    const float Share = 1.f / FMath::Max(1, Players.Num());
    TestTrue(TEXT("Every player has an age from entry to his role's oldest"), bAgesInRange);
    TestTrue(*FString::Printf(TEXT("The league's average age is a pro league's (%.1f)"), Mean(Ages)), Mean(Ages) >= 24.f && Mean(Ages) <= 29.f);
    TestTrue(*FString::Printf(TEXT("Young players are the biggest group (%.0f%% at entry age)"), 100.f * Rookies * Share), Rookies * Share >= 0.15f);
    TestTrue(*FString::Printf(TEXT("...and veterans thin out (%.0f%% 30 or older)"), 100.f * Veterans * Share), Veterans * Share >= 0.05f && Veterans * Share <= 0.35f);
    TestTrue(*FString::Printf(TEXT("Young receivers and backs are slower than ones in their prime (%.1f vs %.1f)"), Mean(YoungSpeed), Mean(PrimeSpeed)), Mean(YoungSpeed) < Mean(PrimeSpeed));

    // The contract manager prices a player at his age, falling back to DefaultPlayerAge.
    UPSContractManager* Contracts = NewObject<UPSContractManager>();
    if (!TestTrue(TEXT("The contract tuning loads"), Contracts->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath())))
    {
        return false;
    }
    Contracts->StartLeague();
    const FName Team(TEXT("Hawks"));
    const FPlayerAttributes Unknown = MakeRated(EPlayerRole::Quarterback, TEXT("AGE_Unknown"), 88.f, 0);
    const FPlayerAttributes Rising = MakeRated(EPlayerRole::Quarterback, TEXT("AGE_Rising"), 88.f, 24);
    const FPlayerAttributes Aging = MakeRated(EPlayerRole::Quarterback, TEXT("AGE_Aging"), 88.f, Contracts->GetTuning().DeclineAge + 4);
    TestEqual(TEXT("No age: the tuning's default"), Contracts->GetPlayerAge(Unknown), Contracts->GetTuning().DefaultPlayerAge);
    TestEqual(TEXT("An age: his own"), Contracts->GetPlayerAge(Aging), Contracts->GetTuning().DeclineAge + 4);
    const int32 UnknownAsk = Contracts->GetDemand(Contracts->MakeNegotiationContext(Unknown, Contracts->GetPlayerAge(Unknown), 0.5f, Team)).AnnualValue;
    const int32 DefaultAsk = Contracts->GetDemand(Contracts->MakeNegotiationContext(Unknown, Contracts->GetTuning().DefaultPlayerAge, 0.5f, Team)).AnnualValue;
    TestEqual(TEXT("A player with no age asks what he did before ages"), UnknownAsk, DefaultAsk);
    TestEqual(TEXT("A new franchise signs both"), Contracts->SignRosterAtDemand(Team, { Rising, Aging }), 2);
    const int32 Year = Contracts->GetLeagueYear();
    TestTrue(*FString::Printf(TEXT("...the one past his decline for less (%d vs %d)"), Contracts->GetCapHit(Aging.PlayerId, Year), Contracts->GetCapHit(Rising.PlayerId, Year)),
        Contracts->GetCapHit(Aging.PlayerId, Year) < Contracts->GetCapHit(Rising.PlayerId, Year));
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- DNA
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLeagueGeneratorDNATest,
    "PlaySports.Content.LeagueGenerator.DNA",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLeagueGeneratorDNATest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueGeneratorTests;

    // The rule itself: RatingLean times the gap from the role's center, plus Spread times the draw.
    FPSDNAAxisDef Axis;
    Axis.Axis = TEXT("Mobility");
    Axis.Roles = { EPlayerRole::Quarterback };
    Axis.Generator.HighAttribute = TEXT("Speed");
    Axis.Generator.LowAttribute = TEXT("Awareness");
    Axis.Generator.RatingLean = 0.05f;
    Axis.Generator.Spread = 0.2f;
    FPSPlayerDNACatalog Rule;
    Rule.Axes.Add(Axis);
    FPlayerAttributes Scrambler = MakeRated(EPlayerRole::Quarterback, TEXT("Scrambler"), 70.f, 25);
    Scrambler.Speed = 84.f;
    FPlayerAttributes Passer = MakeRated(EPlayerRole::Quarterback, TEXT("Passer"), 70.f, 25);
    Passer.Awareness = 80.f;
    const TMap<FName, float> Centers = PSPlayerDNA::GetGeneratorCenters(Rule, { Scrambler, Passer }, EPlayerRole::Quarterback);
    TestEqual(TEXT("The center is the role's average gap"), Centers.FindRef(TEXT("Mobility")), 2.f, 0.001f);
    const FPSPlayerDNA ScramblerDNA = PSPlayerDNA::GenerateProfile(Rule, Scrambler, Centers, []() { return 1.f; });
    TestEqual(TEXT("Lean from the center plus the spread's draw"), ScramblerDNA.Mobility, 0.05f * (14.f - 2.f) + 0.2f, 0.001f);
    const FPSPlayerDNA PasserDNA = PSPlayerDNA::GenerateProfile(Rule, Passer, Centers, []() { return 0.f; });
    TestEqual(TEXT("...the other way for the pocket passer"), PasserDNA.Mobility, 0.05f * (-10.f - 2.f), 0.001f);
    FPlayerAttributes Lineman = MakeRated(EPlayerRole::OffensiveLineman, TEXT("Lineman"), 70.f, 25);
    TestTrue(TEXT("An axis of another role stays neutral"), PSPlayerDNA::GenerateProfile(Rule, Lineman, Centers, []() { return 1.f; }).IsNeutral());

    // The league: every player's DNA on his own role's axes only, leaning with his ratings.
    FPSPlayerDNACatalog Catalog;
    NewObject<UPSDataIngestion>()->LoadPlayerDNACatalogFromJson(UPSPlayerDNASubsystem::GetDefaultCatalogPath(), Catalog);
    const TArray<FPlayerAttributes> Players = AllPlayers(GenerateShippedLeague(LeagueSeed));
    bool bOwnAxesOnly = true;
    int32 WithAxes = 0;
    int32 Styled = 0;
    int32 WithTraits = 0;
    for (const FPlayerAttributes& Player : Players)
    {
        bool bHasAxis = false;
        for (const FPSDNAAxisDef& Def : Catalog.Axes)
        {
            const float Value = PSPlayerDNA::GetAxis(Player.DNA, Def.Axis);
            if (Def.Roles.Contains(Player.Role))
            {
                bHasAxis = true;
            }
            else
            {
                bOwnAxesOnly &= Value == 0.f;
            }
            bOwnAxesOnly &= FMath::Abs(Value) <= 1.f && FMath::IsNearlyEqual(Value * 100.f, FMath::RoundToFloat(Value * 100.f), 0.01f);
        }
        WithAxes += bHasAxis ? 1 : 0;
        Styled += bHasAxis && !Player.DNA.IsNeutral() ? 1 : 0;
        WithTraits += PSPlayerDNA::GetScoutingTraits(Catalog, Player).Num() > 0 ? 1 : 0;
    }
    TestTrue(TEXT("DNA only on the axes of a player's role, to two places"), bOwnAxesOnly);
    TestTrue(*FString::Printf(TEXT("Nearly every player whose role has a style has one (%d of %d)"), Styled, WithAxes), Styled >= WithAxes * 9 / 10);
    TestTrue(*FString::Printf(TEXT("Scouts see traits in many players (%d)"), WithTraits), WithTraits >= WithAxes / 4);

    const FPSDNAAxisDef* RunPower = PSPlayerDNA::FindAxis(Catalog, TEXT("RunPower"));
    if (TestNotNull(TEXT("The catalog has the backs' axis"), RunPower))
    {
        const TMap<FName, float> BackCenters = PSPlayerDNA::GetGeneratorCenters(Catalog, Players, EPlayerRole::RunningBack);
        TArray<float> PowerBuilt;
        TArray<float> ShiftyBuilt;
        for (const FPlayerAttributes& Player : Players)
        {
            if (Player.Role == EPlayerRole::RunningBack)
            {
                (PSPlayerDNA::GetGeneratorGap(*RunPower, Player) > BackCenters.FindRef(TEXT("RunPower")) ? PowerBuilt : ShiftyBuilt).Add(Player.DNA.RunPower);
            }
        }
        TestTrue(*FString::Printf(TEXT("Backs stronger than they are shifty run with more power (%.2f vs %.2f)"), Mean(PowerBuilt), Mean(ShiftyBuilt)), Mean(PowerBuilt) > Mean(ShiftyBuilt));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 -- The draft-class mode
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLeagueGeneratorDraftClassTest,
    "PlaySports.Content.LeagueGenerator.DraftClass",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLeagueGeneratorDraftClassTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueGeneratorTests;

    const FPSLeagueGeneratorTuning Shipped = LoadShippedTuning();
    const FPSProgressionTuning Curve = LoadShippedProgression();
    const TArray<FPlayerAttributes> LeaguePlayers = AllPlayers(GenerateShippedLeague(LeagueSeed));
    UPSLeagueGenerator* Generator = NewObject<UPSLeagueGenerator>();
    const FPSDraftClass Class = Generator->GenerateDraftClass(LeagueSeed, 2027, 32, LeaguePlayers);
    const int32 Expected = 32 * Shipped.DraftClass.ProspectsPerTeam;
    TestEqual(TEXT("The class's year"), Class.DraftYear, 2027);
    if (!TestEqual(TEXT("ProspectsPerTeam for each team"), Class.Prospects.Num(), Expected))
    {
        return false;
    }
    TestEqual(TEXT("PlayerIds name the class"), Class.Prospects[0].PlayerId, FName(TEXT("DC2027_001")));

    TSet<FString> LeagueNames;
    for (const FPlayerAttributes& Player : LeaguePlayers)
    {
        LeagueNames.Add(PSLeagueGenerator::NormalizeName(Player.DisplayName));
    }
    const TSet<FString> Blocked = PSLeagueGenerator::MakeBlockedForms(Shipped.NameBlocklist);
    TSet<FName> Ids;
    TSet<FString> Names;
    bool bRookies = true;
    bool bNewNames = true;
    bool bStyled = false;
    TMap<EPlayerRole, int32> RoleCounts;
    TArray<float> ProspectOveralls;
    for (const FPlayerAttributes& Prospect : Class.Prospects)
    {
        Ids.Add(Prospect.PlayerId);
        const FString Name = PSLeagueGenerator::NormalizeName(Prospect.DisplayName);
        bNewNames &= !LeagueNames.Contains(Name) && !Names.Contains(Name) && !PSLeagueGenerator::IsBlockedName(Blocked, Prospect.DisplayName);
        Names.Add(Name);
        bRookies &= Prospect.Age >= Shipped.EntryAgeMin && Prospect.Age <= Shipped.EntryAgeMax;
        bStyled |= !Prospect.DNA.IsNeutral();
        RoleCounts.FindOrAdd(Prospect.Role)++;
        ProspectOveralls.Add(UPSContractNegotiation::RatePlayer(Prospect));
    }
    TestEqual(TEXT("PlayerIds are unique"), Ids.Num(), Expected);
    TestTrue(TEXT("Every prospect is a rookie at an entry age"), bRookies);
    TestTrue(TEXT("Names are new to the league, unique and not real people's"), bNewNames);
    TestTrue(TEXT("Prospects have DNA"), bStyled);
    for (const FPSRoleProfile& Profile : Shipped.RoleProfiles)
    {
        const float Share = static_cast<float>(Expected) * Profile.RosterCount / 53.f;
        TestTrue(*FString::Printf(TEXT("Roles come in a roster's shares (%d of %.1f)"), RoleCounts.FindRef(Profile.Role), Share), FMath::Abs(RoleCounts.FindRef(Profile.Role) - Share) < 1.f);
    }

    TArray<float> PrimeOveralls;
    for (const FPlayerAttributes& Player : LeaguePlayers)
    {
        if (Player.Age >= Curve.PeakAgeStart && Player.Age <= Curve.PeakAgeEnd)
        {
            PrimeOveralls.Add(UPSContractNegotiation::RatePlayer(Player));
        }
    }
    TestTrue(*FString::Printf(TEXT("Prospects are below the league's players in their prime (%.1f vs %.1f)"), Mean(ProspectOveralls), Mean(PrimeOveralls)), Mean(ProspectOveralls) < Mean(PrimeOveralls));

    TestEqual(TEXT("The same seed and year make the same class"), Fingerprint(Generator->GenerateDraftClass(LeagueSeed, 2027, 32, LeaguePlayers).Prospects), Fingerprint(Class.Prospects));
    TestNotEqual(TEXT("Another year makes another"), Fingerprint(Generator->GenerateDraftClass(LeagueSeed, 2028, 32, LeaguePlayers).Prospects), Fingerprint(Class.Prospects));
    TestEqual(TEXT("A smaller league drafts a smaller class"), Generator->GenerateDraftClass(LeagueSeed, 2027, 4, LeaguePlayers).Prospects.Num(), 4 * Shipped.DraftClass.ProspectsPerTeam);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 -- Written content
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSLeagueGeneratorWriteTest,
    "PlaySports.Content.LeagueGenerator.WritesValidContent",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSLeagueGeneratorWriteTest::RunTest(const FString& Parameters)
{
    using namespace PSLeagueGeneratorTests;

    const FPSGeneratedLeague League = GenerateShippedLeague(LeagueSeed);
    if (!TestEqual(TEXT("The league is generated"), League.Teams.Num(), 32))
    {
        return false;
    }

    // CI's "Validate the generated league" step checks this directory with tools/content.py.
    const FString Root = FPaths::ProjectSavedDir() / TEXT("GeneratedLeague");
    IFileManager::Get().DeleteDirectory(*Root, false, true);
    TArray<FString> Written;
    TestTrue(TEXT("The league is written"), UPSLeagueGenerator::WriteLeague(League, Root, Written));
    TestEqual(TEXT("A config, a teams file and a roster per team"), Written.Num(), 2 + League.Teams.Num());
    TestTrue(TEXT("The config is where the game reads it"), Written.Contains(TEXT("Data/sample_league_config.json")));

    // The game's import of it, with the shipped route library and playbook beside it.
    for (const TCHAR* File : { TEXT("Data/sample_routes.json"), TEXT("Data/sample_playbook.json") })
    {
        IFileManager::Get().Copy(*(Root / File), *(FPaths::ProjectDir() / File));
    }
    TArray<FString> Errors;
    TArray<FString> Loaded;
    TestTrue(TEXT("It imports cleanly through the game's loaders"), UPSContentReimportCommandlet::ReimportAll(Root, Errors, Loaded));
    for (const FString& Error : Errors)
    {
        AddError(FString::Printf(TEXT("Generated content: %s"), *Error));
    }
    TestTrue(TEXT("The config's teams file is followed"), Loaded.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("Data/league_teams.json - 32 teams")); }));
    TestEqual(TEXT("Every roster loads"), Loaded.FilterByPredicate([](const FString& Line) { return Line.Contains(TEXT(" - 53 players (team '")); }).Num(), 32);

    // A roster reads back as it was generated.
    const FPSGeneratedTeam& Team = League.Teams[5];
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    TArray<FString> Problems;
    TestTrue(TEXT("The roster passes the player contract"), Ingestion->ValidatePlayersJson(Root / Team.Team.RosterDataTablePath, Problems));
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = FPlayerAttributes::StaticStruct();
    TestTrue(TEXT("...and loads"), Ingestion->LoadPlayerAttributesFromJson(Root / Team.Team.RosterDataTablePath, Table));
    bool bRoundTrips = Table->GetRowMap().Num() == Team.Players.Num();
    for (const FPlayerAttributes& Player : Team.Players)
    {
        const FPlayerAttributes* Read = Table->FindRow<FPlayerAttributes>(Player.PlayerId, TEXT("PSLeagueGeneratorTests"));
        bRoundTrips &= Read && Read->DisplayName == Player.DisplayName && Read->Role == Player.Role && Read->Age == Player.Age
            && Fingerprint({ *Read }) == Fingerprint({ Player });
    }
    TestTrue(TEXT("Every player reads back as generated: ratings, age and DNA"), bRoundTrips);

    // A new franchise plays with the generated rosters.
    UPSRoster* Roster = UPSLeagueGenerator::MakeRoster(Team, GetTransientPackage());
    const FPlayerAttributes* FirstQuarterback = Team.Players.FindByPredicate([](const FPlayerAttributes& Player) { return Player.Role == EPlayerRole::Quarterback; });
    TestTrue(TEXT("The best quarterback starts"), FirstQuarterback && Roster->GetStarterId(EPlayerRole::Quarterback) == FirstQuarterback->PlayerId);
    UPSFranchiseFlow* Flow = NewObject<UPSFranchiseFlow>();
    for (const FPSGeneratedTeam& Generated : League.Teams)
    {
        Flow->SetTeamRoster(Generated.Team.TeamId, UPSLeagueGenerator::MakeRoster(Generated, Flow));
    }
    TestTrue(TEXT("Every team has its roster"), Flow->GetTeamRoster(League.Teams.Last().Team.TeamId) && Flow->GetTeamRoster(League.Teams.Last().Team.TeamId)->GetFullRoster().Num() == 53);
    UPSContractManager* Contracts = NewObject<UPSContractManager>();
    Contracts->LoadTuningFromJson(UPSContractManager::GetDefaultTuningPath());
    Contracts->StartLeague();
    TestEqual(TEXT("A generated team signs its whole roster under the cap"), Contracts->SignRosterAtDemand(Team.Team.TeamId, Team.Players), Team.Players.Num());
    return true;
}

#endif
