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
	bool IsWarning() const { return State == EState::Telegraph || State == EState::Charge; }

	UPROPERTY(EditAnywhere, Category = "AI")
	float AggroRange = 1100.f;

	/** Distance at which it stops to wind up the tackle. Shorter than the charge travels. */
	UPROPERTY(EditAnywhere, Category = "AI")
	float AttackRange = 240.f;

	UPROPERTY(EditAnywhere, Category = "AI")
	float ChargeSpeed = 820.f;

	UPROPERTY(EditAnywhere, Category = "AI")
	float ChargeSeconds = 0.38f;

	/** Upward speed at the start of the charge: the tackle is a low leap. */
	UPROPERTY(EditAnywhere, Category = "AI")
	float ChargeHop = 230.f;

	/** Centre to centre distance at which the tackle connects. */
	UPROPERTY(EditAnywhere, Category = "AI")
	float TackleRadius = 72.f;

	/** Radius of the warning hoops, and how high their centres hang above the ground. */
	UPROPERTY(EditAnywhere, Category = "AI")
	float RingRadius = 24.5f;

	UPROPERTY(EditAnywhere, Category = "AI")
	float RingHeight = 56.f;

	/** How fast the hoops drift from the enemy towards its target, in world units per second. */
	UPROPERTY(EditAnywhere, Category = "AI")
	float RingDriftSpeed = 36.f;

	UPROPERTY(EditAnywhere, Category = "AI")
	float TelegraphSeconds = 0.5f;

	UPROPERTY(EditAnywhere, Category = "AI")
	float RecoverSeconds = 0.9f;

protected:
	virtual void HandleDeath(AActor* Killer) override;
	virtual int32 GetPoseIndex() const override;

private:
	enum class EState : uint8 { Idle, Chase, Telegraph, Charge, Recover };

	/** Sets the line the hoops are on: from here, Length along Direction. */
	void AimTackleZone(const FVector& Direction, float Length);
	/** Places the hoops along that line, a little further towards the target every frame. */
	void UpdateTackleZone(float DeltaSeconds);
	void HideTackleZone();

	FVector ZoneOrigin = FVector::ZeroVector;
	FVector ZoneDirection = FVector::ForwardVector;
	float ZoneLength = 0.f;
	/** How far the hoops have drifted since they appeared. */
	float ZoneDrift = 0.f;

	/**
	 * Red hoops hanging in the air between the enemy and its target, drifting slowly towards the target.
	 * The line follows the aim while the enemy winds up and stays where it is during the charge.
	 */
	UPROPERTY(VisibleAnywhere, Category = "AI")
	TArray<TObjectPtr<UStaticMeshComponent>> TackleRings;

	/** Damages and throws back the first hostile the body touches. False while nothing is touched. */
	bool TryTackleHit();

	FVector ChargeDirection = FVector::ForwardVector;

	EState State = EState::Idle;
	float StateTime = 0.f;
};
