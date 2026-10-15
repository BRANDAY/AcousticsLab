#include "Solvers/ImageSourceSolver.h"

#include "AcousticsCore.h"
#include "Geometry/AcousticScene.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DECLARE_CYCLE_STAT(TEXT("ImageSourceSolver Run"), STAT_AcousticsImageSourceRun, STATGROUP_Acoustics);

FImageSourceSettings::FImageSourceSettings()
{
	for (double& Value : AirAbsorption)
	{
		Value = Acoustics::DefaultAirAbsorptionPerMeter;
	}
}

bool FImageSourceSettings::Validate(FString& OutError) const
{
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
	if (MaxImageSources < 1)
	{
		OutError = TEXT("MaxImageSources must be at least 1");
		return false;
	}
	return true;
}

void FImageSourceSolver::BuildSurfaces(const FAcousticScene& Scene, TArray<FReflectingSurface>& OutSurfaces, double NormalTolerance, double OffsetTolerance)
{
	OutSurfaces.Reset();
	for (int32 Index = 0; Index < Scene.Triangles.Num(); ++Index)
	{
		const FAcousticTriangle& Triangle = Scene.Triangles[Index];
		const double Offset = Triangle.PlaneOffset();
		FReflectingSurface* Found = nullptr;
		for (FReflectingSurface& Surface : OutSurfaces)
		{
			if (FVector3d::DotProduct(Surface.Plane.Normal, Triangle.N) > 1.0 - NormalTolerance && FMath::Abs(Surface.Plane.Offset - Offset) <= OffsetTolerance)
			{
				Found = &Surface;
				break;
			}
		}
		if (!Found)
		{
			Found = &OutSurfaces.AddDefaulted_GetRef();
			Found->Plane.Normal = Triangle.N;
			Found->Plane.Offset = Offset;
		}
		Found->Triangles.Add(Index);
	}
}

FVector3d FImageSourceSolver::MirrorPoint(const FVector3d& Point, const FAcousticPlane& Plane)
{
	return Point - Plane.Normal * (2.0 * Plane.SignedDistance(Point));
}

bool FImageSourceSolver::SegmentPlaneCrossing(const FVector3d& From, const FVector3d& To, const FAcousticPlane& Plane, FVector3d& OutPoint, double& OutT)
{
	const double DistanceFrom = Plane.SignedDistance(From);
	const double DistanceTo = Plane.SignedDistance(To);
	if ((DistanceFrom > 0.0) == (DistanceTo > 0.0) || DistanceFrom == DistanceTo)
	{
		return false;
	}
	OutT = DistanceFrom / (DistanceFrom - DistanceTo);
	if (!(OutT > 0.0 && OutT < 1.0))
	{
		return false;
	}
	OutPoint = From + (To - From) * OutT;
	return true;
}

int32 FImageSourceSolver::FindContainingTriangle(const FAcousticScene& Scene, const FReflectingSurface& Surface, const FVector3d& Point, double Tolerance)
{
	for (const int32 Index : Surface.Triangles)
	{
		const FAcousticTriangle& Triangle = Scene.Triangles[Index];
		// Barycentric test in the plane: inside when every weight is >= -Tolerance (relative to the triangle size).
		const FVector3d E0 = Triangle.V1 - Triangle.V0;
		const FVector3d E1 = Triangle.V2 - Triangle.V0;
		const FVector3d D = Point - Triangle.V0;
		const double D00 = FVector3d::DotProduct(E0, E0);
		const double D01 = FVector3d::DotProduct(E0, E1);
		const double D11 = FVector3d::DotProduct(E1, E1);
		const double D20 = FVector3d::DotProduct(D, E0);
		const double D21 = FVector3d::DotProduct(D, E1);
		const double Denominator = D00 * D11 - D01 * D01;
		if (FMath::Abs(Denominator) <= 1e-300)
		{
			continue;
		}
		const double V = (D11 * D20 - D01 * D21) / Denominator;
		const double W = (D00 * D21 - D01 * D20) / Denominator;
		const double U = 1.0 - V - W;
		if (U >= -Tolerance && V >= -Tolerance && W >= -Tolerance)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool FImageSourceSolver::IsSegmentOccluded(const FAcousticScene& Scene, const FVector3d& From, const FVector3d& To, bool bUseBVH, double Margin)
{
	const FVector3d Delta = To - From;
	const double Distance = Delta.Length();
	if (Distance <= 2.0 * Margin)
	{
		return false;
	}
	FRayQueryOptions Options;
	Options.TMin = Margin;
	Options.TMax = Distance - Margin;
	FRayHit Hit;
	const FAcousticRay Ray(From, Delta / Distance);
	return bUseBVH ? Scene.IntersectRay(Ray, Hit, Options) : Scene.IntersectRayBruteForce(Ray, Hit, Options);
}

namespace
{
	struct FImageChain
	{
		int32 Order = 0;
		int32 Surface = INDEX_NONE;
		int32 Parent = INDEX_NONE;
		FVector3d Position = FVector3d::ZeroVector;
	};

	/** Enumerates the image chains breadth-first (order 1, then 2, ...), so Images is sorted by order. */
	void EnumerateImages(const FVector3d& Source, const TArray<FReflectingSurface>& Surfaces, int32 MaxOrder, int32 Budget, TArray<FImageChain>& Images, bool& bOutTruncated)
	{
		Images.Reset();
		bOutTruncated = false;
		int32 LevelStart = 0;
		for (int32 Order = 1; Order <= MaxOrder; ++Order)
		{
			const int32 LevelEnd = Images.Num();
			const int32 NumParents = Order == 1 ? 1 : LevelEnd - LevelStart;
			for (int32 ParentSlot = 0; ParentSlot < NumParents; ++ParentSlot)
			{
				const int32 ParentIndex = Order == 1 ? INDEX_NONE : LevelStart + ParentSlot;
				const FVector3d Previous = ParentIndex == INDEX_NONE ? Source : Images[ParentIndex].Position;
				const int32 PreviousSurface = ParentIndex == INDEX_NONE ? INDEX_NONE : Images[ParentIndex].Surface;
				for (int32 SurfaceIndex = 0; SurfaceIndex < Surfaces.Num(); ++SurfaceIndex)
				{
					if (SurfaceIndex == PreviousSurface)
					{
						continue; // two reflections in a row off the same plane are impossible
					}
					const FAcousticPlane& Plane = Surfaces[SurfaceIndex].Plane;
					if (Plane.SignedDistance(Previous) <= 1e-9)
					{
						continue; // the previous image lies behind (or on) this wall: it cannot reflect it
					}
					if (Images.Num() >= Budget)
					{
						bOutTruncated = true;
						return;
					}
					FImageChain& Image = Images.AddDefaulted_GetRef();
					Image.Order = Order;
					Image.Surface = SurfaceIndex;
					Image.Parent = ParentIndex;
					Image.Position = FImageSourceSolver::MirrorPoint(Previous, Plane);
				}
			}
			LevelStart = LevelEnd;
		}
	}
}

FSimulationResult FImageSourceSolver::Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> InReceivers, const FImageSourceSettings& Settings, FSolverProgress* Progress)
{
	SCOPE_CYCLE_COUNTER(STAT_AcousticsImageSourceRun);
	TRACE_CPUPROFILER_EVENT_SCOPE(FImageSourceSolver::Run);

	const double StartTime = FPlatformTime::Seconds();
	FSimulationResult Result;
	Result.SolverName = Name();
	Result.SourcePosition = Source;
	Result.NumTriangles = Scene.NumTriangles();
	Result.Timing.NumWorkers = 1;

	if (!Settings.Validate(Result.Error))
	{
		UE_LOG(LogAcoustics, Error, TEXT("ImageSourceSolver: invalid settings: %s"), *Result.Error);
		return Result;
	}
	if (Settings.bUseBVH && !Scene.HasBVH() && Scene.NumTriangles() > 0)
	{
		Result.Error = TEXT("scene has no BVH for its triangles (call BuildBVH) and bUseBVH is set");
		UE_LOG(LogAcoustics, Error, TEXT("ImageSourceSolver: %s"), *Result.Error);
		return Result;
	}

	TArray<FSolverReceiver> Receivers(InReceivers.GetData(), InReceivers.Num());
	for (FSolverReceiver& Receiver : Receivers)
	{
		if (!(Receiver.Radius > 0.0))
		{
			Receiver.Radius = Settings.ReceiverRadius;
		}
	}
	const double SpeedOfSound = Scene.GetMedium(0).SpeedOfSound;

	// Surfaces and image chains (independent of the receivers).
	TArray<FReflectingSurface> Surfaces;
	BuildSurfaces(Scene, Surfaces);
	TArray<FImageChain> Images;
	bool bTruncated = false;
	EnumerateImages(Source, Surfaces, Settings.MaxOrder, Settings.MaxImageSources, Images, bTruncated);
	if (bTruncated)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("ImageSourceSolver: image budget of %d reached (%d surfaces, order %d); higher orders are incomplete"), Settings.MaxImageSources, Surfaces.Num(), Settings.MaxOrder);
	}
	Result.ImageSources.SetNum(Images.Num());
	for (int32 Index = 0; Index < Images.Num(); ++Index)
	{
		const FImageChain& Image = Images[Index];
		FImageSourceSample& Sample = Result.ImageSources[Index];
		Sample.Order = Image.Order;
		Sample.Position = Image.Position;
		Sample.Parent = Image.Parent;
		Sample.Triangles.Reserve(Image.Order);
		for (int32 Chain = Index; Chain != INDEX_NONE; Chain = Images[Chain].Parent)
		{
			Sample.Triangles.Insert(Surfaces[Images[Chain].Surface].Triangles[0], 0);
		}
	}
	Result.NumImageSources = Images.Num();
	Result.Timing.SetupSeconds = FPlatformTime::Seconds() - StartTime;

	if (Progress)
	{
		Progress->Reset(Receivers.Num());
	}
	const double TraceStart = FPlatformTime::Seconds();
	Result.Receivers.SetNum(Receivers.Num());
	TArray<FVector3d> PathPoints;
	TArray<int32> PathTriangles;
	for (int32 ReceiverIndex = 0; ReceiverIndex < Receivers.Num(); ++ReceiverIndex)
	{
		if (Progress && Progress->IsCancelRequested())
		{
			break;
		}
		const FSolverReceiver& Receiver = Receivers[ReceiverIndex];
		FReceiverResult& Out = Result.Receivers[ReceiverIndex];
		Out.Name = Receiver.Name;
		Out.Position = Receiver.Position;
		Out.Radius = Receiver.Radius;
		Out.Histogram.Init(Settings.BinSeconds, Settings.MaxTimeSeconds);
		const double Radius2 = Receiver.Radius * Receiver.Radius;

		for (int32 ImageIndex = 0; ImageIndex < Images.Num(); ++ImageIndex)
		{
			const FImageChain& Image = Images[ImageIndex];

			// Back-trace: receiver -> plane k (inside a triangle, unoccluded) -> towards S'_(k-1) -> ... -> source.
			PathPoints.Reset();
			PathTriangles.Reset();
			PathPoints.Add(Receiver.Position);
			double Factor[Acoustics::NumBands];
			for (double& Value : Factor)
			{
				Value = 1.0;
			}
			bool bValid = true;
			FVector3d Current = Receiver.Position;
			int32 Chain = ImageIndex;
			while (Chain != INDEX_NONE)
			{
				const FImageChain& Link = Images[Chain];
				const FReflectingSurface& Surface = Surfaces[Link.Surface];
				if (Surface.Plane.SignedDistance(Current) <= 0.0)
				{
					bValid = false; // the point we trace from lies behind the wall
					break;
				}
				FVector3d Crossing;
				double T = 0.0;
				if (!SegmentPlaneCrossing(Current, Link.Position, Surface.Plane, Crossing, T))
				{
					bValid = false;
					break;
				}
				const int32 Triangle = FindContainingTriangle(Scene, Surface, Crossing);
				if (Triangle == INDEX_NONE || IsSegmentOccluded(Scene, Current, Crossing, Settings.bUseBVH))
				{
					bValid = false;
					break;
				}
				const FAcousticMaterialProps& Material = Scene.GetMaterial(Scene.Triangles[Triangle].MaterialId);
				for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
				{
					Factor[Band] *= Material.Reflection(Band);
				}
				PathPoints.Add(Crossing);
				PathTriangles.Add(Triangle);
				Current = Crossing;
				Chain = Link.Parent;
			}
			if (bValid && IsSegmentOccluded(Scene, Current, Source, Settings.bUseBVH))
			{
				bValid = false;
			}
			if (!bValid)
			{
				continue;
			}
			PathPoints.Add(Source);

			const double Distance = (Receiver.Position - Image.Position).Length();
			const double Time = Distance / SpeedOfSound;
			const double Spread = FMath::Min(1.0, Radius2 / (4.0 * FMath::Max(Distance * Distance, 1e-12)));
			FEarlyReflection Reflection;
			Reflection.Order = Image.Order;
			Reflection.TimeSeconds = Time;
			Reflection.Distance = Distance;
			Reflection.ImageSource = Image.Position;
			Reflection.ImageSourceIndex = ImageIndex;
			for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
			{
				Reflection.Energy[Band] = Factor[Band] * Spread * (Settings.bAirAbsorption ? FMath::Exp(-Settings.AirAbsorption[Band] * Distance) : 1.0);
			}
			// Path from the source: reverse of the back-trace (receiver, X_k, ..., X_1, source).
			Reflection.Path.Reserve(PathPoints.Num());
			for (int32 Index = PathPoints.Num() - 1; Index >= 0; --Index)
			{
				Reflection.Path.Add(PathPoints[Index]);
			}
			Reflection.Triangles.Reserve(PathTriangles.Num());
			for (int32 Index = PathTriangles.Num() - 1; Index >= 0; --Index)
			{
				Reflection.Triangles.Add(PathTriangles[Index]);
			}
			Out.Histogram.AddEnergy(Time, Reflection.Energy);
			Out.EarlyReflections.Add(MoveTemp(Reflection));
			Result.ImageSources[ImageIndex].bValidForAnyReceiver = true;
			++Result.NumValidImagePaths;
		}
		Out.EarlyReflections.Sort([](const FEarlyReflection& A, const FEarlyReflection& B) { return A.TimeSeconds < B.TimeSeconds; });
		if (Progress)
		{
			Progress->Add(1);
		}
	}
	Result.Timing.TraceSeconds = FPlatformTime::Seconds() - TraceStart;
	Result.bCancelled = Progress && Progress->IsCancelRequested();

	// Direct sound and metrics, as in the ray tracer (SPEC §2.2.7).
	const double MetricsStart = FPlatformTime::Seconds();
	for (FReceiverResult& Out : Result.Receivers)
	{
		Out.DirectDistance = (Out.Position - Source).Length();
		Out.DirectArrivalSeconds = Out.DirectDistance / SpeedOfSound;
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
