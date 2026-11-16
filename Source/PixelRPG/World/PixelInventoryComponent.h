#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PixelInventoryComponent.generated.h"

/** Stackable items by id. The id doubles as the display name ("Moonleaf", "Copper Ore", "Coin"). */
UCLASS()
class PIXELRPG_API UPixelInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPixelInventoryComponent();

	int32 GetCount(FName ItemId) const;
	void Add(FName ItemId, int32 Amount);
	/** Removes the amount if there is enough of it; otherwise changes nothing and returns false. */
	bool Remove(FName ItemId, int32 Amount);

	const TMap<FName, int32>& GetItems() const { return Items; }
	void SetItems(const TMap<FName, int32>& NewItems) { Items = NewItems; }

private:
	UPROPERTY()
	TMap<FName, int32> Items;
};
