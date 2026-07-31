// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DustSpeciesData.generated.h"

/**
 * How a species regards the player.
 *
 * DUST has no combat system and no bestiary of enemies. Animals have opinions
 * about you, and the opinions differ - that is the entire design. The most
 * memorable creature in the game is the one that decides you are not worth
 * looking up for.
 */
UENUM(BlueprintType)
enum class EDustDisposition : uint8
{
	/** Approaches, observes, follows at a distance. Will investigate camps. */
	Curious,

	/** Defends a home range. Warns first, escalates only if warnings are ignored. */
	Territorial,

	/** Registers the player and carries on with its day. */
	Indifferent,

	/** Flees at long range. Seeing one properly is an achievement in itself. */
	Skittish,

	/** Actively hunts smaller fauna. Not the player - but the player can watch. */
	Predatory
};

/** When a species is awake. Determines whether the player ever meets it. */
UENUM(BlueprintType)
enum class EDustActivityPattern : uint8
{
	/** Active in daylight. */
	Diurnal,

	/** Active at night. */
	Nocturnal,

	/** Active at dawn and dusk, sheltering through midday heat. */
	Crepuscular,

	/** No consistent pattern. */
	Cathemeral
};

/** A need the creature works to satisfy. Needs drive the utility AI. */
USTRUCT(BlueprintType)
struct DUSTFAUNA_API FDustNeedProfile
{
	GENERATED_BODY()

	/** How fast thirst accumulates, per in-game hour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Needs", meta = (ClampMin = "0.0"))
	float ThirstRate = 0.06f;

	/** How fast hunger accumulates, per in-game hour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Needs", meta = (ClampMin = "0.0"))
	float HungerRate = 0.04f;

	/** How fast fatigue accumulates while active, per in-game hour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Needs", meta = (ClampMin = "0.0"))
	float FatigueRate = 0.08f;

	/**
	 * How strongly heat drives shelter-seeking. High values produce creatures
	 * that vanish entirely at midday, which is both accurate and a good way to
	 * make the desert feel emptier when it is hardest to cross.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Needs", meta = (ClampMin = "0.0"))
	float HeatSensitivity = 1.0f;

	/** Maximum distance the creature will travel to satisfy a need, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Needs", meta = (ClampMin = "0.0", Units = "cm"))
	float ForagingRange = 180000.0f;
};

/**
 * A species of the desert.
 *
 * Design rule enforced in review: every species must be justifiable as an
 * evolutionary answer to a specific pressure of this environment. No dragons,
 * no dinosaurs, no generic monsters. If a designer cannot say what problem the
 * animal's body solves, it does not go in.
 */
UCLASS(BlueprintType)
class DUSTFAUNA_API UDustSpeciesData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Stable identifier used in saves and in the creature ledger. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName SpeciesId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	/**
	 * The evolutionary rationale. Required. This is the field that keeps the
	 * bestiary honest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FString EvolutionaryRationale;

	/** Pawn class spawned for a full-fidelity instance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftClassPtr<class APawn> PawnClass;

	// -----------------------------------------------------------------------
	// Behaviour
	// -----------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	EDustDisposition Disposition = EDustDisposition::Indifferent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	EDustActivityPattern ActivityPattern = EDustActivityPattern::Crepuscular;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	FDustNeedProfile Needs;

	/**
	 * Distance at which the creature becomes aware of the player, in cm.
	 * Halved when the player is crouched, doubled when they are sprinting or
	 * riding, and scaled by wind direction - approaching downwind works.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0.0", Units = "cm"))
	float AwarenessRadius = 6000.0f;

	/**
	 * Distance at which the creature reacts, in cm. The gap between awareness
	 * and reaction is where all the interesting behaviour lives: a territorial
	 * animal that knows you are there and is deciding what to do about it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0.0", Units = "cm"))
	float ReactionRadius = 2200.0f;

	/** Radius of a territorial animal's defended home range, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0.0", Units = "cm"))
	float TerritoryRadius = 45000.0f;

	// -----------------------------------------------------------------------
	// Movement
	// -----------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float WalkSpeed = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float RunSpeed = 720.0f;

	/** Slope in degrees this species can traverse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxTraversableSlope = 38.0f;

	/** Surfaces this species will walk on. Empty means any land surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	TArray<EDustSurface> PreferredSurfaces;

	// -----------------------------------------------------------------------
	// Population
	// -----------------------------------------------------------------------

	/** Individuals per square kilometre in a region where this species lives. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "0.0"))
	float DensityPerKm2 = 0.4f;

	/** Typical group size. 1 is solitary. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "1"))
	int32 TypicalGroupSize = 1;

	/** Variation in group size, plus or minus. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "0"))
	int32 GroupSizeVariance = 0;

	/**
	 * If true, this species migrates seasonally between regions rather than
	 * holding a fixed range. Migrations are one of the strongest "the world
	 * exists without me" signals the game has.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population")
	bool bMigratory = false;

	/** Region ids this species migrates between, in order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (EditCondition = "bMigratory"))
	TArray<FName> MigrationRoute;

	/** In-game days spent in each stop on the route. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Population", meta = (EditCondition = "bMigratory", ClampMin = "1.0"))
	float DaysPerMigrationStop = 6.0f;

	// -----------------------------------------------------------------------
	// Discovery
	// -----------------------------------------------------------------------

	/**
	 * Behaviours the player can witness, which unlock journal text when seen.
	 * This is how the game rewards patient observation: watching a creature
	 * long enough to see it do something teaches you about it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Discovery")
	TArray<FName> ObservableBehaviours;

	/** How rare a sighting is, 0..1. Used to weight the discovery moment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Discovery", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SightingRarity = 0.3f;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
