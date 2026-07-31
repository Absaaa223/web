// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Save/DustSaveSubsystem.h"

#include "Async/Async.h"
#include "DustDeveloperSettings.h"
#include "DustLog.h"
#include "Kismet/GameplayStatics.h"
#include "Math/DustNoise.h"

namespace DustSave
{
	static const TCHAR* AutosavePrefix = TEXT("Autosave_");
	static const TCHAR* ManualPrefix   = TEXT("Manual_");
}

void UDustSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();
	AutosaveTimer = 0.0f;
	AutosaveRotation = 0;

	UE_LOG(LogDustSave, Log,
		TEXT("Save subsystem ready. Autosave every %.0fs across %d slots."),
		Settings.AutosaveIntervalSeconds, Settings.AutosaveSlotCount);
}

void UDustSaveSubsystem::Deinitialize()
{
	Participants.Reset();
	Super::Deinitialize();
}

void UDustSaveSubsystem::RegisterParticipant(TScriptInterface<IDustSaveParticipant> Participant)
{
	if (!Participant)
	{
		return;
	}

	Participants.AddUnique(Participant);

	// Keep the list sorted by priority so ApplyState never has to sort during
	// a load, where we are already competing with streaming for frame time.
	Participants.Sort([](const TScriptInterface<IDustSaveParticipant>& A,
	                     const TScriptInterface<IDustSaveParticipant>& B)
	{
		const IDustSaveParticipant* IA = A.GetInterface();
		const IDustSaveParticipant* IB = B.GetInterface();
		const int32 PA = IA ? IA->GetDustLoadPriority() : MAX_int32;
		const int32 PB = IB ? IB->GetDustLoadPriority() : MAX_int32;
		return PA < PB;
	});
}

void UDustSaveSubsystem::UnregisterParticipant(TScriptInterface<IDustSaveParticipant> Participant)
{
	Participants.Remove(Participant);
}

UDustSaveGame* UDustSaveSubsystem::CaptureState(const FText& DisplayName, bool bAutosave)
{
	DUST_SCOPED_TIMER(Dust_Save_CaptureState);

	UDustSaveGame* Save = Cast<UDustSaveGame>(UGameplayStatics::CreateSaveGameObject(UDustSaveGame::StaticClass()));
	if (!Save)
	{
		UE_LOG(LogDustSave, Error, TEXT("Failed to allocate save object."));
		return nullptr;
	}

	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	Save->SaveVersion           = static_cast<uint32>(EDustSaveVersion::Latest);
	Save->WorldAlgorithmVersion = FDustNoise::AlgorithmVersion;
	Save->MasterSeed            = Settings.MasterSeed;
	Save->DisplayName           = DisplayName;
	Save->RealTimestamp         = FDateTime::UtcNow();
	Save->TotalPlaySeconds      = TotalPlaySeconds;
	Save->bIsAutosave           = bAutosave;

	for (const TScriptInterface<IDustSaveParticipant>& Participant : Participants)
	{
		if (IDustSaveParticipant* Interface = Participant.GetInterface())
		{
			Interface->OnDustSave(*Save);
		}
	}

	return Save;
}

void UDustSaveSubsystem::ApplyState(const UDustSaveGame& Save)
{
	DUST_SCOPED_TIMER(Dust_Save_ApplyState);

	TotalPlaySeconds = Save.TotalPlaySeconds;

	// Participants are pre-sorted by priority: world first, player second,
	// populations last.
	for (const TScriptInterface<IDustSaveParticipant>& Participant : Participants)
	{
		if (IDustSaveParticipant* Interface = Participant.GetInterface())
		{
			Interface->OnDustLoad(Save);
		}
	}
}

void UDustSaveSubsystem::WriteAsync(UDustSaveGame* Save, const FString& SlotName)
{
	if (!Save)
	{
		OnSaveCompleted.Broadcast(SlotName, false);
		return;
	}

	if (bWriteInFlight.Load())
	{
		UE_LOG(LogDustSave, Warning, TEXT("Save to '%s' skipped: a write is already in flight."), *SlotName);
		OnSaveCompleted.Broadcast(SlotName, false);
		return;
	}

	// Serialise on the game thread - this touches UObject reflection and must
	// not race with GC - then hand the raw bytes to a background task.
	TArray<uint8> Bytes;
	if (!UGameplayStatics::SaveGameToMemory(Save, Bytes))
	{
		UE_LOG(LogDustSave, Error, TEXT("Serialisation failed for slot '%s'."), *SlotName);
		OnSaveCompleted.Broadcast(SlotName, false);
		return;
	}

	bWriteInFlight = true;

	TWeakObjectPtr<UDustSaveSubsystem> WeakThis(this);

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[Bytes = MoveTemp(Bytes), SlotName, WeakThis]() mutable
		{
			const bool bWritten = UGameplayStatics::SaveDataToSlot(Bytes, SlotName, /*UserIndex=*/0);

			AsyncTask(ENamedThreads::GameThread, [bWritten, SlotName, WeakThis]()
			{
				if (UDustSaveSubsystem* Subsystem = WeakThis.Get())
				{
					Subsystem->bWriteInFlight = false;

					UE_CLOG(!bWritten, LogDustSave, Error, TEXT("Disk write failed for slot '%s'."), *SlotName);
					UE_CLOG(bWritten, LogDustSave, Log, TEXT("Saved slot '%s'."), *SlotName);

					Subsystem->OnSaveCompleted.Broadcast(SlotName, bWritten);
				}
			});
		});
}

void UDustSaveSubsystem::SaveToSlot(const FString& SlotName, const FText& DisplayName)
{
	if (UDustSaveGame* Save = CaptureState(DisplayName, /*bAutosave=*/false))
	{
		WriteAsync(Save, SlotName);
	}
	else
	{
		OnSaveCompleted.Broadcast(SlotName, false);
	}
}

void UDustSaveSubsystem::RequestAutosave()
{
	bAutosavePending = true;
}

FString UDustSaveSubsystem::GetNextAutosaveSlotName() const
{
	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();
	const int32 SlotCount = FMath::Max(1, Settings.AutosaveSlotCount);
	return FString::Printf(TEXT("%s%02d"), DustSave::AutosavePrefix, AutosaveRotation % SlotCount);
}

void UDustSaveSubsystem::TickAutosave(float DeltaSeconds, bool bPlayerInSafeState)
{
	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	AutosaveTimer += DeltaSeconds;

	if (AutosaveTimer >= Settings.AutosaveIntervalSeconds)
	{
		AutosaveTimer = 0.0f;
		bAutosavePending = true;
	}

	// The pending flag is only cleared when the player is somewhere it is
	// reasonable to reload from. Mid-glide autosaves would drop the player back
	// into freefall on load, which is exactly the sort of thing that makes
	// autosave feel hostile.
	if (bAutosavePending && bPlayerInSafeState && !bWriteInFlight.Load())
	{
		bAutosavePending = false;

		const FString SlotName = GetNextAutosaveSlotName();
		++AutosaveRotation;

		if (UDustSaveGame* Save = CaptureState(FText::GetEmpty(), /*bAutosave=*/true))
		{
			WriteAsync(Save, SlotName);
		}
	}
}

void UDustSaveSubsystem::LoadFromSlot(const FString& SlotName)
{
	DUST_SCOPED_TIMER(Dust_Save_Load);

	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		UE_LOG(LogDustSave, Warning, TEXT("Slot '%s' does not exist."), *SlotName);
		OnLoadCompleted.Broadcast(SlotName, false);
		return;
	}

	UDustSaveGame* Save = Cast<UDustSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!Save)
	{
		UE_LOG(LogDustSave, Error, TEXT("Slot '%s' failed to deserialise."), *SlotName);
		OnLoadCompleted.Broadcast(SlotName, false);
		return;
	}

	FString IncompatibleReason;
	if (!Save->IsCompatible(IncompatibleReason))
	{
		UE_LOG(LogDustSave, Error, TEXT("Slot '%s' incompatible: %s"), *SlotName, *IncompatibleReason);
		OnLoadCompleted.Broadcast(SlotName, false);
		return;
	}

	if (!Save->UpgradeToLatest())
	{
		OnLoadCompleted.Broadcast(SlotName, false);
		return;
	}

	ApplyState(*Save);

	UE_LOG(LogDustSave, Log, TEXT("Loaded slot '%s' (%.1f hours played)."),
		*SlotName, Save->TotalPlaySeconds / 3600.0);

	OnLoadCompleted.Broadcast(SlotName, true);
}

TArray<UDustSaveGame*> UDustSaveSubsystem::GetSlotHeaders() const
{
	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	TArray<UDustSaveGame*> Headers;
	Headers.Reserve(Settings.AutosaveSlotCount + Settings.ManualSlotCount);

	auto TryAdd = [&Headers](const FString& SlotName)
	{
		if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
		{
			return;
		}
		if (UDustSaveGame* Save = Cast<UDustSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
		{
			Headers.Add(Save);
		}
	};

	for (int32 Index = 0; Index < Settings.AutosaveSlotCount; ++Index)
	{
		TryAdd(FString::Printf(TEXT("%s%02d"), DustSave::AutosavePrefix, Index));
	}
	for (int32 Index = 0; Index < Settings.ManualSlotCount; ++Index)
	{
		TryAdd(FString::Printf(TEXT("%s%02d"), DustSave::ManualPrefix, Index));
	}

	Headers.Sort([](const UDustSaveGame& A, const UDustSaveGame& B)
	{
		return A.RealTimestamp > B.RealTimestamp;
	});

	return Headers;
}

bool UDustSaveSubsystem::HasAnySave() const
{
	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	for (int32 Index = 0; Index < Settings.AutosaveSlotCount; ++Index)
	{
		if (UGameplayStatics::DoesSaveGameExist(FString::Printf(TEXT("%s%02d"), DustSave::AutosavePrefix, Index), 0))
		{
			return true;
		}
	}
	for (int32 Index = 0; Index < Settings.ManualSlotCount; ++Index)
	{
		if (UGameplayStatics::DoesSaveGameExist(FString::Printf(TEXT("%s%02d"), DustSave::ManualPrefix, Index), 0))
		{
			return true;
		}
	}
	return false;
}

bool UDustSaveSubsystem::LoadMostRecent()
{
	const TArray<UDustSaveGame*> Headers = GetSlotHeaders();
	if (Headers.Num() == 0)
	{
		return false;
	}

	// Headers are newest-first; find the first that this build can actually load.
	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	for (const UDustSaveGame* Header : Headers)
	{
		FString Reason;
		if (!Header || !Header->IsCompatible(Reason))
		{
			continue;
		}

		// Reconstruct the slot name from the header's autosave flag and index.
		// We re-enumerate rather than store it, because a header is a plain
		// deserialised object with no knowledge of where it came from.
		const int32 Count = Header->bIsAutosave ? Settings.AutosaveSlotCount : Settings.ManualSlotCount;
		const TCHAR* Prefix = Header->bIsAutosave ? DustSave::AutosavePrefix : DustSave::ManualPrefix;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FString SlotName = FString::Printf(TEXT("%s%02d"), Prefix, Index);
			const UDustSaveGame* Candidate = Cast<UDustSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));

			if (Candidate && Candidate->RealTimestamp == Header->RealTimestamp)
			{
				LoadFromSlot(SlotName);
				return true;
			}
		}
	}

	return false;
}

bool UDustSaveSubsystem::DeleteSlot(const FString& SlotName)
{
	const bool bDeleted = UGameplayStatics::DeleteGameInSlot(SlotName, 0);
	UE_CLOG(bDeleted, LogDustSave, Log, TEXT("Deleted slot '%s'."), *SlotName);
	return bDeleted;
}
