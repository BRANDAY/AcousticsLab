// Impulse-response synthesis from an energy histogram (SPEC §2.9): noise per bin shaped by sqrt(E), exact impulses.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "AcousticsConstants.h"

struct FEnergyHistogram;
struct FReceiverResult;

/** A deterministic contribution (direct sound, an early reflection from beams or image sources): energy at an exact delay. */
struct ACOUSTICSCORE_API FIRImpulse
{
	double TimeSeconds = 0.0;

	/** Band-0 energy relative to the emitted energy (the same scale as the histogram). */
	double Energy = 0.0;

	FIRImpulse() = default;
	FIRImpulse(double InTimeSeconds, double InEnergy)
		: TimeSeconds(InTimeSeconds)
		, Energy(InEnergy)
	{
	}
};

struct ACOUSTICSCORE_API FIRSynthesisSettings
{
	int32 SampleRate = Acoustics::DefaultIRSampleRate;

	/** Seed of the per-bin noise streams; the same seed reproduces the samples bit for bit. */
	uint32 Seed = 1;

	/** Peak of the normalized response, dBFS (SPEC: -3). */
	double PeakDbFS = Acoustics::DefaultIRPeakDbFS;

	/** The response ends at the first bin where the Schroeder curve of the histogram falls below this level (SPEC: -60 dB). */
	double TruncateBelowDb = Acoustics::DefaultIRTruncateBelowDb;

	/** Hard length limit, s; 0 = the histogram length (T_max). */
	double MaxSeconds = 0.0;

	bool bNormalize = true;

	bool Validate(FString& OutError) const;
};

/** Mono response plus what the synthesis did, for logging and tests. */
struct ACOUSTICSCORE_API FSynthesizedIR
{
	int32 SampleRate = 0;
	TArray<float> Samples;

	double BinSeconds = 0.0;
	int32 NumBinsUsed = 0;

	/** Length before the sample grid: NumBinsUsed * BinSeconds. */
	double TruncationSeconds = 0.0;

	/** True when the Schroeder level cut the response, false when MaxSeconds or the histogram end did. */
	bool bTruncatedByLevel = false;

	double PeakBeforeNormalization = 0.0;

	/** Sum of squares before normalization: the energy the histogram put into the kept bins. */
	double EnergyBeforeNormalization = 0.0;

	double NormalizationGain = 1.0;
	int32 NumImpulses = 0;

	bool IsValid() const { return SampleRate > 0 && Samples.Num() > 0; }
	double DurationSeconds() const { return SampleRate > 0 ? static_cast<double>(Samples.Num()) / SampleRate : 0.0; }

	/** Sum of squares of the samples as they are. */
	double Energy() const;
	double Peak() const;
	double PeakDbFS() const;

	FString Describe() const;
};

/**
 * Every histogram bin becomes a burst of uniform white noise (PCG32 stream (Seed, bin), values in [-1, 1)) scaled so that
 * the sum of squares over the bin's samples equals the bin energy exactly; deterministic impulses are single samples of
 * amplitude sqrt(E) at round(t * fs), and their energy is removed from the bin they land in (the ray tracer already deposits
 * the direct sound in the histogram, SPEC §2.2.7). The response is cut at the first bin whose Schroeder level is below
 * TruncateBelowDb, never beyond MaxSeconds or the histogram, and peak-normalized to PeakDbFS.
 * Band 0 only: with several bands every band's noise would have to be band-pass filtered before summing (TODO for NumBands > 1).
 * Deterministic for fixed inputs; no threads.
 */
class ACOUSTICSCORE_API FIRSynthesizer
{
public:
	static FSynthesizedIR Synthesize(const FEnergyHistogram& Histogram, TConstArrayView<FIRImpulse> Impulses, const FIRSynthesisSettings& Settings, int32 Band = 0);

	/** Histogram of the receiver plus its direct sound as the only deterministic impulse (when not occluded). */
	static FSynthesizedIR SynthesizeForReceiver(const FReceiverResult& Receiver, const FIRSynthesisSettings& Settings, int32 Band = 0);

	/**
	 * Bins kept: up to the first bin whose Schroeder level (RoomAcoustics::SchroederCurveDb) lies below TruncateBelowDb,
	 * capped by MaxSeconds (> 0) and by the last non-empty bin. 0 for an empty histogram.
	 */
	static int32 ComputeNumBins(const FEnergyHistogram& Histogram, int32 Band, double TruncateBelowDb, double MaxSeconds, bool* bOutTruncatedByLevel = nullptr);

	/** Sample range [OutFirst, OutEnd) of a bin on the sample grid; adjacent bins share their boundary. */
	static void BinSampleRange(double BinSeconds, int32 Bin, int32 SampleRate, int32& OutFirst, int32& OutEnd);

	/** Sum of squares of the samples per histogram bin (NumBinsUsed entries), for tests and plots. */
	static void EnergyPerBin(const FSynthesizedIR& IR, TArray<double>& OutEnergy);

	static double DbFSToLinear(double Db) { return FMath::Pow(10.0, Db / 20.0); }
	static double LinearToDbFS(double Amplitude);
};
