#include "PixelGameMode.h"

#include "Combat/PixelEnemyCharacter.h"
#include "Combat/PixelPlayerCharacter.h"
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
#include "World/PixelQuestActors.h"
#include "World/PixelQuestComponent.h"
#include "World/PixelSaveGame.h"

APixelGameMode::APixelGameMode()
{
	DefaultPawnClass = APixelPlayerCharacter::StaticClass();
	HUDClass = APixelHUD::StaticClass();
	QuestLockedWeapons = { TEXT("Spear") };   // Smith Hilda's reward (Smithing quest)
}

void APixelGameMode::BeginPlay()
{
	Super::BeginPlay();

	FParse::Value(FCommandLine::Get(), TEXT("PixelSaveSlot="), SaveSlot);
	LogWorldObjects();

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
		Player->GetQuest()->NotifyEnemyKilled(Enemy);
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

void APixelGameMode::LogWorldObjects() const
{
	int32 Npcs = 0, Nodes = 0, Markers = 0, Spawners = 0, Dummies = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		Npcs += It->IsA<APixelNpc>();
		Nodes += It->IsA<APixelGatherable>();
		Markers += It->IsA<APixelQuestMarker>();
		Spawners += It->IsA<APixelEnemySpawner>();
		Dummies += It->IsA<APixelTrainingDummy>();
	}
	UE_LOG(LogTemp, Display, TEXT("PixelWorld: map has %d NPCs, %d gathering nodes, %d markers, %d spawners, %d dummies"),
		Npcs, Nodes, Markers, Spawners, Dummies);
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
	Save->Quests = Player->GetQuest()->GetRecords();
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
	UE_LOG(LogTemp, Display, TEXT("PixelSave: saved (%s) quests %d items %d depleted %d"), Reason,
		Save->Quests.Num(), Save->Items.Num(), Save->DepletedNodes.Num());
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
	TArray<FPixelQuestRecord> Quests = Save->Quests;
	if (Save->Version < 2 && Save->QuestState > 0)
	{
		// Version 1 knew only the garden quest: its state and slime count.
		FPixelQuestRecord& Garden = Quests.AddDefaulted_GetRef();
		Garden.Id = TEXT("Garden");
		Garden.State = Save->QuestState;
		Garden.Progress = { 0, Save->QuestKills };
	}
	Player->GetQuest()->RestoreRecords(Quests);
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
	FString QuestText;
	for (const FPixelQuestRecord& Record : Quests)
	{
		QuestText += FString::Printf(TEXT(" %s=%d"), *Record.Id.ToString(), Record.State);
	}
	UE_LOG(LogTemp, Display, TEXT("PixelSave: loaded slot %s: quests:%s, total kills %d, max health %.0f, at %s, items:%s"), *SaveSlot,
		*QuestText, KillCount, Player->GetMaxHealth(), *Save->PlayerLocation.ToCompactString(), *Items);
	return true;
}
