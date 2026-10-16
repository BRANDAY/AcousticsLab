#include "Solvers/RayTracer.h"

#include "AcousticsCore.h"
#include "Async/ParallelFor.h"
#include "Geometry/AcousticScene.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Solvers/AcousticRandom.h"
#include "Solvers/ImageSourceSolver.h"

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
	if (ExcludeSpecularMaxOrder < 0 || !(ExcludeSpecularMaxSeconds >= 0.0))
	{
		OutError = TEXT("ExcludeSpecularMaxOrder and ExcludeSpecularMaxSeconds must not be negative");
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
		/** Rays per chunk actually used: Settings.RaysPerChunk, raised so that NumChunks <= MaxChunks (EffectiveRaysPerChunk). */
		int32 RaysPerChunk = 1024;
		/** Cancel flag and progress, checked / advanced every CancelCheckRays rays inside a chunk. */
		FSolverProgress* Progress = nullptr;
		double SpeedOfSound = Acoustics::SpeedOfSoundAir;
		double E0 = 0.0;
		double EnergyCutoff = 0.0;
		double MaxPathLength = 0.0;

		/** Hybrid split (M8): early-specular deposits of order 1..SpecularMaxOrder go to the excluded histograms. */
		int32 SpecularMaxOrder = 0;
		double ExcludedMaxTime = 0.0;
		TArray<int32> TriangleSurface;
	};

	/** Everything one chunk of rays produced; reduced in chunk order afterwards. */
	struct FChunkResult
	{
		TArray<FEnergyHistogram> Histograms;
		TArray<int64> ReceiverHits;
		TArray<FPathSample> Paths;

		/** Hybrid split (M8): early-specular deposits (histograms only as long as t_mix needs) and the specular groups. */
		TArray<FEnergyHistogram> ExcludedHistograms;
		TArray<int64> ExcludedHits;
		TArray<TMap<uint64, FSpecularRayArrival>> Arrivals;

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

		// Hybrid split (M8): purely specular so far, and the surfaces reflected from (while the order is <= K).
		const bool bTrackSpecular = Context.SpecularMaxOrder > 0;
		bool bDiffuse = false;
		uint64 SequenceKey = FRayTracer::SequenceKeyStart;

		if (Path)
		{
			Path->RayIndex = RayIndex;
			Path->Points.Reset();
			Path->Points.Add(Origin);
			Path->SegmentEnergies.Reset();
			Path->ReceiverHits.Reset();
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
			if (Path)
			{
				Path->SegmentEnergies.Add(Context.E0 > 0.0 ? Energy[0] / Context.E0 : 0.0);
			}

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

					// Early-specular (SPEC §2.8 as stated in M8.md): no diffuse reflection yet, order 1..K, image-source delay
					// to the receiver centre <= t_mix. The beams of the hybrid count exactly these paths.
					const bool bSpecularCandidate = bTrackSpecular && !bDiffuse && Order >= 1 && Order <= Context.SpecularMaxOrder;
					double SpecularTime = 0.0;
					bool bEarlySpecular = false;
					if (bSpecularCandidate)
					{
						SpecularTime = FRayTracer::ImageSourceDistance(Origin, Direction, PathLength, Receiver.Position) / Context.SpeedOfSound;
						bEarlySpecular = SpecularTime <= Settings.ExcludeSpecularMaxSeconds;
					}
					FEnergyHistogram& Target = bEarlySpecular ? Out.ExcludedHistograms[ReceiverIndex] : Out.Histograms[ReceiverIndex];
					if (Target.AddEnergy(Time, Deposit))
					{
						++(bEarlySpecular ? Out.ExcludedHits[ReceiverIndex] : Out.ReceiverHits[ReceiverIndex]);
						if (bSpecularCandidate && Settings.bRecordSpecularArrivals)
						{
							FSpecularRayArrival& Arrival = Out.Arrivals[ReceiverIndex].FindOrAdd(SequenceKey);
							if (Arrival.NumHits == 0)
							{
								Arrival.SequenceKey = SequenceKey;
								Arrival.Order = Order;
								Arrival.TimeSeconds = SpecularTime;
								Arrival.ImageSource = Origin - Direction * PathLength;
								Arrival.bExcluded = bEarlySpecular;
							}
							++Arrival.NumHits;
							Arrival.Energy += Deposit[0];
						}
						if (Path)
						{
							if (!bReachedReceiver)
							{
								Path->bReachedReceiver = true;
								Path->FirstReceiverHitOrder = Order;
								Path->FirstReceiverIndex = ReceiverIndex;
							}
							FPathReceiverHit& PathHit = Path->ReceiverHits.AddDefaulted_GetRef();
							PathHit.ReceiverIndex = ReceiverIndex;
							PathHit.Order = Order;
							PathHit.EntryPoint = Origin + Direction * Entry;
							PathHit.TimeSeconds = Time;
							PathHit.Energy = Context.E0 > 0.0 ? Deposit[0] / Context.E0 : 0.0;
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
			if (bTrackSpecular && !bDiffuse && Order <= Context.SpecularMaxOrder && Context.TriangleSurface.IsValidIndex(Hit.TriangleIndex))
			{
				SequenceKey = FRayTracer::CombineSequenceKey(SequenceKey, Context.TriangleSurface[Hit.TriangleIndex]);
			}
			if (Random.NextDouble() < Material.Scattering[0])
			{
				Direction = FRayTracer::LambertDirection(Facing, Random.NextDouble(), Random.NextDouble());
				bDiffuse = true;
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
		const int32 FirstRay = ChunkIndex * Context.RaysPerChunk;
		const int32 EndRay = FMath::Min(FirstRay + Context.RaysPerChunk, Settings.NumRays);

		Out.Histograms.SetNum(Context.Receivers.Num());
		for (FEnergyHistogram& Histogram : Out.Histograms)
		{
			Histogram.Init(Settings.BinSeconds, Settings.MaxTimeSeconds);
		}
		Out.ReceiverHits.Init(0, Context.Receivers.Num());
		if (Context.SpecularMaxOrder > 0)
		{
			Out.ExcludedHistograms.SetNum(Context.Receivers.Num());
			for (FEnergyHistogram& Histogram : Out.ExcludedHistograms)
			{
				Histogram.Init(Settings.BinSeconds, Context.ExcludedMaxTime);
			}
			Out.ExcludedHits.Init(0, Context.Receivers.Num());
			Out.Arrivals.SetNum(Context.Receivers.Num());
		}

		// Path samples: every Stride-th ray (FRayTracer::PathSampleStride), appended in ray order.
		int32 NumPaths = 0;
		for (int32 RayIndex = FirstRay; RayIndex < EndRay; ++RayIndex)
		{
			NumPaths += FRayTracer::IsPathSampled(RayIndex, Settings.NumRays, Settings.MaxPathsForVisualization) ? 1 : 0;
		}
		Out.Paths.Reset(NumPaths);

		// Stop (SPEC §3.6 panel) must answer within a fraction of a second even for long chunks: check the flag and
		// advance the progress every CancelCheckRays rays.
		constexpr int32 CancelCheckRays = 64;
		int32 SinceCheck = 0;
		for (int32 RayIndex = FirstRay; RayIndex < EndRay; ++RayIndex)
		{
			FPathSample* Path = FRayTracer::IsPathSampled(RayIndex, Settings.NumRays, Settings.MaxPathsForVisualization) ? &Out.Paths.AddDefaulted_GetRef() : nullptr;
			TraceRay(Context, RayIndex, Out, Path);
			if (Context.Progress && ++SinceCheck == CancelCheckRays)
			{
				Context.Progress->Add(SinceCheck);
				SinceCheck = 0;
				if (Context.Progress->IsCancelRequested())
				{
					return;
				}
			}
		}
		if (Context.Progress && SinceCheck > 0)
		{
			Context.Progress->Add(SinceCheck);
		}
	}
}

int32 FRayTracer::EffectiveRaysPerChunk(const FRayTracerSettings& Settings)
{
	// Every chunk keeps a histogram per receiver until the fixed-order reduction; bounding the chunk count bounds that
	// memory (5 M rays x 74 receivers x 6 s at 1 ms took ~17 GB with 1024-ray chunks). Depends on NumRays only, so the
	// result stays independent of the worker count.
	return FMath::Max(FMath::Max(1, Settings.RaysPerChunk), FMath::DivideAndRoundUp(Settings.NumRays, MaxChunks));
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

void FRayTracer::BuildTriangleSurfaceMap(const FAcousticScene& Scene, TArray<int32>& OutTriangleSurface)
{
	TArray<FReflectingSurface> Surfaces;
	FImageSourceSolver::BuildSurfaces(Scene, Surfaces);
	OutTriangleSurface.Init(INDEX_NONE, Scene.Triangles.Num());
	for (int32 SurfaceIndex = 0; SurfaceIndex < Surfaces.Num(); ++SurfaceIndex)
	{
		for (const int32 TriangleIndex : Surfaces[SurfaceIndex].Triangles)
		{
			OutTriangleSurface[TriangleIndex] = SurfaceIndex;
		}
	}
}

uint64 FRayTracer::CombineSequenceKey(uint64 Key, int32 Surface)
{
	const uint32 Value = static_cast<uint32>(Surface);
	for (int32 Byte = 0; Byte < 4; ++Byte)
	{
		Key ^= static_cast<uint64>((Value >> (8 * Byte)) & 0xFFu);
		Key *= 1099511628211ull;
	}
	return Key;
}

uint64 FRayTracer::SurfaceSequenceKey(TConstArrayView<int32> Triangles, TConstArrayView<int32> TriangleSurface)
{
	uint64 Key = SequenceKeyStart;
	for (const int32 Triangle : Triangles)
	{
		Key = CombineSequenceKey(Key, TriangleSurface.IsValidIndex(Triangle) ? TriangleSurface[Triangle] : INDEX_NONE);
	}
	return Key;
}

double FRayTracer::ImageSourceDistance(const FVector3d& Origin, const FVector3d& Direction, double PathLength, const FVector3d& Point)
{
	const FVector3d ToPoint = Point - Origin;
	const double Along = FVector3d::DotProduct(ToPoint, Direction);
	const double Perpendicular2 = FMath::Max(0.0, ToPoint.SizeSquared() - Along * Along);
	const double Unfolded = PathLength + Along;
	return FMath::Sqrt(Unfolded * Unfolded + Perpendicular2);
}

int32 FRayTracer::ResolveNumWorkers(int32 Requested)
{
	if (Requested > 0)
	{
		return Requested;
	}
	return FMath::Max(1, FPlatformMisc::NumberOfWorkerThreadsToSpawn());
}

int32 FRayTracer::PathSampleStride(int32 NumRays, int32 MaxPaths)
{
	if (NumRays <= 0 || MaxPaths <= 0)
	{
		return 1;
	}
	return FMath::Max(1, NumRays / MaxPaths);
}

bool FRayTracer::IsPathSampled(int32 RayIndex, int32 NumRays, int32 MaxPaths)
{
	if (MaxPaths <= 0 || RayIndex < 0 || RayIndex >= NumRays)
	{
		return false;
	}
	const int32 Stride = PathSampleStride(NumRays, MaxPaths);
	return RayIndex % Stride == 0 && RayIndex / Stride < MaxPaths;
}

int32 FRayTracer::NumPathSamples(int32 NumRays, int32 MaxPaths)
{
	if (MaxPaths <= 0 || NumRays <= 0)
	{
		return 0;
	}
	const int32 Stride = PathSampleStride(NumRays, MaxPaths);
	return FMath::Min(MaxPaths, FMath::DivideAndRoundUp(NumRays, Stride));
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
	Result.PathSampleStride = PathSampleStride(Settings.NumRays, Settings.MaxPathsForVisualization);

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
	Context.RaysPerChunk = EffectiveRaysPerChunk(Settings);
	Context.Progress = Progress;
	Context.SpeedOfSound = Scene.GetMedium(0).SpeedOfSound;
	Context.E0 = 1.0 / static_cast<double>(Settings.NumRays);
	Context.EnergyCutoff = Context.E0 * Settings.EnergyThreshold;
	Context.MaxPathLength = Settings.MaxTimeSeconds * Context.SpeedOfSound;
	Context.SpecularMaxOrder = Settings.ExcludeSpecularMaxOrder;
	if (Context.SpecularMaxOrder > 0)
	{
		// An early-specular deposit enters the sphere no later than its image-source delay, so the per-chunk excluded
		// histograms only need to reach t_mix (plus two bins for the rounding).
		Context.ExcludedMaxTime = FMath::Min(Settings.MaxTimeSeconds, Settings.ExcludeSpecularMaxSeconds + 2.0 * Settings.BinSeconds);
		BuildTriangleSurfaceMap(Scene, Context.TriangleSurface);
	}

	const int32 NumChunks = FMath::DivideAndRoundUp(Settings.NumRays, Context.RaysPerChunk);
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
				TraceChunk(Context, Chunk, Chunks[Chunk]); // advances the progress itself
			}
		},
		// Background priority: each item runs a worker's whole share of the run, and with the default (normal) priority the
		// items take the scheduler's few foreground workers (Core/Private/Async/Fundamental/Scheduler.cpp:243-246) for the
		// whole run, so the game / render threads starve waiting on their own tasks and the editor freezes (Stop cannot be
		// clicked). Background workers leave the foreground ones to the engine.
		NumWorkers == 1 ? EParallelForFlags::ForceSingleThread : EParallelForFlags::BackgroundPriority);
	Result.Timing.TraceSeconds = FPlatformTime::Seconds() - TraceStart;

	Result.bCancelled = Progress && Progress->IsCancelRequested();
	if (Result.bCancelled)
	{
		// Partial chunks are not a result: skip the reduction and the metrics, so Stop returns at once.
		Result.Timing.TotalSeconds = FPlatformTime::Seconds() - StartTime;
		UE_LOG(LogAcoustics, Log, TEXT("%s"), *Result.Describe());
		return Result;
	}

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
		if (Context.SpecularMaxOrder > 0)
		{
			Out.ExcludedHistogram.Init(Settings.BinSeconds, Settings.MaxTimeSeconds);
		}
	}
	TArray<TMap<uint64, FSpecularRayArrival>> MergedArrivals;
	MergedArrivals.SetNum(Context.SpecularMaxOrder > 0 ? Receivers.Num() : 0);

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
			if (Context.SpecularMaxOrder > 0)
			{
				FReceiverResult& Out = Result.Receivers[ReceiverIndex];
				const FEnergyHistogram& Excluded = Chunk.ExcludedHistograms[ReceiverIndex];
				const int32 NumBins = FMath::Min(Excluded.NumBins, Out.ExcludedHistogram.NumBins);
				for (int32 Bin = 0; Bin < NumBins; ++Bin)
				{
					for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
					{
						Out.ExcludedHistogram.At(Bin, Band) += Excluded.At(Bin, Band);
					}
				}
				Out.NumExcludedHits += Chunk.ExcludedHits[ReceiverIndex];
				// Groups in chunk order, within a chunk in first-hit order: the sums do not depend on the worker count.
				for (const TPair<uint64, FSpecularRayArrival>& Pair : Chunk.Arrivals[ReceiverIndex])
				{
					if (FSpecularRayArrival* Existing = MergedArrivals[ReceiverIndex].Find(Pair.Key))
					{
						Existing->NumHits += Pair.Value.NumHits;
						Existing->Energy += Pair.Value.Energy;
					}
					else
					{
						MergedArrivals[ReceiverIndex].Add(Pair.Key, Pair.Value);
					}
				}
			}
		}
		Result.Paths.Append(Chunk.Paths);
		Result.NumSegments += Chunk.NumSegments;
		Result.NumRaysEscaped += Chunk.NumEscaped;
		SumOrders += Chunk.SumOrders;
		NumTracedRays += Chunk.NumRays;
	}
	Result.MeanOrder = NumTracedRays > 0 ? static_cast<double>(SumOrders) / static_cast<double>(NumTracedRays) : 0.0;
	for (int32 ReceiverIndex = 0; ReceiverIndex < MergedArrivals.Num(); ++ReceiverIndex)
	{
		TArray<FSpecularRayArrival>& Arrivals = Result.Receivers[ReceiverIndex].SpecularRayArrivals;
		MergedArrivals[ReceiverIndex].GenerateValueArray(Arrivals);
		Arrivals.Sort([](const FSpecularRayArrival& A, const FSpecularRayArrival& B)
			{
				return A.TimeSeconds != B.TimeSeconds ? A.TimeSeconds < B.TimeSeconds : A.SequenceKey < B.SequenceKey;
			});
	}

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
