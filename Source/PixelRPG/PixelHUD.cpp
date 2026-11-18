#include "PixelHUD.h"

#include "CanvasItem.h"
#include "Combat/PixelCharacterBase.h"
#include "Combat/PixelCompanionCharacter.h"
#include "Combat/PixelEnemyCharacter.h"
#include "Combat/PixelPlayerCharacter.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "PixelCameraRig.h"
#include "PixelGameMode.h"
#include "World/PixelInteractable.h"
#include "World/PixelInventoryComponent.h"
#include "World/PixelQuestComponent.h"

void APixelHUD::DrawHUD()
{
	Super::DrawHUD();

	const APixelPlayerCharacter* Player = Cast<APixelPlayerCharacter>(GetOwningPawn());
	const APixelCameraRig* Rig = Player ? Player->GetCameraRig() : nullptr;
	UTextureRenderTarget2D* Target = Rig ? Rig->GetRenderTarget() : nullptr;
	if (!Canvas || !Target || !Target->GetResource())
	{
		return;
	}

	const int32 ImageScale = Rig->GetPixelScale(); // screen pixels per pixel of the scene image
	const int32 Scale = Rig->GetUiScale();         // screen pixels per HUD pixel
	const FVector2D Sub = Rig->GetSubPixelOffset();
	const FVector2D DrawSize(Target->SizeX * ImageScale, Target->SizeY * ImageScale);

	// Centre the image, then shift it against the camera's sub-pixel remainder
	// (camera right -> image left, camera up -> image down).
	FVector2D Position((Canvas->ClipX - DrawSize.X) * 0.5, (Canvas->ClipY - DrawSize.Y) * 0.5);
	Position.X -= Sub.X * ImageScale;
	Position.Y += Sub.Y * ImageScale;
	Position.X = FMath::RoundToDouble(Position.X);
	Position.Y = FMath::RoundToDouble(Position.Y);

	FCanvasTileItem Tile(Position, Target->GetResource(), DrawSize, FLinearColor::White);
	Tile.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Tile);

	// --- UI, drawn on the same pixel grid as the scene (all sizes are in low resolution pixels) ---
	auto DrawBox = [this, Scale](const FVector2D& TopLeft, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Box(TopLeft, Size * Scale, Color);
		Box.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Box);
	};
	auto DrawBar = [&DrawBox, Scale](const FVector2D& TopLeft, float Width, float Height, float Fraction, const FLinearColor& Fill)
	{
		DrawBox(TopLeft - FVector2D(Scale, Scale), FVector2D(Width + 2.f, Height + 2.f), FLinearColor(0.02f, 0.02f, 0.04f));
		DrawBox(TopLeft, FVector2D(Width, Height), FLinearColor(0.16f, 0.10f, 0.12f));
		const float Filled = FMath::RoundToFloat(Width * FMath::Clamp(Fraction, 0.f, 1.f));
		if (Filled > 0.f)
		{
			DrawBox(TopLeft, FVector2D(Filled, Height), Fill);
		}
	};

	// Enemy health bars above damaged enemies.
	for (TActorIterator<APixelCharacterBase> It(GetWorld()); It; ++It)
	{
		const APixelCharacterBase* Character = *It;
		if (Character->TeamId == Player->TeamId || Character->IsDead() || Character->GetHealth() >= Character->GetMaxHealth()
			|| FVector::DistSquared(Character->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(EnemyHealthBarRange))
		{
			continue;
		}
		FVector2D Pixel;
		if (!Rig->WorldToTargetPixel(Character->GetFeetLocation() + FVector(0.f, 0.f, 120.f), Pixel))
		{
			continue;
		}
		const FVector2D Screen = Position + FVector2D(FMath::RoundToDouble(Pixel.X), FMath::RoundToDouble(Pixel.Y)) * ImageScale - FVector2D(8.0 * Scale, 0.0);
		DrawBar(Screen, 16.f, 2.f, Character->GetHealth() / Character->GetMaxHealth(), FLinearColor(0.85f, 0.15f, 0.2f));
	}

	// A small red "!" over every enemy that is winding up or carrying out its attack.
	for (TActorIterator<APixelEnemyCharacter> It(GetWorld()); It; ++It)
	{
		FVector2D Pixel;
		if (It->IsDead() || !It->IsWarning() || !Rig->WorldToTargetPixel(It->GetFeetLocation() + FVector(0.f, 0.f, It->GetWarningHeight()), Pixel))
		{
			continue;
		}
		// Bottom centre of the mark; it is 2 wide and 8 tall, with a dark edge around it.
		const FVector2D Base = Position + FVector2D(FMath::RoundToDouble(Pixel.X), FMath::RoundToDouble(Pixel.Y)) * ImageScale;
		const FLinearColor Edge(0.10f, 0.0f, 0.0f);
		const FLinearColor Red(1.0f, 0.12f, 0.10f);
		DrawBox(Base + FVector2D(-2.0, -9.0) * Scale, FVector2D(4.f, 10.f), Edge);
		DrawBox(Base + FVector2D(-1.0, -8.0) * Scale, FVector2D(2.f, 5.f), Red);   // stroke
		DrawBox(Base + FVector2D(-1.0, -2.0) * Scale, FVector2D(2.f, 2.f), Red);   // dot
	}

	auto DrawLine = [this](const FVector2D& At, const FString& String, const UFont* Font, const FLinearColor& Color, bool bCentre)
	{
		FCanvasTextItem Item(At, FText::FromString(String), Font, Color);
		Item.bCentreX = bCentre;
		Item.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Item);
	};

	// Markers above things to use: a quest mark over the NPC, a small pip over nearby gathering nodes.
	for (TActorIterator<APixelInteractable> It(GetWorld()); It; ++It)
	{
		const APixelInteractable* Interactable = *It;
		FVector2D Pixel;
		if (!Interactable->CanInteract(Player)
			|| FVector::DistSquared2D(Interactable->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(1600.f)
			|| !Rig->WorldToTargetPixel(Interactable->GetActorLocation() + FVector(0.f, 0.f, Interactable->MarkerHeight), Pixel))
		{
			continue;
		}
		const FVector2D Screen = Position + FVector2D(FMath::RoundToDouble(Pixel.X), FMath::RoundToDouble(Pixel.Y)) * ImageScale;
		const FString Marker = Interactable->GetMarker(Player);
		if (!Marker.IsEmpty())
		{
			DrawLine(Screen - FVector2D(0.0, 22.0), Marker, GEngine->GetLargeFont(), FLinearColor(1.f, 0.85f, 0.2f), true);
		}
		else if (Interactable->IsA<APixelGatherable>())
		{
			const bool bFocused = Interactable == Player->GetFocus();
			DrawBox(Screen - FVector2D(Scale, Scale), FVector2D(2.f, 2.f), bFocused ? FLinearColor::White : FLinearColor(1.f, 0.85f, 0.2f));
		}
	}

	// Player health bar, weapon and kill count.
	const FVector2D Margin(8.0 * Scale, 8.0 * Scale);
	DrawBar(Margin, 72.f, 6.f, Player->GetHealth() / FMath::Max(Player->GetMaxHealth(), 1.f),
		Player->IsDead() ? FLinearColor(0.3f, 0.3f, 0.3f) : FLinearColor(0.25f, 0.8f, 0.3f));

	const APixelGameMode* GameMode = GetWorld()->GetAuthGameMode<APixelGameMode>();
	const FString Line = FString::Printf(TEXT("%s    처치 %d"), *PixelQuestCatalog::GetItemName(FName(*Player->GetWeapon().Name)), GameMode ? GameMode->GetKillCount() : 0);
	FCanvasTextItem Text(Margin + FVector2D(0.0, 10.0 * Scale), FText::FromString(Line), GEngine->GetMediumFont(), FLinearColor::White);
	Text.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(Text);

	// Companion: health, state and spell cooldowns.
	if (const APixelCompanionCharacter* Companion = Player->GetCompanion())
	{
		const FVector2D Row = Margin + FVector2D(0.0, 26.0 * Scale);
		DrawBar(Row, 48.f, 4.f, Companion->GetHealth() / FMath::Max(Companion->GetMaxHealth(), 1.f),
			Companion->IsDowned() ? FLinearColor(0.3f, 0.3f, 0.3f) : FLinearColor(0.3f, 0.75f, 0.9f));
		DrawBar(Row + FVector2D(54.0 * Scale, 0.0), 14.f, 4.f, 1.f - Companion->GetBoltCooldownFraction(), FLinearColor(0.6f, 0.95f, 1.f));
		DrawBar(Row + FVector2D(72.0 * Scale, 0.0), 14.f, 4.f, 1.f - Companion->GetEmpowerCooldownFraction(), FLinearColor(0.95f, 0.8f, 0.3f));

		const FString State = FString::Printf(TEXT("동료: %s   %s%s"), *Companion->GetStateText(),
			Companion->IsAutoCast() ? TEXT("자동 시전") : TEXT("수동 시전"),
			Player->GetAttackPower() > 1.01f ? TEXT("   강화 중") : TEXT(""));
		FCanvasTextItem CompanionText(Row + FVector2D(0.0, 7.0 * Scale), FText::FromString(State), GEngine->GetMediumFont(), FLinearColor::White);
		CompanionText.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(CompanionText);
	}

	// Bag contents, under the companion block.
	FString Bag;
	for (const TPair<FName, int32>& Item : Player->GetInventory()->GetItems())
	{
		Bag += FString::Printf(TEXT("%s%s %d"), Bag.IsEmpty() ? TEXT("") : TEXT("    "), *PixelQuestCatalog::GetItemName(Item.Key), Item.Value);
	}
	if (!Bag.IsEmpty())
	{
		DrawLine(Margin + FVector2D(0.0, 44.0 * Scale), Bag, GEngine->GetMediumFont(), FLinearColor(0.95f, 0.9f, 0.7f), false);
	}

	// Quest tracker, top right.
	TArray<FString> Tracker;
	Player->GetQuest()->GetTrackerLines(Tracker);
	for (int32 Index = 0; Index < Tracker.Num(); ++Index)
	{
		// Quest titles in gold, their objective lines (indented) in white.
		DrawLine(FVector2D(Canvas->ClipX - Margin.X - 300.0, Margin.Y + Index * 20.0), Tracker[Index], GEngine->GetMediumFont(),
			Tracker[Index].StartsWith(TEXT("  ")) ? FLinearColor::White : FLinearColor(1.f, 0.85f, 0.2f), false);
	}

	// Interaction prompt, dialogue and pop-up notice, bottom centre.
	const double CentreX = Canvas->ClipX * 0.5;
	FString Speaker, Speech, Notice;
	if (Player->GetDialogue(Speaker, Speech))
	{
		TArray<FString> Lines;
		Speech.ParseIntoArrayLines(Lines);
		const double Top = Canvas->ClipY * 0.74;
		const FVector2D BoxSize(620.0, 34.0 + Lines.Num() * 20.0);
		FCanvasTileItem Back(FVector2D(CentreX - BoxSize.X * 0.5, Top), BoxSize, FLinearColor(0.03f, 0.03f, 0.06f, 0.82f));
		Back.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Back);
		DrawLine(FVector2D(CentreX, Top + 6.0), Speaker, GEngine->GetMediumFont(), FLinearColor(1.f, 0.85f, 0.2f), true);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			DrawLine(FVector2D(CentreX, Top + 28.0 + Index * 20.0), Lines[Index], GEngine->GetMediumFont(), FLinearColor::White, true);
		}
	}
	else if (const APixelInteractable* Focus = Player->GetFocus())
	{
		DrawLine(FVector2D(CentreX, Canvas->ClipY * 0.78), FString::Printf(TEXT("[E]  %s"), *Focus->GetPrompt(Player)), GEngine->GetMediumFont(), FLinearColor::White, true);
	}
	if (Player->GetNotice(Notice))
	{
		DrawLine(FVector2D(CentreX, Canvas->ClipY * 0.22), Notice, GEngine->GetLargeFont(), FLinearColor(0.75f, 1.f, 0.75f), true);
	}
	if (GameMode && GetWorld()->GetTimeSeconds() - GameMode->GetLastSaveTime() < 1.5f)
	{
		DrawLine(FVector2D(Canvas->ClipX - Margin.X - 60.0, Canvas->ClipY - Margin.Y - 14.0), TEXT("저장됨"), GEngine->GetSmallFont(), FLinearColor(1.f, 1.f, 1.f, 0.8f), false);
	}

	FCanvasTextItem Help2(FVector2D(Margin.X, Canvas->ClipY - Margin.Y - 30.0),
		FText::FromString(TEXT("E 상호작용   G 태우기/내리기   1 따라오기   2 대기   3 집중   T 자동 시전   F 볼트   C 강화   F5 저장   F9 불러오기")),
		GEngine->GetSmallFont(), FLinearColor(1.f, 1.f, 1.f, 0.8f));
	Help2.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(Help2);

	FCanvasTextItem Help(FVector2D(Margin.X, Canvas->ClipY - Margin.Y - 14.0),
		FText::FromString(TEXT("WASD 이동   마우스 시점   좌클릭/J 공격   Space 구르기   Tab 무기 교체   Z/X 줌   [ / ] 마우스 감도   V 시점 전환")),
		GEngine->GetSmallFont(), FLinearColor(1.f, 1.f, 1.f, 0.8f));
	Help.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(Help);

	if (Player->IsDead())
	{
		FCanvasTextItem Dead(FVector2D(Canvas->ClipX * 0.5, Canvas->ClipY * 0.4), FText::FromString(TEXT("쓰러졌다")), GEngine->GetLargeFont(), FLinearColor(0.9f, 0.2f, 0.2f));
		Dead.bCentreX = true;
		Dead.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Dead);
	}
}
