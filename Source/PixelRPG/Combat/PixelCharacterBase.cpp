#include "Combat/PixelCharacterBase.h"

#include "PixelAudio.h"
#include "PixelCameraRig.h"

#include "AbilitySystemComponent.h"
#include "Combat/PixelAbility_Melee.h"
#include "Combat/PixelAttributeSet.h"
#include "Combat/PixelGameplayTags.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

namespace
{
	const float MeshYawOffset = -90.f; // imported meshes look along +Y; rotate them to +X
	const float CapsuleHalfHeight = 85.f;
}

APixelCharacterBase::APixelCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(28.f, CapsuleHalfHeight);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore); // characters never push the camera in

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = false;
	Movement->bRunPhysicsWithNoController = true; // enemies are driven from Tick without a controller
	Movement->MaxWalkSpeed = 420.f;
	Movement->MaxAcceleration = 4096.f;
	Movement->BrakingDecelerationWalking = 4096.f;
	bUseControllerRotationYaw = false;

	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	Attributes = CreateDefaultSubobject<UPixelAttributeSet>(TEXT("Attributes"));

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(RootComponent);
	VisualRoot->SetRelativeLocation(FVector(0.f, 0.f, -CapsuleHalfHeight));
	VisualRoot->SetUsingAbsoluteRotation(true);

	auto MakeMesh = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(VisualRoot);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetRenderCustomDepth(true); // the post process outlines whatever writes custom depth
		Component->SetReceivesDecals(false);   // ground markings (the enemy's charge zone) stay off the characters
		return Component;
	};
	BodyMesh = MakeMesh(TEXT("BodyMesh"));
	BodyMesh->SetRelativeRotation(FRotator(0.f, MeshYawOffset, 0.f));
	WeaponMesh = MakeMesh(TEXT("WeaponMesh"));
	SlashMesh = MakeMesh(TEXT("SlashMesh"));
	SlashMesh->SetCastShadow(false);
	SlashMesh->SetRenderCustomDepth(false);
	SlashMesh->SetVisibility(false);

	// The mesh that ACharacter creates is not used by these placeholder visuals.
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void APixelCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	AbilitySystem->InitAbilityActorInfo(this, this);
	AbilitySystem->GiveAbility(FGameplayAbilitySpec(UPixelAbility_Melee::StaticClass(), 1));
	ResetHealth();

	if (UStaticMesh* Slash = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_SlashArc.SM_SlashArc")))
	{
		SlashMesh->SetStaticMesh(Slash);
	}
	SetWeapon(Weapon);

	FacingYaw = TargetFacingYaw = GetActorRotation().Yaw;
}

void APixelCharacterBase::SetBodyMesh(const TCHAR* AssetPath)
{
	if (UStaticMesh* Loaded = LoadObject<UStaticMesh>(nullptr, AssetPath))
	{
		BodyMesh->SetStaticMesh(Loaded);
		BodyMaterial = BodyMesh->CreateDynamicMaterialInstance(0);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PixelCharacter: mesh %s not found. Run Tools/setup_pixel_assets.py."), AssetPath);
	}
}

void APixelCharacterBase::LoadPoses(const TCHAR* Prefix, int32 Count)
{
	PoseMeshes.Reset();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FString Name = FString::Printf(TEXT("%s_%02d"), Prefix, Index);
		PoseMeshes.Add(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Meshes/%s.%s"), *Name, *Name)));
	}
	SetBodyMesh(*FString::Printf(TEXT("/Game/Meshes/%s_00.%s_00"), Prefix, Prefix));
	AnimTime = FMath::FRandRange(0.f, 10.f);
}

void APixelCharacterBase::ResetHealth()
{
	Attributes->InitMaxHealth(StartingHealth);
	Attributes->InitHealth(StartingHealth);
	Attributes->InitAttackPower(1.f);
}

void APixelCharacterBase::SetWeapon(const FPixelWeaponDef& NewWeapon)
{
	Weapon = NewWeapon;
	WeaponMesh->SetStaticMesh(Weapon.Mesh.IsNull() ? nullptr : Weapon.Mesh.LoadSynchronous());
}

bool APixelCharacterBase::IsDead() const
{
	return AbilitySystem->HasMatchingGameplayTag(PixelTags::State_Dead);
}

float APixelCharacterBase::GetHealth() const
{
	return Attributes->GetHealth();
}

float APixelCharacterBase::GetAttackPower() const
{
	// Current value: includes active buffs such as Empower.
	return Attributes->GetAttackPower();
}

float APixelCharacterBase::GetMaxHealth() const
{
	return Attributes->GetMaxHealth();
}

bool APixelCharacterBase::HasStateTag(const FGameplayTag& Tag) const
{
	return AbilitySystem->HasMatchingGameplayTag(Tag);
}

FVector APixelCharacterBase::GetFeetLocation() const
{
	return GetActorLocation() - FVector(0.f, 0.f, CapsuleHalfHeight);
}

void APixelCharacterBase::FaceDirection(const FVector& Direction, bool bInstant)
{
	if (Direction.SizeSquared2D() > KINDA_SMALL_NUMBER)
	{
		TargetFacingYaw = Direction.Rotation().Yaw;
		if (bInstant)
		{
			FacingYaw = TargetFacingYaw;
		}
	}
}

bool APixelCharacterBase::TryAttack()
{
	return AbilitySystem->TryActivateAbilityByClass(UPixelAbility_Melee::StaticClass());
}

void APixelCharacterBase::PlayAttackVisual(const FPixelWeaponDef& AttackWeapon)
{
	AttackVisualWeapon = AttackWeapon;
	AttackVisualTime = 0.f;
	UPixelAudioSubsystem::PlayAt(this, EPixelSound::Swing, GetActorLocation());
}

void APixelCharacterBase::StartDash(const FVector& Direction, float Speed, float Duration)
{
	DashDirection = Direction.GetSafeNormal2D();
	DashSpeed = Speed;
	DashTimeLeft = Duration;
}

void APixelCharacterBase::ApplyKnockback(const FVector& Direction, float Strength)
{
	if (Strength > 0.f && !IsDead())
	{
		DashTimeLeft = 0.f;
		LaunchCharacter(Direction.GetSafeNormal2D() * Strength + FVector(0.f, 0.f, 120.f), true, true);
	}
}

void APixelCharacterBase::HandleDamaged(float Amount, AActor* DamageInstigator)
{
	HurtTimeLeft = 0.28f;
	FlashAmount = 1.f;
	UPixelAudioSubsystem::PlayAt(this, EPixelSound::Hit, GetActorLocation());

	if (GetHealth() <= 0.f)
	{
		HandleDeath(DamageInstigator);
		return;
	}

	if (StunOnHitSeconds > 0.f)
	{
		AbilitySystem->CancelAllAbilities();
		AbilitySystem->SetLooseGameplayTagCount(PixelTags::State_Stunned, 1);
		GetWorldTimerManager().SetTimer(StunTimer, this, &APixelCharacterBase::EndStun, StunOnHitSeconds, false);
	}
}

void APixelCharacterBase::EndStun()
{
	AbilitySystem->SetLooseGameplayTagCount(PixelTags::State_Stunned, 0);
}

void APixelCharacterBase::HandleDeath(AActor* Killer)
{
	AbilitySystem->CancelAllAbilities();
	AbilitySystem->SetLooseGameplayTagCount(PixelTags::State_Dead, 1);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->DisableMovement();
	DashTimeLeft = 0.f;
	DeathTime = 0.f;
}

void APixelCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (DashTimeLeft > 0.f)
	{
		DashTimeLeft -= DeltaSeconds;
		UCharacterMovementComponent* Movement = GetCharacterMovement();
		Movement->Velocity = FVector(DashDirection.X * DashSpeed, DashDirection.Y * DashSpeed, Movement->Velocity.Z);
	}

	UpdateVisuals(DeltaSeconds);
}

int32 APixelCharacterBase::GetAttackPhase() const
{
	if (AttackVisualTime < 0.f)
	{
		return -1;
	}
	const FPixelWeaponDef& W = AttackVisualWeapon;
	return AttackVisualTime < W.Windup ? 0 : (AttackVisualTime < W.Windup + W.ActiveTime + 0.06f ? 1 : 2);
}

void APixelCharacterBase::UpdateVisuals(float DeltaSeconds)
{
	AnimTime += DeltaSeconds;
	HurtTimeLeft = FMath::Max(0.f, HurtTimeLeft - DeltaSeconds);

	FacingYaw = FMath::RInterpTo(FRotator(0.f, FacingYaw, 0.f), FRotator(0.f, TargetFacingYaw, 0.f), DeltaSeconds, 16.f).Yaw;
	float ShownYaw = FacingYaw;
	FVector ViewLocation;
	if (FacingSteps > 0 && APixelCameraRig::GetViewLocation(GetWorld(), ViewLocation))
	{
		// Facing measured against the line of sight, in steps. A little hysteresis keeps it from flickering
		// between two steps when it sits on the boundary.
		const float ViewYaw = (GetActorLocation() - ViewLocation).Rotation().Yaw;
		const float StepDegrees = 360.f / FacingSteps;
		const float Relative = FMath::FindDeltaAngleDegrees(ViewYaw, FacingYaw) / StepDegrees;
		float Off = Relative - ShownStep;
		Off -= FMath::RoundToFloat(Off / FacingSteps) * FacingSteps;
		if (FMath::Abs(Off) > 0.62f)
		{
			ShownStep = FMath::RoundToInt32(Relative);
		}
		ShownYaw = ViewYaw + ShownStep * StepDegrees;
	}
	VisualRoot->SetWorldRotation(FRotator(0.f, ShownYaw, 0.f));

	// --- Body: hop while walking, squash while dashing / telegraphing, shrink when dead ---
	const bool bWalking = GetVelocity().SizeSquared2D() > FMath::Square(30.f) && GetCharacterMovement()->IsMovingOnGround();
	WalkTime = bWalking ? WalkTime + DeltaSeconds : 0.f;
	const float Hop = bWalking ? FMath::Abs(FMath::Sin(WalkTime * 11.f)) : 0.f;
	const float Squash = FMath::Clamp(ExtraSquash + (DashTimeLeft > 0.f ? 0.35f : 0.f), 0.f, 1.f);

	float DeathScale = 1.f;
	if (DeathTime >= 0.f)
	{
		DeathTime += DeltaSeconds;
		DeathScale = FMath::Clamp(1.f - DeathTime / 0.35f, 0.f, 1.f);
	}

	const float Wide = (1.f - Hop * 0.05f + Squash * 0.3f) * DeathScale;
	const float Tall = (1.f + Hop * 0.08f - Squash * 0.4f) * DeathScale;
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, Hop * 4.f));
	BodyMesh->SetRelativeScale3D(FVector(Wide, Wide, Tall));

	// Flip-book animation: show the model of the current pose.
	const int32 Pose = GetPoseIndex();
	if (PoseMeshes.IsValidIndex(Pose) && PoseMeshes[Pose] && BodyMesh->GetStaticMesh() != PoseMeshes[Pose])
	{
		BodyMesh->SetStaticMesh(PoseMeshes[Pose]);
	}

	FlashAmount = FMath::Max(0.f, FlashAmount - DeltaSeconds * 6.f);
	if (BodyMaterial)
	{
		BodyMaterial->SetScalarParameterValue(TEXT("Flash"), FlashAmount);
	}

	// --- Weapon: X forward, Y right, Z up in VisualRoot space; the mesh blade points along its +Z ---
	bool bShowSlash = false;
	if (AttackVisualTime >= 0.f)
	{
		const FPixelWeaponDef& W = AttackVisualWeapon;
		AttackVisualTime += DeltaSeconds;
		const float T = AttackVisualTime;
		const float SwingEnd = W.Windup + W.ActiveTime;
		// 0 = wound up, 1 = fully swung.
		const float Swing = FMath::Clamp((T - W.Windup) / FMath::Max(W.ActiveTime, 0.01f), 0.f, 1.f);
		const float Back = FMath::Clamp(T / FMath::Max(W.Windup, 0.01f), 0.f, 1.f);
		const float Return = FMath::Clamp((T - SwingEnd) / FMath::Max(W.Recovery, 0.01f), 0.f, 1.f);

		if (W.bThrust)
		{
			const float Reach = FMath::Lerp(FMath::Lerp(0.f, -35.f, Back), W.Range - 110.f, Swing) * (1.f - Return * Return);
			WeaponMesh->SetRelativeLocationAndRotation(FVector(Reach, 24.f, HandHeight), FRotator(-90.f, 0.f, 0.f));
		}
		else
		{
			const float Angle = FMath::Lerp(FMath::Lerp(75.f, 100.f, Back), -85.f, Swing);
			const FVector Dir = FRotator(0.f, Angle, 0.f).Vector();
			WeaponMesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, HandHeight) + Dir * 26.f, FRotator(-90.f, Angle, 0.f));
		}

		bShowSlash = T >= W.Windup && T <= SwingEnd + 0.06f;
		if (bShowSlash)
		{
			const float Size = W.Range / 100.f;
			SlashMesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, HandHeight - 6.f), FRotator(0.f, 0.f, W.SlashRoll));
			SlashMesh->SetRelativeScale3D(FVector(Size, Size * (W.bThrust ? 0.22f : 1.f), 1.f));
		}

		if (T >= W.TotalTime())
		{
			AttackVisualTime = -1.f;
		}
	}
	else if (Weapon.bThrust)
	{
		WeaponMesh->SetRelativeLocationAndRotation(FVector(-5.f, 30.f, HandHeight - 60.f), FRotator(-12.f, 0.f, 0.f));
	}
	else
	{
		WeaponMesh->SetRelativeLocationAndRotation(FVector(8.f, 30.f, HandHeight - 12.f), FRotator(-30.f, 0.f, 0.f));
	}
	WeaponMesh->SetRelativeScale3D(FVector(DeathScale));
	WeaponMesh->SetVisibility(!bWeaponInBody);
	SlashMesh->SetVisibility(bShowSlash && !SlashMesh->GetStaticMesh() ? false : bShowSlash);
}
