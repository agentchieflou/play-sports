// PSStadiumSetTypes.h - Epics 147.1 and 52: the stadium around the field, from Data/stadium_set.json
#pragma once

#include "CoreMinimal.h"
#include "PSStadiumSetTypes.generated.h"

/**
 * One deck of the bowl (Data/stadium_set.json's Decks): rows of seats rising away from the field
 * all the way round it. Offsets are measured outward from the field wall, which stands at the
 * ground's edge; heights from the field. Human-scale sizes are in cm: a seat row is the same depth
 * whatever the field's scale.
 */
USTRUCT(BlueprintType)
struct FPSStadiumDeck
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FName DeckId = TEXT("Lower");

    /** How far behind the field wall the front row's front edge is. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float FrontOffsetCm = 150.f;

    /** The front row's tread, above the field. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float FrontHeightCm = 160.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    int32 Rows = 28;

    /** Each row's tread depth, and how much higher it is than the row in front: the rake. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float RowDepthCm = 85.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float RowRiseCm = 42.f;

    /** 0: the deck is solid down to the ground, a lower bowl on its embankment. Above 0: each row
     *  is a slab this much deeper than its riser, so the deck hangs over what is below it and its
     *  underside steps up with the rows. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float SlabCm = 0.f;

    /** A solid band this tall under the front row, facing the field (the deck's ribbon board sits
     *  on it), topped by a parapet ParapetHeightCm above the front row's tread. 0: none. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float FasciaHeightCm = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float ParapetHeightCm = 0.f;

    /** Every VomitoryEverySections-th section of a straight (0: none) has a vomitory: a tunnel
     *  mouth VomitoryWidthCm wide where rows VomitoryFirstRow to VomitoryFirstRow +
     *  VomitoryRows - 1 drop to the tunnel's floor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    int32 VomitoryEverySections = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    int32 VomitoryFirstRow = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    int32 VomitoryRows = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float VomitoryWidthCm = 450.f;
};

/**
 * The bowl's plan, its seats and its colours (Data/stadium_set.json's Bowl).
 *
 * The plan is a rounded rectangle: the field wall stands WallOffsetCm past the ground's edge (the
 * out-of-bounds depth past the end lines and sidelines), its corners rounded to CornerRadiusCm,
 * and every row runs parallel to it. Aisles split each straight into sections about
 * SectionWidthCm wide and each corner into CornerSections; the seats of a section fill its row
 * between them.
 */
USTRUCT(BlueprintType)
struct FPSStadiumBowl
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float WallOffsetCm = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float CornerRadiusCm = 2200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float SectionWidthCm = 1300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    int32 CornerSections = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float AisleWidthCm = 120.f;

    /** No straight piece of a curved row is longer than this: how smooth the corners are. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float MaxSegmentCm = 300.f;

    /** A seat every SeatPitchCm along its row: a pan SeatWidthCm wide and SeatDepthCm deep,
     *  SeatHeightCm above the tread, and a back SeatBackHeightCm above the pan. The seat's centre
     *  is SeatSetbackCm behind the row's front edge, leaving the legroom in front. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float SeatPitchCm = 52.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float SeatWidthCm = 46.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float SeatDepthCm = 42.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float SeatHeightCm = 44.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float SeatBackHeightCm = 38.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float SeatSetbackCm = 50.f;

    /** The padded field wall, and the walkway behind it up to the first deck. Walls, fasciae and
     *  parapets are WallThicknessCm thick and capped with a rail RailHeightCm thick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float WallHeightCm = 190.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float WallThicknessCm = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float WalkwayHeightCm = 110.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float RailHeightCm = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString ConcreteColor = TEXT("#8E8B85");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString StairColor = TEXT("#B4AFA5");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString SeatColor = TEXT("#7E1B22");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString WallColor = TEXT("#1B2A4A");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString RailColor = TEXT("#2A2E34");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString FasciaColor = TEXT("#17191D");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString RibbonBoardColor = TEXT("#1E4FA3");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString GlassColor = TEXT("#22313F");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString PortalColor = TEXT("#0C0D0F");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString RoofColor = TEXT("#C3C6CB");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString FixtureColor = TEXT("#F1EFE7");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString BoardFrameColor = TEXT("#121417");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString BoardScreenColor = TEXT("#1C3557");
};

/**
 * What stands on and around the bowl (Data/stadium_set.json's Structures): the cross-aisle and
 * suites between decks, the ribbon boards, the back wall, the canopy and its light banks, the
 * press box and the video boards.
 */
USTRUCT(BlueprintType)
struct FPSStadiumStructures
{
    GENERATED_BODY()

    /** Behind each deck but the last: a cross-aisle CrossAisleCm deep at its top row's height,
     *  then the suites' facade, SuiteLevels bands of glass SuiteGlassHeightCm tall over a floor
     *  SuiteFloorCm thick, and concrete up to the deck above. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float CrossAisleCm = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    int32 SuiteLevels = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float SuiteGlassHeightCm = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float SuiteFloorCm = 150.f;

    /** A ribbon board RibbonHeightCm tall along each deck's fascia (0: none). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float RibbonHeightCm = 120.f;

    /** The back wall behind the last deck's top row, BackWallHeightCm above that row's tread. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BackWallHeightCm = 350.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BackWallThicknessCm = 60.f;

    /** A canopy cantilevered from the back wall over the last deck, CanopyDepthCm deep (0: none),
     *  its underside CanopyHeightCm above the top row's tread; along the sidelines only, or all
     *  the way round. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float CanopyDepthCm = 1400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float CanopyHeightCm = 900.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float CanopyThicknessCm = 80.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    bool bCanopySidelinesOnly = true;

    /** Light banks along the sidelines under the canopy's front edge (or on the back wall when
     *  there is no canopy): one every LightBankSpacingCm, LightBankWidthCm wide and
     *  LightBankHeightCm tall, aimed at the field LightBankTiltDegrees below level. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float LightBankSpacingCm = 1200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float LightBankWidthCm = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float LightBankHeightCm = 250.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float LightBankTiltDegrees = 35.f;

    /** The press box: at the 50 on the -Y sideline, on the first cross-aisle, PressBoxLengthCm
     *  long and PressBoxHeightCm tall, standing PressBoxDepthCm proud of the suites. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float PressBoxLengthCm = 6000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float PressBoxHeightCm = 450.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float PressBoxDepthCm = 250.f;

    /** A video board above each end on the back wall, facing the field: VideoBoardWidthCm by
     *  VideoBoardHeightCm (0: none), its bottom VideoBoardLiftCm above the wall, in a bezel
     *  VideoBoardBezelCm wide. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float VideoBoardWidthCm = 4000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float VideoBoardHeightCm = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float VideoBoardLiftCm = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float VideoBoardBezelCm = 60.f;
};

/**
 * How the stadium around the field is built (Data/stadium_set.json; Architecture rule 4). Where it
 * stands comes from the field's one frame (PSField, Data/field_dimensions.json). The goal posts and
 * benches are field furniture, sized in yards of that frame so they keep the field's proportions;
 * the bowl is sized for people, in cm. Defaults equal the file.
 */
USTRUCT(BlueprintType)
struct FPSStadiumSetStyle
{
    GENERATED_BODY()

    FPSStadiumSetStyle()
    {
        FPSStadiumDeck Lower;
        Lower.VomitoryEverySections = 3;
        Decks.Add(Lower);

        FPSStadiumDeck Upper;
        Upper.DeckId = TEXT("Upper");
        Upper.FrontOffsetCm = 2600.f;
        Upper.FrontHeightCm = 3000.f;
        Upper.Rows = 28;
        Upper.RowRiseCm = 55.f;
        Upper.SlabCm = 60.f;
        Upper.FasciaHeightCm = 280.f;
        Upper.ParapetHeightCm = 95.f;
        Decks.Add(Upper);
    }

    /** A box and a cylinder (axis up), MeshSizeCm across and tall at scale 1, centred on their
     *  pivots, and a material with a vector parameter named ColorParameter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString BoxMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString CylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float MeshSizeCm = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FName ColorParameter = TEXT("Color");

    /** The goal posts (NFL): the crossbar 10 ft up over the end line, uprights 18 ft 6 in apart
     *  rising 35 ft above it, on a base post set back behind the end line with a neck reaching
     *  forward to the crossbar. The base post is padded GoalPostPadHeightYards up, and a wind
     *  ribbon (42 in by 4 in) hangs from each upright's top. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString GoalPostColor = TEXT("#F2C230");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float CrossbarHeightYards = 3.3333f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float CrossbarWidthYards = 6.1667f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float UprightHeightYards = 11.6667f;

    /** The crossbar's and uprights' thickness, and the base post's and neck's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float PostDiameterYards = 0.1111f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BasePostDiameterYards = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BaseSetbackYards = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString GoalPostPadColor = TEXT("#1B2A4A");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float GoalPostPadHeightYards = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float GoalPostPadDiameterYards = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString RibbonColor = TEXT("#E8501E");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float RibbonLengthYards = 1.1667f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float RibbonWidthYards = 0.1111f;

    /** A team bench along each sideline, from one yard line to another, BenchDistanceYards past
     *  the sideline (inside the out-of-bounds depth). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString BenchColor = TEXT("#3A3F47");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BenchFromYardLine = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BenchToYardLine = 70.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BenchDistanceYards = 6.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BenchDepthYards = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BenchHeightYards = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FPSStadiumBowl Bowl;

    /** From the field outward: the first is the lower bowl, each next one above and behind it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    TArray<FPSStadiumDeck> Decks;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FPSStadiumStructures Structures;
};

/** What a piece of the stadium is: each kind is one instanced mesh in one colour. */
UENUM(BlueprintType)
enum class EPSStadiumPieceKind : uint8
{
    GoalPost,
    GoalPostPad,
    Ribbon,
    Bench,
    /** Risers, slabs, walkways, the cross-aisles and the back wall. */
    Concrete,
    /** The aisles' steps. */
    Stair,
    Seat,
    /** The padded field wall. */
    Wall,
    Rail,
    Fascia,
    RibbonBoard,
    Glass,
    /** A vomitory's tunnel mouth. */
    Portal,
    Roof,
    LightFixture,
    BoardFrame,
    BoardScreen,
    Count UMETA(Hidden)
};

/** One piece of the set: a box, or a cylinder along its local Z, placed and sized in world cm. */
USTRUCT(BlueprintType)
struct FPSStadiumPiece
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    EPSStadiumPieceKind Kind = EPSStadiumPieceKind::Concrete;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    bool bCylinder = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FVector Center = FVector::ZeroVector;

    /** A box's extent along X, Y and Z; a cylinder's diameter (X, Y) and length (Z), before Rotation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FVector Size = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FRotator Rotation = FRotator::ZeroRotator;
};

/** One seat in the bowl: where a fan sits (UPSCrowdRenderComponent fills them). */
USTRUCT(BlueprintType)
struct FPSStadiumSeat
{
    GENERATED_BODY()

    /** On the row's tread, under the seat's centre. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FVector Location = FVector::ZeroVector;

    /** The way the seat faces, toward the field, in degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float Yaw = 0.f;

    /** The section (between two aisles), numbered round the bowl, the same in every deck. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    int32 Section = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    int32 Deck = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    int32 Row = 0;
};

/** Everything the stadium set builds: its pieces, its seats and where its light banks aim from. */
USTRUCT(BlueprintType)
struct FPSStadiumLayout
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    TArray<FPSStadiumPiece> Pieces;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    TArray<FPSStadiumSeat> Seats;

    /** Each light bank's face: located at its centre, rotated so +X aims where it shines. Lane V2's
     *  night lighting can hang its lights here. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    TArray<FTransform> LightBanks;

    /** Sections round the bowl. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    int32 NumSections = 0;
};
