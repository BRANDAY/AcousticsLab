#include "Geometry/PolygonClipping.h"

#include "Algo/Reverse.h"

namespace AcousticGeometry
{
	double SignedArea(TConstArrayView<FVector2d> Polygon)
	{
		double Sum = 0.0;
		const int32 Num = Polygon.Num();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FVector2d& P = Polygon[Index];
			const FVector2d& Q = Polygon[(Index + 1) % Num];
			Sum += P.X * Q.Y - Q.X * P.Y;
		}
		return 0.5 * Sum;
	}

	void MakeCounterClockwise(FPolygon2d& Polygon)
	{
		if (SignedArea(Polygon) < 0.0)
		{
			Algo::Reverse(Polygon);
		}
	}

	static double Cross2(const FVector2d& A, const FVector2d& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	bool IsConvexCounterClockwise(TConstArrayView<FVector2d> Polygon, double Tolerance)
	{
		const int32 Num = Polygon.Num();
		if (Num < 3 || !(SignedArea(Polygon) > 0.0))
		{
			return false;
		}
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FVector2d& A = Polygon[Index];
			const FVector2d& B = Polygon[(Index + 1) % Num];
			const FVector2d& C = Polygon[(Index + 2) % Num];
			if (Cross2(B - A, C - B) < -Tolerance)
			{
				return false;
			}
		}
		return true;
	}

	bool ContainsPoint(TConstArrayView<FVector2d> ConvexPolygon, const FVector2d& Point, double Tolerance)
	{
		const int32 Num = ConvexPolygon.Num();
		if (Num < 3)
		{
			return false;
		}
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FVector2d& A = ConvexPolygon[Index];
			const FVector2d& B = ConvexPolygon[(Index + 1) % Num];
			const FVector2d Edge = B - A;
			const double Length = Edge.Length();
			if (Length <= 0.0)
			{
				continue;
			}
			// Signed distance from the edge line, positive on the left (inside).
			if (Cross2(Edge, Point - A) / Length < -Tolerance)
			{
				return false;
			}
		}
		return true;
	}

	FVector2d Centroid(TConstArrayView<FVector2d> Polygon)
	{
		// Area-weighted centroid; falls back to the vertex mean for degenerate polygons.
		const int32 Num = Polygon.Num();
		if (Num == 0)
		{
			return FVector2d::ZeroVector;
		}
		double Area2 = 0.0;
		FVector2d Weighted = FVector2d::ZeroVector;
		FVector2d Mean = FVector2d::ZeroVector;
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FVector2d& P = Polygon[Index];
			const FVector2d& Q = Polygon[(Index + 1) % Num];
			const double Cross = Cross2(P, Q);
			Area2 += Cross;
			Weighted += (P + Q) * Cross;
			Mean += P;
		}
		if (FMath::Abs(Area2) <= 1e-300)
		{
			return Mean / static_cast<double>(Num);
		}
		return Weighted / (3.0 * Area2);
	}

	void WeldVertices(FPolygon2d& Polygon, double Epsilon)
	{
		if (Polygon.Num() < 2)
		{
			return;
		}
		FPolygon2d Welded;
		Welded.Reserve(Polygon.Num());
		for (const FVector2d& Vertex : Polygon)
		{
			if (Welded.Num() == 0 || FVector2d::Distance(Welded.Last(), Vertex) > Epsilon)
			{
				Welded.Add(Vertex);
			}
		}
		while (Welded.Num() > 1 && FVector2d::Distance(Welded.Last(), Welded[0]) <= Epsilon)
		{
			Welded.Pop(EAllowShrinking::No);
		}
		Polygon = MoveTemp(Welded);
	}

	bool ClipByLine(TConstArrayView<FVector2d> Subject, const FVector2d& A, const FVector2d& B, bool bKeepLeft, FPolygon2d& OutPolygon)
	{
		OutPolygon.Reset();
		const int32 Num = Subject.Num();
		if (Num < 3)
		{
			return false;
		}
		const FVector2d Edge = B - A;
		const double Sign = bKeepLeft ? 1.0 : -1.0;

		// Signed "inside" measure of every vertex: >= 0 is kept. Both halves use the same arithmetic so their cut edges coincide.
		TArray<double, TInlineAllocator<16>> Side;
		Side.Reserve(Num);
		for (const FVector2d& Vertex : Subject)
		{
			Side.Add(Sign * Cross2(Edge, Vertex - A));
		}

		OutPolygon.Reserve(Num + 2);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const int32 Next = (Index + 1) % Num;
			const FVector2d& P = Subject[Index];
			const FVector2d& Q = Subject[Next];
			const double SideP = Side[Index];
			const double SideQ = Side[Next];
			const bool bInP = SideP >= 0.0;
			const bool bInQ = SideQ >= 0.0;
			if (bInP)
			{
				OutPolygon.Add(P);
			}
			if (bInP != bInQ)
			{
				// The crossing parameter uses |SideP| / (|SideP| + |SideQ|): symmetric in the two halves.
				const double T = FMath::Abs(SideP) / (FMath::Abs(SideP) + FMath::Abs(SideQ));
				OutPolygon.Add(P + (Q - P) * T);
			}
		}
		WeldVertices(OutPolygon);
		if (OutPolygon.Num() < 3 || FMath::Abs(SignedArea(OutPolygon)) < ClipAreaEpsilon)
		{
			OutPolygon.Reset();
			return false;
		}
		return true;
	}

	FPolygon2d ClipPolygon(TConstArrayView<FVector2d> Subject, TConstArrayView<FVector2d> ClipConvex)
	{
		FPolygon2d Current(Subject.GetData(), Subject.Num());
		const int32 NumEdges = ClipConvex.Num();
		if (NumEdges < 3)
		{
			return FPolygon2d();
		}
		FPolygon2d Next;
		for (int32 Index = 0; Index < NumEdges && Current.Num() >= 3; ++Index)
		{
			ClipByLine(Current, ClipConvex[Index], ClipConvex[(Index + 1) % NumEdges], true, Next);
			Current = MoveTemp(Next);
		}
		return Current;
	}

	void SplitPolygon(TConstArrayView<FVector2d> Subject, TConstArrayView<FVector2d> ClipConvex, FPolygon2d& OutInside, TArray<FPolygon2d>& OutOutside)
	{
		OutInside.Reset();
		OutOutside.Reset();
		const int32 NumEdges = ClipConvex.Num();
		if (Subject.Num() < 3)
		{
			return;
		}
		if (NumEdges < 3)
		{
			OutOutside.Emplace(Subject.GetData(), Subject.Num());
			return;
		}
		FPolygon2d Remaining(Subject.GetData(), Subject.Num());
		FPolygon2d Piece;
		FPolygon2d Kept;
		for (int32 Index = 0; Index < NumEdges && Remaining.Num() >= 3; ++Index)
		{
			const FVector2d& A = ClipConvex[Index];
			const FVector2d& B = ClipConvex[(Index + 1) % NumEdges];
			if (ClipByLine(Remaining, A, B, false, Piece))
			{
				OutOutside.Add(Piece);
			}
			ClipByLine(Remaining, A, B, true, Kept);
			Remaining = MoveTemp(Kept);
		}
		if (Remaining.Num() >= 3)
		{
			OutInside = MoveTemp(Remaining);
		}
	}

	void FanTriangulate(int32 NumVertices, TArray<FIntVector>& OutTriangles)
	{
		OutTriangles.Reset();
		for (int32 Index = 1; Index + 1 < NumVertices; ++Index)
		{
			OutTriangles.Emplace(0, Index, Index + 1);
		}
	}
}
