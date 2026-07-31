// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "DustTypes.h"
#include "DustCameraComponent.generated.h"

class UCharacterMovementComponent;
class UDustVitalsComponent;

/**
 * The player camera.
 *
 * ========================================================================
 * EVERY FRAME A WALLPAPER
 * ========================================================================
 * DUST is a game people will screenshot. The camera is therefore treated as a
 * cinematography problem rather than a viewport problem, and everything here
 * exists to make an arbitrary moment compose well:
 *
 *   - Focal length and sensor size rather than raw FOV, so depth of field and
 *     perspective compression relate to each other the way a real lens does.
 *   - Damped rotation with a velocity-dependent lead, so panning across a
 *     ridge line settles rather than snapping.
 *   - Procedural motion built from a gait model, not a sine wave, so walking
 *     has a footfall rhythm that matches the actual footstep audio.
 *   - Automatic focus pulled toward whatever the player is looking at, so the
 *     background separates without the player operating a focus control.
 *
 * ========================================================================
 * MOTION COMFORT IS NOT AN AFTERTHOUGHT
 * ========================================================================
 * Every procedural motion here is scaled by a single comfort factor the player
 * controls, and at zero the camera becomes perfectly rigid. Head bob, landing
 * dip, gait sway, breathing and the distress wobble all collapse together. A
 * player who needs that setting gets a game that is visually identical and
 * physically bearable, not a reduced version.
 */
UCLASS(ClassGroup = (Dust), meta = (BlueprintSpawnableComponent))
class DUST_API UDustCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:
	UDustCameraComponent();

	//~ Begin UCameraComponent
	virtual void BeginPlay() override;
	virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;
	//~ End UCameraComponent

	/** Switches perspective. The transition is blended, never cut. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Camera")
	void SetCameraMode(EDustCameraMode NewMode);

	UFUNCTION(BlueprintPure, Category = "Dust|Camera")
	EDustCameraMode GetCameraMode() const { return CameraMode; }

	UFUNCTION(BlueprintCallable, Category = "Dust|Camera")
	void ToggleCameraMode();

	/**
	 * Applies a one-off impulse to the camera: a landing, a gust catching the
	 * player, a nearby lightning strike. Decays on a spring.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Camera")
	void AddCameraImpulse(const FVector& Impulse);

	/** Distance the camera is currently focused at, in cm. */
	UFUNCTION(BlueprintPure, Category = "Dust|Camera")
	float GetFocusDistance() const { return CurrentFocusDistance; }

private:
	/** Reads user settings and applies FOV, comfort scale and motion blur. */
	void ApplyUserSettings(FMinimalViewInfo& DesiredView);

	/** Advances the gait model and produces the head bob offset. */
	FVector ComputeHeadBob(float DeltaTime);

	/** Damped rotation with lead, for cinematic panning. */
	void ApplyRotationDamping(float DeltaTime, FMinimalViewInfo& DesiredView);

	/** Autofocus: traces along the view to find what the player is looking at. */
	void UpdateFocus(float DeltaTime, const FMinimalViewInfo& View);

	/** Third-person boom placement with terrain-aware collision. */
	void ApplyThirdPersonBoom(float DeltaTime, FMinimalViewInfo& DesiredView);

	/** Springs the impulse offset back to rest. */
	void UpdateImpulse(float DeltaTime);

private:
	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> Movement;

	UPROPERTY(Transient)
	TObjectPtr<UDustVitalsComponent> Vitals;

	EDustCameraMode CameraMode = EDustCameraMode::FirstPerson;

	/** Blend between first and third person, 0..1. */
	float PerspectiveBlend = 0.0f;

	// --- Gait model ---------------------------------------------------------

	/**
	 * Phase through the current stride, 0..1. Advances with distance travelled
	 * rather than with time, so the bob stays locked to the footfalls even as
	 * the player accelerates.
	 */
	float GaitPhase = 0.0f;

	/** Smoothed speed, so the bob amplitude does not flicker. */
	float SmoothedSpeed = 0.0f;

	// --- Damping ------------------------------------------------------------

	FRotator PreviousRotation = FRotator::ZeroRotator;
	FRotator DampedRotation = FRotator::ZeroRotator;

	/** Angular velocity, used to lead the camera into a turn. */
	FRotator RotationVelocity = FRotator::ZeroRotator;

	// --- Impulse spring -----------------------------------------------------

	FVector ImpulseOffset = FVector::ZeroVector;
	FVector ImpulseVelocity = FVector::ZeroVector;

	// --- Focus --------------------------------------------------------------

	float CurrentFocusDistance = 10000.0f;

	// --- Tuning -------------------------------------------------------------

	/** Sensor width in mm; 36 is full frame. Drives the FOV/focal relationship. */
	static constexpr float SensorWidthMm = 36.0f;

	/** Boom length in third person, in cm. */
	static constexpr float ThirdPersonBoomLength = 340.0f;

	/** How far the camera leads a turn, in seconds of angular velocity. */
	static constexpr float RotationLeadSeconds = 0.06f;

	/** Rotation damping half-life in seconds. Low is snappy, high is filmic. */
	static constexpr float RotationDampingHalfLife = 0.045f;

	/** Impulse spring constants. */
	static constexpr float ImpulseStiffness = 140.0f;
	static constexpr float ImpulseDamping = 14.0f;
};
