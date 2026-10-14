// Bounding volume hierarchy over the scene triangles (SPEC §3.3): median split, leaves of 4-8 triangles.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Geometry/AcousticTriangle.h"
#include "Geometry/Frustum.h"
#include "Geometry/RayIntersection.h"

/**
 * Binary BVH built over an array of triangles that the caller owns (FAcousticScene::Triangles).
 * Every query takes the same array again, so the structure holds no pointers and copies freely.
 * Split: longest axis of the centroid bounds, at the median; leaves hold at most MaxLeafSize triangles
 * (between MaxLeafSize/2 and MaxLeafSize once the scene has more than MaxLeafSize triangles).
 */
class ACOUSTICSCORE_API FBVH
{
public:
	struct FNode
	{
		FBox3d Bounds = FBox3d(ForceInit);
		int32 LeftChild = INDEX_NONE;
		int32 RightChild = INDEX_NONE;

		/** Leaves: range [FirstIndex, FirstIndex + Count) of TriangleIndices. */
		int32 FirstIndex = 0;
		int32 Count = 0;

		bool IsLeaf() const { return Count > 0; }
	};

	static constexpr int32 DefaultMaxLeafSize = 8;

	/** Node bounds are padded by this much so triangles lying on a node face are never missed by the slab test. */
	static constexpr double BoundsPadding = 1e-9;

	void Build(TConstArrayView<FAcousticTriangle> Triangles, int32 MaxLeafSize = DefaultMaxLeafSize);
	void Reset();

	bool IsBuilt() const { return Nodes.Num() > 0; }
	bool IsBuiltFor(TConstArrayView<FAcousticTriangle> Triangles) const { return IsBuilt() && NumTrianglesBuilt == Triangles.Num(); }

	/** Closest hit. Triangles must be the array the BVH was built from. */
	bool IntersectRay(TConstArrayView<FAcousticTriangle> Triangles, const FAcousticRay& Ray, FRayHit& OutHit, const FRayQueryOptions& Options = FRayQueryOptions()) const;

	/** Indices of all triangles whose bounds intersect Box (unsorted). */
	void QueryAABB(TConstArrayView<FAcousticTriangle> Triangles, const FBox3d& Box, TArray<int32>& OutTriangles) const;

	/** Indices of all triangles whose bounds are not outside any frustum plane (conservative, unsorted). */
	void QueryFrustum(TConstArrayView<FAcousticTriangle> Triangles, const FAcousticFrustum& Frustum, TArray<int32>& OutTriangles) const;

	/** Brute-force counterparts with the same per-triangle predicates (tests, bUseBVH = false benchmarks). */
	static void QueryAABBBruteForce(TConstArrayView<FAcousticTriangle> Triangles, const FBox3d& Box, TArray<int32>& OutTriangles);
	static void QueryFrustumBruteForce(TConstArrayView<FAcousticTriangle> Triangles, const FAcousticFrustum& Frustum, TArray<int32>& OutTriangles);

	int32 NumNodes() const { return Nodes.Num(); }
	int32 NumLeaves() const;
	int32 MaxDepth() const { return MaxDepthBuilt; }
	int32 NumTriangles() const { return NumTrianglesBuilt; }
	FBox3d GetBounds() const { return Nodes.Num() > 0 ? Nodes[0].Bounds : FBox3d(ForceInit); }
	const TArray<FNode>& GetNodes() const { return Nodes; }
	const TArray<int32>& GetTriangleIndices() const { return TriangleIndices; }
	FString DescribeStats() const;

private:
	int32 BuildRecursive(const TArray<FVector3d>& Centroids, const TArray<FBox3d>& TriangleBounds, int32 Start, int32 End, int32 MaxLeafSize, int32 Depth);

	TArray<FNode> Nodes;
	TArray<int32> TriangleIndices;
	int32 NumTrianglesBuilt = 0;
	int32 MaxDepthBuilt = 0;
};
