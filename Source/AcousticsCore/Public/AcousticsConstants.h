// Physical and numerical constants shared by every solver (SPEC §2.1, CLAUDE.md "Physics reference").
#pragma once

#include "CoreMinimal.h"

namespace Acoustics
{
	/** Frequency bands carried by every material, histogram and result. v1 is broadband; the 6-octave extension changes this constant only. */
	constexpr int32 NumBands = 1;

	/** Speed of sound, m/s. */
	constexpr double SpeedOfSoundAir = 343.0;
	constexpr double SpeedOfSoundWater = 1480.0;

	/** Receiver sphere radius, m (SPEC §2.2.5). */
	constexpr double DefaultReceiverRadius = 0.5;

	/** Energy histogram defaults: 1 ms bins, 3 s length (SPEC §2.2.8). */
	constexpr double DefaultBinSeconds = 0.001;
	constexpr double DefaultMaxTimeSeconds = 3.0;

	/** Broadband air absorption exponent m in E *= exp(-m d), 1/m (SPEC §2.2.4). */
	constexpr double DefaultAirAbsorptionPerMeter = 0.002;

	/** Stochastic ray tracing defaults (SPEC §2.2): rays per source, reflection-order limit, energy cut-off relative to E0. */
	constexpr int32 DefaultNumRays = 20000;
	constexpr int32 DefaultMaxOrder = 200;
	constexpr double DefaultEnergyThreshold = 1e-6;

	/** Visualization keeps at most this many ray paths (SPEC §3.5). */
	constexpr int32 DefaultMaxPathsForVisualization = 2000;

	/** Impulse-response synthesis (SPEC §2.9): 48 kHz mono, peak -3 dBFS, cut where the Schroeder level drops below -60 dB. */
	constexpr int32 DefaultIRSampleRate = 48000;
	constexpr double DefaultIRPeakDbFS = -3.0;
	constexpr double DefaultIRTruncateBelowDb = -60.0;

	/** UE works in centimeters, the core in meters; conversion happens only at the module boundary. */
	constexpr double CentimetersPerMeter = 100.0;
}
