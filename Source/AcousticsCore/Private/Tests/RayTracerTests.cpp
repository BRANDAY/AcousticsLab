// M2 solver tests (SPEC §4): shoebox T30 against Eyring, determinism for a fixed seed, thread-count invariance, primitives.
#include "Async/Async.h"
#include "Geometry/AcousticScene.h"
#include "Misc/AutomationTest.h"
#include "Results/RoomMetrics.h"
#include "Solvers/AcousticRandom.h"
#include "Solvers/RayTracer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AcousticsRayTracerTests
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

	static FRayTracerSettings TestSettings(int32 NumRays, uint32 Seed, int32 NumThreads = 0)
	{
		FRayTracerSettings Settings;
		Settings.NumRays = NumRays;
		Settings.Seed = Seed;
		Settings.NumThreads = NumThreads;
		Settings.MaxPathsForVisualization = 100;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsRayTracerPrimitivesTest, "AcousticsLab.Core.Solvers.RayTracerPrimitives",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsRayTracerPrimitivesTest::RunTest(const FString& Parameters)
{
	// Fibonacci sphere: unit vectors, zero mean, even hemispheres.
	{
		const int32 Count = 5000;
		FVector3d Sum = FVector3d::ZeroVector;
		int32 Upper = 0;
		double MinDot = 1.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector3d Direction = FRayTracer::FibonacciDirection(Index, Count);
			TestTrue(TEXT("unit length"), FMath::IsNearlyEqual(Direction.Length(), 1.0, 1e-12));
			Sum += Direction;
			Upper += Direction.Z > 0.0 ? 1 : 0;
			if (Index > 0)
			{
				MinDot = FMath::Min(MinDot, FVector3d::DotProduct(Direction, FRayTracer::FibonacciDirection(Index - 1, Count)));
			}
		}
		TestTrue(FString::Printf(TEXT("directions average to zero (|sum| = %.4f)"), Sum.Length()), Sum.Length() < 10.0);
		TestEqual(TEXT("half of the directions point up"), Upper, Count / 2);
	}

	// Specular reflection and the Lambert sampler.
	{
		const FVector3d Normal(0.0, 0.0, 1.0);
		const FVector3d Reflected = FRayTracer::Reflect(FVector3d(1.0, 0.0, -1.0).GetSafeNormal(), Normal);
		TestTrue(TEXT("mirror reflection"), Reflected.Equals(FVector3d(1.0, 0.0, 1.0).GetSafeNormal(), 1e-12));

		FAcousticRandom Random(7, 3);
		double SumCos = 0.0;
		const int32 Samples = 20000;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const FVector3d Direction = FRayTracer::LambertDirection(Normal, Random.NextDouble(), Random.NextDouble());
			TestTrue(TEXT("Lambert direction is unit"), FMath::IsNearlyEqual(Direction.Length(), 1.0, 1e-12));
			TestTrue(TEXT("Lambert direction is in the hemisphere"), Direction.Z > 0.0);
			SumCos += Direction.Z;
		}
		TestTrue(FString::Printf(TEXT("mean cosine of a cosine-weighted hemisphere is 2/3 (got %.4f)"), SumCos / Samples), FMath::IsNearlyEqual(SumCos / Samples, 2.0 / 3.0, 0.01));
	}

	// Segment-sphere entry.
	{
		double Entry = 0.0;
		TestTrue(TEXT("hit through the center"), FRayTracer::IntersectSegmentSphere(FVector3d::ZeroVector, FVector3d(1.0, 0.0, 0.0), 10.0, FVector3d(5.0, 0.0, 0.0), 0.5, Entry));
		TestTrue(TEXT("entry at 4.5"), FMath::IsNearlyEqual(Entry, 4.5, 1e-12));
		TestTrue(TEXT("grazing hit"), FRayTracer::IntersectSegmentSphere(FVector3d::ZeroVector, FVector3d(1.0, 0.0, 0.0), 10.0, FVector3d(5.0, 0.4, 0.0), 0.5, Entry));
		TestTrue(TEXT("grazing entry"), FMath::IsNearlyEqual(Entry, 5.0 - 0.3, 1e-12));
		TestFalse(TEXT("miss beside the sphere"), FRayTracer::IntersectSegmentSphere(FVector3d::ZeroVector, FVector3d(1.0, 0.0, 0.0), 10.0, FVector3d(5.0, 0.6, 0.0), 0.5, Entry));
		TestFalse(TEXT("sphere beyond the segment"), FRayTracer::IntersectSegmentSphere(FVector3d::ZeroVector, FVector3d(1.0, 0.0, 0.0), 4.0, FVector3d(5.0, 0.0, 0.0), 0.5, Entry));
		TestFalse(TEXT("sphere behind the origin"), FRayTracer::IntersectSegmentSphere(FVector3d::ZeroVector, FVector3d(1.0, 0.0, 0.0), 10.0, FVector3d(-5.0, 0.0, 0.0), 0.5, Entry));
		TestFalse(TEXT("origin inside the sphere"), FRayTracer::IntersectSegmentSphere(FVector3d::ZeroVector, FVector3d(1.0, 0.0, 0.0), 10.0, FVector3d(0.1, 0.0, 0.0), 0.5, Entry));
	}

	// Random streams: reproducible, independent per ray, uniform.
	{
		FAcousticRandom A(123, 5);
		FAcousticRandom B(123, 5);
		FAcousticRandom C(123, 6);
		bool bSame = true;
		bool bDifferent = false;
		double Mean = 0.0;
		for (int32 Index = 0; Index < 10000; ++Index)
		{
			const double ValueA = A.NextDouble();
			bSame &= ValueA == B.NextDouble();
			bDifferent |= ValueA != C.NextDouble();
			TestTrue(TEXT("in [0, 1)"), ValueA >= 0.0 && ValueA < 1.0);
			Mean += ValueA;
		}
		TestTrue(TEXT("same seed and stream repeat"), bSame);
		TestTrue(TEXT("other stream differs"), bDifferent);
		TestTrue(TEXT("mean near 0.5"), FMath::IsNearlyEqual(Mean / 10000.0, 0.5, 0.02));
	}

	// Direct sound only: a source and a receiver in free space, no triangles.
	{
		FAcousticScene Empty;
		FRayTracerSettings Settings = AcousticsRayTracerTests::TestSettings(100, 1, 1);
		Settings.bUseBVH = false;
		const FSolverReceiver Receiver(TEXT("R"), FVector3d(4.0, 0.0, 0.0), 0.5);
		const FSimulationResult Result = FRayTracer::Run(Empty, FVector3d::ZeroVector, MakeArrayView(&Receiver, 1), Settings);
		TestTrue(TEXT("run succeeds in free space"), Result.bSuccess);
		TestEqual(TEXT("every ray escapes"), Result.NumRaysEscaped, 100);
		TestTrue(TEXT("direct energy r^2 / (4 d^2)"), FMath::IsNearlyEqual(Result.Receivers[0].DirectEnergy, 0.25 / 64.0, 1e-12));
		TestTrue(TEXT("direct arrival d / c"), FMath::IsNearlyEqual(Result.Receivers[0].DirectArrivalSeconds, 4.0 / 343.0, 1e-12));
		TestTrue(TEXT("histogram holds only the direct sound"), FMath::IsNearlyEqual(Result.Receivers[0].Histogram.TotalEnergy(0), 0.25 / 64.0, 1e-12));
		TestEqual(TEXT("no ray hits (direct sound is analytic)"), static_cast<int32>(Result.Receivers[0].NumRayHits), 0);
		TestEqual(TEXT("paths kept"), Result.Paths.Num(), 100);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsShoeboxSabineTest, "AcousticsLab.Core.Solvers.ShoeboxSabine",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsShoeboxSabineTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsRayTracerTests;

	const double Volume = RoomSize.X * RoomSize.Y * RoomSize.Z;
	const double Surface = 2.0 * (RoomSize.X * RoomSize.Y + RoomSize.X * RoomSize.Z + RoomSize.Y * RoomSize.Z);
	const double Absorptions[] = { 0.1, 0.2, 0.3 };
	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();

	// Sabine / Eyring assume a diffuse field, so the 20 % criterion applies to Lambert walls (s = 1). The same room
	// with almost specular walls (s = 0.1, the Concrete preset) decays slower than Eyring because rays grazing along
	// the long axis keep their energy (Kuttruff, non-diffuse rectangular rooms); it is reported for the table only.
	struct FVariant
	{
		double Scattering;
		bool bAssert;
		const TCHAR* Label;
	};
	const FVariant Variants[] = { { 1.0, true, TEXT("diffuse (s = 1.0)") }, { 0.1, false, TEXT("specular (s = 0.1)") } };

	for (const FVariant& Variant : Variants)
	{
		AddInfo(FString::Printf(TEXT("%s: alpha | Sabine | Eyring | T30 R1 | dev R1 | T30 R2 | dev R2 | trace time"), Variant.Label));
		for (const double Alpha : Absorptions)
		{
			const FAcousticScene Scene = FAcousticScene::MakeShoebox(RoomSize, FAcousticMaterialProps(TEXT("Validation"), Alpha, Variant.Scattering));
			const FRayTracerSettings Settings = TestSettings(20000, 20261015);
			const FSimulationResult Result = FRayTracer::Run(Scene, SourcePosition, Receivers, Settings);
			TestTrue(FString::Printf(TEXT("alpha %.1f: run succeeded"), Alpha), Result.bSuccess);
			TestEqual(FString::Printf(TEXT("alpha %.1f: two receivers"), Alpha), Result.Receivers.Num(), 2);
			if (Result.Receivers.Num() != 2)
			{
				continue;
			}

			const double Sabine = RoomAcoustics::SabineT60(Volume, Surface, Alpha);
			const double Eyring = RoomAcoustics::EyringT60(Volume, Surface, Alpha);
			FString Row = FString::Printf(TEXT("%.1f | %.3f | %.3f"), Alpha, Sabine, Eyring);
			for (const FReceiverResult& Receiver : Result.Receivers)
			{
				const FBandMetrics& Band = Receiver.Metrics.Band(0);
				const double Deviation = Band.bT30Valid ? (Band.T30 - Eyring) / Eyring : 0.0;
				Row += FString::Printf(TEXT(" | %.3f | %+.1f %%"), Band.T30, Deviation * 100.0);

				TestTrue(FString::Printf(TEXT("%s alpha %.1f %s: T30 valid"), Variant.Label, Alpha, *Receiver.Name), Band.bT30Valid);
				if (Variant.bAssert)
				{
					TestTrue(FString::Printf(TEXT("%s alpha %.1f %s: T30 %.3f within 20 %% of Eyring %.3f"), Variant.Label, Alpha, *Receiver.Name, Band.T30, Eyring), Band.bT30Valid && FMath::Abs(Deviation) <= 0.2);
				}
				TestTrue(FString::Printf(TEXT("%s alpha %.1f %s: rays reached the receiver (%lld hits)"), Variant.Label, Alpha, *Receiver.Name, Receiver.NumRayHits), Receiver.NumRayHits > 1000);
				TestFalse(FString::Printf(TEXT("alpha %.1f %s: direct sound unoccluded"), Alpha, *Receiver.Name), Receiver.bDirectOccluded);
				TestTrue(FString::Printf(TEXT("alpha %.1f %s: direct energy r^2 / (4 d^2)"), Alpha, *Receiver.Name),
					FMath::IsNearlyEqual(Receiver.DirectEnergy, 0.25 / (4.0 * Receiver.DirectDistance * Receiver.DirectDistance), 1e-12));
				TestTrue(FString::Printf(TEXT("alpha %.1f %s: clarity is finite"), Alpha, *Receiver.Name), FMath::Abs(Band.C50) < 50.0 && FMath::Abs(Band.C80) < 50.0);
				TestTrue(FString::Printf(TEXT("alpha %.1f %s: nothing before the direct sound"), Alpha, *Receiver.Name),
					Receiver.Histogram.FirstNonEmptyBin(0) >= Receiver.Histogram.BinIndex(Receiver.DirectArrivalSeconds));
			}
			Row += FString::Printf(TEXT(" | %.2f s (%d workers)"), Result.Timing.TraceSeconds, Result.Timing.NumWorkers);
			AddInfo(Row);

			TestEqual(FString::Printf(TEXT("alpha %.1f: no ray escapes the closed room"), Alpha), Result.NumRaysEscaped, 0);
			TestTrue(FString::Printf(TEXT("alpha %.1f: mean order %.1f is plausible"), Alpha, Result.MeanOrder), Result.MeanOrder > 10.0 && Result.MeanOrder <= 201.0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsDeterministicTest, "AcousticsLab.Core.Solvers.Deterministic",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsRayTracerTests;

	const FAcousticScene Scene = FAcousticScene::MakeShoebox(RoomSize, FAcousticMaterialProps(TEXT("Validation"), 0.2, 0.3));
	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();

	const FSimulationResult First = FRayTracer::Run(Scene, SourcePosition, Receivers, TestSettings(8000, 42));
	const FSimulationResult Second = FRayTracer::Run(Scene, SourcePosition, Receivers, TestSettings(8000, 42));
	const FSimulationResult Other = FRayTracer::Run(Scene, SourcePosition, Receivers, TestSettings(8000, 43));

	TestTrue(TEXT("runs succeeded"), First.bSuccess && Second.bSuccess && Other.bSuccess);
	for (int32 Index = 0; Index < Receivers.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("%s: same seed gives a bit-identical histogram"), *Receivers[Index].Name), First.Receivers[Index].Histogram.IsIdentical(Second.Receivers[Index].Histogram));
		TestEqual(FString::Printf(TEXT("%s: same seed gives the same hit count"), *Receivers[Index].Name), First.Receivers[Index].NumRayHits, Second.Receivers[Index].NumRayHits);
		TestTrue(FString::Printf(TEXT("%s: same seed gives the same T30"), *Receivers[Index].Name), First.Receivers[Index].Metrics.Band(0).T30 == Second.Receivers[Index].Metrics.Band(0).T30);
		const int32 Different = First.Receivers[Index].Histogram.NumDifferentBins(Other.Receivers[Index].Histogram, 0);
		TestTrue(FString::Printf(TEXT("%s: another seed changes the histogram (%d bins differ)"), *Receivers[Index].Name, Different), Different > 100);
	}
	TestEqual(TEXT("same seed gives the same segment count"), First.NumSegments, Second.NumSegments);
	TestEqual(TEXT("same seed gives the same paths"), First.Paths.Num(), Second.Paths.Num());
	bool bSamePaths = First.Paths.Num() == Second.Paths.Num();
	for (int32 Index = 0; bSamePaths && Index < First.Paths.Num(); ++Index)
	{
		bSamePaths = First.Paths[Index].Points == Second.Paths[Index].Points && First.Paths[Index].Order == Second.Paths[Index].Order;
	}
	TestTrue(TEXT("path samples are identical"), bSamePaths);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsThreadCountInvarianceTest, "AcousticsLab.Core.Solvers.ThreadCountInvariance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsThreadCountInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsRayTracerTests;

	const FAcousticScene Scene = FAcousticScene::MakeShoebox(RoomSize, FAcousticMaterialProps(TEXT("Validation"), 0.15, 0.2));
	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();

	FRayTracerSettings Settings = TestSettings(8000, 99, 1);
	Settings.RaysPerChunk = 500;
	const FSimulationResult Single = FRayTracer::Run(Scene, SourcePosition, Receivers, Settings);
	Settings.NumThreads = 4;
	const FSimulationResult Four = FRayTracer::Run(Scene, SourcePosition, Receivers, Settings);
	Settings.NumThreads = 0;
	const FSimulationResult Auto = FRayTracer::Run(Scene, SourcePosition, Receivers, Settings);

	TestEqual(TEXT("single-threaded run used one worker"), Single.Timing.NumWorkers, 1);
	TestEqual(TEXT("four-thread run used four workers"), Four.Timing.NumWorkers, 4);
	for (int32 Index = 0; Index < Receivers.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("%s: 1 and 4 workers give bit-identical histograms"), *Receivers[Index].Name), Single.Receivers[Index].Histogram.IsIdentical(Four.Receivers[Index].Histogram));
		TestTrue(FString::Printf(TEXT("%s: 1 and auto workers give bit-identical histograms"), *Receivers[Index].Name), Single.Receivers[Index].Histogram.IsIdentical(Auto.Receivers[Index].Histogram));
		TestTrue(FString::Printf(TEXT("%s: identical T30"), *Receivers[Index].Name), Single.Receivers[Index].Metrics.Band(0).T30 == Four.Receivers[Index].Metrics.Band(0).T30);
	}
	TestEqual(TEXT("same segment count"), Single.NumSegments, Four.NumSegments);
	TestEqual(TEXT("same number of paths"), Single.Paths.Num(), Four.Paths.Num());

	// Brute force and BVH see the same geometry.
	Settings.NumThreads = 1;
	Settings.bUseBVH = false;
	const FSimulationResult BruteForce = FRayTracer::Run(Scene, SourcePosition, Receivers, Settings);
	for (int32 Index = 0; Index < Receivers.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("%s: brute force matches the BVH run"), *Receivers[Index].Name), Single.Receivers[Index].Histogram.IsIdentical(BruteForce.Receivers[Index].Histogram));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsRayTracerCancelTest, "AcousticsLab.Core.Solvers.RayTracerCancel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsRayTracerCancelTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsRayTracerTests;

	// Chunk size: small runs keep RaysPerChunk (the other tests' reductions are unchanged), large runs stay <= MaxChunks.
	FRayTracerSettings Small = TestSettings(20000, 1, 0);
	TestEqual(TEXT("20 000 rays keep 1024-ray chunks"), FRayTracer::EffectiveRaysPerChunk(Small), Small.RaysPerChunk);
	FRayTracerSettings Large = TestSettings(5000000, 1, 0);
	const int32 LargeChunk = FRayTracer::EffectiveRaysPerChunk(Large);
	TestTrue(TEXT("5 M rays: at most MaxChunks chunks"), FMath::DivideAndRoundUp(Large.NumRays, LargeChunk) <= FRayTracer::MaxChunks);

	// Stop during a long run (5 M rays, low absorption: minutes uncancelled) returns within a fraction of a second.
	const FAcousticScene Scene = FAcousticScene::MakeShoebox(RoomSize, FAcousticMaterialProps(TEXT("Validation"), 0.05, 0.2));
	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();
	FSolverProgress Progress;
	TFuture<void> Canceller = Async(EAsyncExecution::Thread, [&Progress]()
	{
		FPlatformProcess::Sleep(0.3f);
		Progress.RequestCancel();
	});
	const double Start = FPlatformTime::Seconds();
	const FSimulationResult Result = FRayTracer::Run(Scene, SourcePosition, Receivers, Large, &Progress);
	const double Seconds = FPlatformTime::Seconds() - Start;
	Canceller.Wait();
	TestTrue(TEXT("cancelled run is flagged"), Result.bCancelled);
	TestFalse(TEXT("cancelled run is not a result"), Result.bSuccess);
	TestTrue(FString::Printf(TEXT("Stop answered quickly (%.2f s after a 0.3 s start)"), Seconds), Seconds < 2.0);
	TestTrue(TEXT("some rays were traced before the stop"), Progress.Fraction() > 0.0f && Progress.Fraction() < 1.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
