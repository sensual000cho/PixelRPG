#pragma once

#include "CoreMinimal.h"
#include "Combat/PixelCharacterBase.h"
#include "PixelPlayerCharacter.generated.h"

class APixelCameraRig;
class APixelCompanionCharacter;
class APixelInteractable;
class UPixelInventoryComponent;
class UPixelQuestComponent;

/**
 * The melee protagonist.
 * Controls: WASD move, left mouse / J attack, Space dodge, Tab / R swap weapon,
 *           G mount / deploy companion, 1/2/3 order follow / hold / focus, T auto mode, F bolt, C empower,
 *           E interact (gather, talk), F5 save, F9 load,
 *           mouse look, Z/X zoom, V third person / top down (Left/Right rotate there), F1 toggle the pixel effect.
 * Command line: -PixelCombatTest fights on its own, -PixelQuestTest also plays through the quest.
 */
UCLASS()
class PIXELRPG_API APixelPlayerCharacter : public APixelCharacterBase
{
	GENERATED_BODY()

public:
	APixelPlayerCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void HandleDamaged(float Amount, AActor* DamageInstigator) override;
	virtual void NotifyHitLanded(int32 TargetCount) override;

	APixelCameraRig* GetCameraRig() const { return CameraRig; }
	APixelCompanionCharacter* GetCompanion() const { return Companion; }
	/** Closest living enemy within the distance, or null. */
	APixelCharacterBase* FindNearestEnemy(float MaxDistance) const;
	int32 GetWeaponIndex() const { return WeaponIndex; }

	UPixelInventoryComponent* GetInventory() const { return Inventory; }
	UPixelQuestComponent* GetQuest() const { return Quest; }
	/** The usable thing the player is standing next to, or null. */
	APixelInteractable* GetFocus() const { return Focus; }

	/** Short pop-up line ("+1 Moonleaf"). */
	void ShowNotice(const FString& Text, float Seconds = 2.5f);
	/** Speech of an NPC; lines are separated by a line break. */
	void ShowDialogue(const FString& Speaker, const FString& Text, float Seconds = 6.f);
	bool GetNotice(FString& OutText) const;
	bool GetDialogue(FString& OutSpeaker, FString& OutText) const;

	/** Sets max health from the quest reward. bHeal also refills health (used when the reward is handed out). */
	void ApplyQuestBonus(bool bHeal);
	void RestoreFromSave(const FVector& Location, float Health, int32 SavedWeaponIndex);

	/** The weapon sets the player can swap between. */
	UPROPERTY(EditAnywhere, Category = "Combat")
	TArray<FPixelWeaponDef> Weapons;

	/**
	 * The attacks that follow the sword's first one when attacks are chained: slash, backhand, rising cut, stab.
	 * Each has its own three poses (see hero_rows in Tools/make_assets_blender.py), in this order.
	 */
	UPROPERTY(EditAnywhere, Category = "Combat")
	TArray<FPixelWeaponDef> SwordCombo;

	/** Attacking again within this time after an attack ends continues the combo; later starts it over. */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float ComboWindow = 0.45f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float DodgeCooldown = 0.6f;

	/** Degrees of camera rotation per unit of mouse movement. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MouseSensitivity = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float RespawnSeconds = 2.f;

protected:
	virtual void HandleDeath(AActor* Killer) override;
	virtual int32 GetPoseIndex() const override;

private:
	void SwapWeapon();
	/** Starts the next attack of the combo (or the weapon's only attack). */
	void StartAttack();
	void Respawn();
	APixelInteractable* FindFocus() const;
	/** What the -PixelQuestTest autoplay should walk to and use next, or null to just fight. */
	APixelInteractable* FindQuestTestGoal() const;

	UPROPERTY(VisibleAnywhere, Category = "World")
	TObjectPtr<UPixelInventoryComponent> Inventory;

	UPROPERTY(VisibleAnywhere, Category = "World")
	TObjectPtr<UPixelQuestComponent> Quest;

	UPROPERTY(Transient)
	TObjectPtr<APixelInteractable> Focus;

	UPROPERTY(Transient)
	TObjectPtr<APixelCameraRig> CameraRig;

	UPROPERTY(Transient)
	TObjectPtr<APixelCompanionCharacter> Companion;

	FVector SpawnLocation = FVector::ZeroVector;
	int32 WeaponIndex = 0;
	/** Attack being shown: 0 is the weapon's own, 1.. are the entries of SwordCombo. */
	int32 ComboStep = 0;
	float ComboUntil = -100.f;
	float LastDodgeTime = -100.f;
	float BaseHealth = 100.f;
	FString NoticeText;
	float NoticeUntil = -1.f;
	FString DialogueSpeaker;
	FString DialogueText;
	float DialogueUntil = -1.f;
	FTimerHandle RespawnTimer;
};
