// Rays, hits and the Moller-Trumbore ray-triangle test (SPEC §3.3).
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Geometry/AcousticTriangle.h"

/** A ray in meters with a unit direction. */
struct ACOUSTICSCORE_API FAcousticRay
{
	FVector3d Origin = FVector3d::ZeroVector;
	FVector3d Direction = FVector3d(1.0, 0.0, 0.0);

	FAcousticRay() = default;
	FAcousticRay(const FVector3d& InOrigin, const FVector3d& InDirection)
		: Origin(InOrigin)
		, Direction(InDirection)
	{
	}

	FVector3d PointAt(double T) const { return Origin + Direction * T; }
};

/** Closest-hit result. T is the distance along the ray (meters for unit directions). */
struct ACOUSTICSCORE_API FRayHit
{
	double T = TNumericLimits<double>::Max();
	int32 TriangleIndex = INDEX_NONE;

	/** Barycentric weights of V1 and V2 (V0 has 1 - U - V). */
	double U = 0.0;
	double V = 0.0;

	/** The ray came from behind the triangle (Direction . N > 0). */
	bool bBackFace = false;

	bool IsHit() const { return TriangleIndex != INDEX_NONE; }
	void Reset() { *this = FRayHit(); }
};

/** Options shared by the BVH and brute-force ray queries. */
struct ACOUSTICSCORE_API FRayQueryOptions
{
	static constexpr double DefaultTMin = 1e-9;

	/** Hits closer than TMin are ignored (self-intersection after a reflection). */
	double TMin = DefaultTMin;

	/** Hits farther than TMax are ignored (segment tests). */
	double TMax = TNumericLimits<double>::Max();

	/** Ignore triangles hit from behind. */
	bool bCullBackFaces = false;

	/** A triangle index to skip, typically the one the ray was just reflected from. */
	int32 IgnoreTriangle = INDEX_NONE;
};

namespace AcousticGeometry
{
	/** |det| below this is treated as a ray parallel to the triangle plane (meter-scale geometry). */
	constexpr double ParallelEpsilon = 1e-12;

	/**
	 * Moller-Trumbore. True when the ray hits the triangle at OutT >= 0; OutU/OutV are the barycentric weights of V1/V2.
	 * bOutBackFace is set when the ray hits the back face. Edges and vertices count as hits; parallel rays never hit.
	 */
	ACOUSTICSCORE_API bool IntersectRayTriangle(const FAcousticRay& Ray, const FAcousticTriangle& Triangle, double& OutT, double& OutU, double& OutV, bool& bOutBackFace);

	/**
	 * Slab test of the parameter interval [TMin, TMax] against an axis-aligned box.
	 * InvDirection holds 1 / Direction per component; components of Direction with |d| < 1e-200 are treated as zero.
	 * OutEntry receives the first T inside the box (>= TMin).
	 */
	ACOUSTICSCORE_API bool IntersectRayBox(const FVector3d& Origin, const FVector3d& Direction, const FVector3d& InvDirection, const FBox3d& Box, double TMin, double TMax, double& OutEntry);

	/** Reciprocal per component; (near-)zero components give +/- DBL_MAX, which IntersectRayBox never multiplies. */
	ACOUSTICSCORE_API FVector3d SafeInverse(const FVector3d& Direction);

	/** Closest hit by testing every triangle. The reference the BVH is validated against. */
	ACOUSTICSCORE_API bool IntersectRayBruteForce(TConstArrayView<FAcousticTriangle> Triangles, const FAcousticRay& Ray, FRayHit& OutHit, const FRayQueryOptions& Options = FRayQueryOptions());
}
