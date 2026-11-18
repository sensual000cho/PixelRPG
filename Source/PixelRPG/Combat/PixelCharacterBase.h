#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Combat/PixelWeaponTypes.h"
#include "GameFramework/Character.h"
#include "PixelCharacterBase.generated.h"

class UAbilitySystemComponent;
class UMaterialInstanceDynamic;
class UPixelAttributeSet;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Shared base of the player and enemies: ability system, health, team, and the
 * procedural placeholder visuals (static meshes, hop walk, weapon swing, hit flash).
 */
UCLASS(Abstract)
class PIXELRPG_API APixelCharacterBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	APixelCharacterBase();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }

	/** Called by the attribute set after damage was applied. */
	virtual void HandleDamaged(float Amount, AActor* DamageInstigator);

	bool IsDead() const;
	/** Can enemies pick this character as a target and hit it? */
	virtual bool IsTargetable() const { return !IsDead(); }
	float GetAttackPower() const;
	USceneComponent* GetVisualRoot() const { return VisualRoot; }
	float GetHealth() const;
	float GetMaxHealth() const;
	bool HasStateTag(const struct FGameplayTag& Tag) const;

	const FPixelWeaponDef& GetWeapon() const { return Weapon; }
	void SetWeapon(const FPixelWeaponDef& NewWeapon);

	FVector GetFacingDirection() const { return FRotator(0.f, FacingYaw, 0.f).Vector(); }
	void FaceDirection(const FVector& Direction, bool bInstant = false);
	FVector GetFeetLocation() const;

	/** Starts the weapon swing / thrust animation for one attack. */
	void PlayAttackVisual(const FPixelWeaponDef& AttackWeapon);
	/** Moves the character at a fixed velocity for a short time (dodge, lunge). */
	void StartDash(const FVector& Direction, float Speed, float Duration);
	void ApplyKnockback(const FVector& Direction, float Strength);
	bool TryAttack();
	/** Called by the melee ability when an attack connected with at least one target. */
	virtual void NotifyHitLanded(int32 TargetCount) {}

	UPROPERTY(EditAnywhere, Category = "Combat")
	int32 TeamId = 0;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float StartingHealth = 100.f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float StunOnHitSeconds = 0.f;

	/** How far hits throw it back: 1 = full knockback, 0 = immovable (heavy monsters). */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float KnockbackScale = 1.f;

	/**
	 * The model is only ever shown from this many angles around it, like the frames of a sprite sheet:
	 * it turns in steps, and keeps the same side to the camera while the camera circles. 0 = turn freely.
	 */
	UPROPERTY(EditAnywhere, Category = "Visual")
	int32 FacingSteps = 16;

protected:
	virtual void HandleDeath(AActor* Killer);
	void SetBodyMesh(const TCHAR* AssetPath);
	/**
	 * Flip-book animation: loads <Prefix>_00, _01, ... (one model per pose; Prefix is a package path such as
	 * /Game/Characters/Hero/SM_Hero, see the folders in Tools/setup_pixel_assets.py) and shows the
	 * one GetPoseIndex() names every frame.
	 */
	void LoadPoses(const TCHAR* Prefix, int32 Count);
	/** Seconds this character has existed, with a random start so groups do not move in step. */
	float GetAnimTime() const { return AnimTime; }
	/** True for a moment after taking damage. */
	bool IsHurt() const { return HurtTimeLeft > 0.f; }
	void ResetHealth();
	void ClearDeathVisual() { DeathTime = -1.f; }

	/** Which pose model to show (animation frame), for characters that have several. */
	virtual int32 GetPoseIndex() const { return 0; }
	/** Seconds spent walking without a stop; 0 while standing. */
	float GetWalkTime() const { return WalkTime; }
	/** Phase of the running attack animation: 0 wind-up, 1 strike, 2 recovery, or -1 when not attacking. */
	int32 GetAttackPhase() const;

	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	UPROPERTY()
	TObjectPtr<UPixelAttributeSet> Attributes;

	/** Rotates with the facing direction (X forward). All visuals hang below it. */
	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> SlashMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> PoseMeshes;

	UPROPERTY(EditAnywhere, Category = "Combat")
	FPixelWeaponDef Weapon;

	/** True when the body model already holds the weapon, so the separate weapon mesh stays hidden. */
	bool bWeaponInBody = false;

	/** Height of the weapon hand above the feet; the swing and the swoosh happen at this height. */
	float HandHeight = 100.f;

	/** Extra squash applied by subclasses (enemy telegraph). 0 = none, 1 = fully squashed. */
	float ExtraSquash = 0.f;
	float FacingYaw = 0.f;
	float TargetFacingYaw = 0.f;
	float FlashAmount = 0.f;

private:
	void UpdateVisuals(float DeltaSeconds);
	void EndStun();

	FPixelWeaponDef AttackVisualWeapon;
	float AttackVisualTime = -1.f;
	float WalkTime = 0.f;
	float AnimTime = 0.f;
	int32 ShownStep = 0;
	float HurtTimeLeft = 0.f;
	float DeathTime = -1.f;
	FVector DashDirection = FVector::ZeroVector;
	float DashSpeed = 0.f;
	float DashTimeLeft = 0.f;
	FTimerHandle StunTimer;
};
