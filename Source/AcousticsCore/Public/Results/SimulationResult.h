// What every solver returns (SPEC §3.5): histograms and metrics per receiver, path samples, timing.
#pragma once

#include "CoreMinimal.h"
#include "AcousticsConstants.h"
#include "Results/EnergyHistogram.h"
#include "Results/RoomMetrics.h"

#include <atomic>

/** A receiver handed to a solver: position in meters and sphere radius. */
struct ACOUSTICSCORE_API FSolverReceiver
{
	FString Name;
	FVector3d Position = FVector3d::ZeroVector;
	double Radius = Acoustics::DefaultReceiverRadius;

	FSolverReceiver() = default;
	FSolverReceiver(const FString& InName, const FVector3d& InPosition, double InRadius = Acoustics::DefaultReceiverRadius)
		: Name(InName)
		, Position(InPosition)
		, Radius(InRadius)
	{
	}
};

/** One traced path kept for visualization (M3): the emission point, every reflection point and the end point. */
struct ACOUSTICSCORE_API FPathSample
{
	TArray<FVector3d> Points;

	/** Number of reflections the path went through. */
	int32 Order = 0;

	/** Energy of band 0 at the end of the path, relative to the emitted E0. */
	double FinalEnergy = 0.0;

	bool bReachedReceiver = false;

	/** Reflection order of the segment that first entered a receiver, INDEX_NONE when none did. */
	int32 FirstReceiverHitOrder = INDEX_NONE;
	int32 FirstReceiverIndex = INDEX_NONE;
};

/** Histogram, metrics and direct-sound data of one receiver. */
struct ACOUSTICSCORE_API FReceiverResult
{
	FString Name;
	FVector3d Position = FVector3d::ZeroVector;
	double Radius = Acoustics::DefaultReceiverRadius;

	FEnergyHistogram Histogram;
	FRoomMetrics Metrics;

	/** Source-receiver distance, m, and the direct-sound arrival d / c, s (also the time origin of C50 / C80). */
	double DirectDistance = 0.0;
	double DirectArrivalSeconds = 0.0;
	bool bDirectOccluded = false;

	/** Direct-sound energy deposited (0 when occluded), band 0. */
	double DirectEnergy = 0.0;

	/** Ray segments that entered the sphere and deposited energy. */
	int64 NumRayHits = 0;
};

struct ACOUSTICSCORE_API FSimulationTiming
{
	double SetupSeconds = 0.0;
	double TraceSeconds = 0.0;
	double MetricsSeconds = 0.0;
	double TotalSeconds = 0.0;
	int32 NumWorkers = 0;
};

struct ACOUSTICSCORE_API FSimulationResult
{
	FString SolverName;
	bool bSuccess = false;
	bool bCancelled = false;
	FString Error;

	FVector3d SourcePosition = FVector3d::ZeroVector;
	TArray<FReceiverResult> Receivers;

	/** The first MaxPathsForVisualization rays, in emission order. */
	TArray<FPathSample> Paths;

	FSimulationTiming Timing;

	int32 NumRays = 0;
	int32 NumTriangles = 0;

	/** Ray segments traced (one per reflection plus the final segment of every ray). */
	int64 NumSegments = 0;

	/** Rays that left the scene without hitting anything. */
	int32 NumRaysEscaped = 0;

	/** Mean number of reflections per ray. */
	double MeanOrder = 0.0;

	const FReceiverResult* FindReceiver(const FString& Name) const;
	FString Describe() const;
};

/** Shared between the solver thread and the caller: completed work units and the cancel flag. */
struct ACOUSTICSCORE_API FSolverProgress
{
	std::atomic<int64> Completed{ 0 };
	std::atomic<int64> Total{ 0 };
	std::atomic<bool> bCancelRequested{ false };

	void Reset(int64 InTotal);
	void Add(int64 Count) { Completed.fetch_add(Count, std::memory_order_relaxed); }
	float Fraction() const;
	void RequestCancel() { bCancelRequested.store(true, std::memory_order_relaxed); }
	bool IsCancelRequested() const { return bCancelRequested.load(std::memory_order_relaxed); }
};
