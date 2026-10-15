// M2 metrics tests (SPEC §2.3, §4): synthetic exponential decays with a known T60, clarity against the closed form, Sabine / Eyring.
#include "Misc/AutomationTest.h"
#include "Results/EnergyHistogram.h"
#include "Results/RoomMetrics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AcousticsMetricsTests
{
	/** E(t) = exp(-k t) with k = ln(1e6) / T60, integrated exactly over every bin so the sums match the closed forms. */
	static FEnergyHistogram MakeExponentialDecay(double T60, double BinSeconds, double MaxTime, double Scale = 1.0)
	{
		FEnergyHistogram Histogram(BinSeconds, MaxTime);
		const double K = FMath::Loge(1e6) / T60;
		for (int32 Bin = 0; Bin < Histogram.NumBins; ++Bin)
		{
			const double T0 = Histogram.BinStartTime(Bin);
			const double T1 = T0 + BinSeconds;
			const double BinEnergy = Scale * (FMath::Exp(-K * T0) - FMath::Exp(-K * T1)) / K;
			for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
			{
				Histogram.At(Bin, Band) = BinEnergy;
			}
		}
		return Histogram;
	}

	/** Closed-form clarity of exp(-k t) truncated at MaxTime with the early window [0, Window). */
	static double ExpectedClarity(double T60, double Window, double MaxTime)
	{
		const double K = FMath::Loge(1e6) / T60;
		const double Early = 1.0 - FMath::Exp(-K * Window);
		const double Late = FMath::Exp(-K * Window) - FMath::Exp(-K * MaxTime);
		return 10.0 * FMath::LogX(10.0, Early / Late);
	}

	static bool Within(double Value, double Expected, double RelativeTolerance)
	{
		return FMath::Abs(Value - Expected) <= RelativeTolerance * FMath::Abs(Expected);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsSyntheticDecayTest, "AcousticsLab.Core.Metrics.SyntheticDecay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsSyntheticDecayTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsMetricsTests;

	constexpr double BinSeconds = 0.001;
	constexpr double MaxTime = 3.0;
	const double DecayTimes[] = { 0.4, 1.2, 2.0 };

	for (const double T60 : DecayTimes)
	{
		const FEnergyHistogram Histogram = MakeExponentialDecay(T60, BinSeconds, MaxTime);
		const FRoomMetrics Metrics = FRoomMetrics::Compute(Histogram);
		const FBandMetrics& Band = Metrics.Band(0);
		AddInfo(FString::Printf(TEXT("T60 %.2f s: %s"), T60, *Band.ToString()));

		TestTrue(TEXT("metrics valid"), Metrics.bValid);
		TestTrue(TEXT("direct arrival at t = 0 for a decay starting at the first bin"), FMath::IsNearlyEqual(Metrics.DirectArrivalSeconds, 0.0, 1e-12));
		TestTrue(TEXT("T30 valid"), Band.bT30Valid);
		TestTrue(TEXT("T20 valid"), Band.bT20Valid);
		TestTrue(TEXT("EDT valid"), Band.bEDTValid);
		TestTrue(FString::Printf(TEXT("T30 %.4f within 2 %% of %.2f"), Band.T30, T60), Within(Band.T30, T60, 0.02));
		TestTrue(FString::Printf(TEXT("T20 %.4f within 2 %% of %.2f"), Band.T20, T60), Within(Band.T20, T60, 0.02));
		TestTrue(FString::Printf(TEXT("EDT %.4f within 2 %% of %.2f"), Band.EDT, T60), Within(Band.EDT, T60, 0.02));

		const double ExpectedC50 = ExpectedClarity(T60, RoomAcoustics::EarlyWindow50, MaxTime);
		const double ExpectedC80 = ExpectedClarity(T60, RoomAcoustics::EarlyWindow80, MaxTime);
		TestTrue(FString::Printf(TEXT("C50 %.4f dB matches the closed form %.4f dB"), Band.C50, ExpectedC50), FMath::IsNearlyEqual(Band.C50, ExpectedC50, 1e-6));
		TestTrue(FString::Printf(TEXT("C80 %.4f dB matches the closed form %.4f dB"), Band.C80, ExpectedC80), FMath::IsNearlyEqual(Band.C80, ExpectedC80, 1e-6));

		const double K = FMath::Loge(1e6) / T60;
		const double ExpectedD50 = (1.0 - FMath::Exp(-K * RoomAcoustics::EarlyWindow50)) / (1.0 - FMath::Exp(-K * MaxTime));
		TestTrue(FString::Printf(TEXT("D50 %.6f matches %.6f"), Band.D50, ExpectedD50), FMath::IsNearlyEqual(Band.D50, ExpectedD50, 1e-9));

		// Center time of exp(-k t) is 1 / k; bin centers of exact bin integrals add only k dt^2 / 12.
		TestTrue(FString::Printf(TEXT("Ts %.4f ms close to 1/k = %.4f ms"), Band.Ts * 1000.0, 1000.0 / K), FMath::IsNearlyEqual(Band.Ts, 1.0 / K, 2e-5));
	}

	// Schroeder curve: starts at 0 dB, strictly falling, linear at -60 dB per T60.
	{
		const FEnergyHistogram Histogram = MakeExponentialDecay(1.0, BinSeconds, MaxTime);
		TArray<double> Curve;
		RoomAcoustics::SchroederCurveDb(Histogram, 0, Curve);
		TestEqual(TEXT("one level per bin"), Curve.Num(), Histogram.NumBins);
		TestTrue(TEXT("curve starts at 0 dB"), FMath::IsNearlyEqual(Curve[0], 0.0, 1e-9));
		TestTrue(TEXT("-60 dB after 1 s"), FMath::IsNearlyEqual(Curve[1000], -60.0, 1e-6));
		bool bMonotonic = true;
		for (int32 Bin = 1; Bin < Curve.Num(); ++Bin)
		{
			bMonotonic &= Curve[Bin] <= Curve[Bin - 1];
		}
		TestTrue(TEXT("curve never rises"), bMonotonic);
	}

	// A response truncated at -42 dB: T30 needs -35 dB plus the 10 dB margin and is invalid, T20 (-25 dB + 10 dB)
	// stays valid and accurate because the truncation plunge lies outside its samples.
	{
		FEnergyHistogram Short = MakeExponentialDecay(1.0, BinSeconds, MaxTime);
		for (int32 Bin = 700; Bin < Short.NumBins; ++Bin)
		{
			Short.At(Bin, 0) = 0.0;
		}
		const FRoomMetrics Metrics = FRoomMetrics::Compute(Short);
		TestFalse(TEXT("T30 invalid when the curve ends at -42 dB"), Metrics.Band(0).bT30Valid);
		TestTrue(TEXT("T20 still valid"), Metrics.Band(0).bT20Valid);
		TestTrue(FString::Printf(TEXT("T20 %.4f of the truncated decay within 2 %%"), Metrics.Band(0).T20), Within(Metrics.Band(0).T20, 1.0, 0.02));
		TestTrue(FString::Printf(TEXT("usable decay range %.1f dB between 35 and 45 dB"), Metrics.Band(0).DecayRangeDb), Metrics.Band(0).DecayRangeDb > 35.0 && Metrics.Band(0).DecayRangeDb < 45.0);
	}

	// Direct arrival given explicitly: the clarity windows move with it.
	{
		FEnergyHistogram Shifted(BinSeconds, MaxTime);
		const FEnergyHistogram Base = MakeExponentialDecay(1.0, BinSeconds, MaxTime);
		for (int32 Bin = 0; Bin + 100 < Shifted.NumBins; ++Bin)
		{
			Shifted.At(Bin + 100, 0) = Base.At(Bin, 0);
		}
		const FRoomMetrics Auto = FRoomMetrics::Compute(Shifted);
		const FRoomMetrics Given = FRoomMetrics::Compute(Shifted, 0.1);
		const FRoomMetrics Reference = FRoomMetrics::Compute(Base);
		TestTrue(TEXT("auto-detected arrival is the first non-empty bin"), FMath::IsNearlyEqual(Auto.DirectArrivalSeconds, 0.1, 1e-12));
		TestTrue(TEXT("C50 of the shifted decay equals the unshifted one"), FMath::IsNearlyEqual(Given.Band(0).C50, Reference.Band(0).C50, 1e-3));
		TestTrue(TEXT("C50 auto-detected equals the explicit one"), FMath::IsNearlyEqual(Auto.Band(0).C50, Given.Band(0).C50, 1e-9));
	}

	// Empty histogram: nothing valid, no crash.
	{
		const FEnergyHistogram Empty(BinSeconds, MaxTime);
		const FRoomMetrics Metrics = FRoomMetrics::Compute(Empty);
		TestFalse(TEXT("empty histogram is invalid"), Metrics.bValid);
		TestFalse(TEXT("empty histogram has no T30"), Metrics.Band(0).bT30Valid);
	}

	// Histogram basics.
	{
		FEnergyHistogram Histogram(BinSeconds, MaxTime);
		TestEqual(TEXT("3000 bins"), Histogram.NumBins, 3000);
		TestTrue(TEXT("add inside"), Histogram.AddEnergy(0.0215, 0, 2.0));
		TestFalse(TEXT("add beyond T_max is rejected"), Histogram.AddEnergy(3.0, 0, 1.0));
		TestFalse(TEXT("negative time is rejected"), Histogram.AddEnergy(-0.001, 0, 1.0));
		TestEqual(TEXT("bin of 21.5 ms"), Histogram.BinIndex(0.0215), 21);
		TestTrue(TEXT("total"), FMath::IsNearlyEqual(Histogram.TotalEnergy(0), 2.0, 1e-12));
		TestEqual(TEXT("first non-empty"), Histogram.FirstNonEmptyBin(0), 21);
		FEnergyHistogram Copy = Histogram;
		TestTrue(TEXT("copy identical"), Copy.IsIdentical(Histogram));
		Copy.AddEnergy(1.0, 0, 1e-9);
		TestFalse(TEXT("changed copy differs"), Copy.IsIdentical(Histogram));
		TestEqual(TEXT("one bin differs"), Copy.NumDifferentBins(Histogram, 0), 1);
		TestTrue(TEXT("accumulate"), Histogram.Accumulate(Copy));
		TestTrue(TEXT("accumulated total"), FMath::IsNearlyEqual(Histogram.TotalEnergy(0), 4.0 + 1e-9, 1e-12));
	}

	// Sabine / Eyring for the SPEC shoebox: 10 x 7 x 3 m, alpha 0.1 -> 1.40 s / 1.33 s.
	{
		const double Sabine = RoomAcoustics::SabineT60(210.0, 242.0, 0.1);
		const double Eyring = RoomAcoustics::EyringT60(210.0, 242.0, 0.1);
		TestTrue(FString::Printf(TEXT("Sabine %.3f s is 1.40 s"), Sabine), FMath::IsNearlyEqual(Sabine, 1.397, 0.005));
		TestTrue(FString::Printf(TEXT("Eyring %.3f s is 1.33 s"), Eyring), FMath::IsNearlyEqual(Eyring, 1.327, 0.005));
		TestTrue(TEXT("Eyring is shorter than Sabine"), Eyring < Sabine);
		TestTrue(TEXT("air absorption shortens the decay"), RoomAcoustics::SabineT60(210.0, 242.0, 0.1, 0.002) < Sabine);
		TestTrue(TEXT("mean free path 4V/S"), FMath::IsNearlyEqual(RoomAcoustics::MeanFreePath(210.0, 242.0), 840.0 / 242.0, 1e-12));
		TestTrue(TEXT("clarity limits"), RoomAcoustics::ClarityDb(1.0, 0.0) == RoomAcoustics::ClarityLimitDb && RoomAcoustics::ClarityDb(0.0, 1.0) == -RoomAcoustics::ClarityLimitDb);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
