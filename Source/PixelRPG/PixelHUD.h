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
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	/** Enemy health bars are hidden beyond this distance from the player (cm). */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float EnemyHealthBarRange = 1500.f;

private:
	/** The two lines of key help at the bottom. H toggles them; the choice is kept in GameUserSettings.ini. */
	bool bShowHelp = true;
};
