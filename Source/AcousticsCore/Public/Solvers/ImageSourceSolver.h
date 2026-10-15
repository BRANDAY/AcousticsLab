// Image-source method (SPEC §2.4): deterministic specular early reflections up to a small order.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "AcousticsConstants.h"
#include "Geometry/Frustum.h"
#include "Results/SimulationResult.h"

struct FAcousticScene;

/** Core-side settings of the image-source solver; FSolverSettings (game module) converts to it. */
struct ACOUSTICSCORE_API FImageSourceSettings
{
	/** Highest reflection order enumerated (SPEC: <= 3, exponential growth). */
	int32 MaxOrder = 3;

	/** Used for receivers whose own radius is not positive. */
	double ReceiverRadius = Acoustics::DefaultReceiverRadius;

	double BinSeconds = Acoustics::DefaultBinSeconds;
	double MaxTimeSeconds = Acoustics::DefaultMaxTimeSeconds;

	bool bAirAbsorption = false;
	double AirAbsorption[Acoustics::NumBands];

	/** The enumeration stops (with a warning) once this many images exist. */
	int32 MaxImageSources = 200000;

	/** Brute-force ray queries when false. */
	bool bUseBVH = true;

	/** Direct sound r^2 / (4 d^2) added analytically with an occlusion test, like the ray tracer. */
	bool bAnalyticDirectSound = true;

	FImageSourceSettings();

	bool Validate(FString& OutError) const;
};

/** Coplanar triangles of the scene grouped into one reflecting surface: images are mirrored across planes, not triangles. */
struct ACOUSTICSCORE_API FReflectingSurface
{
	FAcousticPlane Plane;
	TArray<int32> Triangles;
};

/**
 * Pure function of (scene, source, receivers, settings) -> FSimulationResult; meters, seconds, doubles, no threads.
 * Images: for every surface S' = mirror(S, plane); chains up to MaxOrder without immediate repeats, pruned when the
 * previous image lies behind the next surface (a wall reflects only what is in front of it). Validity per receiver by
 * back-tracing (SPEC §2.4): the segment receiver -> S'_k cuts plane k inside one of its triangles and is unoccluded,
 * then from that point towards S'_(k-1), ..., finally to the real source. Energy Prod(1 - alpha_i) r^2 / (4 d^2) at
 * t = d / c with d = |receiver - S'_k|, optional air absorption exp(-m d). Every valid path is added to the receiver's
 * histogram and listed in FReceiverResult::EarlyReflections; all images are listed in FSimulationResult::ImageSources.
 */
class ACOUSTICSCORE_API FImageSourceSolver
{
public:
	static const TCHAR* Name() { return TEXT("ImageSource"); }

	static FSimulationResult Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> Receivers, const FImageSourceSettings& Settings, FSolverProgress* Progress = nullptr);

	/** Groups the scene triangles into coplanar surfaces (normals within NormalTolerance, offsets within OffsetTolerance). */
	static void BuildSurfaces(const FAcousticScene& Scene, TArray<FReflectingSurface>& OutSurfaces, double NormalTolerance = 1e-6, double OffsetTolerance = 1e-6);

	static FVector3d MirrorPoint(const FVector3d& Point, const FAcousticPlane& Plane);

	/**
	 * Point where the segment From -> To crosses the plane; false when the segment does not cross it (or runs in it).
	 * OutT is the crossing parameter in (0, 1).
	 */
	static bool SegmentPlaneCrossing(const FVector3d& From, const FVector3d& To, const FAcousticPlane& Plane, FVector3d& OutPoint, double& OutT);

	/** Index of the triangle of the surface that contains the (coplanar) point within Tolerance, INDEX_NONE when none does. */
	static int32 FindContainingTriangle(const FAcousticScene& Scene, const FReflectingSurface& Surface, const FVector3d& Point, double Tolerance = 1e-9);

	/** True when the open segment From -> To (shortened by Margin at both ends) hits any triangle. */
	static bool IsSegmentOccluded(const FAcousticScene& Scene, const FVector3d& From, const FVector3d& To, bool bUseBVH, double Margin = SegmentMargin);

	/** Ends of a back-traced segment are shortened by this much, m, so the triangles they lie on do not count as occluders. */
	static constexpr double SegmentMargin = 1e-6;
};
