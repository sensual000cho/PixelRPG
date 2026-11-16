#include "Combat/PixelGameplayEffects.h"

#include "Combat/PixelAttributeSet.h"
#include "Combat/PixelGameplayTags.h"

UPixelGE_Damage::UPixelGE_Damage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FSetByCallerFloat Magnitude;
	Magnitude.DataTag = PixelTags::Data_Damage;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UPixelAttributeSet::GetDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::AddBase;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
	Modifiers.Add(Modifier);
}

UPixelGE_Empower::UPixelGE_Empower()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(DurationSeconds));

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UPixelAttributeSet::GetAttackPowerAttribute();
	Modifier.ModifierOp = EGameplayModOp::AddBase;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(0.5f));
	Modifiers.Add(Modifier);
}
