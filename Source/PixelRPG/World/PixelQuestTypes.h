#pragma once

#include "CoreMinimal.h"
#include "PixelQuestTypes.generated.h"

/**
 * Quest data. The quests themselves are written in PixelQuestCatalog.cpp: one table, so a new quest is
 * a new entry there and nothing else. Who gives a quest is a FName NpcId that APixelNpc carries.
 */

enum class EPixelQuestState : uint8
{
	NotStarted,
	Active,
	Completed,
};

enum class EPixelObjective : uint8
{
	/** Walk into an APixelQuestMarker whose MarkerId is Target. */
	Reach,
	/** Do something Count times. Target is one of the PixelQuestActions names. */
	Action,
	/** Carry Count of item Target. The items are taken away on turn-in unless bKeepItems. */
	Collect,
	/** Defeat Count enemies that have the actor tag Target (None = any enemy). */
	Kill,
};

/** Names of the things the player can do that an Action objective can ask for. */
namespace PixelQuestActions
{
	inline const FName ToggleView(TEXT("ToggleView"));         // V
	inline const FName Dodge(TEXT("Dodge"));                   // Space
	inline const FName HitDummy(TEXT("HitDummy"));             // any hit on an APixelTrainingDummy
	inline const FName ComboFinisher(TEXT("ComboFinisher"));   // the 5th sword attack lands
	inline const FName CompanionDeploy(TEXT("CompanionDeploy"));// G while the companion rides along
	inline const FName ManualCast(TEXT("ManualCast"));         // T, switching the companion's auto-cast off
	inline const FName Bolt(TEXT("Bolt"));                     // F
	inline const FName Empower(TEXT("Empower"));               // C
	inline const FName SwapWeapon(TEXT("SwapWeapon"));         // Tab / R
}

struct FPixelQuestObjective
{
	EPixelObjective Type = EPixelObjective::Action;
	FName Target;
	int32 Count = 1;
	/** Tracker line, e.g. "Gather Moonleaf". The HUD adds "  2/3". */
	FString Text;
};

struct FPixelQuestReward
{
	int32 Coins = 0;
	/** Added to the player's max health for as long as the quest is completed. */
	float MaxHealth = 0.f;
	/** A weapon (FPixelWeaponDef::Name) the game mode keeps locked until this quest is done. */
	FName UnlockWeapon;
};

struct FPixelQuestDef
{
	FName Id;
	FString Title;
	/** NpcId of who offers the quest, and of who takes it back (usually the same). */
	FName Giver;
	FName TurnIn;
	/** Quests that must be completed before this one is offered. */
	TArray<FName> Requires;
	/** Lines are separated with \n; the dialogue box shows up to three. */
	FString OfferText;
	FString ProgressText;
	FString CompleteText;
	TArray<FPixelQuestObjective> Objectives;
	FPixelQuestReward Reward;
	bool bKeepItems = false;
};

struct FPixelNpcDef
{
	FName Id;
	FString DisplayName;
	/** Said when the NPC has nothing to give or take. */
	FString IdleText;
};

namespace PixelQuestCatalog
{
	const TArray<FPixelQuestDef>& GetQuests();
	const FPixelQuestDef* FindQuest(FName Id);
	const FPixelNpcDef* FindNpc(FName Id);
	/** Display name of an item (or weapon) id, e.g. "Moonleaf" -> its Korean name. Unknown ids are shown as they are. */
	FString GetItemName(FName ItemId);
}

/** One quest in the save game. */
USTRUCT()
struct FPixelQuestRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FName Id;

	UPROPERTY()
	uint8 State = 0;

	/** Per objective: how far along it is. Collect objectives are read from the bag instead. */
	UPROPERTY()
	TArray<int32> Progress;
};
