#include "Results/RoomMetrics.h"

#include "Results/EnergyHistogram.h"

namespace
{
	FString DecayToString(const TCHAR* Name, double Value, bool bValid)
	{
		return bValid ? FString::Printf(TEXT("%s %.3f s"), Name, Value) : FString::Printf(TEXT("%s n/a"), Name);
	}
}

FString FBandMetrics::ToString() const
{
	return FString::Printf(TEXT("%s, %s, %s, C50 %.1f dB, C80 %.1f dB, D50 %.3f, Ts %.1f ms, range %.1f dB, total %.4g"),
		*DecayToString(TEXT("EDT"), EDT, bEDTValid), *DecayToString(TEXT("T20"), T20, bT20Valid), *DecayToString(TEXT("T30"), T30, bT30Valid),
		C50, C80, D50, Ts * 1000.0, DecayRangeDb, TotalEnergy);
}

FString FRoomMetrics::ToString() const
{
	FString Result = FString::Printf(TEXT("direct at %.2f ms"), DirectArrivalSeconds * 1000.0);
	for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
	{
		Result += FString::Printf(TEXT("; band %d: %s"), Band, *Bands[Band].ToString());
	}
	return Result;
}

FRoomMetrics FRoomMetrics::Compute(const FEnergyHistogram& Histogram, double DirectArrivalSeconds)
{
	FRoomMetrics Result;
	if (!Histogram.IsInitialized())
	{
		return Result;
	}

	if (DirectArrivalSeconds < 0.0)
	{
		const int32 FirstBin = Histogram.FirstNonEmptyBin(0);
		DirectArrivalSeconds = FirstBin == INDEX_NONE ? 0.0 : Histogram.BinStartTime(FirstBin);
	}
	Result.DirectArrivalSeconds = DirectArrivalSeconds;

	const int32 DirectBin = FMath::Clamp(static_cast<int32>(DirectArrivalSeconds / Histogram.BinSeconds), 0, Histogram.NumBins);
	const int32 Split50 = FMath::Min(DirectBin + FMath::RoundToInt32(RoomAcoustics::EarlyWindow50 / Histogram.BinSeconds), Histogram.NumBins);
	const int32 Split80 = FMath::Min(DirectBin + FMath::RoundToInt32(RoomAcoustics::EarlyWindow80 / Histogram.BinSeconds), Histogram.NumBins);

	TArray<double> Curve;
	for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
	{
		FBandMetrics& Metrics = Result.Bands[Band];
		Metrics.TotalEnergy = Histogram.TotalEnergy(Band);
		if (Metrics.TotalEnergy <= 0.0)
		{
			continue;
		}
		Result.bValid = true;

		Metrics.EarlyEnergy50 = Histogram.SumRange(0, Split50, Band);
		Metrics.EarlyEnergy80 = Histogram.SumRange(0, Split80, Band);
		Metrics.C50 = RoomAcoustics::ClarityDb(Metrics.EarlyEnergy50, Metrics.TotalEnergy - Metrics.EarlyEnergy50);
		Metrics.C80 = RoomAcoustics::ClarityDb(Metrics.EarlyEnergy80, Metrics.TotalEnergy - Metrics.EarlyEnergy80);
		Metrics.D50 = Metrics.EarlyEnergy50 / Metrics.TotalEnergy;

		double Moment = 0.0;
		for (int32 Bin = 0; Bin < Histogram.NumBins; ++Bin)
		{
			Moment += (Histogram.BinCenterTime(Bin) - DirectArrivalSeconds) * Histogram.At(Bin, Band);
		}
		Metrics.Ts = Moment / Metrics.TotalEnergy;

		RoomAcoustics::SchroederCurveDb(Histogram, Band, Curve);
		const int32 UsableEnd = RoomAcoustics::UsableEndBin(Curve, Histogram.BinSeconds);
		Metrics.DecayRangeDb = UsableEnd >= 0 ? FMath::Max(0.0, -Curve[UsableEnd]) : 0.0;

		double Time = 0.0;
		Metrics.bEDTValid = RoomAcoustics::DecayTimeByRegression(Curve, Histogram.BinSeconds, 0.0, -10.0, Time);
		Metrics.EDT = Metrics.bEDTValid ? Time : 0.0;
		Metrics.bT20Valid = RoomAcoustics::DecayTimeByRegression(Curve, Histogram.BinSeconds, -5.0, -25.0, Time);
		Metrics.T20 = Metrics.bT20Valid ? Time : 0.0;
		Metrics.bT30Valid = RoomAcoustics::DecayTimeByRegression(Curve, Histogram.BinSeconds, -5.0, -35.0, Time);
		Metrics.T30 = Metrics.bT30Valid ? Time : 0.0;
	}
	return Result;
}

namespace RoomAcoustics
{
	void SchroederCurveDb(const FEnergyHistogram& Histogram, int32 Band, TArray<double>& OutLevelDb)
	{
		OutLevelDb.SetNumUninitialized(Histogram.NumBins);
		if (Histogram.NumBins == 0)
		{
			return;
		}

		// Backward integration keeps the tail accurate: the small late energies are summed first.
		double Running = 0.0;
		for (int32 Bin = Histogram.NumBins - 1; Bin >= 0; --Bin)
		{
			Running += Histogram.At(Bin, Band);
			OutLevelDb[Bin] = Running;
		}

		const double Total = OutLevelDb[0];
		for (double& Level : OutLevelDb)
		{
			Level = (Total > 0.0 && Level > 0.0) ? 10.0 * FMath::LogX(10.0, Level / Total) : SilenceDb;
		}
	}

	int32 UsableEndBin(TConstArrayView<double> LevelDb, double BinSeconds)
	{
		int32 LastFinite = INDEX_NONE;
		for (int32 Bin = LevelDb.Num() - 1; Bin >= 0; --Bin)
		{
			if (LevelDb[Bin] > SilenceDb)
			{
				LastFinite = Bin;
				break;
			}
		}
		if (LastFinite == INDEX_NONE || BinSeconds <= 0.0)
		{
			return INDEX_NONE;
		}
		const int32 Guard = FMath::RoundToInt32(TruncationGuardSeconds / BinSeconds);
		return LastFinite - Guard;
	}

	bool DecayTimeByRegression(TConstArrayView<double> LevelDb, double BinSeconds, double StartDb, double EndDb, double& OutDecayTime60, double* OutSlopeDbPerSecond)
	{
		OutDecayTime60 = 0.0;
		if (OutSlopeDbPerSecond)
		{
			*OutSlopeDbPerSecond = 0.0;
		}
		if (LevelDb.Num() < 2 || BinSeconds <= 0.0 || EndDb >= StartDb)
		{
			return false;
		}

		// The usable curve has to reach EndDb with real energy; a histogram that simply stops is not a measured decay.
		const int32 UsableEnd = UsableEndBin(LevelDb, BinSeconds);
		if (UsableEnd < 1)
		{
			return false;
		}
		bool bReachesEnd = false;
		for (int32 Bin = 0; Bin <= UsableEnd; ++Bin)
		{
			if (LevelDb[Bin] > SilenceDb && LevelDb[Bin] <= EndDb - ValidityMarginDb)
			{
				bReachesEnd = true;
				break;
			}
		}
		if (!bReachesEnd)
		{
			return false;
		}

		double SumT = 0.0, SumL = 0.0, SumTT = 0.0, SumTL = 0.0;
		int32 Count = 0;
		for (int32 Bin = 0; Bin <= UsableEnd; ++Bin)
		{
			const double Level = LevelDb[Bin];
			if (Level <= SilenceDb || Level > StartDb || Level < EndDb)
			{
				continue;
			}
			const double Time = Bin * BinSeconds;
			SumT += Time;
			SumL += Level;
			SumTT += Time * Time;
			SumTL += Time * Level;
			++Count;
		}
		if (Count < 2)
		{
			return false;
		}

		const double Denominator = Count * SumTT - SumT * SumT;
		if (Denominator <= 0.0)
		{
			return false;
		}
		const double Slope = (Count * SumTL - SumT * SumL) / Denominator;
		if (!(Slope < 0.0))
		{
			return false;
		}
		if (OutSlopeDbPerSecond)
		{
			*OutSlopeDbPerSecond = Slope;
		}
		OutDecayTime60 = -60.0 / Slope;
		return true;
	}

	double ClarityDb(double EarlyEnergy, double LateEnergy)
	{
		if (EarlyEnergy <= 0.0 && LateEnergy <= 0.0)
		{
			return 0.0;
		}
		if (LateEnergy <= 0.0)
		{
			return ClarityLimitDb;
		}
		if (EarlyEnergy <= 0.0)
		{
			return -ClarityLimitDb;
		}
		return FMath::Clamp(10.0 * FMath::LogX(10.0, EarlyEnergy / LateEnergy), -ClarityLimitDb, ClarityLimitDb);
	}

	double SabineT60(double VolumeM3, double SurfaceM2, double MeanAbsorption, double AirAbsorptionPerMeter)
	{
		const double Absorption = SurfaceM2 * MeanAbsorption + 4.0 * AirAbsorptionPerMeter * VolumeM3;
		return Absorption > 0.0 ? SabineConstant * VolumeM3 / Absorption : 0.0;
	}

	double EyringT60(double VolumeM3, double SurfaceM2, double MeanAbsorption, double AirAbsorptionPerMeter)
	{
		if (MeanAbsorption >= 1.0)
		{
			return 0.0;
		}
		const double Absorption = -SurfaceM2 * FMath::Loge(1.0 - MeanAbsorption) + 4.0 * AirAbsorptionPerMeter * VolumeM3;
		return Absorption > 0.0 ? SabineConstant * VolumeM3 / Absorption : 0.0;
	}

	double MeanFreePath(double VolumeM3, double SurfaceM2)
	{
		return SurfaceM2 > 0.0 ? 4.0 * VolumeM3 / SurfaceM2 : 0.0;
	}
}
