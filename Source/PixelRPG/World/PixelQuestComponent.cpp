#include "World/PixelQuestComponent.h"

#include "GameFramework/Actor.h"
#include "PixelAudio.h"
#include "World/PixelInventoryComponent.h"

const FName UPixelQuestComponent::HerbItem(TEXT("Moonleaf"));
const FName UPixelQuestComponent::CoinItem(TEXT("Coin"));

UPixelQuestComponent::UPixelQuestComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UPixelInventoryComponent* UPixelQuestComponent::GetInventory() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UPixelInventoryComponent>() : nullptr;
}

int32 UPixelQuestComponent::GetHerbs() const
{
	const UPixelInventoryComponent* Inventory = GetInventory();
	return Inventory ? FMath::Min(Inventory->GetCount(HerbItem), HerbsNeeded) : 0;
}

bool UPixelQuestComponent::IsReadyToTurnIn() const
{
	return State == EPixelQuestState::Active && GetHerbs() >= HerbsNeeded && Kills >= KillsNeeded;
}

bool UPixelQuestComponent::Accept()
{
	if (State != EPixelQuestState::NotStarted)
	{
		return false;
	}
	State = EPixelQuestState::Active;
	Kills = 0;
	UE_LOG(LogTemp, Display, TEXT("PixelQuest: accepted"));
	return true;
}

bool UPixelQuestComponent::TurnIn()
{
	UPixelInventoryComponent* Inventory = GetInventory();
	if (!IsReadyToTurnIn() || !Inventory || !Inventory->Remove(HerbItem, HerbsNeeded))
	{
		return false;
	}
	Inventory->Add(CoinItem, CoinReward);
	State = EPixelQuestState::Completed;
	UE_LOG(LogTemp, Display, TEXT("PixelQuest: completed, paid %d coin"), CoinReward);
	return true;
}

void UPixelQuestComponent::NotifyEnemyKilled()
{
	if (State == EPixelQuestState::Active && Kills < KillsNeeded)
	{
		const bool bWasReady = IsReadyToTurnIn();
		++Kills;
		if (!bWasReady && IsReadyToTurnIn())
		{
			UPixelAudioSubsystem::Play(this, EPixelSound::QuestReady);
		}
		UE_LOG(LogTemp, Display, TEXT("PixelQuest: slimes %d/%d"), Kills, KillsNeeded);
	}
}

void UPixelQuestComponent::Restore(EPixelQuestState NewState, int32 NewKills)
{
	State = NewState;
	Kills = FMath::Clamp(NewKills, 0, KillsNeeded);
}

void UPixelQuestComponent::GetTrackerLines(TArray<FString>& OutLines) const
{
	OutLines.Reset();
	if (State != EPixelQuestState::Active)
	{
		return;
	}
	OutLines.Add(TEXT("The Trampled Garden"));
	if (IsReadyToTurnIn())
	{
		OutLines.Add(TEXT("Return to Herbalist Orlen"));
		return;
	}
	OutLines.Add(FString::Printf(TEXT("Gather Moonleaf  %d/%d"), GetHerbs(), HerbsNeeded));
	OutLines.Add(FString::Printf(TEXT("Defeat slimes  %d/%d"), Kills, KillsNeeded));
}
