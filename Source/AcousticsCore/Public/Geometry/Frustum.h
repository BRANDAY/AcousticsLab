// Planes and the frustum of a triangular beam, used by FBVH::QueryFrustum (SPEC §2.5.2, §3.3).
#pragma once

#include "CoreMinimal.h"
#include "Math/Box.h"

/** Plane Normal . X = Offset with a unit normal; the positive side is "inside" for frustum planes. */
struct ACOUSTICSCORE_API FAcousticPlane
{
	FVector3d Normal = FVector3d(0.0, 0.0, 1.0);
	double Offset = 0.0;

	FAcousticPlane() = default;
	FAcousticPlane(const FVector3d& InNormal, const FVector3d& PointOnPlane);

	double SignedDistance(const FVector3d& P) const { return FVector3d::DotProduct(Normal, P) - Offset; }

	/** True when the whole box lies on the negative side. */
	bool IsBoxOutside(const FBox3d& Box) const;
};

/**
 * Convex region = intersection of the positive half-spaces of its planes.
 * For a triangular beam: three side planes through the apex plus the apex plane (normal along the beam axis),
 * so the region is the one-sided cone spanned by the three guide rays.
 */
struct ACOUSTICSCORE_API FAcousticFrustum
{
	TArray<FAcousticPlane, TInlineAllocator<6>> Planes;

	/** Beam with apex Apex and guide directions D0, D1, D2 (any winding; planes are oriented automatically). */
	static FAcousticFrustum FromTriangularBeam(const FVector3d& Apex, const FVector3d& D0, const FVector3d& D1, const FVector3d& D2);

	bool ContainsPoint(const FVector3d& P, double Tolerance = 1e-12) const;

	/** Conservative: false only when the box is entirely outside one plane. An empty frustum intersects everything. */
	bool IntersectsBox(const FBox3d& Box) const;
};
