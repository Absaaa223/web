// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "DustTypes.h"
#include "DustSaveGame.generated.h"

/**
 * Save data versioning.
 *
 * DUST saves must survive patches. The rule: never remove or reorder a field,
 * never change the meaning of an existing one. Add new fields with a default
 * that means "as before", bump ELatest, and handle the upgrade in
 * UDustSaveGame::UpgradeFrom.
 */
UENUM()
enum class EDustSaveVersion : uint32
{
	Initial            = 1,

	/** Added per-region visit timestamps. */
	RegionTimestamps   = 2,

	/** Added photo gallery manifest and equipment loadout. */
	GalleryAndLoadout  = 3,

	/** Added creature encounter ledger. */
	CreatureLedger     = 4,

	// -- Add new versions above this line --
	Latest             = CreatureLedger
};

/** One entry in the player's journal, written as they piece the world together. */
USTRUCT()
struct DUSTCORE_API FDustJournalEntryRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FName EntryId;

	/** In-game time the entry was unlocked. */
	UPROPERTY()
	FDustWorldTime UnlockedAt;

	/** Where the player was standing. Used by the journal map spread. */
	UPROPERTY()
	FVector UnlockedLocation = FVector::ZeroVector;
};

/** Persistent record of a point of interest the player has interacted with. */
USTRUCT()
struct DUSTCORE_API FDustDiscoveryRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid PoiId;

	UPROPERTY()
	EDustDiscoveryState State = EDustDiscoveryState::Unknown;

	UPROPERTY()
	FDustWorldTime FirstSeenAt;

	/** Sub-elements resolved within this POI (inscriptions read, caches opened). */
	UPROPERTY()
	TSet<FName> ResolvedElements;
};

/** Everything we remember about a species the player has met. */
USTRUCT()
struct DUSTCORE_API FDustCreatureLedgerEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FName SpeciesId;

	UPROPERTY()
	int32 TimesSighted = 0;

	UPROPERTY()
	FDustWorldTime FirstSighting;

	/** Behaviours the player has personally witnessed; unlocks journal text. */
	UPROPERTY()
	TSet<FName> ObservedBehaviours;
};

/** A saved photograph reference for the in-game gallery. */
USTRUCT()
struct DUSTCORE_API FDustPhotoRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FString RelativePath;

	UPROPERTY()
	FDustWorldTime CapturedAt;

	UPROPERTY()
	FVector CapturedLocation = FVector::ZeroVector;

	UPROPERTY()
	FRotator CapturedRotation = FRotator::ZeroRotator;

	/** Region the shot was taken in, for gallery grouping. */
	UPROPERTY()
	FName RegionId;
};

/**
 * The DUST save game.
 *
 * Design note: we store a *seed and a delta*, not a world snapshot. The terrain,
 * POI layout and creature populations are all reproducible from
 * (MasterSeed, AlgorithmVersion). What we persist is only what the player
 * changed or learned. A complete save is a few hundred kilobytes, which keeps
 * Steam Cloud sync instant and makes save corruption almost impossible to
 * cause by a content patch.
 */
UCLASS(BlueprintType)
class DUSTCORE_API UDustSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// -----------------------------------------------------------------------
	// Header - readable without deserialising the whole save, for the slot list
	// -----------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Dust|Save")
	uint32 SaveVersion = static_cast<uint32>(EDustSaveVersion::Latest);

	/** Noise algorithm version the world was generated with. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Save")
	int32 WorldAlgorithmVersion = 0;

	/** The seed this world was built from. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Save")
	int32 MasterSeed = 0;

	/** Slot display name; autosaves are named by location. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Save")
	FText DisplayName;

	/** Real-world timestamp for sorting the slot list. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Save")
	FDateTime RealTimestamp;

	/** Total real seconds played across all sessions in this world. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Save")
	double TotalPlaySeconds = 0.0;

	/** True if this slot was written by the autosave rotation. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Save")
	bool bIsAutosave = false;

	// -----------------------------------------------------------------------
	// Player state
	// -----------------------------------------------------------------------

	UPROPERTY()
	FVector PlayerLocation = FVector::ZeroVector;

	UPROPERTY()
	FRotator PlayerRotation = FRotator::ZeroRotator;

	UPROPERTY()
	FDustWorldTime WorldTime;

	UPROPERTY()
	float Stamina = 1.0f;

	UPROPERTY()
	float Hydration = 1.0f;

	/** Equipment the player has found: board, glider, rope, hoverbike, lamp. */
	UPROPERTY()
	TSet<FName> UnlockedEquipment;

	/** Currently equipped traversal tool, if any. */
	UPROPERTY()
	FName ActiveEquipment;

	/** Carried artefacts, in the order they were picked up. */
	UPROPERTY()
	TArray<FName> CarriedArtefacts;

	// -----------------------------------------------------------------------
	// World knowledge
	// -----------------------------------------------------------------------

	UPROPERTY()
	TArray<FDustDiscoveryRecord> Discoveries;

	UPROPERTY()
	TArray<FDustJournalEntryRecord> JournalEntries;

	UPROPERTY()
	TArray<FDustCreatureLedgerEntry> CreatureLedger;

	/** First-visit timestamps keyed by region id. Drives the "days here" readout. */
	UPROPERTY()
	TMap<FName, FDustWorldTime> RegionFirstVisit;

	/** Photographs the player kept. */
	UPROPERTY()
	TArray<FDustPhotoRecord> Photos;

	// -----------------------------------------------------------------------
	// World deltas
	// -----------------------------------------------------------------------

	/**
	 * Objects the player permanently changed: doors opened, bridges dropped,
	 * caches emptied. Keyed by a stable actor identity, not by name.
	 */
	UPROPERTY()
	TMap<FGuid, FName> WorldStateFlags;

	/** Weather state carried across the save so a storm does not vanish on load. */
	UPROPERTY()
	FGameplayTag ActiveWeatherTag;

	UPROPERTY()
	float WeatherPhase = 0.0f;

	// -----------------------------------------------------------------------
	// Migration
	// -----------------------------------------------------------------------

	/**
	 * Upgrades an older save in place. Returns false only if the save is from
	 * a future version or a different world seed, in which case the slot is
	 * shown as incompatible rather than silently discarded.
	 */
	bool UpgradeToLatest();

	/** True if this save can be loaded by the running build. */
	bool IsCompatible(FString& OutReason) const;
};
