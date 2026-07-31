// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Save/DustSaveGame.h"

#include "DustLog.h"
#include "Math/DustNoise.h"

bool UDustSaveGame::UpgradeToLatest()
{
	const uint32 Latest = static_cast<uint32>(EDustSaveVersion::Latest);

	if (SaveVersion > Latest)
	{
		UE_LOG(LogDustSave, Warning,
			TEXT("Save is from a newer build (v%u > v%u); refusing to downgrade."),
			SaveVersion, Latest);
		return false;
	}

	if (SaveVersion == Latest)
	{
		return true;
	}

	UE_LOG(LogDustSave, Log, TEXT("Upgrading save v%u -> v%u."), SaveVersion, Latest);

	// Each step upgrades by exactly one version so chains compose correctly.
	// Fallthrough is intentional.
	switch (static_cast<EDustSaveVersion>(SaveVersion))
	{
	case EDustSaveVersion::Initial:
		// v1 -> v2: region timestamps did not exist. Seed them from the
		// discovery records so the journal's "days here" is approximately right
		// rather than reading zero for a 40-hour save.
		for (const FDustDiscoveryRecord& Record : Discoveries)
		{
			// Region attribution for legacy saves is best-effort; the sampler
			// resolves the real region on next entry and overwrites this.
			if (!RegionFirstVisit.Contains(NAME_None))
			{
				RegionFirstVisit.Add(NAME_None, Record.FirstSeenAt);
			}
		}
		[[fallthrough]];

	case EDustSaveVersion::RegionTimestamps:
		// v2 -> v3: gallery and loadout added. Empty containers are correct
		// defaults - a legacy player simply has no saved photos.
		[[fallthrough]];

	case EDustSaveVersion::GalleryAndLoadout:
		// v3 -> v4: creature ledger added. Nothing to backfill; the player
		// re-registers sightings naturally as they play on.
		[[fallthrough]];

	case EDustSaveVersion::CreatureLedger:
	default:
		break;
	}

	SaveVersion = Latest;
	return true;
}

bool UDustSaveGame::IsCompatible(FString& OutReason) const
{
	if (SaveVersion > static_cast<uint32>(EDustSaveVersion::Latest))
	{
		OutReason = TEXT("Saved by a newer version of the game.");
		return false;
	}

	// A noise algorithm change moves the ground. We would rather show the slot
	// as incompatible than drop the player inside a mountain.
	if (WorldAlgorithmVersion != 0 && WorldAlgorithmVersion != FDustNoise::AlgorithmVersion)
	{
		OutReason = FString::Printf(
			TEXT("World was generated with terrain version %d; this build uses %d."),
			WorldAlgorithmVersion, FDustNoise::AlgorithmVersion);
		return false;
	}

	OutReason.Reset();
	return true;
}
