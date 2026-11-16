#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Combat/PixelCharacterBase.h"
#include "PixelCompanionCharacter.generated.h"

UENUM()
enum class EPixelCompanionOrder : uint8
{
	Follow,	// stay near the partner
	Hold,	// stay where it is
	Focus,	// go after the partner's nearest enemy and attack it
};

/**
 * The spell-casting companion.
 *
 * Mounted: rides on the partner's back, cannot be targeted, casts from there.
 * Deployed: stands on the ground, follows orders, can be attacked. When its health
 * runs out it is downed for a while (no spells), then returns to the partner's back.
 *
 * Spells are cast on request (CastBolt / CastEmpower) and, in auto mode, by itself.
 */
UCLASS()
class PIXELRPG_API APixelCompanionCharacter : public APixelCharacterBase
{
	GENERATED_BODY()

public:
	APixelCompanionCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual int32 GetPoseIndex() const override;
	virtual bool IsTargetable() const override { return !IsDead() && !bMounted; }

	void SetPartner(APixelCharacterBase* NewPartner) { Partner = NewPartner; }
	APixelCharacterBase* GetPartner() const { return Partner; }
	APixelCharacterBase* GetSpellTarget() const { return SpellTarget.Get(); }

	bool IsMounted() const { return bMounted; }
	bool IsDowned() const { return IsDead(); }
	void Mount();
	void Deploy();
	/** Mounted -> deploy. Deployed -> run back to the partner and climb on. */
	void ToggleMount();

	void SetOrder(EPixelCompanionOrder NewOrder);
	EPixelCompanionOrder GetOrder() const { return Order; }
	void SetAutoCast(bool bEnabled) { bAutoCast = bEnabled; }
	bool IsAutoCast() const { return bAutoCast; }

	/** Fires a homing bolt at the target (or the nearest enemy in range). False if on cooldown or nothing to hit. */
	bool CastBolt(APixelCharacterBase* PreferredTarget = nullptr);
	/** Buffs the partner's attack power for a few seconds. */
	bool CastEmpower();

	/** 0 = ready, 1 = just cast. */
	float GetBoltCooldownFraction() const;
	float GetEmpowerCooldownFraction() const;
	FString GetStateText() const;

	UPROPERTY(EditAnywhere, Category = "Spells")
	float BoltRange = 950.f;

	UPROPERTY(EditAnywhere, Category = "Spells")
	float BoltDamage = 7.f;

	UPROPERTY(EditAnywhere, Category = "Spells")
	float BoltCooldown = 1.1f;

	UPROPERTY(EditAnywhere, Category = "Spells")
	float EmpowerCooldown = 12.f;

	UPROPERTY(EditAnywhere, Category = "AI")
	float FollowDistance = 240.f;

	UPROPERTY(EditAnywhere, Category = "AI")
	float DownedSeconds = 10.f;

protected:
	virtual void HandleDeath(AActor* Killer) override;

private:
	APixelCharacterBase* FindNearestEnemy(const FVector& From, float MaxDistance) const;
	void Revive();

	UPROPERTY(Transient)
	TObjectPtr<APixelCharacterBase> Partner;

	TWeakObjectPtr<APixelCharacterBase> SpellTarget;
	EPixelCompanionOrder Order = EPixelCompanionOrder::Follow;
	bool bMounted = false;
	bool bReturning = false;
	bool bAutoCast = true;
	float LastBoltTime = -100.f;
	float LastEmpowerTime = -100.f;
	FTimerHandle ReviveTimer;
};

/** Spawns a homing bolt from the companion toward its current spell target. */
UCLASS()
class PIXELRPG_API UPixelAbility_Bolt : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UPixelAbility_Bolt();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

/** Applies the Empower buff to the companion's partner. */
UCLASS()
class PIXELRPG_API UPixelAbility_Empower : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UPixelAbility_Empower();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

/** Homing magic bolt. Applies damage through the instigator's ability system when it reaches the target. */
UCLASS()
class PIXELRPG_API APixelProjectile : public AActor
{
	GENERATED_BODY()

public:
	APixelProjectile();

	virtual void Tick(float DeltaSeconds) override;

	void Launch(APixelCharacterBase* InSource, APixelCharacterBase* InTarget, float InDamage);

	UPROPERTY(EditAnywhere, Category = "Projectile")
	float Speed = 1500.f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> Mesh;

	TWeakObjectPtr<APixelCharacterBase> Source;
	TWeakObjectPtr<APixelCharacterBase> Target;
	FVector Direction = FVector::ForwardVector;
	float Damage = 0.f;
};
