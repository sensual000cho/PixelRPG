#pragma once

#include "CoreMinimal.h"
#include "Combat/PixelCharacterBase.h"
#include "PixelEnemyCharacter.generated.h"


/**
 * Enemy with a small state machine: idle -> chase -> telegraph -> charge -> recover.
 * Its attack is a body tackle: it crouches, then throws itself at the target and hurts whoever it runs into.
 * Kept in C++ for the slice; this is the part to move to StateTree once there are several enemy types.
 */
UCLASS()
class PIXELRPG_API APixelEnemyCharacter : public APixelCharacterBase
{
	GENERATED_BODY()

public:
	APixelEnemyCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** True from the moment it starts winding up until the tackle is over: the HUD shows a warning mark. */
	virtual bool IsWarning() const { return State == EState::Telegraph || State == EState::Charge; }
	/** Height above the feet at which the HUD draws the warning mark: just over the head. */
	virtual float GetWarningHeight() const { return 78.f; }

	UPROPERTY(EditAnywhere, Category = "AI")
	float AggroRange = 1100.f;

	/** Distance at which it stops to wind up the tackle. Shorter than the charge travels. */
	UPROPERTY(EditAnywhere, Category = "AI")
	float AttackRange = 240.f;

	UPROPERTY(EditAnywhere, Category = "AI")
	float ChargeSpeed = 567.f;

	UPROPERTY(EditAnywhere, Category = "AI")
	float ChargeSeconds = 0.55f;

	/** Upward speed at the start of the charge: the tackle is a low leap. */
	UPROPERTY(EditAnywhere, Category = "AI")
	float ChargeHop = 270.f;

	/** Centre to centre distance at which the tackle connects. */
	UPROPERTY(EditAnywhere, Category = "AI")
	float TackleRadius = 72.f;

	UPROPERTY(EditAnywhere, Category = "AI")
	float TelegraphSeconds = 0.65f;

	UPROPERTY(EditAnywhere, Category = "AI")
	float RecoverSeconds = 0.9f;

protected:
	virtual void HandleDeath(AActor* Killer) override;
	virtual int32 GetPoseIndex() const override;

	/**
	 * Flip-book models loaded in BeginPlay: <PosePrefix>_00 ... Every enemy kind uses the same seven
	 * poses (see GetPoseIndex), so a new monster only needs its own models and numbers, not its own AI.
	 */
	FString PosePrefix = TEXT("/Game/Characters/Enemies/Slime/SM_Slime");
	int32 PoseCount = 7;

	// Building blocks for enemies with their own attack pattern (APixelMushroomEnemy).

	/**
	 * Closest hostile it can attack (the player, or the companion on the ground) within MaxDistance, or null.
	 * Also keeps the fight small: an enemy that is not fighting yet joins (once the target comes within AggroRange)
	 * only while fewer than APixelGameMode::MaxEngagedEnemies are; until then it gets null and waits where it is.
	 * Hitting it makes it join anyway.
	 */
	const APixelCharacterBase* FindTarget(float MaxDistance) const;

	/** True while it is one of the enemies fighting (see FindTarget). */
	bool IsEngaged() const { return bEngaged; }
	/** Damages and throws back (along ChargeDirection) the first hostile the body touches. False while nothing is touched. */
	bool TryTackleHit();

	FVector ChargeDirection = FVector::ForwardVector;

private:
	enum class EState : uint8 { Idle, Chase, Telegraph, Charge, Recover };

	EState State = EState::Idle;
	float StateTime = 0.f;

	/** Set and cleared by FindTarget (mutable because subclasses call it as a query). */
	mutable bool bEngaged = false;
};
