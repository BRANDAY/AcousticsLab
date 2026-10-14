// A triangle of the acoustic scene: meters, double precision (SPEC §3.3).
#pragma once

#include "CoreMinimal.h"
#include "Math/Box.h"

/**
 * One triangle of the acoustic scene in meters.
 * The front face is the side the normal points to: N = normalize((V1 - V0) x (V2 - V0)), right-handed.
 * Room walls therefore wind so that N points into the room. UE meshes use the opposite winding
 * ((V2 - V0) x (V1 - V0)); UAcousticSceneExtractorComponent swaps V1/V2 while importing them.
 */
struct ACOUSTICSCORE_API FAcousticTriangle
{
	FVector3d V0 = FVector3d::ZeroVector;
	FVector3d V1 = FVector3d::ZeroVector;
	FVector3d V2 = FVector3d::ZeroVector;

	/** Unit front normal. ComputeNormal() keeps it in sync; FAcousticScene::AddTriangle calls it. */
	FVector3d N = FVector3d(0.0, 0.0, 1.0);

	/** Index into FAcousticScene::Materials. */
	int32 MaterialId = 0;

	/** Indices into FAcousticScene::Media: the medium on the normal side and on the opposite side. */
	int32 MediumFront = 0;
	int32 MediumBack = 0;

	/** True for a boundary between two media (refraction, SPEC §2.6); ordinary walls are false. */
	bool bInterface = false;

	FAcousticTriangle() = default;
	FAcousticTriangle(const FVector3d& InV0, const FVector3d& InV1, const FVector3d& InV2, int32 InMaterialId = 0);

	/** Recomputes N from the vertices. Returns false (N untouched) for a degenerate triangle. */
	bool ComputeNormal();

	double Area() const;
	FVector3d Centroid() const;
	FBox3d Bounds() const;

	/** Offset d of the plane N . X = d that contains the triangle. */
	double PlaneOffset() const { return FVector3d::DotProduct(N, V0); }

	/** Signed distance of a point from the triangle plane, positive on the front side. */
	double SignedDistance(const FVector3d& P) const { return FVector3d::DotProduct(N, P - V0); }

	bool IsDegenerate(double MinArea = 1e-10) const { return Area() < MinArea; }
};
