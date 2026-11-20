#include "PixelCameraRig.h"

#include "Combat/PixelCharacterBase.h"
#include "EngineUtils.h"
#include "World/PixelInteractable.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/DirectionalLight.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

APixelCameraRig::APixelCameraRig()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics; // after the target has moved this frame

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(RootComponent);
	SceneCapture->SetUsingAbsoluteLocation(true);
	SceneCapture->SetUsingAbsoluteRotation(true);
	SceneCapture->ProjectionType = ECameraProjectionMode::Orthographic;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCapture->bCaptureEveryFrame = false; // captured manually in Tick: deferred captures do not run while world rendering is disabled
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->bAlwaysPersistRenderingState = true;
	SceneCapture->PostProcessBlendWeight = 1.f;

	// Everything that smears or re-filters pixels is switched off.
	SceneCapture->ShowFlags.SetAntiAliasing(false);
	SceneCapture->ShowFlags.SetTemporalAA(false);
	SceneCapture->ShowFlags.SetMotionBlur(false);
	SceneCapture->ShowFlags.SetBloom(false);
	SceneCapture->ShowFlags.SetEyeAdaptation(false);
	SceneCapture->ShowFlags.SetTonemapper(false); // keep the palette exactly as the post process outputs it

	FPostProcessSettings& PP = SceneCapture->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = AEM_Manual;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = 0.f;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = false;
	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = 0.f;
	PP.bOverride_MotionBlurAmount = true;
	PP.MotionBlurAmount = 0.f;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = 0.f;
	PP.bOverride_FilmGrainIntensity = true;
	PP.FilmGrainIntensity = 0.f;
	PP.bOverride_SceneFringeIntensity = true;
	PP.SceneFringeIntensity = 0.f;

	PostProcessMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_PixelPost.M_PixelPost")));
}

void APixelCameraRig::BeginPlay()
{
	Super::BeginPlay();

	if (UMaterialInterface* Material = PostProcessMaterial.LoadSynchronous())
	{
		PostMaterial = UMaterialInstanceDynamic::Create(Material, this);
		SceneCapture->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, PostMaterial));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PixelCameraRig: post process material %s not found. Run Tools/setup_pixel_assets.py."), *PostProcessMaterial.ToString());
	}

	// Props in front of the character are only cut away while playing: the switch is off in the asset,
	// so the editor viewport shows everything, and this turns it on for this world alone.
	if (UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Materials/MPC_Pixel.MPC_Pixel")))
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, Collection, TEXT("SeeThrough"), 1.f);
	}

	// The HUD draws the captured image; the normal full resolution scene render is not needed.
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->bDisableWorldRendering = true;
	}

	SetPixelEffectEnabled(true);
}

void APixelCameraRig::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->bDisableWorldRendering = false;
	}
	Super::EndPlay(EndPlayReason);
}

void APixelCameraRig::SetTarget(AActor* NewTarget)
{
	Target = NewTarget;
	if (Target)
	{
		AddTickPrerequisiteActor(Target);
	}
}

void APixelCameraRig::AddLookInput(float YawDelta, float PitchDelta)
{
	if (bThirdPerson)
	{
		TargetYaw += YawDelta;
		CurrentYaw = TargetYaw;
		ThirdPersonPitch = FMath::Clamp(ThirdPersonPitch + PitchDelta, MinPitch, MaxPitch);
	}
}

void APixelCameraRig::ToggleViewMode()
{
	bThirdPerson = !bThirdPerson;
	if (!bThirdPerson)
	{
		// Top down turns in fixed steps, so settle on the nearest one.
		TargetYaw = FMath::RoundToFloat(TargetYaw / YawStepDegrees) * YawStepDegrees;
	}
}

void APixelCameraRig::Shake(float Pixels)
{
	ShakePixels = FMath::Max(ShakePixels, Pixels);
	ShakeTimeLeft = 0.18f;
}

void APixelCameraRig::SetPixelEffectEnabled(bool bEnabled)
{
	bPixelEffectEnabled = bEnabled;
	for (FWeightedBlendable& Blendable : SceneCapture->PostProcessSettings.WeightedBlendables.Array)
	{
		Blendable.Weight = bEnabled ? 1.f : 0.f;
	}
}

void APixelCameraRig::UpdateRenderTarget(const FIntPoint& ViewportSize)
{
	// Third person: the scene is full resolution and only characters are pixelated (by the post process).
	// Top down: the whole image is rendered small and scaled up.
	UiScale = FMath::Max(1, FMath::RoundToInt32(static_cast<float>(ViewportSize.Y) / FMath::Max(1, TargetPixelHeight)));
	PixelScale = bPixelEffectEnabled && !bThirdPerson ? UiScale : 1;

	// One extra pixel on every side leaves room for the sub-pixel shift.
	const int32 Width = FMath::DivideAndRoundUp(ViewportSize.X, PixelScale) + 2;
	const int32 Height = FMath::DivideAndRoundUp(ViewportSize.Y, PixelScale) + 2;

	if (!RenderTarget || RenderTarget->SizeX != Width || RenderTarget->SizeY != Height)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this);
		RenderTarget->RenderTargetFormat = RTF_RGBA8_SRGB;
		RenderTarget->Filter = TF_Nearest;
		RenderTarget->AddressX = TA_Clamp;
		RenderTarget->AddressY = TA_Clamp;
		RenderTarget->InitAutoFormat(Width, Height);
		RenderTarget->UpdateResourceImmediate(true);
		SceneCapture->TextureTarget = RenderTarget;
	}
}

bool APixelCameraRig::WorldToTargetPixel(const FVector& WorldLocation, FVector2D& OutPixel) const
{
	if (!RenderTarget)
	{
		return false;
	}
	const FVector Relative = WorldLocation - CaptureLocation;
	double X = FVector::DotProduct(Relative, ViewRight);
	double Y = FVector::DotProduct(Relative, ViewUp);
	if (bThirdPerson)
	{
		// Perspective divide. Pixels are square, so the horizontal field of view scales both axes.
		const double Depth = FVector::DotProduct(Relative, ViewForward);
		if (Depth < 10.0)
		{
			return false;
		}
		const double PixelsPerUnit = RenderTarget->SizeX * 0.5 / (Depth * TanHalfFov);
		X *= PixelsPerUnit;
		Y *= PixelsPerUnit;
	}
	else
	{
		X /= WorldUnitsPerPixel;
		Y /= WorldUnitsPerPixel;
	}
	OutPixel = FVector2D(RenderTarget->SizeX * 0.5 + X, RenderTarget->SizeY * 0.5 - Y);
	return true;
}

void APixelCameraRig::UpdateAutomation(float DeltaSeconds)
{
	// Automated check: "-PixelShot=<seconds>" saves Saved/PixelShot.png after that time and quits.
	static float ShotDelay = -1.f;
	static bool bParsed = false;
	static bool bShotTaken = false;
	static float Elapsed = 0.f;
	if (!bParsed)
	{
		bParsed = true;
		FParse::Value(FCommandLine::Get(), TEXT("PixelShot="), ShotDelay);
	}
	// "-PixelShotFrames=<n>" also saves the next n captured frames as Saved/FlickerFrames/Frame_NN.png, to check
	// frame-to-frame flicker of the pixel effect.
	static int32 FramesWanted = 0;
	static int32 FramesSaved = 0;
	if (ShotDelay > 0.f && FramesWanted == 0)
	{
		FParse::Value(FCommandLine::Get(), TEXT("PixelShotFrames="), FramesWanted);
		FramesWanted = FMath::Max(FramesWanted, -1);
	}
	// "-PixelOrbit=<degrees per second>": turns the camera on its own from half a second before the shot, to check
	// how much the characters change while the camera orbits (with -PixelShotFrames).
	static float OrbitSpeed = 0.f;
	if (ShotDelay > 0.f && OrbitSpeed == 0.f)
	{
		FParse::Value(FCommandLine::Get(), TEXT("PixelOrbit="), OrbitSpeed);
		OrbitSpeed = OrbitSpeed == 0.f ? 1e-6f : OrbitSpeed;
	}
	if (ShotDelay > 0.f && FMath::Abs(OrbitSpeed) > 1e-3f && Elapsed >= ShotDelay - 0.5f)
	{
		AddLookInput(OrbitSpeed * DeltaSeconds, 0.f);
	}
	if (ShotDelay > 0.f)
	{
		Elapsed += DeltaSeconds;
		if (bShotTaken && FramesSaved < FramesWanted)
		{
			UKismetRenderingLibrary::ExportRenderTarget(this, RenderTarget, FPaths::ProjectSavedDir() / TEXT("FlickerFrames"),
				FString::Printf(TEXT("Frame_%02d.png"), FramesSaved++));
		}
		if (!bShotTaken && Elapsed >= ShotDelay)
		{
			bShotTaken = true;
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("PixelShot.png"), false, false);
			UKismetRenderingLibrary::ExportRenderTarget(this, RenderTarget, FPaths::ProjectSavedDir(), TEXT("PixelRT.png"));
		}
		else if (bShotTaken && Elapsed >= ShotDelay + 2.f)
		{
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
	}
}

void APixelCameraRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FIntPoint ViewportSize(1920, 1080);
	if (const UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		FVector2D Size;
		Viewport->GetViewportSize(Size);
		if (Size.X >= 1.0 && Size.Y >= 1.0)
		{
			ViewportSize = FIntPoint(FMath::RoundToInt32(Size.X), FMath::RoundToInt32(Size.Y));
		}
	}
	UpdateRenderTarget(ViewportSize);
	UpdateAutomation(DeltaSeconds);

	CurrentYaw = FMath::FInterpTo(CurrentYaw, TargetYaw, DeltaSeconds, YawInterpSpeed);
	if (FMath::IsNearlyEqual(CurrentYaw, TargetYaw, 0.01f))
	{
		CurrentYaw = TargetYaw;
	}

	const FRotator CameraRotation(bThirdPerson ? ThirdPersonPitch : PitchDegrees, CurrentYaw, 0.f);
	const FRotationMatrix Basis(CameraRotation);
	ViewForward = Basis.GetUnitAxis(EAxis::X);
	ViewRight = Basis.GetUnitAxis(EAxis::Y);
	ViewUp = Basis.GetUnitAxis(EAxis::Z);

	// The ground under the target's origin.
	FVector Feet = Target ? Target->GetActorLocation() : GetActorLocation();
	if (Target)
	{
		Feet.Z -= Target->GetSimpleCollisionHalfHeight();
	}

	FVector CameraLocation;
	SubPixelOffset = FVector2D::ZeroVector;
	// Size of one pixel in the world: constant for the orthographic view, proportional to depth for perspective.
	float PixelSizeOrtho = 0.f;
	float PixelSizePerDepth = 0.f;

	if (bThirdPerson)
	{
		SceneCapture->ProjectionType = ECameraProjectionMode::Perspective;
		SceneCapture->FOVAngle = FieldOfView;
		TanHalfFov = FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5f));
		PixelSizePerDepth = 2.f * TanHalfFov / RenderTarget->SizeX;

		// Boom from a pivot above the target; pull the camera in when something is in the way.
		const FVector Pivot = Feet + FVector(0.f, 0.f, PivotHeight);
		CameraLocation = Pivot - ViewForward * (ArmLength * Zoom);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PixelCameraBoom), false, Target);
		FHitResult Hit;
		bool bBlocked = false;
		// Things tagged NoCameraBlock (tree trunks) are skipped: in a forest they would pull the camera in constantly.
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			bBlocked = GetWorld()->SweepSingleByChannel(Hit, Pivot, CameraLocation, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(12.f), Params);
			if (!bBlocked || !Hit.GetComponent() || !Hit.GetComponent()->ComponentHasTag(TEXT("NoCameraBlock")))
			{
				break;
			}
			Params.AddIgnoredComponent(Hit.GetComponent());
			bBlocked = false;
		}
		if (bBlocked)
		{
			// Stay on this side of the obstacle. As the boom gets short, lift the camera so it
			// looks over the character's head instead of into the back of it.
			const float FullLength = ArmLength * Zoom;
			const float Length = FMath::Max(Hit.Distance, MinArmLength);
			CameraLocation = Pivot - ViewForward * Length + FVector(0.f, 0.f, (1.f - Length / FullLength) * CloseLift);
			UE_LOG(LogTemp, Verbose, TEXT("PixelCameraRig: boom blocked by %s (%s) at %.0f, start penetrating %d"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Cast<UStaticMeshComponent>(Hit.GetComponent()) ? Cast<UStaticMeshComponent>(Hit.GetComponent())->GetStaticMesh() : nullptr), Hit.Distance, Hit.bStartPenetrating ? 1 : 0);
		}
	}
	else
	{
		SceneCapture->ProjectionType = ECameraProjectionMode::Orthographic;

		// One low resolution pixel always covers the same number of world units, whatever the window size.
		// With the effect off the image is full resolution, so a pixel covers proportionally less.
		const int32 EffectScale = FMath::Max(1, FMath::RoundToInt32(static_cast<float>(ViewportSize.Y) / FMath::Max(1, TargetPixelHeight)));
		WorldUnitsPerPixel = ViewHeightWorldUnits * Zoom / FMath::Max(1, TargetPixelHeight) / (bPixelEffectEnabled ? 1.0 : EffectScale);
		SceneCapture->OrthoWidth = WorldUnitsPerPixel * RenderTarget->SizeX;
		PixelSizeOrtho = WorldUnitsPerPixel;

		CameraLocation = Feet + FVector(0.f, 0.f, 60.f) - ViewForward * CameraDistance;

		if (bSnapToPixelGrid && bPixelEffectEnabled && CurrentYaw == TargetYaw)
		{
			// Express the position in camera space and round the two screen-plane axes to whole pixels.
			const double X = FVector::DotProduct(CameraLocation, ViewRight) / WorldUnitsPerPixel;
			const double Y = FVector::DotProduct(CameraLocation, ViewUp) / WorldUnitsPerPixel;
			const double SnappedX = FMath::RoundToDouble(X);
			const double SnappedY = FMath::RoundToDouble(Y);
			SubPixelOffset = FVector2D(X - SnappedX, Y - SnappedY);
			CameraLocation = ViewRight * (SnappedX * WorldUnitsPerPixel) + ViewUp * (SnappedY * WorldUnitsPerPixel)
				+ ViewForward * FVector::DotProduct(CameraLocation, ViewForward);
		}
	}

	if (PostMaterial)
	{
		PostMaterial->SetScalarParameterValue(TEXT("PixelSizeOrtho"), PixelSizeOrtho);
		PostMaterial->SetScalarParameterValue(TEXT("PixelSizePerDepth"), PixelSizePerDepth);
	}

	if (ShakeTimeLeft > 0.f)
	{
		// Whole-pixel offsets keep the shake crisp.
		ShakeTimeLeft -= DeltaSeconds;
		const int32 Range = FMath::Max(1, FMath::RoundToInt32(ShakePixels));
		const double ShakeUnit = bThirdPerson ? PixelSizePerDepth * ArmLength * Zoom * UiScale : WorldUnitsPerPixel;
		CameraLocation += ViewRight * (FMath::RandRange(-Range, Range) * ShakeUnit)
			+ ViewUp * (FMath::RandRange(-Range, Range) * ShakeUnit);
		if (ShakeTimeLeft <= 0.f)
		{
			ShakePixels = 0.f;
		}
	}

	CaptureLocation = CameraLocation;
	SceneCapture->SetWorldLocationAndRotation(CameraLocation, CameraRotation);
	UpdateCharacterPixels();
	SceneCapture->CaptureScene();
}

FVector APixelCameraRig::GetCameraLocation() const
{
	return SceneCapture ? SceneCapture->GetComponentLocation() : GetActorLocation();
}

bool APixelCameraRig::GetViewLocation(const UWorld* World, FVector& OutLocation)
{
	for (TActorIterator<APixelCameraRig> It(World); It; ++It)
	{
		OutLocation = It->GetCameraLocation();
		return true;
	}
	return false;
}

void APixelCameraRig::UpdateCharacterPixels()
{
	// Hands the post process one grid per character: where it is anchored on screen and how big its
	// blocks are. The character's meshes are marked with a stencil value so the grid finds them.
	static const int32 MaxSlots = 16;
	if (!PostMaterial || !RenderTarget)
	{
		return;
	}

	// Camera-relative lighting (see CHARACTER_NORMAL_CODE in Tools/setup_pixel_assets.py): how far to turn the
	// characters' normals so the sun reaches them from CharacterLightYaw relative to the camera.
	if (!bLookedForSun)
	{
		bLookedForSun = true;
		for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		{
			Sun = *It;
			break;
		}
	}
	const float LightTurn = Sun.IsValid()
		? FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(CurrentYaw + CharacterLightYaw, Sun->GetActorForwardVector().Rotation().Yaw))
		: 0.f;

	int32 Slot = 0;
	auto Assign = [this, &Slot, LightTurn](AActor* Actor, FVector Feet)
	{
		if (Slot >= MaxSlots)
		{
			return;
		}
		// A rider shares the grid of whoever carries it, so the two never shear against each other.
		if (const APixelCharacterBase* Carrier = Cast<APixelCharacterBase>(Actor->GetAttachParentActor()))
		{
			Feet = Carrier->GetFeetLocation();
		}

		float Size = 0.f;
		FVector2D Pixel = FVector2D::ZeroVector;
		const double Depth = FVector::DotProduct(Feet - CaptureLocation, ViewForward);
		// Pixel snap (see FLATTEN_CODE in Tools/setup_pixel_assets.py): the model's vertices are snapped to this grid,
		// in world units, from the world point that lands exactly on the grid's (rounded) anchor pixel.
		float SnapSize = 0.f;
		FVector SnapOrigin = Feet;
		if (bThirdPerson && bPixelEffectEnabled && Depth > 10.0 && WorldToTargetPixel(Feet, Pixel))
		{
			const double WorldUnitsPerScreenPixel = Depth * 2.0 * TanHalfFov / RenderTarget->SizeX;
			Size = FMath::Clamp(FMath::RoundToFloat(CharacterTexelSize / WorldUnitsPerScreenPixel), 1.f, 64.f);
			SnapSize = Size * WorldUnitsPerScreenPixel;
			SnapOrigin = Feet + ViewRight * ((FMath::RoundToDouble(Pixel.X) - Pixel.X) * WorldUnitsPerScreenPixel)
				- ViewUp * ((FMath::RoundToDouble(Pixel.Y) - Pixel.Y) * WorldUnitsPerScreenPixel);
		}

		TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
		for (UPrimitiveComponent* Component : Components)
		{
			if (Component->bRenderCustomDepth)
			{
				Component->SetCustomDepthStencilValue(Slot + 1);
				for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
				{
					UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(Index));
					if (!Material && Component->GetMaterial(Index))
					{
						Material = Component->CreateDynamicMaterialInstance(Index);
					}
					if (Material)
					{
						Material->SetScalarParameterValue(TEXT("SnapSize"), SnapSize);
						Material->SetScalarParameterValue(TEXT("LightTurn"), LightTurn);
						Material->SetVectorParameterValue(TEXT("SnapOrigin"), FLinearColor(SnapOrigin.X, SnapOrigin.Y, SnapOrigin.Z, 0.f));
					}
				}
			}
		}
		// Whole pixels: every block of the character then covers exactly Size x Size screen pixels.
		PostMaterial->SetVectorParameterValue(*FString::Printf(TEXT("Char%d"), Slot),
			FLinearColor(FMath::RoundToFloat(Pixel.X), FMath::RoundToFloat(Pixel.Y), Size, 0.f));
		++Slot;
	};

	// There are more characters in the valley than grids. The player, its companion and the NPCs always get one,
	// the rest go to the characters nearest the camera that are in front of it. The others get stencil 0 (drawn at
	// full resolution, which only happens far away or off screen); before, a slot went to whoever came first, so an
	// NPC could lose its grid from one frame to the next and blink between pixelated and smooth.
	struct FCandidate
	{
		AActor* Actor;
		FVector Feet;
		double Priority;
	};
	TArray<FCandidate> Candidates;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	auto Rank = [&](AActor* Actor, const FVector& Feet, bool bAlways)
	{
		const FVector Relative = Feet - CaptureLocation;
		double Priority = Relative.Size();
		if (FVector::DotProduct(Relative, ViewForward) <= 10.0)
		{
			Priority += 1e7; // behind the camera
		}
		Candidates.Add({ Actor, Feet, bAlways ? Priority - 1e9 : Priority });
	};
	for (TActorIterator<APixelCharacterBase> It(GetWorld()); It; ++It)
	{
		Rank(*It, It->GetFeetLocation(), *It == PlayerPawn || It->TeamId == 0); // the player's team: player and companion
	}
	for (TActorIterator<APixelNpc> It(GetWorld()); It; ++It)
	{
		Rank(*It, It->GetActorLocation(), true);
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Priority < B.Priority; });

	for (const FCandidate& Candidate : Candidates)
	{
		if (Slot < MaxSlots)
		{
			Assign(Candidate.Actor, Candidate.Feet);
			continue;
		}
		TInlineComponentArray<UPrimitiveComponent*> Components(Candidate.Actor);
		for (UPrimitiveComponent* Component : Components)
		{
			if (Component->bRenderCustomDepth && Component->CustomDepthStencilValue != 0)
			{
				Component->SetCustomDepthStencilValue(0);
			}
		}
	}
	for (; Slot < MaxSlots; ++Slot)
	{
		PostMaterial->SetVectorParameterValue(*FString::Printf(TEXT("Char%d"), Slot), FLinearColor(0.f, 0.f, 0.f, 0.f));
	}
}
