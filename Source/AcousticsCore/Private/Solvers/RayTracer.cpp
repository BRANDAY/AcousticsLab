#include "Solvers/RayTracer.h"

#include "AcousticsCore.h"
#include "Async/ParallelFor.h"
#include "Geometry/AcousticScene.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Solvers/AcousticRandom.h"

DECLARE_CYCLE_STAT(TEXT("RayTracer Run"), STAT_AcousticsRayTracerRun, STATGROUP_Acoustics);

FRayTracerSettings::FRayTracerSettings()
{
	for (double& Value : AirAbsorption)
	{
		Value = Acoustics::DefaultAirAbsorptionPerMeter;
	}
}

bool FRayTracerSettings::Validate(FString& OutError) const
{
	if (NumRays < 1)
	{
		OutError = TEXT("NumRays must be at least 1");
		return false;
	}
	if (MaxOrder < 0)
	{
		OutError = TEXT("MaxOrder must not be negative");
		return false;
	}
	if (!(BinSeconds > 0.0) || !(MaxTimeSeconds > BinSeconds))
	{
		OutError = TEXT("BinSeconds must be positive and smaller than MaxTimeSeconds");
		return false;
	}
	if (!(EnergyThreshold > 0.0) || EnergyThreshold >= 1.0)
	{
		OutError = TEXT("EnergyThreshold must lie in (0, 1)");
		return false;
	}
	if (RaysPerChunk < 1)
	{
		OutError = TEXT("RaysPerChunk must be at least 1");
		return false;
	}
	return true;
}

namespace
{
	/** Read-only data shared by every worker. */
	struct FTraceContext
	{
		const FAcousticScene& Scene;
		FVector3d Source;
		TConstArrayView<FSolverReceiver> Receivers;
		const FRayTracerSettings& Settings;
		double SpeedOfSound = Acoustics::SpeedOfSoundAir;
		double E0 = 0.0;
		double EnergyCutoff = 0.0;
		double MaxPathLength = 0.0;
	};

	/** Everything one chunk of rays produced; reduced in chunk order afterwards. */
	struct FChunkResult
	{
		TArray<FEnergyHistogram> Histograms;
		TArray<int64> ReceiverHits;
		TArray<FPathSample> Paths;
		int64 NumSegments = 0;
		int64 SumOrders = 0;
		int32 NumRays = 0;
		int32 NumEscaped = 0;
	};

	void TraceRay(const FTraceContext& Context, int32 RayIndex, FChunkResult& Out, FPathSample* Path)
	{
		const FRayTracerSettings& Settings = Context.Settings;
		FAcousticRandom Random(Settings.Seed, static_cast<uint64>(RayIndex));

		double Energy[Acoustics::NumBands];
		for (double& Value : Energy)
		{
			Value = Context.E0;
		}

		FVector3d Origin = Context.Source;
		FVector3d Direction = FRayTracer::FibonacciDirection(RayIndex, Settings.NumRays);
		double PathLength = 0.0;
		int32 Order = 0;
		int32 LastTriangle = INDEX_NONE;
		bool bReachedReceiver = false;

		if (Path)
		{
			Path->Points.Reset();
			Path->Points.Add(Origin);
		}

		for (;;)
		{
			FRayQueryOptions Options;
			Options.IgnoreTriangle = LastTriangle;
			FRayHit Hit;
			const FAcousticRay Ray(Origin, Direction);
			const bool bHit = Settings.bUseBVH ? Context.Scene.IntersectRay(Ray, Hit, Options) : Context.Scene.IntersectRayBruteForce(Ray, Hit, Options);
			const double SegmentLength = bHit ? Hit.T : (Context.MaxPathLength - PathLength);
			++Out.NumSegments;

			// Receivers are transparent: the energy is recorded at the entry point and the ray continues.
			// Order-0 segments are skipped when the direct sound is added analytically (SPEC §2.2.7).
			if (Order > 0 || !Settings.bAnalyticDirectSound)
			{
				for (int32 ReceiverIndex = 0; ReceiverIndex < Context.Receivers.Num(); ++ReceiverIndex)
				{
					const FSolverReceiver& Receiver = Context.Receivers[ReceiverIndex];
					double Entry = 0.0;
					if (!FRayTracer::IntersectSegmentSphere(Origin, Direction, SegmentLength, Receiver.Position, Receiver.Radius, Entry))
					{
						continue;
					}
					const double Time = (PathLength + Entry) / Context.SpeedOfSound;
					double Deposit[Acoustics::NumBands];
					for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
					{
						Deposit[Band] = Settings.bAirAbsorption ? Energy[Band] * FMath::Exp(-Settings.AirAbsorption[Band] * Entry) : Energy[Band];
					}
					if (Out.Histograms[ReceiverIndex].AddEnergy(Time, Deposit))
					{
						++Out.ReceiverHits[ReceiverIndex];
						if (Path && !bReachedReceiver)
						{
							Path->bReachedReceiver = true;
							Path->FirstReceiverHitOrder = Order;
							Path->FirstReceiverIndex = ReceiverIndex;
						}
						bReachedReceiver = true;
					}
				}
			}

			if (!bHit)
			{
				++Out.NumEscaped;
				if (Path)
				{
					Path->Points.Add(Origin + Direction * SegmentLength);
				}
				break;
			}

			PathLength += SegmentLength;
			if (Settings.bAirAbsorption)
			{
				for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
				{
					Energy[Band] *= FMath::Exp(-Settings.AirAbsorption[Band] * SegmentLength);
				}
			}

			const FAcousticTriangle& Triangle = Context.Scene.Triangles[Hit.TriangleIndex];
			const FAcousticMaterialProps& Material = Context.Scene.GetMaterialOf(Triangle);
			const FVector3d HitPoint = Ray.PointAt(Hit.T);
			++Order;
			if (Path)
			{
				Path->Points.Add(HitPoint);
			}

			double MaxEnergy = 0.0;
			for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
			{
				Energy[Band] *= Material.Reflection(Band);
				MaxEnergy = FMath::Max(MaxEnergy, Energy[Band]);
			}
			if (Order > Settings.MaxOrder || MaxEnergy < Context.EnergyCutoff || PathLength >= Context.MaxPathLength)
			{
				break;
			}

			// Reflect about the side the ray came from; a hit from behind (open geometry) uses the flipped normal.
			const FVector3d Facing = Hit.bBackFace ? -Triangle.N : Triangle.N;
			if (Random.NextDouble() < Material.Scattering[0])
			{
				Direction = FRayTracer::LambertDirection(Facing, Random.NextDouble(), Random.NextDouble());
			}
			else
			{
				Direction = FRayTracer::Reflect(Direction, Facing);
			}
			Origin = HitPoint + Facing * FRayTracer::SurfaceOffset;
			LastTriangle = Hit.TriangleIndex;
		}

		Out.SumOrders += Order;
		++Out.NumRays;
		if (Path)
		{
			Path->Order = Order;
			Path->FinalEnergy = Context.E0 > 0.0 ? Energy[0] / Context.E0 : 0.0;
		}
	}

	void TraceChunk(const FTraceContext& Context, int32 ChunkIndex, FChunkResult& Out)
	{
		const FRayTracerSettings& Settings = Context.Settings;
		const int32 FirstRay = ChunkIndex * Settings.RaysPerChunk;
		const int32 EndRay = FMath::Min(FirstRay + Settings.RaysPerChunk, Settings.NumRays);

		Out.Histograms.SetNum(Context.Receivers.Num());
		for (FEnergyHistogram& Histogram : Out.Histograms)
		{
			Histogram.Init(Settings.BinSeconds, Settings.MaxTimeSeconds);
		}
		Out.ReceiverHits.Init(0, Context.Receivers.Num());

		const int32 NumPaths = FMath::Clamp(Settings.MaxPathsForVisualization - FirstRay, 0, EndRay - FirstRay);
		Out.Paths.SetNum(NumPaths);

		for (int32 RayIndex = FirstRay; RayIndex < EndRay; ++RayIndex)
		{
			FPathSample* Path = RayIndex - FirstRay < NumPaths ? &Out.Paths[RayIndex - FirstRay] : nullptr;
			TraceRay(Context, RayIndex, Out, Path);
		}
	}
}

FVector3d FRayTracer::FibonacciDirection(int32 Index, int32 Count)
{
	// Golden-angle spiral: Count points with near-equal solid angles (SPEC §2.2.1).
	constexpr double GoldenAngle = UE_DOUBLE_PI * (3.0 - 2.2360679774997896964);
	const double Z = 1.0 - 2.0 * (static_cast<double>(Index) + 0.5) / static_cast<double>(FMath::Max(Count, 1));
	const double Radius = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
	const double Phi = GoldenAngle * static_cast<double>(Index);
	return FVector3d(Radius * FMath::Cos(Phi), Radius * FMath::Sin(Phi), Z);
}

FVector3d FRayTracer::Reflect(const FVector3d& Direction, const FVector3d& Normal)
{
	return (Direction - Normal * (2.0 * FVector3d::DotProduct(Direction, Normal))).GetSafeNormal();
}

FVector3d FRayTracer::LambertDirection(const FVector3d& Normal, double U1, double U2)
{
	const double Radius = FMath::Sqrt(U1);
	const double Theta = 2.0 * UE_DOUBLE_PI * U2;
	const double X = Radius * FMath::Cos(Theta);
	const double Y = Radius * FMath::Sin(Theta);
	const double Z = FMath::Sqrt(FMath::Max(0.0, 1.0 - U1));

	const FVector3d Helper = FMath::Abs(Normal.X) < 0.9 ? FVector3d(1.0, 0.0, 0.0) : FVector3d(0.0, 1.0, 0.0);
	const FVector3d Tangent = FVector3d::CrossProduct(Helper, Normal).GetSafeNormal();
	const FVector3d Bitangent = FVector3d::CrossProduct(Normal, Tangent);
	return (Tangent * X + Bitangent * Y + Normal * Z).GetSafeNormal();
}

bool FRayTracer::IntersectSegmentSphere(const FVector3d& Origin, const FVector3d& Direction, double Length, const FVector3d& Center, double Radius, double& OutEntryDistance)
{
	const FVector3d ToCenter = Center - Origin;
	const double Along = FVector3d::DotProduct(ToCenter, Direction);
	const double Perpendicular2 = ToCenter.SizeSquared() - Along * Along;
	const double Radius2 = Radius * Radius;
	if (Perpendicular2 > Radius2)
	{
		return false;
	}
	const double HalfChord = FMath::Sqrt(FMath::Max(0.0, Radius2 - Perpendicular2));
	OutEntryDistance = Along - HalfChord;
	return OutEntryDistance >= 0.0 && OutEntryDistance <= Length;
}

int32 FRayTracer::ResolveNumWorkers(int32 Requested)
{
	if (Requested > 0)
	{
		return Requested;
	}
	return FMath::Max(1, FPlatformMisc::NumberOfWorkerThreadsToSpawn());
}

FSimulationResult FRayTracer::Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> InReceivers, const FRayTracerSettings& Settings, FSolverProgress* Progress)
{
	SCOPE_CYCLE_COUNTER(STAT_AcousticsRayTracerRun);
	TRACE_CPUPROFILER_EVENT_SCOPE(FRayTracer::Run);

	const double StartTime = FPlatformTime::Seconds();
	FSimulationResult Result;
	Result.SolverName = Name();
	Result.SourcePosition = Source;
	Result.NumRays = Settings.NumRays;
	Result.NumTriangles = Scene.NumTriangles();

	if (!Settings.Validate(Result.Error))
	{
		UE_LOG(LogAcoustics, Error, TEXT("RayTracer: invalid settings: %s"), *Result.Error);
		return Result;
	}
	if (Settings.bUseBVH && !Scene.HasBVH() && Scene.NumTriangles() > 0)
	{
		Result.Error = TEXT("scene has no BVH for its triangles (call BuildBVH) and bUseBVH is set");
		UE_LOG(LogAcoustics, Error, TEXT("RayTracer: %s"), *Result.Error);
		return Result;
	}

	// Receivers with a non-positive radius take the settings' radius.
	TArray<FSolverReceiver> Receivers(InReceivers.GetData(), InReceivers.Num());
	for (FSolverReceiver& Receiver : Receivers)
	{
		if (!(Receiver.Radius > 0.0))
		{
			Receiver.Radius = Settings.ReceiverRadius;
		}
	}

	FTraceContext Context{ Scene, Source, Receivers, Settings };
	Context.SpeedOfSound = Scene.GetMedium(0).SpeedOfSound;
	Context.E0 = 1.0 / static_cast<double>(Settings.NumRays);
	Context.EnergyCutoff = Context.E0 * Settings.EnergyThreshold;
	Context.MaxPathLength = Settings.MaxTimeSeconds * Context.SpeedOfSound;

	const int32 NumChunks = FMath::DivideAndRoundUp(Settings.NumRays, Settings.RaysPerChunk);
	const int32 NumWorkers = FMath::Min(ResolveNumWorkers(Settings.NumThreads), NumChunks);
	Result.Timing.NumWorkers = NumWorkers;
	if (Progress)
	{
		Progress->Reset(Settings.NumRays);
	}

	TArray<FChunkResult> Chunks;
	Chunks.SetNum(NumChunks);
	Result.Timing.SetupSeconds = FPlatformTime::Seconds() - StartTime;

	const double TraceStart = FPlatformTime::Seconds();
	ParallelFor(TEXT("AcousticsRayTracer"), NumWorkers, 1,
		[&Context, &Chunks, NumChunks, NumWorkers, Progress](int32 Worker)
		{
			for (int32 Chunk = Worker; Chunk < NumChunks; Chunk += NumWorkers)
			{
				if (Progress && Progress->IsCancelRequested())
				{
					return;
				}
				TraceChunk(Context, Chunk, Chunks[Chunk]);
				if (Progress)
				{
					Progress->Add(Chunks[Chunk].NumRays);
				}
			}
		},
		NumWorkers == 1 ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
	Result.Timing.TraceSeconds = FPlatformTime::Seconds() - TraceStart;

	Result.bCancelled = Progress && Progress->IsCancelRequested();

	// Fixed-order reduction: chunk 0, 1, 2, ... whatever thread produced them.
	const double MetricsStart = FPlatformTime::Seconds();
	Result.Receivers.SetNum(Receivers.Num());
	for (int32 ReceiverIndex = 0; ReceiverIndex < Receivers.Num(); ++ReceiverIndex)
	{
		FReceiverResult& Out = Result.Receivers[ReceiverIndex];
		Out.Name = Receivers[ReceiverIndex].Name;
		Out.Position = Receivers[ReceiverIndex].Position;
		Out.Radius = Receivers[ReceiverIndex].Radius;
		Out.Histogram.Init(Settings.BinSeconds, Settings.MaxTimeSeconds);
	}

	int64 SumOrders = 0;
	int32 NumTracedRays = 0;
	for (const FChunkResult& Chunk : Chunks)
	{
		if (Chunk.NumRays == 0)
		{
			continue;
		}
		for (int32 ReceiverIndex = 0; ReceiverIndex < Receivers.Num(); ++ReceiverIndex)
		{
			Result.Receivers[ReceiverIndex].Histogram.Accumulate(Chunk.Histograms[ReceiverIndex]);
			Result.Receivers[ReceiverIndex].NumRayHits += Chunk.ReceiverHits[ReceiverIndex];
		}
		Result.Paths.Append(Chunk.Paths);
		Result.NumSegments += Chunk.NumSegments;
		Result.NumRaysEscaped += Chunk.NumEscaped;
		SumOrders += Chunk.SumOrders;
		NumTracedRays += Chunk.NumRays;
	}
	Result.MeanOrder = NumTracedRays > 0 ? static_cast<double>(SumOrders) / static_cast<double>(NumTracedRays) : 0.0;

	// Direct sound: r^2 / (4 d^2) at d / c when the segment source-receiver is free (SPEC §2.2.7).
	for (FReceiverResult& Out : Result.Receivers)
	{
		Out.DirectDistance = (Out.Position - Source).Length();
		Out.DirectArrivalSeconds = Out.DirectDistance / Context.SpeedOfSound;
		Out.bDirectOccluded = Scene.IsOccluded(Source, Out.Position);
		if (Settings.bAnalyticDirectSound && !Out.bDirectOccluded)
		{
			const double Distance2 = FMath::Max(Out.DirectDistance * Out.DirectDistance, 1e-12);
			const double Direct = FMath::Min(1.0, Out.Radius * Out.Radius / (4.0 * Distance2));
			double Deposit[Acoustics::NumBands];
			for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
			{
				Deposit[Band] = Settings.bAirAbsorption ? Direct * FMath::Exp(-Settings.AirAbsorption[Band] * Out.DirectDistance) : Direct;
			}
			if (Out.Histogram.AddEnergy(Out.DirectArrivalSeconds, Deposit))
			{
				Out.DirectEnergy = Deposit[0];
			}
		}
		Out.Metrics = FRoomMetrics::Compute(Out.Histogram, Out.DirectArrivalSeconds);
	}
	Result.Timing.MetricsSeconds = FPlatformTime::Seconds() - MetricsStart;
	Result.Timing.TotalSeconds = FPlatformTime::Seconds() - StartTime;
	Result.bSuccess = !Result.bCancelled;

	UE_LOG(LogAcoustics, Log, TEXT("%s"), *Result.Describe());
	return Result;
}
