#include "Combat/PixelRockEnemy.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "PixelAudio.h"

namespace
{
	/** Height of the centre of the curled-up model (SM_RockMonster_05) above its lowest point, in cm. */
	constexpr float CurledCentreHeight = 38.8f;

	/**
	 * The boulder is not round: how far the centre drops (cm) when it has rolled forward by 0, 10, ... 350
	 * degrees, so the lowest point stays on the ground. Measured on the model in RockMonster.blend.
	 */
	constexpr float RollDrop[36] = {
		0.0f, 0.5f, -0.2f, -2.1f, -3.1f, -3.4f, -4.7f, -7.0f, -9.4f, -10.8f, -10.7f, -11.6f,
		-11.2f, -9.1f, -7.3f, -5.2f, -2.6f, -1.1f, -0.7f, -1.5f, -2.5f, -3.0f, -4.6f, -7.3f,
		-6.5f, -6.6f, -7.7f, -9.7f, -10.2f, -8.7f, -8.1f, -8.4f, -9.6f, -8.3f, -4.4f, -1.7f};

	float DropAt(float Degrees)
	{
		const float Index = Degrees / 10.f;
		const int32 A = static_cast<int32>(FMath::Floor(Index)) % 36;
		return FMath::Lerp(RollDrop[A], RollDrop[(A + 1) % 36], Index - FMath::Floor(Index));
	}

	/** Same as MeshYawOffset in PixelCharacterBase.cpp: imported meshes look along +Y. */
	const FQuat MeshYaw(FRotator(0.f, -90.f, 0.f));

	/** Player walking speed if there is no player to ask (APixelCharacterBase's default). */
	constexpr float DefaultPlayerWalkSpeed = 420.f;
}

APixelRockEnemy::APixelRockEnemy()
{
	PosePrefix = TEXT("/Game/Characters/Enemies/RockMonster/SM_RockMonster");
	PoseCount = 7;
	Tags.Add(TEXT("Rock"));

	// About 1 m tall and 0.8 m across.
	GetCapsuleComponent()->SetCapsuleRadius(38.f);
	HandHeight = 40.f;

	// Tough: four times the toadstool's health (45), hits do not stun it and barely move it.
	StartingHealth = 120.f;
	StunOnHitSeconds = 0.f;
	KnockbackScale = 0.15f;

	AggroRange = 1500.f;
	TackleRadius = 85.f;
	Weapon.Name = TEXT("Body");
	GetCharacterMovement()->MaxWalkSpeed = DefaultPlayerWalkSpeed * WalkSpeedFactor;
}

void APixelRockEnemy::BeginPlay()
{
	Super::BeginPlay();
	GetCharacterMovement()->MaxWalkSpeed = GetWalkSpeed();
}

float APixelRockEnemy::GetWalkSpeed() const
{
	const ACharacter* Player = UGameplayStatics::GetPlayerCharacter(this, 0);
	const float PlayerSpeed = Player ? Player->GetCharacterMovement()->MaxWalkSpeed : DefaultPlayerWalkSpeed;
	return PlayerSpeed * WalkSpeedFactor;
}

bool APixelRockEnemy::IsWarning() const
{
	return Mode == EMode::Curl || Mode == EMode::Roll || Mode == EMode::SlamWindup;
}

bool APixelRockEnemy::IsCurled() const
{
	switch (Mode)
	{
	case EMode::Curl:   return ModeTime > ModeDuration * 0.5f;
	case EMode::Roll:
	case EMode::Brake:  return true;
	case EMode::Uncurl: return ModeTime < ModeDuration - UncurlSeconds * 0.5f;   // a bonk keeps it curled up, dazed
	default:            return false;
	}
}

int32 APixelRockEnemy::GetPoseIndex() const
{
	// 0-1 idle, 2-3 walking, 4 winding up, 5 curled up, 6 knocked over. It does not flinch when hit (only flashes).
	if (IsDead())
	{
		return 6;
	}
	if (IsCurled())
	{
		return 5;
	}
	if (Mode == EMode::Curl || Mode == EMode::Uncurl || Mode == EMode::SlamWindup)
	{
		return 4;
	}
	if (GetWalkTime() > 0.f)
	{
		return 2 + static_cast<int32>(GetWalkTime() * 5.f) % 2;   // heavy, slow steps
	}
	return static_cast<int32>(GetAnimTime() * 1.5f) % 2;
}

void APixelRockEnemy::SetMode(EMode NewMode, float Duration)
{
	if (Mode == EMode::Roll || Mode == EMode::Brake)
	{
		GetCharacterMovement()->MaxWalkSpeed = GetWalkSpeed();
		RollCooldownLeft = RollCooldown;
	}
	Mode = NewMode;
	ModeTime = 0.f;
	ModeDuration = Duration;
}

void APixelRockEnemy::DriveAlongRoll(float Speed)
{
	// Steering input at a speed cap equal to the wanted speed: the movement component neither speeds it up
	// past it nor brakes it on its own (same trick as the toadstool's charge).
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = FMath::Max(Speed, 1.f);
	Movement->Velocity = FVector(ChargeDirection.X * Speed, ChargeDirection.Y * Speed, Movement->Velocity.Z);
	AddMovementInput(ChargeDirection);
}

void APixelRockEnemy::Tick(float DeltaSeconds)
{
	// Its own behaviour replaces the slime's tackle, so skip APixelEnemyCharacter::Tick.
	APixelCharacterBase::Tick(DeltaSeconds);
	ExtraSquash = 0.f;
	RollCooldownLeft = FMath::Max(0.f, RollCooldownLeft - DeltaSeconds);

	if (IsDead())
	{
		UpdateBodyTurn(DeltaSeconds);
		return;
	}

	const APixelCharacterBase* Target = FindTarget(AggroRange * 1.5f);
	const bool bCommitted = Mode == EMode::Roll || Mode == EMode::Brake || Mode == EMode::Slam;
	if (!Target && !bCommitted)
	{
		if (Mode != EMode::Idle)
		{
			SetMode(EMode::Idle);
			GetCharacterMovement()->MaxWalkSpeed = GetWalkSpeed();
		}
		UpdateBodyTurn(DeltaSeconds);
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
			SetMode(EMode::Walk);
		}
		break;

	case EMode::Walk:
		GetCharacterMovement()->MaxWalkSpeed = GetWalkSpeed();
		FaceDirection(Direction);
		if (Distance > RollRange && RollCooldownLeft <= 0.f)
		{
			SetMode(EMode::Curl, CurlSeconds);   // too far to walk: curl up and roll at it
		}
		else if (Distance <= SlamRange)
		{
			SetMode(EMode::SlamWindup, SlamWindupSeconds);
		}
		else
		{
			AddMovementInput(Direction);
		}
		break;

	case EMode::Curl:
	{
		// Crouches, then tucks everything in; blinks so the player can read it.
		FaceDirection(Direction);
		const float Alpha = FMath::Clamp(ModeTime / ModeDuration, 0.f, 1.f);
		ExtraSquash = Alpha < 0.5f ? 0.5f * Alpha : 0.f;
		FlashAmount = FMath::Max(FlashAmount, 0.3f * FMath::Abs(FMath::Sin(ModeTime * 20.f)));
		if (ModeTime >= ModeDuration)
		{
			ChargeDirection = Direction;
			FaceDirection(Direction, true);
			CurrentSpeed = RollStartSpeed;
			RollDistance = 0.f;
			StuckTime = 0.f;
			LastLocation = Here;
			UE_LOG(LogTemp, Display, TEXT("PixelCombat: rock rolls at the target from %.0f away"), Distance);
			UPixelAudioSubsystem::PlayAt(this, EPixelSound::Swing, Here);
			SetMode(EMode::Roll);
		}
		break;
	}

	case EMode::Roll:
	{
		// Speed grows with the distance actually rolled: a long run-up makes it fast and dangerous.
		const float Moved = FVector::Dist2D(Here, LastLocation);
		LastLocation = Here;
		RollDistance += Moved;
		CurrentSpeed = FMath::Min(RollMaxSpeed, RollStartSpeed + RollSpeedPerDistance * RollDistance);
		DriveAlongRoll(CurrentSpeed);
		FaceDirection(ChargeDirection, true);

		const float Power = FMath::GetRangePct(RollStartSpeed, RollMaxSpeed, CurrentSpeed);
		Weapon.Damage = FMath::Lerp(RollDamage.X, RollDamage.Y, Power);
		Weapon.Knockback = FMath::Lerp(RollKnockback.X, RollKnockback.Y, Power);
		if (TryTackleHit())
		{
			UE_LOG(LogTemp, Display, TEXT("PixelCombat: rock roll hit after %.0f at speed %.0f (damage %.1f)"),
				RollDistance, CurrentSpeed, Weapon.Damage);
			GetCharacterMovement()->Velocity = FVector::ZeroVector;
			StartDash(-ChargeDirection, 180.f, 0.15f);   // bounce off
			SetMode(EMode::Uncurl, UncurlSeconds);
			break;
		}

		// Rolled into a wall or a tree: it barely moves although it is pushing hard.
		const bool bBlocked = ModeTime > 0.25f && Moved < CurrentSpeed * DeltaSeconds * 0.35f;
		StuckTime = bBlocked ? StuckTime + DeltaSeconds : 0.f;
		if (StuckTime > 0.12f)
		{
			UE_LOG(LogTemp, Display, TEXT("PixelCombat: rock rolled into something after %.0f"), RollDistance);
			FlashAmount = 0.7f;
			UPixelAudioSubsystem::PlayAt(this, EPixelSound::Hit, Here);
			GetCharacterMovement()->Velocity = FVector::ZeroVector;
			SetMode(EMode::Uncurl, BonkSeconds);
			break;
		}

		// Missed (the target stepped aside and it rolled past) or ran out of ground: skid to a stop.
		const bool bPassed = !Target || FVector::DotProduct(ToTarget, ChargeDirection) < -60.f;
		if (bPassed || RollDistance > RollMaxDistance)
		{
			UE_LOG(LogTemp, Display, TEXT("PixelCombat: rock missed, skidding from speed %.0f after %.0f"), CurrentSpeed, RollDistance);
			SetMode(EMode::Brake);
		}
		break;
	}

	case EMode::Brake:
		CurrentSpeed = FMath::Max(0.f, CurrentSpeed - BrakeDeceleration * DeltaSeconds);
		DriveAlongRoll(CurrentSpeed);
		if (CurrentSpeed > 350.f && TryTackleHit())
		{
			GetCharacterMovement()->Velocity = FVector::ZeroVector;
			SetMode(EMode::Uncurl, UncurlSeconds);
			break;
		}
		if (CurrentSpeed <= 0.f)
		{
			GetCharacterMovement()->Velocity = FVector::ZeroVector;
			SetMode(EMode::Uncurl, UncurlSeconds);
		}
		break;

	case EMode::Uncurl:
		if (ModeTime >= ModeDuration)
		{
			SetMode(EMode::Walk);
		}
		break;

	case EMode::SlamWindup:
	{
		// Slow and obvious: leans back, crouches deeper and deeper, blinks.
		FaceDirection(Direction);
		const float Alpha = FMath::Clamp(ModeTime / ModeDuration, 0.f, 1.f);
		ExtraSquash = 0.35f * Alpha + 0.06f * FMath::Abs(FMath::Sin(ModeTime * 14.f));
		FlashAmount = FMath::Max(FlashAmount, 0.3f * FMath::Abs(FMath::Sin(ModeTime * 22.f)));
		if (ModeTime >= ModeDuration)
		{
			// Throws its whole weight forwards at where the target is now.
			ChargeDirection = Target ? Direction : ChargeDirection;
			FaceDirection(ChargeDirection, true);
			Weapon.Damage = SlamDamage;
			Weapon.Knockback = SlamKnockback;
			bSlamHit = false;
			StartDash(ChargeDirection, SlamLungeSpeed, SlamLungeSeconds);
			LaunchCharacter(FVector(0.f, 0.f, 180.f), false, true);
			UPixelAudioSubsystem::PlayAt(this, EPixelSound::Swing, Here);
			SetMode(EMode::Slam, SlamLungeSeconds + 0.12f);
		}
		break;
	}

	case EMode::Slam:
		if (!bSlamHit && TryTackleHit())
		{
			bSlamHit = true;
			UE_LOG(LogTemp, Display, TEXT("PixelCombat: rock body slam hit (damage %.1f)"), Weapon.Damage);
		}
		if (ModeTime >= ModeDuration)
		{
			SetMode(EMode::SlamRecover, SlamRecoverSeconds);
		}
		break;

	case EMode::SlamRecover:
		if (ModeTime >= ModeDuration)
		{
			SetMode(EMode::Walk);
		}
		break;
	}

	UpdateBodyTurn(DeltaSeconds);
}

void APixelRockEnemy::UpdateBodyTurn(float DeltaSeconds)
{
	const FVector Scale = BodyMesh->GetRelativeScale3D();

	if (!IsDead() && IsCurled())
	{
		// Distance rolled along the way it faces turns into rotation (backwards when it is pushed back).
		const FVector Forward = GetVisualRoot()->GetForwardVector();
		const float Speed = FVector::DotProduct(GetVelocity() * FVector(1.f, 1.f, 0.f), Forward);
		RollDegrees = FMath::Fmod(RollDegrees + FMath::RadiansToDegrees(Speed * DeltaSeconds / RollRadius), 360.f);
		if (RollDegrees < 0.f)
		{
			RollDegrees += 360.f;
		}

		// Turn about the body's centre (VisualRoot space: X forward, Z up; a positive turn about Y brings the
		// top forwards) and drop it by the table so the lowest point keeps touching the ground.
		const FVector Centre(0.f, 0.f, CurledCentreHeight * Scale.Z);
		const FQuat Roll(FVector::YAxisVector, FMath::DegreesToRadians(RollDegrees));
		const FVector Drop(0.f, 0.f, DropAt(RollDegrees) * Scale.Z);
		BodyMesh->SetRelativeLocationAndRotation(Centre - Roll.RotateVector(Centre) + Drop, Roll * MeshYaw);
		return;
	}

	RollDegrees = 0.f;
	if (Mode == EMode::Slam && !IsDead())
	{
		// The slam: tips forwards over its front edge and back again.
		const float Alpha = FMath::Clamp(ModeTime / FMath::Max(ModeDuration, 0.01f), 0.f, 1.f);
		const FQuat Tip(FVector::YAxisVector, FMath::DegreesToRadians(32.f * FMath::Sin(Alpha * PI)));
		const FVector Pivot(30.f * Scale.X, 0.f, 0.f);   // the front of its feet
		BodyMesh->SetRelativeLocationAndRotation(BodyMesh->GetRelativeLocation() + Pivot - Tip.RotateVector(Pivot), Tip * MeshYaw);
		return;
	}
	BodyMesh->SetRelativeRotation(MeshYaw);   // upright, as APixelCharacterBase placed it
}
