// Triangular beam tracing with beam division (SPEC §2.5, Sikora et al. 2011 "Beam Division in Acoustic Simulation
// of Non-Homogenous Environments") and refraction at interfaces between media (SPEC §2.6, Sikora, Mateljan, Bogunovic
// 2012 "Beam Tracing with Refraction"): a beam tree built once per source and queried per receiver.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "AcousticsConstants.h"
#include "Geometry/Frustum.h"
#include "Geometry/PolygonClipping.h"
#include "Results/SimulationResult.h"

struct FAcousticScene;

/** Core-side settings of the beam tracer; FSolverSettings (game module) converts to it. */
struct ACOUSTICSCORE_API FBeamTracerSettings
{
	/** Icosahedron subdivisions of the emission sphere: 0 = 20 beams, 1 = 80, 2 = 320, 3 = 1280 (SPEC §2.5.1). */
	int32 SubdivisionLevel = 2;

	/** Depth of the beam tree: beams are reflected up to this order (SPEC §2.5.5: 3-6). */
	int32 MaxBeamOrder = 3;

	/** Used for receivers whose own radius is not positive. */
	double ReceiverRadius = Acoustics::DefaultReceiverRadius;

	double BinSeconds = Acoustics::DefaultBinSeconds;
	double MaxTimeSeconds = Acoustics::DefaultMaxTimeSeconds;

	bool bAirAbsorption = false;
	double AirAbsorption[Acoustics::NumBands];

	/** A beam is not reflected further once its accumulated factor is below this in every band. */
	double EnergyThreshold = Acoustics::DefaultEnergyThreshold;

	/** Node budget of the tree; the build stops (bTruncated) when it is reached. */
	int32 MaxBeamNodes = 500000;

	/** Brute-force triangle search when false (M7 benchmark). */
	bool bUseBVH = true;

	/** Direct sound r^2 / (4 d^2) added analytically with an occlusion test, like the ray tracer; order-0 beams then only feed the visualization. */
	bool bAnalyticDirectSound = true;

	/** Reserved for M7 (parallel subtrees); the M5 build is sequential and the result never depends on it. */
	int32 NumThreads = 0;

	/**
	 * Refraction (SPEC §2.6, M6): a beam hitting an interface triangle (FAcousticTriangle::bInterface with different media
	 * on its sides) splits into a reflected beam (factor R by impedances) and a refracted one (factor T = 1 - R, guide
	 * rays by Snell, apex = least-squares intersection of the refracted guide rays); beyond the critical angle only
	 * the reflected beam exists (factor 1). Off: interface triangles reflect like ordinary walls with their material.
	 */
	bool bRefraction = false;

	/** Medium (index into FAcousticScene::Media) the source sits in; delays use its speed of sound until the first refraction. */
	int32 SourceMedium = 0;

	/**
	 * Unidirectional source (the lens test of Sikora 2012): only root beams whose centre direction lies within
	 * SourceConeHalfAngleDeg of SourceDirection are emitted, and the direct sound reaches only receivers inside the cone.
	 * The energy outside the cone is discarded (a mask, not a redistribution).
	 */
	bool bDirectionalSource = false;
	FVector3d SourceDirection = FVector3d(1.0, 0.0, 0.0);
	double SourceConeHalfAngleDeg = 180.0;

	FBeamTracerSettings();

	bool Validate(FString& OutError) const;

	/** Root beams for a subdivision level: 20 * 4^Level. */
	static int32 NumRootBeams(int32 Level) { return 20 * (1 << (2 * FMath::Max(0, Level))); }
};

/**
 * One node of the beam tree (SPEC §2.5.5): a beam whose whole cross-section hits one triangle (HitTriangle) or nothing.
 * Apex = the real source (order 0), the mirrored apex of the parent (reflection) or the least-squares focus of the
 * refracted guide rays (refraction, SPEC §2.6); Directions are the propagation directions of the three guide rays; the
 * cone is the intersection of the three side half-spaces (positive side = inside). A reflected or refracted beam is
 * valid only beyond the plane it entered through (ReflectionPlane, positive side) and before the triangle it hits next
 * (HitPlane, positive side = the side the beam comes from). A refracted beam may converge (bFocusAhead: the apex lies
 * beyond the entry plane); the node is then the nappe between the start triangle and the focus, tested on the point
 * mirrored through the apex, and the part beyond the focus is a separate diverging node (bContinuation) when the beam
 * reaches its focus without hitting anything. Order-0 nodes are the pieces of the emission beams.
 */
struct ACOUSTICSCORE_API FBeamNode
{
	FVector3d Apex = FVector3d::ZeroVector;
	FVector3d Directions[3];
	FAcousticPlane SidePlanes[3];

	/** Plane the beam entered through (the triangle the parent was reflected from or refracted at); unused for order 0. */
	FAcousticPlane ReflectionPlane;
	bool bHasReflectionPlane = false;
	int32 ReflectionTriangle = INDEX_NONE;

	/** Medium (FAcousticScene::Media index) the beam propagates in: delays use its speed of sound. */
	int32 Medium = 0;

	/** Refractions along the chain from the root; bRefracted marks a node produced by a refraction at ReflectionTriangle. */
	int32 NumRefractions = 0;
	bool bRefracted = false;

	/** The apex lies on the propagation side of the entry plane: the beam converges towards it (refracted beams only). */
	bool bFocusAhead = false;

	/** The diverging continuation of a converging beam beyond its focus (same order, same factor, entry plane through the focus). */
	bool bContinuation = false;

	/** The beam hits an interface beyond the critical angle: only the reflected child exists and it keeps the whole energy. */
	bool bTotalInternalReflection = false;

	/** Triangle the beam hits (INDEX_NONE: the beam leaves the scene) and its plane oriented towards the apex. */
	int32 HitTriangle = INDEX_NONE;
	FAcousticPlane HitPlane;

	int32 Parent = INDEX_NONE;
	int32 RootBeam = INDEX_NONE;
	int32 Order = 0;

	/** Accumulated reflection factor Prod(1 - alpha_i) per band. */
	double Factor[Acoustics::NumBands];

	/** Cross-section where the beam starts (the apex itself for order 0, the fan triangle on the reflecting wall otherwise). */
	FVector3d StartPoints[3];

	/** Cross-section on the hit triangle (or at EscapeLength along the guide rays for beams that leave the scene). */
	FVector3d EndPoints[3];

	/** Hash of the reflection / refraction sequence (coplanar surfaces from the root), for removing duplicate contributions. */
	uint64 SequenceHash = 0;

	FBeamNode();

	/** Inside the three side half-spaces (the antipodal nappe for bFocusAhead), beyond the entry plane and before the hit plane (tolerances in meters). */
	bool ContainsPoint(const FVector3d& Point, double Tolerance = 1e-9) const;

	/** The cone test alone (three half-spaces through the apex, along Directions). */
	bool ConeContainsPoint(const FVector3d& Point, double Tolerance = 1e-9) const;

	/** Mean propagation direction of the three guide rays. */
	FVector3d Axis() const { return (Directions[0] + Directions[1] + Directions[2]).GetSafeNormal(); }

	/** Builds the side planes from Apex and Directions (oriented inwards). */
	void ComputeSidePlanes();
};

/** A contribution of the tree to a point: the receiver lies inside node Node. */
struct ACOUSTICSCORE_API FBeamContribution
{
	int32 Node = INDEX_NONE;
	int32 Order = 0;

	/** Distance from the point to the apex, m: the length of the specular path from the source. */
	double Distance = 0.0;
};

/** The tree: nodes in breadth-first order (all order-0 nodes first, then order 1, ...). */
struct ACOUSTICSCORE_API FBeamTree
{
	FVector3d Source = FVector3d::ZeroVector;
	int32 SourceMedium = 0;
	int32 SubdivisionLevel = 0;
	int32 NumRootBeams = 0;

	/** Root beams actually traced (all of them for an omnidirectional source, those inside the cone otherwise). */
	int32 NumRootBeamsEmitted = 0;
	int32 MaxOrder = 0;
	TArray<FBeamNode> Nodes;

	/** Nodes per order (NumNodesPerOrder[k] = nodes of order k). */
	TArray<int32> NumNodesPerOrder;

	/** Beam pieces whose cross-section was not covered by any triangle (open scenes). */
	int32 NumEscaped = 0;

	/** Beam pieces dropped after the centre-ray check could not settle the nearest triangle within the pass limit. */
	int32 NumUnresolved = 0;

	/** Pieces and child triangles dropped as slivers of the clipping arithmetic (area or altitude below the thresholds). */
	int32 NumSlivers = 0;

	/** Refraction counters (M6): refracted beams created, beams beyond the critical angle (reflected only), converging beams
	 *  continued beyond their focus, refracted beams whose guide rays were parallel (apex placed far behind the start triangle). */
	int32 NumRefracted = 0;
	int32 NumTotalInternalReflections = 0;
	int32 NumContinuations = 0;
	int32 NumParallelApexes = 0;

	/** The node budget stopped the build. */
	bool bTruncated = false;

	/** Guide rays of escaping beams are drawn this long, m. */
	double EscapeLength = 10.0;

	/**
	 * Every node containing Point (SPEC §2.5.6), orders 0..MaxOrder. With bRemoveDuplicateSequences a point lying exactly
	 * on a shared beam boundary is reported once per reflection sequence.
	 */
	void Query(const FVector3d& Point, TArray<FBeamContribution>& OutContributions, bool bRemoveDuplicateSequences = true) const;

	/**
	 * Path from the source to Point through the reflection / refraction points of the node chain (Order + 2 points) and
	 * the triangles passed. OutSegmentMedia (optional) receives the medium of every segment (OutPath.Num() - 1 entries),
	 * so the delay is the sum of |segment| / c(medium) (SPEC §2.6: delays inside a medium use its speed of sound).
	 */
	void TracePath(int32 NodeIndex, const FVector3d& Point, TArray<FVector3d>& OutPath, TArray<int32>& OutTriangles, TArray<int32>* OutSegmentMedia = nullptr) const;

	/** Sum of |segment| / c over a path with per-segment media. */
	static double PathDelaySeconds(const FAcousticScene& Scene, TConstArrayView<FVector3d> Path, TConstArrayView<int32> SegmentMedia);

	/** Same nodes with the same apexes, directions, triangles and factors (determinism test). */
	bool IsIdentical(const FBeamTree& Other, double Tolerance = 0.0) const;

	FString DescribeStats() const;
};

/**
 * Pure function of (scene, source, receivers, settings) -> FSimulationResult; meters, seconds, doubles.
 * Emission: icosahedron faces subdivided SubdivisionLevel times (rotated by a fixed angle so that no beam boundary
 * coincides with a coordinate plane). A beam is divided (SPEC §2.5.3) by clipping its cross-section against every
 * triangle it can see: the cross-section is kept as convex polygons in the beam's reference plane (gnomonic projection
 * from the apex), each candidate triangle (from FBVH::QueryFrustum, nearest first) is cut to the beam and the apex
 * half-space, projected and clipped with Sutherland-Hodgman; the covered part becomes child beams (fan triangulation,
 * apex mirrored in the triangle plane, guide rays reflected, factor *= 1 - alpha), the uncovered parts continue to the
 * next triangle; a ray through the centre of every covered piece confirms that the triangle is the nearest one, which
 * keeps the division correct in non-convex scenes. The tree is built once; every receiver is a query (SPEC §2.5.7).
 * Refraction (SPEC §2.6, bRefraction): at an interface triangle the beam splits into the reflected beam (factor R by
 * the impedances of the two media, normal incidence) and the refracted beam (factor T = 1 - R, guide rays by Snell,
 * apex = least-squares intersection of the refracted guide rays - an approximation, the refracted beam has no exact
 * apex; Sikora 2012 §3.1.2, Sikora 2010). The node factor also carries (r1 / r_in)^2 (apex distances to the start
 * triangle, Sikora 2012 Eq. 6) so the energy density is continuous across the interface. Beyond the critical angle the
 * beam is only reflected (factor 1). Scattering and diffraction are not modelled (TODO: Sikora 2012, sections on
 * diffuse reflection and edge diffraction). Deterministic; no threads in M5 / M6.
 */
class ACOUSTICSCORE_API FBeamTracer
{
public:
	static const TCHAR* Name() { return TEXT("BeamTracer"); }

	static FSimulationResult Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> Receivers, const FBeamTracerSettings& Settings, FSolverProgress* Progress = nullptr);

	/** Builds the beam tree of a source (no receivers involved). */
	static TSharedPtr<FBeamTree> BuildTree(const FAcousticScene& Scene, const FVector3d& Source, const FBeamTracerSettings& Settings, FSolverProgress* Progress = nullptr);

	/** Fills the receivers of Result (histograms, early reflections, containing nodes, direct sound, metrics) from the tree. */
	static void QueryReceivers(const FAcousticScene& Scene, const FBeamTree& Tree, TConstArrayView<FSolverReceiver> Receivers, const FBeamTracerSettings& Settings, FSimulationResult& Result);

	/** Unit directions of the subdivided icosahedron: 3 per beam, OutDirections.Num() = 3 * NumRootBeams(Level). */
	static void EmissionDirections(int32 Level, TArray<FVector3d>& OutDirections);

	/** Fixed rotation applied to the icosahedron (keeps beam boundaries off the coordinate planes). */
	static FQuat4d EmissionRotation();

	static FVector3d MirrorPoint(const FVector3d& Point, const FAcousticPlane& Plane);
	static FVector3d ReflectDirection(const FVector3d& Direction, const FVector3d& Normal);

	/**
	 * Snell: sin(theta2) / sin(theta1) = C2 / C1 for a unit direction arriving at a surface whose unit Normal points
	 * against the arrival (towards the incident medium; the sign is corrected when it does not). Returns false for total
	 * internal reflection (sin(theta2) > 1); OutDirection is then untouched. The refracted direction lies in the plane of
	 * incidence and points away from the surface on the transmitted side. With MaxSinRefracted < 1 the refraction angle is
	 * clamped to asin(MaxSinRefracted) instead (always true): the guide rays of a beam whose axis refracts (the per-beam
	 * decision of Sikora 2012, sec. 3.3.4) stay usable when they straddle the critical angle.
	 */
	static bool RefractDirection(const FVector3d& Direction, const FVector3d& Normal, double C1, double C2, FVector3d& OutDirection, double MaxSinRefracted = 1.0);

	/** sin of the largest refraction angle given to a guide ray beyond the critical angle (about 87.4 deg). */
	static constexpr double MaxGuideRaySinRefracted = 0.999;

	/**
	 * Point minimizing the sum of squared distances to the lines (Origins[i] + s Directions[i]), the apex of a refracted
	 * beam (SPEC §2.6). Exact for concurrent lines. Returns false when the lines are (nearly) parallel; OutPoint is then
	 * the mean origin moved FallbackDistance back along the mean direction (a beam from far away).
	 */
	static bool LeastSquaresApex(TConstArrayView<FVector3d> Origins, TConstArrayView<FVector3d> Directions, FVector3d& OutPoint, double FallbackDistance = ParallelApexDistance);

	/** Apex distance used for refracted beams whose guide rays do not converge or diverge measurably, m. */
	static constexpr double ParallelApexDistance = 1.0e4;

	/** A refracted apex closer than this to the entry plane is treated as parallel (the projection would be ill-conditioned), m. */
	static constexpr double MinApexClearance = 1e-6;

	/** Point where the ray from Apex along Direction meets the plane; false when parallel or behind the apex. */
	static bool RayPlaneHit(const FVector3d& Apex, const FVector3d& Direction, const FAcousticPlane& Plane, FVector3d& OutPoint, double& OutDistance);

	/** 3D Sutherland-Hodgman: the part of a convex polygon on the positive side of the plane (shifted by Offset). */
	static void ClipPolygon3DByPlane(TArray<FVector3d>& Polygon, const FAcousticPlane& Plane, double Offset = 0.0);

	/** Triangles nearer than this to the apex plane are cut off before projection, m. */
	static constexpr double ApexClearance = 1e-6;

	/** Candidate triangles are cut to the beam's side planes widened by this (meters) before the division. */
	static constexpr double SideClearance = 1e-9;

	/** Passes of the nearest-first division before the leftover pieces are dropped. */
	static constexpr int32 MaxDivisionPasses = 6;
};
