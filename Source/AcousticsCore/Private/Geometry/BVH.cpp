#include "Geometry/BVH.h"

#include "AcousticsCore.h"
#include "Algo/Sort.h"

void FBVH::Reset()
{
	Nodes.Reset();
	TriangleIndices.Reset();
	NumTrianglesBuilt = 0;
	MaxDepthBuilt = 0;
}

void FBVH::Build(TConstArrayView<FAcousticTriangle> Triangles, int32 MaxLeafSize)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FBVH::Build);
	Reset();
	NumTrianglesBuilt = Triangles.Num();
	if (Triangles.Num() == 0)
	{
		return;
	}
	MaxLeafSize = FMath::Max(1, MaxLeafSize);

	TArray<FVector3d> Centroids;
	TArray<FBox3d> TriangleBounds;
	Centroids.Reserve(Triangles.Num());
	TriangleBounds.Reserve(Triangles.Num());
	TriangleIndices.SetNumUninitialized(Triangles.Num());
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		Centroids.Add(Triangles[Index].Centroid());
		TriangleBounds.Add(Triangles[Index].Bounds());
		TriangleIndices[Index] = Index;
	}

	Nodes.Reserve(2 * (Triangles.Num() / MaxLeafSize + 1));
	BuildRecursive(Centroids, TriangleBounds, 0, Triangles.Num(), MaxLeafSize, 0);
}

int32 FBVH::BuildRecursive(const TArray<FVector3d>& Centroids, const TArray<FBox3d>& TriangleBounds, int32 Start, int32 End, int32 MaxLeafSize, int32 Depth)
{
	const int32 NodeIndex = Nodes.AddDefaulted();
	MaxDepthBuilt = FMath::Max(MaxDepthBuilt, Depth);

	FBox3d Bounds(ForceInit);
	FBox3d CentroidBounds(ForceInit);
	for (int32 Index = Start; Index < End; ++Index)
	{
		const int32 Triangle = TriangleIndices[Index];
		Bounds += TriangleBounds[Triangle];
		CentroidBounds += Centroids[Triangle];
	}
	Nodes[NodeIndex].Bounds = Bounds.ExpandBy(BoundsPadding);

	const int32 Count = End - Start;
	if (Count <= MaxLeafSize)
	{
		Nodes[NodeIndex].FirstIndex = Start;
		Nodes[NodeIndex].Count = Count;
		return NodeIndex;
	}

	const FVector3d Extent = CentroidBounds.GetSize();
	const int32 Axis = Extent.X >= Extent.Y ? (Extent.X >= Extent.Z ? 0 : 2) : (Extent.Y >= Extent.Z ? 1 : 2);
	if (Extent[Axis] > 0.0)
	{
		TArrayView<int32> Range(TriangleIndices.GetData() + Start, Count);
		Algo::Sort(Range, [&Centroids, Axis](int32 A, int32 B) { return Centroids[A][Axis] < Centroids[B][Axis]; });
	}
	// Else every centroid coincides: split by index order only to keep the leaves small.

	const int32 Mid = Start + Count / 2;
	// Children are addressed by index: Nodes may reallocate during the recursion, so no references are held across the calls.
	const int32 Left = BuildRecursive(Centroids, TriangleBounds, Start, Mid, MaxLeafSize, Depth + 1);
	const int32 Right = BuildRecursive(Centroids, TriangleBounds, Mid, End, MaxLeafSize, Depth + 1);
	Nodes[NodeIndex].LeftChild = Left;
	Nodes[NodeIndex].RightChild = Right;
	return NodeIndex;
}

bool FBVH::IntersectRay(TConstArrayView<FAcousticTriangle> Triangles, const FAcousticRay& Ray, FRayHit& OutHit, const FRayQueryOptions& Options) const
{
	OutHit.Reset();
	if (!IsBuilt())
	{
		return false;
	}
	if (!ensureMsgf(Triangles.Num() == NumTrianglesBuilt, TEXT("FBVH::IntersectRay: built for %d triangles, got %d"), NumTrianglesBuilt, Triangles.Num()))
	{
		return false;
	}

	const FVector3d InvDirection = AcousticGeometry::SafeInverse(Ray.Direction);
	double BestT = Options.TMax;

	TArray<int32, TInlineAllocator<64>> Stack;
	Stack.Push(0);
	while (Stack.Num() > 0)
	{
		const int32 NodeIndex = Stack.Pop(EAllowShrinking::No);
		const FNode& Node = Nodes[NodeIndex];

		double Entry;
		if (!AcousticGeometry::IntersectRayBox(Ray.Origin, Ray.Direction, InvDirection, Node.Bounds, Options.TMin, BestT, Entry))
		{
			continue;
		}

		if (Node.IsLeaf())
		{
			for (int32 Index = 0; Index < Node.Count; ++Index)
			{
				const int32 Triangle = TriangleIndices[Node.FirstIndex + Index];
				if (Triangle == Options.IgnoreTriangle)
				{
					continue;
				}
				double T, U, V;
				bool bBackFace;
				if (!AcousticGeometry::IntersectRayTriangle(Ray, Triangles[Triangle], T, U, V, bBackFace))
				{
					continue;
				}
				if (T < Options.TMin || T >= BestT || (Options.bCullBackFaces && bBackFace))
				{
					continue;
				}
				BestT = T;
				OutHit.T = T;
				OutHit.TriangleIndex = Triangle;
				OutHit.U = U;
				OutHit.V = V;
				OutHit.bBackFace = bBackFace;
			}
			continue;
		}

		// Visit the nearer child first so BestT shrinks early and prunes the other one.
		double EntryLeft = 0.0;
		double EntryRight = 0.0;
		const bool bLeft = AcousticGeometry::IntersectRayBox(Ray.Origin, Ray.Direction, InvDirection, Nodes[Node.LeftChild].Bounds, Options.TMin, BestT, EntryLeft);
		const bool bRight = AcousticGeometry::IntersectRayBox(Ray.Origin, Ray.Direction, InvDirection, Nodes[Node.RightChild].Bounds, Options.TMin, BestT, EntryRight);
		if (bLeft && bRight)
		{
			if (EntryLeft <= EntryRight)
			{
				Stack.Push(Node.RightChild);
				Stack.Push(Node.LeftChild);
			}
			else
			{
				Stack.Push(Node.LeftChild);
				Stack.Push(Node.RightChild);
			}
		}
		else if (bLeft)
		{
			Stack.Push(Node.LeftChild);
		}
		else if (bRight)
		{
			Stack.Push(Node.RightChild);
		}
	}
	return OutHit.IsHit();
}

void FBVH::QueryAABB(TConstArrayView<FAcousticTriangle> Triangles, const FBox3d& Box, TArray<int32>& OutTriangles) const
{
	OutTriangles.Reset();
	if (!IsBuilt() || !ensureMsgf(Triangles.Num() == NumTrianglesBuilt, TEXT("FBVH::QueryAABB: triangle count mismatch")))
	{
		return;
	}

	TArray<int32, TInlineAllocator<64>> Stack;
	Stack.Push(0);
	while (Stack.Num() > 0)
	{
		const FNode& Node = Nodes[Stack.Pop(EAllowShrinking::No)];
		if (!Node.Bounds.Intersect(Box))
		{
			continue;
		}
		if (Node.IsLeaf())
		{
			for (int32 Index = 0; Index < Node.Count; ++Index)
			{
				const int32 Triangle = TriangleIndices[Node.FirstIndex + Index];
				if (Triangles[Triangle].Bounds().Intersect(Box))
				{
					OutTriangles.Add(Triangle);
				}
			}
		}
		else
		{
			Stack.Push(Node.LeftChild);
			Stack.Push(Node.RightChild);
		}
	}
}

void FBVH::QueryFrustum(TConstArrayView<FAcousticTriangle> Triangles, const FAcousticFrustum& Frustum, TArray<int32>& OutTriangles) const
{
	OutTriangles.Reset();
	if (!IsBuilt() || !ensureMsgf(Triangles.Num() == NumTrianglesBuilt, TEXT("FBVH::QueryFrustum: triangle count mismatch")))
	{
		return;
	}

	TArray<int32, TInlineAllocator<64>> Stack;
	Stack.Push(0);
	while (Stack.Num() > 0)
	{
		const FNode& Node = Nodes[Stack.Pop(EAllowShrinking::No)];
		if (!Frustum.IntersectsBox(Node.Bounds))
		{
			continue;
		}
		if (Node.IsLeaf())
		{
			for (int32 Index = 0; Index < Node.Count; ++Index)
			{
				const int32 Triangle = TriangleIndices[Node.FirstIndex + Index];
				if (Frustum.IntersectsBox(Triangles[Triangle].Bounds()))
				{
					OutTriangles.Add(Triangle);
				}
			}
		}
		else
		{
			Stack.Push(Node.LeftChild);
			Stack.Push(Node.RightChild);
		}
	}
}

void FBVH::QueryAABBBruteForce(TConstArrayView<FAcousticTriangle> Triangles, const FBox3d& Box, TArray<int32>& OutTriangles)
{
	OutTriangles.Reset();
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		if (Triangles[Index].Bounds().Intersect(Box))
		{
			OutTriangles.Add(Index);
		}
	}
}

void FBVH::QueryFrustumBruteForce(TConstArrayView<FAcousticTriangle> Triangles, const FAcousticFrustum& Frustum, TArray<int32>& OutTriangles)
{
	OutTriangles.Reset();
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		if (Frustum.IntersectsBox(Triangles[Index].Bounds()))
		{
			OutTriangles.Add(Index);
		}
	}
}

int32 FBVH::NumLeaves() const
{
	int32 Leaves = 0;
	for (const FNode& Node : Nodes)
	{
		Leaves += Node.IsLeaf() ? 1 : 0;
	}
	return Leaves;
}

FString FBVH::DescribeStats() const
{
	return FString::Printf(TEXT("BVH: %d triangles, %d nodes, %d leaves, depth %d"), NumTrianglesBuilt, Nodes.Num(), NumLeaves(), MaxDepthBuilt);
}
