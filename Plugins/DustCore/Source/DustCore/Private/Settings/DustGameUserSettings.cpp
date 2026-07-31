// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Settings/DustGameUserSettings.h"

#include "DustLog.h"
#include "Engine/Engine.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"

namespace DustSettings
{
	static void SetCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByGameSetting);
		}
	}

	static void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByGameSetting);
		}
	}

	static bool CVarExists(const TCHAR* Name)
	{
		return IConsoleManager::Get().FindConsoleVariable(Name) != nullptr;
	}

	/**
	 * Screen percentage per quality tier. These are the vendor-recommended
	 * ratios; DLSS/FSR/XeSS each map their own preset enum onto the same
	 * numbers, so we keep one table and let each backend interpret it.
	 */
	static float ScreenPercentageForQuality(EDustUpscalerQuality Quality)
	{
		switch (Quality)
		{
		case EDustUpscalerQuality::UltraPerformance: return 33.0f;
		case EDustUpscalerQuality::Performance:      return 50.0f;
		case EDustUpscalerQuality::Balanced:         return 58.0f;
		case EDustUpscalerQuality::Quality:          return 66.0f;
		case EDustUpscalerQuality::UltraQuality:     return 77.0f;
		case EDustUpscalerQuality::DynamicResolution:
		default:                                     return 0.0f; // governor-driven
		}
	}
}

UDustGameUserSettings::UDustGameUserSettings()
{
	SetToDefaults();
}

UDustGameUserSettings* UDustGameUserSettings::Get()
{
	return GEngine ? Cast<UDustGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UDustGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();

	Upscaler         = EDustUpscaler::TSR;
	UpscalerQuality  = EDustUpscalerQuality::Quality;
	TargetFrameRate  = 60;
	HDRDisplayNits   = 1000;

	FirstPersonFOV   = 95.0f;
	ThirdPersonFOV   = 80.0f;
	HeadBobScale     = 0.65f;
	MotionComfort    = EDustMotionComfort::Cinematic;
	MotionBlurAmount = 0.35f;

	HUDPersistence   = 0.0f;
	SubtitleScale    = 1.0f;
}

void UDustGameUserSettings::LoadSettings(bool bForceReload)
{
	Super::LoadSettings(bForceReload);

	// A saved upscaler may not exist on this machine - the player moved their
	// drive to an AMD card, or a driver was rolled back. Fall back rather than
	// booting into a black screen.
	const TArray<EDustUpscaler> Supported = GetSupportedUpscalers();
	if (!Supported.Contains(Upscaler))
	{
		UE_LOG(LogDust, Warning,
			TEXT("Saved upscaler %d unavailable on this hardware; falling back to TSR."),
			static_cast<int32>(Upscaler));
		Upscaler = EDustUpscaler::TSR;
	}
}

void UDustGameUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
	Super::ApplySettings(bCheckForCommandLineOverrides);

	ApplyUpscalerSettings();
	ApplyCameraSettings();
}

void UDustGameUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();

	ApplyUpscalerSettings();
	ApplyCameraSettings();

	// Frame rate target feeds both the engine cap and the governor. We set the
	// engine cap slightly above the target so the governor, not the frame
	// limiter, is what the player experiences as "smooth".
	if (TargetFrameRate > 0)
	{
		SetFrameRateLimit(static_cast<float>(TargetFrameRate));
	}
	else
	{
		SetFrameRateLimit(0.0f);
	}
}

TArray<EDustUpscaler> UDustGameUserSettings::GetSupportedUpscalers() const
{
	using namespace DustSettings;

	TArray<EDustUpscaler> Result;

	// Always available: these are engine features, not vendor plugins.
	Result.Add(EDustUpscaler::None);
	Result.Add(EDustUpscaler::TAAU);
	Result.Add(EDustUpscaler::TSR);

	// Vendor upscalers are detected by the presence of their plugin's console
	// variable, which is the only detection that survives the plugin being
	// absent from the build entirely.
	if (CVarExists(TEXT("r.NGX.DLSS.Enable")))
	{
		Result.Add(EDustUpscaler::DLSS);
	}
	if (CVarExists(TEXT("r.FidelityFX.FSR3.Enabled")) || CVarExists(TEXT("r.FidelityFX.FI.Enabled")))
	{
		Result.Add(EDustUpscaler::FSR);
	}
	if (CVarExists(TEXT("r.XeSS.Enabled")))
	{
		Result.Add(EDustUpscaler::XeSS);
	}

	return Result;
}

void UDustGameUserSettings::SetUpscaler(EDustUpscaler InUpscaler)
{
	if (Upscaler == InUpscaler)
	{
		return;
	}

	if (!GetSupportedUpscalers().Contains(InUpscaler))
	{
		UE_LOG(LogDust, Warning, TEXT("Requested unsupported upscaler %d; ignoring."), static_cast<int32>(InUpscaler));
		return;
	}

	Upscaler = InUpscaler;
	ApplyUpscalerSettings();
}

void UDustGameUserSettings::SetUpscalerQuality(EDustUpscalerQuality InQuality)
{
	if (UpscalerQuality == InQuality)
	{
		return;
	}

	UpscalerQuality = InQuality;
	ApplyUpscalerSettings();
}

void UDustGameUserSettings::ApplyUpscalerSettings()
{
	using namespace DustSettings;

	// Reset every backend first so switching never leaves two upscalers armed.
	SetCVar(TEXT("r.NGX.DLSS.Enable"), 0);
	SetCVar(TEXT("r.FidelityFX.FSR3.Enabled"), 0);
	SetCVar(TEXT("r.XeSS.Enabled"), 0);

	const float ScreenPercentage = ScreenPercentageForQuality(UpscalerQuality);
	const bool bDynamic = (UpscalerQuality == EDustUpscalerQuality::DynamicResolution);

	// Dynamic resolution and a fixed screen percentage are mutually exclusive;
	// the governor owns the former.
	SetCVar(TEXT("r.DynamicRes.OperationMode"), bDynamic ? 2 : 0);
	if (!bDynamic)
	{
		SetCVar(TEXT("r.ScreenPercentage"), ScreenPercentage);
	}

	switch (Upscaler)
	{
	case EDustUpscaler::None:
		SetCVar(TEXT("r.AntiAliasingMethod"), 2); // TAA without upsampling
		SetCVar(TEXT("r.ScreenPercentage"), 100.0f);
		break;

	case EDustUpscaler::TAAU:
		SetCVar(TEXT("r.AntiAliasingMethod"), 2);
		SetCVar(TEXT("r.TemporalAA.Upsampling"), 1);
		break;

	case EDustUpscaler::TSR:
		SetCVar(TEXT("r.AntiAliasingMethod"), 4);
		// History rejection tuned for a world that is mostly slow parallax with
		// bursts of fast particulate. The default is too eager to reject dust.
		SetCVar(TEXT("r.TSR.History.ScreenPercentage"), 200.0f);
		SetCVar(TEXT("r.TSR.ShadingRejection.Flickering"), 1);
		break;

	case EDustUpscaler::DLSS:
		SetCVar(TEXT("r.AntiAliasingMethod"), 4);
		SetCVar(TEXT("r.NGX.DLSS.Enable"), 1);
		SetCVar(TEXT("r.NGX.DLSS.Quality"), static_cast<int32>(UpscalerQuality));
		break;

	case EDustUpscaler::FSR:
		SetCVar(TEXT("r.FidelityFX.FSR3.Enabled"), 1);
		SetCVar(TEXT("r.FidelityFX.FSR3.QualityMode"), static_cast<int32>(UpscalerQuality));
		break;

	case EDustUpscaler::XeSS:
		SetCVar(TEXT("r.XeSS.Enabled"), 1);
		SetCVar(TEXT("r.XeSS.Quality"), static_cast<int32>(UpscalerQuality));
		break;
	}

	UE_LOG(LogDust, Log, TEXT("Upscaler set to %d at quality %d (%.0f%% screen)."),
		static_cast<int32>(Upscaler), static_cast<int32>(UpscalerQuality), ScreenPercentage);
}

void UDustGameUserSettings::ApplyCameraSettings()
{
	using namespace DustSettings;

	// Motion comfort scales the procedural camera systems. The camera component
	// reads these rather than the enum so a single float drives everything.
	float ComfortScale = 1.0f;
	switch (MotionComfort)
	{
	case EDustMotionComfort::Cinematic: ComfortScale = 1.0f;  break;
	case EDustMotionComfort::Reduced:   ComfortScale = 0.45f; break;
	case EDustMotionComfort::Off:       ComfortScale = 0.0f;  break;
	}

	SetCVar(TEXT("dust.Camera.ComfortScale"), ComfortScale);
	SetCVar(TEXT("dust.Camera.HeadBobScale"), HeadBobScale * ComfortScale);
	SetCVar(TEXT("r.MotionBlurQuality"), MotionBlurAmount > 0.0f ? 4 : 0);
	SetCVar(TEXT("r.DefaultFeature.MotionBlur"), MotionBlurAmount > 0.0f ? 1 : 0);
	SetCVar(TEXT("r.MotionBlur.Amount"), MotionBlurAmount);

	if (IsHDREnabled())
	{
		SetCVar(TEXT("r.HDR.Display.OutputDevice"), 3); // ST-2084 / PQ (Rec.2020)
		SetCVar(TEXT("r.HDR.Display.ColorGamut"), 2);
		SetCVar(TEXT("r.HDR.Display.MaxLuminance"), HDRDisplayNits);
	}
}

void UDustGameUserSettings::SetTargetFrameRate(int32 InFrameRate)
{
	TargetFrameRate = FMath::Max(0, InFrameRate);
	SetFrameRateLimit(static_cast<float>(TargetFrameRate));
}

void UDustGameUserSettings::SetHDRDisplayNits(int32 Nits)
{
	HDRDisplayNits = FMath::Clamp(Nits, 400, 4000);
	ApplyCameraSettings();
}

void UDustGameUserSettings::SetFirstPersonFOV(float Degrees)
{
	FirstPersonFOV = FMath::Clamp(Degrees, 70.0f, 120.0f);
}

void UDustGameUserSettings::SetThirdPersonFOV(float Degrees)
{
	ThirdPersonFOV = FMath::Clamp(Degrees, 60.0f, 110.0f);
}

void UDustGameUserSettings::SetHeadBobScale(float Scale)
{
	HeadBobScale = FMath::Clamp(Scale, 0.0f, 1.0f);
	ApplyCameraSettings();
}

void UDustGameUserSettings::SetMotionComfort(EDustMotionComfort InComfort)
{
	MotionComfort = InComfort;
	ApplyCameraSettings();
}

void UDustGameUserSettings::SetMotionBlurAmount(float Amount)
{
	MotionBlurAmount = FMath::Clamp(Amount, 0.0f, 1.0f);
	ApplyCameraSettings();
}

void UDustGameUserSettings::SetHUDPersistence(float InPersistence)
{
	HUDPersistence = FMath::Clamp(InPersistence, 0.0f, 1.0f);
}

void UDustGameUserSettings::SetSubtitleScale(float Scale)
{
	SubtitleScale = FMath::Clamp(Scale, 0.75f, 2.5f);
}

float UDustGameUserSettings::GetAspectCorrectedFOV(float BaseHorizontalFOV, float AspectRatio) const
{
	// Hor+ : hold the vertical FOV of 16:9 constant and widen horizontally.
	// Without this, a 32:9 player would see *less* vertically than a 16:9
	// player, which on a game about horizons is exactly backwards.
	constexpr float ReferenceAspect = 16.0f / 9.0f;

	if (FMath::IsNearlyEqual(AspectRatio, ReferenceAspect, 0.01f))
	{
		return BaseHorizontalFOV;
	}

	const float HalfBaseRad = FMath::DegreesToRadians(BaseHorizontalFOV * 0.5f);
	const float VerticalTan = FMath::Tan(HalfBaseRad) / ReferenceAspect;
	const float CorrectedHalfRad = FMath::Atan(VerticalTan * AspectRatio);

	// Cap the widening: past ~130 degrees the perspective distortion at the
	// screen edges becomes a nausea problem rather than a feature.
	return FMath::Min(FMath::RadiansToDegrees(CorrectedHalfRad) * 2.0f, 130.0f);
}

bool UDustGameUserSettings::IsHandheldProfile()
{
	// Valve sets SteamDeck=1 in the environment on Deck hardware; the device
	// profile name is the reliable fallback for other handhelds we profile for.
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("SteamDeck")) == TEXT("1"))
	{
		return true;
	}

	const FString DeviceProfile = FPlatformProperties::PlatformName();
	return DeviceProfile.Contains(TEXT("SteamDeck")) || DeviceProfile.Contains(TEXT("Handheld"));
}

void UDustGameUserSettings::ApplyAutoDetectedProfile()
{
	if (IsHandheldProfile())
	{
		// 800p60 with FSR Balanced is the shipping Deck target. Verified
		// against the worst case in the game: the sandstorm crossing of the
		// Screaming Flat with full particulate and volumetric fog.
		SetScreenResolution(FIntPoint(1280, 800));
		SetFullscreenMode(EWindowMode::Fullscreen);
		SetOverallScalabilityLevel(1);

		const TArray<EDustUpscaler> Supported = GetSupportedUpscalers();
		SetUpscaler(Supported.Contains(EDustUpscaler::FSR) ? EDustUpscaler::FSR : EDustUpscaler::TSR);
		SetUpscalerQuality(EDustUpscalerQuality::Balanced);
		SetTargetFrameRate(60);
		SetMotionBlurAmount(0.0f);

		UE_LOG(LogDust, Log, TEXT("Applied handheld profile: 1280x800, FSR Balanced, 60 FPS target."));
	}
	else
	{
		// RunHardwareBenchmark writes into the scalability groups; we then sit
		// on top with our own upscaler choice, which the benchmark knows nothing about.
		RunHardwareBenchmark();
		ApplyHardwareBenchmarkResults();

		SetUpscaler(EDustUpscaler::TSR);
		SetUpscalerQuality(EDustUpscalerQuality::Quality);
		SetTargetFrameRate(60);

		UE_LOG(LogDust, Log, TEXT("Applied benchmarked desktop profile at scalability level %d."),
			GetOverallScalabilityLevel());
	}

	ApplySettings(/*bCheckForCommandLineOverrides=*/false);
	SaveSettings();
}
