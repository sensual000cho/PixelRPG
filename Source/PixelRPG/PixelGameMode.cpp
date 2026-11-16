#include "PixelGameMode.h"

#include "Combat/PixelEnemyCharacter.h"
#include "Combat/PixelPlayerCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "LandscapeProxy.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PixelHUD.h"
#include "TimerManager.h"
#include "World/PixelInteractable.h"
#include "World/PixelInventoryComponent.h"
#include "World/PixelQuestComponent.h"
#include "World/PixelSaveGame.h"

APixelGameMode::APixelGameMode()
{
	DefaultPawnClass = APixelPlayerCharacter::StaticClass();
	HUDClass = APixelHUD::StaticClass();
}

void APixelGameMode::BeginPlay()
{
	Super::BeginPlay();

	FParse::Value(FCommandLine::Get(), TEXT("PixelSaveSlot="), SaveSlot);
	SpawnWorldObjects();

	GetWorldTimerManager().SetTimer(SpawnTimer, this, &APixelGameMode::SpawnEnemies, SpawnInterval, true, 1.f);
	GetWorldTimerManager().SetTimer(AutosaveTimer, this, &APixelGameMode::Autosave, AutosaveInterval, true);
	// The player pawn may begin play after the game mode; load once everything exists.
	GetWorldTimerManager().SetTimerForNextTick(this, &APixelGameMode::LoadOnStart);
}

APixelPlayerCharacter* APixelGameMode::GetPlayer() const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	return PC ? Cast<APixelPlayerCharacter>(PC->GetPawn()) : nullptr;
}

void APixelGameMode::NotifyEnemyKilled(APixelEnemyCharacter* Enemy)
{
	++KillCount;
	UE_LOG(LogTemp, Display, TEXT("PixelCombat: enemy killed (total %d)"), KillCount);
	if (const APixelPlayerCharacter* Player = GetPlayer())
	{
		Player->GetQuest()->NotifyEnemyKilled();
	}
}

bool APixelGameMode::FindGround(const FVector2D& Point, FVector& OutLocation) const
{
	const FVector Start(Point.X, Point.Y, 8000.f);
	const FVector End(Point.X, Point.Y, -4000.f);
	FCollisionQueryParams Params;
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) || !Hit.GetComponent())
		{
			return false;
		}
		if (Hit.GetActor() && Hit.GetActor()->IsA<ALandscapeProxy>())
		{
			OutLocation = Hit.Location;
			return true;
		}
		Params.AddIgnoredComponent(Hit.GetComponent());
	}
	return false;
}

void APixelGameMode::SpawnWorldObjects()
{
	// Same coordinates as the cleared spots in Tools/setup_pixel_assets.py (build_level).
	FVector Ground;
	if (FindGround(FVector2D(300.f, -300.f), Ground))
	{
		// Standing in front of the house, looking at the start of the path.
		GetWorld()->SpawnActor<APixelNpc>(APixelNpc::StaticClass(), Ground, FRotator(0.f, 135.f, 0.f));
	}

	struct FNode
	{
		const TCHAR* Id;
		const TCHAR* Item;
		const TCHAR* Mesh;
		float Scale;
		FVector2D Point;
	};
	const TCHAR* HerbMesh = TEXT("/Game/Meshes/SM_Herb.SM_Herb");
	const TCHAR* OreMesh = TEXT("/Game/Meshes/SM_Ore.SM_Ore");
	const FNode Nodes[] = {
		{ TEXT("Herb0"), TEXT("Moonleaf"), HerbMesh, 1.5f, FVector2D(-260.f, -320.f) },
		{ TEXT("Herb1"), TEXT("Moonleaf"), HerbMesh, 1.3f, FVector2D(220.f, 400.f) },
		{ TEXT("Herb2"), TEXT("Moonleaf"), HerbMesh, 1.6f, FVector2D(-720.f, -160.f) },
		{ TEXT("Herb3"), TEXT("Moonleaf"), HerbMesh, 1.4f, FVector2D(480.f, 260.f) },
		{ TEXT("Herb4"), TEXT("Moonleaf"), HerbMesh, 1.5f, FVector2D(-160.f, 720.f) },
		{ TEXT("Herb5"), TEXT("Moonleaf"), HerbMesh, 1.3f, FVector2D(920.f, 220.f) },
		{ TEXT("Herb6"), TEXT("Moonleaf"), HerbMesh, 1.6f, FVector2D(-900.f, 500.f) },
		{ TEXT("Herb7"), TEXT("Moonleaf"), HerbMesh, 1.4f, FVector2D(300.f, 1100.f) },
		{ TEXT("Ore0"), TEXT("Copper Ore"), OreMesh, 1.0f, FVector2D(-380.f, 250.f) },
		{ TEXT("Ore1"), TEXT("Copper Ore"), OreMesh, 1.2f, FVector2D(-700.f, 560.f) },
		{ TEXT("Ore2"), TEXT("Copper Ore"), OreMesh, 1.1f, FVector2D(1150.f, -250.f) },
		{ TEXT("Ore3"), TEXT("Copper Ore"), OreMesh, 1.3f, FVector2D(-1100.f, -500.f) },
	};
	int32 Placed = 0;
	for (const FNode& Node : Nodes)
	{
		if (!FindGround(Node.Point, Ground))
		{
			continue;
		}
		const FRotator Rotation(0.f, FMath::Fmod(Node.Point.X * 0.37f + Node.Point.Y * 0.11f, 360.f), 0.f);
		if (APixelGatherable* Gatherable = GetWorld()->SpawnActor<APixelGatherable>(APixelGatherable::StaticClass(), Ground, Rotation))
		{
			Gatherable->Configure(Node.Id, Node.Item, Node.Mesh, Node.Scale);
			++Placed;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PixelWorld: placed %d of %d gathering nodes"), Placed, static_cast<int32>(UE_ARRAY_COUNT(Nodes)));
}

void APixelGameMode::SpawnEnemies()
{
	const APawn* Player = GetPlayer();
	if (!Player)
	{
		return;
	}

	int32 Alive = 0;
	for (TActorIterator<APixelEnemyCharacter> It(GetWorld()); It; ++It)
	{
		Alive += It->IsDead() ? 0 : 1;
	}

	// One per tick of the timer, so they trickle in instead of appearing as a group.
	if (Alive < MaxEnemies)
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * UE_PI);
		const float Distance = FMath::FRandRange(SpawnMinDistance, SpawnMaxDistance);
		const FVector Around = Player->GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Distance;

		// Find the ground: the terrain rises and falls, so the player's height is not good enough.
		FVector Ground;
		if (!FindGround(FVector2D(Around), Ground))
		{
			return;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
		GetWorld()->SpawnActor<APixelEnemyCharacter>(APixelEnemyCharacter::StaticClass(), Ground + FVector(0.f, 0.f, 110.f), FRotator::ZeroRotator, Params);
	}
}

// --- Save game ---

void APixelGameMode::LoadOnStart()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("PixelNewGame")))
	{
		UGameplayStatics::DeleteGameInSlot(SaveSlot, 0);
		UE_LOG(LogTemp, Display, TEXT("PixelSave: new game, slot %s cleared"), *SaveSlot);
		return;
	}
	if (!LoadGame())
	{
		UE_LOG(LogTemp, Display, TEXT("PixelSave: no save in slot %s, starting fresh"), *SaveSlot);
	}
}

void APixelGameMode::Autosave()
{
	SaveGame(TEXT("autosave"));
}

bool APixelGameMode::SaveGame(const TCHAR* Reason)
{
	const APixelPlayerCharacter* Player = GetPlayer();
	if (!Player || Player->IsDead())
	{
		return false;
	}

	UPixelSaveGame* Save = NewObject<UPixelSaveGame>(this);
	Save->PlayerLocation = Player->GetActorLocation();
	Save->PlayerHealth = Player->GetHealth();
	Save->WeaponIndex = Player->GetWeaponIndex();
	Save->Items = Player->GetInventory()->GetItems();
	Save->QuestState = static_cast<uint8>(Player->GetQuest()->GetState());
	Save->QuestKills = Player->GetQuest()->GetKills();
	Save->KillCount = KillCount;
	for (TActorIterator<APixelGatherable> It(GetWorld()); It; ++It)
	{
		if (It->IsDepleted())
		{
			Save->DepletedNodes.Add(It->GetNodeId());
		}
	}

	if (!UGameplayStatics::SaveGameToSlot(Save, SaveSlot, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("PixelSave: writing slot %s failed"), *SaveSlot);
		return false;
	}
	LastSaveTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Display, TEXT("PixelSave: saved (%s) quest %d kills %d items %d depleted %d"), Reason,
		Save->QuestState, Save->QuestKills, Save->Items.Num(), Save->DepletedNodes.Num());
	return true;
}

bool APixelGameMode::LoadGame()
{
	APixelPlayerCharacter* Player = GetPlayer();
	const UPixelSaveGame* Save = Cast<UPixelSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
	if (!Player || !Save)
	{
		return false;
	}

	KillCount = Save->KillCount;
	Player->GetInventory()->SetItems(Save->Items);
	Player->GetQuest()->Restore(static_cast<EPixelQuestState>(FMath::Min<uint8>(Save->QuestState, static_cast<uint8>(EPixelQuestState::Completed))), Save->QuestKills);
	Player->RestoreFromSave(Save->PlayerLocation, Save->PlayerHealth, Save->WeaponIndex);

	for (TActorIterator<APixelGatherable> It(GetWorld()); It; ++It)
	{
		It->SetDepleted(Save->DepletedNodes.Contains(It->GetNodeId()));
	}

	FString Items;
	for (const TPair<FName, int32>& Item : Save->Items)
	{
		Items += FString::Printf(TEXT(" %s=%d"), *Item.Key.ToString(), Item.Value);
	}
	UE_LOG(LogTemp, Display, TEXT("PixelSave: loaded slot %s: quest %d kills %d, total kills %d, max health %.0f, at %s, items:%s"), *SaveSlot,
		Save->QuestState, Save->QuestKills, KillCount, Player->GetMaxHealth(), *Save->PlayerLocation.ToCompactString(), *Items);
	return true;
}
