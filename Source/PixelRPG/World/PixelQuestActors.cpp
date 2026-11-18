#include "World/PixelQuestActors.h"

#include "Combat/PixelEnemyCharacter.h"
#include "Combat/PixelPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "World/PixelQuestComponent.h"

namespace
{
	APixelPlayerCharacter* GetPlayer(const UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? Cast<APixelPlayerCharacter>(PC->GetPawn()) : nullptr;
	}
}

// --- Quest marker ---

APixelQuestMarker::APixelQuestMarker()
{
	PrimaryActorTick.bCanEverTick = true;
	InteractRange = 0.f;
	MarkerHeight = 190.f;
}

void APixelQuestMarker::BeginPlay()
{
	Super::BeginPlay();
	SetMesh(TEXT("/Game/Environment/Rocks/SM_Crystal.SM_Crystal"));
	Mesh->SetRelativeScale3D(FVector(0.3f));
	Mesh->SetCastShadow(false);
}

bool APixelQuestMarker::IsWanted() const
{
	const APixelPlayerCharacter* Player = GetPlayer(GetWorld());
	return Player && Player->GetQuest()->WantsReach(MarkerId);
}

bool APixelQuestMarker::CanInteract(const APixelPlayerCharacter* Player) const
{
	return Player && Player->GetQuest()->WantsReach(MarkerId);
}

void APixelQuestMarker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APixelPlayerCharacter* Player = GetPlayer(GetWorld());
	const bool bWanted = Player && Player->GetQuest()->WantsReach(MarkerId);
	Mesh->SetVisibility(bWanted);
	if (!bWanted)
	{
		return;
	}
	// A slowly turning crystal bobbing above the spot.
	const float Now = GetWorld()->GetTimeSeconds();
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, 110.f + 12.f * FMath::Sin(Now * 2.2f)));
	Mesh->SetRelativeRotation(FRotator(0.f, Now * 60.f, 0.f));

	if (FVector::DistSquared2D(Player->GetActorLocation(), GetActorLocation()) < FMath::Square(ReachRadius))
	{
		Player->GetQuest()->NotifyReached(MarkerId);
	}
}

// --- Training dummy ---

APixelTrainingDummy::APixelTrainingDummy()
{
	PrimaryActorTick.bCanEverTick = true;
	TeamId = 1;              // the player's attacks hit it; enemies (same team) leave it alone
	StartingHealth = 1000.f;
	FacingSteps = 0;

	struct FPart
	{
		const TCHAR* Name;
		const TCHAR* Shape;
		FVector Location;
		FVector Scale;
	};
	// Engine shapes are 100 units across, centred. The body mesh sits at the feet.
	const FPart Defs[] = {
		{ TEXT("Post"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.f, 0.f, 65.f), FVector(0.14f, 0.14f, 1.3f) },
		{ TEXT("Body"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.f, 0.f, 88.f), FVector(0.42f, 0.42f, 0.62f) },
		{ TEXT("Bar"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.f, 0.f, 104.f), FVector(0.12f, 1.05f, 0.12f) },
		{ TEXT("Head"), TEXT("/Engine/BasicShapes/Sphere.Sphere"), FVector(0.f, 0.f, 138.f), FVector(0.36f) },
	};
	for (const FPart& Def : Defs)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Def.Name);
		Part->SetupAttachment(BodyMesh);
		Part->SetRelativeLocation(Def.Location);
		Part->SetRelativeScale3D(Def.Scale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetRenderCustomDepth(true); // drawn in big pixels like the other characters
		Part->SetReceivesDecals(false);
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		const FString Shape = Def.Shape;
		Part->SetStaticMesh(Shape.Contains(TEXT("Cube")) ? Cube.Object : Shape.Contains(TEXT("Sphere")) ? Sphere.Object : Cylinder.Object);
		Parts.Add(Part);
	}
}

void APixelTrainingDummy::BeginPlay()
{
	Super::BeginPlay();
	Home = GetActorLocation();

	// Straw body and head, wooden post and bar.
	const FLinearColor Straw(0.62f, 0.46f, 0.18f);
	const FLinearColor Wood(0.26f, 0.15f, 0.07f);
	const FLinearColor Colours[] = { Wood, Straw, Wood, Straw };
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		if (UMaterialInstanceDynamic* Material = Parts[Index]->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Colours[Index % UE_ARRAY_COUNT(Colours)]);
		}
	}
}

void APixelTrainingDummy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Rooted to the spot: knockback launches it, this puts it straight back.
	GetCharacterMovement()->StopMovementImmediately();
	if (!GetActorLocation().Equals(Home, 1.f))
	{
		SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// Rocks back and forth after a hit, settling over half a second.
	WobbleTime += DeltaSeconds;
	const float Amount = FMath::Max(0.f, 1.f - WobbleTime / 0.6f);
	BodyMesh->SetRelativeRotation(FRotator(0.f, 0.f, WobbleSide * 18.f * Amount * FMath::Sin(WobbleTime * 28.f)));
}

void APixelTrainingDummy::HandleDamaged(float Amount, AActor* DamageInstigator)
{
	Super::HandleDamaged(Amount, DamageInstigator);
	ResetHealth(); // never shows a health bar, never dies
	WobbleTime = 0.f;
	WobbleSide = -WobbleSide;

	if (APixelPlayerCharacter* Player = Cast<APixelPlayerCharacter>(DamageInstigator))
	{
		Player->GetQuest()->NotifyAction(PixelQuestActions::HitDummy);
	}
}

void APixelTrainingDummy::HandleDeath(AActor* Killer)
{
	ResetHealth();
}

// --- Enemy spawner ---

APixelEnemySpawner::APixelEnemySpawner()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	EnemyClass = APixelEnemyCharacter::StaticClass();
}

void APixelEnemySpawner::BeginPlay()
{
	Super::BeginPlay();
	NextSpawnTime = 1.f;
	GetWorldTimerManager().SetTimer(Timer, this, &APixelEnemySpawner::Update, 0.5f, true, FMath::FRandRange(0.2f, 0.7f));
}

bool APixelEnemySpawner::IsActive() const
{
	if (RequiredQuest.IsNone() && !bOnlyWhileWanted)
	{
		return true;
	}
	const APixelPlayerCharacter* Player = GetPlayer(GetWorld());
	if (!Player)
	{
		return false;
	}
	const UPixelQuestComponent* Quest = Player->GetQuest();
	if (!RequiredQuest.IsNone() && Quest->GetState(RequiredQuest) != EPixelQuestState::Active)
	{
		return false;
	}
	return !bOnlyWhileWanted || Quest->WantsKill(EnemyTag);
}

void APixelEnemySpawner::Update()
{
	const float Now = GetWorld()->GetTimeSeconds();
	const int32 Before = Spawned.Num();
	Spawned.RemoveAll([](const TWeakObjectPtr<APixelEnemyCharacter>& Enemy) { return !Enemy.IsValid() || Enemy->IsDead(); });
	if (Spawned.Num() < Before)
	{
		NextSpawnTime = FMath::Max(NextSpawnTime, Now + RespawnDelay);
	}
	if (!EnemyClass || Spawned.Num() >= MaxAlive || Now < NextSpawnTime || !IsActive())
	{
		return;
	}

	// Somewhere inside the radius, not right next to the player.
	const APixelPlayerCharacter* Player = GetPlayer(GetWorld());
	for (int32 Attempt = 0; Attempt < 6; ++Attempt)
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * UE_PI);
		const float Distance = SpawnRadius * FMath::Sqrt(FMath::FRand());
		const FVector2D Point = FVector2D(GetActorLocation()) + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
		FVector Ground;
		if (!FindGround(Point, Ground) || (Player && FVector::Dist2D(Player->GetActorLocation(), Ground) < 450.f))
		{
			continue;
		}
		const FTransform Transform(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Ground + FVector(0.f, 0.f, 110.f * EnemyScale), FVector(EnemyScale));
		APixelEnemyCharacter* Enemy = GetWorld()->SpawnActorDeferred<APixelEnemyCharacter>(EnemyClass, Transform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
		if (!Enemy)
		{
			continue;
		}
		if (EnemyHealth > 0.f)
		{
			Enemy->StartingHealth = EnemyHealth;
		}
		if (!EnemyTag.IsNone())
		{
			Enemy->Tags.Add(EnemyTag);
		}
		Enemy->FinishSpawning(Transform);
		if (IsValid(Enemy))
		{
			Spawned.Add(Enemy);
			NextSpawnTime = Now + 0.8f; // a group trickles in instead of popping up at once
			UE_LOG(LogTemp, Display, TEXT("PixelWorld: %s spawned %s%s"), *GetActorNameOrLabel(), *Enemy->GetName(),
				EnemyTag.IsNone() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *EnemyTag.ToString()));
		}
		return;
	}
}

bool APixelEnemySpawner::FindGround(const FVector2D& Point, FVector& OutLocation) const
{
	// The terrain, ignoring rocks, houses and characters standing on it.
	const FVector Start(Point.X, Point.Y, GetActorLocation().Z + 4000.f);
	const FVector End(Point.X, Point.Y, GetActorLocation().Z - 4000.f);
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
