// PSBallLook.h - Epic 147.4: what the ball looks like, from Data/ball_look.json
#pragma once

#include "CoreMinimal.h"
#include "PSBallLook.generated.h"

class UStaticMeshComponent;

/**
 * The ball's look (Data/ball_look.json; Architecture rule 4): a mesh stretched to a football's
 * size, its long axis along the ball's X, in one colour through a dynamic material instance. No
 * asset of its own: by default an engine sphere. Its collision is the ball's own sphere
 * (APSBall's CollisionComponent), so the look never changes how the ball flies or lands.
 * Defaults equal the file.
 */
USTRUCT(BlueprintType)
struct FPSBallLookStyle
{
    GENERATED_BODY()

    /** A mesh MeshSizeCm across at scale 1, centred on its pivot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball")
    FString MeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball")
    float MeshSizeCm = 100.f;

    /** Tip to tip (11 in), and across the middle (about 7 in). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball")
    float LengthCm = 28.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball")
    float WidthCm = 17.f;

    /** A material with a vector parameter named ColorParameter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball")
    FString MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball")
    FName ColorParameter = TEXT("Color");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ball")
    FString BallColor = TEXT("#6B3A1E");
};

/** Puts the look on a ball's mesh. Pure, apart from loading the style and its assets. */
namespace PSBallLook
{
    PLAYSPORTS_API FString GetDefaultStylePath();

    /** Data/ball_look.json through UPSDataIngestion; the defaults when it is missing or unsound. */
    PLAYSPORTS_API FPSBallLookStyle LoadStyle(const FString& Path);

    /** Problems with Style, one line each (empty when sound); mirrors tools/validate_data.py. */
    PLAYSPORTS_API TArray<FString> ValidateStyle(const FPSBallLookStyle& Style);

    /** Sets Mesh's static mesh, its scale (LengthCm along X, WidthCm across) and a dynamic
     *  instance of the material in BallColor, made with Outer. The mesh keeps no collision.
     *  False, with the reason logged, when the mesh or the material can't be loaded. */
    PLAYSPORTS_API bool Apply(UStaticMeshComponent* Mesh, const FPSBallLookStyle& Style, UObject* Outer);
}
