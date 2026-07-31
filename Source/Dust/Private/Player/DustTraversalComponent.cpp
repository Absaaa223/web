// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Player/DustTraversalComponent.h"

#include "DustGameplayTags.h"
#include "DustLog.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/DustVitalsComponent.h"
#include "Weather/DustWeatherSubsystem.h"
#include "World/DustSandDeformation.h"
#include "World/DustWorldSubsystem.h"

const FName UDustTraversalComponent::EquipmentBoard     = TEXT("Board");
const FName UDustTraversalComponent::EquipmentGlider    = TEXT("Glider");
const FName UDustTraversalComponent::EquipmentRope      = TEXT("Rope");
const FName UDustTraversalComponent::EquipmentHoverbike = TEXT("Hoverbike");

namespace DustTraversalCVars
{
	static int32 DebugTraversal = 0;
	static FAutoConsoleVariableRef CVarDebugTraversal(
		TEXT("dust.debug.traversal"),
		DebugTraversal,
		TEXT("Draw the traversal probes and log state transitions."),
		ECVF_Cheat);

	static int32 UnlockAll = 0;
	static FAutoConsoleVariableRef CVarUnlockAll(
		TEXT("dust.traversal.UnlockAll"),
		UnlockAll,
		TEXT("Grant every piece of traversal equipment. For testing routes."),
		ECVF_Cheat);
}

UDustTraversalComponent::UDustTraversalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Must run before the movement component consumes input, so that speed
	// changes and custom modes take effect on the same frame they are decided.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UDustTraversalComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningCharacter = Cast<ACharacter>(GetOwner());
	if (OwningCharacter)
	{
		Movement = OwningCharacter->GetCharacterMovement();
		Vitals = OwningCharacter->FindComponentByClass<UDustVitalsComponent>();
	}

	if (!Movement)
	{
		UE_LOG(LogDustTraversal, Error, TEXT("Traversal component requires a character with a movement component."));
		SetComponentTickEnabled(false);
		return;
	}

	SetMovementState(TAG_Dust_Move_Walk);
	UpdateMaxSpeed();
}

void UDustTraversalComponent::SetMovementState(const FGameplayTag& NewTag)
{
	if (MovementStateTag == NewTag)
	{
		return;
	}

	if (DustTraversalCVars::DebugTraversal != 0)
	{
		UE_LOG(LogDustTraversal, Log, TEXT("Movement state: %s -> %s"),
			*MovementStateTag.ToString(), *NewTag.ToString());
	}

	MovementStateTag = NewTag;
}

void UDustTraversalComponent::UnlockEquipment(FName EquipmentId)
{
	bool bAlreadyHad = false;
	UnlockedEquipment.Add(EquipmentId, &bAlreadyHad);

	if (!bAlreadyHad)
	{
		UE_LOG(LogDustTraversal, Log, TEXT("Equipment unlocked: %s"), *EquipmentId.ToString());
	}
}

bool UDustTraversalComponent::IsClimbing() const
{
	return Movement
		&& Movement->MovementMode == MOVE_Custom
		&& Movement->CustomMovementMode == static_cast<uint8>(EDustCustomMove::Climb);
}

bool UDustTraversalComponent::IsGliding() const
{
	return Movement
		&& Movement->MovementMode == MOVE_Custom
		&& Movement->CustomMovementMode == static_cast<uint8>(EDustCustomMove::Glide);
}

// ---------------------------------------------------------------------------
// Probing
// ---------------------------------------------------------------------------

void UDustTraversalComponent::UpdateProbe()
{
	DUST_SCOPED_TIMER(Dust_Traversal_Probe);

	Probe = FDustTraversalProbe();

	const UWorld* World = GetWorld();
	if (!World || !OwningCharacter)
	{
		return;
	}

	const FVector Location = OwningCharacter->GetActorLocation();
	const FVector Forward = OwningCharacter->GetActorForwardVector();

	// Ground description comes from the sampler, not from a trace: it knows
	// about loose depth and surface type, which no collision query does.
	if (const UDustWorldSubsystem* WorldSubsystem = UDustWorldSubsystem::Get(this))
	{
		Probe.Ground = WorldSubsystem->SampleGround(Location);
	}

	const float CapsuleHalfHeight = OwningCharacter->GetSimpleCollisionHalfHeight();
	const FVector FeetLocation = Location - FVector(0, 0, CapsuleHalfHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(DustTraversalProbe), /*bTraceComplex=*/false, OwningCharacter);

	// --- Forward probe: is there a wall? ------------------------------------
	// Cast at chest height. A hit here means something is in the way; what we
	// do about it depends on how tall it is.
	const FVector ChestStart = FeetLocation + FVector(0, 0, CapsuleHalfHeight);
	const FVector ChestEnd = ChestStart + Forward * ProbeReach;

	FHitResult WallHit;
	const bool bWallHit = World->LineTraceSingleByChannel(
		WallHit, ChestStart, ChestEnd, ECC_Visibility, Params);

	if (!bWallHit)
	{
		return;
	}

	Probe.WallNormal = WallHit.ImpactNormal;

	// Angle of the surface from horizontal. Near 90 is a wall, near 0 is floor.
	const float SurfaceAngle = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(static_cast<float>(WallHit.ImpactNormal.Z), -1.0f, 1.0f)));

	// --- Downward probe from above: where is the top? -----------------------
	// Trace down from above the obstacle to find its lip. Starting above
	// MaxMantleHeight and coming down is far more reliable than probing upward,
	// because it finds the *top* surface rather than the first thing it touches.
	const FVector OverheadStart = FeetLocation
		+ Forward * (ProbeReach * 0.85f)
		+ FVector(0, 0, MaxMantleHeight + 60.0f);

	const FVector OverheadEnd = OverheadStart - FVector(0, 0, MaxMantleHeight + 120.0f);

	FHitResult LedgeHit;
	if (World->LineTraceSingleByChannel(LedgeHit, OverheadStart, OverheadEnd, ECC_Visibility, Params))
	{
		const float Height = static_cast<float>(LedgeHit.ImpactPoint.Z - FeetLocation.Z);

		// The lip must be roughly flat, or it is not somewhere to stand.
		const bool bFlatEnough = LedgeHit.ImpactNormal.Z > 0.7;

		if (bFlatEnough && Height > 20.0f && Height <= MaxMantleHeight)
		{
			// Headroom check: is there space for the player on top? Without
			// this the player mantles into an overhang and gets stuck, which is
			// the single most common traversal bug in games of this kind.
			const FVector HeadroomStart = LedgeHit.ImpactPoint + FVector(0, 0, 10.0f);
			const FVector HeadroomEnd = HeadroomStart + FVector(0, 0, CapsuleHalfHeight * 2.0f);

			FHitResult HeadroomHit;
			const bool bBlocked = World->LineTraceSingleByChannel(
				HeadroomHit, HeadroomStart, HeadroomEnd, ECC_Visibility, Params);

			if (!bBlocked)
			{
				Probe.bLedgeFound = true;
				Probe.LedgeLocation = LedgeHit.ImpactPoint;
				Probe.LedgeHeight = Height;
				Probe.bVaultable = Height <= MaxVaultHeight;
			}
		}
	}

	// --- Climbability -------------------------------------------------------
	// A wall is climbable if it is steep, solid, and not loose sand. Sand is
	// explicitly excluded: the whole point of a dune is that you cannot simply
	// go up it, and letting the player climb one would collapse the traversal
	// design of the entire dune sea.
	const bool bSteep = SurfaceAngle > 55.0f && SurfaceAngle < MaxClimbAngle;
	const bool bSolid = Probe.Ground.Surface == EDustSurface::Rock
		|| Probe.Ground.Surface == EDustSurface::Worked
		|| Probe.Ground.Surface == EDustSurface::Clay
		|| Probe.Ground.Surface == EDustSurface::Scoria;

	Probe.bClimbableWall = bSteep && bSolid;

	if (DustTraversalCVars::DebugTraversal != 0)
	{
		DrawDebugLine(World, ChestStart, ChestEnd, FColor::Cyan, false, -1.0f, 0, 1.5f);
		if (Probe.bLedgeFound)
		{
			DrawDebugSphere(World, Probe.LedgeLocation, 12.0f, 8,
				Probe.bVaultable ? FColor::Green : FColor::Yellow, false, -1.0f);
		}
	}
}

void UDustTraversalComponent::UpdateAvailableAction()
{
	AvailableAction = EDustTraversalAction::None;

	if (!Movement)
	{
		return;
	}

	// State-specific exits take priority: while climbing, the button lets go.
	if (IsClimbing())
	{
		AvailableAction = Probe.bLedgeFound ? EDustTraversalAction::Mantle : EDustTraversalAction::StopClimb;
		return;
	}

	if (IsGliding())
	{
		AvailableAction = EDustTraversalAction::StopGlide;
		return;
	}

	// On the ground: mantle and vault first, then climb.
	if (Movement->IsMovingOnGround())
	{
		if (Probe.bLedgeFound)
		{
			AvailableAction = Probe.bVaultable ? EDustTraversalAction::Vault : EDustTraversalAction::Mantle;
			return;
		}

		if (Probe.bClimbableWall)
		{
			AvailableAction = EDustTraversalAction::StartClimb;
			return;
		}

		if (Probe.Ground.IsSlideable() && !bCrouching)
		{
			AvailableAction = EDustTraversalAction::StartSlide;
			return;
		}
	}
	else if (Movement->IsFalling())
	{
		// Airborne: mantle onto a ledge we are passing, or deploy the glider.
		if (Probe.bLedgeFound)
		{
			AvailableAction = EDustTraversalAction::Mantle;
			return;
		}

		if (HasEquipment(EquipmentGlider) || DustTraversalCVars::UnlockAll != 0)
		{
			AvailableAction = EDustTraversalAction::StartGlide;
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------

bool UDustTraversalComponent::TryMantle()
{
	if (!Probe.bLedgeFound || !OwningCharacter || !Movement)
	{
		return false;
	}

	// Cost scales with height, so hauling yourself up something tall is a real
	// expenditure and chaining mantles up a cliff has a natural limit.
	const float Cost = FMath::GetMappedRangeValueClamped(
		FVector2f(0.0f, MaxMantleHeight), FVector2f(0.02f, 0.16f), Probe.LedgeHeight);

	if (Vitals && !Vitals->TrySpendStamina(Cost))
	{
		return false;
	}

	// Move the capsule to the ledge. This is deliberately instant rather than a
	// root-motion montage: a montage would take control away for half a second,
	// and the second design rule says we never do that. The camera smooths the
	// translation so it reads as a heave rather than a teleport.
	const float CapsuleHalfHeight = OwningCharacter->GetSimpleCollisionHalfHeight();
	const FVector Target = Probe.LedgeLocation
		+ FVector(0, 0, CapsuleHalfHeight + 2.0f)
		+ OwningCharacter->GetActorForwardVector() * 25.0f;

	OwningCharacter->SetActorLocation(Target, /*bSweep=*/false);

	if (IsClimbing())
	{
		StopClimb();
	}

	Movement->SetMovementMode(MOVE_Walking);

	// Carry a little forward momentum out of the mantle, so arriving on a ledge
	// flows into a walk instead of a dead stop.
	Movement->Velocity = OwningCharacter->GetActorForwardVector() * 180.0f;

	SetMovementState(TAG_Dust_Move_Mantle);
	return true;
}

bool UDustTraversalComponent::TryVault()
{
	// A vault is a cheap mantle over something low. Same mechanics, lower cost,
	// and it preserves more speed - vaulting a wall while sprinting should not
	// stop the sprint.
	if (!Probe.bVaultable || !OwningCharacter || !Movement)
	{
		return false;
	}

	if (Vitals && !Vitals->TrySpendStamina(0.015f))
	{
		return false;
	}

	const float CapsuleHalfHeight = OwningCharacter->GetSimpleCollisionHalfHeight();
	const FVector Target = Probe.LedgeLocation
		+ FVector(0, 0, CapsuleHalfHeight + 2.0f)
		+ OwningCharacter->GetActorForwardVector() * 45.0f;

	const FVector PreviousVelocity = Movement->Velocity;

	OwningCharacter->SetActorLocation(Target, /*bSweep=*/false);
	Movement->Velocity = PreviousVelocity;

	return true;
}

bool UDustTraversalComponent::TryStartClimb()
{
	if (!Probe.bClimbableWall || !Movement)
	{
		return false;
	}

	if (Vitals && (Vitals->IsExhausted() || !Vitals->TrySpendStamina(0.04f)))
	{
		return false;
	}

	ClimbSurfaceNormal = Probe.WallNormal;
	ClimbDuration = 0.0f;

	Movement->SetMovementMode(MOVE_Custom, static_cast<uint8>(EDustCustomMove::Climb));
	SetMovementState(TAG_Dust_Move_Climb);

	return true;
}

void UDustTraversalComponent::StopClimb()
{
	if (!Movement)
	{
		return;
	}

	Movement->SetMovementMode(MOVE_Falling);
	SetMovementState(TAG_Dust_Move_Walk);
	ClimbDuration = 0.0f;
}

bool UDustTraversalComponent::TryStartSlide()
{
	if (!Movement || !Probe.Ground.IsSlideable())
	{
		return false;
	}

	SlideSpeed = Movement->Velocity.Size();

	Movement->SetMovementMode(MOVE_Custom, static_cast<uint8>(EDustCustomMove::Slide));
	SetMovementState(TAG_Dust_Move_Slide);

	return true;
}

void UDustTraversalComponent::StopSlide()
{
	if (!Movement)
	{
		return;
	}

	Movement->SetMovementMode(MOVE_Walking);
	SetMovementState(TAG_Dust_Move_Walk);
	SlideSpeed = 0.0f;
}

void UDustTraversalComponent::StartGlide()
{
	if (!Movement || !Movement->IsFalling())
	{
		return;
	}

	if (!HasEquipment(EquipmentGlider) && DustTraversalCVars::UnlockAll == 0)
	{
		return;
	}

	GlideDuration = 0.0f;

	Movement->SetMovementMode(MOVE_Custom, static_cast<uint8>(EDustCustomMove::Glide));
	SetMovementState(TAG_Dust_Move_Glide);
}

void UDustTraversalComponent::StopGlide()
{
	if (!Movement)
	{
		return;
	}

	Movement->SetMovementMode(MOVE_Falling);
	SetMovementState(TAG_Dust_Move_Walk);
	GlideDuration = 0.0f;
}

void UDustTraversalComponent::TraverseAction()
{
	switch (AvailableAction)
	{
	case EDustTraversalAction::Mantle:      TryMantle();     break;
	case EDustTraversalAction::Vault:       TryVault();      break;
	case EDustTraversalAction::StartClimb:  TryStartClimb(); break;
	case EDustTraversalAction::StopClimb:   StopClimb();     break;
	case EDustTraversalAction::StartSlide:  TryStartSlide(); break;
	case EDustTraversalAction::StartGlide:  StartGlide();    break;
	case EDustTraversalAction::StopGlide:   StopGlide();     break;

	case EDustTraversalAction::None:
	default:
		// Nothing context-sensitive available, so the button is a jump.
		// Falling through to jump rather than doing nothing is the first
		// design rule in practice: the button always does *something*.
		if (OwningCharacter && Movement && Movement->IsMovingOnGround())
		{
			if (!Vitals || Vitals->TrySpendStamina(0.03f))
			{
				OwningCharacter->Jump();
			}
		}
		break;
	}
}

void UDustTraversalComponent::CancelTraversal()
{
	if (IsClimbing())
	{
		StopClimb();
	}
	else if (IsGliding())
	{
		StopGlide();
	}
	else if (Movement && Movement->MovementMode == MOVE_Custom)
	{
		Movement->SetMovementMode(MOVE_Walking);
		SetMovementState(TAG_Dust_Move_Walk);
	}
}

void UDustTraversalComponent::ToggleGlider()
{
	IsGliding() ? StopGlide() : StartGlide();
}

void UDustTraversalComponent::ToggleBoard()
{
	if (!Movement)
	{
		return;
	}

	if (!HasEquipment(EquipmentBoard) && DustTraversalCVars::UnlockAll == 0)
	{
		return;
	}

	const bool bOnBoard = Movement->MovementMode == MOVE_Custom
		&& Movement->CustomMovementMode == static_cast<uint8>(EDustCustomMove::Board);

	if (bOnBoard)
	{
		Movement->SetMovementMode(MOVE_Walking);
		SetMovementState(TAG_Dust_Move_Walk);
	}
	else if (Probe.Ground.LooseDepth > 20.0f)
	{
		Movement->SetMovementMode(MOVE_Custom, static_cast<uint8>(EDustCustomMove::Board));
		SetMovementState(TAG_Dust_Move_Board);
	}
}

void UDustTraversalComponent::SetSprinting(bool bWantsToSprint)
{
	// Exhaustion blocks starting a sprint but never interrupts walking.
	if (bWantsToSprint && Vitals && Vitals->IsExhausted())
	{
		bSprinting = false;
		return;
	}

	bSprinting = bWantsToSprint;

	if (bSprinting && bCrouching)
	{
		bCrouching = false;
	}
}

void UDustTraversalComponent::SetCrouching(bool bWantsToCrouch)
{
	bCrouching = bWantsToCrouch;

	if (bCrouching)
	{
		bSprinting = false;
	}

	if (OwningCharacter)
	{
		bCrouching ? OwningCharacter->Crouch() : OwningCharacter->UnCrouch();
	}
}

// ---------------------------------------------------------------------------
// Per-frame state
// ---------------------------------------------------------------------------

void UDustTraversalComponent::UpdateMaxSpeed()
{
	if (!Movement)
	{
		return;
	}

	float Speed = BaseWalkSpeed;

	if (bSprinting)
	{
		Speed *= SprintMultiplier;
		SetMovementState(TAG_Dust_Move_Sprint);
	}
	else if (bCrouching)
	{
		Speed *= CrouchMultiplier;
		SetMovementState(TAG_Dust_Move_Crouch);
	}
	else if (Movement->IsMovingOnGround())
	{
		SetMovementState(TAG_Dust_Move_Walk);
	}

	// --- Loose sand ---------------------------------------------------------
	// Deep sand is genuinely hard work. At full depth the player loses a third
	// of their speed, which is what makes crossing a dune field feel like an
	// expedition and makes the packed interdune corridors worth seeking out.
	const float SandDrag = FMath::GetMappedRangeValueClamped(
		FVector2f(0.0f, 140.0f), FVector2f(1.0f, 0.66f), Probe.Ground.LooseDepth);

	Speed *= SandDrag;

	// --- Uphill -------------------------------------------------------------
	// Climbing costs speed on any surface, and more on sand where each step
	// slips back.
	if (Probe.Ground.SlopeDegrees > 8.0f)
	{
		const float SlopePenalty = FMath::GetMappedRangeValueClamped(
			FVector2f(8.0f, 40.0f), FVector2f(1.0f, 0.55f), Probe.Ground.SlopeDegrees);

		Speed *= SlopePenalty;
	}

	// --- Wind ---------------------------------------------------------------
	// Walking into a sandstorm is hard. This is small enough not to feel like
	// the controls have been taken away, and large enough that the player will
	// choose to wait out a storm in shelter rather than push through it.
	if (const UDustWeatherSubsystem* Weather = UDustWeatherSubsystem::Get(this))
	{
		if (OwningCharacter)
		{
			const FDustWindSample Wind = Weather->SampleWind(OwningCharacter->GetActorLocation());
			const float HeadwindDot = static_cast<float>(FVector::DotProduct(
				OwningCharacter->GetActorForwardVector(), -Wind.Direction));

			if (HeadwindDot > 0.0f)
			{
				const float WindStrength = FMath::Clamp(Wind.Speed / 2400.0f, 0.0f, 1.0f);
				Speed *= FMath::Lerp(1.0f, 0.72f, HeadwindDot * WindStrength);
			}
		}
	}

	// --- Vitals -------------------------------------------------------------
	// Distress slows the player, but only mildly. The stamina ceiling is the
	// real limiter; this is the felt edge of it.
	if (Vitals)
	{
		Speed *= FMath::Lerp(1.0f, 0.82f, Vitals->GetDistress());
	}

	Movement->MaxWalkSpeed = Speed;
	Movement->MaxWalkSpeedCrouched = BaseWalkSpeed * CrouchMultiplier * SandDrag;
}

void UDustTraversalComponent::UpdateSurfaceMovement(float DeltaTime)
{
	if (!Movement || !OwningCharacter || !Movement->IsMovingOnGround())
	{
		return;
	}

	// Above the angle of repose, loose sand does not hold. The player slips
	// downhill whether they want to or not - which is what makes the top of a
	// dune a genuinely committing place to stand, and what turns the descent
	// into an invitation to slide rather than a walk.
	if (Probe.Ground.SlopeDegrees > SandSlipAngle && Probe.Ground.LooseDepth > 15.0f)
	{
		const float SlipStrength = FMath::GetMappedRangeValueClamped(
			FVector2f(SandSlipAngle, 40.0f), FVector2f(0.0f, 1.0f), Probe.Ground.SlopeDegrees);

		// Downhill direction is the horizontal component of the surface normal,
		// inverted.
		FVector Downhill = FVector(Probe.Ground.Normal.X, Probe.Ground.Normal.Y, 0.0).GetSafeNormal();

		if (!Downhill.IsNearlyZero())
		{
			Movement->AddForce(-Downhill * SlipStrength * 90000.0f);
		}
	}

	// Sprinting drains stamina continuously.
	if (bSprinting && Vitals && Movement->Velocity.SizeSquared() > 10000.0f)
	{
		Vitals->DrainStamina(SprintStaminaDrain, DeltaTime);

		if (Vitals->IsExhausted())
		{
			SetSprinting(false);
		}
	}
}

void UDustTraversalComponent::TickClimb(float DeltaTime)
{
	if (!IsClimbing() || !Movement || !OwningCharacter)
	{
		return;
	}

	ClimbDuration += DeltaTime;

	// Climbing drains stamina at an accelerating rate: hanging on a wall gets
	// progressively harder, so long climbs need ledges to rest on. This is the
	// mechanism that makes the mountain regions a route-finding problem rather
	// than a hold-forward problem.
	if (Vitals)
	{
		const float DrainRate = ClimbStaminaDrain * FMath::Lerp(1.0f, 2.2f,
			FMath::Clamp(ClimbDuration / 25.0f, 0.0f, 1.0f));

		Vitals->DrainStamina(DrainRate, DeltaTime);

		// Out of stamina means letting go. It is a fall, not a death - the
		// player lands, takes no damage, and has to find another way up.
		if (Vitals->IsExhausted())
		{
			UE_LOG(LogDustTraversal, Log, TEXT("Climb failed: out of stamina after %.1fs."), ClimbDuration);
			StopClimb();
			return;
		}
	}

	// Leaving the wall - the player climbed off the side of it - drops them.
	if (!Probe.bClimbableWall && !Probe.bLedgeFound)
	{
		StopClimb();
		return;
	}

	// Hold the character against the wall so they do not drift off it.
	const FVector ToWall = -ClimbSurfaceNormal;
	Movement->AddForce(ToWall * 40000.0f);
}

void UDustTraversalComponent::TickSlide(float DeltaTime)
{
	if (!Movement || !OwningCharacter)
	{
		return;
	}

	const bool bSliding = Movement->MovementMode == MOVE_Custom
		&& Movement->CustomMovementMode == static_cast<uint8>(EDustCustomMove::Slide);

	if (!bSliding)
	{
		return;
	}

	// Stop when the slope runs out. Sliding onto flat ground should carry the
	// player a little way and then set them back on their feet, not stop dead.
	if (Probe.Ground.SlopeDegrees < SandSlipAngle * 0.6f)
	{
		SlideSpeed = FMath::FInterpConstantTo(SlideSpeed, 0.0f, DeltaTime, 420.0f);

		if (SlideSpeed < 120.0f)
		{
			StopSlide();
			return;
		}
	}
	else
	{
		// Accelerate down the slope, with drag rising as speed does so there is
		// a terminal velocity rather than an ever-increasing one.
		const float Gravity = 980.0f;
		const float SlopeRad = FMath::DegreesToRadians(Probe.Ground.SlopeDegrees);

		const float Acceleration = Gravity * FMath::Sin(SlopeRad);
		const float Drag = FMath::Square(SlideSpeed) * 0.00035f;

		SlideSpeed = FMath::Clamp(SlideSpeed + (Acceleration - Drag) * DeltaTime, 0.0f, 2600.0f);
	}

	FVector Downhill = FVector(Probe.Ground.Normal.X, Probe.Ground.Normal.Y, 0.0).GetSafeNormal();
	if (Downhill.IsNearlyZero())
	{
		StopSlide();
		return;
	}

	// The player retains steering authority while sliding. Losing control on a
	// dune face would be realistic and miserable; carving across it is the
	// whole appeal.
	const FVector SteerInput = OwningCharacter->GetActorForwardVector();
	const FVector SlideDirection = (-Downhill * 0.78 + SteerInput * 0.22).GetSafeNormal();

	Movement->Velocity = SlideDirection * SlideSpeed;

	// Sliding churns an enormous amount of sand.
	if (UDustSandDeformationSubsystem* Sand = UDustSandDeformationSubsystem::Get(this))
	{
		FDustSandImpact Impact;
		Impact.Location = OwningCharacter->GetActorLocation()
			- FVector(0, 0, OwningCharacter->GetSimpleCollisionHalfHeight());
		Impact.Radius = 55.0f;
		Impact.Depth = 11.0f;
		Impact.Type = EDustImpactType::Slide;
		Impact.Direction = FVector2D(SlideDirection.X, SlideDirection.Y);

		Sand->AddImpact(Impact);
	}
}

void UDustTraversalComponent::TickGlide(float DeltaTime)
{
	if (!IsGliding() || !Movement || !OwningCharacter)
	{
		return;
	}

	GlideDuration += DeltaTime;

	// The canopy takes a moment to inflate, so deploying does not instantly
	// arrest a fall. This is both physically right and mechanically useful:
	// it makes the decision of *when* to deploy matter.
	const float CanopyOpen = FMath::Clamp(GlideDuration / 0.85f, 0.0f, 1.0f);

	// --- Aerodynamics -------------------------------------------------------
	// A simple but honest glide model: a fixed sink rate, forward speed derived
	// from pitch, and full authority in yaw. The glide ratio is about 4:1,
	// which is a plausible paraglider and, more to the point, is enough to make
	// launching from a mesa a genuinely useful way to cross a valley.
	constexpr float SinkRate = 220.0f;      // cm/s
	constexpr float ForwardSpeed = 900.0f;  // cm/s
	constexpr float GlideRatio = 4.0f;

	const FVector Forward = OwningCharacter->GetActorForwardVector();

	FVector Velocity = Forward * ForwardSpeed;
	Velocity.Z = -SinkRate;

	// --- Wind ---------------------------------------------------------------
	// This is where the wind field pays for itself. Thermals and ridge lift
	// mean the player can gain altitude by finding rising air over a windward
	// slope, which turns gliding from a descent into a skill.
	if (const UDustWeatherSubsystem* Weather = UDustWeatherSubsystem::Get(this))
	{
		const FDustWindSample Wind = Weather->SampleWind(OwningCharacter->GetActorLocation());

		// Horizontal drift: a tailwind extends the glide considerably.
		Velocity += Wind.Direction * Wind.Speed * 0.55f;

		// Ridge lift: air forced up a windward slope. We detect it the same way
		// the wind field does - by comparing terrain height upwind of here.
		if (const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this))
		{
			const FVector Location = OwningCharacter->GetActorLocation();
			const FVector UpwindProbe = Location - Wind.Direction * 6000.0;

			const double HereHeight = World->SampleHeight(Location);
			const double UpwindHeight = World->SampleHeight(UpwindProbe);

			const double Rise = HereHeight - UpwindHeight;

			if (Rise > 0.0)
			{
				// Lift falls off with height above the slope: the band of
				// rising air is only a few hundred metres deep.
				const double HeightAboveGround = Location.Z - HereHeight;
				const float LiftFalloff = 1.0f - FMath::Clamp(
					static_cast<float>(HeightAboveGround) / 30000.0f, 0.0f, 1.0f);

				const float Lift = static_cast<float>(Rise / 6000.0)
					* Wind.Speed * 0.4f * LiftFalloff;

				Velocity.Z += Lift;
			}
		}
	}

	// Blend from the pre-deployment fall into full glide as the canopy opens.
	Movement->Velocity = FMath::Lerp(Movement->Velocity, Velocity, CanopyOpen);

	// Landing.
	if (Movement->Velocity.Z < 0.0f)
	{
		const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this);
		if (World)
		{
			const FVector Location = OwningCharacter->GetActorLocation();
			const double GroundHeight = World->SampleHeight(Location);
			const float CapsuleHalfHeight = OwningCharacter->GetSimpleCollisionHalfHeight();

			if (Location.Z - CapsuleHalfHeight <= GroundHeight + 40.0)
			{
				StopGlide();
			}
		}
	}
}

void UDustTraversalComponent::UpdateFootfalls(float DeltaTime)
{
	if (!Movement || !OwningCharacter || !Movement->IsMovingOnGround())
	{
		DistanceSinceFootfall = 0.0f;
		return;
	}

	const float Speed = Movement->Velocity.Size2D();
	if (Speed < 20.0f)
	{
		return;
	}

	DistanceSinceFootfall += Speed * DeltaTime;

	// Stride lengthens with speed, so a sprint does not simply play footsteps
	// faster - it plays fewer, longer strides, which is what running sounds like.
	const float Stride = FootfallStride * FMath::Lerp(1.0f, 1.6f,
		FMath::Clamp(Speed / (BaseWalkSpeed * SprintMultiplier), 0.0f, 1.0f));

	if (DistanceSinceFootfall < Stride)
	{
		return;
	}

	DistanceSinceFootfall = 0.0f;
	bLeftFootNext = !bLeftFootNext;

	// Only soft surfaces take a print. Rock does not deform, and spending the
	// deformation budget on it would waste texels the sand needs.
	const bool bSoftSurface =
		Probe.Ground.Surface == EDustSurface::LooseSand ||
		Probe.Ground.Surface == EDustSurface::PackedSand ||
		Probe.Ground.Surface == EDustSurface::Salt ||
		Probe.Ground.Surface == EDustSurface::Clay ||
		Probe.Ground.Surface == EDustSurface::Verdant;

	if (!bSoftSurface || Probe.Ground.LooseDepth < 2.0f)
	{
		return;
	}

	if (UDustSandDeformationSubsystem* Sand = UDustSandDeformationSubsystem::Get(this))
	{
		const float CapsuleHalfHeight = OwningCharacter->GetSimpleCollisionHalfHeight();
		const FVector Forward = OwningCharacter->GetActorForwardVector();
		const FVector Right = OwningCharacter->GetActorRightVector();

		// Offset left and right so the trail reads as two lines of prints
		// rather than one central furrow.
		const FVector FootLocation = OwningCharacter->GetActorLocation()
			- FVector(0, 0, CapsuleHalfHeight)
			+ Right * (bLeftFootNext ? -13.0f : 13.0f);

		// Deeper prints in deeper sand, and deeper again when sprinting.
		const float Weight = FMath::Lerp(1.0f, 1.7f,
			FMath::Clamp(Speed / (BaseWalkSpeed * SprintMultiplier), 0.0f, 1.0f))
			* FMath::Clamp(Probe.Ground.LooseDepth / 60.0f, 0.35f, 1.6f);

		Sand->AddFootstep(FootLocation, Forward, 15.0f, Weight);
	}
}

void UDustTraversalComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	DUST_SCOPED_TIMER(Dust_Traversal_Tick);

	if (!Movement || !OwningCharacter)
	{
		return;
	}

	UpdateProbe();
	UpdateAvailableAction();
	UpdateMaxSpeed();
	UpdateSurfaceMovement(DeltaTime);

	TickClimb(DeltaTime);
	TickSlide(DeltaTime);
	TickGlide(DeltaTime);

	UpdateFootfalls(DeltaTime);

	// Auto-start a slide when the player runs onto a face too steep to walk.
	// Without this the player would stumble down every dune; with it, cresting
	// a dune at a run flows straight into a slide, which is one of the best
	// feelings in the game and something no player should have to press a
	// button to discover.
	if (Movement->IsMovingOnGround()
		&& MovementStateTag != TAG_Dust_Move_Slide
		&& Probe.Ground.SlopeDegrees > 34.0f
		&& Probe.Ground.LooseDepth > 25.0f
		&& Movement->Velocity.Size2D() > BaseWalkSpeed * 1.2f)
	{
		TryStartSlide();
	}
}
