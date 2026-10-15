// Room-acoustic metrics from an energy histogram (SPEC §2.3) and the Sabine / Eyring references.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "AcousticsConstants.h"

struct FEnergyHistogram;

/** Metrics of one band. Decay times carry a validity flag: the Schroeder curve must reach the end of the fit range. */
struct ACOUSTICSCORE_API FBandMetrics
{
	/** Early decay time: 0 -> -10 dB, times 6. */
	double EDT = 0.0;
	/** -5 -> -25 dB, times 3. */
	double T20 = 0.0;
	/** -5 -> -35 dB, times 2. */
	double T30 = 0.0;
	bool bEDTValid = false;
	bool bT20Valid = false;
	bool bT30Valid = false;

	/** Clarity 10 log10(early / late) with 50 ms and 80 ms after the direct sound, dB. */
	double C50 = 0.0;
	double C80 = 0.0;
	/** Definition: early 50 ms energy over total. */
	double D50 = 0.0;
	/** Center time, s after the direct sound. */
	double Ts = 0.0;

	double TotalEnergy = 0.0;
	double EarlyEnergy50 = 0.0;
	double EarlyEnergy80 = 0.0;

	/** How far the Schroeder curve falls before the energy runs out (0 for an empty histogram), dB. */
	double DecayRangeDb = 0.0;

	FString ToString() const;
};

struct ACOUSTICSCORE_API FRoomMetrics
{
	FBandMetrics Bands[Acoustics::NumBands];

	/** Time origin of the clarity / definition / center-time windows. */
	double DirectArrivalSeconds = 0.0;

	/** At least one band had energy. */
	bool bValid = false;

	/**
	 * Metrics of every band. DirectArrivalSeconds < 0 means "unknown": the start of the first non-empty bin
	 * (band 0) is used, which is what a synthetic decay starting at t = 0 expects.
	 */
	static FRoomMetrics Compute(const FEnergyHistogram& Histogram, double DirectArrivalSeconds = -1.0);

	const FBandMetrics& Band(int32 Index = 0) const { return Bands[Index]; }
	FString ToString() const;
};

namespace RoomAcoustics
{
	/** Level assigned to bins where the backward integral is zero (no energy left). */
	constexpr double SilenceDb = -1000.0;

	constexpr double EarlyWindow50 = 0.050;
	constexpr double EarlyWindow80 = 0.080;

	/** Sabine's constant 24 ln(10) / c for air at 20 C, s/m. */
	constexpr double SabineConstant = 0.161;

	/** Clarity returned when one side of the ratio has no energy. */
	constexpr double ClarityLimitDb = 100.0;

	/**
	 * The backward integral always plunges where the energy stops (truncation), so the last TruncationGuardSeconds
	 * before the final non-empty bin are not used: neither for the fits nor for the usable decay range.
	 */
	constexpr double TruncationGuardSeconds = 0.050;

	/**
	 * A fit is valid only when the usable curve continues this far below the end of its range (ISO 3382 asks for
	 * 10 dB of headroom above the noise floor); it keeps the truncation plunge out of the regression samples.
	 */
	constexpr double ValidityMarginDb = 10.0;

	/** Schroeder backward integration L(t) = 10 log10(EDC(t) / EDC(0)) per bin, SilenceDb once the integral is empty. */
	ACOUSTICSCORE_API void SchroederCurveDb(const FEnergyHistogram& Histogram, int32 Band, TArray<double>& OutLevelDb);

	/** Last bin of the curve that lies before the truncation guard, INDEX_NONE for an empty curve. */
	ACOUSTICSCORE_API int32 UsableEndBin(TConstArrayView<double> LevelDb, double BinSeconds);

	/**
	 * Least-squares line through the samples of the curve with StartDb >= L >= EndDb (bin start times, up to the
	 * usable end), decay time extrapolated to 60 dB. False when the usable curve never reaches EndDb or the fit is
	 * not a decay.
	 */
	ACOUSTICSCORE_API bool DecayTimeByRegression(TConstArrayView<double> LevelDb, double BinSeconds, double StartDb, double EndDb, double& OutDecayTime60, double* OutSlopeDbPerSecond = nullptr);

	/** 10 log10(Early / Late), +-ClarityLimitDb when one side is empty. */
	ACOUSTICSCORE_API double ClarityDb(double EarlyEnergy, double LateEnergy);

	/** Sabine T60 = 0.161 V / (S alpha + 4 m V). */
	ACOUSTICSCORE_API double SabineT60(double VolumeM3, double SurfaceM2, double MeanAbsorption, double AirAbsorptionPerMeter = 0.0);

	/** Eyring T60 = 0.161 V / (-S ln(1 - alpha) + 4 m V). */
	ACOUSTICSCORE_API double EyringT60(double VolumeM3, double SurfaceM2, double MeanAbsorption, double AirAbsorptionPerMeter = 0.0);

	/** Mean free path 4 V / S. */
	ACOUSTICSCORE_API double MeanFreePath(double VolumeM3, double SurfaceM2);
}
