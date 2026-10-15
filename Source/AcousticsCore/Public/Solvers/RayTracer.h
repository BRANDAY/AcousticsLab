// Stochastic ray tracer (SPEC §2.2): energy rays from a point source to spherical receivers.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "AcousticsConstants.h"
#include "Results/SimulationResult.h"

struct FAcousticScene;

/** Core-side settings of the ray tracer; the game module's FSolverSettings (USTRUCT) converts to it. */
struct ACOUSTICSCORE_API FRayTracerSettings
{
	int32 NumRays = Acoustics::DefaultNumRays;

	/** A ray stops once its reflection order exceeds this. */
	int32 MaxOrder = Acoustics::DefaultMaxOrder;

	/** Used for receivers whose own radius is not positive. */
	double ReceiverRadius = Acoustics::DefaultReceiverRadius;

	double BinSeconds = Acoustics::DefaultBinSeconds;
	double MaxTimeSeconds = Acoustics::DefaultMaxTimeSeconds;

	uint32 Seed = 1;

	bool bAirAbsorption = false;
	double AirAbsorption[Acoustics::NumBands];

	/** A ray stops once every band is below E0 * EnergyThreshold. */
	double EnergyThreshold = Acoustics::DefaultEnergyThreshold;

	/** Worker count: 0 = one per worker thread of the task graph, 1 = the calling thread only. */
	int32 NumThreads = 0;

	int32 MaxPathsForVisualization = Acoustics::DefaultMaxPathsForVisualization;

	/** Brute-force triangle search when false (M7 benchmark). */
	bool bUseBVH = true;

	/** Direct sound r^2 / (4 d^2) added analytically; order-0 segments then skip the receiver test. */
	bool bAnalyticDirectSound = true;

	/** Rays per work unit; histograms are reduced in chunk order, so the result is independent of NumThreads. */
	int32 RaysPerChunk = 1024;

	FRayTracerSettings();

	bool Validate(FString& OutError) const;
};

/**
 * Pure function of (scene, source, receivers, settings) -> FSimulationResult. Meters, seconds, doubles.
 * Emission on a Fibonacci sphere with E0 = 1 / N per ray; at every hit E *= (1 - alpha), the new direction is
 * Lambert-diffuse with probability s (band 0 decides) and specular otherwise; optional air absorption exp(-m d);
 * receivers are transparent spheres that record the energy at the entry time; termination at E < E0 * EnergyThreshold,
 * t > MaxTime or order > MaxOrder. Deterministic for a fixed seed and bit-identical for any worker count.
 */
class ACOUSTICSCORE_API FRayTracer
{
public:
	static const TCHAR* Name() { return TEXT("RayTracer"); }

	static FSimulationResult Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> Receivers, const FRayTracerSettings& Settings, FSolverProgress* Progress = nullptr);

	/** Direction Index of Count on the Fibonacci sphere (unit vector). */
	static FVector3d FibonacciDirection(int32 Index, int32 Count);

	static FVector3d Reflect(const FVector3d& Direction, const FVector3d& Normal);

	/** Cosine-weighted direction around Normal from two uniform numbers in [0, 1). */
	static FVector3d LambertDirection(const FVector3d& Normal, double U1, double U2);

	/**
	 * Entry point of the segment Origin + Direction * [0, Length] into the sphere: OutEntryDistance along the ray.
	 * False when the segment starts inside the sphere, the sphere lies behind the origin or beyond Length.
	 */
	static bool IntersectSegmentSphere(const FVector3d& Origin, const FVector3d& Direction, double Length, const FVector3d& Center, double Radius, double& OutEntryDistance);

	/** Workers actually used for a requested NumThreads. */
	static int32 ResolveNumWorkers(int32 Requested);

	/**
	 * Path samples are every Stride-th ray, Stride = max(1, NumRays / MaxPaths), so that min(MaxPaths, NumRays) samples
	 * cover the whole Fibonacci sphere. The choice depends on the ray index only (deterministic, thread-count invariant).
	 */
	static int32 PathSampleStride(int32 NumRays, int32 MaxPaths);
	static bool IsPathSampled(int32 RayIndex, int32 NumRays, int32 MaxPaths);
	static int32 NumPathSamples(int32 NumRays, int32 MaxPaths);

	/** Reflected rays start this far from the surface along the facing normal, m. */
	static constexpr double SurfaceOffset = 1e-7;
};
