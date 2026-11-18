#include "Combat/PixelMushroomEnemy.h"

#include "Combat/PixelGameplayTags.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PixelAudio.h"

APixelMushroomEnemy::APixelMushroomEnemy()
{
	PosePrefix = TEXT("/Game/Characters/Enemies/Mushroom/SM_Mushroom");
	PoseCount = 7;
	Tags = { TEXT("Mushroom") };   // not a slime: replaces the tag APixelEnemyCharacter gives

	// About 1 m tall and 0.9 m across: a little wider than the default capsule.
	GetCapsuleComponent()->SetCapsuleRadius(36.f);
	HandHeight = 45.f;

	StartingHealth = 45.f;           // slime: 30
	StunOnHitSeconds = 0.25f;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;

	Weapon.Name = TEXT("Charge");
	AggroRange = 1200.f;
	TackleRadius = 80.f;
}

void APixelMushroomEnemy::SetMode(EMode NewMode)
{
	if (Mode == EMode::Charge || Mode == EMode::Brake)
	{
		GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	}
	Mode = NewMode;
	ModeTime = 0.f;
}

void APixelMushroomEnemy::DriveAlongCharge(float Speed)
{
	// Steering input at a speed cap equal to the wanted speed: the movement component neither speeds it up
	// past it nor brakes it on its own, so the charge and the skid follow our numbers exactly.
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = FMath::Max(Speed, 1.f);
	Movement->Velocity = FVector(ChargeDirection.X * Speed, ChargeDirection.Y * Speed, Movement->Velocity.Z);
	AddMovementInput(ChargeDirection);
}

void APixelMushroomEnemy::StartCharge(const FVector& Direction)
{
	ChargeDirection = Direction;
	FaceDirection(Direction, true);
	CurrentSpeed = ChargeStartSpeed;
	ChargeDistance = 0.f;
	StuckTime = 0.f;
	LastLocation = GetActorLocation();
	UE_LOG(LogTemp, Display, TEXT("PixelCombat: mushroom charges"));
	UPixelAudioSubsystem::PlayAt(this, EPixelSound::Swing, GetActorLocation());
	SetMode(EMode::Charge);
}

int32 APixelMushroomEnemy::GetPoseIndex() const
{
	// Same seven poses as the slime: 0-1 idle, 2-3 moving, 4 about to attack, 5 charging, 6 hurt (also the skid).
	if (IsDead() || IsHurt() || Mode == EMode::Brake)
	{
		return 6;
	}
	if (Mode == EMode::Windup)
	{
		return 4;
	}
	if (Mode == EMode::Charge)
	{
		return 5;
	}
	return GetWalkTime() > 0.f ? 2 + static_cast<int32>(GetWalkTime() * 6.f) % 2 : static_cast<int32>(GetAnimTime() * 2.f) % 2;
}

void APixelMushroomEnemy::Tick(float DeltaSeconds)
{
	// Its own attack pattern replaces the slime's tackle, so skip APixelEnemyCharacter::Tick.
	APixelCharacterBase::Tick(DeltaSeconds);

	ExtraSquash = 0.f;
	if (IsDead())
	{
		return;
	}

	const APixelCharacterBase* Target = FindTarget(AggroRange * 1.5f);
	const bool bRunning = Mode == EMode::Charge || Mode == EMode::Brake;   // a charge cannot be called off
	if (!bRunning && (!Target || HasStateTag(PixelTags::State_Stunned)))
	{
		if (Mode != EMode::Idle && Mode != EMode::Chase)
		{
			SetMode(EMode::Chase);   // a hit during the wind-up spoils it: walk on and start over
		}
		if (!Target)
		{
			SetMode(EMode::Idle);
		}
		return;
	}

	const FVector Here = GetActorLocation();
	const FVector ToTarget = Target ? (Target->GetActorLocation() - Here) * FVector(1.f, 1.f, 0.f) : FVector::ZeroVector;
	const float Distance = ToTarget.Size();
	const FVector Direction = ToTarget.GetSafeNormal();
	ModeTime += DeltaSeconds;

	switch (Mode)
	{
	case EMode::Idle:
		if (Distance < AggroRange)
		{
			SetMode(EMode::Chase);
		}
		break;

	case EMode::Chase:
		FaceDirection(Direction);
		if (Distance <= ChargeRange)
		{
			ModeDuration = WindupSeconds;
			SetMode(EMode::Windup);
		}
		else
		{
			AddMovementInput(Direction);
		}
		break;

	case EMode::Windup:
	{
		// Pawing the ground: crouches deeper and deeper, bobbing, and blinks.
		FaceDirection(Direction);
		const float Alpha = FMath::Clamp(ModeTime / ModeDuration, 0.f, 1.f);
		ExtraSquash = 0.45f * Alpha + 0.12f * FMath::Abs(FMath::Sin(ModeTime * 16.f));
		FlashAmount = FMath::Max(FlashAmount, 0.3f * FMath::Abs(FMath::Sin(ModeTime * 22.f)));
		if (ModeTime >= ModeDuration)
		{
			StartCharge(Direction);
		}
		break;
	}

	case EMode::Charge:
	{
		// Speed grows with the distance actually covered: a long run-up makes it fast and dangerous.
		const float Moved = FVector::Dist2D(Here, LastLocation);
		LastLocation = Here;
		ChargeDistance += Moved;
		CurrentSpeed = FMath::Min(ChargeMaxSpeed, ChargeStartSpeed + ChargeSpeedPerDistance * ChargeDistance);
		DriveAlongCharge(CurrentSpeed);
		FaceDirection(ChargeDirection, true);

		// Automated check ("-PixelMushroomDodgeTest"): the target sidesteps every charge just before it lands,
		// so the miss / brake / wind-up-again path runs without a human player.
		static const bool bDodgeTest = FParse::Param(FCommandLine::Get(), TEXT("PixelMushroomDodgeTest"));
		if (bDodgeTest && Target && Distance < 260.f && FVector::DotProduct(ToTarget, ChargeDirection) > 0.f)
		{
			const_cast<APixelCharacterBase*>(Target)->StartDash(FVector(-ChargeDirection.Y, ChargeDirection.X, 0.f), 900.f, 0.3f);
		}

		const float Power = FMath::GetRangePct(ChargeStartSpeed, ChargeMaxSpeed, CurrentSpeed);
		Weapon.Damage = FMath::Lerp(ChargeDamage.X, ChargeDamage.Y, Power);
		Weapon.Knockback = FMath::Lerp(ChargeKnockback.X, ChargeKnockback.Y, Power);
		if (TryTackleHit())
		{
			UE_LOG(LogTemp, Display, TEXT("PixelCombat: mushroom charge hit after %.0f at speed %.0f (damage %.1f)"),
				ChargeDistance, CurrentSpeed, Weapon.Damage);
			StartDash(-ChargeDirection, 260.f, 0.18f);   // bounce off
			ModeDuration = 0.9f;
			SetMode(EMode::Turn);
			break;
		}

		// Ran into a wall or a tree: it barely moves although it is pushing hard.
		const bool bBlocked = ModeTime > 0.2f && Moved < CurrentSpeed * DeltaSeconds * 0.35f;
		StuckTime = bBlocked ? StuckTime + DeltaSeconds : 0.f;
		if (StuckTime > 0.12f)
		{
			UE_LOG(LogTemp, Display, TEXT("PixelCombat: mushroom ran into something after %.0f"), ChargeDistance);
			FlashAmount = 0.7f;
			UPixelAudioSubsystem::PlayAt(this, EPixelSound::Hit, Here);
			GetCharacterMovement()->Velocity = FVector::ZeroVector;
			ModeDuration = BonkSeconds;
			SetMode(EMode::Turn);
			break;
		}

		// Missed: the target sidestepped and it ran past, or it ran out of steam. Slam on the brakes.
		const bool bPassed = !Target || FVector::DotProduct(ToTarget, ChargeDirection) < -60.f;
		if (bPassed || ChargeDistance > ChargeMaxDistance)
		{
			UE_LOG(LogTemp, Display, TEXT("PixelCombat: mushroom missed, braking from speed %.0f after %.0f"), CurrentSpeed, ChargeDistance);
			SetMode(EMode::Brake);
		}
		break;
	}

	case EMode::Brake:
		// Skidding to a stop along the charge line, leaning back. Still hurts whoever it slides into.
		CurrentSpeed = FMath::Max(0.f, CurrentSpeed - BrakeDeceleration * DeltaSeconds);
		DriveAlongCharge(CurrentSpeed);
		ExtraSquash = 0.3f * FMath::Clamp(CurrentSpeed / 600.f, 0.f, 1.f);
		if (CurrentSpeed > 350.f && TryTackleHit())
		{
			StartDash(-ChargeDirection, 200.f, 0.15f);
			ModeDuration = 0.9f;
			SetMode(EMode::Turn);
			break;
		}
		if (CurrentSpeed <= 0.f)
		{
			UE_LOG(LogTemp, Display, TEXT("PixelCombat: mushroom stopped after skidding %.0f, winding up again"),
				FVector::Dist2D(Here, LastLocation));
			GetCharacterMovement()->Velocity = FVector::ZeroVector;
			ModeDuration = TurnSeconds;
			SetMode(EMode::Turn);
		}
		break;

	case EMode::Turn:
		// Turns round to the target; then, if it is still close, winds up the next charge straight away.
		if (Target)
		{
			FaceDirection(Direction);
		}
		if (ModeTime >= ModeDuration)
		{
			if (Target && Distance <= ChargeRange * 1.4f)
			{
				ModeDuration = RewindSeconds;
				SetMode(EMode::Windup);
			}
			else
			{
				SetMode(EMode::Chase);
			}
		}
		break;
	}
}
