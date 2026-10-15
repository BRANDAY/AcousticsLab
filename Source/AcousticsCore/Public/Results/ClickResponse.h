// Impulse response from a recorded click (SPEC §3.8, M9 optional): the industry plugins expose no impulse-response API,
// so a 1-sample click is played through the plugin source and the main submix is recorded; this turns the recording into
// an energy histogram and the SPEC §2.3 metrics, comparable with the solver results.
#pragma once

#include "CoreMinimal.h"
#include "Results/SimulationResult.h"

struct ACOUSTICSCORE_API FClickResponseSettings
{
	/** Onset = first frame whose energy reaches this level relative to the loudest frame, dB. */
	double OnsetThresholdDb = -40.0;

	/** Source-receiver distance, m; the onset is placed at DirectDistance / SpeedOfSound (the solvers' time origin). */
	double DirectDistance = 0.0;
	double SpeedOfSound = Acoustics::SpeedOfSoundAir;

	/** Energy within this window after the onset counts as the direct sound (the plugins' HRTF smears the click over a few ms). */
	double DirectWindowSeconds = 0.0025;

	double BinSeconds = Acoustics::DefaultBinSeconds;
	double MaxTimeSeconds = Acoustics::DefaultMaxTimeSeconds;
};

struct ACOUSTICSCORE_API FClickResponse
{
	/**
	 * Interleaved float recording (any channel count) -> Out (Name / Position are left to the caller): energy per frame =
	 * sum of the squared channels, frames before the onset dropped (latency of the audio pipeline), frame n deposited at
	 * t = d / c + (n - onset) / fs, metrics by FRoomMetrics::Compute with the direct arrival d / c. False for a silent
	 * recording or invalid arguments (OutError says why).
	 */
	static bool Analyze(TConstArrayView<float> Interleaved, int32 NumChannels, int32 SampleRate, const FClickResponseSettings& Settings,
		FReceiverResult& Out, FString& OutError, int32* OutOnsetFrame = nullptr);
};
