#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Combat/PixelWeaponTypes.h"
#include "PixelAbility_Melee.generated.h"

/**
 * One melee attack with the avatar's current weapon: windup -> hit check -> recovery.
 * Used by both the player and enemies; all numbers come from FPixelWeaponDef.
 */
UCLASS()
class PIXELRPG_API UPixelAbility_Melee : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UPixelAbility_Melee();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	UFUNCTION()
	void OnWindupFinished();

	UFUNCTION()
	void OnAttackFinished();

	FPixelWeaponDef ActiveWeapon;
};

/** Short invulnerable dash in the given direction. */
UCLASS()
class PIXELRPG_API UPixelAbility_Dodge : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UPixelAbility_Dodge();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UPROPERTY(EditDefaultsOnly, Category = "Dodge")
	float Speed = 950.f;

	UPROPERTY(EditDefaultsOnly, Category = "Dodge")
	float Duration = 0.28f;

private:
	UFUNCTION()
	void OnDodgeFinished();
};
