#include "Combat/PixelAbility_Melee.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystemComponent.h"
#include "Combat/PixelCharacterBase.h"
#include "Combat/PixelGameplayEffects.h"
#include "Combat/PixelGameplayTags.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

UPixelAbility_Melee::UPixelAbility_Melee()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer Tags;
	Tags.AddTag(PixelTags::Ability_Attack);
	SetAssetTags(Tags);

	ActivationOwnedTags.AddTag(PixelTags::State_Attacking);
	ActivationBlockedTags.AddTag(PixelTags::State_Dead);
	ActivationBlockedTags.AddTag(PixelTags::State_Stunned);
	ActivationBlockedTags.AddTag(PixelTags::State_Dodging);
}

void UPixelAbility_Melee::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	APixelCharacterBase* Character = Cast<APixelCharacterBase>(GetAvatarActorFromActorInfo());
	if (!Character || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Copy the weapon: swapping mid-attack must not change an attack that already started.
	ActiveWeapon = Character->GetWeapon();
	Character->PlayAttackVisual(ActiveWeapon);

	UAbilityTask_WaitDelay* Windup = UAbilityTask_WaitDelay::WaitDelay(this, ActiveWeapon.Windup);
	Windup->OnFinish.AddDynamic(this, &UPixelAbility_Melee::OnWindupFinished);
	Windup->ReadyForActivation();
}

void UPixelAbility_Melee::OnWindupFinished()
{
	APixelCharacterBase* Character = Cast<APixelCharacterBase>(GetAvatarActorFromActorInfo());
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (Character && SourceASC)
	{
		const FVector Origin = Character->GetActorLocation();
		const FVector Facing = Character->GetFacingDirection();

		FCollisionQueryParams Params(SCENE_QUERY_STAT(PixelMelee), false, Character);
		TArray<FOverlapResult> Overlaps;
		GetWorld()->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity,
			FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(ActiveWeapon.Range + 40.f), Params);

		TSet<APixelCharacterBase*> Hit;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			APixelCharacterBase* Target = Cast<APixelCharacterBase>(Overlap.GetActor());
			if (!Target || Hit.Contains(Target) || Target->TeamId == Character->TeamId || !Target->IsTargetable())
			{
				continue;
			}

			const FVector ToTarget = (Target->GetActorLocation() - Origin) * FVector(1.f, 1.f, 0.f);
			const float Distance = ToTarget.Size();
			const float TargetRadius = 30.f;
			if (Distance - TargetRadius > ActiveWeapon.Range)
			{
				continue;
			}
			// Widen the cone by the angle the target's body covers, so near misses at the edge still connect.
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(ToTarget.GetSafeNormal(), Facing)));
			const float Allowance = FMath::RadiansToDegrees(FMath::Atan2(TargetRadius, FMath::Max(Distance, 1.f)));
			if (Distance > TargetRadius && Angle > ActiveWeapon.ArcDegrees * 0.5f + Allowance)
			{
				continue;
			}
			Hit.Add(Target);

			FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UPixelGE_Damage::StaticClass(), 1.f);
			if (Spec.IsValid())
			{
				Spec.Data->SetSetByCallerMagnitude(PixelTags::Data_Damage, ActiveWeapon.Damage * Character->GetAttackPower());
				SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), Target->GetAbilitySystemComponent());
			}
			Target->ApplyKnockback(ToTarget.IsNearlyZero() ? Facing : ToTarget, ActiveWeapon.Knockback);
		}
		if (Hit.Num() > 0)
		{
			Character->NotifyHitLanded(Hit.Num());
		}
	}

	UAbilityTask_WaitDelay* Rest = UAbilityTask_WaitDelay::WaitDelay(this, ActiveWeapon.ActiveTime + ActiveWeapon.Recovery);
	Rest->OnFinish.AddDynamic(this, &UPixelAbility_Melee::OnAttackFinished);
	Rest->ReadyForActivation();
}

void UPixelAbility_Melee::OnAttackFinished()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

UPixelAbility_Dodge::UPixelAbility_Dodge()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer Tags;
	Tags.AddTag(PixelTags::Ability_Dodge);
	SetAssetTags(Tags);

	ActivationOwnedTags.AddTag(PixelTags::State_Dodging);
	ActivationOwnedTags.AddTag(PixelTags::State_Invulnerable);
	ActivationBlockedTags.AddTag(PixelTags::State_Dead);
	ActivationBlockedTags.AddTag(PixelTags::State_Attacking);

	// A dodge interrupts nothing by itself, but it cannot start mid-swing (see the blocked tag above).
}

void UPixelAbility_Dodge::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	APixelCharacterBase* Character = Cast<APixelCharacterBase>(GetAvatarActorFromActorInfo());
	if (!Character || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Character->StartDash(Character->GetFacingDirection(), Speed, Duration);

	UAbilityTask_WaitDelay* Wait = UAbilityTask_WaitDelay::WaitDelay(this, Duration);
	Wait->OnFinish.AddDynamic(this, &UPixelAbility_Dodge::OnDodgeFinished);
	Wait->ReadyForActivation();
}

void UPixelAbility_Dodge::OnDodgeFinished()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
