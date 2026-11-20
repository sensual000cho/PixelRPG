#pragma once

#include "CoreMinimal.h"
#include "Combat/PixelCharacterBase.h"
#include "PixelPlayerCharacter.generated.h"

class APixelCameraRig;
class APixelCompanionCharacter;
class APixelInteractable;
class UAnimSequence;
class USkeletalMeshComponent;
class UPixelInventoryComponent;
class UPixelQuestComponent;

/**
 * The melee protagonist.
 * Controls: WASD move, left mouse / J attack, Space dodge, Tab / R swap weapon,
 *           G mount / deploy companion, 1/2/3 order follow / hold / focus, T auto mode, F bolt, C empower,
 *           E interact (gather, talk), F5 save, F9 load,
 *           mouse look, Z/X zoom, V third person / top down (Left/Right rotate there), F1 toggle the pixel effect.
 * Command line: -PixelCombatTest fights on its own, -PixelQuestTest also plays through the quests.
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

	/** False for a weapon the game mode keeps locked (APixelGameMode::QuestLockedWeapons) until a quest unlocks it. */
	bool IsWeaponAvailable(int32 Index) const;

	/** Sets max health from the quest rewards. bHeal also refills health (used when a reward is handed out). */
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

	/** Degrees of camera rotation per unit of mouse movement. Changed in game with [ and ], kept in GameUserSettings.ini. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MouseSensitivity = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float RespawnSeconds = 2.f;

	/**
	 * The rigged hero's animations are shown at this many frames per second, like hand-drawn sprite frames
	 * (they are keyed at 24). 0 = smooth.
	 */
	UPROPERTY(EditAnywhere, Category = "Visual")
	float AnimFrameRate = 15.f;

protected:
	virtual void HandleDeath(AActor* Killer) override;
	virtual int32 GetPoseIndex() const override;

private:
	/** Picks the hero's animation and its frame from what it is doing (attack phase, dodge, hurt, walk, idle). */
	void UpdateBodyAnimation();
	UAnimSequence* FindAnim(const FString& Name) const;

	/** The rigged hero model (SK_Hero, from Desktop/Project_PixelRPG/Hero/make_hero.py). Replaces the flip-book BodyMesh. */
	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<USkeletalMeshComponent> BodySkeleton;

	/** A_Hero_<Name> by name (Idle_Sword, Run_Spear, Combo1, ...). */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UAnimSequence>> BodyAnims;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ShownAnim;

	/** Switches to the next available weapon. False if there is no other one. */
	bool SwapWeapon();
	/** After a Tab that did nothing: tells the player which quest unlocks the locked weapon. */
	void ExplainLockedWeapon();
	// The input actions that quests can ask for: each does the thing and tells the quest log when it worked.
	void ToggleView();
	void ToggleCompanionMount();
	void ToggleCompanionAutoCast();
	void CastCompanionBolt();
	void CastCompanionEmpower();
	/** Starts the next attack of the combo (or the weapon's only attack). */
	void StartAttack();
	void Respawn();
	APixelInteractable* FindFocus() const;
	/** What the -PixelQuestTest autoplay should walk to and use next, or null to just fight. */
	APixelInteractable* FindQuestTestGoal() const;
	/** What the autoplay should attack: the nearest enemy, skipping training dummies no quest asks for. */
	APixelCharacterBase* FindAutoplayTarget() const;
	/** -PixelQuestTest: does what the quests ask for that walking and fighting does not cover. */
	void AutoplayQuestActions();
	float NextAutoplayAction = 0.f;
	FVector StuckCheckLocation = FVector::ZeroVector;
	float NextStuckCheck = 0.f;
	float DetourUntil = 0.f;
	float DetourSide = 1.f;

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
