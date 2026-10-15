// Energy histogram of an impulse response: energy per time bin and band (SPEC §2.2.8).
#pragma once

#include "CoreMinimal.h"
#include "AcousticsConstants.h"

/**
 * E(t) per band on a fixed time grid: NumBins bins of BinSeconds starting at t = 0, Acoustics::NumBands values per bin.
 * The layout is bin-major (Energy[Bin * NumBands + Band]) so the bands of one bin are contiguous.
 * Energies are plain sums of what the solvers deposit (E0 = 1 / N per ray): the direct sound of an unoccluded source
 * at d meters is r^2 / (4 d^2), and a lossless closed room would integrate to the emitted energy 1.
 */
struct ACOUSTICSCORE_API FEnergyHistogram
{
	double BinSeconds = Acoustics::DefaultBinSeconds;
	int32 NumBins = 0;
	TArray<double> Energy;

	FEnergyHistogram() = default;
	FEnergyHistogram(double InBinSeconds, double MaxTimeSeconds);

	/** ceil(MaxTimeSeconds / InBinSeconds) zeroed bins. */
	void Init(double InBinSeconds, double MaxTimeSeconds);

	/** Zeroes every bin, keeps the layout. */
	void Clear();

	bool IsInitialized() const { return NumBins > 0 && Energy.Num() == NumBins * Acoustics::NumBands; }
	double MaxTime() const { return NumBins * BinSeconds; }

	/** Bin containing TimeSeconds, INDEX_NONE outside [0, MaxTime). */
	int32 BinIndex(double TimeSeconds) const;
	double BinStartTime(int32 Bin) const { return Bin * BinSeconds; }
	double BinCenterTime(int32 Bin) const { return (Bin + 0.5) * BinSeconds; }

	double& At(int32 Bin, int32 Band) { return Energy[Bin * Acoustics::NumBands + Band]; }
	double At(int32 Bin, int32 Band) const { return Energy[Bin * Acoustics::NumBands + Band]; }

	/** Adds Value to the bin of TimeSeconds; false (nothing added) when the time lies outside the histogram. */
	bool AddEnergy(double TimeSeconds, int32 Band, double Value);
	bool AddEnergy(double TimeSeconds, const double (&Values)[Acoustics::NumBands]);
	void AddToBin(int32 Bin, const double (&Values)[Acoustics::NumBands]);

	/** Bin-wise sum with Other; false when the layouts differ (nothing added). */
	bool Accumulate(const FEnergyHistogram& Other);
	void Scale(double Factor);

	double TotalEnergy(int32 Band) const;

	/** Sum over bins [FirstBin, EndBin), clamped to the histogram. */
	double SumRange(int32 FirstBin, int32 EndBin, int32 Band) const;
	int32 FirstNonEmptyBin(int32 Band) const;
	int32 LastNonEmptyBin(int32 Band) const;
	double PeakEnergy(int32 Band) const;

	bool SameLayout(const FEnergyHistogram& Other) const { return BinSeconds == Other.BinSeconds && NumBins == Other.NumBins && Energy.Num() == Other.Energy.Num(); }

	/** Same layout and bit-identical energies (determinism tests). */
	bool IsIdentical(const FEnergyHistogram& Other) const;
	int32 NumDifferentBins(const FEnergyHistogram& Other, int32 Band) const;

	FString DescribeStats(int32 Band = 0) const;
};
