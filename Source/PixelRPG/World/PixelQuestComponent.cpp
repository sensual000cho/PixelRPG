#include "World/PixelQuestComponent.h"

#include "Combat/PixelPlayerCharacter.h"
#include "GameFramework/Actor.h"
#include "PixelAudio.h"
#include "World/PixelInventoryComponent.h"

UPixelQuestComponent::UPixelQuestComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UPixelInventoryComponent* UPixelQuestComponent::GetInventory() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UPixelInventoryComponent>() : nullptr;
}

UPixelQuestComponent::FQuestProgress& UPixelQuestComponent::FindOrAdd(const FPixelQuestDef& Quest)
{
	FQuestProgress& Entry = Progress.FindOrAdd(Quest.Id);
	Entry.Counts.SetNumZeroed(Quest.Objectives.Num());
	Entry.WasDone.SetNumZeroed(Quest.Objectives.Num());
	return Entry;
}

EPixelQuestState UPixelQuestComponent::GetState(FName QuestId) const
{
	const FQuestProgress* Entry = Find(QuestId);
	return Entry ? Entry->State : EPixelQuestState::NotStarted;
}

bool UPixelQuestComponent::IsAvailable(FName QuestId) const
{
	const FPixelQuestDef* Quest = PixelQuestCatalog::FindQuest(QuestId);
	if (!Quest || GetState(QuestId) != EPixelQuestState::NotStarted)
	{
		return false;
	}
	for (const FName Required : Quest->Requires)
	{
		if (!IsCompleted(Required))
		{
			return false;
		}
	}
	return true;
}

int32 UPixelQuestComponent::GetProgress(const FPixelQuestDef& Quest, int32 ObjectiveIndex) const
{
	if (!Quest.Objectives.IsValidIndex(ObjectiveIndex))
	{
		return 0;
	}
	const FPixelQuestObjective& Objective = Quest.Objectives[ObjectiveIndex];
	const FQuestProgress* Entry = Find(Quest.Id);
	if (!Entry || Entry->State == EPixelQuestState::NotStarted)
	{
		return 0;
	}
	if (Entry->State == EPixelQuestState::Completed)
	{
		return Objective.Count;
	}
	if (Objective.Type == EPixelObjective::Collect)
	{
		const UPixelInventoryComponent* Inventory = GetInventory();
		return Inventory ? FMath::Min(Inventory->GetCount(Objective.Target), Objective.Count) : 0;
	}
	return Entry->Counts.IsValidIndex(ObjectiveIndex) ? FMath::Min(Entry->Counts[ObjectiveIndex], Objective.Count) : 0;
}

bool UPixelQuestComponent::IsReadyToTurnIn(FName QuestId) const
{
	const FPixelQuestDef* Quest = PixelQuestCatalog::FindQuest(QuestId);
	if (!Quest || GetState(QuestId) != EPixelQuestState::Active)
	{
		return false;
	}
	for (int32 Index = 0; Index < Quest->Objectives.Num(); ++Index)
	{
		if (GetProgress(*Quest, Index) < Quest->Objectives[Index].Count)
		{
			return false;
		}
	}
	return true;
}

bool UPixelQuestComponent::Accept(FName QuestId)
{
	const FPixelQuestDef* Quest = PixelQuestCatalog::FindQuest(QuestId);
	if (!Quest || !IsAvailable(QuestId))
	{
		return false;
	}
	FQuestProgress& Entry = FindOrAdd(*Quest);
	Entry.State = EPixelQuestState::Active;
	for (int32& Count : Entry.Counts)
	{
		Count = 0;
	}
	UE_LOG(LogTemp, Display, TEXT("PixelQuest: accepted %s"), *QuestId.ToString());
	Refresh(); // a Collect objective may already be met by what the player carries
	return true;
}

bool UPixelQuestComponent::TurnIn(FName QuestId)
{
	const FPixelQuestDef* Quest = PixelQuestCatalog::FindQuest(QuestId);
	UPixelInventoryComponent* Inventory = GetInventory();
	if (!Quest || !Inventory || !IsReadyToTurnIn(QuestId))
	{
		return false;
	}
	if (!Quest->bKeepItems)
	{
		for (const FPixelQuestObjective& Objective : Quest->Objectives)
		{
			if (Objective.Type == EPixelObjective::Collect && !Inventory->Remove(Objective.Target, Objective.Count))
			{
				return false;
			}
		}
	}
	if (Quest->Reward.Coins > 0)
	{
		Inventory->Add(TEXT("Coin"), Quest->Reward.Coins);
	}
	FQuestProgress& Entry = FindOrAdd(*Quest);
	Entry.State = EPixelQuestState::Completed;
	Entry.bWasReady = false;
	UE_LOG(LogTemp, Display, TEXT("PixelQuest: completed %s, paid %d coin"), *QuestId.ToString(), Quest->Reward.Coins);
	Refresh(); // handing in items may undo another quest's Collect objective
	return true;
}

const FPixelQuestDef* UPixelQuestComponent::FindTurnInFor(FName NpcId) const
{
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		if (Quest.TurnIn == NpcId && IsReadyToTurnIn(Quest.Id))
		{
			return &Quest;
		}
	}
	return nullptr;
}

const FPixelQuestDef* UPixelQuestComponent::FindOfferFor(FName NpcId) const
{
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		if (Quest.Giver == NpcId && IsAvailable(Quest.Id))
		{
			return &Quest;
		}
	}
	return nullptr;
}

const FPixelQuestDef* UPixelQuestComponent::FindActiveFor(FName NpcId) const
{
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		if ((Quest.TurnIn == NpcId || Quest.Giver == NpcId) && GetState(Quest.Id) == EPixelQuestState::Active)
		{
			return &Quest;
		}
	}
	return nullptr;
}

void UPixelQuestComponent::AddProgress(EPixelObjective Type, FName Target, int32 Amount)
{
	bool bChanged = false;
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		if (GetState(Quest.Id) != EPixelQuestState::Active)
		{
			continue;
		}
		FQuestProgress& Entry = FindOrAdd(Quest);
		for (int32 Index = 0; Index < Quest.Objectives.Num(); ++Index)
		{
			const FPixelQuestObjective& Objective = Quest.Objectives[Index];
			if (Objective.Type == Type && Objective.Target == Target && Entry.Counts[Index] < Objective.Count)
			{
				Entry.Counts[Index] = FMath::Min(Entry.Counts[Index] + Amount, Objective.Count);
				bChanged = true;
				UE_LOG(LogTemp, Display, TEXT("PixelQuest: %s: %s %d/%d"), *Quest.Id.ToString(), *Objective.Text, Entry.Counts[Index], Objective.Count);
			}
		}
	}
	if (bChanged)
	{
		Refresh();
	}
}

void UPixelQuestComponent::NotifyAction(FName Action, int32 Amount)
{
	AddProgress(EPixelObjective::Action, Action, Amount);
}

void UPixelQuestComponent::NotifyReached(FName MarkerId)
{
	AddProgress(EPixelObjective::Reach, MarkerId, 1);
}

void UPixelQuestComponent::NotifyEnemyKilled(const AActor* Enemy)
{
	// Objectives without a tag take any enemy; tagged ones only enemies with that tag.
	AddProgress(EPixelObjective::Kill, NAME_None, 1);
	if (Enemy)
	{
		for (const FName Tag : Enemy->Tags)
		{
			AddProgress(EPixelObjective::Kill, Tag, 1);
		}
	}
}

void UPixelQuestComponent::NotifyItemsChanged()
{
	Refresh();
}

bool UPixelQuestComponent::WantsReach(FName MarkerId) const
{
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		for (int32 Index = 0; Index < Quest.Objectives.Num(); ++Index)
		{
			const FPixelQuestObjective& Objective = Quest.Objectives[Index];
			if (Objective.Type == EPixelObjective::Reach && Objective.Target == MarkerId && GetState(Quest.Id) == EPixelQuestState::Active
				&& GetProgress(Quest, Index) < Objective.Count)
			{
				return true;
			}
		}
	}
	return false;
}

bool UPixelQuestComponent::WantsAction(FName Action) const
{
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		for (int32 Index = 0; Index < Quest.Objectives.Num(); ++Index)
		{
			const FPixelQuestObjective& Objective = Quest.Objectives[Index];
			if (Objective.Type == EPixelObjective::Action && Objective.Target == Action && GetState(Quest.Id) == EPixelQuestState::Active
				&& GetProgress(Quest, Index) < Objective.Count)
			{
				return true;
			}
		}
	}
	return false;
}

bool UPixelQuestComponent::WantsItem(FName ItemId) const
{
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		for (int32 Index = 0; Index < Quest.Objectives.Num(); ++Index)
		{
			const FPixelQuestObjective& Objective = Quest.Objectives[Index];
			if (Objective.Type == EPixelObjective::Collect && Objective.Target == ItemId && GetState(Quest.Id) == EPixelQuestState::Active
				&& GetProgress(Quest, Index) < Objective.Count)
			{
				return true;
			}
		}
	}
	return false;
}

bool UPixelQuestComponent::WantsKill(FName EnemyTag) const
{
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		for (int32 Index = 0; Index < Quest.Objectives.Num(); ++Index)
		{
			const FPixelQuestObjective& Objective = Quest.Objectives[Index];
			if (Objective.Type == EPixelObjective::Kill && Objective.Target == EnemyTag && GetState(Quest.Id) == EPixelQuestState::Active
				&& GetProgress(Quest, Index) < Objective.Count)
			{
				return true;
			}
		}
	}
	return false;
}

float UPixelQuestComponent::GetBonusMaxHealth() const
{
	float Bonus = 0.f;
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		Bonus += IsCompleted(Quest.Id) ? Quest.Reward.MaxHealth : 0.f;
	}
	return Bonus;
}

bool UPixelQuestComponent::HasUnlockedWeapon(FName WeaponName) const
{
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		if (Quest.Reward.UnlockWeapon == WeaponName && IsCompleted(Quest.Id))
		{
			return true;
		}
	}
	return false;
}

TArray<FPixelQuestRecord> UPixelQuestComponent::GetRecords() const
{
	TArray<FPixelQuestRecord> Records;
	for (const TPair<FName, FQuestProgress>& Pair : Progress)
	{
		if (Pair.Value.State == EPixelQuestState::NotStarted)
		{
			continue;
		}
		FPixelQuestRecord& Record = Records.AddDefaulted_GetRef();
		Record.Id = Pair.Key;
		Record.State = static_cast<uint8>(Pair.Value.State);
		Record.Progress = Pair.Value.Counts;
	}
	return Records;
}

void UPixelQuestComponent::RestoreRecords(const TArray<FPixelQuestRecord>& Records)
{
	Progress.Reset();
	for (const FPixelQuestRecord& Record : Records)
	{
		const FPixelQuestDef* Quest = PixelQuestCatalog::FindQuest(Record.Id);
		if (!Quest)
		{
			UE_LOG(LogTemp, Warning, TEXT("PixelQuest: save has unknown quest %s, skipped"), *Record.Id.ToString());
			continue;
		}
		FQuestProgress& Entry = FindOrAdd(*Quest);
		Entry.State = static_cast<EPixelQuestState>(FMath::Min<uint8>(Record.State, static_cast<uint8>(EPixelQuestState::Completed)));
		for (int32 Index = 0; Index < Entry.Counts.Num(); ++Index)
		{
			Entry.Counts[Index] = Record.Progress.IsValidIndex(Index) ? FMath::Clamp(Record.Progress[Index], 0, Quest->Objectives[Index].Count) : 0;
		}
	}
	// Take the loaded state as already seen, so loading does not announce every finished objective.
	for (TPair<FName, FQuestProgress>& Pair : Progress)
	{
		const FPixelQuestDef* Quest = PixelQuestCatalog::FindQuest(Pair.Key);
		for (int32 Index = 0; Index < Pair.Value.WasDone.Num(); ++Index)
		{
			Pair.Value.WasDone[Index] = GetProgress(*Quest, Index) >= Quest->Objectives[Index].Count;
		}
		Pair.Value.bWasReady = IsReadyToTurnIn(Pair.Key);
	}
}

void UPixelQuestComponent::Refresh()
{
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		if (GetState(Quest.Id) != EPixelQuestState::Active)
		{
			continue;
		}
		FQuestProgress& Entry = FindOrAdd(Quest);
		FString Done;
		for (int32 Index = 0; Index < Quest.Objectives.Num(); ++Index)
		{
			const bool bDone = GetProgress(Quest, Index) >= Quest.Objectives[Index].Count;
			if (bDone && !Entry.WasDone[Index])
			{
				Done = Quest.Objectives[Index].Text;
			}
			Entry.WasDone[Index] = bDone;
		}
		const bool bReady = IsReadyToTurnIn(Quest.Id);
		if (bReady && !Entry.bWasReady)
		{
			const FPixelNpcDef* Npc = PixelQuestCatalog::FindNpc(Quest.TurnIn);
			Tell(FString::Printf(TEXT("%s - %s에게 보고하기"), *Quest.Title, Npc ? *Npc->DisplayName : *Quest.TurnIn.ToString()));
			UPixelAudioSubsystem::Play(this, EPixelSound::QuestReady);
			UE_LOG(LogTemp, Display, TEXT("PixelQuest: %s ready to turn in"), *Quest.Id.ToString());
		}
		else if (!Done.IsEmpty())
		{
			Tell(FString::Printf(TEXT("완료: %s"), *Done));
			UPixelAudioSubsystem::Play(this, EPixelSound::ItemObtained);
		}
		Entry.bWasReady = bReady;
	}
}

void UPixelQuestComponent::Tell(const FString& Text) const
{
	if (APixelPlayerCharacter* Player = Cast<APixelPlayerCharacter>(GetOwner()))
	{
		Player->ShowNotice(Text, 3.f);
	}
}

void UPixelQuestComponent::GetTrackerLines(TArray<FString>& OutLines) const
{
	OutLines.Reset();
	int32 Shown = 0;
	for (const FPixelQuestDef& Quest : PixelQuestCatalog::GetQuests())
	{
		if (GetState(Quest.Id) != EPixelQuestState::Active || Shown == 3)
		{
			continue;
		}
		++Shown;
		OutLines.Add(Quest.Title);
		if (IsReadyToTurnIn(Quest.Id))
		{
			const FPixelNpcDef* Npc = PixelQuestCatalog::FindNpc(Quest.TurnIn);
			OutLines.Add(FString::Printf(TEXT("  %s에게 보고하기"), Npc ? *Npc->DisplayName : *Quest.TurnIn.ToString()));
			continue;
		}
		for (int32 Index = 0; Index < Quest.Objectives.Num(); ++Index)
		{
			const FPixelQuestObjective& Objective = Quest.Objectives[Index];
			const int32 Count = GetProgress(Quest, Index);
			if (Count >= Objective.Count)
			{
				OutLines.Add(FString::Printf(TEXT("  %s  완료"), *Objective.Text));
			}
			else if (Objective.Count > 1)
			{
				OutLines.Add(FString::Printf(TEXT("  %s  %d/%d"), *Objective.Text, Count, Objective.Count));
			}
			else
			{
				OutLines.Add(FString::Printf(TEXT("  %s"), *Objective.Text));
			}
		}
	}
}
