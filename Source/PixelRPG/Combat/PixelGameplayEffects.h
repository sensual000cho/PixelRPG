#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "PixelGameplayEffects.generated.h"

/** Instant damage. The amount is passed by the ability as a SetByCaller magnitude tagged Data.Damage. */
UCLASS()
class PIXELRPG_API UPixelGE_Damage : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UPixelGE_Damage();
};

/** Timed buff: +50% attack power. */
UCLASS()
class PIXELRPG_API UPixelGE_Empower : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UPixelGE_Empower();

	static constexpr float DurationSeconds = 6.f;
};
