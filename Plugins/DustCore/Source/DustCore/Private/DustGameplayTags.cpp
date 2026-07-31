// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "DustGameplayTags.h"

#include "DustLog.h"
#include "GameplayTagsManager.h"

// Movement
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Move_Walk, "Dust.Move.Walk", "Default locomotion.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Move_Sprint, "Dust.Move.Sprint", "Sustained run, drains stamina.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Move_Crouch, "Dust.Move.Crouch", "Lowered stance; halves fauna detection radius.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Move_Slide, "Dust.Move.Slide", "Sliding down a dune face on foot.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Move_Climb, "Dust.Move.Climb", "Free climbing a wall surface.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Move_Mantle, "Dust.Move.Mantle", "Root-motion ledge entry.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Move_Swim, "Dust.Move.Swim", "In water volume.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Move_Rope, "Dust.Move.Rope", "Attached to a deployed rope.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Move_Glide, "Dust.Move.Glide", "Paraglider deployed.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Move_Board, "Dust.Move.Board", "Riding the sand board.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Move_Hoverbike, "Dust.Move.Hoverbike", "Piloting the hoverbike.");

// Vitals
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Vitals_Exhausted, "Dust.Vitals.Exhausted", "Stamina floor reached; sprint locked out until recovery.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Vitals_Parched, "Dust.Vitals.Parched", "Hydration low; stamina ceiling reduced.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Vitals_Overheating, "Dust.Vitals.Overheating", "Sustained exposure in open sun.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Vitals_Cold, "Dust.Vitals.Cold", "Night or high-altitude exposure.");

// Weather
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Weather_Clear, "Dust.Weather.Clear", "Open sky, long draw distance.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Weather_Haze, "Dust.Weather.Haze", "Suspended dust, warm scattering.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Weather_Sandstorm, "Dust.Weather.Sandstorm", "Visibility collapse, dune migration accelerates.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Weather_Rain, "Dust.Weather.Rain", "Rare. Triggers the bloom event in flora regions.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Weather_Fog, "Dust.Weather.Fog", "Dawn ground fog in basins.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Weather_Storm, "Dust.Weather.Storm", "Electrical storm; lightning strikes on high ground.");

// Time
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Time_Dawn, "Dust.Time.Dawn", "Sun below 10 degrees, rising.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Time_Day, "Dust.Time.Day", "Sun above 10 degrees.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Time_Dusk, "Dust.Time.Dusk", "Sun below 10 degrees, setting.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Time_Night, "Dust.Time.Night", "Sun below the horizon.");

// Discovery
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Discovery_Landmark, "Dust.Discovery.Landmark", "Silhouette readable from kilometres away.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Discovery_Ruin, "Dust.Discovery.Ruin", "Built structure of the vanished civilisation.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Discovery_Artefact, "Dust.Discovery.Artefact", "Carryable object with narrative weight.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Discovery_Inscription, "Dust.Discovery.Inscription", "Readable carving or journal fragment.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Discovery_Creature, "Dust.Discovery.Creature", "First sighting of a species.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Discovery_Vista, "Dust.Discovery.Vista", "Authored composition viewpoint.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Discovery_Cache, "Dust.Discovery.Cache", "Hidden supply or equipment cache.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Discovery_Anomaly, "Dust.Discovery.Anomaly", "Rare emergent event or unexplained phenomenon.");

// Fauna
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Fauna_Curious, "Dust.Fauna.Curious", "Approaches and observes the player.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Fauna_Territorial, "Dust.Fauna.Territorial", "Defends a home range; warns before escalating.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Fauna_Indifferent, "Dust.Fauna.Indifferent", "Registers the player and carries on.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Fauna_Skittish, "Dust.Fauna.Skittish", "Flees at long range.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Fauna_Migrating, "Dust.Fauna.Migrating", "Currently following a seasonal route.");

// Audio
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Audio_Silence, "Dust.Audio.Silence", "Deliberate musical silence.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Audio_Wander, "Dust.Audio.Wander", "Sparse ambient bed.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Audio_Approach, "Dust.Audio.Approach", "Something of interest is near.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Audio_Reveal, "Dust.Audio.Reveal", "Discovery landed.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Dust_Audio_Threat, "Dust.Audio.Threat", "Environmental or animal pressure.");

void FDustGameplayTags::InitialiseNativeTags()
{
	// UE_DEFINE_GAMEPLAY_TAG_COMMENT registers on static init; this hook exists
	// so the module has a deterministic point to assert on required content
	// tags and to log the final tag count for build diffing.
	const UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	FGameplayTagContainer AllTags;
	Manager.RequestAllGameplayTags(AllTags, /*bOnlyIncludeDictionaryTags=*/false);

	UE_LOG(LogDust, Log, TEXT("Gameplay tags registered: %d"), AllTags.Num());
}
