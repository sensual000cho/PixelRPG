#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PixelGameMode.generated.h"

class APixelEnemyCharacter;
class APixelPlayerCharacter;

/**
 * Spawns the player and owns the save game. The map (PixelTest, built by Tools/setup_pixel_assets.py) places
 * everything else: the village NPCs, gathering nodes, quest markers, training dummies and the enemy spawners.
 * Optionally keeps wandering slimes alive around the player (MaxEnemies).
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

	/** Wandering slimes kept alive around the player. 0: the map places camps of spawners around the valley instead. */
	UPROPERTY(EditAnywhere, Category = "Combat")
	int32 MaxEnemies = 0;

	/** Most enemies that fight the player (and companion) at the same time; the rest wait (APixelEnemyCharacter::FindTarget). */
	UPROPERTY(EditAnywhere, Category = "Combat")
	int32 MaxEngagedEnemies = 5;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SpawnInterval = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SpawnMinDistance = 700.f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SpawnMaxDistance = 1200.f;

	UPROPERTY(EditAnywhere, Category = "Save")
	float AutosaveInterval = 30.f;

	/** Weapons (FPixelWeaponDef::Name) the player cannot switch to until a quest reward unlocks them. */
	UPROPERTY(EditAnywhere, Category = "Quest")
	TArray<FName> QuestLockedWeapons;

protected:
	FString SaveSlot = TEXT("PixelSlot0");

private:
	/** Logs what the map placed for the quests, so a broken map shows up in the log. */
	void LogWorldObjects() const;
	void SpawnEnemies();
	void LoadOnStart();
	void Autosave();
	/** Height of the terrain below a point, ignoring houses, rocks and anything else standing on it. */
	bool FindGround(const FVector2D& Point, FVector& OutLocation) const;
	APixelPlayerCharacter* GetPlayer() const;

	int32 KillCount = 0;
	float LastSaveTime = -100.f;
	FTimerHandle SpawnTimer;
	FTimerHandle AutosaveTimer;
};
