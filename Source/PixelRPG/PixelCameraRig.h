#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PixelCameraRig.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * Pixel-art camera that follows a target actor.
 *
 * The scene is captured into a low resolution render target and APixelHUD draws that target
 * with an integer scale and nearest filtering.
 *
 * - Third person (default): perspective camera on a boom behind the target, orbited with the mouse.
 * - Top down: orthographic camera snapped to the world-space pixel grid; the HUD shifts the image
 *   by the sub-pixel remainder of the snap so scrolling stays smooth.
 */
UCLASS()
class PIXELRPG_API APixelCameraRig : public AActor
{
	GENERATED_BODY()

public:
	APixelCameraRig();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	void SetTarget(AActor* NewTarget);
	void RotateView(int32 Steps) { TargetYaw += Steps * YawStepDegrees; }
	/** Mouse look for the third person camera (degrees). */
	void AddLookInput(float YawDelta, float PitchDelta);
	void ToggleViewMode();
	bool IsThirdPerson() const { return bThirdPerson; }
	void AddZoomInput(float Input, float DeltaSeconds) { Zoom = FMath::Clamp(Zoom * (1.f + Input * DeltaSeconds), 0.5f, 3.f); }
	void TogglePixelEffect() { SetPixelEffectEnabled(!bPixelEffectEnabled); }
	/** Shakes the view by up to this many low resolution pixels for a moment. */
	void Shake(float Pixels);

	float GetViewYaw() const { return CurrentYaw; }
	/** Where the scene is rendered from. */
	FVector GetCameraLocation() const;
	/** Camera location of the rig in this world. False before the rig exists. */
	static bool GetViewLocation(const UWorld* World, FVector& OutLocation);
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }
	int32 GetPixelScale() const { return PixelScale; }
	/** Size of one HUD pixel on screen: the HUD keeps its chunky look when the scene is full resolution. */
	int32 GetUiScale() const { return UiScale; }
	/** Camera position minus snapped camera position, in low resolution pixels (x right, y up). */
	FVector2D GetSubPixelOffset() const { return SubPixelOffset; }
	/** Position of a world point in render target pixels (origin top-left). False if it is behind the camera. */
	bool WorldToTargetPixel(const FVector& WorldLocation, FVector2D& OutPixel) const;

	UPROPERTY(EditAnywhere, Category = "Camera")
	bool bThirdPerson = true;

	UPROPERTY(EditAnywhere, Category = "Camera|Third Person")
	float ArmLength = 1400.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Third Person")
	float MinArmLength = 60.f;

	/** How far the camera rises when an obstacle pushes it right up to the character. */
	UPROPERTY(EditAnywhere, Category = "Camera|Third Person")
	float CloseLift = 110.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Third Person")
	float PivotHeight = 115.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Third Person")
	float FieldOfView = 55.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Third Person")
	float ThirdPersonPitch = -18.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Third Person")
	float MinPitch = -65.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Third Person")
	float MaxPitch = 12.f;

	/** Desired vertical resolution of the pixel-art image. The integer upscale factor is window height / this, rounded. */
	UPROPERTY(EditAnywhere, Category = "Pixel")
	int32 TargetPixelHeight = 360;

	/**
	 * Size in world units of one "pixel" of a character. In third person the world is rendered at
	 * full resolution and only characters are redrawn in screen-aligned blocks of this size, so a
	 * 3D character reads as a sprite of about 40 pixels that grows and shrinks with distance.
	 */
	UPROPERTY(EditAnywhere, Category = "Pixel")
	float CharacterTexelSize = 3.4f;

	/**
	 * Characters are lit as if the sun always came from this yaw relative to the camera (degrees; the direction
	 * the light travels, +40 = from behind the camera's left), so their shading stays put when the camera orbits.
	 */
	UPROPERTY(EditAnywhere, Category = "Pixel")
	float CharacterLightYaw = 40.f;

	/** World units covered by TargetPixelHeight pixels. Defines the size of one pixel in the world. */
	UPROPERTY(EditAnywhere, Category = "Pixel")
	float ViewHeightWorldUnits = 1080.f;

	UPROPERTY(EditAnywhere, Category = "Pixel")
	bool bSnapToPixelGrid = true;

	UPROPERTY(EditAnywhere, Category = "Pixel")
	TSoftObjectPtr<UMaterialInterface> PostProcessMaterial;

	UPROPERTY(EditAnywhere, Category = "Camera|Top Down")
	float PitchDegrees = -30.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Top Down")
	float CameraDistance = 2500.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float YawStepDegrees = 45.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float YawInterpSpeed = 8.f;

private:
	void SetPixelEffectEnabled(bool bEnabled);
	void UpdateRenderTarget(const FIntPoint& ViewportSize);
	void UpdateAutomation(float DeltaSeconds);

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Target;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PostMaterial;

	/** The level's sun (the first directional light), for the camera-relative character lighting. */
	TWeakObjectPtr<class ADirectionalLight> Sun;
	bool bLookedForSun = false;

	FVector ViewForward = FVector::ForwardVector;
	float TanHalfFov = 0.f;

	FVector CaptureLocation = FVector::ZeroVector;
	FVector ViewRight = FVector::RightVector;
	FVector ViewUp = FVector::UpVector;
	double WorldUnitsPerPixel = 4.0;
	FVector2D SubPixelOffset = FVector2D::ZeroVector;
	float TargetYaw = 45.f;
	float CurrentYaw = 45.f;
	float Zoom = 1.f;
	float ShakePixels = 0.f;
	float ShakeTimeLeft = 0.f;
	int32 PixelScale = 1;
	int32 UiScale = 1;

	void UpdateCharacterPixels();
	bool bPixelEffectEnabled = true;
};
