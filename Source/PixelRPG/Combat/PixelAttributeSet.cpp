#include "Combat/PixelAttributeSet.h"

#include "Combat/PixelCharacterBase.h"
#include "Combat/PixelGameplayTags.h"
#include "GameplayEffectExtension.h"

void UPixelAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute != GetDamageAttribute())
	{
		return;
	}

	const float Incoming = GetDamage();
	SetDamage(0.f);

	const UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
	if (Incoming <= 0.f || !ASC
		|| ASC->HasMatchingGameplayTag(PixelTags::State_Invulnerable)
		|| ASC->HasMatchingGameplayTag(PixelTags::State_Dead))
	{
		return;
	}

	SetHealth(FMath::Clamp(GetHealth() - Incoming, 0.f, GetMaxHealth()));

	if (APixelCharacterBase* Character = Cast<APixelCharacterBase>(GetOwningActor()))
	{
		Character->HandleDamaged(Incoming, Data.EffectSpec.GetContext().GetInstigator());
	}
}
