// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Math/DustNoise.h"

namespace DustNoiseInternal
{
	/** Gradient lookup for 2D. Twelve directions avoids the axis bias of 4 or 8. */
	static FORCEINLINE FVector2f Grad2(uint32 Hash)
	{
		// 12 evenly spaced directions on the unit circle, indexed by 4 bits with
		// a modulo. Precomputed to keep this branch-free and trig-free.
		static const FVector2f Gradients[12] =
		{
			FVector2f( 1.000000f,  0.000000f), FVector2f( 0.866025f,  0.500000f),
			FVector2f( 0.500000f,  0.866025f), FVector2f( 0.000000f,  1.000000f),
			FVector2f(-0.500000f,  0.866025f), FVector2f(-0.866025f,  0.500000f),
			FVector2f(-1.000000f,  0.000000f), FVector2f(-0.866025f, -0.500000f),
			FVector2f(-0.500000f, -0.866025f), FVector2f( 0.000000f, -1.000000f),
			FVector2f( 0.500000f, -0.866025f), FVector2f( 0.866025f, -0.500000f),
		};
		return Gradients[Hash % 12];
	}

	/** Gradient lookup for 3D: the 12 edge-midpoint vectors of a cube. */
	static FORCEINLINE FVector Grad3(uint32 Hash)
	{
		static const FVector Gradients[12] =
		{
			FVector( 1,  1,  0), FVector(-1,  1,  0), FVector( 1, -1,  0), FVector(-1, -1,  0),
			FVector( 1,  0,  1), FVector(-1,  0,  1), FVector( 1,  0, -1), FVector(-1,  0, -1),
			FVector( 0,  1,  1), FVector( 0, -1,  1), FVector( 0,  1, -1), FVector( 0, -1, -1),
		};
		return Gradients[Hash % 12];
	}

	/** Floor that matches on all platforms for negative inputs. */
	static FORCEINLINE int32 FastFloor(float V)
	{
		const int32 I = static_cast<int32>(V);
		return (V < static_cast<float>(I)) ? I - 1 : I;
	}
}

float FDustNoise::Gradient2D(FVector2f P, uint32 Seed)
{
	using namespace DustNoiseInternal;

	const int32 X0 = FastFloor(P.X);
	const int32 Y0 = FastFloor(P.Y);
	const int32 X1 = X0 + 1;
	const int32 Y1 = Y0 + 1;

	const float FX = P.X - static_cast<float>(X0);
	const float FY = P.Y - static_cast<float>(Y0);

	const float U = Quintic(FX);
	const float V = Quintic(FY);

	// Dot each corner gradient with the vector from that corner to P.
	const float N00 = FVector2f::DotProduct(Grad2(Hash2(X0, Y0, Seed)), FVector2f(FX,        FY));
	const float N10 = FVector2f::DotProduct(Grad2(Hash2(X1, Y0, Seed)), FVector2f(FX - 1.0f, FY));
	const float N01 = FVector2f::DotProduct(Grad2(Hash2(X0, Y1, Seed)), FVector2f(FX,        FY - 1.0f));
	const float N11 = FVector2f::DotProduct(Grad2(Hash2(X1, Y1, Seed)), FVector2f(FX - 1.0f, FY - 1.0f));

	const float NX0 = FMath::Lerp(N00, N10, U);
	const float NX1 = FMath::Lerp(N01, N11, U);

	// sqrt(2) normalisation brings the theoretical range to ~[-1,1].
	return FMath::Lerp(NX0, NX1, V) * 1.4142136f;
}

float FDustNoise::Gradient3D(FVector P, uint32 Seed)
{
	using namespace DustNoiseInternal;

	const int32 X0 = FastFloor(static_cast<float>(P.X));
	const int32 Y0 = FastFloor(static_cast<float>(P.Y));
	const int32 Z0 = FastFloor(static_cast<float>(P.Z));

	const float FX = static_cast<float>(P.X) - static_cast<float>(X0);
	const float FY = static_cast<float>(P.Y) - static_cast<float>(Y0);
	const float FZ = static_cast<float>(P.Z) - static_cast<float>(Z0);

	const float U = Quintic(FX);
	const float V = Quintic(FY);
	const float W = Quintic(FZ);

	auto Corner = [&](int32 DX, int32 DY, int32 DZ) -> float
	{
		const FVector Offset(FX - DX, FY - DY, FZ - DZ);
		return static_cast<float>(FVector::DotProduct(Grad3(Hash3(X0 + DX, Y0 + DY, Z0 + DZ, Seed)), Offset));
	};

	const float N000 = Corner(0, 0, 0);
	const float N100 = Corner(1, 0, 0);
	const float N010 = Corner(0, 1, 0);
	const float N110 = Corner(1, 1, 0);
	const float N001 = Corner(0, 0, 1);
	const float N101 = Corner(1, 0, 1);
	const float N011 = Corner(0, 1, 1);
	const float N111 = Corner(1, 1, 1);

	const float NX00 = FMath::Lerp(N000, N100, U);
	const float NX10 = FMath::Lerp(N010, N110, U);
	const float NX01 = FMath::Lerp(N001, N101, U);
	const float NX11 = FMath::Lerp(N011, N111, U);

	const float NXY0 = FMath::Lerp(NX00, NX10, V);
	const float NXY1 = FMath::Lerp(NX01, NX11, V);

	return FMath::Lerp(NXY0, NXY1, W) * 1.1547005f; // 2/sqrt(3)
}

float FDustNoise::Simplex2D(FVector2f P, uint32 Seed)
{
	using namespace DustNoiseInternal;

	// Skew factors for the 2D simplex lattice.
	static const float F2 = 0.3660254f; // (sqrt(3) - 1) / 2
	static const float G2 = 0.2113249f; // (3 - sqrt(3)) / 6

	const float Skew = (P.X + P.Y) * F2;
	const int32 I = FastFloor(P.X + Skew);
	const int32 J = FastFloor(P.Y + Skew);

	const float Unskew = static_cast<float>(I + J) * G2;
	const float X0 = P.X - (static_cast<float>(I) - Unskew);
	const float Y0 = P.Y - (static_cast<float>(J) - Unskew);

	// Which of the two triangles in the rhombus are we in?
	const int32 I1 = (X0 > Y0) ? 1 : 0;
	const int32 J1 = (X0 > Y0) ? 0 : 1;

	const float X1 = X0 - static_cast<float>(I1) + G2;
	const float Y1 = Y0 - static_cast<float>(J1) + G2;
	const float X2 = X0 - 1.0f + 2.0f * G2;
	const float Y2 = Y0 - 1.0f + 2.0f * G2;

	auto Contribution = [&](float DX, float DY, int32 GI, int32 GJ) -> float
	{
		float T = 0.5f - DX * DX - DY * DY;
		if (T <= 0.0f)
		{
			return 0.0f;
		}
		T *= T;
		const FVector2f G = Grad2(Hash2(GI, GJ, Seed));
		return T * T * (G.X * DX + G.Y * DY);
	};

	const float N0 = Contribution(X0, Y0, I, J);
	const float N1 = Contribution(X1, Y1, I + I1, J + J1);
	const float N2 = Contribution(X2, Y2, I + 1, J + 1);

	// 70 is the standard scale that brings simplex output to ~[-1,1].
	return 70.0f * (N0 + N1 + N2);
}

float FDustNoise::FBM2D(FVector2f P, uint32 Seed, const FFractalParams& Params)
{
	float Sum = 0.0f;
	float Amplitude = 1.0f;
	float Frequency = Params.Frequency;
	float Normaliser = 0.0f;

	for (int32 Octave = 0; Octave < Params.Octaves; ++Octave)
	{
		Sum += Simplex2D(P * Frequency, Seed + static_cast<uint32>(Octave) * 1013u) * Amplitude;
		Normaliser += Amplitude;

		Frequency *= Params.Lacunarity;
		Amplitude *= Params.Gain;
	}

	return (Normaliser > 0.0f) ? (Sum / Normaliser) * Params.Amplitude : 0.0f;
}

float FDustNoise::FBM3D(FVector P, uint32 Seed, const FFractalParams& Params)
{
	float Sum = 0.0f;
	float Amplitude = 1.0f;
	float Frequency = Params.Frequency;
	float Normaliser = 0.0f;

	for (int32 Octave = 0; Octave < Params.Octaves; ++Octave)
	{
		Sum += Gradient3D(P * Frequency, Seed + static_cast<uint32>(Octave) * 1013u) * Amplitude;
		Normaliser += Amplitude;

		Frequency *= Params.Lacunarity;
		Amplitude *= Params.Gain;
	}

	return (Normaliser > 0.0f) ? (Sum / Normaliser) * Params.Amplitude : 0.0f;
}

float FDustNoise::Ridged2D(FVector2f P, uint32 Seed, const FFractalParams& Params)
{
	float Sum = 0.0f;
	float Amplitude = 1.0f;
	float Frequency = Params.Frequency;
	float Normaliser = 0.0f;

	// Ridged multifractal: fold the signal about zero, invert, then weight each
	// octave by the previous one so ridges stay sharp and valleys stay smooth.
	float PreviousOctave = 1.0f;

	for (int32 Octave = 0; Octave < Params.Octaves; ++Octave)
	{
		const float Raw = Simplex2D(P * Frequency, Seed + static_cast<uint32>(Octave) * 1013u);
		float Signal = 1.0f - FMath::Abs(Raw);
		Signal *= Signal;
		Signal *= PreviousOctave;

		// Weight for the next octave, clamped so the fractal cannot run away.
		PreviousOctave = FMath::Clamp(Signal * 2.0f, 0.0f, 1.0f);

		Sum += Signal * Amplitude;
		Normaliser += Amplitude;

		Frequency *= Params.Lacunarity;
		Amplitude *= Params.Gain;
	}

	return (Normaliser > 0.0f) ? FMath::Clamp((Sum / Normaliser) * Params.Amplitude, 0.0f, 1.0f) : 0.0f;
}

float FDustNoise::Billow2D(FVector2f P, uint32 Seed, const FFractalParams& Params)
{
	float Sum = 0.0f;
	float Amplitude = 1.0f;
	float Frequency = Params.Frequency;
	float Normaliser = 0.0f;

	for (int32 Octave = 0; Octave < Params.Octaves; ++Octave)
	{
		const float Raw = Simplex2D(P * Frequency, Seed + static_cast<uint32>(Octave) * 1013u);
		Sum += FMath::Abs(Raw) * Amplitude;
		Normaliser += Amplitude;

		Frequency *= Params.Lacunarity;
		Amplitude *= Params.Gain;
	}

	return (Normaliser > 0.0f) ? FMath::Clamp((Sum / Normaliser) * Params.Amplitude, 0.0f, 1.0f) : 0.0f;
}

float FDustNoise::WarpedFBM2D(FVector2f P, uint32 Seed, const FFractalParams& Params, float WarpStrength)
{
	// Two-level warp. One level reads as "wobbly"; two reads as erosion.
	FFractalParams WarpParams = Params;
	WarpParams.Octaves = FMath::Max(2, Params.Octaves - 2);

	const uint32 WarpSeedA = Seed ^ 0x5F3759DFu;
	const uint32 WarpSeedB = Seed ^ 0xA1B2C3D4u;

	const FVector2f Q(
		FBM2D(P, WarpSeedA, WarpParams),
		FBM2D(P + FVector2f(5137.0f, 1731.0f), WarpSeedA, WarpParams));

	const FVector2f R(
		FBM2D(P + Q * WarpStrength, WarpSeedB, WarpParams),
		FBM2D(P + Q * WarpStrength + FVector2f(2791.0f, 8431.0f), WarpSeedB, WarpParams));

	return FBM2D(P + R * WarpStrength, Seed, Params);
}

FDustNoise::FCellularResult FDustNoise::Cellular2D(FVector2f P, uint32 Seed, float Jitter)
{
	using namespace DustNoiseInternal;

	FCellularResult Result;
	Result.F1 = TNumericLimits<float>::Max();
	Result.F2 = TNumericLimits<float>::Max();

	const int32 CX = FastFloor(P.X);
	const int32 CY = FastFloor(P.Y);

	Jitter = FMath::Clamp(Jitter, 0.0f, 1.0f);

	// 3x3 neighbourhood is sufficient for jitter <= 1.0.
	for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
	{
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			const int32 NX = CX + OffsetX;
			const int32 NY = CY + OffsetY;

			const uint32 H = Hash2(NX, NY, Seed);

			// Two independent offsets inside the cell.
			const float JX = HashToUnit(H) - 0.5f;
			const float JY = HashToUnit(Hash(H ^ 0x68BC21EBu)) - 0.5f;

			const FVector2f Feature(
				static_cast<float>(NX) + 0.5f + JX * Jitter,
				static_cast<float>(NY) + 0.5f + JY * Jitter);

			const float Dist = (Feature - P).Size();

			if (Dist < Result.F1)
			{
				Result.F2 = Result.F1;
				Result.F1 = Dist;
				Result.Cell = FIntPoint(NX, NY);
				Result.FeaturePoint = Feature;
			}
			else if (Dist < Result.F2)
			{
				Result.F2 = Dist;
			}
		}
	}

	return Result;
}

float FDustNoise::CellularEdges2D(FVector2f P, uint32 Seed, float EdgeWidth)
{
	const FCellularResult Cell = Cellular2D(P, Seed);
	const float Edge = Cell.EdgeDistance();

	// 1 on the border, falling to 0 over EdgeWidth.
	return 1.0f - FMath::SmoothStep(0.0f, FMath::Max(EdgeWidth, KINDA_SMALL_NUMBER), Edge);
}

uint32 FDustNoise::DeriveSeed(uint32 MasterSeed, const TCHAR* Channel)
{
	// FNV-1a over the channel name, mixed with the master seed. Stable across
	// platforms because it operates on UTF-32 code units explicitly.
	uint32 H = 2166136261u;

	for (const TCHAR* C = Channel; C && *C; ++C)
	{
		H ^= static_cast<uint32>(*C);
		H *= 16777619u;
	}

	return Hash(H ^ MasterSeed);
}
