#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "World/PixelQuestTypes.h"
#include "PixelSaveGame.generated.h"

/** Everything the world slice remembers between sessions. Written and read by APixelGameMode. */
UCLASS()
class PIXELRPG_API UPixelSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** 1: one quest (QuestState, QuestKills). 2: every quest in Quests. */
	UPROPERTY()
	int32 Version = 2;

	UPROPERTY()
	FVector PlayerLocation = FVector::ZeroVector;

	UPROPERTY()
	float PlayerHealth = 100.f;

	UPROPERTY()
	int32 WeaponIndex = 0;

	UPROPERTY()
	TMap<FName, int32> Items;

	UPROPERTY()
	TArray<FPixelQuestRecord> Quests;

	/** Version 1 only, read when loading an old save. */
	UPROPERTY()
	uint8 QuestState = 0;

	UPROPERTY()
	int32 QuestKills = 0;

	UPROPERTY()
	int32 KillCount = 0;

	/** Gathering nodes that were empty when the game was saved. They start growing back on load. */
	UPROPERTY()
	TArray<FName> DepletedNodes;
};
