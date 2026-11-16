#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PixelGameMode.generated.h"

class APixelEnemyCharacter;
class APixelPlayerCharacter;

/**
 * Spawns the player, keeps a handful of enemies alive around them, places the quest giver and the
 * gathering nodes, and owns the save game.
 *
 * Command line: -PixelNewGame ignores and replaces an existing save, -PixelSaveSlot=<name> uses another slot.
 */
UCLASS()
class PIXELRPG_API APixelGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APixelGameMode();

	virtual void BeginPlay() override;

	void NotifyEnemyKilled(APixelEnemyCharacter* Enemy);
	int32 GetKillCount() const { return KillCount; }

	/** Writes the save slot. Skipped while the player is dead, so a save never holds a corpse. */
	bool SaveGame(const TCHAR* Reason);
	/** Applies the save slot to the running game. False if there is no usable save. */
	bool LoadGame();
	/** World time of the last successful save, for the "Saved" hint on the HUD. */
	float GetLastSaveTime() const { return LastSaveTime; }

	UPROPERTY(EditAnywhere, Category = "Combat")
	int32 MaxEnemies = 5;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SpawnInterval = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SpawnMinDistance = 700.f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SpawnMaxDistance = 1200.f;

	UPROPERTY(EditAnywhere, Category = "Save")
	float AutosaveInterval = 30.f;

private:
	void SpawnEnemies();
	void SpawnWorldObjects();
	void LoadOnStart();
	void Autosave();
	/** Height of the terrain below a point, ignoring houses, rocks and anything else standing on it. */
	bool FindGround(const FVector2D& Point, FVector& OutLocation) const;
	APixelPlayerCharacter* GetPlayer() const;

	int32 KillCount = 0;
	float LastSaveTime = -100.f;
	FString SaveSlot = TEXT("PixelSlot0");
	FTimerHandle SpawnTimer;
	FTimerHandle AutosaveTimer;
};
