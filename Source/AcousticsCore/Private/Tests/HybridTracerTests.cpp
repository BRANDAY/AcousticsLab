// M8 hybrid tests (SPEC §2.8, §4, §5 exp. 3): early part = pure beam, tail = pure ray, no path counted twice or lost,
// thread-count invariance, the reflections a small ray run misses.
#include "Geometry/AcousticScene.h"
#include "Misc/AutomationTest.h"
#include "Results/RoomMetrics.h"
#include "Solvers/BeamTracer.h"
#include "Solvers/HybridTracer.h"
#include "Solvers/RayTracer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AcousticsHybridTests
{
	static const FVector3d RoomSize(10.0, 7.0, 3.0);
	static const FVector3d SourcePosition(-3.0, -1.5, 1.5);

	static TArray<FSolverReceiver> ShoeboxReceivers()
	{
		TArray<FSolverReceiver> Receivers;
		Receivers.Emplace(TEXT("R1"), FVector3d(3.0, 1.5, 1.5), 0.5);
		Receivers.Emplace(TEXT("R2"), FVector3d(0.0, 2.5, 1.2), 0.5);
		return Receivers;
	}

	static FHybridTracerSettings TestSettings(int32 NumRays, int32 K, double MixingTimeSeconds, int32 NumThreads = 0)
	{
		FHybridTracerSettings Settings;
		Settings.Ray.NumRays = NumRays;
		Settings.Ray.Seed = 20261015;
		Settings.Ray.MaxOrder = 500;
		Settings.Ray.NumThreads = NumThreads;
		Settings.Ray.MaxPathsForVisualization = 50;
		Settings.Beam.SubdivisionLevel = 2;
		Settings.Beam.NumThreads = NumThreads;
		Settings.K = K;
		Settings.MixingTimeSeconds = MixingTimeSeconds;
		return Settings;
	}

	static TMap<uint64, const FEarlyReflection*> ReflectionsByKey(const FAcousticScene& Scene, TConstArrayView<FEarlyReflection> Reflections, int32 K, double MixingTime)
	{
		TArray<int32> Surfaces;
		FRayTracer::BuildTriangleSurfaceMap(Scene, Surfaces);
		TMap<uint64, const FEarlyReflection*> Map;
		for (const FEarlyReflection& Reflection : Reflections)
		{
			if (FHybridTracer::IsEarlyBeamReflection(Reflection, K, MixingTime))
			{
				Map.Add(FRayTracer::SurfaceSequenceKey(Reflection.Triangles, Surfaces), &Reflection);
			}
		}
		return Map;
	}

	static double RelativeDifference(double A, double B)
	{
		return FMath::Abs(A - B) / FMath::Max(FMath::Abs(B), 1e-300);
	}

	static double WindowEnergy(const FEnergyHistogram& Histogram, double EndSeconds)
	{
		return Histogram.SumRange(0, Histogram.BinIndex(EndSeconds) + 1, 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsHybridConsistencyTest, "AcousticsLab.Core.Solvers.HybridConsistency",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsHybridConsistencyTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsHybridTests;

	// The image-source distance of a specular ray (unfolded space): floor reflection S -> P -> R.
	{
		const FVector3d Receiver(3.0, 1.5, 1.5);
		const FVector3d Image(SourcePosition.X, SourcePosition.Y, -SourcePosition.Z);
		const double T = Image.Z / (Image.Z - Receiver.Z); // where the line image -> receiver crosses z = 0
		const FVector3d FloorPoint = Image + (Receiver - Image) * T;
		const FVector3d Direction = (Receiver - FloorPoint).GetSafeNormal();
		const double PathLength = (FloorPoint - SourcePosition).Length();
		TestTrue(TEXT("image distance of a point on the ray"), FMath::IsNearlyEqual(FRayTracer::ImageSourceDistance(FloorPoint, Direction, PathLength, Receiver), (Receiver - Image).Length(), 1e-9));
		const FVector3d OffAxis = Receiver + FVector3d(0.0, 0.0, 0.3);
		TestTrue(TEXT("image distance of a point off the ray"), FMath::IsNearlyEqual(FRayTracer::ImageSourceDistance(FloorPoint, Direction, PathLength, OffAxis), (OffAxis - Image).Length(), 1e-9));
	}

	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();
	struct FCase
	{
		double Alpha;
		double Scattering;
	};
	for (const FCase& Case : { FCase{ 0.1, 0.1 }, FCase{ 0.1, 0.5 } })
	{
		const FAcousticScene Scene = FAcousticScene::MakeShoebox(RoomSize, FAcousticMaterialProps(TEXT("Hybrid"), Case.Alpha, Case.Scattering));
		const FHybridTracerSettings Settings = TestSettings(20000, 2, 0.080);
		const FHybridResult Result = FHybridTracer::Run(Scene, SourcePosition, Receivers, Settings);
		const FString Tag = FString::Printf(TEXT("alpha %.2f, s %.2f"), Case.Alpha, Case.Scattering);
		if (!TestTrue(Tag + TEXT(": hybrid run succeeded"), Result.IsValid() && Result.Hybrid->bSuccess))
		{
			continue;
		}
		AddInfo(Result.Describe());

		// The early part equals an independent pure beam run (same settings) cut at K / t_mix: delays +-0.1 ms, energies +-1 %.
		const FBeamTracerSettings BeamSettings = Settings.MakeBeamSettings();
		const FSimulationResult Beam = FBeamTracer::Run(Scene, SourcePosition, Receivers, BeamSettings);
		FBeamTracerSettings ImageEnergySettings = BeamSettings;
		ImageEnergySettings.bScatteringLoss = false;
		const FSimulationResult BeamNoLoss = FBeamTracer::Run(Scene, SourcePosition, Receivers, ImageEnergySettings);
		for (int32 Index = 0; Index < Receivers.Num(); ++Index)
		{
			const FReceiverResult& Hybrid = Result.Hybrid->Receivers[Index];
			const TMap<uint64, const FEarlyReflection*> Expected = ReflectionsByKey(Scene, Beam.Receivers[Index].EarlyReflections, Settings.K, Settings.MixingTimeSeconds);
			const TMap<uint64, const FEarlyReflection*> Got = ReflectionsByKey(Scene, Hybrid.EarlyReflections, Settings.K, Settings.MixingTimeSeconds);
			const TMap<uint64, const FEarlyReflection*> NoLoss = ReflectionsByKey(Scene, BeamNoLoss.Receivers[Index].EarlyReflections, Settings.K, Settings.MixingTimeSeconds);
			TestTrue(FString::Printf(TEXT("%s %s: early reflections found (%d)"), *Tag, *Hybrid.Name, Expected.Num()), Expected.Num() >= 20);
			TestEqual(FString::Printf(TEXT("%s %s: hybrid early reflections = pure beam"), *Tag, *Hybrid.Name), Got.Num(), Expected.Num());
			TestEqual(FString::Printf(TEXT("%s %s: every early reflection kept"), *Tag, *Hybrid.Name), Hybrid.EarlyReflections.Num(), Expected.Num());
			int32 NumMatched = 0;
			double MaxDelayError = 0.0;
			double MaxEnergyError = 0.0;
			double MaxLossError = 0.0;
			for (const TPair<uint64, const FEarlyReflection*>& Pair : Expected)
			{
				const FEarlyReflection* const* Match = Got.Find(Pair.Key);
				if (!Match)
				{
					continue;
				}
				++NumMatched;
				MaxDelayError = FMath::Max(MaxDelayError, FMath::Abs((*Match)->TimeSeconds - Pair.Value->TimeSeconds));
				MaxEnergyError = FMath::Max(MaxEnergyError, RelativeDifference((*Match)->Energy[0], Pair.Value->Energy[0]));
				TestEqual(TEXT("one triangle per reflection"), Pair.Value->Triangles.Num(), Pair.Value->Order);
				if (const FEarlyReflection* const* Lossless = NoLoss.Find(Pair.Key))
				{
					const double Factor = FMath::Pow(1.0 - Case.Scattering, static_cast<double>(Pair.Value->Order));
					MaxLossError = FMath::Max(MaxLossError, RelativeDifference(Pair.Value->Energy[0], (*Lossless)->Energy[0] * Factor));
				}
			}
			TestEqual(FString::Printf(TEXT("%s %s: every reflection matched by its surface sequence"), *Tag, *Hybrid.Name), NumMatched, Expected.Num());
			TestTrue(FString::Printf(TEXT("%s %s: delays within 0.1 ms (max %.3g ms)"), *Tag, *Hybrid.Name, MaxDelayError * 1000.0), MaxDelayError <= 1e-4);
			TestTrue(FString::Printf(TEXT("%s %s: energies within 1 %% (max %.3g)"), *Tag, *Hybrid.Name, MaxEnergyError), MaxEnergyError <= 0.01);
			TestTrue(FString::Printf(TEXT("%s %s: beams carry (1 - s)^k (max error %.3g)"), *Tag, *Hybrid.Name, MaxLossError), MaxLossError <= 1e-9);

			// Hybrid histogram = ray part + the early beam reflections, bin by bin.
			FEnergyHistogram EarlyOnly(Hybrid.Histogram.BinSeconds, Hybrid.Histogram.MaxTime());
			for (const FEarlyReflection& Reflection : Hybrid.EarlyReflections)
			{
				EarlyOnly.AddEnergy(Reflection.TimeSeconds, Reflection.Energy);
			}
			int32 BadBins = 0;
			for (int32 Bin = 0; Bin < Hybrid.Histogram.NumBins; ++Bin)
			{
				const double Expect = Result.LateHistograms[Index].At(Bin, 0) + EarlyOnly.At(Bin, 0);
				BadBins += FMath::Abs(Hybrid.Histogram.At(Bin, 0) - Expect) > 1e-12 * FMath::Max(Expect, 1e-12) ? 1 : 0;
			}
			TestEqual(FString::Printf(TEXT("%s %s: hybrid = late rays + early beams in every bin"), *Tag, *Hybrid.Name), BadBins, 0);

			// Same tail as the pure ray tracer: T30 within 5 %.
			const FBandMetrics& H = Hybrid.Metrics.Band(0);
			const FBandMetrics& R = Result.PureRay->Receivers[Index].Metrics.Band(0);
			TestTrue(FString::Printf(TEXT("%s %s: T30 valid"), *Tag, *Hybrid.Name), H.bT30Valid && R.bT30Valid);
			const double T30Error = R.T30 > 0.0 ? FMath::Abs(H.T30 - R.T30) / R.T30 : 1.0;
			AddInfo(FString::Printf(TEXT("%s %s: T30 hybrid %.3f s, pure ray %.3f s (%.2f %%); EDT %.3f / %.3f s; C50 %.2f / %.2f dB; C80 %.2f / %.2f dB; D50 %.3f / %.3f"),
				*Tag, *Hybrid.Name, H.T30, R.T30, T30Error * 100.0, H.EDT, R.EDT, H.C50, R.C50, H.C80, R.C80, H.D50, R.D50));
			TestTrue(FString::Printf(TEXT("%s %s: hybrid T30 within 5 %% of pure ray (%.2f %%)"), *Tag, *Hybrid.Name, T30Error * 100.0), T30Error <= 0.05);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsHybridNoDoubleCountingTest, "AcousticsLab.Core.Solvers.HybridNoDoubleCounting",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsHybridNoDoubleCountingTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsHybridTests;
	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();
	const FAcousticScene Scene = FAcousticScene::MakeShoebox(RoomSize, FAcousticMaterialProps(TEXT("Hybrid"), 0.1, 0.1));
	struct FCase
	{
		int32 K;
		double MixingTime;
	};
	// K = 2 / 80 ms: the order limit comes first. K = 3 / 30 ms: t_mix comes first, so specular paths of order <= K
	// arrive after t_mix and must stay with the rays.
	for (const FCase& Case : { FCase{ 2, 0.080 }, FCase{ 3, 0.030 } })
	{
		const FString Tag = FString::Printf(TEXT("K %d, t_mix %.0f ms"), Case.K, Case.MixingTime * 1000.0);
		const FHybridTracerSettings Settings = TestSettings(50000, Case.K, Case.MixingTime);
		const FHybridResult Result = FHybridTracer::Run(Scene, SourcePosition, Receivers, Settings);
		if (!TestTrue(Tag + TEXT(": hybrid run succeeded"), Result.IsValid() && Result.Hybrid->bSuccess))
		{
			continue;
		}
		AddInfo(Result.Describe());

		// An independent plain ray run with the same seed: every deposit is either late or excluded, never both, never lost.
		const FSimulationResult Reference = FRayTracer::Run(Scene, SourcePosition, Receivers, Settings.Ray);
		const FSimulationResult PureBeam = FBeamTracer::Run(Scene, SourcePosition, Receivers, Settings.MakeBeamSettings());
		int32 TotalDropped = 0;
		int32 TotalLateGroups = 0;
		for (int32 Index = 0; Index < Receivers.Num(); ++Index)
		{
			const FReceiverResult& Ref = Reference.Receivers[Index];
			const FReceiverResult& Pure = Result.PureRay->Receivers[Index];
			const FReceiverResult& Hybrid = Result.Hybrid->Receivers[Index];
			const FEnergyHistogram& Late = Result.LateHistograms[Index];
			const FHybridReceiverStats& Stats = Result.Stats[Index];
			const FString Name = Tag + TEXT(" ") + Hybrid.Name;

			int32 BadBins = 0;
			for (int32 Bin = 0; Bin < Ref.Histogram.NumBins; ++Bin)
			{
				const double Expect = Ref.Histogram.At(Bin, 0);
				BadBins += FMath::Abs(Pure.Histogram.At(Bin, 0) - Expect) > 1e-9 * FMath::Max(Expect, 1e-15) ? 1 : 0;
			}
			TestEqual(Name + TEXT(": late + excluded = plain ray run in every bin"), BadBins, 0);
			TestEqual(Name + TEXT(": ray hits = late + excluded hits"), Pure.NumRayHits, Ref.NumRayHits);

			// After t_mix the hybrid is the plain ray run, bit for bit (nothing is excluded there, no beam adds anything).
			int32 DifferentAfterMix = 0;
			for (int32 Bin = Ref.Histogram.BinIndex(Case.MixingTime) + 1; Bin < Ref.Histogram.NumBins; ++Bin)
			{
				DifferentAfterMix += Hybrid.Histogram.At(Bin, 0) != Ref.Histogram.At(Bin, 0) ? 1 : 0;
			}
			TestEqual(Name + TEXT(": hybrid bins after t_mix identical to pure ray"), DifferentAfterMix, 0);

			// The specular groups cover every excluded deposit.
			double GroupEnergy = 0.0;
			int64 GroupHits = 0;
			for (const FSpecularRayArrival& Arrival : Pure.SpecularRayArrivals)
			{
				TestTrue(Name + TEXT(": a group is excluded exactly when it arrives by t_mix"), Arrival.bExcluded == (Arrival.TimeSeconds <= Case.MixingTime));
				TestTrue(Name + TEXT(": group order within 1..K"), Arrival.Order >= 1 && Arrival.Order <= Case.K);
				if (Arrival.bExcluded)
				{
					GroupEnergy += Arrival.Energy;
					GroupHits += Arrival.NumHits;
				}
			}
			TestEqual(Name + TEXT(": excluded hits = hits of the excluded groups"), GroupHits, Pure.NumExcludedHits);
			TestTrue(Name + TEXT(": excluded energy = energy of the excluded groups"), RelativeDifference(GroupEnergy, Pure.ExcludedHistogram.TotalEnergy(0)) < 1e-9);

			// No specular path counted twice: the beams' early sequences and the rays' kept (late) sequences are disjoint;
			// none lost: every excluded ray sequence has an early beam reflection, every late one a beam reflection after t_mix.
			// Matched by image source (A-B and B-A off two perpendicular walls share it, see FSpecularRayArrival::ImageSource).
			auto HasImage = [](TConstArrayView<FEarlyReflection> Reflections, const FSpecularRayArrival& Arrival)
			{
				return Reflections.ContainsByPredicate([&Arrival](const FEarlyReflection& Reflection) { return FHybridTracer::IsSameImage(Arrival, Reflection); });
			};
			int32 DoubleCounted = 0;
			int32 ExcludedWithoutBeam = 0;
			int32 LateWithoutBeam = 0;
			for (const FSpecularRayArrival& Arrival : Pure.SpecularRayArrivals)
			{
				TestTrue(Name + TEXT(": image-source delay of a group = its image distance / c"), FMath::IsNearlyEqual(Arrival.TimeSeconds, (Hybrid.Position - Arrival.ImageSource).Length() / Acoustics::SpeedOfSoundAir, 1e-8));
				if (Arrival.bExcluded)
				{
					ExcludedWithoutBeam += HasImage(Hybrid.EarlyReflections, Arrival) ? 0 : 1;
				}
				else
				{
					DoubleCounted += HasImage(Hybrid.EarlyReflections, Arrival) ? 1 : 0;
					LateWithoutBeam += HasImage(PureBeam.Receivers[Index].EarlyReflections, Arrival) ? 0 : 1;
					++TotalLateGroups;
				}
			}
			TestEqual(Name + TEXT(": no specular path kept late by the rays that the beams count early"), DoubleCounted, 0);
			TestEqual(Name + TEXT(": every excluded specular ray path is an early beam reflection"), ExcludedWithoutBeam, 0);
			TestEqual(Name + TEXT(": every late specular ray path of order <= K is a beam path after t_mix"), LateWithoutBeam, 0);
			TestEqual(Name + TEXT(": stats agree (groups without beam)"), Stats.NumRayGroupsWithoutBeam, 0);
			for (const FEarlyReflection& Reflection : Hybrid.EarlyReflections)
			{
				TestTrue(Name + TEXT(": hybrid early reflections arrive by t_mix"), Reflection.TimeSeconds <= Case.MixingTime && Reflection.Order <= Case.K);
			}
			TotalDropped += Stats.NumBeamDropped;

			// Energies: beams and rays estimate the same early-specular block; totals agree within the ray noise.
			const double EarlyBlockError = RelativeDifference(Stats.BeamEarlyEnergy, Stats.RayExcludedEnergy);
			const double TotalError = RelativeDifference(Hybrid.Histogram.TotalEnergy(0), Pure.Histogram.TotalEnergy(0));
			const double WindowError = RelativeDifference(WindowEnergy(Hybrid.Histogram, Case.MixingTime), WindowEnergy(Pure.Histogram, Case.MixingTime));
			AddInfo(FString::Printf(TEXT("%s: early-specular block beams %.5g vs rays %.5g (%.2f %%), 0..t_mix hybrid vs ray %.2f %%, total %.2f %%; groups excluded %d, late %d, beam reflections early %d, dropped %d"),
				*Name, Stats.BeamEarlyEnergy, Stats.RayExcludedEnergy, EarlyBlockError * 100.0, WindowError * 100.0, TotalError * 100.0,
				Stats.NumRayGroupsExcluded, Stats.NumRayGroupsLate, Stats.NumBeamEarly, Stats.NumBeamDropped));
			TestTrue(FString::Printf(TEXT("%s: early-specular energy beams vs rays within 10 %% (%.2f %%)"), *Name, EarlyBlockError * 100.0), EarlyBlockError <= 0.10);
			TestTrue(FString::Printf(TEXT("%s: energy 0..t_mix hybrid vs pure ray within 5 %% (%.2f %%)"), *Name, WindowError * 100.0), WindowError <= 0.05);
			TestTrue(FString::Printf(TEXT("%s: total energy hybrid vs pure ray within 2 %% (%.2f %%)"), *Name, TotalError * 100.0), TotalError <= 0.02);
		}
		if (Case.K == 3)
		{
			TestTrue(Tag + TEXT(": t_mix drops beam reflections of order <= K"), TotalDropped > 0);
			TestTrue(Tag + TEXT(": the rays keep specular paths of order <= K after t_mix"), TotalLateGroups > 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsHybridDeterminismTest, "AcousticsLab.Core.Solvers.HybridDeterminism",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsHybridDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsHybridTests;
	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();
	const FAcousticScene Scene = FAcousticScene::MakeShoebox(RoomSize, FAcousticMaterialProps(TEXT("Hybrid"), 0.15, 0.3));

	const FHybridResult Base = FHybridTracer::Run(Scene, SourcePosition, Receivers, TestSettings(8000, 2, 0.080, 1));
	if (!TestTrue(TEXT("single-thread run succeeded"), Base.IsValid() && Base.Hybrid->bSuccess))
	{
		return false;
	}
	for (const int32 Threads : { 4, 0, 4 })
	{
		const FHybridResult Other = FHybridTracer::Run(Scene, SourcePosition, Receivers, TestSettings(8000, 2, 0.080, Threads));
		const FString Tag = FString::Printf(TEXT("%d threads"), Threads);
		if (!TestTrue(Tag + TEXT(": run succeeded"), Other.IsValid() && Other.Hybrid->bSuccess))
		{
			continue;
		}
		TestEqual(Tag + TEXT(": beam nodes"), Other.Hybrid->NumBeamNodes, Base.Hybrid->NumBeamNodes);
		for (int32 Index = 0; Index < Receivers.Num(); ++Index)
		{
			const FReceiverResult& A = Base.Hybrid->Receivers[Index];
			const FReceiverResult& B = Other.Hybrid->Receivers[Index];
			const FString Name = Tag + TEXT(" ") + A.Name;
			TestTrue(Name + TEXT(": hybrid histogram bit-identical"), A.Histogram.IsIdentical(B.Histogram));
			TestTrue(Name + TEXT(": pure ray histogram bit-identical"), Base.PureRay->Receivers[Index].Histogram.IsIdentical(Other.PureRay->Receivers[Index].Histogram));
			TestTrue(Name + TEXT(": pure beam histogram bit-identical"), Base.PureBeam->Receivers[Index].Histogram.IsIdentical(Other.PureBeam->Receivers[Index].Histogram));
			if (TestEqual(Name + TEXT(": early reflections"), B.EarlyReflections.Num(), A.EarlyReflections.Num()))
			{
				for (int32 Reflection = 0; Reflection < A.EarlyReflections.Num(); ++Reflection)
				{
					TestTrue(Name + TEXT(": same early reflection"), A.EarlyReflections[Reflection].TimeSeconds == B.EarlyReflections[Reflection].TimeSeconds
						&& A.EarlyReflections[Reflection].Energy[0] == B.EarlyReflections[Reflection].Energy[0] && A.EarlyReflections[Reflection].Triangles == B.EarlyReflections[Reflection].Triangles);
				}
			}
			const TArray<FSpecularRayArrival>& ArrivalsA = Base.PureRay->Receivers[Index].SpecularRayArrivals;
			const TArray<FSpecularRayArrival>& ArrivalsB = Other.PureRay->Receivers[Index].SpecularRayArrivals;
			if (TestEqual(Name + TEXT(": specular ray groups"), ArrivalsB.Num(), ArrivalsA.Num()))
			{
				for (int32 Group = 0; Group < ArrivalsA.Num(); ++Group)
				{
					TestTrue(Name + TEXT(": same specular group"), ArrivalsA[Group].SequenceKey == ArrivalsB[Group].SequenceKey && ArrivalsA[Group].NumHits == ArrivalsB[Group].NumHits
						&& ArrivalsA[Group].Energy == ArrivalsB[Group].Energy && ArrivalsA[Group].TimeSeconds == ArrivalsB[Group].TimeSeconds);
				}
			}
			TestEqual(Name + TEXT(": found by rays"), Other.Stats[Index].NumFoundByRays, Base.Stats[Index].NumFoundByRays);
			TestTrue(Name + TEXT(": T30 identical"), A.Metrics.Band(0).T30 == B.Metrics.Band(0).T30);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsHybridMissedReflectionsTest, "AcousticsLab.Core.Solvers.HybridMissedReflections",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsHybridMissedReflectionsTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsHybridTests;
	// SPEC §5 experiment 3: how many early specular reflections (order <= 2, by 80 ms) a pure ray run with N rays misses
	// compared with the beams, which never miss one. Shoebox alpha 0.1, s 0.1; the rays only need to live to t_mix.
	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();
	const FAcousticScene Scene = FAcousticScene::MakeShoebox(RoomSize, FAcousticMaterialProps(TEXT("Hybrid"), 0.1, 0.1));
	FString Table = TEXT("| rays N | R1 found / beams | R1 missed | R2 found / beams | R2 missed | ray hits R1 + R2 (early-specular) |\n|---|---|---|---|---|---|");
	TArray<int32> Missed;
	for (const int32 NumRays : { 250, 1000, 4000, 16000, 64000, 256000 })
	{
		FHybridTracerSettings Settings = TestSettings(NumRays, 2, 0.080);
		Settings.Ray.MaxTimeSeconds = 0.25;
		const FHybridResult Result = FHybridTracer::Run(Scene, SourcePosition, Receivers, Settings);
		if (!TestTrue(FString::Printf(TEXT("N %d: run succeeded"), NumRays), Result.IsValid() && Result.Hybrid->bSuccess))
		{
			continue;
		}
		const FHybridReceiverStats& S1 = Result.Stats[0];
		const FHybridReceiverStats& S2 = Result.Stats[1];
		TestEqual(FString::Printf(TEXT("N %d: found + missed = beam reflections"), NumRays), S1.NumFoundByRays + S1.NumMissedByRays, S1.NumBeamEarly);
		Table += FString::Printf(TEXT("\n| %d | %d / %d | %d | %d / %d | %d | %lld |"), NumRays, S1.NumFoundByRays, S1.NumBeamEarly, S1.NumMissedByRays,
			S2.NumFoundByRays, S2.NumBeamEarly, S2.NumMissedByRays, Result.PureRay->Receivers[0].NumExcludedHits + Result.PureRay->Receivers[1].NumExcludedHits);
		Missed.Add(S1.NumMissedByRays + S2.NumMissedByRays);
	}
	AddInfo(TEXT("SPEC 5 experiment 3, missed early reflections (order <= 2, t <= 80 ms):\n") + Table);
	if (TestEqual(TEXT("all N ran"), Missed.Num(), 6))
	{
		TestTrue(FString::Printf(TEXT("250 rays miss early reflections (%d)"), Missed[0]), Missed[0] > 0);
		TestEqual(TEXT("256000 rays find every early reflection"), Missed.Last(), 0);
		TestTrue(TEXT("more rays never miss more than 250 rays"), Missed.Last() <= Missed[0]);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
