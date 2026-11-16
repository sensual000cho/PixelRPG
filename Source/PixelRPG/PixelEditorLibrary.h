#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PixelEditorLibrary.generated.h"

class UMaterialInterface;

/** Editor helpers for Tools/setup_pixel_assets.py: things the Python API cannot do by itself. */
UCLASS()
class PIXELRPG_API UPixelEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates a square landscape from height samples. Editor only; returns null in a game build.
	 *
	 * @param Heights            Landscape height values (32768 = the Z of the actor, 128 steps per unit of Scale.Z / 100),
	 *                           row by row along X, (Components * QuadsPerComponent + 1) squared of them.
	 * @param Location           World position of the first sample (the -X -Y corner).
	 * @param Scale              World units between samples in X and Y; Z scales the heights.
	 */
	UFUNCTION(BlueprintCallable, Category = "Pixel|Editor")
	static AActor* CreateLandscape(UWorld* World, const TArray<int32>& Heights, int32 Components, int32 QuadsPerComponent,
		FVector Location, FVector Scale, UMaterialInterface* Material);
};
