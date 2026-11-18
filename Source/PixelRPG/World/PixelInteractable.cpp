#include "World/PixelInteractable.h"

#include "Combat/PixelPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LandscapeProxy.h"
#include "PixelAudio.h"
#include "PixelGameMode.h"
#include "TimerManager.h"
#include "World/PixelInventoryComponent.h"
#include "World/PixelQuestComponent.h"
#include "World/PixelQuestTypes.h"

APixelInteractable::APixelInteractable()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRenderCustomDepth(true); // marks it for the post process, which redraws characters in big pixels
	Mesh->SetReceivesDecals(false);   // ground markings (the charge rings of an enemy) are not painted onto it
}

void APixelInteractable::SetMesh(const TCHAR* AssetPath)
{
	if (UStaticMesh* Loaded = LoadObject<UStaticMesh>(nullptr, AssetPath))
	{
		Mesh->SetStaticMesh(Loaded);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PixelWorld: mesh %s not found (run Tools/setup_pixel_assets.py)"), AssetPath);
	}
}

// --- Gatherable ---

APixelGatherable::APixelGatherable()
{
	Mesh->SetRenderCustomDepth(false); // part of the world, not a character: stays full resolution
}

void APixelGatherable::BeginPlay()
{
	Super::BeginPlay();
	Mesh->SetRenderCustomDepth(false);
}

void APixelGatherable::Configure(FName InNodeId, FName InItemId, const TCHAR* MeshPath, float Scale)
{
	NodeId = InNodeId;
	ItemId = InItemId;
	SetMesh(MeshPath);
	Mesh->SetRenderCustomDepth(false);
	SetActorScale3D(FVector(Scale));
}

FString APixelGatherable::GetPrompt(const APixelPlayerCharacter* Player) const
{
	return FString::Printf(TEXT("%s 채집"), *PixelQuestCatalog::GetItemName(ItemId));
}

void APixelGatherable::Interact(APixelPlayerCharacter* Player)
{
	if (bDepleted || !Player)
	{
		return;
	}
	Player->GetInventory()->Add(ItemId, Amount);
	const bool bHerb = ItemId == TEXT("Moonleaf");
	UPixelAudioSubsystem::Play(this, bHerb ? EPixelSound::GatherHerb : EPixelSound::GatherOre);
	UPixelAudioSubsystem::Play(this, EPixelSound::ItemObtained);
	Player->ShowNotice(FString::Printf(TEXT("%s +%d"), *PixelQuestCatalog::GetItemName(ItemId), Amount));
	Player->GetQuest()->NotifyItemsChanged(); // may replace the notice with "Done: ..." or "return to ..."
	UE_LOG(LogTemp, Display, TEXT("PixelWorld: gathered %s from %s (now %d)"), *ItemId.ToString(), *NodeId.ToString(),
		Player->GetInventory()->GetCount(ItemId));

	SetDepleted(true);
	if (APixelGameMode* GameMode = GetWorld()->GetAuthGameMode<APixelGameMode>())
	{
		GameMode->SaveGame(TEXT("gathered"));
	}
}

void APixelGatherable::SetDepleted(bool bNewDepleted)
{
	bDepleted = bNewDepleted;
	SetActorHiddenInGame(bDepleted);
	GetWorldTimerManager().ClearTimer(RespawnTimer);
	if (bDepleted)
	{
		GetWorldTimerManager().SetTimer(RespawnTimer, this, &APixelGatherable::Regrow, RespawnSeconds, false);
	}
}

void APixelGatherable::Regrow()
{
	SetDepleted(false);
}

// --- NPC ---

APixelNpc::APixelNpc()
{
	PrimaryActorTick.bCanEverTick = true;
	MarkerHeight = 200.f;
	InteractRange = 210.f;

	// The actor origin is at the feet; the body blocks characters but not the camera.
	Body = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->InitCapsuleSize(30.f, 85.f);
	Body->SetRelativeLocation(FVector(0.f, 0.f, 85.f));
	Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	Mesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); // the mesh faces +Y, the actor faces +X
}

void APixelNpc::BeginPlay()
{
	Super::BeginPlay();
	SetMesh(TEXT("/Game/Characters/Villager/SM_Villager_00.SM_Villager_00"));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		PoseMeshes.Add(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Characters/Villager/SM_Villager_%02d.SM_Villager_%02d"), Index, Index)));
	}

	Home = GetActorLocation();
	HomeYaw = GetActorRotation().Yaw;
	StandYaw = HomeYaw;
	// Not everyone sets off at the same moment.
	StateUntil = GetWorld()->GetTimeSeconds() + FMath::FRandRange(1.f, PauseTime.Y);
}

void APixelNpc::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Now = GetWorld()->GetTimeSeconds();
	// Poses 0-1 idle (stepped faster while walking), 2-3 talking.
	const float IdleRate = State == EIdleState::Walking ? 3.f : 1.2f;
	const int32 Pose = Now < TalkUntil ? 2 + static_cast<int32>(Now * 4.f) % 2 : static_cast<int32>(Now * IdleRate) % 2;
	if (PoseMeshes.IsValidIndex(Pose) && PoseMeshes[Pose] && Mesh->GetStaticMesh() != PoseMeshes[Pose])
	{
		Mesh->SetStaticMesh(PoseMeshes[Pose]);
	}

	if (State == EIdleState::Talking)
	{
		TickTalking(DeltaSeconds, Now, Listener.Get());
	}
	else
	{
		TickIdle(DeltaSeconds, Now);
	}
}

void APixelNpc::TickTalking(float DeltaSeconds, float Now, const APawn* Player)
{
	// The conversation lasts while the dialogue is up, and longer while the player stays close.
	const bool bNear = Player && FVector::DistSquared2D(Player->GetActorLocation(), GetActorLocation()) < FMath::Square(InteractRange + 150.f);
	if (bNear && Now < ConversationUntil)
	{
		TurnTowards((Player->GetActorLocation() - GetActorLocation()).Rotation().Yaw, DeltaSeconds, 360.f);
		return;
	}
	// Back to work after a short moment.
	Listener.Reset();
	State = EIdleState::Standing;
	StandYaw = HomeYaw;
	StateUntil = Now + FMath::FRandRange(1.5f, 3.f);
}

void APixelNpc::TickIdle(float DeltaSeconds, float Now)
{
	if (State == EIdleState::Standing)
	{
		TurnTowards(StandYaw, DeltaSeconds, 120.f);
		if (Now >= StateUntil)
		{
			if (PickWalkTarget())
			{
				State = EIdleState::Walking;
				StateUntil = Now + 20.f;   // gives up on a target it cannot reach
			}
			else
			{
				// Look around for a bit instead: mostly back at its work, sometimes elsewhere.
				StandYaw = FMath::FRand() < 0.6f ? HomeYaw : HomeYaw + FMath::FRandRange(-120.f, 120.f);
				StateUntil = Now + FMath::FRandRange(PauseTime.X, PauseTime.Y);
			}
		}
		return;
	}

	// Walking.
	FVector ToTarget = WalkTarget - GetActorLocation();
	ToTarget.Z = 0.f;
	const float Distance = ToTarget.Size();
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	const bool bBlocked = Player && FVector::DistSquared2D(Player->GetActorLocation() , GetActorLocation() + ToTarget.GetSafeNormal() * 60.f) < FMath::Square(70.f);
	if (Distance < 10.f || Now >= StateUntil)
	{
		State = EIdleState::Standing;
		// At home it faces its work; elsewhere it looks about.
		StandYaw = FVector::DistSquared2D(GetActorLocation(), Home) < FMath::Square(60.f) || FMath::FRand() < 0.5f
			? HomeYaw : FMath::FRandRange(-180.f, 180.f);
		StateUntil = Now + FMath::FRandRange(PauseTime.X, PauseTime.Y);
		return;
	}
	TurnTowards(ToTarget.Rotation().Yaw, DeltaSeconds, 240.f);
	if (bBlocked)
	{
		return;   // waits for the player to step aside
	}
	const FVector Step = ToTarget.GetSafeNormal() * FMath::Min(Distance, WalkSpeed * DeltaSeconds);
	FVector Ground;
	if (FindGround(GetActorLocation() + Step, Ground))
	{
		SetActorLocation(Ground);
	}
	else
	{
		StateUntil = Now;   // off the edge of the world: stop here
	}
}

bool APixelNpc::PickWalkTarget()
{
	if (WanderRadius <= 0.f)
	{
		return false;
	}
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		// Every other stroll goes back home, so it never drifts to the edge of its area and stays there.
		const bool bHome = FVector::DistSquared2D(GetActorLocation(), Home) > FMath::Square(80.f) && FMath::FRand() < 0.5f;
		const float Angle = FMath::FRandRange(0.f, 2.f * UE_PI);
		const float Radius = FMath::FRandRange(0.35f, 1.f) * WanderRadius;
		const FVector Candidate = bHome ? Home : Home + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius;
		FVector Ground;
		if (!FindGround(Candidate, Ground) || FMath::Abs(Ground.Z - GetActorLocation().Z) > 120.f)
		{
			continue;
		}
		// The way there must be free of houses, rocks, trunks and other villagers.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PixelNpcWalk), false, this);
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(34.f, 45.f);
		const FVector Lift(0.f, 0.f, 100.f);
		if (GetWorld()->SweepTestByChannel(GetActorLocation() + Lift, Ground + Lift, FQuat::Identity, ECC_Pawn, Shape, Params))
		{
			continue;
		}
		WalkTarget = Ground;
		return true;
	}
	return false;
}

bool APixelNpc::FindGround(const FVector& Point, FVector& OutGround) const
{
	// Villagers walk on the terrain: grass tufts, flowers and props in the way are skipped.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PixelNpcGround), false, this);
	for (int32 Attempt = 0; Attempt < 6; ++Attempt)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 150.f), Point - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params)
			|| !Hit.GetComponent())
		{
			return false;
		}
		if (Hit.GetActor() && Hit.GetActor()->IsA<ALandscapeProxy>())
		{
			OutGround = Hit.Location;
			return true;
		}
		Params.AddIgnoredComponent(Hit.GetComponent());
	}
	return false;
}

void APixelNpc::TurnTowards(float Yaw, float DeltaSeconds, float DegreesPerSecond)
{
	const float NewYaw = FMath::FixedTurn(GetActorRotation().Yaw, Yaw, DegreesPerSecond * DeltaSeconds);
	SetActorRotation(FRotator(0.f, NewYaw, 0.f));
}

FString APixelNpc::GetDisplayName() const
{
	if (!DisplayName.IsEmpty())
	{
		return DisplayName;
	}
	const FPixelNpcDef* Npc = PixelQuestCatalog::FindNpc(NpcId);
	return Npc ? Npc->DisplayName : NpcId.ToString();
}

FString APixelNpc::GetPrompt(const APixelPlayerCharacter* Player) const
{
	return FString::Printf(TEXT("%s에게 말 걸기"), *GetDisplayName());
}

FString APixelNpc::GetMarker(const APixelPlayerCharacter* Player) const
{
	const UPixelQuestComponent* Quest = Player ? Player->GetQuest() : nullptr;
	if (!Quest)
	{
		return FString();
	}
	if (Quest->FindTurnInFor(NpcId))
	{
		return TEXT("?");
	}
	return Quest->FindOfferFor(NpcId) ? TEXT("!") : FString();
}

void APixelNpc::Interact(APixelPlayerCharacter* Player)
{
	UPixelQuestComponent* Quest = Player ? Player->GetQuest() : nullptr;
	if (!Quest)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	TalkUntil = Now + 3.f;
	// Stops what it is doing and turns to the player for the conversation.
	State = EIdleState::Talking;
	Listener = Player;
	ConversationUntil = Now + 12.f;
	const FString Name = GetDisplayName();

	// Hand in first, then offer the next quest, then talk about the one under way.
	bool bChanged = false;
	if (const FPixelQuestDef* Ready = Quest->FindTurnInFor(NpcId))
	{
		const float HealthBefore = Player->GetMaxHealth();
		if (Quest->TurnIn(Ready->Id))
		{
			bChanged = true;
			Player->ApplyQuestBonus(true);
			UPixelAudioSubsystem::Play(this, EPixelSound::NpcJoy);
			UPixelAudioSubsystem::Play(this, EPixelSound::QuestComplete);
			if (Ready->Reward.Coins > 0)
			{
				UPixelAudioSubsystem::Play(this, EPixelSound::Coins);
			}
			Player->ShowDialogue(Name, Ready->CompleteText);

			FString Reward = FString::Printf(TEXT("퀘스트 완료: %s"), *Ready->Title);
			if (Ready->Reward.Coins > 0)
			{
				Reward += FString::Printf(TEXT("   코인 +%d"), Ready->Reward.Coins);
			}
			if (Player->GetMaxHealth() > HealthBefore)
			{
				Reward += FString::Printf(TEXT("   최대 체력 +%.0f"), Player->GetMaxHealth() - HealthBefore);
			}
			if (!Ready->Reward.UnlockWeapon.IsNone())
			{
				Reward += FString::Printf(TEXT("   새 무기: %s [Tab]"), *PixelQuestCatalog::GetItemName(Ready->Reward.UnlockWeapon));
			}
			Player->ShowNotice(Reward, 4.f);
		}
	}
	else if (const FPixelQuestDef* Offer = Quest->FindOfferFor(NpcId))
	{
		bChanged = Quest->Accept(Offer->Id);
		UPixelAudioSubsystem::Play(this, EPixelSound::NpcHello);
		UPixelAudioSubsystem::Play(this, EPixelSound::QuestAccept);
		Player->ShowDialogue(Name, Offer->OfferText, 8.f);
		Player->ShowNotice(FString::Printf(TEXT("새 퀘스트: %s"), *Offer->Title));
	}
	else if (const FPixelQuestDef* Active = Quest->FindActiveFor(NpcId))
	{
		UPixelAudioSubsystem::Play(this, EPixelSound::NpcReply);
		Player->ShowDialogue(Name, Active->ProgressText);
	}
	else
	{
		UPixelAudioSubsystem::Play(this, EPixelSound::NpcReply);
		const FPixelNpcDef* Npc = PixelQuestCatalog::FindNpc(NpcId);
		Player->ShowDialogue(Name, Npc ? Npc->IdleText : TEXT("좋은 하루 보내요."));
	}

	if (bChanged)
	{
		if (APixelGameMode* GameMode = GetWorld()->GetAuthGameMode<APixelGameMode>())
		{
			GameMode->SaveGame(TEXT("quest"));
		}
	}
}
