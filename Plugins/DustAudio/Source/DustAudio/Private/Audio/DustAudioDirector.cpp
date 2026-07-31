// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Audio/DustAudioDirector.h"

#include "Components/AudioComponent.h"
#include "DustGameplayTags.h"
#include "DustLog.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Narrative/DustDiscoverySubsystem.h"
#include "Weather/DustWeatherSubsystem.h"
#include "World/DustWorldSubsystem.h"

namespace DustAudioCVars
{
	static int32 DebugAudio = 0;
	static FAutoConsoleVariableRef CVarDebugAudio(
		TEXT("dust.debug.audio"),
		DebugAudio,
		TEXT("Log the audio director's state, music density and enclosure once per second."),
		ECVF_Cheat);

	static int32 IgnoreSilenceBudget = 0;
	static FAutoConsoleVariableRef CVarIgnoreSilence(
		TEXT("dust.audio.IgnoreSilenceBudget"),
		IgnoreSilenceBudget,
		TEXT("Let every requested cue through. For auditioning music only - never ship enabled."),
		ECVF_Cheat);
}

UDustAudioDirector* UDustAudioDirector::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDustAudioDirector>() : nullptr;
}

bool UDustAudioDirector::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDustAudioDirector::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	CurrentState = TAG_Dust_Audio_Silence;
	SilenceTimer = MinimumSilenceSeconds; // allow the first cue immediately

	if (UDustDiscoverySubsystem* Discovery = UDustDiscoverySubsystem::Get(this))
	{
		Discovery->OnDiscoveryMade.AddDynamic(this, &UDustAudioDirector::HandleDiscoveryMade);
	}

	UE_LOG(LogDustAudio, Log, TEXT("Audio director online. Music budget %.0f%% of a %.0f minute window."),
		MaxMusicDensity * 100.0f, DensityWindowSeconds / 60.0f);
}

void UDustAudioDirector::Deinitialize()
{
	if (UDustDiscoverySubsystem* Discovery = UDustDiscoverySubsystem::Get(this))
	{
		Discovery->OnDiscoveryMade.RemoveDynamic(this, &UDustAudioDirector::HandleDiscoveryMade);
	}

	if (WindBedComponent)
	{
		WindBedComponent->Stop();
		WindBedComponent = nullptr;
	}

	Super::Deinitialize();
}

TStatId UDustAudioDirector::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDustAudioDirector, STATGROUP_Tickables);
}

FGameplayTag UDustAudioDirector::EvaluateDesiredState() const
{
	// Threat first: weather severe enough to be dangerous overrides everything.
	if (const UDustWeatherSubsystem* Weather = UDustWeatherSubsystem::Get(this))
	{
		if (Weather->GetParticulate() > 0.55f)
		{
			return TAG_Dust_Audio_Threat;
		}
	}

	// Approach: something significant is close and visible.
	if (const UDustDiscoverySubsystem* Discovery = UDustDiscoverySubsystem::Get(this))
	{
		const FDustCuriosityCandidate& Best = Discovery->GetBestCuriosityTarget();

		if (Best.bVisible && Best.Poi.Significance > 0.65f && Best.Distance < 30000.0)
		{
			return TAG_Dust_Audio_Approach;
		}

		// Wander only when there is genuinely something to wander toward.
		// In a dead zone we stay silent, which is correct: the game should not
		// paper over emptiness with music.
		if (Discovery->GetCuriosityLevel() > 0.45f)
		{
			return TAG_Dust_Audio_Wander;
		}
	}

	return TAG_Dust_Audio_Silence;
}

bool UDustAudioDirector::RequestCue(FGameplayTag StateTag, float Intensity)
{
	if (DustAudioCVars::IgnoreSilenceBudget != 0)
	{
		ForceCue(StateTag, Intensity);
		return true;
	}

	// Two independent gates. Both exist because they fail differently: the
	// density budget stops the game becoming wall-to-wall over an hour, and the
	// minimum silence stops two cues stacking within a minute of each other.
	if (MusicDensity > MaxMusicDensity)
	{
		DUST_VERBOSE(LogDustAudio,
			TEXT("Cue '%s' declined: music density %.2f exceeds budget %.2f."),
			*StateTag.ToString(), MusicDensity, MaxMusicDensity);
		return false;
	}

	if (SilenceTimer < MinimumSilenceSeconds)
	{
		DUST_VERBOSE(LogDustAudio,
			TEXT("Cue '%s' declined: only %.0fs of silence since the last cue."),
			*StateTag.ToString(), SilenceTimer);
		return false;
	}

	ForceCue(StateTag, Intensity);
	return true;
}

void UDustAudioDirector::ForceCue(FGameplayTag StateTag, float Intensity)
{
	const FGameplayTag Previous = CurrentState;

	CurrentState = StateTag;
	TimeInState = 0.0f;
	SilenceTimer = 0.0f;

	bMusicPlaying = (StateTag != TAG_Dust_Audio_Silence);

	UE_LOG(LogDustAudio, Log, TEXT("Audio state: %s -> %s (intensity %.2f)."),
		*Previous.ToString(), *StateTag.ToString(), Intensity);

	// The actual layer playback is driven by the audio content layer, which
	// subscribes to this state. Keeping playback out of the director means the
	// pacing logic is testable headless, which matters because the silence
	// budget is the sort of thing that is very easy to break by accident.
}

void UDustAudioDirector::HandleDiscoveryMade(FGameplayTag Category, float Significance)
{
	// Only genuinely significant finds earn a reveal. A buried coin does not
	// get a musical sting; the temple you spent forty minutes walking to does.
	if (Significance < 0.6f)
	{
		return;
	}

	// Landmarks and anomalies always get their moment. Everything else has to
	// pass the budget, which means a player who finds six ruins in a row hears
	// music for the first and the sixth, not all six.
	const bool bAlwaysWorthIt =
		Category == TAG_Dust_Discovery_Landmark ||
		Category == TAG_Dust_Discovery_Anomaly;

	if (bAlwaysWorthIt && Significance > 0.85f)
	{
		ForceCue(TAG_Dust_Audio_Reveal, Significance);
	}
	else
	{
		RequestCue(TAG_Dust_Audio_Reveal, Significance);
	}
}

void UDustAudioDirector::UpdateMusicDensity(float DeltaTime)
{
	// Exponential moving average toward 1 while music plays and 0 while it does
	// not, with a time constant of the density window. This approximates
	// "fraction of the last twenty minutes that had music" without storing a
	// history buffer.
	const float Target = bMusicPlaying ? 1.0f : 0.0f;
	const float Alpha = FMath::Clamp(DeltaTime / DensityWindowSeconds, 0.0f, 1.0f);

	MusicDensity = FMath::Lerp(MusicDensity, Target, Alpha);

	if (!bMusicPlaying)
	{
		SilenceTimer += DeltaTime;
	}
}

void UDustAudioDirector::UpdateEnclosure()
{
	const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this);
	const UWorld* GameWorld = GetWorld();
	if (!World || !GameWorld)
	{
		return;
	}

	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GameWorld, 0);
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	const FVector Location = Pawn->GetActorLocation();

	// Sample the terrain skyline in eight directions. The higher the surrounding
	// terrain rises relative to the player, the more enclosed they are. A canyon
	// floor scores high, a dune crest scores near zero - and the reverb follows,
	// with no volumes placed by hand anywhere in the 100 km2.
	constexpr int32 DirectionCount = 8;
	constexpr double ProbeDistance = 12000.0;

	float TotalElevationAngle = 0.0f;

	for (int32 Index = 0; Index < DirectionCount; ++Index)
	{
		const double Angle = (2.0 * PI * Index) / DirectionCount;
		const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);

		// Take the highest point along each ray, not just the endpoint: a near
		// wall matters more than what is behind it.
		double MaxElevationAngle = 0.0;

		for (int32 Step = 1; Step <= 4; ++Step)
		{
			const double Distance = ProbeDistance * (static_cast<double>(Step) / 4.0);
			const FVector Probe = Location + Direction * Distance;

			const double Height = World->SampleHeight(Probe);
			const double Rise = Height - Location.Z;

			if (Rise > 0.0)
			{
				MaxElevationAngle = FMath::Max(MaxElevationAngle, FMath::Atan2(Rise, Distance));
			}
		}

		TotalElevationAngle += static_cast<float>(MaxElevationAngle);
	}

	// Mean elevation angle of the skyline, normalised so that 45 degrees all
	// around reads as fully enclosed.
	const float MeanAngle = TotalElevationAngle / DirectionCount;
	const float TargetEnclosure = FMath::Clamp(MeanAngle / (PI * 0.25f), 0.0f, 1.0f);

	// Slew slowly: walking past a boulder should not flicker the reverb.
	Enclosure = FMath::FInterpTo(Enclosure, TargetEnclosure, 1.0f, 0.5f);
}

void UDustAudioDirector::UpdateWindBed(float DeltaTime)
{
	const UDustWeatherSubsystem* Weather = UDustWeatherSubsystem::Get(this);
	const UWorld* World = GetWorld();
	if (!Weather || !World)
	{
		return;
	}

	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	// Sample the real wind field at the player's ears, so the bed follows the
	// same gusts that move the sand and bend the flora.
	const FDustWindSample Wind = Weather->SampleWind(Pawn->GetActorLocation() + FVector(0, 0, 160.0));

	// Loudness maps from gust speed, not sustained speed: what you hear is the
	// turbulence, and a steady 8 m/s wind is much quieter than a gusting one.
	const float TargetLevel = FMath::Clamp(Wind.GustSpeed / 2400.0f, 0.0f, 1.0f);

	// Enclosure damps the wind: in a canyon or a cave the wind is a distant
	// sound overhead rather than something in your ears.
	const float ShelterDamping = FMath::Lerp(1.0f, 0.25f, Enclosure);

	WindBedLevel = FMath::FInterpTo(WindBedLevel, TargetLevel * ShelterDamping, DeltaTime, 1.6f);

	if (WindBedComponent)
	{
		WindBedComponent->SetVolumeMultiplier(WindBedLevel);

		// Particulate in the air dulls the high end - a sandstorm sounds
		// muffled and close rather than bright and open.
		WindBedComponent->SetFloatParameter(TEXT("Particulate"), Wind.Particulate);
		WindBedComponent->SetFloatParameter(TEXT("Enclosure"), Enclosure);
		WindBedComponent->SetFloatParameter(TEXT("GustSpeed"), Wind.GustSpeed);
	}
}

void UDustAudioDirector::Tick(float DeltaTime)
{
	DUST_SCOPED_TIMER(Dust_Audio_Tick);

	TimeInState += DeltaTime;

	UpdateMusicDensity(DeltaTime);

	// Enclosure is expensive (32 height samples) so it runs at 2 Hz. It changes
	// on the scale of walking into a canyon, which takes many seconds.
	EnclosureAccumulator += DeltaTime;
	if (EnclosureAccumulator >= 0.5f)
	{
		EnclosureAccumulator = 0.0f;
		UpdateEnclosure();
	}

	UpdateWindBed(DeltaTime);

	// State evaluation. We only *request* - the budget may decline, and if it
	// does the correct outcome is silence, not a fallback cue.
	const FGameplayTag Desired = EvaluateDesiredState();

	if (Desired != CurrentState && TimeInState > 12.0f)
	{
		if (Desired == TAG_Dust_Audio_Silence)
		{
			// Returning to silence never needs permission.
			CurrentState = Desired;
			TimeInState = 0.0f;
			bMusicPlaying = false;
		}
		else
		{
			RequestCue(Desired, 0.5f);
		}
	}

	if (DustAudioCVars::DebugAudio != 0)
	{
		static float DebugTimer = 0.0f;
		DebugTimer += DeltaTime;
		if (DebugTimer >= 1.0f)
		{
			DebugTimer = 0.0f;
			UE_LOG(LogDustAudio, Log,
				TEXT("Audio: state '%s' for %.0fs | density %.2f/%.2f%s | silence %.0fs | wind %.2f | enclosure %.2f"),
				*CurrentState.ToString(), TimeInState,
				MusicDensity, MaxMusicDensity,
				IsSilenceEnforced() ? TEXT(" (ENFORCED)") : TEXT(""),
				SilenceTimer, WindBedLevel, Enclosure);
		}
	}
}
