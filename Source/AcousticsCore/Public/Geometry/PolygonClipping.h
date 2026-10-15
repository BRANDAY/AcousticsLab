// Convex polygon clipping in a plane (Sutherland-Hodgman) for the beam division of SPEC §2.5.3.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

/** A polygon in a plane: vertices in counter-clockwise order (positive signed area). */
using FPolygon2d = TArray<FVector2d>;

namespace AcousticGeometry
{
	/** Vertices closer than this (plane units) are welded after a clip; pieces with less area than AreaEpsilon are dropped. */
	constexpr double ClipWeldEpsilon = 1e-12;
	constexpr double ClipAreaEpsilon = 1e-18;

	/** Twice the signed area is the shoelace sum; positive for counter-clockwise vertices. */
	ACOUSTICSCORE_API double SignedArea(TConstArrayView<FVector2d> Polygon);

	/** Reverses the vertex order when the polygon is clockwise. */
	ACOUSTICSCORE_API void MakeCounterClockwise(FPolygon2d& Polygon);

	/** Every vertex lies on the left of (or on) every edge: convex and counter-clockwise. Degenerate polygons are not convex. */
	ACOUSTICSCORE_API bool IsConvexCounterClockwise(TConstArrayView<FVector2d> Polygon, double Tolerance = 0.0);

	/** True when the point lies inside the convex counter-clockwise polygon or within Tolerance of its boundary. */
	ACOUSTICSCORE_API bool ContainsPoint(TConstArrayView<FVector2d> ConvexPolygon, const FVector2d& Point, double Tolerance = 0.0);

	ACOUSTICSCORE_API FVector2d Centroid(TConstArrayView<FVector2d> Polygon);

	/**
	 * One Sutherland-Hodgman step: the part of Subject on the left of the directed line A -> B (bKeepLeft) or on its right.
	 * Boundary points belong to both sides, so the two halves of a polygon share their cut edge exactly (same intersection
	 * arithmetic for both). Near-duplicate vertices are welded. Returns false (OutPolygon empty) when nothing remains.
	 */
	ACOUSTICSCORE_API bool ClipByLine(TConstArrayView<FVector2d> Subject, const FVector2d& A, const FVector2d& B, bool bKeepLeft, FPolygon2d& OutPolygon);

	/** Subject clipped against every edge of the convex counter-clockwise polygon Clip. Empty when the polygons do not overlap. */
	ACOUSTICSCORE_API FPolygon2d ClipPolygon(TConstArrayView<FVector2d> Subject, TConstArrayView<FVector2d> ClipConvex);

	/**
	 * Splits Subject into the part inside the convex polygon Clip (OutInside, possibly empty) and the parts outside it
	 * (OutOutside): piece i is the part of Subject outside edge i and inside edges 0 .. i - 1 of Clip, so the pieces are
	 * convex, do not overlap and together with OutInside cover Subject exactly (SPEC §2.5.3: the uncovered parts of a
	 * beam cross-section continue as separate beams). Pieces below ClipAreaEpsilon are dropped.
	 */
	ACOUSTICSCORE_API void SplitPolygon(TConstArrayView<FVector2d> Subject, TConstArrayView<FVector2d> ClipConvex, FPolygon2d& OutInside, TArray<FPolygon2d>& OutOutside);

	/** Fan triangulation of a convex polygon: triangles (0, i, i + 1) as index triples; empty for fewer than 3 vertices. */
	ACOUSTICSCORE_API void FanTriangulate(int32 NumVertices, TArray<FIntVector>& OutTriangles);

	/** Removes vertices within Epsilon of their predecessor (cyclically); keeps the first vertex. */
	ACOUSTICSCORE_API void WeldVertices(FPolygon2d& Polygon, double Epsilon = ClipWeldEpsilon);
}
