#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PixelInteractable.generated.h"

class APixelPlayerCharacter;
class UCapsuleComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Something the player can walk up to and use with the interact key. */
UCLASS(Abstract)
class PIXELRPG_API APixelInteractable : public AActor
{
	GENERATED_BODY()

public:
	APixelInteractable();

	virtual bool CanInteract(const APixelPlayerCharacter* Player) const { return true; }
	/** Shown next to the key hint, e.g. "Gather Moonleaf". */
	virtual FString GetPrompt(const APixelPlayerCharacter* Player) const { return TEXT("Interact"); }
	/** Short text floated above the actor ("!" for a quest), or empty. */
	virtual FString GetMarker(const APixelPlayerCharacter* Player) const { return FString(); }
	virtual void Interact(APixelPlayerCharacter* Player) {}

	UPROPERTY(EditAnywhere, Category = "Interaction")
	float InteractRange = 190.f;

	/** Height above the actor origin (its feet) where the HUD draws the marker. */
	UPROPERTY(EditAnywhere, Category = "Interaction")
	float MarkerHeight = 110.f;

protected:
	void SetMesh(const TCHAR* AssetPath);

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> Mesh;
};

/** A resource node: gives an item when used, then disappears until it grows back. */
UCLASS()
class PIXELRPG_API APixelGatherable : public APixelInteractable
{
	GENERATED_BODY()

public:
	/** Called once right after spawning. NodeId identifies the node in save games. */
	void Configure(FName InNodeId, FName InItemId, const TCHAR* MeshPath, float Scale);

	virtual bool CanInteract(const APixelPlayerCharacter* Player) const override { return !bDepleted; }
	virtual FString GetPrompt(const APixelPlayerCharacter* Player) const override;
	virtual void Interact(APixelPlayerCharacter* Player) override;

	FName GetNodeId() const { return NodeId; }
	FName GetItemId() const { return ItemId; }
	bool IsDepleted() const { return bDepleted; }
	void SetDepleted(bool bNewDepleted);

	UPROPERTY(EditAnywhere, Category = "Gathering")
	int32 Amount = 1;

	UPROPERTY(EditAnywhere, Category = "Gathering")
	float RespawnSeconds = 60.f;

private:
	void Regrow();

	FName NodeId;
	FName ItemId;
	bool bDepleted = false;
	FTimerHandle RespawnTimer;
};

/** The herbalist: gives out and completes the quest. Turns to face the player when they come close. */
UCLASS()
class PIXELRPG_API APixelNpc : public APixelInteractable
{
	GENERATED_BODY()

public:
	APixelNpc();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual FString GetPrompt(const APixelPlayerCharacter* Player) const override;
	virtual FString GetMarker(const APixelPlayerCharacter* Player) const override;
	virtual void Interact(APixelPlayerCharacter* Player) override;

	UPROPERTY(EditAnywhere, Category = "Npc")
	FString DisplayName = TEXT("Herbalist Orlen");

private:
	UPROPERTY(VisibleAnywhere, Category = "Npc")
	TObjectPtr<UCapsuleComponent> Body;

	/** Flip-book poses (VILLAGER_POSES in Tools/make_assets_blender.py): 0-1 idle, 2-3 talking. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> PoseMeshes;

	float TalkUntil = -1.f;
};
