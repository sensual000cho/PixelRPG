#include "Combat/PixelEnemyCharacter.h"

#include "AbilitySystemComponent.h"
#include "Combat/PixelGameplayEffects.h"
#include "Combat/PixelGameplayTags.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "PixelAudio.h"
#include "PixelGameMode.h"

APixelEnemyCharacter::APixelEnemyCharacter()
{
	TeamId = 1;
	StartingHealth = 30.f;
	StunOnHitSeconds = 0.35f;
	HandHeight = 30.f;
	GetCharacterMovement()->MaxWalkSpeed = 230.f;

	// The "weapon" is its body (see TryTackleHit): only the damage and the knockback are used.
	Weapon.Name = TEXT("Tackle");
	Weapon.Damage = 8.f;
	Weapon.Knockback = 420.f;
}

void APixelEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	LoadPoses(*PosePrefix, PoseCount);
}

int32 APixelEnemyCharacter::GetPoseIndex() const
{
	// See SLIME_POSES in Tools/make_assets_blender.py (the mushroom follows the same order):
	// 0-1 idle, 2-3 moving, 4 about to attack, 5 flying at the target, 6 hurt.
	if (IsDead() || IsHurt())
	{
		return 6;
	}
	if (State == EState::Telegraph)
	{
		return 4;
	}
	if (State == EState::Charge)
	{
		return 5;
	}
	return GetWalkTime() > 0.f ? 2 + static_cast<int32>(GetWalkTime() * 6.f) % 2 : static_cast<int32>(GetAnimTime() * 2.f) % 2;
}

const APixelCharacterBase* APixelEnemyCharacter::FindTarget(float MaxDistance) const
{
	// Closest hostile that can be attacked: the player, or the companion while it is on the ground.
	const APixelCharacterBase* Target = nullptr;
	float BestDistSq = FMath::Square(MaxDistance);
	for (TActorIterator<APixelCharacterBase> It(GetWorld()); It; ++It)
	{
		if (It->TeamId != TeamId && It->IsTargetable())
		{
			const float DistSq = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Target = *It;
			}
		}
	}

	// Only a few enemies fight at once; the others wait their turn.
	if (!Target || IsDead())
	{
		bEngaged = false;
		return nullptr;
	}
	if (!bEngaged)
	{
		if (BestDistSq >= FMath::Square(AggroRange) && !IsHurt())
		{
			return Target; // not close enough to start anything yet
		}
		const APixelGameMode* GameMode = GetWorld()->GetAuthGameMode<APixelGameMode>();
		const int32 MaxEngaged = GameMode ? GameMode->MaxEngagedEnemies : 5;
		int32 Engaged = 0;
		for (TActorIterator<APixelEnemyCharacter> It(GetWorld()); It; ++It)
		{
			Engaged += (*It != this && It->bEngaged && !It->IsDead()) ? 1 : 0;
		}
		if (Engaged >= MaxEngaged && !IsHurt())
		{
			return nullptr;
		}
		bEngaged = true;
	}
	return Target;
}

bool APixelEnemyCharacter::TryTackleHit()
{
	for (TActorIterator<APixelCharacterBase> It(GetWorld()); It; ++It)
	{
		APixelCharacterBase* Other = *It;
		// Someone in the middle of a dodge is passed through, not bounced off.
		if (Other->TeamId == TeamId || !Other->IsTargetable() || Other->HasStateTag(PixelTags::State_Invulnerable))
		{
			continue;
		}
		const FVector Offset = Other->GetActorLocation() - GetActorLocation();
		if (Offset.SizeSquared2D() > FMath::Square(TackleRadius) || FMath::Abs(Offset.Z) > 150.f)
		{
			continue;
		}

		const FGameplayEffectSpecHandle Spec = AbilitySystem->MakeOutgoingSpec(UPixelGE_Damage::StaticClass(), 1.f, AbilitySystem->MakeEffectContext());
		if (Spec.IsValid())
		{
			Spec.Data->SetSetByCallerMagnitude(PixelTags::Data_Damage, Weapon.Damage * GetAttackPower());
			AbilitySystem->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), Other->GetAbilitySystemComponent());
		}
		Other->ApplyKnockback(ChargeDirection, Weapon.Knockback);
		return true;
	}
	return false;
}

void APixelEnemyCharacter::HandleDeath(AActor* Killer)
{
	Super::HandleDeath(Killer);
	SetLifeSpan(0.5f);
	UPixelAudioSubsystem::PlayAt(this, EPixelSound::SlimeDeath, GetActorLocation());
	if (APixelGameMode* GameMode = GetWorld()->GetAuthGameMode<APixelGameMode>())
	{
		GameMode->NotifyEnemyKilled(this);
	}
}

void APixelEnemyCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ExtraSquash = 0.f;
	if (IsDead())
	{
		return;
	}

	const APixelCharacterBase* Target = FindTarget(AggroRange * 1.5f);
	if (!Target || HasStateTag(PixelTags::State_Stunned))
	{
		State = EState::Idle;
		StateTime = 0.f;
		return;
	}

	const FVector ToTarget = (Target->GetActorLocation() - GetActorLocation()) * FVector(1.f, 1.f, 0.f);
	const float Distance = ToTarget.Size();
	const FVector Direction = ToTarget.GetSafeNormal();
	StateTime += DeltaSeconds;

	switch (State)
	{
	case EState::Idle:
		if (Distance < AggroRange)
		{
			State = EState::Chase;
			StateTime = 0.f;
		}
		break;

	case EState::Chase:
		FaceDirection(Direction);
		if (Distance <= AttackRange)
		{
			State = EState::Telegraph;
			StateTime = 0.f;
		}
		else
		{
			AddMovementInput(Direction);
		}
		break;

	case EState::Telegraph:
		// Crouch and blink so the player can read the attack and dodge.
		FaceDirection(Direction);
		ExtraSquash = FMath::Clamp(StateTime / TelegraphSeconds, 0.f, 1.f);
		FlashAmount = FMath::Max(FlashAmount, 0.35f * FMath::Abs(FMath::Sin(StateTime * 25.f)));
		if (StateTime >= TelegraphSeconds)
		{
			// Throw the whole body at where the target is now: a low leap, so it can be dodged sideways.
			FaceDirection(Direction, true);
			ChargeDirection = Direction;
			StartDash(ChargeDirection, ChargeSpeed, ChargeSeconds);
			LaunchCharacter(FVector(0.f, 0.f, ChargeHop), false, true);
			UPixelAudioSubsystem::PlayAt(this, EPixelSound::Swing, GetActorLocation());
			State = EState::Charge;
			StateTime = 0.f;
		}
		break;

	case EState::Charge:
		if (TryTackleHit())
		{
			StartDash(-ChargeDirection, 260.f, 0.18f); // bounce off
			State = EState::Recover;
			StateTime = 0.f;
		}
		else if (StateTime >= ChargeSeconds)
		{
			State = EState::Recover;
			StateTime = 0.f;
		}
		break;

	case EState::Recover:
		if (StateTime >= RecoverSeconds)
		{
			State = EState::Chase;
			StateTime = 0.f;
		}
		break;
	}
}
