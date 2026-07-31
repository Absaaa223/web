// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

// ---------------------------------------------------------------------------
// Log categories.
//
// One category per major system so that `log LogDustFauna Verbose` can be used
// to isolate a single simulation without drowning in the rest of the world.
// Shipping keeps Warning and above; everything below is compiled out.
// ---------------------------------------------------------------------------

DUSTCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDust, Log, All);
DUSTCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDustWorld, Log, All);
DUSTCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDustWeather, Log, All);
DUSTCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDustFauna, Log, All);
DUSTCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDustNarrative, Log, All);
DUSTCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDustAudio, Log, All);
DUSTCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDustTraversal, Log, All);
DUSTCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDustSave, Log, All);
DUSTCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogDustPerf, Log, All);

/**
 * Named CPU scope. Wraps both the Unreal Insights trace channel and the stat
 * system so a single macro covers profiling in editor and in a cooked build.
 */
#define DUST_SCOPED_TIMER(Name) TRACE_CPUPROFILER_EVENT_SCOPE(Name)

/**
 * Verbose logging that costs nothing in Shipping. Use for per-frame or
 * per-agent chatter that would otherwise be too expensive to leave in.
 */
#if !UE_BUILD_SHIPPING
	#define DUST_VERBOSE(Category, Format, ...) UE_LOG(Category, Verbose, Format, ##__VA_ARGS__)
#else
	#define DUST_VERBOSE(Category, Format, ...)
#endif
