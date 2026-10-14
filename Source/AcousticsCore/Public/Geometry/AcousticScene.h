// The acoustic scene handed to every solver: triangles, materials, media and the BVH (SPEC §3.3).
#pragma once

#include "CoreMinimal.h"
#include "Geometry/AcousticMaterialProps.h"
#include "Geometry/AcousticTriangle.h"
#include "Geometry/BVH.h"
#include "Geometry/Medium.h"
#include "Geometry/RayIntersection.h"

/**
 * Plain data in meters. Materials[0] is always the default (Concrete) and Media[0] is always air,
 * so a freshly reset scene is usable without further setup. Solvers treat it as read-only.
 */
struct ACOUSTICSCORE_API FAcousticScene
{
	TArray<FAcousticTriangle> Triangles;
	TArray<FAcousticMaterialProps> Materials;
	TArray<FMedium> Media;
	FBVH Bvh;

	/** Minimum triangle area accepted by AddTriangle, m^2 (0.01 mm^2). */
	static constexpr double MinTriangleArea = 1e-10;

	FAcousticScene();

	/** Back to the default material and medium, no triangles, no BVH. */
	void Reset();

	int32 AddMaterial(const FAcousticMaterialProps& Props);
	int32 FindMaterial(const FString& Name) const;
	int32 FindOrAddMaterial(const FAcousticMaterialProps& Props);
	int32 AddMedium(const FMedium& Medium);

	/** Appends the triangle after recomputing its normal; returns INDEX_NONE for degenerate ones (not added). */
	int32 AddTriangle(FAcousticTriangle Triangle);
	int32 AddTriangle(const FVector3d& A, const FVector3d& B, const FVector3d& C, int32 MaterialId = 0);

	/** Two triangles (A, B, C) and (A, C, D); the front normal follows the right-hand rule on A, B, C. */
	void AddQuad(const FVector3d& A, const FVector3d& B, const FVector3d& C, const FVector3d& D, int32 MaterialId = 0);

	void BuildBVH(int32 MaxLeafSize = FBVH::DefaultMaxLeafSize);
	bool HasBVH() const { return Bvh.IsBuiltFor(Triangles); }

	/** Closest hit through the BVH when it is built for the current triangles, brute force otherwise. */
	bool IntersectRay(const FAcousticRay& Ray, FRayHit& OutHit, const FRayQueryOptions& Options = FRayQueryOptions()) const;
	bool IntersectRayBruteForce(const FAcousticRay& Ray, FRayHit& OutHit, const FRayQueryOptions& Options = FRayQueryOptions()) const;

	/** True when any triangle (except IgnoreTriangle) cuts the open segment From-To. */
	bool IsOccluded(const FVector3d& From, const FVector3d& To, int32 IgnoreTriangle = INDEX_NONE) const;

	int32 NumTriangles() const { return Triangles.Num(); }
	double TotalArea() const;
	FBox3d ComputeBounds() const;

	/** Falls back to Materials[0] / Media[0] for out-of-range ids. */
	const FAcousticMaterialProps& GetMaterial(int32 MaterialId) const;
	const FMedium& GetMedium(int32 MediumId) const;
	const FAcousticMaterialProps& GetMaterialOf(const FAcousticTriangle& Triangle) const { return GetMaterial(Triangle.MaterialId); }

	FString DescribeStats() const;

	/**
	 * Rectangular room spanning [-X/2, X/2] x [-Y/2, Y/2] x [0, Z] meters: 6 quads = 12 triangles with inward normals,
	 * all walls using WallMaterial, BVH built. Used by the tests and by the Sabine/Eyring validation.
	 */
	static FAcousticScene MakeShoebox(const FVector3d& SizeMeters, const FAcousticMaterialProps& WallMaterial = FAcousticMaterialProps());
};
