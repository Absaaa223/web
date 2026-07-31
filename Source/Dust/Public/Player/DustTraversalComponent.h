// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DustTypes.h"
#include "DustTraversalComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class UDustVitalsComponent;

/**
 * Custom movement modes. Registered above MOVE_Custom so the character movement
 * component can drive them through its normal physics dispatch, which keeps
 * networking, root motion and the movement mode change events all working.
 */
UENUM(BlueprintType)
enum class EDustCustomMove : uint8
{
	/** Free-climbing a wall surface. */
	Climb   = 0,

	/** Sliding down a dune face on foot. */
	Slide   = 1,

	/** Paraglider deployed. */
	Glide   = 2,

	/** Riding the sand board. */
	Board   = 3,

	/** Attached to a deployed rope. */
	Rope    = 4,
};

/** A traversal move the player can currently perform. */
UENUM(BlueprintType)
enum class EDustTraversalAction : uint8
{
	None,
	Mantle,
	Vault,
	StartClimb,
	StopClimb,
	StartSlide,
	StartGlide,
	StopGlide,
	DeployBoard,
	StowBoard,
	GrabRope,
	ReleaseRope
};

/** What the traversal system found in front of the player this frame. */
USTRUCT(BlueprintType)
struct DUST_API FDustTraversalProbe
{
	GENERATED_BODY()

	/** True if there is a ledge within mantling reach. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Traversal")
	bool bLedgeFound = false;

	/** World location of the ledge lip. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Traversal")
	FVector LedgeLocation = FVector::ZeroVector;

	/** Height of the ledge above the player's feet, in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Traversal")
	float LedgeHeight = 0.0f;

	/** True if the surface ahead is climbable. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Traversal")
	bool bClimbableWall = false;

	/** Outward normal of the climbable wall. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Traversal")
	FVector WallNormal = FVector::ZeroVector;

	/** True if the obstacle is short enough to vault rather than mantle. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Traversal")
	bool bVaultable = false;

	/** Ground sample under the player, from the world sampler. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Traversal")
	FDustGroundSample Ground;
};

/**
 * Every way the player can move.
 *
 * ========================================================================
 * MOVEMENT IS THE VERB
 * ========================================================================
 * DUST has no combat, no dialogue and no inventory puzzles. Moving through the
 * world is essentially the entire interaction vocabulary, which means the feel
 * of movement is not polish - it is the game. Everything here is tuned to two
 * rules:
 *
 *   1. NEVER REFUSE SILENTLY. If the player presses jump at a ledge they
 *      *nearly* reached, they get the mantle. The probes are deliberately
 *      generous and the tolerances deliberately wide, because the frustration
 *      of a move that should have worked is far worse than the cost of one
 *      that arguably should not have.
 *
 *   2. NEVER TAKE CONTROL AWAY. There are no cinematic traversal animations
 *      that lock input. Mantles are fast and interruptible. The player can
 *      always let go, always bail out, always change their mind mid-move.
 *
 * ========================================================================
 * THE SAND IS A MOVEMENT SYSTEM
 * ========================================================================
 * Loose sand is not a texture, it is terrain with rules: it slows you going up,
 * it slides you going down, it swallows your footing above the angle of repose.
 * The traversal component reads the world sampler's loose depth and slope
 * directly, so climbing a dune is genuinely harder than climbing a rock of the
 * same gradient, and cresting one is a small physical achievement.
 */
UCLASS(ClassGroup = (Dust), meta = (BlueprintSpawnableComponent))
class DUST_API UDustTraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustTraversalComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	// -----------------------------------------------------------------------
	// Input entry points
	// -----------------------------------------------------------------------

	/** Sprint held. Ignored while exhausted. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Traversal")
	void SetSprinting(bool bWantsToSprint);

	/** Crouch toggled. Halves fauna detection radius and enables tight spaces. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Traversal")
	void SetCrouching(bool bWantsToCrouch);

	/**
	 * The context-sensitive traversal button.
	 *
	 * One button does everything: jump, mantle, vault, start a climb, grab a
	 * rope. The system decides from the probe what the player must have meant.
	 * This is a deliberate choice over separate bindings - in a game about
	 * looking at the horizon, the player should not be looking at their hands.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Traversal")
	void TraverseAction();

	/** Cancels the current traversal state and returns to walking. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Traversal")
	void CancelTraversal();

	/** Toggles the paraglider, if unlocked and airborne. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Traversal")
	void ToggleGlider();

	/** Toggles the sand board, if unlocked and on a slideable surface. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Traversal")
	void ToggleBoard();

	// -----------------------------------------------------------------------
	// State
	// -----------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Dust|Traversal")
	FGameplayTag GetMovementStateTag() const { return MovementStateTag; }

	UFUNCTION(BlueprintPure, Category = "Dust|Traversal")
	const FDustTraversalProbe& GetProbe() const { return Probe; }

	/** The action the traverse button would perform right now, for the HUD prompt. */
	UFUNCTION(BlueprintPure, Category = "Dust|Traversal")
	EDustTraversalAction GetAvailableAction() const { return AvailableAction; }

	UFUNCTION(BlueprintPure, Category = "Dust|Traversal")
	bool IsSprinting() const { return bSprinting; }

	UFUNCTION(BlueprintPure, Category = "Dust|Traversal")
	bool IsClimbing() const;

	UFUNCTION(BlueprintPure, Category = "Dust|Traversal")
	bool IsGliding() const;

	/** Unlocks a piece of traversal equipment. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Traversal")
	void UnlockEquipment(FName EquipmentId);

	UFUNCTION(BlueprintPure, Category = "Dust|Traversal")
	bool HasEquipment(FName EquipmentId) const { return UnlockedEquipment.Contains(EquipmentId); }

	/** Equipment ids. Kept as constants so content and code cannot disagree. */
	static const FName EquipmentBoard;
	static const FName EquipmentGlider;
	static const FName EquipmentRope;
	static const FName EquipmentHoverbike;

private:
	// --- Per-frame updates --------------------------------------------------

	/** Casts the traversal probes ahead of the player. */
	void UpdateProbe();

	/** Decides what the traverse button would do, for the prompt and for input. */
	void UpdateAvailableAction();

	/** Applies sand physics to walking: drag uphill, slip downhill. */
	void UpdateSurfaceMovement(float DeltaTime);

	/** Runs the climb state. */
	void TickClimb(float DeltaTime);

	/** Runs the slide state. */
	void TickSlide(float DeltaTime);

	/** Runs the glide state. */
	void TickGlide(float DeltaTime);

	/** Spawns footprints and surface effects. */
	void UpdateFootfalls(float DeltaTime);

	// --- Transitions --------------------------------------------------------

	bool TryMantle();
	bool TryVault();
	bool TryStartClimb();
	void StopClimb();
	bool TryStartSlide();
	void StopSlide();
	void StartGlide();
	void StopGlide();

	/** Sets the state tag and logs the transition. */
	void SetMovementState(const FGameplayTag& NewTag);

	/** Recomputes max walk speed from state, surface and vitals. */
	void UpdateMaxSpeed();

private:
	UPROPERTY(Transient)
	TObjectPtr<ACharacter> OwningCharacter;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> Movement;

	UPROPERTY(Transient)
	TObjectPtr<UDustVitalsComponent> Vitals;

	FDustTraversalProbe Probe;
	EDustTraversalAction AvailableAction = EDustTraversalAction::None;
	FGameplayTag MovementStateTag;

	bool bSprinting = false;
	bool bCrouching = false;

	/** Equipment the player has found. */
	TSet<FName> UnlockedEquipment;

	// --- Climb state --------------------------------------------------------

	/** Surface normal of the wall currently being climbed. */
	FVector ClimbSurfaceNormal = FVector::ZeroVector;

	/** Seconds spent on the current climb, for the stamina drain ramp. */
	float ClimbDuration = 0.0f;

	// --- Slide state --------------------------------------------------------

	/** Accumulated slide speed along the slope. */
	float SlideSpeed = 0.0f;

	// --- Glide state --------------------------------------------------------

	/** Seconds since the glider deployed, for the canopy-opening ramp. */
	float GlideDuration = 0.0f;

	// --- Footfalls ----------------------------------------------------------

	/** Distance travelled since the last footfall, in cm. */
	float DistanceSinceFootfall = 0.0f;

	/** Which foot is next; alternates so prints are offset left and right. */
	bool bLeftFootNext = false;

	// --- Tuning -------------------------------------------------------------

	/** Base walk speed on firm ground, cm/s. */
	static constexpr float BaseWalkSpeed = 360.0f;

	/** Sprint multiplier. */
	static constexpr float SprintMultiplier = 2.05f;

	/** Crouch multiplier. */
	static constexpr float CrouchMultiplier = 0.42f;

	/** Maximum ledge height that can be mantled, in cm. */
	static constexpr float MaxMantleHeight = 220.0f;

	/** Maximum obstacle height that is vaulted rather than mantled, in cm. */
	static constexpr float MaxVaultHeight = 95.0f;

	/** Forward reach of the traversal probe, in cm. */
	static constexpr float ProbeReach = 110.0f;

	/** Steepest wall that can be free-climbed, in degrees from horizontal. */
	static constexpr float MaxClimbAngle = 91.0f;

	/** Stamina drained per second while climbing. */
	static constexpr float ClimbStaminaDrain = 0.085f;

	/** Stamina drained per second while sprinting. */
	static constexpr float SprintStaminaDrain = 1.0f / 22.0f;

	/** Slope in degrees at which loose sand begins to give way underfoot. */
	static constexpr float SandSlipAngle = 28.0f;

	/** Distance between footfalls at walking pace, in cm. */
	static constexpr float FootfallStride = 95.0f;
};
