#include "World/PixelInteractable.h"

#include "Combat/PixelPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "PixelAudio.h"
#include "PixelGameMode.h"
#include "TimerManager.h"
#include "World/PixelInventoryComponent.h"
#include "World/PixelQuestComponent.h"

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

void APixelGatherable::Configure(FName InNodeId, FName InItemId, const TCHAR* MeshPath, float Scale)
{
	NodeId = InNodeId;
	ItemId = InItemId;
	SetMesh(MeshPath);
	Mesh->SetRenderCustomDepth(false); // part of the world, not a character: stays full resolution
	SetActorScale3D(FVector(Scale));
}

FString APixelGatherable::GetPrompt(const APixelPlayerCharacter* Player) const
{
	return FString::Printf(TEXT("Gather %s"), *ItemId.ToString());
}

void APixelGatherable::Interact(APixelPlayerCharacter* Player)
{
	if (bDepleted || !Player)
	{
		return;
	}
	const bool bWasReady = Player->GetQuest()->IsReadyToTurnIn();
	Player->GetInventory()->Add(ItemId, Amount);
	const bool bHerb = ItemId == UPixelQuestComponent::HerbItem;
	UPixelAudioSubsystem::Play(this, bHerb ? EPixelSound::GatherHerb : EPixelSound::GatherOre);
	UPixelAudioSubsystem::Play(this, !bWasReady && Player->GetQuest()->IsReadyToTurnIn() ? EPixelSound::QuestReady : EPixelSound::ItemObtained);
	Player->ShowNotice(FString::Printf(TEXT("+%d %s"), Amount, *ItemId.ToString()));
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
	SetMesh(TEXT("/Game/Meshes/SM_Villager_00.SM_Villager_00"));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		PoseMeshes.Add(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Meshes/SM_Villager_%02d.SM_Villager_%02d"), Index, Index)));
	}
}

void APixelNpc::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Now = GetWorld()->GetTimeSeconds();
	const int32 Pose = Now < TalkUntil ? 2 + static_cast<int32>(Now * 4.f) % 2 : static_cast<int32>(Now * 1.2f) % 2;
	if (PoseMeshes.IsValidIndex(Pose) && PoseMeshes[Pose] && Mesh->GetStaticMesh() != PoseMeshes[Pose])
	{
		Mesh->SetStaticMesh(PoseMeshes[Pose]);
	}

	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (Player && FVector::DistSquared2D(Player->GetActorLocation(), GetActorLocation()) < FMath::Square(600.f))
	{
		const float Wanted = (Player->GetActorLocation() - GetActorLocation()).Rotation().Yaw;
		const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, Wanted, 240.f * DeltaSeconds);
		SetActorRotation(FRotator(0.f, Yaw, 0.f));
	}
}

FString APixelNpc::GetPrompt(const APixelPlayerCharacter* Player) const
{
	return FString::Printf(TEXT("Talk to %s"), *DisplayName);
}

FString APixelNpc::GetMarker(const APixelPlayerCharacter* Player) const
{
	const UPixelQuestComponent* Quest = Player ? Player->GetQuest() : nullptr;
	if (!Quest)
	{
		return FString();
	}
	if (Quest->GetState() == EPixelQuestState::NotStarted)
	{
		return TEXT("!");
	}
	return Quest->IsReadyToTurnIn() ? TEXT("?") : FString();
}

void APixelNpc::Interact(APixelPlayerCharacter* Player)
{
	UPixelQuestComponent* Quest = Player ? Player->GetQuest() : nullptr;
	if (!Quest)
	{
		return;
	}
	TalkUntil = GetWorld()->GetTimeSeconds() + 3.f;

	bool bChanged = false;
	switch (Quest->GetState())
	{
	case EPixelQuestState::NotStarted:
		bChanged = Quest->Accept();
		UPixelAudioSubsystem::Play(this, EPixelSound::NpcHello);
		UPixelAudioSubsystem::Play(this, EPixelSound::QuestAccept);
		Player->ShowDialogue(DisplayName, FString::Printf(
			TEXT("Slimes keep trampling my moonleaf beds.\nBring me %d Moonleaf and thin out %d of those slimes, would you?"),
			Quest->HerbsNeeded, Quest->KillsNeeded));
		break;

	case EPixelQuestState::Active:
		if (Quest->TurnIn())
		{
			bChanged = true;
			Player->ApplyQuestBonus(true);
			UPixelAudioSubsystem::Play(this, EPixelSound::NpcJoy);
			UPixelAudioSubsystem::Play(this, EPixelSound::QuestComplete);
			UPixelAudioSubsystem::Play(this, EPixelSound::Coins);
			Player->ShowDialogue(DisplayName, FString::Printf(
				TEXT("That is the lot, and the garden is quiet again!\nDrink this tonic - and here, %d coin for your trouble."), Quest->CoinReward));
			Player->ShowNotice(TEXT("Quest complete: max health +25"));
		}
		else
		{
			UPixelAudioSubsystem::Play(this, EPixelSound::NpcReply);
			Player->ShowDialogue(DisplayName, FString::Printf(TEXT("Not done yet? I still need %d Moonleaf and %d slimes gone."),
				Quest->HerbsNeeded - Quest->GetHerbs(), Quest->KillsNeeded - Quest->GetKills()));
		}
		break;

	default:
		UPixelAudioSubsystem::Play(this, EPixelSound::NpcReply);
		Player->ShowDialogue(DisplayName, TEXT("The moonleaf is growing back nicely. Thank you, traveller."));
		break;
	}

	if (bChanged)
	{
		if (APixelGameMode* GameMode = GetWorld()->GetAuthGameMode<APixelGameMode>())
		{
			GameMode->SaveGame(TEXT("quest"));
		}
	}
}
