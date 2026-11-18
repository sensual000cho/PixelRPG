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
	/** Shown next to the key hint, e.g. "월광초 채집". */
	virtual FString GetPrompt(const APixelPlayerCharacter* Player) const { return TEXT("상호작용"); }
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
	APixelGatherable();

	virtual void BeginPlay() override;
	/** Called once right after spawning. NodeId identifies the node in save games. Placed nodes set the properties instead. */
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

protected:
	/** Unique per map: save games remember empty nodes by it. */
	UPROPERTY(EditAnywhere, Category = "Gathering")
	FName NodeId;

	/** Item given, which is also its display name ("Moonleaf", "Copper Ore"). */
	UPROPERTY(EditAnywhere, Category = "Gathering")
	FName ItemId;

private:
	void Regrow();

	bool bDepleted = false;
	FTimerHandle RespawnTimer;
};

/**
 * A villager who gives out and takes back the quests of PixelQuestCatalog whose Giver / TurnIn is its NpcId.
 * Goes about its day around the spot it was placed on: strolls to a nearby point, stands there a while facing its
 * work (the direction it was placed with) or looking around, then strolls on. Only when the player talks to it does it
 * stop and turn to them, and it keeps facing them while they stay for the conversation.
 */
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

	/** Key into PixelQuestCatalog: which quests this NPC deals with, its name and its idle line. */
	UPROPERTY(EditAnywhere, Category = "Npc")
	FName NpcId = TEXT("Orlen");

	/** Overrides the catalog name when set. */
	UPROPERTY(EditAnywhere, Category = "Npc")
	FString DisplayName;

	FString GetDisplayName() const;

	/** How far from where it was placed the NPC strolls. 0: it stays on its spot (and still looks around). */
	UPROPERTY(EditAnywhere, Category = "Npc|Idle", meta = (ClampMin = "0"))
	float WanderRadius = 250.f;

	UPROPERTY(EditAnywhere, Category = "Npc|Idle", meta = (ClampMin = "10"))
	float WalkSpeed = 70.f;

	/** Seconds it stands at a spot before strolling on (random in this range). */
	UPROPERTY(EditAnywhere, Category = "Npc|Idle")
	FVector2D PauseTime = FVector2D(4.f, 9.f);

private:
	enum class EIdleState : uint8 { Standing, Walking, Talking };

	void TickIdle(float DeltaSeconds, float Now);
	void TickTalking(float DeltaSeconds, float Now, const APawn* Player);
	/** Picks the next spot to walk to; false if none was found (it then stands a while longer). */
	bool PickWalkTarget();
	/** Ground height below a point (landscape or anything walkable), ignoring this NPC. */
	bool FindGround(const FVector& Point, FVector& OutGround) const;
	void TurnTowards(float Yaw, float DeltaSeconds, float DegreesPerSecond);

	UPROPERTY(VisibleAnywhere, Category = "Npc")
	TObjectPtr<UCapsuleComponent> Body;

	/** Flip-book poses (VILLAGER_POSES in Tools/make_assets_blender.py): 0-1 idle, 2-3 talking. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> PoseMeshes;

	float TalkUntil = -1.f;

	EIdleState State = EIdleState::Standing;
	FVector Home = FVector::ZeroVector;
	float HomeYaw = 0.f;
	FVector WalkTarget = FVector::ZeroVector;
	/** Where it looks while standing: its work (HomeYaw) most of the time, now and then somewhere else. */
	float StandYaw = 0.f;
	float StateUntil = 0.f;
	/** The player it is talking to, and when the conversation ends if they do not walk away first. */
	TWeakObjectPtr<const APawn> Listener;
	float ConversationUntil = -1.f;
};
