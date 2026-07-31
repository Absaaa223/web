// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Save/DustSaveGame.h"
#include "DustSaveSubsystem.generated.h"

class UDustSaveGame;

/** Broadcast when a save completes. bSuccess is false on disk or quota failure. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDustSaveCompleted, const FString&, SlotName, bool, bSuccess);

/** Broadcast when a load completes and the world has been restored. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDustLoadCompleted, const FString&, SlotName, bool, bSuccess);

/**
 * Interface implemented by anything that owns persistent state.
 *
 * Systems register themselves with the save subsystem rather than the save
 * subsystem knowing about them. This keeps DustCore free of dependencies on
 * DustWorld, DustFauna and the rest.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UDustSaveParticipant : public UInterface
{
	GENERATED_BODY()
};

class DUSTCORE_API IDustSaveParticipant
{
	GENERATED_BODY()

public:
	/** Write this system's state into the save object. */
	virtual void OnDustSave(UDustSaveGame& Save) = 0;

	/** Restore this system's state from the save object. */
	virtual void OnDustLoad(const UDustSaveGame& Save) = 0;

	/**
	 * Participants are restored in ascending priority order. The world sampler
	 * and streaming must be ready (priority 0) before the player is teleported
	 * (priority 100) before fauna repopulate (priority 200).
	 */
	virtual int32 GetDustLoadPriority() const { return 100; }
};

/**
 * Owns saving, loading, autosave rotation and slot enumeration.
 *
 * Threading: serialisation of the save object happens on the game thread
 * (it touches UObjects), but the byte-array write to disk is dispatched to a
 * background task. A save therefore costs well under a millisecond of frame
 * time even on a spinning disk, which is why autosave never causes a hitch.
 */
UCLASS()
class DUSTCORE_API UDustSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// -----------------------------------------------------------------------
	// Registration
	// -----------------------------------------------------------------------

	/** Registers a participant. Safe to call multiple times; duplicates ignored. */
	void RegisterParticipant(TScriptInterface<IDustSaveParticipant> Participant);

	/** Unregisters a participant. Called automatically on Deinitialize. */
	void UnregisterParticipant(TScriptInterface<IDustSaveParticipant> Participant);

	// -----------------------------------------------------------------------
	// Saving
	// -----------------------------------------------------------------------

	/**
	 * Writes a manual save to the given slot.
	 *
	 * @param SlotName    Slot identifier, e.g. "Manual_03".
	 * @param DisplayName Player-visible label.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Save")
	void SaveToSlot(const FString& SlotName, const FText& DisplayName);

	/**
	 * Requests an autosave. The request is *deferred* until the player is in a
	 * safe state - on the ground, not gliding, not mid-climb, not being chased.
	 * This is why the player never sees a save icon at a bad moment.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Save")
	void RequestAutosave();

	/** True if an autosave is queued and waiting for a safe moment. */
	UFUNCTION(BlueprintPure, Category = "Dust|Save")
	bool IsAutosavePending() const { return bAutosavePending; }

	// -----------------------------------------------------------------------
	// Loading
	// -----------------------------------------------------------------------

	/** Loads the given slot and restores all participants in priority order. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Save")
	void LoadFromSlot(const FString& SlotName);

	/** Loads the most recent valid slot, autosave or manual. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Save")
	bool LoadMostRecent();

	/** True if any loadable save exists, used to enable the Continue button. */
	UFUNCTION(BlueprintPure, Category = "Dust|Save")
	bool HasAnySave() const;

	// -----------------------------------------------------------------------
	// Slot enumeration
	// -----------------------------------------------------------------------

	/**
	 * Returns headers for every slot on disk, newest first. Reads only the
	 * header portion of each file, so opening the load menu does not stall.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Save")
	TArray<UDustSaveGame*> GetSlotHeaders() const;

	/** Deletes a slot. Autosave slots may be deleted like any other. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Save")
	bool DeleteSlot(const FString& SlotName);

	// -----------------------------------------------------------------------
	// Events
	// -----------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Dust|Save")
	FDustSaveCompleted OnSaveCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Dust|Save")
	FDustLoadCompleted OnLoadCompleted;

	/** Ticks the autosave timer. Called by the world subsystem each frame. */
	void TickAutosave(float DeltaSeconds, bool bPlayerInSafeState);

	/** Total real seconds played in the current world. */
	double GetTotalPlaySeconds() const { return TotalPlaySeconds; }

	/** Accumulates play time; called once per frame by the game mode. */
	void AccumulatePlayTime(double DeltaSeconds) { TotalPlaySeconds += DeltaSeconds; }

private:
	/** Builds a fully populated save object from every registered participant. */
	UDustSaveGame* CaptureState(const FText& DisplayName, bool bAutosave);

	/** Applies a loaded save to every registered participant, in priority order. */
	void ApplyState(const UDustSaveGame& Save);

	/** Writes the object to disk asynchronously and fires OnSaveCompleted. */
	void WriteAsync(UDustSaveGame* Save, const FString& SlotName);

	/** Name of the next autosave slot in the rotation. */
	FString GetNextAutosaveSlotName() const;

private:
	/** Registered participants. Weak so a destroyed actor cannot keep us alive. */
	UPROPERTY()
	TArray<TScriptInterface<IDustSaveParticipant>> Participants;

	/** Seconds since the last autosave. */
	float AutosaveTimer = 0.0f;

	/** True once the interval elapsed; cleared when the save actually happens. */
	bool bAutosavePending = false;

	/** Index into the rolling autosave slots. */
	int32 AutosaveRotation = 0;

	/** Accumulated real play time for the current world. */
	double TotalPlaySeconds = 0.0;

	/** True while a write is in flight, to prevent overlapping saves. */
	TAtomic<bool> bWriteInFlight { false };
};
