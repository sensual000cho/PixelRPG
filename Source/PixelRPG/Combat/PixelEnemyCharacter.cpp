#include "Combat/PixelEnemyCharacter.h"

#include "AbilitySystemComponent.h"
#include "Combat/PixelGameplayEffects.h"
#include "Combat/PixelGameplayTags.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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

	for (int32 Index = 0; Index < 4; ++Index)
	{
		UStaticMeshComponent* Ring = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("TackleRing%d"), Index));
		Ring->SetupAttachment(RootComponent);
		Ring->SetUsingAbsoluteLocation(true);
		Ring->SetUsingAbsoluteRotation(true);
		Ring->SetUsingAbsoluteScale(true);
		Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ring->SetCastShadow(false);
		Ring->SetVisibility(false);
		TackleRings.Add(Ring);
	}
}

void APixelEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	LoadPoses(TEXT("SM_Slime"), 7);
	UStaticMesh* RingMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_Ring.SM_Ring"));
	UMaterialInterface* RingMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Pixel/M_TackleZone.M_TackleZone"));
	if (!RingMesh || !RingMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("PixelCombat: SM_Ring or M_TackleZone not found. Run Tools/setup_pixel_assets.py."));
	}
	for (UStaticMeshComponent* Ring : TackleRings)
	{
		Ring->SetStaticMesh(RingMesh);
		Ring->SetMaterial(0, RingMaterial);
	}
}

void APixelEnemyCharacter::AimTackleZone(const FVector& Direction, float Length)
{
	ZoneOrigin = GetFeetLocation();
	ZoneDirection = Direction;
	ZoneLength = FMath::Max(Length, 40.f);
}

void APixelEnemyCharacter::UpdateTackleZone(float DeltaSeconds)
{
	ZoneDrift += RingDriftSpeed * DeltaSeconds;
	// The mesh is 100 units across with its opening along Y: turn that to face down the line.
	const FRotator Rotation(0.f, ZoneDirection.Rotation().Yaw + 90.f, 0.f);
	const FVector Scale(RingRadius / 50.f);
	for (int32 Index = 0; Index < TackleRings.Num(); ++Index)
	{
		// Evenly spaced from the enemy to the target, each hanging above the ground below it. A hoop that
		// reaches the target starts again at the enemy.
		const float Along = FMath::Frac((Index + 0.5f) / TackleRings.Num() + ZoneDrift / ZoneLength);
		FVector Location = ZoneOrigin + ZoneDirection * (ZoneLength * Along);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Location + FVector(0.f, 0.f, 300.f), Location - FVector(0.f, 0.f, 300.f),
			FCollisionObjectQueryParams(ECC_WorldStatic)))
		{
			Location.Z = Hit.Location.Z;
		}
		Location.Z += RingHeight;
		UStaticMeshComponent* Ring = TackleRings[Index];
		Ring->SetWorldLocationAndRotation(Location, Rotation);
		Ring->SetWorldScale3D(Scale);
		Ring->SetVisibility(true);
	}
}

void APixelEnemyCharacter::HideTackleZone()
{
	ZoneDrift = 0.f;
	for (UStaticMeshComponent* Ring : TackleRings)
	{
		if (Ring->IsVisible())
		{
			Ring->SetVisibility(false);
		}
	}
}

int32 APixelEnemyCharacter::GetPoseIndex() const
{
	// See SLIME_POSES in Tools/make_assets_blender.py: 0-1 idle, 2-3 moving, 4 about to attack, 5 flying at the target, 6 hurt.
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
		HideTackleZone();
		return;
	}

	// Closest hostile that can be attacked: the player, or the companion while it is on the ground.
	const APixelCharacterBase* Target = nullptr;
	float BestDistSq = FMath::Square(AggroRange * 1.5f);
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
	if (!Target || HasStateTag(PixelTags::State_Stunned))
	{
		State = EState::Idle;
		StateTime = 0.f;
		HideTackleZone();
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
		AimTackleZone(Direction, Distance); // follows the target until the charge starts, then stays where it is
		UpdateTackleZone(DeltaSeconds);
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
		UpdateTackleZone(DeltaSeconds);
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
		HideTackleZone();
		if (StateTime >= RecoverSeconds)
		{
			State = EState::Chase;
			StateTime = 0.f;
		}
		break;
	}
}
