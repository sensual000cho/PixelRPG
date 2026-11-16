#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PixelAudio.generated.h"

class UAudioComponent;
class USoundBase;

/** Every sound the game plays. The files behind each one are listed in PixelAudio.cpp. */
enum class EPixelSound : uint8
{
	Swing,          // a weapon cutting the air
	Hit,            // an attack connecting with a body
	PlayerHurt,
	PlayerDeath,
	SlimeDeath,
	Dodge,
	WeaponSwap,
	BoltCast,
	BoltHit,
	Empower,
	CompanionMount,
	CompanionDeploy,
	CompanionDowned,
	GatherHerb,
	GatherOre,
	ItemObtained,
	Coins,
	QuestAccept,
	QuestReady,     // every objective met: go back to the quest giver
	QuestComplete,
	NpcHello,
	NpcReply,
	NpcJoy,
	Save,
	Load,
	Count
};

/**
 * Plays the sound effects and keeps the ambient loops running.
 * The sounds come from the packs under /Game/Dev (RPG_Shop_Sounds, Environment_Sounds, NMVi1).
 *
 * Command line: -PixelMute plays nothing.
 */
UCLASS()
class PIXELRPG_API UPixelAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Plays one of the sound's variations without a position (interface, rewards). */
	static void Play(const UObject* WorldContext, EPixelSound Sound, float Volume = 1.f);
	/** Plays it at a place in the world: quieter the further it is from the player, silent when far away. */
	static void PlayAt(const UObject* WorldContext, EPixelSound Sound, const FVector& Location, float Volume = 1.f);

private:
	void PlayInternal(EPixelSound Sound, float Volume);
	void StartAmbience();

	UFUNCTION()
	void RestartAmbience();

	/** Variations per sound, indexed by EPixelSound. */
	TArray<TArray<int32>> Variations;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> Sounds;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> Ambience;

	/** World time each sound was last played, so the same one never stacks within a frame or two. */
	TArray<float> LastPlayed;
	TArray<int32> LastVariation;
	bool bMuted = false;
};
