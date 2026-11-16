#include "PixelAudio.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"

namespace
{
	struct FSoundDef
	{
		EPixelSound Sound;
		/** Folder below /Game/Dev. */
		const TCHAR* Folder;
		/** Asset names separated by spaces; one is picked at random each time. */
		const TCHAR* Names;
		float Volume;
		float Pitch;
		/** Random pitch change either way, so repeats do not sound identical. */
		float PitchJitter;
	};

	const TCHAR* Shop = TEXT("RPG_Shop_Sounds/WAV");
	const TCHAR* Animals = TEXT("Environment_Sounds/Animals/wav");
	const TCHAR* Misc = TEXT("Environment_Sounds/Miscellaneous/wav");
	const TCHAR* Crafting = TEXT("Environment_Sounds/Crafting/wav");
	const TCHAR* Voice = TEXT("NMVi1/Wavs");

	// Volumes and pitches were chosen from the file names, not by ear: tune them here.
	const FSoundDef SoundDefs[] = {
		{ EPixelSound::Swing, Animals, TEXT("Wing_Flap_1_3 Wing_Flap_1_4 Wing_Flap_1_5"), 0.55f, 1.35f, 0.12f },
		{ EPixelSound::Hit, Misc, TEXT("Soil_Impact_1_1 Soil_Impact_1_2 Soil_Impact_1_3 Soil_Impact_1_4 Soil_Impact_1_5"), 0.9f, 1.1f, 0.1f },
		{ EPixelSound::PlayerHurt, Shop, TEXT("Armour_01 Armour_02 Armour_03"), 0.8f, 1.f, 0.06f },
		{ EPixelSound::PlayerDeath, Shop, TEXT("Break_Item"), 0.9f, 0.85f, 0.f },
		{ EPixelSound::SlimeDeath, Shop, TEXT("Water_01 Water_02 Water_03"), 0.8f, 1.15f, 0.1f },
		{ EPixelSound::Dodge, Shop, TEXT("Clothes"), 0.6f, 1.2f, 0.08f },
		{ EPixelSound::WeaponSwap, Shop, TEXT("Equipment_Interaction_01 Equipment_Interaction_02 Equipment_Interaction_03"), 0.7f, 1.f, 0.05f },
		{ EPixelSound::BoltCast, Misc, TEXT("Fire_Whoosh_1_1 Fire_Whoosh_1_2 Fire_Whoosh_1_3 Fire_Whoosh_1_4 Fire_Whoosh_1_5"), 0.5f, 1.3f, 0.1f },
		{ EPixelSound::BoltHit, Misc, TEXT("Sizzle_1_2 Sizzle_1_3 Sizzle_1_4"), 0.45f, 1.2f, 0.1f },
		{ EPixelSound::Empower, Shop, TEXT("Special_Item_01 Special_Item_02 Special_Item_03"), 0.7f, 1.f, 0.f },
		{ EPixelSound::CompanionMount, Shop, TEXT("UI_Select_Equipment_01"), 0.6f, 1.f, 0.f },
		{ EPixelSound::CompanionDeploy, Shop, TEXT("UI_Select_Equipment_02"), 0.6f, 1.f, 0.f },
		{ EPixelSound::CompanionDowned, Shop, TEXT("Funny_01 Funny_02"), 0.7f, 1.f, 0.f },
		{ EPixelSound::GatherHerb, Misc, TEXT("Foliage_1_1 Foliage_1_2 Foliage_1_3 Foliage_1_4 Foliage_1_5"), 0.9f, 1.f, 0.08f },
		{ EPixelSound::GatherOre, Crafting, TEXT("Mining_1_1 Mining_1_2 Mining_1_3 Mining_1_4 Mining_1_5"), 0.9f, 1.f, 0.06f },
		{ EPixelSound::ItemObtained, Shop, TEXT("Items_Obtained_01 Items_Obtained_02 Items_Obtained_03"), 0.6f, 1.f, 0.f },
		{ EPixelSound::Coins, Shop, TEXT("Coin_01 Coin_02 Coin_03"), 0.8f, 1.f, 0.f },
		{ EPixelSound::QuestAccept, Shop, TEXT("Paper_Envelope_01"), 0.8f, 1.f, 0.f },
		{ EPixelSound::QuestReady, Shop, TEXT("Notification_01"), 0.8f, 1.f, 0.f },
		{ EPixelSound::QuestComplete, Shop, TEXT("Magic_Reward_01"), 0.9f, 1.f, 0.f },
		{ EPixelSound::NpcHello, Voice, TEXT("NMVi1_01_01_Hello"), 1.f, 1.f, 0.f },
		{ EPixelSound::NpcReply, Voice, TEXT("NMVi1_01_04_Reply NMVi1_01_05_Think"), 1.f, 1.f, 0.f },
		{ EPixelSound::NpcJoy, Voice, TEXT("NMVi1_01_08_Joy NMVi1_01_07_Laugh"), 1.f, 1.f, 0.f },
		{ EPixelSound::Save, Shop, TEXT("UI_Click_01"), 0.5f, 1.f, 0.f },
		{ EPixelSound::Load, Shop, TEXT("UI_Click_Select_Option"), 0.5f, 1.f, 0.f },
	};

	struct FAmbienceDef
	{
		const TCHAR* Path;
		float Volume;
	};
	const FAmbienceDef AmbienceDefs[] = {
		{ TEXT("/Game/Dev/Environment_Sounds/Ambient_Loops/wav/Birds_Loop_01.Birds_Loop_01"), 0.35f },
		{ TEXT("/Game/Dev/Environment_Sounds/Ambient_Loops/wav/Wind_Loop_1.Wind_Loop_1"), 0.2f },
	};

	/** Distance from the player at which a positioned sound starts to fade, and where it is gone. */
	const float FullVolumeDistance = 700.f;
	const float SilentDistance = 3200.f;
	/** The same sound is not started twice within this time. */
	const float MinRepeatSeconds = 0.04f;
}

bool UPixelAudioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPixelAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	bMuted = FParse::Param(FCommandLine::Get(), TEXT("PixelMute"));
	const int32 Count = static_cast<int32>(EPixelSound::Count);
	Variations.SetNum(Count);
	LastPlayed.Init(-100.f, Count);
	LastVariation.Init(-1, Count);

	// Load everything up front: the files are small, and a first swing must not hitch.
	int32 Wanted = 0;
	for (const FSoundDef& Def : SoundDefs)
	{
		TArray<FString> Names;
		FString(Def.Names).ParseIntoArray(Names, TEXT(" "));
		for (const FString& Name : Names)
		{
			++Wanted;
			const FString Path = FString::Printf(TEXT("/Game/Dev/%s/%s.%s"), Def.Folder, *Name, *Name);
			if (USoundBase* Loaded = LoadObject<USoundBase>(nullptr, *Path))
			{
				Variations[static_cast<int32>(Def.Sound)].Add(Sounds.Add(Loaded));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("PixelAudio: sound %s not found"), *Path);
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PixelAudio: loaded %d of %d sounds%s"), Sounds.Num(), Wanted, bMuted ? TEXT(" (muted)") : TEXT(""));

	if (!bMuted)
	{
		StartAmbience();
	}
}

void UPixelAudioSubsystem::StartAmbience()
{
	for (const FAmbienceDef& Def : AmbienceDefs)
	{
		USoundBase* Loop = LoadObject<USoundBase>(nullptr, Def.Path);
		if (!Loop)
		{
			UE_LOG(LogTemp, Warning, TEXT("PixelAudio: ambience %s not found"), Def.Path);
			continue;
		}
		// Not auto-destroyed: the files do not loop by themselves, so each one is started again when it ends.
		if (UAudioComponent* Component = UGameplayStatics::SpawnSound2D(this, Loop, Def.Volume, 1.f, 0.f, nullptr, false, false))
		{
			Component->OnAudioFinished.AddDynamic(this, &UPixelAudioSubsystem::RestartAmbience);
			Ambience.Add(Component);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PixelAudio: %d ambient loops playing"), Ambience.Num());
}

void UPixelAudioSubsystem::RestartAmbience()
{
	for (UAudioComponent* Component : Ambience)
	{
		if (Component && !Component->IsPlaying())
		{
			Component->Play();
		}
	}
}

void UPixelAudioSubsystem::PlayInternal(EPixelSound Sound, float Volume)
{
	const int32 Index = static_cast<int32>(Sound);
	if (bMuted || Volume <= 0.01f || !Variations.IsValidIndex(Index) || Variations[Index].Num() == 0)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastPlayed[Index] < MinRepeatSeconds)
	{
		return;
	}
	LastPlayed[Index] = Now;

	// Never the same variation twice in a row when there is a choice.
	const TArray<int32>& Choices = Variations[Index];
	int32 Pick = FMath::RandRange(0, Choices.Num() - 1);
	if (Choices.Num() > 1 && Pick == LastVariation[Index])
	{
		Pick = (Pick + 1) % Choices.Num();
	}
	LastVariation[Index] = Pick;

	for (const FSoundDef& Def : SoundDefs)
	{
		if (Def.Sound == Sound)
		{
			const float Pitch = Def.Pitch + FMath::FRandRange(-Def.PitchJitter, Def.PitchJitter);
			UGameplayStatics::PlaySound2D(this, Sounds[Choices[Pick]], Def.Volume * Volume, Pitch);
			return;
		}
	}
}

void UPixelAudioSubsystem::Play(const UObject* WorldContext, EPixelSound Sound, float Volume)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (UPixelAudioSubsystem* Audio = World ? World->GetSubsystem<UPixelAudioSubsystem>() : nullptr)
	{
		Audio->PlayInternal(Sound, Volume);
	}
}

void UPixelAudioSubsystem::PlayAt(const UObject* WorldContext, EPixelSound Sound, const FVector& Location, float Volume)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UPixelAudioSubsystem* Audio = World ? World->GetSubsystem<UPixelAudioSubsystem>() : nullptr;
	if (!Audio)
	{
		return;
	}
	// The camera circles the player at a distance, so loudness follows the distance to the player instead.
	const APlayerController* PC = World->GetFirstPlayerController();
	if (const APawn* Player = PC ? PC->GetPawn() : nullptr)
	{
		const float Distance = FVector::Dist(Player->GetActorLocation(), Location);
		Volume *= 1.f - FMath::Clamp((Distance - FullVolumeDistance) / (SilentDistance - FullVolumeDistance), 0.f, 1.f);
	}
	Audio->PlayInternal(Sound, Volume);
}
