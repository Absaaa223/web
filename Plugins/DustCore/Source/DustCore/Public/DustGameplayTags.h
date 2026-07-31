// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Natively declared gameplay tags.
 *
 * Tags are the lingua franca between systems that must not depend on each
 * other: the weather subsystem raises Dust.Weather.Sandstorm, and fauna, audio
 * and the curiosity director all react without any of them including a weather
 * header. Anything crossing a module boundary should be a tag.
 *
 * Content-authored tags (per-region, per-species, per-artefact) live in
 * Config/Tags/ and are declared in data, not here.
 */
struct DUSTCORE_API FDustGameplayTags
{
	/** Registers descriptions and validates that required tags exist. */
	static void InitialiseNativeTags();
};

// --- Movement / traversal state --------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Move_Walk);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Move_Sprint);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Move_Crouch);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Move_Slide);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Move_Climb);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Move_Mantle);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Move_Swim);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Move_Rope);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Move_Glide);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Move_Board);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Move_Hoverbike);

// --- Vitals ----------------------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Vitals_Exhausted);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Vitals_Parched);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Vitals_Overheating);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Vitals_Cold);

// --- Weather ---------------------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Weather_Clear);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Weather_Haze);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Weather_Sandstorm);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Weather_Rain);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Weather_Fog);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Weather_Storm);

// --- Time of day -----------------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Time_Dawn);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Time_Day);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Time_Dusk);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Time_Night);

// --- Discovery categories --------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Discovery_Landmark);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Discovery_Ruin);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Discovery_Artefact);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Discovery_Inscription);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Discovery_Creature);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Discovery_Vista);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Discovery_Cache);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Discovery_Anomaly);

// --- Fauna behaviour -------------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Fauna_Curious);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Fauna_Territorial);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Fauna_Indifferent);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Fauna_Skittish);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Fauna_Migrating);

// --- Audio states ----------------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Audio_Silence);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Audio_Wander);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Audio_Approach);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Audio_Reveal);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Dust_Audio_Threat);
