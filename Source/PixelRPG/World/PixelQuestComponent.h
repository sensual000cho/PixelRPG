#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PixelQuestComponent.generated.h"

class UPixelInventoryComponent;

UENUM()
enum class EPixelQuestState : uint8
{
	NotStarted,
	Active,
	Completed,
};

/**
 * The one quest of the world slice, "The Trampled Garden": bring the herbalist some Moonleaf
 * and defeat a few slimes. Lives on the player; the herbalist NPC starts and finishes it.
 */
UCLASS()
class PIXELRPG_API UPixelQuestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPixelQuestComponent();

	EPixelQuestState GetState() const { return State; }
	bool IsCompleted() const { return State == EPixelQuestState::Completed; }
	int32 GetKills() const { return Kills; }
	/** Herbs carried, capped at what the quest asks for. */
	int32 GetHerbs() const;
	/** Active with every objective met: time to go back to the quest giver. */
	bool IsReadyToTurnIn() const;

	bool Accept();
	/** Takes the herbs and pays the reward. False if the objectives are not met. */
	bool TurnIn();
	void NotifyEnemyKilled();
	void Restore(EPixelQuestState NewState, int32 NewKills);

	/** Title and objective lines for the HUD; empty when there is nothing to track. */
	void GetTrackerLines(TArray<FString>& OutLines) const;

	static const FName HerbItem;
	static const FName CoinItem;

	UPROPERTY(EditAnywhere, Category = "Quest")
	int32 HerbsNeeded = 3;

	UPROPERTY(EditAnywhere, Category = "Quest")
	int32 KillsNeeded = 3;

	UPROPERTY(EditAnywhere, Category = "Quest")
	int32 CoinReward = 50;

private:
	UPixelInventoryComponent* GetInventory() const;

	EPixelQuestState State = EPixelQuestState::NotStarted;
	int32 Kills = 0;
};
