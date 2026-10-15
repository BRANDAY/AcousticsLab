#include "Results/EnergyHistogram.h"

FEnergyHistogram::FEnergyHistogram(double InBinSeconds, double MaxTimeSeconds)
{
	Init(InBinSeconds, MaxTimeSeconds);
}

void FEnergyHistogram::Init(double InBinSeconds, double MaxTimeSeconds)
{
	BinSeconds = InBinSeconds > 0.0 ? InBinSeconds : Acoustics::DefaultBinSeconds;
	NumBins = FMath::Max(1, FMath::CeilToInt32(MaxTimeSeconds / BinSeconds - 1e-9));
	Energy.Init(0.0, NumBins * Acoustics::NumBands);
}

void FEnergyHistogram::Clear()
{
	for (double& Value : Energy)
	{
		Value = 0.0;
	}
}

int32 FEnergyHistogram::BinIndex(double TimeSeconds) const
{
	if (!(TimeSeconds >= 0.0) || NumBins <= 0)
	{
		return INDEX_NONE;
	}
	const double Scaled = TimeSeconds / BinSeconds;
	if (Scaled >= static_cast<double>(NumBins))
	{
		return INDEX_NONE;
	}
	return static_cast<int32>(Scaled);
}

bool FEnergyHistogram::AddEnergy(double TimeSeconds, int32 Band, double Value)
{
	const int32 Bin = BinIndex(TimeSeconds);
	if (Bin == INDEX_NONE)
	{
		return false;
	}
	At(Bin, Band) += Value;
	return true;
}

bool FEnergyHistogram::AddEnergy(double TimeSeconds, const double (&Values)[Acoustics::NumBands])
{
	const int32 Bin = BinIndex(TimeSeconds);
	if (Bin == INDEX_NONE)
	{
		return false;
	}
	AddToBin(Bin, Values);
	return true;
}

void FEnergyHistogram::AddToBin(int32 Bin, const double (&Values)[Acoustics::NumBands])
{
	double* Target = &Energy[Bin * Acoustics::NumBands];
	for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
	{
		Target[Band] += Values[Band];
	}
}

bool FEnergyHistogram::Accumulate(const FEnergyHistogram& Other)
{
	if (!SameLayout(Other))
	{
		return false;
	}
	for (int32 Index = 0; Index < Energy.Num(); ++Index)
	{
		Energy[Index] += Other.Energy[Index];
	}
	return true;
}

void FEnergyHistogram::Scale(double Factor)
{
	for (double& Value : Energy)
	{
		Value *= Factor;
	}
}

double FEnergyHistogram::TotalEnergy(int32 Band) const
{
	return SumRange(0, NumBins, Band);
}

double FEnergyHistogram::SumRange(int32 FirstBin, int32 EndBin, int32 Band) const
{
	FirstBin = FMath::Max(FirstBin, 0);
	EndBin = FMath::Min(EndBin, NumBins);
	double Sum = 0.0;
	for (int32 Bin = FirstBin; Bin < EndBin; ++Bin)
	{
		Sum += At(Bin, Band);
	}
	return Sum;
}

int32 FEnergyHistogram::FirstNonEmptyBin(int32 Band) const
{
	for (int32 Bin = 0; Bin < NumBins; ++Bin)
	{
		if (At(Bin, Band) > 0.0)
		{
			return Bin;
		}
	}
	return INDEX_NONE;
}

int32 FEnergyHistogram::LastNonEmptyBin(int32 Band) const
{
	for (int32 Bin = NumBins - 1; Bin >= 0; --Bin)
	{
		if (At(Bin, Band) > 0.0)
		{
			return Bin;
		}
	}
	return INDEX_NONE;
}

double FEnergyHistogram::PeakEnergy(int32 Band) const
{
	double Peak = 0.0;
	for (int32 Bin = 0; Bin < NumBins; ++Bin)
	{
		Peak = FMath::Max(Peak, At(Bin, Band));
	}
	return Peak;
}

bool FEnergyHistogram::IsIdentical(const FEnergyHistogram& Other) const
{
	if (!SameLayout(Other))
	{
		return false;
	}
	return Energy.Num() == 0 || FMemory::Memcmp(Energy.GetData(), Other.Energy.GetData(), Energy.Num() * sizeof(double)) == 0;
}

int32 FEnergyHistogram::NumDifferentBins(const FEnergyHistogram& Other, int32 Band) const
{
	if (!SameLayout(Other))
	{
		return FMath::Max(NumBins, Other.NumBins);
	}
	int32 Count = 0;
	for (int32 Bin = 0; Bin < NumBins; ++Bin)
	{
		Count += At(Bin, Band) != Other.At(Bin, Band) ? 1 : 0;
	}
	return Count;
}

FString FEnergyHistogram::DescribeStats(int32 Band) const
{
	const int32 First = FirstNonEmptyBin(Band);
	const int32 Last = LastNonEmptyBin(Band);
	return FString::Printf(TEXT("%d bins of %.1f ms, band %d: total %.6g, peak %.6g, energy in bins %d..%d (%.1f..%.1f ms)"),
		NumBins, BinSeconds * 1000.0, Band, TotalEnergy(Band), PeakEnergy(Band), First, Last,
		First == INDEX_NONE ? 0.0 : BinStartTime(First) * 1000.0, Last == INDEX_NONE ? 0.0 : BinStartTime(Last) * 1000.0);
}
