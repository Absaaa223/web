// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DustTypes.h"
#include "Save/DustSaveSubsystem.h"
#include "DustEcologySubsystem.generated.h"

class UDustSpeciesData;
class UDustCreatureBrain;

/**
 * A creature the simulation is tracking but has not spawned an actor for.
 *
 * Most of the desert's animals exist only as one of these: a position, a
 * species, and a set of needs that keep evolving. They migrate, they get
 * thirsty, they move between regions. When the player gets close, the record is
 * promoted to a real pawn at the position it had reached; when they leave, the
 * pawn is demoted back to a record.
 *
 * This is what makes the world feel populated rather than staged. The herd the
 * player watched at the oasis yesterday is not respawned at the oasis today -
 * it is wherever the simulation walked it to overnight.
 */
USTRUCT()
struct DUSTFAUNA_API FDustCreatureRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FName SpeciesId;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FVector HomeLocation = FVector::ZeroVector;

	UPROPERTY()
	float Thirst = 0.0f;

	UPROPERTY()
	float Hunger = 0.0f;

	UPROPERTY()
	float Fatigue = 0.0f;

	/** Group this individual belongs to; members move together. */
	UPROPERTY()
	int32 GroupId = INDEX_NONE;

	/** Actor currently representing this record, if promoted. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> SpawnedActor;

	FORCEINLINE bool IsPromoted() const { return SpawnedActor.IsValid(); }
};

/**
 * Owns the desert's animal populations.
 *
 * RESPONSIBILITIES
 *   - Deciding how many of each species live where, from region data
 *   - Advancing the coarse simulation for creatures the player cannot see
 *   - Promoting records to pawns and demoting them again, within a budget
 *   - Running migrations
 *   - Recording sightings into the player's creature ledger
 */
UCLASS()
class DUSTFAUNA_API UDustEcologySubsystem
	: public UTickableWorldSubsystem
	, public IDustSaveParticipant
{
	GENERATED_BODY()

public:
	//~ Begin UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	//~ Begin IDustSaveParticipant
	virtual void OnDustSave(UDustSaveGame& Save) override;
	virtual void OnDustLoad(const UDustSaveGame& Save) override;
	/** Last: fauna need terrain and time to exist before they can be placed. */
	virtual int32 GetDustLoadPriority() const override { return 200; }
	//~ End IDustSaveParticipant

	static UDustEcologySubsystem* Get(const UObject* WorldContext);

	/** Species data by id. */
	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	UDustSpeciesData* FindSpecies(FName SpeciesId) const;

	/** Number of creatures currently promoted to full pawns. */
	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	int32 GetPromotedCount() const;

	/** Total creatures tracked, promoted or not. */
	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	int32 GetTrackedCount() const { return Records.Num(); }

	/**
	 * Records that the player has observed a species doing something.
	 * Called by the creature's action-changed hook when the player is watching.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Fauna")
	void RecordSighting(FName SpeciesId, FName ObservedBehaviour);

	/** True if the player has ever seen this species. */
	UFUNCTION(BlueprintPure, Category = "Dust|Fauna")
	bool HasSeenSpecies(FName SpeciesId) const;

private:
	/** Loads every species asset. */
	void LoadSpecies();

	/** Populates records for a region the player has come within range of. */
	void PopulateRegion(FName RegionId);

	/** Advances needs and positions for unpromoted records. */
	void TickCoarseSimulation(float DeltaHours);

	/** Promotes and demotes records based on distance and budget. */
	void UpdatePromotion();

	/** Spawns a pawn for a record. */
	bool PromoteRecord(FDustCreatureRecord& Record);

	/** Writes a pawn's state back into its record and destroys it. */
	void DemoteRecord(FDustCreatureRecord& Record);

private:
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UDustSpeciesData>> SpeciesById;

	UPROPERTY(Transient)
	TArray<FDustCreatureRecord> Records;

	/** Regions already populated, so we do not double-populate. */
	TSet<FName> PopulatedRegions;

	/** Species the player has seen, and the behaviours they witnessed. */
	TMap<FName, TSet<FName>> ObservedBehaviours;
	TMap<FName, int32> SightingCounts;
	TMap<FName, FDustWorldTime> FirstSightings;

	/** Accumulator for the coarse simulation, which runs at 1 Hz. */
	float CoarseAccumulator = 0.0f;

	/** Accumulator for promotion checks, which run at 4 Hz. */
	float PromotionAccumulator = 0.0f;

	/** Next group id to hand out. */
	int32 NextGroupId = 0;
};
