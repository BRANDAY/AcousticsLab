// Triangular beam tracing with beam division (SPEC §2.5, Sikora et al. 2011 "Beam Division in Acoustic Simulation
// of Non-Homogenous Environments"): a beam tree built once per source and queried per receiver.
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

	FBeamTracerSettings();

	bool Validate(FString& OutError) const;

	/** Root beams for a subdivision level: 20 * 4^Level. */
	static int32 NumRootBeams(int32 Level) { return 20 * (1 << (2 * FMath::Max(0, Level))); }
};

/**
 * One node of the beam tree (SPEC §2.5.5): a beam whose whole cross-section hits one triangle (HitTriangle) or nothing.
 * Apex = the real source (order 0) or the mirrored apex of the parent; the three guide directions span the cone; the
 * cone is the intersection of the three side half-spaces (positive side = inside). A reflected beam is valid only beyond
 * the plane it was reflected from (ReflectionPlane, positive side) and before the triangle it hits next (HitPlane,
 * positive side = apex side). Order-0 nodes are the pieces of the emission beams.
 */
struct ACOUSTICSCORE_API FBeamNode
{
	FVector3d Apex = FVector3d::ZeroVector;
	FVector3d Directions[3];
	FAcousticPlane SidePlanes[3];

	/** Plane of the triangle the parent beam was reflected from; unused for order 0. */
	FAcousticPlane ReflectionPlane;
	bool bHasReflectionPlane = false;
	int32 ReflectionTriangle = INDEX_NONE;

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

	/** Hash of the reflection sequence (coplanar surfaces from the root), for removing duplicate contributions. */
	uint64 SequenceHash = 0;

	FBeamNode();

	/** Inside the three side half-spaces, beyond the reflection plane and before the hit plane (tolerances in meters). */
	bool ContainsPoint(const FVector3d& Point, double Tolerance = 1e-9) const;

	/** The cone test alone (three half-spaces through the apex). */
	bool ConeContainsPoint(const FVector3d& Point, double Tolerance = 1e-9) const;

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
	int32 SubdivisionLevel = 0;
	int32 NumRootBeams = 0;
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

	/** The node budget stopped the build. */
	bool bTruncated = false;

	/** Guide rays of escaping beams are drawn this long, m. */
	double EscapeLength = 10.0;

	/**
	 * Every node containing Point (SPEC §2.5.6), orders 0..MaxOrder. With bRemoveDuplicateSequences a point lying exactly
	 * on a shared beam boundary is reported once per reflection sequence.
	 */
	void Query(const FVector3d& Point, TArray<FBeamContribution>& OutContributions, bool bRemoveDuplicateSequences = true) const;

	/** Path from the source to Point through the reflection points of the node chain (Order + 2 points) and the triangles hit. */
	void TracePath(int32 NodeIndex, const FVector3d& Point, TArray<FVector3d>& OutPath, TArray<int32>& OutTriangles) const;

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
 * Scattering and diffraction are not modelled (TODO: Sikora, Mateljan, Bogunovic 2012, "Beam Tracing with Refraction",
 * sections on diffuse reflection and edge diffraction). Deterministic; no threads in M5.
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
