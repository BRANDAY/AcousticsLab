#include "Solvers/BeamTracer.h"
#include "Solvers/ImageSourceSolver.h"

#include "AcousticsCore.h"
#include "Algo/Sort.h"
#include "Geometry/AcousticScene.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DECLARE_CYCLE_STAT(TEXT("BeamTracer Run"), STAT_AcousticsBeamTracerRun, STATGROUP_Acoustics);
DECLARE_CYCLE_STAT(TEXT("BeamTracer BuildTree"), STAT_AcousticsBeamTracerBuild, STATGROUP_Acoustics);

FBeamTracerSettings::FBeamTracerSettings()
{
	for (double& Value : AirAbsorption)
	{
		Value = Acoustics::DefaultAirAbsorptionPerMeter;
	}
}

bool FBeamTracerSettings::Validate(FString& OutError) const
{
	if (SubdivisionLevel < 0 || SubdivisionLevel > 7)
	{
		OutError = TEXT("SubdivisionLevel must lie in 0..7");
		return false;
	}
	if (MaxBeamOrder < 0)
	{
		OutError = TEXT("MaxBeamOrder must not be negative");
		return false;
	}
	if (!(BinSeconds > 0.0) || !(MaxTimeSeconds > BinSeconds))
	{
		OutError = TEXT("BinSeconds must be positive and smaller than MaxTimeSeconds");
		return false;
	}
	if (!(EnergyThreshold > 0.0) || EnergyThreshold >= 1.0)
	{
		OutError = TEXT("EnergyThreshold must lie in (0, 1)");
		return false;
	}
	if (MaxBeamNodes < 1)
	{
		OutError = TEXT("MaxBeamNodes must be at least 1");
		return false;
	}
	return true;
}

FBeamNode::FBeamNode()
{
	for (FVector3d& Direction : Directions)
	{
		Direction = FVector3d(0.0, 0.0, 1.0);
	}
	for (double& Value : Factor)
	{
		Value = 1.0;
	}
	for (FVector3d& Point : StartPoints)
	{
		Point = FVector3d::ZeroVector;
	}
	for (FVector3d& Point : EndPoints)
	{
		Point = FVector3d::ZeroVector;
	}
}

void FBeamNode::ComputeSidePlanes()
{
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FVector3d& A = Directions[Index];
		const FVector3d& B = Directions[(Index + 1) % 3];
		const FVector3d& C = Directions[(Index + 2) % 3];
		FVector3d Normal = FVector3d::CrossProduct(A, B);
		const double Length = Normal.Length();
		if (Length > 1e-300)
		{
			Normal /= Length;
			if (FVector3d::DotProduct(Normal, C) < 0.0)
			{
				Normal = -Normal;
			}
		}
		else
		{
			Normal = (A + B + C).GetSafeNormal(); // degenerate pair: a plane that accepts the whole half-space in front
		}
		SidePlanes[Index] = FAcousticPlane(Normal, Apex);
	}
}

bool FBeamNode::ConeContainsPoint(const FVector3d& Point, double Tolerance) const
{
	for (const FAcousticPlane& Plane : SidePlanes)
	{
		if (Plane.SignedDistance(Point) < -Tolerance)
		{
			return false;
		}
	}
	return true;
}

bool FBeamNode::ContainsPoint(const FVector3d& Point, double Tolerance) const
{
	if (!ConeContainsPoint(Point, Tolerance))
	{
		return false;
	}
	if (bHasReflectionPlane && ReflectionPlane.SignedDistance(Point) < -Tolerance)
	{
		return false; // between the mirrored apex and the wall: not reachable
	}
	if (HitTriangle != INDEX_NONE && HitPlane.SignedDistance(Point) < -Tolerance)
	{
		return false; // behind the triangle the beam hits next
	}
	return true;
}

void FBeamTree::Query(const FVector3d& Point, TArray<FBeamContribution>& OutContributions, bool bRemoveDuplicateSequences) const
{
	OutContributions.Reset();
	TSet<uint64> Seen;
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const FBeamNode& Node = Nodes[Index];
		if (!Node.ContainsPoint(Point))
		{
			continue;
		}
		if (bRemoveDuplicateSequences)
		{
			bool bAlready = false;
			Seen.Add(Node.SequenceHash, &bAlready);
			if (bAlready)
			{
				continue;
			}
		}
		FBeamContribution& Contribution = OutContributions.AddDefaulted_GetRef();
		Contribution.Node = Index;
		Contribution.Order = Node.Order;
		Contribution.Distance = (Point - Node.Apex).Length();
	}
}

void FBeamTree::TracePath(int32 NodeIndex, const FVector3d& Point, TArray<FVector3d>& OutPath, TArray<int32>& OutTriangles) const
{
	OutPath.Reset();
	OutTriangles.Reset();
	if (!Nodes.IsValidIndex(NodeIndex))
	{
		return;
	}
	// Back from the point: the segment to the apex of every node crosses the plane it was reflected from.
	TArray<FVector3d, TInlineAllocator<8>> Reverse;
	TArray<int32, TInlineAllocator<8>> ReverseTriangles;
	Reverse.Add(Point);
	FVector3d Current = Point;
	int32 Index = NodeIndex;
	while (Index != INDEX_NONE)
	{
		const FBeamNode& Node = Nodes[Index];
		if (Node.bHasReflectionPlane)
		{
			const FVector3d ToApex = Node.Apex - Current;
			const double DistanceCurrent = Node.ReflectionPlane.SignedDistance(Current);
			const double DistanceApex = Node.ReflectionPlane.SignedDistance(Node.Apex);
			const double Denominator = DistanceCurrent - DistanceApex;
			const double T = FMath::Abs(Denominator) > 1e-300 ? FMath::Clamp(DistanceCurrent / Denominator, 0.0, 1.0) : 0.0;
			Current = Current + ToApex * T;
			Reverse.Add(Current);
			ReverseTriangles.Add(Node.ReflectionTriangle);
		}
		Index = Node.Parent;
	}
	Reverse.Add(Source);
	OutPath.Reserve(Reverse.Num());
	for (int32 I = Reverse.Num() - 1; I >= 0; --I)
	{
		OutPath.Add(Reverse[I]);
	}
	OutTriangles.Reserve(ReverseTriangles.Num());
	for (int32 I = ReverseTriangles.Num() - 1; I >= 0; --I)
	{
		OutTriangles.Add(ReverseTriangles[I]);
	}
}

bool FBeamTree::IsIdentical(const FBeamTree& Other, double Tolerance) const
{
	if (Nodes.Num() != Other.Nodes.Num() || NumRootBeams != Other.NumRootBeams || MaxOrder != Other.MaxOrder || !Source.Equals(Other.Source, Tolerance))
	{
		return false;
	}
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const FBeamNode& A = Nodes[Index];
		const FBeamNode& B = Other.Nodes[Index];
		if (A.Order != B.Order || A.Parent != B.Parent || A.HitTriangle != B.HitTriangle || A.ReflectionTriangle != B.ReflectionTriangle || A.RootBeam != B.RootBeam || A.SequenceHash != B.SequenceHash)
		{
			return false;
		}
		if (!A.Apex.Equals(B.Apex, Tolerance))
		{
			return false;
		}
		for (int32 I = 0; I < 3; ++I)
		{
			if (!A.Directions[I].Equals(B.Directions[I], Tolerance) || !A.EndPoints[I].Equals(B.EndPoints[I], Tolerance))
			{
				return false;
			}
		}
		for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
		{
			if (!FMath::IsNearlyEqual(A.Factor[Band], B.Factor[Band], Tolerance))
			{
				return false;
			}
		}
	}
	return true;
}

FString FBeamTree::DescribeStats() const
{
	FString PerOrder;
	for (int32 Order = 0; Order < NumNodesPerOrder.Num(); ++Order)
	{
		PerOrder += FString::Printf(TEXT("%s%d"), Order > 0 ? TEXT("/") : TEXT(""), NumNodesPerOrder[Order]);
	}
	return FString::Printf(TEXT("%d root beams (level %d), %d nodes by order %s, %d escaped, %d unresolved, %d slivers dropped%s"),
		NumRootBeams, SubdivisionLevel, Nodes.Num(), *PerOrder, NumEscaped, NumUnresolved, NumSlivers, bTruncated ? TEXT(", TRUNCATED by the node budget") : TEXT(""));
}

FQuat4d FBeamTracer::EmissionRotation()
{
	// Arbitrary fixed angles: no edge of the rotated icosahedron lies in a coordinate plane, so a source and a receiver
	// at the same height never sit exactly on a beam boundary.
	return FQuat4d::MakeFromEuler(FVector3d(17.3, 29.1, 41.7));
}

void FBeamTracer::EmissionDirections(int32 Level, TArray<FVector3d>& OutDirections)
{
	OutDirections.Reset();
	const double T = (1.0 + FMath::Sqrt(5.0)) * 0.5;
	const FVector3d Vertices[12] = {
		FVector3d(-1.0, T, 0.0), FVector3d(1.0, T, 0.0), FVector3d(-1.0, -T, 0.0), FVector3d(1.0, -T, 0.0),
		FVector3d(0.0, -1.0, T), FVector3d(0.0, 1.0, T), FVector3d(0.0, -1.0, -T), FVector3d(0.0, 1.0, -T),
		FVector3d(T, 0.0, -1.0), FVector3d(T, 0.0, 1.0), FVector3d(-T, 0.0, -1.0), FVector3d(-T, 0.0, 1.0)
	};
	const int32 Faces[20][3] = {
		{ 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
		{ 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
		{ 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
		{ 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 }
	};
	const FQuat4d Rotation = EmissionRotation();
	const int32 NumBeams = FBeamTracerSettings::NumRootBeams(Level);
	OutDirections.Reserve(NumBeams * 3);

	struct FSubdivider
	{
		static void Split(const FVector3d& A, const FVector3d& B, const FVector3d& C, int32 Depth, TArray<FVector3d>& Out)
		{
			if (Depth == 0)
			{
				Out.Add(A);
				Out.Add(B);
				Out.Add(C);
				return;
			}
			const FVector3d AB = (A + B).GetSafeNormal();
			const FVector3d BC = (B + C).GetSafeNormal();
			const FVector3d CA = (C + A).GetSafeNormal();
			Split(A, AB, CA, Depth - 1, Out);
			Split(AB, B, BC, Depth - 1, Out);
			Split(CA, BC, C, Depth - 1, Out);
			Split(AB, BC, CA, Depth - 1, Out);
		}
	};
	for (const int32(&Face)[3] : Faces)
	{
		FSubdivider::Split(Rotation.RotateVector(Vertices[Face[0]].GetSafeNormal()), Rotation.RotateVector(Vertices[Face[1]].GetSafeNormal()), Rotation.RotateVector(Vertices[Face[2]].GetSafeNormal()), FMath::Max(0, Level), OutDirections);
	}
	check(OutDirections.Num() == NumBeams * 3);
}

FVector3d FBeamTracer::MirrorPoint(const FVector3d& Point, const FAcousticPlane& Plane)
{
	return Point - Plane.Normal * (2.0 * Plane.SignedDistance(Point));
}

FVector3d FBeamTracer::ReflectDirection(const FVector3d& Direction, const FVector3d& Normal)
{
	return (Direction - Normal * (2.0 * FVector3d::DotProduct(Direction, Normal))).GetSafeNormal();
}

bool FBeamTracer::RayPlaneHit(const FVector3d& Apex, const FVector3d& Direction, const FAcousticPlane& Plane, FVector3d& OutPoint, double& OutDistance)
{
	const double Denominator = FVector3d::DotProduct(Direction, Plane.Normal);
	if (FMath::Abs(Denominator) < 1e-15)
	{
		return false;
	}
	OutDistance = -Plane.SignedDistance(Apex) / Denominator;
	if (!(OutDistance > 0.0))
	{
		return false;
	}
	OutPoint = Apex + Direction * OutDistance;
	return true;
}

void FBeamTracer::ClipPolygon3DByPlane(TArray<FVector3d>& Polygon, const FAcousticPlane& Plane, double Offset)
{
	const int32 Num = Polygon.Num();
	if (Num < 3)
	{
		Polygon.Reset();
		return;
	}
	TArray<FVector3d, TInlineAllocator<8>> Out;
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FVector3d& P = Polygon[Index];
		const FVector3d& Q = Polygon[(Index + 1) % Num];
		const double SideP = Plane.SignedDistance(P) - Offset;
		const double SideQ = Plane.SignedDistance(Q) - Offset;
		const bool bInP = SideP >= 0.0;
		const bool bInQ = SideQ >= 0.0;
		if (bInP)
		{
			Out.Add(P);
		}
		if (bInP != bInQ)
		{
			const double T = FMath::Abs(SideP) / (FMath::Abs(SideP) + FMath::Abs(SideQ));
			Out.Add(P + (Q - P) * T);
		}
	}
	Polygon.Reset();
	for (const FVector3d& Vertex : Out)
	{
		if (Polygon.Num() == 0 || !Polygon.Last().Equals(Vertex, 1e-12))
		{
			Polygon.Add(Vertex);
		}
	}
	while (Polygon.Num() > 1 && Polygon.Last().Equals(Polygon[0], 1e-12))
	{
		Polygon.Pop(EAllowShrinking::No);
	}
	if (Polygon.Num() < 3)
	{
		Polygon.Reset();
	}
}

namespace
{
	using namespace AcousticGeometry;

	/** Reference plane of a beam: directions are mapped by the gnomonic projection from the apex. */
	struct FBeamFrame
	{
		FVector3d Apex;
		FVector3d Axis;
		FVector3d E1;
		FVector3d E2;

		FBeamFrame(const FVector3d& InApex, const FVector3d& D0, const FVector3d& D1, const FVector3d& D2)
			: Apex(InApex)
		{
			Axis = (D0 + D1 + D2).GetSafeNormal();
			const FVector3d Helper = FMath::Abs(Axis.X) < 0.9 ? FVector3d(1.0, 0.0, 0.0) : FVector3d(0.0, 1.0, 0.0);
			E1 = FVector3d::CrossProduct(Helper, Axis).GetSafeNormal();
			E2 = FVector3d::CrossProduct(Axis, E1);
		}

		bool ProjectDirection(const FVector3d& Direction, FVector2d& Out) const
		{
			const double W = FVector3d::DotProduct(Direction, Axis);
			if (!(W > 1e-12))
			{
				return false;
			}
			Out = FVector2d(FVector3d::DotProduct(Direction, E1) / W, FVector3d::DotProduct(Direction, E2) / W);
			return true;
		}

		bool ProjectPoint(const FVector3d& Point, FVector2d& Out) const
		{
			return ProjectDirection(Point - Apex, Out);
		}

		FVector3d Unproject(const FVector2d& P) const
		{
			return (E1 * P.X + E2 * P.Y + Axis).GetSafeNormal();
		}
	};

	/** A beam waiting to be divided. */
	struct FPendingBeam
	{
		FVector3d Apex = FVector3d::ZeroVector;
		FVector3d Directions[3];
		int32 Order = 0;
		double Factor[Acoustics::NumBands];
		FAcousticPlane ReflectionPlane;
		bool bHasReflectionPlane = false;
		int32 ReflectionTriangle = INDEX_NONE;
		int32 Parent = INDEX_NONE;
		int32 RootBeam = INDEX_NONE;
		uint64 SequenceHash = 0;
	};

	struct FCandidate
	{
		int32 Triangle = INDEX_NONE;
		double Distance = 0.0;
		FPolygon2d Projected;
		FAcousticPlane HitPlane;
	};

	uint64 CombineSequence(uint64 ParentHash, int32 Triangle)
	{
		uint64 Hash = ParentHash ^ (static_cast<uint64>(Triangle + 1) * 0x9E3779B97F4A7C15ull);
		Hash ^= Hash >> 29;
		Hash *= 0xBF58476D1CE4E5B9ull;
		Hash ^= Hash >> 32;
		return Hash;
	}

	/**
	 * Pieces smaller than this (reference-plane units: solid angle near the axis) or thinner than MinPieceAltitude are
	 * slivers from the clipping arithmetic, not geometry: dropped and counted. The beam triangle of level 2 has an area of
	 * about 0.02; a sliver of altitude 1e-9 is 10 nm wide at 10 m. Nearly collinear corner directions would also make the
	 * three side planes of the node ill-conditioned (their orientation becomes a rounding decision).
	 */
	constexpr double MinPieceArea = 1e-12;
	constexpr double MinPieceAltitude = 1e-9;

	bool IsSliver(TConstArrayView<FVector2d> Polygon)
	{
		if (Polygon.Num() < 3)
		{
			return true;
		}
		const double Area = FMath::Abs(SignedArea(Polygon));
		if (Area < MinPieceArea)
		{
			return true;
		}
		double LongestEdge = 0.0;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			LongestEdge = FMath::Max(LongestEdge, FVector2d::Distance(Polygon[Index], Polygon[(Index + 1) % Polygon.Num()]));
		}
		return 2.0 * Area < MinPieceAltitude * LongestEdge;
	}

	bool IsSliver(const FVector2d& P0, const FVector2d& P1, const FVector2d& P2)
	{
		const FVector2d Triangle[3] = { P0, P1, P2 };
		return IsSliver(MakeArrayView(Triangle, 3));
	}

	struct FBeamBuilder
	{
		const FAcousticScene& Scene;
		const FBeamTracerSettings& Settings;
		FBeamTree& Tree;
		TArray<int32> Candidates;
		TArray<FCandidate> Prepared;
		TArray<FVector3d> Polygon3D;
		TArray<FPolygon2d> Pieces;
		TArray<FPolygon2d> NextPieces;
		TArray<FPolygon2d> Outside;
		FPolygon2d Inside;
		TArray<FIntVector> Fan;

		FBeamBuilder(const FAcousticScene& InScene, const FBeamTracerSettings& InSettings, FBeamTree& InTree)
			: Scene(InScene)
			, Settings(InSettings)
			, Tree(InTree)
		{
		}

		bool NearestHitIsPlane(const FPendingBeam& Beam, const FVector3d& Direction, const FAcousticPlane& HitPlane) const
		{
			// A ray through the centre of the piece: it must reach the candidate plane before any other triangle.
			FVector3d Origin = Beam.Apex;
			if (Beam.bHasReflectionPlane)
			{
				FVector3d Crossing;
				double Distance = 0.0;
				if (!FBeamTracer::RayPlaneHit(Beam.Apex, Direction, Beam.ReflectionPlane, Crossing, Distance))
				{
					return true;
				}
				Origin = Crossing;
			}
			FVector3d PlanePoint;
			double PlaneDistance = 0.0;
			if (!FBeamTracer::RayPlaneHit(Origin, Direction, HitPlane, PlanePoint, PlaneDistance))
			{
				return true;
			}
			FRayQueryOptions Options;
			Options.IgnoreTriangle = Beam.ReflectionTriangle;
			Options.TMin = 1e-9;
			Options.TMax = PlaneDistance * (1.0 - 1e-6);
			FRayHit Hit;
			const FAcousticRay Ray(Origin, Direction);
			const bool bHit = Settings.bUseBVH ? Scene.IntersectRay(Ray, Hit, Options) : Scene.IntersectRayBruteForce(Ray, Hit, Options);
			return !bHit;
		}

		void AddNode(const FPendingBeam& Beam, const FBeamFrame& Frame, const FVector2d& P0, const FVector2d& P1, const FVector2d& P2, const FCandidate* Candidate)
		{
			if (IsSliver(P0, P1, P2))
			{
				++Tree.NumSlivers;
				return;
			}
			if (Tree.Nodes.Num() >= Settings.MaxBeamNodes)
			{
				Tree.bTruncated = true;
				return;
			}
			FBeamNode Node;
			Node.Apex = Beam.Apex;
			Node.Directions[0] = Frame.Unproject(P0);
			Node.Directions[1] = Frame.Unproject(P1);
			Node.Directions[2] = Frame.Unproject(P2);
			Node.ComputeSidePlanes();
			Node.ReflectionPlane = Beam.ReflectionPlane;
			Node.bHasReflectionPlane = Beam.bHasReflectionPlane;
			Node.ReflectionTriangle = Beam.ReflectionTriangle;
			Node.Parent = Beam.Parent;
			Node.RootBeam = Beam.RootBeam;
			Node.Order = Beam.Order;
			Node.SequenceHash = Beam.SequenceHash;
			for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
			{
				Node.Factor[Band] = Beam.Factor[Band];
			}
			for (int32 Index = 0; Index < 3; ++Index)
			{
				FVector3d Point;
				double Distance = 0.0;
				if (Beam.bHasReflectionPlane && FBeamTracer::RayPlaneHit(Beam.Apex, Node.Directions[Index], Beam.ReflectionPlane, Point, Distance))
				{
					Node.StartPoints[Index] = Point;
				}
				else
				{
					Node.StartPoints[Index] = Beam.Apex;
				}
				if (Candidate && FBeamTracer::RayPlaneHit(Beam.Apex, Node.Directions[Index], Candidate->HitPlane, Point, Distance))
				{
					Node.EndPoints[Index] = Point;
				}
				else
				{
					Node.EndPoints[Index] = Beam.Apex + Node.Directions[Index] * Tree.EscapeLength;
				}
			}
			if (Candidate)
			{
				Node.HitTriangle = Candidate->Triangle;
				Node.HitPlane = Candidate->HitPlane;
			}
			Tree.Nodes.Add(MoveTemp(Node));
		}

		/** Divides one beam into nodes (SPEC §2.5.3). Returns false when the tree is truncated. */
		bool Divide(const FPendingBeam& Beam)
		{
			const FBeamFrame Frame(Beam.Apex, Beam.Directions[0], Beam.Directions[1], Beam.Directions[2]);
			FPolygon2d BeamPolygon;
			for (const FVector3d& Direction : Beam.Directions)
			{
				FVector2d P;
				if (!Frame.ProjectDirection(Direction, P))
				{
					return true; // degenerate beam (guide ray at 90 degrees from the axis): nothing to divide
				}
				BeamPolygon.Add(P);
			}
			MakeCounterClockwise(BeamPolygon);
			if (IsSliver(BeamPolygon))
			{
				++Tree.NumSlivers;
				return true;
			}

			// Candidate triangles: those inside the frustum, cut to the apex half-space and (for reflected beams) to the
			// room side of the reflecting plane, projected on the reference plane, nearest first.
			const FAcousticFrustum Frustum = FAcousticFrustum::FromTriangularBeam(Beam.Apex, Beam.Directions[0], Beam.Directions[1], Beam.Directions[2]);
			if (Settings.bUseBVH)
			{
				Scene.Bvh.QueryFrustum(Scene.Triangles, Frustum, Candidates);
			}
			else
			{
				FBVH::QueryFrustumBruteForce(Scene.Triangles, Frustum, Candidates);
			}
			Prepared.Reset();
			const FAcousticPlane ApexPlane(Frame.Axis, Beam.Apex);
			for (const int32 TriangleIndex : Candidates)
			{
				if (TriangleIndex == Beam.ReflectionTriangle)
				{
					continue;
				}
				const FAcousticTriangle& Triangle = Scene.Triangles[TriangleIndex];
				Polygon3D.Reset();
				Polygon3D.Add(Triangle.V0);
				Polygon3D.Add(Triangle.V1);
				Polygon3D.Add(Triangle.V2);
				FBeamTracer::ClipPolygon3DByPlane(Polygon3D, ApexPlane, FBeamTracer::ApexClearance);
				if (Beam.bHasReflectionPlane && Polygon3D.Num() >= 3)
				{
					FBeamTracer::ClipPolygon3DByPlane(Polygon3D, Beam.ReflectionPlane, 1e-9);
				}
				// Cut to the beam's own side planes (slightly widened): the part outside the beam can never be hit, and
				// without this cut a vertex near the apex plane projects to coordinates of 1e6 and more, where the rounding
				// of the clipping lines opens slivers between adjacent triangles.
				for (const FAcousticPlane& SidePlane : Frustum.Planes)
				{
					if (Polygon3D.Num() < 3)
					{
						break;
					}
					FBeamTracer::ClipPolygon3DByPlane(Polygon3D, SidePlane, -FBeamTracer::SideClearance);
				}
				if (Polygon3D.Num() < 3)
				{
					continue;
				}
				FCandidate Candidate;
				Candidate.Triangle = TriangleIndex;
				bool bProjected = true;
				for (const FVector3d& Vertex : Polygon3D)
				{
					FVector2d P;
					if (!Frame.ProjectPoint(Vertex, P))
					{
						bProjected = false;
						break;
					}
					Candidate.Projected.Add(P);
				}
				if (!bProjected)
				{
					continue;
				}
				MakeCounterClockwise(Candidate.Projected);
				WeldVertices(Candidate.Projected);
				if (IsSliver(Candidate.Projected))
				{
					continue;
				}
				// Hit plane oriented towards the apex (the side the beam comes from).
				FVector3d Normal = Triangle.N;
				if (Triangle.SignedDistance(Beam.Apex) < 0.0)
				{
					Normal = -Normal;
				}
				Candidate.HitPlane = FAcousticPlane(Normal, Triangle.V0);
				Candidate.Distance = (Triangle.Centroid() - Beam.Apex).Length();
				Prepared.Add(MoveTemp(Candidate));
			}
			Algo::Sort(Prepared, [](const FCandidate& A, const FCandidate& B) { return A.Distance < B.Distance || (A.Distance == B.Distance && A.Triangle < B.Triangle); });

			Pieces.Reset();
			Pieces.Add(BeamPolygon);
			for (int32 Pass = 0; Pass < FBeamTracer::MaxDivisionPasses && Pieces.Num() > 0 && Prepared.Num() > 0; ++Pass)
			{
				bool bClaimed = false;
				for (const FCandidate& Candidate : Prepared)
				{
					NextPieces.Reset();
					for (const FPolygon2d& Piece : Pieces)
					{
						SplitPolygon(Piece, Candidate.Projected, Inside, Outside);
						if (Inside.Num() < 3 || IsSliver(Inside))
						{
							NextPieces.Add(Piece);
							continue;
						}
						if (!NearestHitIsPlane(Beam, Frame.Unproject(Centroid(Inside)), Candidate.HitPlane))
						{
							NextPieces.Add(Piece); // another triangle is nearer in this direction: wait for its turn
							continue;
						}
						bClaimed = true;
						FanTriangulate(Inside.Num(), Fan);
						for (const FIntVector& Triangle : Fan)
						{
							AddNode(Beam, Frame, Inside[Triangle.X], Inside[Triangle.Y], Inside[Triangle.Z], &Candidate);
							if (Tree.bTruncated)
							{
								return false;
							}
						}
						for (FPolygon2d& Rest : Outside)
						{
							if (!IsSliver(Rest))
							{
								NextPieces.Add(MoveTemp(Rest));
							}
							else
							{
								++Tree.NumSlivers;
							}
						}
					}
					Swap(Pieces, NextPieces);
					if (Pieces.Num() == 0)
					{
						break;
					}
				}
				if (!bClaimed)
				{
					break;
				}
			}

			// Leftover pieces: uncovered (open scene) when no candidate covers them, unresolved otherwise.
			for (const FPolygon2d& Piece : Pieces)
			{
				if (IsSliver(Piece))
				{
					++Tree.NumSlivers;
					continue;
				}
				bool bCovered = false;
				bool bSliverOverlap = true;
				const FVector2d Center = Centroid(Piece);
				for (const FCandidate& Candidate : Prepared)
				{
					if (ContainsPoint(Candidate.Projected, Center))
					{
						bCovered = true;
						SplitPolygon(Piece, Candidate.Projected, Inside, Outside);
						bSliverOverlap &= Inside.Num() < 3 || IsSliver(Inside);
					}
				}
				if (bCovered && bSliverOverlap)
				{
					++Tree.NumSlivers; // a marginal piece whose overlap with every covering triangle is below the sliver thresholds
					continue;
				}
				if (bCovered)
				{
					++Tree.NumUnresolved;
					if (Tree.NumUnresolved <= 5)
					{
						const FVector3d Direction = Frame.Unproject(Center);
						FString Covering;
						for (const FCandidate& Candidate : Prepared)
						{
							if (ContainsPoint(Candidate.Projected, Center))
							{
								Covering += FString::Printf(TEXT("%d(%s) "), Candidate.Triangle, NearestHitIsPlane(Beam, Direction, Candidate.HitPlane) ? TEXT("nearest") : TEXT("blocked"));
							}
						}
						UE_LOG(LogAcoustics, Log, TEXT("BeamTracer: unresolved piece, order %d apex (%.4f %.4f %.4f) centre direction (%.6f %.6f %.6f) area %.3e, covering candidates: %s"),
							Beam.Order, Beam.Apex.X, Beam.Apex.Y, Beam.Apex.Z, Direction.X, Direction.Y, Direction.Z, SignedArea(Piece), *Covering);
					}
					continue;
				}
				++Tree.NumEscaped;
				FanTriangulate(Piece.Num(), Fan);
				for (const FIntVector& Triangle : Fan)
				{
					AddNode(Beam, Frame, Piece[Triangle.X], Piece[Triangle.Y], Piece[Triangle.Z], nullptr);
					if (Tree.bTruncated)
					{
						return false;
					}
				}
			}
			return true;
		}
	};
}

TSharedPtr<FBeamTree> FBeamTracer::BuildTree(const FAcousticScene& Scene, const FVector3d& Source, const FBeamTracerSettings& Settings, FSolverProgress* Progress)
{
	SCOPE_CYCLE_COUNTER(STAT_AcousticsBeamTracerBuild);
	TRACE_CPUPROFILER_EVENT_SCOPE(FBeamTracer::BuildTree);

	TSharedPtr<FBeamTree> Tree = MakeShared<FBeamTree>();
	Tree->Source = Source;
	Tree->SubdivisionLevel = Settings.SubdivisionLevel;
	Tree->MaxOrder = Settings.MaxBeamOrder;
	Tree->NumRootBeams = FBeamTracerSettings::NumRootBeams(Settings.SubdivisionLevel);
	Tree->NumNodesPerOrder.Init(0, Settings.MaxBeamOrder + 1);
	const FBox3d Bounds = Scene.ComputeBounds();
	Tree->EscapeLength = Bounds.IsValid ? FMath::Max(10.0, 1.5 * Bounds.GetSize().Length()) : 10.0;

	TArray<FVector3d> Directions;
	EmissionDirections(Settings.SubdivisionLevel, Directions);

	// Reflection sequences are identified by surface (coplanar triangle group), not by triangle: the two halves of a wall
	// give the same image source, and a receiver on their shared edge would otherwise count the reflection twice.
	TArray<FReflectingSurface> Surfaces;
	FImageSourceSolver::BuildSurfaces(Scene, Surfaces);
	TArray<int32> TriangleSurface;
	TriangleSurface.Init(INDEX_NONE, Scene.Triangles.Num());
	for (int32 SurfaceIndex = 0; SurfaceIndex < Surfaces.Num(); ++SurfaceIndex)
	{
		for (const int32 TriangleIndex : Surfaces[SurfaceIndex].Triangles)
		{
			TriangleSurface[TriangleIndex] = SurfaceIndex;
		}
	}

	FBeamBuilder Builder(Scene, Settings, *Tree);
	Tree->Nodes.Reserve(FMath::Min(Settings.MaxBeamNodes, Tree->NumRootBeams * 8));

	// Order 0: the emission beams.
	for (int32 Beam = 0; Beam < Tree->NumRootBeams; ++Beam)
	{
		if (Progress && Progress->IsCancelRequested())
		{
			return Tree;
		}
		FPendingBeam Pending;
		Pending.Apex = Source;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Pending.Directions[Index] = Directions[Beam * 3 + Index];
		}
		for (double& Value : Pending.Factor)
		{
			Value = 1.0;
		}
		Pending.RootBeam = Beam;
		if (!Builder.Divide(Pending))
		{
			break;
		}
	}
	Tree->NumNodesPerOrder[0] = Tree->Nodes.Num();
	if (Progress)
	{
		Progress->Add(1);
	}

	// Reflections, breadth-first: every node with a hit triangle spawns a reflected beam while the order allows.
	int32 LevelStart = 0;
	for (int32 Order = 1; Order <= Settings.MaxBeamOrder && !Tree->bTruncated; ++Order)
	{
		const int32 LevelEnd = Tree->Nodes.Num();
		for (int32 Index = LevelStart; Index < LevelEnd; ++Index)
		{
			if (Progress && Progress->IsCancelRequested())
			{
				return Tree;
			}
			FPendingBeam Pending;
			{
				const FBeamNode& Node = Tree->Nodes[Index];
				if (Node.HitTriangle == INDEX_NONE)
				{
					continue;
				}
				const FAcousticTriangle& Triangle = Scene.Triangles[Node.HitTriangle];
				const FAcousticMaterialProps& Material = Scene.GetMaterialOf(Triangle);
				double MaxFactor = 0.0;
				for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
				{
					Pending.Factor[Band] = Node.Factor[Band] * Material.Reflection(Band);
					MaxFactor = FMath::Max(MaxFactor, Pending.Factor[Band]);
				}
				if (MaxFactor < Settings.EnergyThreshold)
				{
					continue;
				}
				// The reflected beam is valid on the side of the wall the parent came from (HitPlane faces the parent apex).
				Pending.ReflectionPlane = Node.HitPlane;
				Pending.bHasReflectionPlane = true;
				Pending.ReflectionTriangle = Node.HitTriangle;
				Pending.Apex = MirrorPoint(Node.Apex, Node.HitPlane);
				for (int32 I = 0; I < 3; ++I)
				{
					Pending.Directions[I] = ReflectDirection(Node.Directions[I], Node.HitPlane.Normal);
				}
				Pending.Order = Order;
				Pending.Parent = Index;
				Pending.RootBeam = Node.RootBeam;
				Pending.SequenceHash = CombineSequence(Node.SequenceHash, TriangleSurface[Node.HitTriangle]);
			}
			if (!Builder.Divide(Pending))
			{
				break;
			}
		}
		Tree->NumNodesPerOrder[Order] = Tree->Nodes.Num() - LevelEnd;
		LevelStart = LevelEnd;
		if (Progress)
		{
			Progress->Add(1);
		}
	}
	return Tree;
}

void FBeamTracer::QueryReceivers(const FAcousticScene& Scene, const FBeamTree& Tree, TConstArrayView<FSolverReceiver> Receivers, const FBeamTracerSettings& Settings, FSimulationResult& Result)
{
	const double SpeedOfSound = Scene.GetMedium(0).SpeedOfSound;
	Result.Receivers.SetNum(Receivers.Num());
	TArray<FBeamContribution> Contributions;
	for (int32 ReceiverIndex = 0; ReceiverIndex < Receivers.Num(); ++ReceiverIndex)
	{
		const FSolverReceiver& Receiver = Receivers[ReceiverIndex];
		FReceiverResult& Out = Result.Receivers[ReceiverIndex];
		Out.Name = Receiver.Name;
		Out.Position = Receiver.Position;
		Out.Radius = Receiver.Radius;
		Out.Histogram.Init(Settings.BinSeconds, Settings.MaxTimeSeconds);
		Out.EarlyReflections.Reset();
		Out.ContainingBeamNodes.Reset();
		const double Radius2 = Receiver.Radius * Receiver.Radius;

		Tree.Query(Receiver.Position, Contributions);
		for (const FBeamContribution& Contribution : Contributions)
		{
			Out.ContainingBeamNodes.Add(Contribution.Node);
			if (Contribution.Order == 0 && Settings.bAnalyticDirectSound)
			{
				continue; // the direct sound is added analytically below, like the ray tracer does
			}
			const FBeamNode& Node = Tree.Nodes[Contribution.Node];
			const double Distance = Contribution.Distance;
			const double Spread = FMath::Min(1.0, Radius2 / (4.0 * FMath::Max(Distance * Distance, 1e-12)));
			FEarlyReflection Reflection;
			Reflection.Order = Contribution.Order;
			Reflection.Distance = Distance;
			Reflection.TimeSeconds = Distance / SpeedOfSound;
			Reflection.ImageSource = Node.Apex;
			Reflection.BeamNode = Contribution.Node;
			for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
			{
				Reflection.Energy[Band] = Node.Factor[Band] * Spread * (Settings.bAirAbsorption ? FMath::Exp(-Settings.AirAbsorption[Band] * Distance) : 1.0);
			}
			Tree.TracePath(Contribution.Node, Receiver.Position, Reflection.Path, Reflection.Triangles);
			Out.Histogram.AddEnergy(Reflection.TimeSeconds, Reflection.Energy);
			Out.EarlyReflections.Add(MoveTemp(Reflection));
		}
		Out.EarlyReflections.Sort([](const FEarlyReflection& A, const FEarlyReflection& B) { return A.TimeSeconds < B.TimeSeconds; });

		// Direct sound and metrics as in the ray tracer (SPEC §2.2.7).
		Out.DirectDistance = (Out.Position - Tree.Source).Length();
		Out.DirectArrivalSeconds = Out.DirectDistance / SpeedOfSound;
		Out.bDirectOccluded = Scene.IsOccluded(Tree.Source, Out.Position);
		if (Settings.bAnalyticDirectSound && !Out.bDirectOccluded)
		{
			const double Distance2 = FMath::Max(Out.DirectDistance * Out.DirectDistance, 1e-12);
			const double Direct = FMath::Min(1.0, Out.Radius * Out.Radius / (4.0 * Distance2));
			double Deposit[Acoustics::NumBands];
			for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
			{
				Deposit[Band] = Settings.bAirAbsorption ? Direct * FMath::Exp(-Settings.AirAbsorption[Band] * Out.DirectDistance) : Direct;
			}
			if (Out.Histogram.AddEnergy(Out.DirectArrivalSeconds, Deposit))
			{
				Out.DirectEnergy = Deposit[0];
			}
		}
		Out.Metrics = FRoomMetrics::Compute(Out.Histogram, Out.DirectArrivalSeconds);
	}
}

FSimulationResult FBeamTracer::Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> InReceivers, const FBeamTracerSettings& Settings, FSolverProgress* Progress)
{
	SCOPE_CYCLE_COUNTER(STAT_AcousticsBeamTracerRun);
	TRACE_CPUPROFILER_EVENT_SCOPE(FBeamTracer::Run);

	const double StartTime = FPlatformTime::Seconds();
	FSimulationResult Result;
	Result.SolverName = Name();
	Result.SourcePosition = Source;
	Result.NumTriangles = Scene.NumTriangles();
	Result.Timing.NumWorkers = 1;

	if (!Settings.Validate(Result.Error))
	{
		UE_LOG(LogAcoustics, Error, TEXT("BeamTracer: invalid settings: %s"), *Result.Error);
		return Result;
	}
	if (Settings.bUseBVH && !Scene.HasBVH() && Scene.NumTriangles() > 0)
	{
		Result.Error = TEXT("scene has no BVH for its triangles (call BuildBVH) and bUseBVH is set");
		UE_LOG(LogAcoustics, Error, TEXT("BeamTracer: %s"), *Result.Error);
		return Result;
	}

	TArray<FSolverReceiver> Receivers(InReceivers.GetData(), InReceivers.Num());
	for (FSolverReceiver& Receiver : Receivers)
	{
		if (!(Receiver.Radius > 0.0))
		{
			Receiver.Radius = Settings.ReceiverRadius;
		}
	}
	if (Progress)
	{
		Progress->Reset(Settings.MaxBeamOrder + 1 + Receivers.Num());
	}

	// The tree depends on the source and the scene only (SPEC §2.5.7); receivers are queries.
	TSharedPtr<FBeamTree> Tree = BuildTree(Scene, Source, Settings, Progress);
	Result.Timing.SetupSeconds = FPlatformTime::Seconds() - StartTime;
	Result.BeamTree = Tree;
	Result.NumRootBeams = Tree->NumRootBeams;
	Result.NumBeamNodes = Tree->Nodes.Num();
	if (Tree->bTruncated)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("BeamTracer: node budget of %d reached; the tree is incomplete (%s)"), Settings.MaxBeamNodes, *Tree->DescribeStats());
	}
	if (Tree->NumUnresolved > 0)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("BeamTracer: %d beam pieces could not be assigned to a nearest triangle and were dropped"), Tree->NumUnresolved);
	}
	if (Progress && Progress->IsCancelRequested())
	{
		Result.bCancelled = true;
		Result.Timing.TotalSeconds = FPlatformTime::Seconds() - StartTime;
		return Result;
	}

	const double TraceStart = FPlatformTime::Seconds();
	QueryReceivers(Scene, *Tree, Receivers, Settings, Result);
	if (Progress)
	{
		Progress->Add(Receivers.Num());
	}
	Result.Timing.TraceSeconds = FPlatformTime::Seconds() - TraceStart;
	Result.Timing.MetricsSeconds = 0.0;
	Result.Timing.TotalSeconds = FPlatformTime::Seconds() - StartTime;
	Result.bSuccess = true;

	UE_LOG(LogAcoustics, Log, TEXT("BeamTracer: %s"), *Tree->DescribeStats());
	UE_LOG(LogAcoustics, Log, TEXT("%s"), *Result.Describe());
	return Result;
}
