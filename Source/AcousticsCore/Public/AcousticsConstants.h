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

	/** UE works in centimeters, the core in meters; conversion happens only at the module boundary. */
	constexpr double CentimetersPerMeter = 100.0;
}
