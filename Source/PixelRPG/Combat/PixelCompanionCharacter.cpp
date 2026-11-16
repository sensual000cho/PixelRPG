#include "Combat/PixelCompanionCharacter.h"

#include "AbilitySystemComponent.h"
#include "Combat/PixelGameplayEffects.h"
#include "Combat/PixelGameplayTags.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PixelAudio.h"
#include "TimerManager.h"

namespace
{
	// Where the companion sits, relative to the partner's visual root (X forward, Z up).
	// Its own visuals hang 85 units (the capsule half height) below its origin, so this puts its feet on the backpack.
	// Rides small on the partner's back, below the head, so the partner stays readable from behind.
	const float MountScale = 0.6f;
	const FVector MountOffset(-24.f, 0.f, 40.f + 85.f * MountScale);
}

APixelCompanionCharacter::APixelCompanionCharacter()
{
	TeamId = 0;
	StartingHealth = 40.f;
	GetCharacterMovement()->MaxWalkSpeed = 460.f;
	Weapon.Mesh = nullptr; // no melee weapon
}

void APixelCompanionCharacter::BeginPlay()
{
	Super::BeginPlay();
	LoadPoses(TEXT("SM_Companion"), 7);
	AbilitySystem->GiveAbility(FGameplayAbilitySpec(UPixelAbility_Bolt::StaticClass(), 1));
	AbilitySystem->GiveAbility(FGameplayAbilitySpec(UPixelAbility_Empower::StaticClass(), 1));
}

int32 APixelCompanionCharacter::GetPoseIndex() const
{
	// See COMPANION_POSES in Tools/make_assets_blender.py: 0-1 idle, 2-3 moving, 4-5 casting, 6 hurt.
	if (IsDead() || IsHurt())
	{
		return 6;
	}
	const float SinceCast = GetWorld()->GetTimeSeconds() - FMath::Max(LastBoltTime, LastEmpowerTime);
	if (SinceCast < 0.36f)
	{
		return SinceCast < 0.12f ? 4 : 5;
	}
	return GetWalkTime() > 0.f ? 2 + static_cast<int32>(GetWalkTime() * 6.f) % 2 : static_cast<int32>(GetAnimTime() * 1.4f) % 2;
}

void APixelCompanionCharacter::Mount()
{
	if (!Partner || IsDead())
	{
		return;
	}
	bMounted = true;
	bReturning = false;
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->DisableMovement();
	AttachToComponent(Partner->GetVisualRoot(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(MountOffset);
	SetActorScale3D(FVector(MountScale));
	UE_LOG(LogTemp, Display, TEXT("PixelCombat: companion mounted"));
	if (GetWorld()->GetTimeSeconds() > 0.5f) // not for the mount that happens when the game starts
	{
		UPixelAudioSubsystem::Play(this, EPixelSound::CompanionMount);
	}
}

void APixelCompanionCharacter::Deploy()
{
	if (!Partner || !bMounted || IsDead())
	{
		return;
	}
	bMounted = false;
	bReturning = false;
	Order = EPixelCompanionOrder::Follow;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorScale3D(FVector::OneVector);

	// Step off to the partner's right.
	const FVector Right = FRotator(0.f, 90.f, 0.f).RotateVector(Partner->GetFacingDirection());
	SetActorLocation(Partner->GetActorLocation() + Right * 80.f, false, nullptr, ETeleportType::TeleportPhysics);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	UE_LOG(LogTemp, Display, TEXT("PixelCombat: companion deployed"));
	UPixelAudioSubsystem::Play(this, EPixelSound::CompanionDeploy);
}

void APixelCompanionCharacter::ToggleMount()
{
	if (IsDead())
	{
		return;
	}
	if (bMounted)
	{
		Deploy();
	}
	else
	{
		bReturning = true; // Tick walks it back and mounts when close
	}
}

void APixelCompanionCharacter::SetOrder(EPixelCompanionOrder NewOrder)
{
	Order = NewOrder;
	bReturning = false;
}

APixelCharacterBase* APixelCompanionCharacter::FindNearestEnemy(const FVector& From, float MaxDistance) const
{
	APixelCharacterBase* Best = nullptr;
	float BestDistSq = FMath::Square(MaxDistance);
	for (TActorIterator<APixelCharacterBase> It(GetWorld()); It; ++It)
	{
		if (It->TeamId != TeamId && It->IsTargetable())
		{
			const float DistSq = FVector::DistSquared2D(It->GetActorLocation(), From);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = *It;
			}
		}
	}
	return Best;
}

bool APixelCompanionCharacter::CastBolt(APixelCharacterBase* PreferredTarget)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (IsDead() || Now - LastBoltTime < BoltCooldown)
	{
		return false;
	}

	APixelCharacterBase* Target = PreferredTarget;
	if (!Target || !Target->IsTargetable() || FVector::Dist2D(Target->GetActorLocation(), GetActorLocation()) > BoltRange)
	{
		Target = FindNearestEnemy(GetActorLocation(), BoltRange);
	}
	if (!Target)
	{
		return false;
	}

	SpellTarget = Target;
	if (!AbilitySystem->TryActivateAbilityByClass(UPixelAbility_Bolt::StaticClass()))
	{
		return false;
	}
	LastBoltTime = Now;
	UPixelAudioSubsystem::PlayAt(this, EPixelSound::BoltCast, GetActorLocation());
	if (!bMounted)
	{
		FaceDirection(Target->GetActorLocation() - GetActorLocation(), true);
	}
	return true;
}

bool APixelCompanionCharacter::CastEmpower()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (IsDead() || !Partner || Partner->IsDead() || Now - LastEmpowerTime < EmpowerCooldown)
	{
		return false;
	}
	if (!AbilitySystem->TryActivateAbilityByClass(UPixelAbility_Empower::StaticClass()))
	{
		return false;
	}
	LastEmpowerTime = Now;
	UPixelAudioSubsystem::Play(this, EPixelSound::Empower);
	UE_LOG(LogTemp, Display, TEXT("PixelCombat: companion cast Empower (partner attack power %.2f)"), Partner->GetAttackPower());
	return true;
}

float APixelCompanionCharacter::GetBoltCooldownFraction() const
{
	return 1.f - FMath::Clamp((GetWorld()->GetTimeSeconds() - LastBoltTime) / BoltCooldown, 0.f, 1.f);
}

float APixelCompanionCharacter::GetEmpowerCooldownFraction() const
{
	return 1.f - FMath::Clamp((GetWorld()->GetTimeSeconds() - LastEmpowerTime) / EmpowerCooldown, 0.f, 1.f);
}

FString APixelCompanionCharacter::GetStateText() const
{
	if (IsDead()) { return TEXT("Downed"); }
	if (bMounted) { return TEXT("Mounted"); }
	if (bReturning) { return TEXT("Returning"); }
	switch (Order)
	{
	case EPixelCompanionOrder::Hold: return TEXT("Hold");
	case EPixelCompanionOrder::Focus: return TEXT("Focus");
	default: return TEXT("Follow");
	}
}

void APixelCompanionCharacter::HandleDeath(AActor* Killer)
{
	Super::HandleDeath(Killer);
	UE_LOG(LogTemp, Display, TEXT("PixelCombat: companion downed"));
	UPixelAudioSubsystem::Play(this, EPixelSound::CompanionDowned);
	GetWorldTimerManager().SetTimer(ReviveTimer, this, &APixelCompanionCharacter::Revive, DownedSeconds, false);
}

void APixelCompanionCharacter::Revive()
{
	AbilitySystem->SetLooseGameplayTagCount(PixelTags::State_Dead, 0);
	ResetHealth();
	ClearDeathVisual();
	Mount();
}

void APixelCompanionCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsDead() || !Partner)
	{
		return;
	}

	APixelCharacterBase* FocusTarget = nullptr;

	if (bMounted)
	{
		FaceDirection(Partner->GetFacingDirection(), true);
	}
	else
	{
		const FVector ToPartner = (Partner->GetActorLocation() - GetActorLocation()) * FVector(1.f, 1.f, 0.f);
		const float PartnerDistance = ToPartner.Size();

		if (bReturning)
		{
			if (PartnerDistance < 130.f)
			{
				Mount();
				return;
			}
			AddMovementInput(ToPartner.GetSafeNormal());
			FaceDirection(ToPartner);
		}
		else if (Order == EPixelCompanionOrder::Follow)
		{
			if (PartnerDistance > FollowDistance)
			{
				AddMovementInput(ToPartner.GetSafeNormal());
				FaceDirection(ToPartner);
			}
		}
		else if (Order == EPixelCompanionOrder::Focus)
		{
			// Attack what the partner is fighting; close in until it is comfortably in range.
			FocusTarget = FindNearestEnemy(Partner->GetActorLocation(), BoltRange * 1.5f);
			if (FocusTarget)
			{
				const FVector ToTarget = (FocusTarget->GetActorLocation() - GetActorLocation()) * FVector(1.f, 1.f, 0.f);
				if (ToTarget.Size() > BoltRange * 0.6f)
				{
					AddMovementInput(ToTarget.GetSafeNormal());
				}
				FaceDirection(ToTarget);
			}
		}
	}

	// Casting on its own: always for a Focus order, otherwise only in auto mode.
	if (FocusTarget)
	{
		CastBolt(FocusTarget);
	}
	if (bAutoCast && !bReturning)
	{
		// While riding, defend the partner; on the ground, shoot what is closest to itself.
		CastBolt(FindNearestEnemy(GetActorLocation(), BoltRange));
		if (FindNearestEnemy(Partner->GetActorLocation(), 600.f))
		{
			CastEmpower();
		}
	}
}

// ---------------------------------------------------------------------------------------------

UPixelAbility_Bolt::UPixelAbility_Bolt()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	ActivationBlockedTags.AddTag(PixelTags::State_Dead);
}

void UPixelAbility_Bolt::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	APixelCompanionCharacter* Companion = Cast<APixelCompanionCharacter>(GetAvatarActorFromActorInfo());
	APixelCharacterBase* Target = Companion ? Companion->GetSpellTarget() : nullptr;
	const bool bOk = Target && CommitAbility(Handle, ActorInfo, ActivationInfo);
	if (bOk)
	{
		const FVector Start = Companion->GetFeetLocation() + FVector(0.f, 0.f, 80.f);
		if (APixelProjectile* Bolt = GetWorld()->SpawnActor<APixelProjectile>(APixelProjectile::StaticClass(), Start, FRotator::ZeroRotator))
		{
			Bolt->Launch(Companion, Target, Companion->BoltDamage);
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !bOk);
}

UPixelAbility_Empower::UPixelAbility_Empower()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	ActivationBlockedTags.AddTag(PixelTags::State_Dead);
}

void UPixelAbility_Empower::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	APixelCompanionCharacter* Companion = Cast<APixelCompanionCharacter>(GetAvatarActorFromActorInfo());
	APixelCharacterBase* Partner = Companion ? Companion->GetPartner() : nullptr;
	const bool bOk = Partner && CommitAbility(Handle, ActorInfo, ActivationInfo);
	if (bOk)
	{
		const FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UPixelGE_Empower::StaticClass(), 1.f);
		if (Spec.IsValid())
		{
			GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), Partner->GetAbilitySystemComponent());
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !bOk);
}

// ---------------------------------------------------------------------------------------------

APixelProjectile::APixelProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	InitialLifeSpan = 2.f;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	RootComponent = Mesh;
}

void APixelProjectile::Launch(APixelCharacterBase* InSource, APixelCharacterBase* InTarget, float InDamage)
{
	Source = InSource;
	Target = InTarget;
	Damage = InDamage;
	Direction = (InTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	if (UStaticMesh* BoltMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_Bolt.SM_Bolt")))
	{
		Mesh->SetStaticMesh(BoltMesh);
	}
}

void APixelProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APixelCharacterBase* TargetCharacter = Target.Get();
	if (TargetCharacter && TargetCharacter->IsTargetable())
	{
		const FVector ToTarget = TargetCharacter->GetActorLocation() - GetActorLocation();
		if (ToTarget.Size() <= FMath::Max(45.f, Speed * DeltaSeconds))
		{
			APixelCharacterBase* SourceCharacter = Source.Get();
			UAbilitySystemComponent* SourceASC = SourceCharacter ? SourceCharacter->GetAbilitySystemComponent() : nullptr;
			if (SourceASC)
			{
				const FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(UPixelGE_Damage::StaticClass(), 1.f, SourceASC->MakeEffectContext());
				if (Spec.IsValid())
				{
					Spec.Data->SetSetByCallerMagnitude(PixelTags::Data_Damage, Damage);
					SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetCharacter->GetAbilitySystemComponent());
					UE_LOG(LogTemp, Display, TEXT("PixelCombat: bolt hit for %.0f, target health %.0f"), Damage, TargetCharacter->GetHealth());
				}
			}
			TargetCharacter->ApplyKnockback(Direction, 180.f);
			UPixelAudioSubsystem::PlayAt(this, EPixelSound::BoltHit, GetActorLocation());
			Destroy();
			return;
		}
		Direction = ToTarget.GetSafeNormal(); // homing
	}

	SetActorLocation(GetActorLocation() + Direction * Speed * DeltaSeconds);
	AddActorLocalRotation(FRotator(0.f, 720.f * DeltaSeconds, 0.f));
}
