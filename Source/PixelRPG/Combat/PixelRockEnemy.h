#pragma once

#include "CoreMinimal.h"
#include "Combat/PixelEnemyCharacter.h"
#include "PixelRockEnemy.generated.h"

/**
 * The rock monster: a mossy boulder with stubby arms and legs and a sprout on top. Tough: lots of health,
 * hits neither stun it nor push it far, and it does not flinch.
 *
 * Two ways of moving:
 *  - Far from the target (beyond RollRange) it curls up and rolls at it like a boulder: a charge in a straight
 *    line that picks up speed with the distance covered (0.6 of the toadstool's rate), hurts whoever it runs
 *    into, skids to a stop when it misses and stops dead against walls.
 *  - Closer than that it walks, at WalkSpeedFactor of the player's walking speed.
 * Next to the target it throws its body at it: a slow, clearly telegraphed slam with a long recovery.
 *
 * Models: SM_RockMonster_00..06 (Desktop/Project_PixelRPG/RockMonster/make_rock_monster.py, from the rigged RockMonster.blend):
 * 0-1 idle, 2-3 walking, 4 winding up, 5 curled up, 6 knocked over. The curled-up model is turned about its
 * centre here while it rolls, so the rolling is real rather than a flip-book.
 * Carries the actor tag "Rock" when spawned by Spawner_Rocks.
 */
UCLASS()
class PIXELRPG_API APixelRockEnemy : public APixelEnemyCharacter
{
	GENERATED_BODY()

public:
	APixelRockEnemy();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsWarning() const override;
	virtual float GetWarningHeight() const override { return 115.f; }

	// --- Walking ---

	/** Walking speed as a share of the player's walking speed. */
	UPROPERTY(EditAnywhere, Category = "AI|Walk")
	float WalkSpeedFactor = 0.8f;

	// --- Rolling charge ---

	/** Farther away than this it curls up and rolls at the target; closer, it walks. */
	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	float RollRange = 650.f;

	/** Curling up before it rolls off: the time the player has to read it. */
	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	float CurlSeconds = 0.7f;

	/** Speed when it rolls off, and speed gained per unit of distance rolled (toadstool: 1.5). */
	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	float RollStartSpeed = 200.f;

	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	float RollSpeedPerDistance = 0.9f;

	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	float RollMaxSpeed = 1000.f;

	/** It gives up on a roll that has gone this far without hitting anything. */
	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	float RollMaxDistance = 1800.f;

	/** Damage and knockback at the starting speed and at full speed. */
	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	FVector2D RollDamage = FVector2D(8.f, 22.f);

	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	FVector2D RollKnockback = FVector2D(400.f, 1000.f);

	/** Deceleration of the skid after a miss (units/s^2). */
	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	float BrakeDeceleration = 1800.f;

	/** Uncurling and standing up after a roll. */
	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	float UncurlSeconds = 0.45f;

	/** Dazed after rolling into a wall or tree. */
	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	float BonkSeconds = 1.5f;

	/** No new roll for this long after one ends: it walks meanwhile. */
	UPROPERTY(EditAnywhere, Category = "AI|Roll")
	float RollCooldown = 2.5f;

	// --- Body slam (close range) ---

	/** Centre-to-centre distance at which it stops walking and winds up the slam. */
	UPROPERTY(EditAnywhere, Category = "AI|Slam")
	float SlamRange = 150.f;

	/** Slow, readable wind-up. */
	UPROPERTY(EditAnywhere, Category = "AI|Slam")
	float SlamWindupSeconds = 0.9f;

	/** The lunge: speed and duration of the throw forwards. */
	UPROPERTY(EditAnywhere, Category = "AI|Slam")
	float SlamLungeSpeed = 480.f;

	UPROPERTY(EditAnywhere, Category = "AI|Slam")
	float SlamLungeSeconds = 0.28f;

	UPROPERTY(EditAnywhere, Category = "AI|Slam")
	float SlamDamage = 14.f;

	UPROPERTY(EditAnywhere, Category = "AI|Slam")
	float SlamKnockback = 650.f;

	/** Catching its breath after a slam: the opening for the player. */
	UPROPERTY(EditAnywhere, Category = "AI|Slam")
	float SlamRecoverSeconds = 1.5f;

	/** Distance from the centre of the curled-up body to the ground, on average: sets how fast it turns. */
	UPROPERTY(EditAnywhere, Category = "Visual")
	float RollRadius = 37.f;

protected:
	virtual int32 GetPoseIndex() const override;

private:
	enum class EMode : uint8 { Idle, Walk, Curl, Roll, Brake, Uncurl, SlamWindup, Slam, SlamRecover };

	void SetMode(EMode NewMode, float Duration = 0.f);
	/** Holds the body at Speed along ChargeDirection this frame (roll and skid). */
	void DriveAlongRoll(float Speed);
	/** Walking speed: a share of the player's. */
	float GetWalkSpeed() const;
	/** True while the curled-up model (pose 5) is shown. */
	bool IsCurled() const;
	/** Turns the curled-up model about its centre by the distance rolled (keeping it on the ground), or tips
	 *  the standing model forwards during the slam. */
	void UpdateBodyTurn(float DeltaSeconds);

	EMode Mode = EMode::Idle;
	float ModeTime = 0.f;
	float ModeDuration = 0.f;

	float CurrentSpeed = 0.f;
	float RollDistance = 0.f;
	float StuckTime = 0.f;
	float RollCooldownLeft = 0.f;
	FVector LastLocation = FVector::ZeroVector;
	bool bSlamHit = false;

	/** Forward roll in degrees, 0..360. */
	float RollDegrees = 0.f;
};
