#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "World/PixelQuestTypes.h"
#include "PixelQuestComponent.generated.h"

class UPixelInventoryComponent;

/**
 * The player's quest log: state and progress of every quest in PixelQuestCatalog.
 * NPCs offer and take back quests through it; the world reports what the player does with the Notify* calls.
 */
UCLASS()
class PIXELRPG_API UPixelQuestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPixelQuestComponent();

	EPixelQuestState GetState(FName QuestId) const;
	bool IsCompleted(FName QuestId) const { return GetState(QuestId) == EPixelQuestState::Completed; }
	/** Not started yet, and every quest it requires is completed. */
	bool IsAvailable(FName QuestId) const;
	/** Active with every objective met: time to go back to the quest giver. */
	bool IsReadyToTurnIn(FName QuestId) const;
	/** How far an objective is, capped at its count. */
	int32 GetProgress(const FPixelQuestDef& Quest, int32 ObjectiveIndex) const;

	bool Accept(FName QuestId);
	/** Takes the collected items and pays the reward. False if the objectives are not met. */
	bool TurnIn(FName QuestId);

	/** What the NPC has for the player: a quest to hand in, a quest to offer, or one still under way. Null if none. */
	const FPixelQuestDef* FindTurnInFor(FName NpcId) const;
	const FPixelQuestDef* FindOfferFor(FName NpcId) const;
	const FPixelQuestDef* FindActiveFor(FName NpcId) const;

	void NotifyAction(FName Action, int32 Amount = 1);
	void NotifyReached(FName MarkerId);
	void NotifyEnemyKilled(const AActor* Enemy);
	/** Call after the bag changed, so Collect objectives can report being done. */
	void NotifyItemsChanged();

	/** Is an active quest waiting for this right now? Used by markers, spawners and the autoplay test. */
	bool WantsReach(FName MarkerId) const;
	bool WantsAction(FName Action) const;
	bool WantsItem(FName ItemId) const;
	bool WantsKill(FName EnemyTag) const;

	/** Sum of the max health rewards of all completed quests. */
	float GetBonusMaxHealth() const;
	/** True if a completed quest gave this weapon. */
	bool HasUnlockedWeapon(FName WeaponName) const;

	TArray<FPixelQuestRecord> GetRecords() const;
	void RestoreRecords(const TArray<FPixelQuestRecord>& Records);

	/** Title and objective lines of the active quests for the HUD. Objective lines start with two spaces. */
	void GetTrackerLines(TArray<FString>& OutLines) const;

private:
	struct FQuestProgress
	{
		EPixelQuestState State = EPixelQuestState::NotStarted;
		TArray<int32> Counts;
		/** What the last refresh saw, to notice objectives that just got done. */
		TArray<bool> WasDone;
		bool bWasReady = false;
	};

	UPixelInventoryComponent* GetInventory() const;
	FQuestProgress& FindOrAdd(const FPixelQuestDef& Quest);
	const FQuestProgress* Find(FName QuestId) const { return Progress.Find(QuestId); }
	/** Adds Amount to every active objective of this type and target. */
	void AddProgress(EPixelObjective Type, FName Target, int32 Amount);
	/** Tells the player about objectives that just got done and quests that are ready to hand in. */
	void Refresh();
	void Tell(const FString& Text) const;

	TMap<FName, FQuestProgress> Progress;
};
