#pragma once

#include "CoreMinimal.h"
#include "PixelWeaponTypes.generated.h"

class UStaticMesh;

/** Everything the melee ability needs to know about a weapon. Plain data so it can later move to a data asset. */
USTRUCT(BlueprintType)
struct FPixelWeaponDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Weapon")
	FString Name = TEXT("Weapon");

	UPROPERTY(EditAnywhere, Category = "Weapon")
	float Damage = 10.f;

	/** Reach from the attacker's centre, in world units. */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	float Range = 150.f;

	/** Full width of the hit cone in degrees. */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	float ArcDegrees = 120.f;

	/** Seconds before the hit lands, the hit stays visible, and the attacker recovers. */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	float Windup = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Weapon")
	float ActiveTime = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Weapon")
	float Recovery = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Weapon")
	float Knockback = 450.f;

	/** Thrust (spear) instead of swing (sword): changes the weapon animation and the swoosh shape. */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	bool bThrust = false;

	/** How far the swoosh is tipped out of the horizontal, in degrees: 0 for a level slash, towards 90 for a chop. */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	float SlashRoll = 0.f;

	UPROPERTY(EditAnywhere, Category = "Weapon")
	TSoftObjectPtr<UStaticMesh> Mesh;

	float TotalTime() const { return Windup + ActiveTime + Recovery; }
};
