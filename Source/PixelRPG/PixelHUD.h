#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PixelHUD.generated.h"

/** Presents the low resolution scene capture of APixelCameraPawn: integer scale, nearest filter, sub-pixel scroll. */
UCLASS()
class PIXELRPG_API APixelHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Enemy health bars are hidden beyond this distance from the player (cm). */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float EnemyHealthBarRange = 1500.f;
};
