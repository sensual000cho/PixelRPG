#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "PixelAttributeSet.generated.h"

UCLASS()
class PIXELRPG_API UPixelAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS_BASIC(UPixelAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS_BASIC(UPixelAttributeSet, MaxHealth)

	/** Multiplier on outgoing weapon damage. 1 = normal; raised by buffs. */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS_BASIC(UPixelAttributeSet, AttackPower)

	/** Meta attribute: incoming damage. Consumed in PostGameplayEffectExecute and turned into a Health change. */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData Damage;
	ATTRIBUTE_ACCESSORS_BASIC(UPixelAttributeSet, Damage)

	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
};
