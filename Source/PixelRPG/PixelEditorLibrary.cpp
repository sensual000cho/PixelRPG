#include "PixelEditorLibrary.h"

#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

#if WITH_EDITOR
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeProxy.h"
#endif

AActor* UPixelEditorLibrary::CreateLandscape(UWorld* World, const TArray<int32>& Heights, int32 Components, int32 QuadsPerComponent,
	FVector Location, FVector Scale, UMaterialInterface* Material)
{
#if WITH_EDITOR
	const int32 Size = Components * QuadsPerComponent + 1;
	if (!World || Components < 1 || QuadsPerComponent < 1 || Heights.Num() != Size * Size)
	{
		UE_LOG(LogTemp, Error, TEXT("PixelEditor: CreateLandscape needs %d x %d heights, got %d"), Size, Size, Heights.Num());
		return nullptr;
	}

	TArray<uint16> HeightData;
	HeightData.Reserve(Heights.Num());
	for (const int32 Height : Heights)
	{
		HeightData.Add(static_cast<uint16>(FMath::Clamp(Height, 0, 65535)));
	}

	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator);
	if (!Landscape)
	{
		return nullptr;
	}
	Landscape->LandscapeMaterial = Material;
	Landscape->SetActorRelativeScale3D(Scale);

	// One layer of data under the default (invalid) guid, as the New Landscape tool does.
	TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
	HeightDataPerLayer.Add(FGuid(), MoveTemp(HeightData));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialDataPerLayer;
	MaterialDataPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());
	Landscape->Import(FGuid::NewGuid(), 0, 0, Size - 1, Size - 1, 1, QuadsPerComponent, HeightDataPerLayer, TEXT(""),
		MaterialDataPerLayer, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());

	// Always full detail: the world is small, and props are placed on the ground exactly.
	for (ULandscapeComponent* Component : Landscape->LandscapeComponents)
	{
		if (Component)
		{
			Component->SetForcedLOD(0);
		}
	}
	Landscape->MarkPackageDirty();
	UE_LOG(LogTemp, Display, TEXT("PixelEditor: landscape created, %d x %d samples in %d components"), Size, Size, Landscape->LandscapeComponents.Num());
	return Landscape;
#else
	return nullptr;
#endif
}
