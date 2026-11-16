#include "World/PixelInventoryComponent.h"

UPixelInventoryComponent::UPixelInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 UPixelInventoryComponent::GetCount(FName ItemId) const
{
	const int32* Count = Items.Find(ItemId);
	return Count ? *Count : 0;
}

void UPixelInventoryComponent::Add(FName ItemId, int32 Amount)
{
	if (Amount > 0)
	{
		Items.FindOrAdd(ItemId) += Amount;
	}
}

bool UPixelInventoryComponent::Remove(FName ItemId, int32 Amount)
{
	int32* Count = Items.Find(ItemId);
	if (!Count || *Count < Amount)
	{
		return false;
	}
	*Count -= Amount;
	if (*Count <= 0)
	{
		Items.Remove(ItemId);
	}
	return true;
}
