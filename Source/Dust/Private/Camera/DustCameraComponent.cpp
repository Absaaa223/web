// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Camera/DustCameraComponent.h"

#include "DustLog.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/DustVitalsComponent.h"
#include "Settings/DustGameUserSettings.h"
#include "World/DustWorldSubsystem.h"

namespace DustCameraCVars
{
	static float ComfortScale = 1.0f;
	static FAutoConsoleVariableRef CVarComfortScale(
		TEXT("dust.Camera.ComfortScale"),
		ComfortScale,
		TEXT("Master scale on every procedural camera motion. 0 makes the camera perfectly rigid."),
		ECVF_Default);

	static float HeadBobScale = 0.65f;
	static FAutoConsoleVariableRef CVarHeadBobScale(
		TEXT("dust.Camera.HeadBobScale"),
		HeadBobScale,
		TEXT("Head bob amplitude, 0..1."),
		ECVF_Default);
}

UDustCameraComponent::UDustCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	bUsePawnControlRotation = true;

	// Post-process is driven here rather than on a volume so that focus,
	// exposure and the distress grade all move together and can be frozen
	// coherently by photo mode.
	PostProcessBlendWeight = 1.0f;
}

void UDustCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Movement = Character->GetCharacterMovement();
		Vitals = Character->FindComponentByClass<UDustVitalsComponent>();
	}

	DampedRotation = GetComponentRotation();
	PreviousRotation = DampedRotation;
}

void UDustCameraComponent::SetCameraMode(EDustCameraMode NewMode)
{
	if (CameraMode == NewMode)
	{
		return;
	}

	CameraMode = NewMode;

	UE_LOG(LogDust, Log, TEXT("Camera mode: %d"), static_cast<int32>(NewMode));
}

void UDustCameraComponent::ToggleCameraMode()
{
	SetCameraMode(CameraMode == EDustCameraMode::FirstPerson
		? EDustCameraMode::ThirdPerson
		: EDustCameraMode::FirstPerson);
}

void UDustCameraComponent::AddCameraImpulse(const FVector& Impulse)
{
	ImpulseVelocity += Impulse * DustCameraCVars::ComfortScale;
}

void UDustCameraComponent::UpdateImpulse(float DeltaTime)
{
	// Critically-ish damped spring back to rest. A spring rather than a decay
	// curve because an impulse should overshoot slightly - a landing that
	// simply eases back to zero reads as soft.
	const FVector SpringForce = -ImpulseOffset * ImpulseStiffness;
	const FVector DampingForce = -ImpulseVelocity * ImpulseDamping;

	ImpulseVelocity += (SpringForce + DampingForce) * DeltaTime;
	ImpulseOffset += ImpulseVelocity * DeltaTime;

	// Clamp so a pathological impulse cannot throw the camera into the terrain.
	ImpulseOffset = ImpulseOffset.GetClampedToMaxSize(28.0);
}

FVector UDustCameraComponent::ComputeHeadBob(float DeltaTime)
{
	if (!Movement || !Movement->IsMovingOnGround())
	{
		// Airborne: no gait, and the phase resets so landing starts on a
		// footfall rather than mid-stride.
		GaitPhase = 0.0f;
		return FVector::ZeroVector;
	}

	const float Speed = Movement->Velocity.Size2D();
	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, Speed, DeltaTime, 6.0f);

	if (SmoothedSpeed < 15.0f)
	{
		return FVector::ZeroVector;
	}

	// Phase advances with *distance*, not time. This is the detail that makes
	// the bob feel attached to the ground: accelerating lengthens the stride
	// instead of speeding up an oscillation, and the visual footfall lands on
	// the same frame as the audio one.
	const float StrideLength = FMath::Lerp(95.0f, 152.0f,
		FMath::Clamp(SmoothedSpeed / 740.0f, 0.0f, 1.0f));

	GaitPhase = FMath::Frac(GaitPhase + (SmoothedSpeed * DeltaTime) / StrideLength);

	const float Amplitude = FMath::Lerp(0.6f, 1.0f,
		FMath::Clamp(SmoothedSpeed / 740.0f, 0.0f, 1.0f))
		* DustCameraCVars::HeadBobScale
		* DustCameraCVars::ComfortScale;

	const float Angle = GaitPhase * 2.0f * PI;

	// Vertical: two dips per stride, one per foot. Asymmetric - the fall is
	// faster than the rise, because that is what weight transferring onto a
	// leg actually does.
	const float RawVertical = FMath::Sin(Angle * 2.0f);
	const float Vertical = -FMath::Sign(RawVertical) * FMath::Pow(FMath::Abs(RawVertical), 0.7f) * 2.4f;

	// Lateral: one sway per stride, offset a quarter phase from the dip so the
	// weight shift leads the footfall.
	const float Lateral = FMath::Sin(Angle + HALF_PI) * 1.7f;

	// Forward: a small surge as each foot pushes off.
	const float Forward = FMath::Sin(Angle * 2.0f + 0.6f) * 0.7f;

	// Breathing, always present, much slower and much smaller. It is what stops
	// a stationary first-person camera looking like a tripod.
	const float BreathPhase = GetWorld() ? GetWorld()->GetTimeSeconds() * 0.55f : 0.0f;
	float Breath = FMath::Sin(BreathPhase) * 0.35f;

	// Distress deepens and quickens the breathing.
	if (Vitals)
	{
		const float Distress = Vitals->GetDistress();
		Breath = FMath::Sin(BreathPhase * FMath::Lerp(1.0f, 2.3f, Distress))
			* FMath::Lerp(0.35f, 1.4f, Distress);
	}

	return FVector(Forward, Lateral, Vertical + Breath) * Amplitude;
}

void UDustCameraComponent::ApplyRotationDamping(float DeltaTime, FMinimalViewInfo& DesiredView)
{
	if (DustCameraCVars::ComfortScale <= KINDA_SMALL_NUMBER)
	{
		DampedRotation = DesiredView.Rotation;
		return;
	}

	const FRotator Target = DesiredView.Rotation;

	// Angular velocity for the lead term.
	const FRotator Delta = (Target - PreviousRotation).GetNormalized();
	RotationVelocity = FMath::RInterpTo(RotationVelocity,
		FRotator(Delta.Pitch / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER),
		         Delta.Yaw   / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER),
		         0.0f),
		DeltaTime, 12.0f);

	PreviousRotation = Target;

	// Lead the target slightly in the direction of the turn, then damp toward
	// it. The lead is what makes a pan feel like a camera operator following
	// something rather than a mouse cursor.
	const FRotator Led = Target + RotationVelocity * (RotationLeadSeconds * DustCameraCVars::ComfortScale);

	// Frame-rate independent damping from a half-life.
	const float Alpha = 1.0f - FMath::Exp(-DeltaTime * (0.6931f / RotationDampingHalfLife));

	DampedRotation = FMath::Lerp(DampedRotation, Led, Alpha);

	DesiredView.Rotation = DampedRotation;
}

void UDustCameraComponent::UpdateFocus(float DeltaTime, const FMinimalViewInfo& View)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// What is the player looking at? We ask the terrain sampler rather than
	// tracing, because at these distances the geometry is not streamed in and
	// a trace would find nothing - and reporting "infinity" would throw the
	// depth of field wide open every time the player looked at the horizon.
	float TargetDistance = 12000.0f;

	if (const UDustWorldSubsystem* WorldSubsystem = UDustWorldSubsystem::Get(this))
	{
		const FVector Start = View.Location;
		const FVector Direction = View.Rotation.Vector();

		// March along the view ray until it meets the ground.
		constexpr int32 StepCount = 20;
		constexpr double MaxDistance = 900000.0; // 9 km

		double Found = MaxDistance;

		for (int32 Index = 1; Index <= StepCount; ++Index)
		{
			// Geometric stepping: fine detail near the player where focus
			// matters most, coarse at range where it barely does.
			const double T = MaxDistance * FMath::Pow(static_cast<double>(Index) / StepCount, 2.2);
			const FVector Point = Start + Direction * T;

			if (WorldSubsystem->SampleHeight(Point) > Point.Z)
			{
				Found = T;
				break;
			}
		}

		TargetDistance = static_cast<float>(Found);
	}

	// Focus pulls are slow and eased - a real focus puller does not snap.
	CurrentFocusDistance = FMath::FInterpTo(CurrentFocusDistance, TargetDistance, DeltaTime, 2.2f);
}

void UDustCameraComponent::ApplyThirdPersonBoom(float DeltaTime, FMinimalViewInfo& DesiredView)
{
	PerspectiveBlend = FMath::FInterpTo(PerspectiveBlend,
		(CameraMode == EDustCameraMode::ThirdPerson) ? 1.0f : 0.0f,
		DeltaTime, 4.5f);

	if (PerspectiveBlend <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	const FVector PivotLocation = DesiredView.Location;
	const FVector Backward = -DesiredView.Rotation.Vector();

	// Offset the boom to one side. A perfectly centred third-person camera puts
	// the character in the middle of every composition, which is the least
	// interesting place for them to be.
	const FVector RightVector = FRotationMatrix(DesiredView.Rotation).GetScaledAxis(EAxis::Y);
	const FVector DesiredLocation = PivotLocation
		+ Backward * ThirdPersonBoomLength
		+ RightVector * 55.0f
		+ FVector(0, 0, 22.0);

	// Pull in if something is in the way.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DustCameraBoom), false, Owner);

	FHitResult Hit;
	FVector FinalLocation = DesiredLocation;

	if (World->SweepSingleByChannel(Hit, PivotLocation, DesiredLocation, FQuat::Identity,
		ECC_Camera, FCollisionShape::MakeSphere(18.0f), Params))
	{
		FinalLocation = Hit.Location;
	}

	// Also respect the terrain height field, which catches the case of the
	// boom clipping into a dune that has no collision loaded yet.
	if (const UDustWorldSubsystem* WorldSubsystem = UDustWorldSubsystem::Get(this))
	{
		const double GroundHeight = WorldSubsystem->SampleHeight(FinalLocation);
		FinalLocation.Z = FMath::Max(FinalLocation.Z, GroundHeight + 45.0);
	}

	DesiredView.Location = FMath::Lerp(PivotLocation, FinalLocation, PerspectiveBlend);
}

void UDustCameraComponent::ApplyUserSettings(FMinimalViewInfo& DesiredView)
{
	const UDustGameUserSettings* Settings = UDustGameUserSettings::Get();
	if (!Settings)
	{
		return;
	}

	const float BaseFOV = (CameraMode == EDustCameraMode::ThirdPerson)
		? Settings->GetThirdPersonFOV()
		: Settings->GetFirstPersonFOV();

	// Ultrawide correction: hold the 16:9 vertical FOV and widen horizontally,
	// so a 32:9 player sees more desert rather than less sky.
	const float AspectRatio = (DesiredView.AspectRatio > KINDA_SMALL_NUMBER)
		? DesiredView.AspectRatio
		: 16.0f / 9.0f;

	DesiredView.FOV = Settings->GetAspectCorrectedFOV(BaseFOV, AspectRatio);

	// Derive focal length from FOV so depth of field behaves like a real lens.
	// A 95 degree horizontal FOV on full frame is about 18 mm, which gives the
	// deep focus and dramatic near-far relationship the game's compositions
	// are built around.
	const float HalfFOVRad = FMath::DegreesToRadians(DesiredView.FOV * 0.5f);
	const float FocalLengthMm = (SensorWidthMm * 0.5f) / FMath::Max(FMath::Tan(HalfFOVRad), KINDA_SMALL_NUMBER);

	DesiredView.PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
	DesiredView.PostProcessSettings.DepthOfFieldFocalDistance = CurrentFocusDistance;

	DesiredView.PostProcessSettings.bOverride_DepthOfFieldSensorWidth = true;
	DesiredView.PostProcessSettings.DepthOfFieldSensorWidth = SensorWidthMm;

	// A modest aperture: enough separation to read as a lens, not so much that
	// the landscape - the actual subject - goes soft.
	DesiredView.PostProcessSettings.bOverride_DepthOfFieldFstop = true;
	DesiredView.PostProcessSettings.DepthOfFieldFstop = 5.6f;

	// Motion blur, entirely under player control.
	DesiredView.PostProcessSettings.bOverride_MotionBlurAmount = true;
	DesiredView.PostProcessSettings.MotionBlurAmount = Settings->GetMotionBlurAmount();

	// Distress grade: desaturation and a warm push as the player suffers. It is
	// the only health bar the game has, and it is entirely diegetic.
	if (Vitals)
	{
		const float Distress = Vitals->GetDistress();

		if (Distress > 0.01f)
		{
			DesiredView.PostProcessSettings.bOverride_ColorSaturation = true;
			DesiredView.PostProcessSettings.ColorSaturation =
				FVector4(FVector(FMath::Lerp(1.0f, 0.38f, Distress)), 1.0f);

			DesiredView.PostProcessSettings.bOverride_ColorGain = true;
			DesiredView.PostProcessSettings.ColorGain = FVector4(
				1.0f,
				FMath::Lerp(1.0f, 0.94f, Distress),
				FMath::Lerp(1.0f, 0.86f, Distress),
				1.0f);

			// Vignette closes in as distress rises.
			DesiredView.PostProcessSettings.bOverride_VignetteIntensity = true;
			DesiredView.PostProcessSettings.VignetteIntensity = FMath::Lerp(0.35f, 0.85f, Distress);
		}
	}
}

void UDustCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
	DUST_SCOPED_TIMER(Dust_Camera_GetView);

	Super::GetCameraView(DeltaTime, DesiredView);

	UpdateImpulse(DeltaTime);

	// Procedural motion, all in view space so it follows where the player looks.
	const FVector Bob = ComputeHeadBob(DeltaTime);
	const FVector TotalOffset = Bob + ImpulseOffset;

	const FRotationMatrix ViewMatrix(DesiredView.Rotation);
	DesiredView.Location +=
		ViewMatrix.GetScaledAxis(EAxis::X) * TotalOffset.X +
		ViewMatrix.GetScaledAxis(EAxis::Y) * TotalOffset.Y +
		ViewMatrix.GetScaledAxis(EAxis::Z) * TotalOffset.Z;

	ApplyRotationDamping(DeltaTime, DesiredView);
	ApplyThirdPersonBoom(DeltaTime, DesiredView);
	UpdateFocus(DeltaTime, DesiredView);
	ApplyUserSettings(DesiredView);
}
