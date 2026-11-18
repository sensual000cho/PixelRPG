#pragma once

#include "CoreMinimal.h"
#include "Combat/PixelEnemyCharacter.h"
#include "PixelMushroomEnemy.generated.h"

/**
 * The toadstool: charges like a boar. It paws the ground, then runs at the target in a straight line (a "!" over
 * its head while it runs), picking up
 * speed the further it runs (so a long run-up hits much harder than a short one). If the target sidesteps and the
 * mushroom runs past, it slams on the brakes and skids to a stop, turns round and winds up the next charge.
 * Running into a wall stops it dead and dazes it for a moment. Once a charge has started, hits do not stop it.
 *
 * Models: SM_Mushroom_00..06, smooth vertex-coloured blobs like the slime's
 * (Desktop/Project_PixelRPG/make_mushroom.py). Carries the actor tag "Mushroom" for Kill objectives.
 */
UCLASS()
class PIXELRPG_API APixelMushroomEnemy : public APixelEnemyCharacter
{
	GENERATED_BODY()

public:
	APixelMushroomEnemy();

	virtual void Tick(float DeltaSeconds) override;
	/** The "!" over its head (drawn by the HUD): only while it is actually charging. */
	virtual bool IsWarning() const override { return Mode == EMode::Charge; }
	virtual float GetWarningHeight() const override { return 125.f; }   // over the cap (the model is 1 m tall)

	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	float WalkSpeed = 170.f;

	/** It starts winding up a charge once the target is this close (and keeps walking closer until then). */
	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	float ChargeRange = 750.f;

	/** Pawing the ground before the charge: the time the player has to read it. */
	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	float WindupSeconds = 0.9f;

	/** Wind-up after a missed charge, when it is already facing the target and angry. */
	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	float RewindSeconds = 0.6f;

	/** Speed when it sets off, and how much speed it gains per unit of distance run (1/s). */
	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	float ChargeStartSpeed = 200.f;

	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	float ChargeSpeedPerDistance = 1.5f;

	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	float ChargeMaxSpeed = 1150.f;

	/** It gives up on a charge that has run this far without hitting anything. */
	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	float ChargeMaxDistance = 1600.f;

	/** Damage and knockback at the starting speed and at full speed; in between they scale with the speed. */
	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	FVector2D ChargeDamage = FVector2D(6.f, 18.f);

	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	FVector2D ChargeKnockback = FVector2D(300.f, 850.f);

	/** How hard it brakes after missing, in units/s^2: a short skid, not an instant stop. */
	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	float BrakeDeceleration = 2600.f;

	/** Time to turn round after the skid, before winding up again. */
	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	float TurnSeconds = 0.35f;

	/** Dazed after running into a wall or tree. */
	UPROPERTY(EditAnywhere, Category = "AI|Charge")
	float BonkSeconds = 1.2f;

protected:
	virtual int32 GetPoseIndex() const override;

private:
	enum class EMode : uint8 { Idle, Chase, Windup, Charge, Brake, Turn };

	void SetMode(EMode NewMode);
	/** Holds the body at Speed along ChargeDirection this frame (charge and skid). */
	void DriveAlongCharge(float Speed);
	void StartCharge(const FVector& Direction);
	EMode Mode = EMode::Idle;
	float ModeTime = 0.f;
	/** How long the current Windup or Turn lasts. */
	float ModeDuration = 0.f;

	float CurrentSpeed = 0.f;
	float ChargeDistance = 0.f;
	float StuckTime = 0.f;
	FVector LastLocation = FVector::ZeroVector;
};
