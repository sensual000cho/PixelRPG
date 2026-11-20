#include "Combat/PixelPlayerCharacter.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Combat/PixelAbility_Melee.h"
#include "Combat/PixelAttributeSet.h"
#include "Combat/PixelCompanionCharacter.h"
#include "Combat/PixelGameplayTags.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "PixelAudio.h"
#include "PixelCameraRig.h"
#include "PixelGameMode.h"
#include "TimerManager.h"
#include "World/PixelInteractable.h"
#include "World/PixelInventoryComponent.h"
#include "World/PixelQuestActors.h"
#include "World/PixelQuestComponent.h"

APixelPlayerCharacter::APixelPlayerCharacter()
{
	TeamId = 0;
	StartingHealth = BaseHealth;
	bWeaponInBody = true;
	bHopWhileWalking = false; // the run animation bounces the body
	HandHeight = 70.f;

	BodySkeleton = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodySkeleton"));
	BodySkeleton->SetupAttachment(VisualRoot);
	BodySkeleton->SetRelativeRotation(BodyMesh->GetRelativeRotation()); // same -90 yaw as the other character models
	BodySkeleton->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodySkeleton->SetRenderCustomDepth(true); // drawn in big pixels with an outline, like every character
	BodySkeleton->SetReceivesDecals(false);
	BodySkeleton->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	BodySkeleton->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	Inventory = CreateDefaultSubobject<UPixelInventoryComponent>(TEXT("Inventory"));
	Quest = CreateDefaultSubobject<UPixelQuestComponent>(TEXT("Quest"));

	FPixelWeaponDef Sword;
	Sword.Name = TEXT("Sword");
	Sword.Damage = 10.f;
	Sword.Range = 165.f;
	Sword.ArcDegrees = 140.f;
	Sword.Windup = 0.08f;
	Sword.ActiveTime = 0.10f;
	Sword.Recovery = 0.16f;
	Sword.Knockback = 420.f;
	Sword.SlashRoll = -62.f; // overhead chop
	Sword.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/SM_Sword.SM_Sword")));

	FPixelWeaponDef Spear;
	Spear.Name = TEXT("Spear");
	Spear.Damage = 16.f;
	Spear.Range = 260.f;
	Spear.ArcDegrees = 28.f;
	Spear.Windup = 0.22f;
	Spear.ActiveTime = 0.10f;
	Spear.Recovery = 0.34f;
	Spear.Knockback = 650.f;
	Spear.bThrust = true;
	Spear.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/SM_Spear.SM_Spear")));

	Weapons = { Sword, Spear };

	FPixelWeaponDef Slash = Sword;      // level, right to left: wide
	Slash.ArcDegrees = 175.f;
	Slash.SlashRoll = -14.f;
	FPixelWeaponDef Backhand = Slash;   // and back again
	Backhand.SlashRoll = 14.f;
	FPixelWeaponDef Rising = Sword;     // narrow, hits harder and throws further
	Rising.Damage = 13.f;
	Rising.ArcDegrees = 90.f;
	Rising.Windup = 0.10f;
	Rising.Knockback = 520.f;
	Rising.SlashRoll = 62.f;
	FPixelWeaponDef Stab = Sword;       // the finisher: long, thin, slow to recover
	Stab.Damage = 18.f;
	Stab.Range = 215.f;
	Stab.ArcDegrees = 36.f;
	Stab.Windup = 0.12f;
	Stab.Recovery = 0.26f;
	Stab.Knockback = 700.f;
	Stab.bThrust = true;
	Stab.SlashRoll = 0.f;
	SwordCombo = { Slash, Backhand, Rising, Stab };
	Weapon = Sword;
}

void APixelPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (USkeletalMesh* Hero = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Hero/SK_Hero.SK_Hero")))
	{
		BodySkeleton->SetSkeletalMeshAsset(Hero);
		BodySkeleton->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		BodyMaterial = BodySkeleton->CreateDynamicMaterialInstance(0); // the hit flash (APixelCharacterBase)
		BodyMesh->SetVisibility(false);
		for (const TCHAR* WeaponName : { TEXT("Sword"), TEXT("Spear") })
		{
			for (const TCHAR* Kind : { TEXT("Idle"), TEXT("Run"), TEXT("Attack"), TEXT("Dodge"), TEXT("Hurt") })
			{
				const FString Name = FString::Printf(TEXT("%s_%s"), Kind, WeaponName);
				BodyAnims.Add(Name, LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Characters/Hero/Anims/A_Hero_%s.A_Hero_%s"), *Name, *Name)));
			}
		}
		for (int32 Step = 1; Step <= SwordCombo.Num(); ++Step)
		{
			BodyAnims.Add(FString::Printf(TEXT("Combo%d"), Step),
				LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Characters/Hero/Anims/A_Hero_Combo%d.A_Hero_Combo%d"), Step, Step)));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PixelPlayer: SK_Hero not found, using the old flip-book poses. Run Tools/setup_pixel_assets.py."));
		BodySkeleton->SetVisibility(false);
		LoadPoses(TEXT("/Game/Characters/Hero/SM_Hero"), 22 + SwordCombo.Num() * 3);
	}
	GConfig->GetFloat(TEXT("PixelRPG.Controls"), TEXT("MouseSensitivity"), MouseSensitivity, GGameUserSettingsIni);
	AbilitySystem->GiveAbility(FGameplayAbilitySpec(UPixelAbility_Dodge::StaticClass(), 1));
	SpawnLocation = GetActorLocation();

	FActorSpawnParameters Params;
	Params.Owner = this;
	CameraRig = GetWorld()->SpawnActor<APixelCameraRig>(APixelCameraRig::StaticClass(), GetActorTransform(), Params);
	CameraRig->SetTarget(this);
	FaceDirection(FRotator(0.f, CameraRig->GetViewYaw(), 0.f).Vector(), true); // start facing away from the camera

	Companion = GetWorld()->SpawnActor<APixelCompanionCharacter>(APixelCompanionCharacter::StaticClass(), GetActorTransform(), Params);
	Companion->SetPartner(this);
	Companion->Mount();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// Capture the mouse for camera look.
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
}

int32 APixelPlayerCharacter::GetPoseIndex() const
{
	// Poses per weapon (see hero_rows in Tools/make_assets_blender.py):
	// 0-1 idle, 2-5 walk, 6-8 attack, 9 dodge, 10 hurt.
	// After both weapons come the other attacks of the sword combo, three poses each.
	const int32 Base = WeaponIndex * 11;
	if (IsDead() || IsHurt())
	{
		return Base + 10;
	}
	if (HasStateTag(PixelTags::State_Dodging))
	{
		return Base + 9;
	}
	const int32 Attack = GetAttackPhase();
	if (Attack >= 0)
	{
		return ComboStep > 0 ? Weapons.Num() * 11 + (ComboStep - 1) * 3 + Attack : Base + 6 + Attack;
	}
	return GetWalkTime() > 0.f ? Base + 2 + static_cast<int32>(GetWalkTime() * 9.f) % 4
		: Base + static_cast<int32>(GetAnimTime() * 1.6f) % 2;
}

UAnimSequence* APixelPlayerCharacter::FindAnim(const FString& Name) const
{
	const TObjectPtr<UAnimSequence>* Found = BodyAnims.Find(Name);
	return Found ? Found->Get() : nullptr;
}

void APixelPlayerCharacter::UpdateBodyAnimation()
{
	if (!BodySkeleton->GetSkeletalMeshAsset())
	{
		return;
	}
	// The base class squashes / shrinks the (hidden) flip-book mesh for dashes and death: follow it.
	BodySkeleton->SetRelativeLocation(BodyMesh->GetRelativeLocation());
	BodySkeleton->SetRelativeScale3D(BodyMesh->GetRelativeScale3D());

	// Every one-shot animation is shown over the time the game gives that action, as Alpha 0..1;
	// loops run on their own clock. The attack animations are keyed (at 24 fps) as: frame 0 guard,
	// 6 wound up, 10 strike done, 20 back in guard, and each part is stretched over its attack phase.
	const FString WeaponName = WeaponIndex == 1 ? TEXT("Spear") : TEXT("Sword");
	FString Name;
	float Time = 0.f;
	float Alpha = -1.f;
	if (IsDead() || IsHurt())
	{
		Name = TEXT("Hurt_") + WeaponName;
		Alpha = IsDead() ? 1.f : 1.f - GetHurtTimeLeft() / 0.28f;
	}
	else if (HasStateTag(PixelTags::State_Dodging))
	{
		Name = TEXT("Dodge_") + WeaponName;
		Alpha = (GetWorld()->GetTimeSeconds() - LastDodgeTime) / FMath::Max(GetDefault<UPixelAbility_Dodge>()->Duration, 0.05f);
	}
	else if (GetAttackVisualTime() >= 0.f)
	{
		Name = ComboStep > 0 ? FString::Printf(TEXT("Combo%d"), ComboStep) : TEXT("Attack_") + WeaponName;
		const FPixelWeaponDef& W = GetAttackVisualWeapon();
		const float T = GetAttackVisualTime();
		const float StrikeEnd = W.Windup + W.ActiveTime;
		const float Frame = T < W.Windup ? FMath::GetMappedRangeValueClamped(FVector2f(0.f, W.Windup), FVector2f(0.f, 6.f), T)
			: T < StrikeEnd ? FMath::GetMappedRangeValueClamped(FVector2f(W.Windup, StrikeEnd), FVector2f(6.f, 10.f), T)
			: FMath::GetMappedRangeValueClamped(FVector2f(StrikeEnd, W.TotalTime()), FVector2f(10.f, 20.f), T);
		Time = Frame / 24.f;
	}
	else if (GetWalkTime() > 0.f)
	{
		Name = TEXT("Run_") + WeaponName;
		Time = GetWalkTime();
	}
	else
	{
		Name = TEXT("Idle_") + WeaponName;
		Time = GetAnimTime();
	}

	UAnimSequence* Anim = FindAnim(Name);
	if (!Anim)
	{
		return;
	}
	const float Length = Anim->GetPlayLength();
	Time = Alpha >= 0.f ? FMath::Clamp(Alpha, 0.f, 1.f) * Length : (Name.StartsWith(TEXT("Idle")) || Name.StartsWith(TEXT("Run")) ? FMath::Fmod(Time, Length) : FMath::Min(Time, Length));
	if (AnimFrameRate > 0.f)
	{
		Time = FMath::Min(FMath::FloorToFloat(Time * AnimFrameRate + 0.001f) / AnimFrameRate, Length);
	}
	if (Anim != ShownAnim)
	{
		BodySkeleton->SetAnimation(Anim);
		BodySkeleton->SetPlayRate(0.f); // the frame is set below, every tick
		ShownAnim = Anim;
	}
	BodySkeleton->SetPosition(Time, false);
}

bool APixelPlayerCharacter::IsWeaponAvailable(int32 Index) const
{
	if (!Weapons.IsValidIndex(Index))
	{
		return false;
	}
	const APixelGameMode* GameMode = GetWorld()->GetAuthGameMode<APixelGameMode>();
	const FName Name(*Weapons[Index].Name);
	return !GameMode || !GameMode->QuestLockedWeapons.Contains(Name) || Quest->HasUnlockedWeapon(Name);
}

bool APixelPlayerCharacter::SwapWeapon()
{
	if (Weapons.Num() < 2 || HasStateTag(PixelTags::State_Attacking))
	{
		return false;
	}
	for (int32 Offset = 1; Offset < Weapons.Num(); ++Offset)
	{
		const int32 Next = (WeaponIndex + Offset) % Weapons.Num();
		if (IsWeaponAvailable(Next))
		{
			WeaponIndex = Next;
			SetWeapon(Weapons[WeaponIndex]);
			ComboStep = 0;
			ComboUntil = -100.f;
			UPixelAudioSubsystem::Play(this, EPixelSound::WeaponSwap);
			Quest->NotifyAction(PixelQuestActions::SwapWeapon);
			return true;
		}
	}
	return false;
}

void APixelPlayerCharacter::ExplainLockedWeapon()
{
	// Tab did nothing: say why, if a weapon is waiting on a quest (and not just because an attack is under way).
	if (HasStateTag(PixelTags::State_Attacking))
	{
		return;
	}
	for (int32 Index = 0; Index < Weapons.Num(); ++Index)
	{
		if (IsWeaponAvailable(Index))
		{
			continue;
		}
		const FName Name(*Weapons[Index].Name);
		for (const FPixelQuestDef& Def : PixelQuestCatalog::GetQuests())
		{
			if (Def.Reward.UnlockWeapon == Name)
			{
				const FPixelNpcDef* Npc = PixelQuestCatalog::FindNpc(Def.Giver);
				ShowNotice(FString::Printf(TEXT("%s: 아직 없음 - %s의 의뢰 \"%s\" 보상"), *PixelQuestCatalog::GetItemName(Name),
					Npc ? *Npc->DisplayName : *Def.Giver.ToString(), *Def.Title), 2.5f);
				return;
			}
		}
		ShowNotice(FString::Printf(TEXT("%s: 아직 쓸 수 없습니다"), *PixelQuestCatalog::GetItemName(Name)), 2.5f);
		return;
	}
}

void APixelPlayerCharacter::ToggleView()
{
	CameraRig->ToggleViewMode();
	Quest->NotifyAction(PixelQuestActions::ToggleView);
}

void APixelPlayerCharacter::ToggleCompanionMount()
{
	const bool bWasMounted = Companion->IsMounted();
	Companion->ToggleMount();
	if (bWasMounted && !Companion->IsMounted())
	{
		Quest->NotifyAction(PixelQuestActions::CompanionDeploy);
	}
}

void APixelPlayerCharacter::ToggleCompanionAutoCast()
{
	Companion->SetAutoCast(!Companion->IsAutoCast());
	if (!Companion->IsAutoCast())
	{
		Quest->NotifyAction(PixelQuestActions::ManualCast);
	}
}

void APixelPlayerCharacter::CastCompanionBolt()
{
	if (Companion->CastBolt(FindNearestEnemy(Companion->BoltRange)))
	{
		Quest->NotifyAction(PixelQuestActions::Bolt);
	}
}

void APixelPlayerCharacter::CastCompanionEmpower()
{
	if (Companion->CastEmpower())
	{
		Quest->NotifyAction(PixelQuestActions::Empower);
	}
}

void APixelPlayerCharacter::StartAttack()
{
	if (!Weapons.IsValidIndex(WeaponIndex))
	{
		TryAttack();
		return;
	}
	// Only the sword (the first weapon) has a combo. The melee ability reads the attack from Weapon.
	const float Now = GetWorld()->GetTimeSeconds();
	const int32 Steps = WeaponIndex == 0 ? 1 + SwordCombo.Num() : 1;
	const int32 Step = Now <= ComboUntil ? (ComboStep + 1) % Steps : 0;
	const FPixelWeaponDef Previous = Weapon;
	Weapon = Step > 0 ? SwordCombo[Step - 1] : Weapons[WeaponIndex];
	if (!TryAttack())
	{
		Weapon = Previous;
		return;
	}
	ComboStep = Step;
	ComboUntil = Now + Weapon.TotalTime() + ComboWindow;
	if (Weapon.bThrust && Step > 0)
	{
		StartDash(GetFacingDirection(), 480.f, Weapon.Windup + 0.04f); // step into the stab
	}
}

APixelCharacterBase* APixelPlayerCharacter::FindNearestEnemy(float MaxDistance) const
{
	APixelCharacterBase* Best = nullptr;
	float BestDistSq = FMath::Square(MaxDistance);
	for (TActorIterator<APixelCharacterBase> It(GetWorld()); It; ++It)
	{
		if (It->TeamId != TeamId && !It->IsDead())
		{
			const float DistSq = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = *It;
			}
		}
	}
	return Best;
}

APixelInteractable* APixelPlayerCharacter::FindFocus() const
{
	APixelInteractable* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<APixelInteractable> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
		if (DistSq < FMath::Square(It->InteractRange) && DistSq < BestDistSq && It->CanInteract(this))
		{
			BestDistSq = DistSq;
			Best = *It;
		}
	}
	return Best;
}

APixelInteractable* APixelPlayerCharacter::FindQuestTestGoal() const
{
	// Hand in, then work on objectives (markers, nodes), then pick up new quests.
	APixelInteractable* Best = nullptr;
	int32 BestRank = 0;
	float BestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<APixelInteractable> It(GetWorld()); It; ++It)
	{
		int32 Rank = 0;
		if (const APixelGatherable* Gatherable = Cast<APixelGatherable>(*It))
		{
			Rank = !Gatherable->IsDepleted() && Quest->WantsItem(Gatherable->GetItemId()) ? 2 : 0;
		}
		else if (const APixelQuestMarker* Marker = Cast<APixelQuestMarker>(*It))
		{
			Rank = Quest->WantsReach(Marker->MarkerId) ? 2 : 0;
		}
		else if (const APixelNpc* Npc = Cast<APixelNpc>(*It))
		{
			Rank = Quest->FindTurnInFor(Npc->NpcId) ? 3 : Quest->FindOfferFor(Npc->NpcId) ? 1 : 0;
		}
		const float DistSq = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
		if (Rank > 0 && (Rank > BestRank || (Rank == BestRank && DistSq < BestDistSq)))
		{
			Best = *It;
			BestRank = Rank;
			BestDistSq = DistSq;
		}
	}
	return Best;
}

APixelCharacterBase* APixelPlayerCharacter::FindAutoplayTarget() const
{
	// Bolts need something in range too: stay at the dummy while the companion practises on it.
	const bool bWantDummy = Quest->WantsAction(PixelQuestActions::HitDummy) || Quest->WantsAction(PixelQuestActions::ComboFinisher)
		|| Quest->WantsAction(PixelQuestActions::Bolt);
	APixelCharacterBase* Best = nullptr;
	float BestDistSq = FMath::Square(5000.f);
	for (TActorIterator<APixelCharacterBase> It(GetWorld()); It; ++It)
	{
		if (It->TeamId == TeamId || It->IsDead() || (It->IsA<APixelTrainingDummy>() && !bWantDummy))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = *It;
		}
	}
	return Best;
}

void APixelPlayerCharacter::AutoplayQuestActions()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextAutoplayAction)
	{
		return;
	}
	NextAutoplayAction = Now + 1.2f;
	if (Quest->WantsAction(PixelQuestActions::ToggleView))
	{
		ToggleView();
		ToggleView(); // and back, so the screenshots keep the usual view
	}
	if (Companion && !Companion->IsDowned())
	{
		if (Quest->WantsAction(PixelQuestActions::CompanionDeploy) && Companion->IsMounted())
		{
			ToggleCompanionMount();
		}
		else if (Quest->WantsAction(PixelQuestActions::ManualCast) && Companion->IsAutoCast())
		{
			ToggleCompanionAutoCast();
		}
		else if (Quest->WantsAction(PixelQuestActions::Bolt))
		{
			CastCompanionBolt();
		}
		else if (Quest->WantsAction(PixelQuestActions::Empower))
		{
			CastCompanionEmpower();
		}
	}
}

void APixelPlayerCharacter::ShowNotice(const FString& Text, float Seconds)
{
	NoticeText = Text;
	NoticeUntil = GetWorld()->GetTimeSeconds() + Seconds;
}

void APixelPlayerCharacter::ShowDialogue(const FString& Speaker, const FString& Text, float Seconds)
{
	DialogueSpeaker = Speaker;
	DialogueText = Text;
	DialogueUntil = GetWorld()->GetTimeSeconds() + Seconds;
	UE_LOG(LogTemp, Display, TEXT("PixelWorld: %s says: %s"), *Speaker, *Text.Replace(TEXT("\n"), TEXT(" ")));
}

bool APixelPlayerCharacter::GetNotice(FString& OutText) const
{
	OutText = NoticeText;
	return GetWorld()->GetTimeSeconds() < NoticeUntil;
}

bool APixelPlayerCharacter::GetDialogue(FString& OutSpeaker, FString& OutText) const
{
	OutSpeaker = DialogueSpeaker;
	OutText = DialogueText;
	return GetWorld()->GetTimeSeconds() < DialogueUntil;
}

void APixelPlayerCharacter::ApplyQuestBonus(bool bHeal)
{
	const float Health = GetHealth();
	StartingHealth = BaseHealth + Quest->GetBonusMaxHealth(); // also what a respawn restores
	Attributes->SetMaxHealth(StartingHealth);
	Attributes->SetHealth(bHeal ? StartingHealth : FMath::Min(Health, StartingHealth));
}

void APixelPlayerCharacter::RestoreFromSave(const FVector& Location, float Health, int32 SavedWeaponIndex)
{
	SetActorLocation(Location + FVector(0.f, 0.f, 10.f), false, nullptr, ETeleportType::TeleportPhysics);
	ApplyQuestBonus(true);
	Attributes->SetHealth(FMath::Clamp(Health, 1.f, StartingHealth));
	if (IsWeaponAvailable(SavedWeaponIndex))
	{
		WeaponIndex = SavedWeaponIndex;
		SetWeapon(Weapons[WeaponIndex]);
		ComboStep = 0;
	}
}

void APixelPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBodyAnimation();

	if (IsDead() || !CameraRig)
	{
		Focus = nullptr;
		return;
	}
	Focus = FindFocus();

	FVector2D MoveInput = FVector2D::ZeroVector;
	bool bAttack = false;
	bool bDodge = false;

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		MoveInput.X = (PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
		MoveInput.Y = (PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
		bAttack = PC->IsInputKeyDown(EKeys::LeftMouseButton) || PC->IsInputKeyDown(EKeys::J);
		bDodge = PC->WasInputKeyJustPressed(EKeys::SpaceBar);

		if ((PC->WasInputKeyJustPressed(EKeys::Tab) || PC->WasInputKeyJustPressed(EKeys::R)) && !SwapWeapon()) { ExplainLockedWeapon(); }
		float MouseX = 0.f, MouseY = 0.f;
		PC->GetInputMouseDelta(MouseX, MouseY);
		CameraRig->AddLookInput(MouseX * MouseSensitivity, MouseY * MouseSensitivity);
		if (PC->WasInputKeyJustPressed(EKeys::V)) { ToggleView(); }

		if (Companion)
		{
			if (PC->WasInputKeyJustPressed(EKeys::G)) { ToggleCompanionMount(); }
			if (PC->WasInputKeyJustPressed(EKeys::One)) { Companion->SetOrder(EPixelCompanionOrder::Follow); }
			if (PC->WasInputKeyJustPressed(EKeys::Two)) { Companion->SetOrder(EPixelCompanionOrder::Hold); }
			if (PC->WasInputKeyJustPressed(EKeys::Three)) { Companion->SetOrder(EPixelCompanionOrder::Focus); }
			if (PC->WasInputKeyJustPressed(EKeys::T)) { ToggleCompanionAutoCast(); }
			if (PC->WasInputKeyJustPressed(EKeys::F)) { CastCompanionBolt(); }
			if (PC->WasInputKeyJustPressed(EKeys::C)) { CastCompanionEmpower(); }
		}
		if (PC->WasInputKeyJustPressed(EKeys::E) && Focus) { Focus->Interact(this); }
		if (APixelGameMode* GameMode = GetWorld()->GetAuthGameMode<APixelGameMode>())
		{
			if (PC->WasInputKeyJustPressed(EKeys::F5) && GameMode->SaveGame(TEXT("manual")))
			{
				ShowNotice(TEXT("저장했습니다"));
				UPixelAudioSubsystem::Play(this, EPixelSound::Save);
			}
			if (PC->WasInputKeyJustPressed(EKeys::F9))
			{
				const bool bLoaded = GameMode->LoadGame();
				ShowNotice(bLoaded ? TEXT("불러왔습니다") : TEXT("불러올 저장이 없습니다"));
				if (bLoaded)
				{
					UPixelAudioSubsystem::Play(this, EPixelSound::Load);
				}
			}
		}
		if (PC->WasInputKeyJustPressed(EKeys::Left)) { CameraRig->RotateView(1); }
		if (PC->WasInputKeyJustPressed(EKeys::Right)) { CameraRig->RotateView(-1); }
		if (PC->WasInputKeyJustPressed(EKeys::F1)) { CameraRig->TogglePixelEffect(); }
		const int32 SensitivityStep = (PC->WasInputKeyJustPressed(EKeys::RightBracket) ? 1 : 0) - (PC->WasInputKeyJustPressed(EKeys::LeftBracket) ? 1 : 0);
		if (SensitivityStep != 0)
		{
			MouseSensitivity = FMath::Clamp(FMath::RoundToFloat((MouseSensitivity + 0.1f * SensitivityStep) * 10.f) / 10.f, 0.1f, 3.f);
			GConfig->SetFloat(TEXT("PixelRPG.Controls"), TEXT("MouseSensitivity"), MouseSensitivity, GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
			ShowNotice(FString::Printf(TEXT("마우스 감도 %.1f"), MouseSensitivity), 1.5f);
		}
		CameraRig->AddZoomInput((PC->IsInputKeyDown(EKeys::X) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::Z) ? 1.f : 0.f), DeltaSeconds);
	}

	FVector MoveDir = FRotator(0.f, CameraRig->GetViewYaw(), 0.f).RotateVector(FVector(MoveInput.X, MoveInput.Y, 0.f)).GetSafeNormal();

	// Automated check ("-PixelCombatTest"): walk to the nearest enemy, attack it, swap weapons now and then.
	// "-PixelQuestTest" does the same but also takes the quest, gathers for it and hands it in.
	static const bool bQuestTest = FParse::Param(FCommandLine::Get(), TEXT("PixelQuestTest"));
	static const bool bAutoPlay = bQuestTest || FParse::Param(FCommandLine::Get(), TEXT("PixelCombatTest"));
	if (bAutoPlay)
	{
		const APixelCharacterBase* Enemy = bQuestTest ? FindAutoplayTarget() : FindNearestEnemy(5000.f);
		APixelInteractable* Goal = bQuestTest ? FindQuestTestGoal() : nullptr;
		if (bQuestTest)
		{
			AutoplayQuestActions();
			bDodge = Quest->WantsAction(PixelQuestActions::Dodge) && GetWorld()->GetTimeSeconds() - LastDodgeTime > 1.f;
		}
		const bool bEnemyInTheWay = Enemy && FVector::Dist2D(Enemy->GetActorLocation(), GetActorLocation()) < 320.f;
		// The dummy is in the way on purpose while a quest wants it hit; anything else on the way gets fought first.
		const bool bTrainHere = Enemy && Enemy->IsA<APixelTrainingDummy>();
		if (Goal && !bEnemyInTheWay && !bTrainHere)
		{
			const FVector ToGoal = (Goal->GetActorLocation() - GetActorLocation()) * FVector(1.f, 1.f, 0.f);
			// Turn the camera towards the goal, so the screenshot shows what is being used.
			const float YawError = FMath::FindDeltaAngleDegrees(CameraRig->GetViewYaw(), ToGoal.Rotation().Yaw);
			CameraRig->AddLookInput(FMath::Clamp(YawError, -240.f * DeltaSeconds, 240.f * DeltaSeconds), 0.f);
			if (ToGoal.Size() < Goal->InteractRange * 0.7f)
			{
				Goal->Interact(this);
				MoveDir = FVector::ZeroVector;
			}
			else
			{
				MoveDir = ToGoal.GetSafeNormal();
				// No path finding: when a house or rock stops the walk, slide along it for a second.
				const float Time = GetWorld()->GetTimeSeconds();
				if (Time >= NextStuckCheck)
				{
					const bool bStuck = FVector::Dist2D(GetActorLocation(), StuckCheckLocation) < 60.f;
					StuckCheckLocation = GetActorLocation();
					NextStuckCheck = Time + 0.8f;
					if (bStuck)
					{
						DetourSide = -DetourSide;
						DetourUntil = Time + 1.2f;
					}
				}
				if (Time < DetourUntil)
				{
					MoveDir = (MoveDir + FVector::CrossProduct(FVector::UpVector, MoveDir) * DetourSide * 1.5f).GetSafeNormal();
				}
			}
		}
		else if (Enemy)
		{
			const FVector ToEnemy = (Enemy->GetActorLocation() - GetActorLocation()) * FVector(1.f, 1.f, 0.f);
			bAttack = ToEnemy.Size() < Weapon.Range * 0.85f;
			MoveDir = bAttack ? FVector::ZeroVector : ToEnemy.GetSafeNormal();
		}
		// Exercise the companion: ride for 8 s, fight on the ground until 18 s, then call it back.
		const float Now = GetWorld()->GetTimeSeconds();
		if (!bQuestTest && Companion && !Companion->IsDowned() && Companion->IsMounted() == (Now > 8.f && Now < 18.f))
		{
			Companion->ToggleMount();
		}
		if (static_cast<int32>(GetWorld()->GetTimeSeconds() / 5.f) % Weapons.Num() != WeaponIndex)
		{
			SwapWeapon();
		}
	}

	const bool bAttacking = HasStateTag(PixelTags::State_Attacking);
	const bool bDodging = HasStateTag(PixelTags::State_Dodging);

	if (!MoveDir.IsNearlyZero() && !bDodging)
	{
		// Committed to the swing: slow down and keep the facing while attacking.
		AddMovementInput(MoveDir, bAttacking ? 0.25f : 1.f);
		if (!bAttacking)
		{
			FaceDirection(MoveDir);
		}
	}

	if (bDodge && !bDodging && GetWorld()->GetTimeSeconds() - LastDodgeTime >= DodgeCooldown)
	{
		if (!MoveDir.IsNearlyZero())
		{
			FaceDirection(MoveDir, true);
		}
		if (AbilitySystem->TryActivateAbilityByClass(UPixelAbility_Dodge::StaticClass()))
		{
			LastDodgeTime = GetWorld()->GetTimeSeconds();
			UPixelAudioSubsystem::Play(this, EPixelSound::Dodge);
			Quest->NotifyAction(PixelQuestActions::Dodge);
		}
	}

	if (bAttack && !bAttacking && !bDodging)
	{
		// Soft lock: turn to the closest enemy that is roughly in reach.
		if (const APixelCharacterBase* Enemy = FindNearestEnemy(Weapon.Range * 1.6f))
		{
			FaceDirection(Enemy->GetActorLocation() - GetActorLocation(), true);
		}
		StartAttack();
	}
}

void APixelPlayerCharacter::HandleDamaged(float Amount, AActor* DamageInstigator)
{
	Super::HandleDamaged(Amount, DamageInstigator);
	UE_LOG(LogTemp, Display, TEXT("PixelCombat: player took %.0f damage, health %.0f/%.0f"), Amount, GetHealth(), GetMaxHealth());
	UPixelAudioSubsystem::Play(this, EPixelSound::PlayerHurt);
	if (CameraRig)
	{
		CameraRig->Shake(3.f);
	}
}

void APixelPlayerCharacter::NotifyHitLanded(int32 TargetCount)
{
	if (WeaponIndex == 0 && ComboStep == SwordCombo.Num() && SwordCombo.Num() > 0)
	{
		Quest->NotifyAction(PixelQuestActions::ComboFinisher);
	}
	if (CameraRig)
	{
		CameraRig->Shake(2.f);
	}
}

void APixelPlayerCharacter::HandleDeath(AActor* Killer)
{
	Super::HandleDeath(Killer);
	UE_LOG(LogTemp, Display, TEXT("PixelCombat: player died"));
	UPixelAudioSubsystem::Play(this, EPixelSound::PlayerDeath);
	GetWorldTimerManager().SetTimer(RespawnTimer, this, &APixelPlayerCharacter::Respawn, RespawnSeconds, false);
}

void APixelPlayerCharacter::Respawn()
{
	// Undo APixelCharacterBase::HandleDeath and start over at the spawn point.
	AbilitySystem->SetLooseGameplayTagCount(PixelTags::State_Dead, 0);
	ResetHealth();
	SetActorLocation(SpawnLocation);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	ClearDeathVisual();
	ShowNotice(TEXT("마을에서 다시 일어났다"), 2.5f);
}
