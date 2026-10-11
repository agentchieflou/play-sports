// PSCrowdRenderTests.cpp -- Epic 48: the fans in the stadium's seats
//
// Tests covered:
//   1. Data/crowd_look.json loads through UPSDataIngestion, equals FPSCrowdLookStyle's defaults and
//      validates; an unsound style is caught.
//   2. The crowd fills the stadium's seats: the density's share of them, the same fans every time,
//      the home share's split with the away fans mostly together in pockets, one shirt instance per
//      fan and a head per figure, none for cards, nobody at density 0 or detail None.
//   3. The fans stand as the excitement reaches their thresholds, rising by StandRiseCm, sit again
//      under the hysteresis, and at most MaxStandChangesPerUpdate move per update.
//   4. The shirts wear the match's team colours from the team identity data, and the defaults for a
//      team that has none.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PSCharacterLook.h"
#include "PSCrowdLookTypes.h"
#include "PSCrowdRenderComponent.h"
#include "PSDataIngestion.h"
#include "PSFieldDimensions.h"
#include "PSStadiumSet.h"
#include "PSStadiumSetTypes.h"
#include "PSUITeamCatalog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PSCrowdRenderTests
{
    /** A test world with a stadium set that isn't built: its crowd component is what's tested. */
    struct FCrowdWorld
    {
        UWorld* World = nullptr;
        APSStadiumSet* Set = nullptr;

        FCrowdWorld()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            if (World)
            {
                FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
                Context.SetCurrentWorld(World);
                FActorSpawnParameters SpawnParams;
                SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                Set = World->SpawnActor<APSStadiumSet>(APSStadiumSet::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
            }
        }

        ~FCrowdWorld()
        {
            if (World)
            {
                GEngine->DestroyWorldContext(World);
                World->DestroyWorld(false);
            }
        }

        UPSCrowdRenderComponent* Crowd() const
        {
            return Set ? Set->GetCrowd() : nullptr;
        }
    };

    /** The default bowl's seats: the crowd sits in the stadium's own seats. */
    const TArray<FPSStadiumSeat>& BowlSeats()
    {
        static const TArray<FPSStadiumSeat> Seats = APSStadiumSet::ComputeLayout(PSField::GetDimensions(), FPSStadiumSetStyle(), EPSStadiumDetail::Reduced).Seats;
        return Seats;
    }

    int32 InstancesIn(const TArray<UInstancedStaticMeshComponent*>& Meshes)
    {
        int32 Count = 0;
        for (const UInstancedStaticMeshComponent* Mesh : Meshes)
        {
            Count += Mesh ? Mesh->GetInstanceCount() : 0;
        }
        return Count;
    }

    TArray<UInstancedStaticMeshComponent*> ShirtMeshes(const UPSCrowdRenderComponent* Crowd, const FPSCrowdLookStyle& Style)
    {
        TArray<UInstancedStaticMeshComponent*> Meshes;
        for (int32 Shirt = 0; Shirt < PSCrowdShirt::FirstNeutral + Style.NeutralColors.Num(); ++Shirt)
        {
            Meshes.Add(Crowd->GetShirtMesh(Shirt));
        }
        return Meshes;
    }

    TArray<UInstancedStaticMeshComponent*> HeadMeshes(const UPSCrowdRenderComponent* Crowd, const FPSCrowdLookStyle& Style)
    {
        TArray<UInstancedStaticMeshComponent*> Meshes;
        for (int32 Skin = 0; Skin < Style.SkinTones.Num(); ++Skin)
        {
            Meshes.Add(Crowd->GetHeadMesh(Skin));
        }
        return Meshes;
    }
}

// ---------------------------------------------------------------------------
// Test 1 -- The look's data file
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCrowdLookDataTest,
    "PlaySports.Crowd.LookData",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCrowdLookDataTest::RunTest(const FString& Parameters)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSCrowdLookStyle Loaded;
    if (!TestTrue(TEXT("Data/crowd_look.json loads"), Ingestion->LoadCrowdLookStyleFromJson(UPSCrowdRenderComponent::GetDefaultStylePath(), Loaded)))
    {
        return false;
    }
    TestEqual(TEXT("...and validates"), UPSCrowdRenderComponent::ValidateStyle(Loaded).Num(), 0);

    const FPSCrowdLookStyle Defaults;
    TestEqual(TEXT("BodyMeshPath"), Loaded.BodyMeshPath, Defaults.BodyMeshPath);
    TestEqual(TEXT("MeshSizeCm"), Loaded.MeshSizeCm, Defaults.MeshSizeCm);
    TestEqual(TEXT("MaterialPath"), Loaded.MaterialPath, Defaults.MaterialPath);
    TestEqual(TEXT("ColorParameter"), Loaded.ColorParameter, Defaults.ColorParameter);
    TestEqual(TEXT("Seed"), Loaded.Seed, Defaults.Seed);
    TestEqual(TEXT("TorsoWidthCm"), Loaded.TorsoWidthCm, Defaults.TorsoWidthCm);
    TestEqual(TEXT("TorsoDepthCm"), Loaded.TorsoDepthCm, Defaults.TorsoDepthCm);
    TestEqual(TEXT("TorsoHeightCm"), Loaded.TorsoHeightCm, Defaults.TorsoHeightCm);
    TestEqual(TEXT("HeadSizeCm"), Loaded.HeadSizeCm, Defaults.HeadSizeCm);
    TestEqual(TEXT("CardDepthCm"), Loaded.CardDepthCm, Defaults.CardDepthCm);
    TestEqual(TEXT("SizeVariation"), Loaded.SizeVariation, Defaults.SizeVariation);
    TestEqual(TEXT("YawJitterDegrees"), Loaded.YawJitterDegrees, Defaults.YawJitterDegrees);
    TestEqual(TEXT("PrimaryShare"), Loaded.PrimaryShare, Defaults.PrimaryShare);
    TestEqual(TEXT("SecondaryShare"), Loaded.SecondaryShare, Defaults.SecondaryShare);
    TestEqual(TEXT("WhiteShare"), Loaded.WhiteShare, Defaults.WhiteShare);
    TestEqual(TEXT("WhiteColor"), Loaded.WhiteColor, Defaults.WhiteColor);
    TestTrue(TEXT("NeutralColors"), Loaded.NeutralColors == Defaults.NeutralColors);
    TestTrue(TEXT("SkinTones"), Loaded.SkinTones == Defaults.SkinTones);
    TestEqual(TEXT("DefaultHomePrimary"), Loaded.DefaultHomePrimary, Defaults.DefaultHomePrimary);
    TestEqual(TEXT("DefaultHomeSecondary"), Loaded.DefaultHomeSecondary, Defaults.DefaultHomeSecondary);
    TestEqual(TEXT("DefaultAwayPrimary"), Loaded.DefaultAwayPrimary, Defaults.DefaultAwayPrimary);
    TestEqual(TEXT("DefaultAwaySecondary"), Loaded.DefaultAwaySecondary, Defaults.DefaultAwaySecondary);
    TestEqual(TEXT("AwayPocketShare"), Loaded.AwayPocketShare, Defaults.AwayPocketShare);
    TestEqual(TEXT("AwayPocketYardLine"), Loaded.AwayPocketYardLine, Defaults.AwayPocketYardLine);
    TestEqual(TEXT("AwayPocketLateralYards"), Loaded.AwayPocketLateralYards, Defaults.AwayPocketLateralYards);
    TestEqual(TEXT("AlwaysStandShare"), Loaded.AlwaysStandShare, Defaults.AlwaysStandShare);
    TestEqual(TEXT("StandExcitementMin"), Loaded.StandExcitementMin, Defaults.StandExcitementMin);
    TestEqual(TEXT("StandExcitementMax"), Loaded.StandExcitementMax, Defaults.StandExcitementMax);
    TestEqual(TEXT("StandHysteresis"), Loaded.StandHysteresis, Defaults.StandHysteresis);
    TestEqual(TEXT("StandRiseCm"), Loaded.StandRiseCm, Defaults.StandRiseCm);
    TestEqual(TEXT("StandForwardCm"), Loaded.StandForwardCm, Defaults.StandForwardCm);
    TestEqual(TEXT("MaxStandChangesPerUpdate"), Loaded.MaxStandChangesPerUpdate, Defaults.MaxStandChangesPerUpdate);

    FPSCrowdLookStyle Broken;
    Broken.SkinTones.Reset();
    Broken.PrimaryShare = 0.9f;
    Broken.StandExcitementMin = 0.99f;
    Broken.WhiteColor = TEXT("white");
    TestEqual(TEXT("An unsound style is caught, one problem each"), UPSCrowdRenderComponent::ValidateStyle(Broken).Num(), 4);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 -- The crowd fills the seats
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCrowdPopulatesTest,
    "PlaySports.Crowd.FillsTheSeats",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCrowdPopulatesTest::RunTest(const FString& Parameters)
{
    using namespace PSCrowdRenderTests;

    FCrowdWorld Stage;
    UPSCrowdRenderComponent* Crowd = Stage.Crowd();
    if (!TestNotNull(TEXT("A stadium set with its crowd"), Crowd))
    {
        return false;
    }
    const TArray<FPSStadiumSeat>& Seats = BowlSeats();
    const FPSCrowdLookStyle Style;
    const float Density = 0.6f;
    const float HomeShare = 0.8f;
    if (!TestTrue(TEXT("The crowd fills the seats"), Crowd->Populate(Seats, 44.f, Style, Density, EPSCrowdDetail::Figures, HomeShare)))
    {
        return false;
    }

    const int32 Fans = Crowd->GetNumFans();
    TestTrue(*FString::Printf(TEXT("About %.0f%% of %d seats are taken (%d)"), Density * 100.f, Seats.Num(), Fans), FMath::Abs(Fans - Density * Seats.Num()) < 0.02f * Seats.Num());
    TestEqual(TEXT("A shirt for every fan"), InstancesIn(ShirtMeshes(Crowd, Style)), Fans);
    TestEqual(TEXT("A head for every figure"), InstancesIn(HeadMeshes(Crowd, Style)), Fans);

    int32 Home = 0;
    TSet<int32> SeatsTaken;
    TMap<int32, int32> AwayBySection;
    for (const FPSCrowdFan& Fan : Crowd->GetFans())
    {
        SeatsTaken.Add(Fan.Seat);
        if (Fan.bHome)
        {
            ++Home;
            TestTrue(TEXT("A home fan wears home colours or neutral ones"), Fan.Shirt != PSCrowdShirt::AwayPrimary && Fan.Shirt != PSCrowdShirt::AwaySecondary);
        }
        else
        {
            ++AwayBySection.FindOrAdd(Seats[Fan.Seat].Deck * 1000 + Seats[Fan.Seat].Section);
        }
    }
    TestEqual(TEXT("One fan to a seat"), SeatsTaken.Num(), Fans);
    TestTrue(*FString::Printf(TEXT("The home crowd is its share (%.3f)"), static_cast<float>(Home) / Fans), FMath::Abs(static_cast<float>(Home) / Fans - HomeShare) < 0.03f);

    // The away fans sit mostly together: the sections where they are the majority hold most of them.
    const int32 Away = Fans - Home;
    TMap<int32, int32> FansBySection;
    for (const FPSCrowdFan& Fan : Crowd->GetFans())
    {
        ++FansBySection.FindOrAdd(Seats[Fan.Seat].Deck * 1000 + Seats[Fan.Seat].Section);
    }
    int32 InAwaySections = 0;
    for (const TPair<int32, int32>& Section : AwayBySection)
    {
        if (Section.Value * 2 > FansBySection[Section.Key])
        {
            InAwaySections += Section.Value;
        }
    }
    TestTrue(*FString::Printf(TEXT("Most away fans sit in their pockets (%d of %d)"), InAwaySections, Away), InAwaySections >= Style.AwayPocketShare * Away * 0.9f);

    // The same crowd every time.
    TArray<FPSCrowdFan> First = Crowd->GetFans();
    Crowd->Populate(Seats, 44.f, Style, Density, EPSCrowdDetail::Figures, HomeShare);
    bool bSame = First.Num() == Crowd->GetNumFans();
    for (int32 Index = 0; bSame && Index < First.Num(); ++Index)
    {
        const FPSCrowdFan& Again = Crowd->GetFans()[Index];
        bSame = Again.Seat == First[Index].Seat && Again.Shirt == First[Index].Shirt && Again.Skin == First[Index].Skin && Again.bHome == First[Index].bHome;
    }
    TestTrue(TEXT("The same seed seats the same crowd"), bSame);
    TestEqual(TEXT("...replacing the first one's instances"), InstancesIn(ShirtMeshes(Crowd, Style)), Crowd->GetNumFans());

    // A phone's cards: one instance a fan, no heads.
    Crowd->Populate(Seats, 44.f, Style, Density, EPSCrowdDetail::Cards, HomeShare);
    TestEqual(TEXT("Cards: a card for every fan"), InstancesIn(ShirtMeshes(Crowd, Style)), Crowd->GetNumFans());
    TestNull(TEXT("...and no heads"), Crowd->GetHeadMesh(0));

    Crowd->Populate(Seats, 44.f, Style, 0.f, EPSCrowdDetail::Figures, HomeShare);
    TestEqual(TEXT("Density 0: an empty stadium"), Crowd->GetNumFans(), 0);
    Crowd->Populate(Seats, 44.f, Style, Density, EPSCrowdDetail::None, HomeShare);
    TestEqual(TEXT("Detail None: no crowd"), Crowd->GetNumFans(), 0);
    TestNull(TEXT("...and no meshes"), Crowd->GetShirtMesh(0));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 -- The fans stand with the excitement
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCrowdStandsTest,
    "PlaySports.Crowd.StandsWithExcitement",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCrowdStandsTest::RunTest(const FString& Parameters)
{
    using namespace PSCrowdRenderTests;

    FCrowdWorld Stage;
    UPSCrowdRenderComponent* Crowd = Stage.Crowd();
    if (!TestNotNull(TEXT("A stadium set with its crowd"), Crowd))
    {
        return false;
    }
    // A few sections' worth of seats keeps every change within one update.
    TArray<FPSStadiumSeat> Seats;
    for (const FPSStadiumSeat& Seat : BowlSeats())
    {
        if (Seat.Section < 3)
        {
            Seats.Add(Seat);
        }
    }
    FPSCrowdLookStyle Style;
    Style.MaxStandChangesPerUpdate = Seats.Num();
    if (!TestTrue(TEXT("The crowd fills the seats"), Crowd->Populate(Seats, 44.f, Style, 1.f, EPSCrowdDetail::Figures, 1.f)))
    {
        return false;
    }
    const int32 Fans = Crowd->GetNumFans();
    const int32 AlwaysUp = Crowd->GetNumStanding();
    TestTrue(*FString::Printf(TEXT("A few fans stand all game (%d of %d)"), AlwaysUp, Fans), AlwaysUp > 0 && AlwaysUp < Fans * (Style.AlwaysStandShare + 0.05f));

    TestEqual(TEXT("A quiet crowd stays seated"), Crowd->ApplyExcitement(Style.StandExcitementMin - 0.1f), 0);

    // Find a fan who sits, and where his shirt is.
    int32 Sitter = INDEX_NONE;
    for (int32 Index = 0; Index < Fans && Sitter == INDEX_NONE; ++Index)
    {
        Sitter = Crowd->GetFans()[Index].bStanding ? INDEX_NONE : Index;
    }
    if (!TestTrue(TEXT("Someone is sitting"), Sitter != INDEX_NONE))
    {
        return false;
    }
    const FPSCrowdFan Seated = Crowd->GetFans()[Sitter];
    FTransform Before;
    Crowd->GetShirtMesh(Seated.Shirt)->GetInstanceTransform(Seated.ShirtInstance, Before);

    const int32 Rose = Crowd->ApplyExcitement(1.f);
    TestEqual(TEXT("An erupting crowd is on its feet"), Crowd->GetNumStanding(), Fans);
    TestEqual(TEXT("...everyone who sat rose"), Rose, Fans - AlwaysUp);
    FTransform After;
    Crowd->GetShirtMesh(Seated.Shirt)->GetInstanceTransform(Seated.ShirtInstance, After);
    TestEqual(TEXT("A fan who stands rises by StandRiseCm"), After.GetLocation().Z - Before.GetLocation().Z, static_cast<double>(Style.StandRiseCm), 0.01);
    FTransform Head;
    Crowd->GetHeadMesh(Seated.Skin)->GetInstanceTransform(Seated.HeadInstance, Head);
    TestTrue(TEXT("...and his head is on his shoulders"), Head.GetLocation().Z > After.GetLocation().Z);

    // Just under a standing fan's threshold he stays up; well under it he sits.
    const float Threshold = Crowd->GetFans()[Sitter].StandThreshold;
    Crowd->ApplyExcitement(Threshold - Style.StandHysteresis * 0.5f);
    TestTrue(TEXT("The hysteresis keeps him standing just under his threshold"), Crowd->GetFans()[Sitter].bStanding);
    Crowd->ApplyExcitement(0.f);
    TestEqual(TEXT("A hushed crowd sits back down"), Crowd->GetNumStanding(), AlwaysUp);
    Crowd->GetShirtMesh(Seated.Shirt)->GetInstanceTransform(Seated.ShirtInstance, After);
    TestTrue(TEXT("...back in his seat"), After.GetLocation().Equals(Before.GetLocation(), 0.01));

    // The budget: at most MaxStandChangesPerUpdate move per update, the rest the next one.
    Style.MaxStandChangesPerUpdate = 10;
    Crowd->Populate(Seats, 44.f, Style, 1.f, EPSCrowdDetail::Figures, 1.f);
    TestEqual(TEXT("An update moves at most its budget"), Crowd->ApplyExcitement(1.f), 10);
    TestEqual(TEXT("...and the next update the next ones"), Crowd->ApplyExcitement(1.f), 10);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 -- The shirts are the teams' colours
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPSCrowdTeamColorsTest,
    "PlaySports.Crowd.TeamColors",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSCrowdTeamColorsTest::RunTest(const FString& Parameters)
{
    using namespace PSCrowdRenderTests;

    FCrowdWorld Stage;
    UPSCrowdRenderComponent* Crowd = Stage.Crowd();
    if (!TestNotNull(TEXT("A stadium set with its crowd"), Crowd))
    {
        return false;
    }
    const FPSCrowdLookStyle Style;
    Crowd->Populate(BowlSeats(), 44.f, Style, 0.2f, EPSCrowdDetail::Cards, 0.85f);

    FLinearColor Expected;
    UPSUITeamCatalog::ParseHexColor(Style.DefaultHomePrimary, Expected);
    TestTrue(TEXT("Before the match is known the home fans wear the default colours"), Crowd->GetShirtColor(PSCrowdShirt::HomePrimary).Equals(Expected, 0.001f));

    FLinearColor HomePrimary;
    FLinearColor HomeSecondary;
    FLinearColor AwayPrimary;
    FLinearColor AwaySecondary;
    const FString Teams = UPSUITeamCatalog::GetDefaultTeamsPath();
    if (!TestTrue(TEXT("The team identity data has the Falcons and the Hawks"),
        UPSCharacterLookComponent::FindTeamColors(Teams, TEXT("Falcons"), HomePrimary, HomeSecondary)
        && UPSCharacterLookComponent::FindTeamColors(Teams, TEXT("Hawks"), AwayPrimary, AwaySecondary)))
    {
        return false;
    }
    Crowd->SetTeamColors(TEXT("Falcons"), TEXT("Hawks"));
    TestTrue(TEXT("The home fans wear the home team's primary"), Crowd->GetShirtColor(PSCrowdShirt::HomePrimary).Equals(HomePrimary, 0.001f));
    TestTrue(TEXT("...and its secondary"), Crowd->GetShirtColor(PSCrowdShirt::HomeSecondary).Equals(HomeSecondary, 0.001f));
    TestTrue(TEXT("The away fans wear the visitors' primary"), Crowd->GetShirtColor(PSCrowdShirt::AwayPrimary).Equals(AwayPrimary, 0.001f));
    TestTrue(TEXT("...and their secondary"), Crowd->GetShirtColor(PSCrowdShirt::AwaySecondary).Equals(AwaySecondary, 0.001f));

    Crowd->SetTeamColors(TEXT("NoSuchTeam"), TEXT("Hawks"));
    TestTrue(TEXT("A team with no colours wears the defaults"), Crowd->GetShirtColor(PSCrowdShirt::HomePrimary).Equals(Expected, 0.001f));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
