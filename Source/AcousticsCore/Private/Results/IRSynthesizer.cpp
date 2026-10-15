#include "Results/IRSynthesizer.h"

#include "AcousticsCore.h"
#include "Results/EnergyHistogram.h"
#include "Results/RoomMetrics.h"
#include "Results/SimulationResult.h"
#include "Solvers/AcousticRandom.h"

DECLARE_CYCLE_STAT(TEXT("IRSynthesizer::Synthesize"), STAT_Acoustics_IRSynthesize, STATGROUP_Acoustics);

bool FIRSynthesisSettings::Validate(FString& OutError) const
{
	if (SampleRate < 8000 || SampleRate > 192000)
	{
		OutError = FString::Printf(TEXT("sample rate %d Hz outside 8000..192000"), SampleRate);
		return false;
	}
	if (PeakDbFS > 0.0)
	{
		OutError = FString::Printf(TEXT("peak %.1f dBFS above full scale"), PeakDbFS);
		return false;
	}
	if (TruncateBelowDb >= 0.0)
	{
		OutError = FString::Printf(TEXT("truncation level %.1f dB must be negative"), TruncateBelowDb);
		return false;
	}
	if (MaxSeconds < 0.0)
	{
		OutError = TEXT("MaxSeconds must not be negative");
		return false;
	}
	return true;
}

double FSynthesizedIR::Energy() const
{
	double Sum = 0.0;
	for (const float Sample : Samples)
	{
		Sum += static_cast<double>(Sample) * Sample;
	}
	return Sum;
}

double FSynthesizedIR::Peak() const
{
	double Max = 0.0;
	for (const float Sample : Samples)
	{
		Max = FMath::Max(Max, FMath::Abs(static_cast<double>(Sample)));
	}
	return Max;
}

double FSynthesizedIR::PeakDbFS() const
{
	return FIRSynthesizer::LinearToDbFS(Peak());
}

FString FSynthesizedIR::Describe() const
{
	if (!IsValid())
	{
		return TEXT("empty impulse response");
	}
	return FString::Printf(TEXT("%d samples at %d Hz (%.3f s, %d bins, cut by %s), peak %.2f dBFS, energy %.4g (before normalization %.4g, gain %.3g), %d impulses"),
		Samples.Num(), SampleRate, DurationSeconds(), NumBinsUsed, bTruncatedByLevel ? TEXT("level") : TEXT("length"),
		PeakDbFS(), Energy(), EnergyBeforeNormalization, NormalizationGain, NumImpulses);
}

double FIRSynthesizer::LinearToDbFS(double Amplitude)
{
	return Amplitude > 0.0 ? 20.0 * FMath::LogX(10.0, Amplitude) : RoomAcoustics::SilenceDb;
}

void FIRSynthesizer::BinSampleRange(double BinSeconds, int32 Bin, int32 SampleRate, int32& OutFirst, int32& OutEnd)
{
	OutFirst = FMath::RoundToInt32(Bin * BinSeconds * SampleRate);
	OutEnd = FMath::RoundToInt32((Bin + 1) * BinSeconds * SampleRate);
}

int32 FIRSynthesizer::ComputeNumBins(const FEnergyHistogram& Histogram, int32 Band, double TruncateBelowDb, double MaxSeconds, bool* bOutTruncatedByLevel)
{
	bool bByLevel = false;
	int32 NumBins = 0;
	if (Histogram.IsInitialized() && Band >= 0 && Band < Acoustics::NumBands)
	{
		const int32 LastBin = Histogram.LastNonEmptyBin(Band);
		if (LastBin != INDEX_NONE)
		{
			NumBins = LastBin + 1;
			TArray<double> LevelDb;
			RoomAcoustics::SchroederCurveDb(Histogram, Band, LevelDb);
			for (int32 Bin = 0; Bin < NumBins && Bin < LevelDb.Num(); ++Bin)
			{
				if (LevelDb[Bin] < TruncateBelowDb)
				{
					NumBins = Bin;
					bByLevel = true;
					break;
				}
			}
			if (MaxSeconds > 0.0)
			{
				const int32 Cap = FMath::CeilToInt32(MaxSeconds / Histogram.BinSeconds - 1e-9);
				if (NumBins > Cap)
				{
					NumBins = FMath::Max(0, Cap);
					bByLevel = false;
				}
			}
		}
	}
	if (bOutTruncatedByLevel)
	{
		*bOutTruncatedByLevel = bByLevel;
	}
	return NumBins;
}

void FIRSynthesizer::EnergyPerBin(const FSynthesizedIR& IR, TArray<double>& OutEnergy)
{
	OutEnergy.Reset();
	OutEnergy.Init(0.0, FMath::Max(0, IR.NumBinsUsed));
	for (int32 Bin = 0; Bin < IR.NumBinsUsed; ++Bin)
	{
		int32 First = 0;
		int32 End = 0;
		BinSampleRange(IR.BinSeconds, Bin, IR.SampleRate, First, End);
		double Sum = 0.0;
		for (int32 Index = FMath::Max(0, First); Index < End && Index < IR.Samples.Num(); ++Index)
		{
			Sum += static_cast<double>(IR.Samples[Index]) * IR.Samples[Index];
		}
		OutEnergy[Bin] = Sum;
	}
}

FSynthesizedIR FIRSynthesizer::Synthesize(const FEnergyHistogram& Histogram, TConstArrayView<FIRImpulse> Impulses, const FIRSynthesisSettings& Settings, int32 Band)
{
	SCOPE_CYCLE_COUNTER(STAT_Acoustics_IRSynthesize);
	TRACE_CPUPROFILER_EVENT_SCOPE(FIRSynthesizer::Synthesize);

	FSynthesizedIR Out;
	FString Error;
	if (!Settings.Validate(Error))
	{
		UE_LOG(LogAcoustics, Error, TEXT("IR synthesis: %s"), *Error);
		return Out;
	}
	if (!Histogram.IsInitialized() || Band < 0 || Band >= Acoustics::NumBands)
	{
		return Out;
	}

	Out.SampleRate = Settings.SampleRate;
	Out.BinSeconds = Histogram.BinSeconds;
	bool bByLevel = false;
	const int32 NumBins = ComputeNumBins(Histogram, Band, Settings.TruncateBelowDb, Settings.MaxSeconds, &bByLevel);
	Out.NumBinsUsed = NumBins;
	Out.bTruncatedByLevel = bByLevel;
	Out.TruncationSeconds = NumBins * Histogram.BinSeconds;
	if (NumBins <= 0)
	{
		return Out;
	}

	int32 LastFirst = 0;
	int32 NumSamples = 0;
	BinSampleRange(Histogram.BinSeconds, NumBins - 1, Settings.SampleRate, LastFirst, NumSamples);
	if (NumSamples <= 0)
	{
		return Out;
	}
	Out.Samples.SetNumZeroed(NumSamples);

	// Deterministic impulses first; their energy leaves the noise of the bin that contains the sample.
	TArray<double> ImpulseEnergyPerBin;
	ImpulseEnergyPerBin.Init(0.0, NumBins);
	for (const FIRImpulse& Impulse : Impulses)
	{
		if (Impulse.Energy <= 0.0 || Impulse.TimeSeconds < 0.0)
		{
			continue;
		}
		const int32 SampleIndex = FMath::RoundToInt32(Impulse.TimeSeconds * Settings.SampleRate);
		if (SampleIndex < 0 || SampleIndex >= NumSamples)
		{
			continue; // beyond the truncated response
		}
		const int32 Bin = FMath::Clamp(static_cast<int32>(SampleIndex / (Histogram.BinSeconds * Settings.SampleRate)), 0, NumBins - 1);
		ImpulseEnergyPerBin[Bin] += Impulse.Energy;
		Out.Samples[SampleIndex] += static_cast<float>(FMath::Sqrt(Impulse.Energy));
		++Out.NumImpulses;
	}

	TArray<double> Noise;
	for (int32 Bin = 0; Bin < NumBins; ++Bin)
	{
		const double BinEnergy = Histogram.At(Bin, Band) - ImpulseEnergyPerBin[Bin];
		if (BinEnergy <= 0.0)
		{
			continue;
		}
		int32 First = 0;
		int32 End = 0;
		BinSampleRange(Histogram.BinSeconds, Bin, Settings.SampleRate, First, End);
		First = FMath::Max(0, First);
		End = FMath::Min(End, NumSamples);
		const int32 Count = End - First;
		if (Count <= 0)
		{
			continue;
		}
		FAcousticRandom Rng(Settings.Seed, static_cast<uint64>(Bin));
		Noise.SetNumUninitialized(Count, EAllowShrinking::No);
		double SumSquares = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Value = 2.0 * Rng.NextDouble() - 1.0;
			Noise[Index] = Value;
			SumSquares += Value * Value;
		}
		if (SumSquares <= 0.0)
		{
			continue;
		}
		const double Scale = FMath::Sqrt(BinEnergy / SumSquares);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Out.Samples[First + Index] += static_cast<float>(Noise[Index] * Scale);
		}
	}

	Out.PeakBeforeNormalization = Out.Peak();
	Out.EnergyBeforeNormalization = Out.Energy();
	if (Settings.bNormalize && Out.PeakBeforeNormalization > 0.0)
	{
		Out.NormalizationGain = DbFSToLinear(Settings.PeakDbFS) / Out.PeakBeforeNormalization;
		const float Gain = static_cast<float>(Out.NormalizationGain);
		for (float& Sample : Out.Samples)
		{
			Sample *= Gain;
		}
	}
	return Out;
}

FSynthesizedIR FIRSynthesizer::SynthesizeForReceiver(const FReceiverResult& Receiver, const FIRSynthesisSettings& Settings, int32 Band)
{
	TArray<FIRImpulse> Impulses;
	Impulses.Reserve(1 + Receiver.EarlyReflections.Num());
	if (!Receiver.bDirectOccluded && Receiver.DirectEnergy > 0.0)
	{
		Impulses.Emplace(Receiver.DirectArrivalSeconds, Receiver.DirectEnergy);
	}
	// Early reflections of the image-source and beam solvers are exact impulses too (SPEC §2.9); the solver already
	// deposited their energy in the histogram, Synthesize removes it from the noise of their bins.
	for (const FEarlyReflection& Reflection : Receiver.EarlyReflections)
	{
		if (Reflection.Order > 0 && Reflection.Energy[Band] > 0.0)
		{
			Impulses.Emplace(Reflection.TimeSeconds, Reflection.Energy[Band]);
		}
	}
	return Synthesize(Receiver.Histogram, Impulses, Settings, Band);
}
