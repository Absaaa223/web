// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DustTypes.h"
#include "Fauna/DustSpeciesData.h"
#include "DustCreatureBrain.generated.h"

class UDustSpeciesData;

/** What a creature is doing right now. */
UENUM(BlueprintType)
enum class EDustCreatureAction : uint8
{
	/** Standing still, doing nothing in particular. */
	Idle,

	/** Moving without a specific goal, within the home range. */
	Wander,

	/** Heading to water. */
	SeekWater,

	/** Heading to a food source. */
	Forage,

	/** Heading to shade or a burrow. */
	SeekShelter,

	/** Asleep. */
	Rest,

	/** Moving toward the player to look at them. */
	Investigate,

	/** Standing tall, facing the player, making itself large. A warning. */
	Threaten,

	/** Running away. */
	Flee,

	/** Following a migration route. */
	Migrate,

	/** Stalking prey. */
	Hunt,

	Count UMETA(Hidden)
};

/**
 * Utility-driven creature AI.
 *
 * ------------------------------------------------------------------------
 * WHY UTILITY AI AND NOT A BEHAVIOUR TREE
 * ------------------------------------------------------------------------
 * A behaviour tree encodes a designer's decision about priority. That is
 * exactly right for a guard on patrol and exactly wrong for an animal, because
 * the whole point of an animal is that its priorities shift continuously with
 * its internal state and its surroundings.
 *
 * Here, every candidate action scores itself from the creature's current needs
 * and context, and the highest score wins. A thirsty animal walks to water. A
 * thirsty animal that has just noticed a predator runs first and drinks later.
 * A thirsty, exhausted animal at midday in 44 degree heat goes and lies in the
 * shade and deals with the thirst at dusk. Nobody wrote any of those three
 * behaviours; they fall out of the scoring.
 *
 * The result reads as "this animal has its own agenda", which is the single
 * most important thing fauna can communicate in a game with no dialogue.
 *
 * ------------------------------------------------------------------------
 * HYSTERESIS
 * ------------------------------------------------------------------------
 * Raw utility selection oscillates: two actions with nearly equal scores swap
 * every evaluation and the creature twitches. The current action gets a
 * commitment bonus that decays over time, so a creature sticks with what it is
 * doing long enough to look deliberate, but can still be interrupted by
 * something genuinely more urgent.
 */
UCLASS(ClassGroup = (Dust), meta = (BlueprintSpawnableComponent))
class DUSTFAUNA_API UDustCreatureBrain : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustCreatureBrain();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/** Assigns the species this creature belongs to. Must be set before BeginPlay. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Fauna")
	void SetSpecies(UDustSpeciesData* InSpecies);

	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	UDustSpeciesData* GetSpecies() const { return Species; }

	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	EDustCreatureAction GetCurrentAction() const { return CurrentAction; }

	/** The location the creature is currently moving toward. */
	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	FVector GetMoveTarget() const { return MoveTarget; }

	/** Speed the creature wants to move at right now, in cm/s. */
	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	float GetDesiredSpeed() const;

	/** Home range centre. Creatures return here when nothing else is pressing. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Fauna")
	void SetHomeLocation(const FVector& InHome) { HomeLocation = InHome; }

	// --- Needs, exposed for the debug overlay and for journal text ----------

	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	float GetThirst() const { return Thirst; }

	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	float GetHunger() const { return Hunger; }

	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	float GetFatigue() const { return Fatigue; }

	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	float GetAlarm() const { return Alarm; }

	/**
	 * Level of detail. Distant creatures still think, but far less often and
	 * without any perception queries - they simply continue satisfying needs.
	 * This is what lets a few hundred animals exist without a frame cost.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Fauna")
	void SetSimulationLOD(int32 InLOD);

	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	int32 GetSimulationLOD() const { return SimulationLOD; }

	/** Broadcast when the creature switches action, for animation and audio. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FDustActionChanged, EDustCreatureAction /*New*/, EDustCreatureAction /*Old*/);
	FDustActionChanged OnActionChanged;

private:
	/** Advances thirst, hunger, fatigue and alarm. */
	void UpdateNeeds(float DeltaHours);

	/** Updates awareness of the player and of threats. */
	void UpdatePerception();

	/** Scores every action and commits to the best. */
	void SelectAction();

	/** Computes the destination for the committed action. */
	void UpdateMoveTarget();

	/** Utility score for one action, 0..1. */
	float ScoreAction(EDustCreatureAction Action) const;

	/** Finds the nearest water surface within the foraging range. */
	bool FindWater(FVector& OutLocation) const;

	/** Finds shade: a north-facing slope, an overhang, or a canyon floor. */
	bool FindShelter(FVector& OutLocation) const;

	/** Distance to the player, or a large number if there is no player. */
	double GetPlayerDistance() const;

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Fauna", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDustSpeciesData> Species;

	// --- Internal state -----------------------------------------------------

	/** 0 = satisfied, 1 = desperate. */
	float Thirst = 0.15f;
	float Hunger = 0.2f;
	float Fatigue = 0.1f;

	/**
	 * How alarmed the creature is, 0..1. Rises with proximity and with sudden
	 * player movement; decays slowly. The decay is why an animal you spooked
	 * stays wary for a while rather than instantly forgetting.
	 */
	float Alarm = 0.0f;

	/** True if the creature currently knows where the player is. */
	bool bAwareOfPlayer = false;

	/** Last known player location, which may be stale. */
	FVector LastKnownPlayerLocation = FVector::ZeroVector;

	EDustCreatureAction CurrentAction = EDustCreatureAction::Idle;

	/** Seconds spent in the current action. Feeds the commitment bonus. */
	float TimeInAction = 0.0f;

	FVector MoveTarget = FVector::ZeroVector;
	FVector HomeLocation = FVector::ZeroVector;

	/** 0 = full fidelity, 1 = reduced, 2 = distant bookkeeping only. */
	int32 SimulationLOD = 0;

	/** Accumulator so decision-making runs at a fixed rate, not per frame. */
	float DecisionAccumulator = 0.0f;

	/** Per-creature phase offset so a herd does not think in lockstep. */
	float DecisionPhase = 0.0f;

	/** Migration route progress. */
	int32 MigrationStopIndex = 0;
	float DaysAtCurrentStop = 0.0f;

	/** Decision interval per LOD, in seconds. */
	static constexpr float DecisionIntervals[3] = { 0.35f, 1.5f, 8.0f };
};
