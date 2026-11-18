#pragma once

#include "CoreMinimal.h"
#include "Combat/PixelCharacterBase.h"
#include "World/PixelInteractable.h"
#include "PixelQuestActors.generated.h"

class APixelEnemyCharacter;

/**
 * A place a Reach objective sends the player to. Shows a floating crystal and a "v" mark while a quest wants it,
 * and counts as reached when the player walks within ReachRadius.
 */
UCLASS()
class PIXELRPG_API APixelQuestMarker : public APixelInteractable
{
	GENERATED_BODY()

public:
	APixelQuestMarker();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	/** Only "usable" (so the HUD draws its mark) while a quest wants it. Never focused: InteractRange is 0. */
	virtual bool CanInteract(const APixelPlayerCharacter* Player) const override;
	virtual FString GetMarker(const APixelPlayerCharacter* Player) const override { return TEXT("v"); }

	/** What Reach objectives ask for (FPixelQuestObjective::Target). */
	UPROPERTY(EditAnywhere, Category = "Quest")
	FName MarkerId;

	UPROPERTY(EditAnywhere, Category = "Quest")
	float ReachRadius = 260.f;

private:
	bool IsWanted() const;
};

/**
 * Something to practise on: takes hits, never dies, does not move or fight back.
 * Every hit the player lands counts for the HitDummy action.
 */
UCLASS()
class PIXELRPG_API APixelTrainingDummy : public APixelCharacterBase
{
	GENERATED_BODY()

public:
	APixelTrainingDummy();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void HandleDamaged(float Amount, AActor* DamageInstigator) override;

protected:
	virtual void HandleDeath(AActor* Killer) override;

private:
	/** Post, cross bar and head, hung under the body mesh so they squash and wobble with it. */
	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	FVector Home = FVector::ZeroVector;
	float WobbleTime = 10.f;
	float WobbleSide = 1.f;
};

/**
 * Keeps up to MaxAlive enemies around itself, optionally only while a quest is active.
 * Enemies get EnemyTag as an actor tag, which Kill objectives can ask for.
 */
UCLASS()
class PIXELRPG_API APixelEnemySpawner : public AActor
{
	GENERATED_BODY()

public:
	APixelEnemySpawner();

	virtual void BeginPlay() override;

	/** Any enemy class: the slime by default, other monsters as they are added. */
	UPROPERTY(EditAnywhere, Category = "Spawner")
	TSubclassOf<APixelEnemyCharacter> EnemyClass;

	UPROPERTY(EditAnywhere, Category = "Spawner")
	int32 MaxAlive = 3;

	UPROPERTY(EditAnywhere, Category = "Spawner")
	float SpawnRadius = 400.f;

	/** Seconds between an enemy dying and the next one appearing. */
	UPROPERTY(EditAnywhere, Category = "Spawner")
	float RespawnDelay = 5.f;

	UPROPERTY(EditAnywhere, Category = "Spawner")
	FName EnemyTag;

	/** 0 keeps the enemy class's own health. */
	UPROPERTY(EditAnywhere, Category = "Spawner")
	float EnemyHealth = 0.f;

	UPROPERTY(EditAnywhere, Category = "Spawner")
	float EnemyScale = 1.f;

	/** When set, only spawns while this quest is active. */
	UPROPERTY(EditAnywhere, Category = "Spawner")
	FName RequiredQuest;

	/** Only spawns while an active quest still wants enemies with EnemyTag killed (a boss appears once). */
	UPROPERTY(EditAnywhere, Category = "Spawner")
	bool bOnlyWhileWanted = false;

private:
	void Update();
	bool IsActive() const;
	bool FindGround(const FVector2D& Point, FVector& OutLocation) const;

	TArray<TWeakObjectPtr<APixelEnemyCharacter>> Spawned;
	float NextSpawnTime = 0.f;
	FTimerHandle Timer;
};
