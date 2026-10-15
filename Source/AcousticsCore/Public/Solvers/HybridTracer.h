// Hybrid beam/ray tracing (SPEC §2.8, M8; Sikora, Russo, Mateljan, "Hybrid beam-ray tracing method", Euronoise 2018):
// beams for the early specular reflections, stochastic rays for everything else.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Results/SimulationResult.h"
#include "Solvers/BeamTracer.h"
#include "Solvers/RayTracer.h"

struct FAcousticScene;

/** Settings of both parts; FSolverSettings (game module) converts to it. */
struct ACOUSTICSCORE_API FHybridTracerSettings
{
	/** Late part: every ray-tracer setting (rays, seed, order, threshold, threads, ...); the split fields are set by the hybrid. */
	FRayTracerSettings Ray;

	/** Early part: emission subdivision, node budget, threads, ...; MaxBeamOrder = K, refraction and directivity are switched off. */
	FBeamTracerSettings Beam;

	/** Highest reflection order the beams contribute (SPEC: 2). 0 = no beams: the hybrid is the ray tracer. */
	int32 K = 2;

	/** Beams contribute paths arriving no later than this after the emission, s (SPEC: 80 ms). */
	double MixingTimeSeconds = 0.080;

	/** Beams carry (1 - alpha)(1 - s) per reflection, the specular share the rays leave to them (see M8.md). */
	bool bScatteringLoss = true;

	/** Group the specular ray arrivals per reflection sequence (found / missed early reflections, SPEC §5 exp. 3). */
	bool bCountMissedReflections = true;

	bool Validate(FString& OutError) const;

	/** The beam-tracer settings of the early part (MaxBeamOrder = K, omnidirectional, no refraction, bScatteringLoss). */
	FBeamTracerSettings MakeBeamSettings() const;

	/** The ray-tracer settings of the late part (the early-specular split for K and MixingTimeSeconds). */
	FRayTracerSettings MakeRaySettings() const;
};

/** What the split did at one receiver. */
struct ACOUSTICSCORE_API FHybridReceiverStats
{
	/** Beam reflections of order 1..K: kept (t <= t_mix, the early part) and dropped by t_mix (left to the rays). */
	int32 NumBeamEarly = 0;
	int32 NumBeamDropped = 0;

	/** Kept beam reflections whose image source some ray also found (SPEC §5 exp. 3), and the ones no ray found. */
	int32 NumFoundByRays = 0;
	int32 NumMissedByRays = 0;

	/** Specular ray groups of order 1..K that left the rays (excluded) and those that stayed because they arrive after t_mix. */
	int32 NumRayGroupsExcluded = 0;
	int32 NumRayGroupsLate = 0;

	/** Excluded ray groups whose image source is not an early beam reflection (receiver-sphere edge effects; 0 in a shoebox). */
	int32 NumRayGroupsWithoutBeam = 0;

	/** Band-0 energies: early beams, the rays' estimate of the same block (excluded), everything else from the rays (direct included). */
	double BeamEarlyEnergy = 0.0;
	double RayExcludedEnergy = 0.0;
	double RayLateEnergy = 0.0;

	/** Arrival of the last kept beam reflection, s (0 without one). */
	double LastEarlyArrivalSeconds = 0.0;
};

/**
 * Hybrid = direct + beams(early-specular) + rays(everything else); see FHybridTracer. PureRay is the same ray run with
 * nothing excluded (Histogram = late + excluded) and PureBeam the beam run of the early part without the t_mix cut, so
 * the three IR curves of the comparison panel come out of one run.
 */
struct ACOUSTICSCORE_API FHybridResult
{
	TSharedPtr<const FSimulationResult> Hybrid;
	TSharedPtr<const FSimulationResult> PureRay;
	TSharedPtr<const FSimulationResult> PureBeam;

	/** Per receiver: the ray part as it was deposited (direct sound + every not early-specular deposit). */
	TArray<FEnergyHistogram> LateHistograms;

	TArray<FHybridReceiverStats> Stats;

	int32 K = 0;
	double MixingTimeSeconds = 0.0;
	bool bScatteringLoss = true;

	double BeamSeconds = 0.0;
	double RaySeconds = 0.0;
	double ComposeSeconds = 0.0;

	bool IsValid() const { return Hybrid.IsValid() && PureRay.IsValid() && PureBeam.IsValid(); }
	FString Describe() const;
};

/**
 * SPEC §2.8 with the split rule of M8.md. A path is early-specular when it has no diffuse reflection, order 1..K and an
 * image-source delay d' / c <= t_mix. The beams (FBeamTracer through the M7 pool, MaxBeamOrder = K) give exactly the
 * early-specular paths: their reflections with t <= t_mix enter the hybrid, later ones are dropped. The rays (FRayTracer)
 * give everything else: a ray deposit is moved out of the histogram only when the ray is still purely specular, its order
 * is 1..K and the image-source delay of its path to the receiver centre (the beam's delay, computed in the unfolded space)
 * is <= t_mix - so a specular path of order <= K that arrives after t_mix stays with the rays, and a path is never counted
 * by both parts nor by neither. Direct sound is analytic (ray part). Pure function of (scene, source, receivers, settings);
 * meters, seconds, double; deterministic and independent of the thread count (both parts are, the composition runs in
 * receiver order on the calling thread).
 */
class ACOUSTICSCORE_API FHybridTracer
{
public:
	static const TCHAR* Name() { return TEXT("HybridTracer"); }

	static FHybridResult Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> Receivers, const FHybridTracerSettings& Settings, FSolverProgress* Progress = nullptr);

	/** Rays and beams name the same specular path: same order, image sources within ImageMatchTolerance. */
	static bool IsSameImage(const FSpecularRayArrival& Arrival, const FEarlyReflection& Reflection)
	{
		return Arrival.Order == Reflection.Order && FVector3d::DistSquared(Arrival.ImageSource, Reflection.ImageSource) <= ImageMatchTolerance * ImageMatchTolerance;
	}

	/** Image sources of rays carry the reflection offset (1e-7 m per reflection) and rounding; distinct images are centimetres apart, m. */
	static constexpr double ImageMatchTolerance = 1e-4;

	/** Beam reflection is part of the early block: order 1..K and t <= t_mix. */
	static bool IsEarlyBeamReflection(const FEarlyReflection& Reflection, int32 K, double MixingTimeSeconds)
	{
		return Reflection.Order >= 1 && Reflection.Order <= K && Reflection.TimeSeconds <= MixingTimeSeconds;
	}
};
