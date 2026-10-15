#include "Solvers/BeamTracer.h"
#include "Solvers/BeamTreeBuild.h"
#include "Solvers/ImageSourceSolver.h"
#include "Solvers/ParallelBeamTracer.h"

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
	// A converging beam occupies the nappe between its start triangle and the focus: the mirror image of that nappe
	// through the apex is the cone along Directions, so the point is tested mirrored.
	const FVector3d Tested = bFocusAhead ? Apex * 2.0 - Point : Point;
	if (!ConeContainsPoint(Tested, Tolerance))
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

void FBeamTree::TracePath(int32 NodeIndex, const FVector3d& Point, TArray<FVector3d>& OutPath, TArray<int32>& OutTriangles, TArray<int32>* OutSegmentMedia) const
{
	OutPath.Reset();
	OutTriangles.Reset();
	if (OutSegmentMedia)
	{
		OutSegmentMedia->Reset();
	}
	if (!Nodes.IsValidIndex(NodeIndex))
	{
		return;
	}
	// Back from the point: the line through the point and the apex of every node crosses the plane the beam entered
	// through (behind the point for a diverging beam, in front of it towards the focus for a converging one). The
	// continuation of a converging beam shares its apex with its parent, so the line keeps the previous direction there.
	TArray<FVector3d, TInlineAllocator<8>> Reverse;
	TArray<int32, TInlineAllocator<8>> ReverseTriangles;
	TArray<int32, TInlineAllocator<8>> ReverseMedia;
	Reverse.Add(Point);
	FVector3d Current = Point;
	FVector3d PreviousDirection = FVector3d::ZeroVector;
	int32 LastMedium = SourceMedium;
	int32 Index = NodeIndex;
	while (Index != INDEX_NONE)
	{
		const FBeamNode& Node = Nodes[Index];
		LastMedium = Node.Medium;
		if (Node.bHasReflectionPlane)
		{
			FVector3d LineDirection = Node.Apex - Current;
			if (LineDirection.SquaredLength() < 1e-24)
			{
				LineDirection = PreviousDirection.SquaredLength() > 1e-24 ? PreviousDirection : -Node.Axis();
			}
			const double Denominator = FVector3d::DotProduct(LineDirection, Node.ReflectionPlane.Normal);
			const double DistanceCurrent = Node.ReflectionPlane.SignedDistance(Current);
			double S = FMath::Abs(Denominator) > 1e-300 ? -DistanceCurrent / Denominator : 0.0;
			const double MaxStep = 4.0 * EscapeLength / FMath::Max(LineDirection.Length(), 1e-300);
			S = FMath::Clamp(S, -MaxStep, MaxStep);
			Current = Current + LineDirection * S;
			PreviousDirection = LineDirection;
			if (!Node.bContinuation)
			{
				Reverse.Add(Current);
				ReverseTriangles.Add(Node.ReflectionTriangle);
				ReverseMedia.Add(Node.Medium);
			}
		}
		Index = Node.Parent;
	}
	Reverse.Add(Source);
	ReverseMedia.Add(LastMedium);
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
	if (OutSegmentMedia)
	{
		OutSegmentMedia->Reserve(ReverseMedia.Num());
		for (int32 I = ReverseMedia.Num() - 1; I >= 0; --I)
		{
			OutSegmentMedia->Add(ReverseMedia[I]);
		}
		check(OutSegmentMedia->Num() == OutPath.Num() - 1);
	}
}

double FBeamTree::PathDelaySeconds(const FAcousticScene& Scene, TConstArrayView<FVector3d> Path, TConstArrayView<int32> SegmentMedia)
{
	double Delay = 0.0;
	for (int32 Segment = 0; Segment + 1 < Path.Num(); ++Segment)
	{
		const int32 Medium = SegmentMedia.IsValidIndex(Segment) ? SegmentMedia[Segment] : 0;
		Delay += (Path[Segment + 1] - Path[Segment]).Length() / Scene.GetMedium(Medium).SpeedOfSound;
	}
	return Delay;
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
		if (A.Medium != B.Medium || A.NumRefractions != B.NumRefractions || A.bRefracted != B.bRefracted || A.bFocusAhead != B.bFocusAhead || A.bContinuation != B.bContinuation || A.bTotalInternalReflection != B.bTotalInternalReflection)
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
	FString Refraction;
	if (NumRefracted > 0 || NumTotalInternalReflections > 0)
	{
		Refraction = FString::Printf(TEXT(", %d refracted, %d total internal reflections, %d continued beyond the focus, %d parallel apexes"), NumRefracted, NumTotalInternalReflections, NumContinuations, NumParallelApexes);
	}
	const FString Emitted = NumRootBeamsEmitted != NumRootBeams ? FString::Printf(TEXT(" (%d emitted in the cone)"), NumRootBeamsEmitted) : FString();
	return FString::Printf(TEXT("%d root beams (level %d)%s, %d nodes by order %s, %d escaped, %d unresolved, %d slivers dropped%s%s"),
		NumRootBeams, SubdivisionLevel, *Emitted, Nodes.Num(), *PerOrder, NumEscaped, NumUnresolved, NumSlivers, *Refraction, bTruncated ? TEXT(", TRUNCATED by the node budget") : TEXT(""));
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

bool FBeamTracer::RefractDirection(const FVector3d& Direction, const FVector3d& Normal, double C1, double C2, FVector3d& OutDirection, double MaxSinRefracted)
{
	// Snell with the normal pointing into the incident medium: cos(theta1) = -D . N.
	FVector3d N = Normal;
	double CosIn = -FVector3d::DotProduct(Direction, N);
	if (CosIn < 0.0)
	{
		N = -N;
		CosIn = -CosIn;
	}
	CosIn = FMath::Min(CosIn, 1.0);
	const double Eta = C1 > 0.0 ? C2 / C1 : 1.0;
	const double SinIn2 = FMath::Max(0.0, 1.0 - CosIn * CosIn);
	const double SinOut2 = Eta * Eta * SinIn2;
	if (MaxSinRefracted < 1.0 && SinOut2 > MaxSinRefracted * MaxSinRefracted)
	{
		// Beyond the clamp: the largest allowed refraction angle in the plane of incidence.
		const FVector3d Tangent = (Direction + N * CosIn).GetSafeNormal();
		const double CosOut = FMath::Sqrt(1.0 - MaxSinRefracted * MaxSinRefracted);
		OutDirection = (Tangent * MaxSinRefracted - N * CosOut).GetSafeNormal();
		return true;
	}
	if (SinOut2 > 1.0)
	{
		return false; // total internal reflection
	}
	const double CosOut = FMath::Sqrt(1.0 - SinOut2);
	// Tangential component scaled by eta, normal component rebuilt from cos(theta2), pointing away from the incident side.
	OutDirection = (Direction * Eta + N * (Eta * CosIn - CosOut)).GetSafeNormal();
	return true;
}

bool FBeamTracer::LeastSquaresApex(TConstArrayView<FVector3d> Origins, TConstArrayView<FVector3d> Directions, FVector3d& OutPoint, double FallbackDistance)
{
	// Minimize sum_i |(X - O_i) - ((X - O_i) . D_i) D_i|^2: A X = B with A = sum (I - D D^T), B = sum (I - D D^T) O.
	const int32 Num = FMath::Min(Origins.Num(), Directions.Num());
	double A[3][3] = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } };
	FVector3d B = FVector3d::ZeroVector;
	FVector3d MeanOrigin = FVector3d::ZeroVector;
	FVector3d MeanDirection = FVector3d::ZeroVector;
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FVector3d D = Directions[Index].GetSafeNormal();
		const FVector3d& O = Origins[Index];
		const double Dc[3] = { D.X, D.Y, D.Z };
		const double Oc[3] = { O.X, O.Y, O.Z };
		const double OdotD = FVector3d::DotProduct(O, D);
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Col = 0; Col < 3; ++Col)
			{
				A[Row][Col] += (Row == Col ? 1.0 : 0.0) - Dc[Row] * Dc[Col];
			}
		}
		B += O - D * OdotD;
		MeanOrigin += O;
		MeanDirection += D;
	}
	if (Num > 0)
	{
		MeanOrigin /= Num;
	}
	MeanDirection = MeanDirection.GetSafeNormal();

	const double Det =
		A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) -
		A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
		A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
	// A is singular exactly when every line is parallel; the eigenvalue along a nearly common direction is
	// 1 - cos^2 of the spread, so a determinant below this means directions within about 1e-6 rad of each other.
	if (Num < 2 || !(FMath::Abs(Det) > 1e-12))
	{
		OutPoint = MeanOrigin - MeanDirection * FallbackDistance;
		return false;
	}
	const double Inv[3][3] = {
		{ (A[1][1] * A[2][2] - A[1][2] * A[2][1]) / Det, (A[0][2] * A[2][1] - A[0][1] * A[2][2]) / Det, (A[0][1] * A[1][2] - A[0][2] * A[1][1]) / Det },
		{ (A[1][2] * A[2][0] - A[1][0] * A[2][2]) / Det, (A[0][0] * A[2][2] - A[0][2] * A[2][0]) / Det, (A[0][2] * A[1][0] - A[0][0] * A[1][2]) / Det },
		{ (A[1][0] * A[2][1] - A[1][1] * A[2][0]) / Det, (A[0][1] * A[2][0] - A[0][0] * A[2][1]) / Det, (A[0][0] * A[1][1] - A[0][1] * A[1][0]) / Det }
	};
	const double Bc[3] = { B.X, B.Y, B.Z };
	double X[3];
	for (int32 Row = 0; Row < 3; ++Row)
	{
		X[Row] = Inv[Row][0] * Bc[0] + Inv[Row][1] * Bc[1] + Inv[Row][2] * Bc[2];
	}
	OutPoint = FVector3d(X[0], X[1], X[2]);
	return true;
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

	/** A beam waiting to be divided. Directions are propagation directions; for a converging beam (bFocusAhead) they point away from the entry triangle towards the focus and beyond. */
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
		int32 Medium = 0;
		int32 NumRefractions = 0;
		bool bRefracted = false;
		bool bFocusAhead = false;
		bool bContinuation = false;

		/** +1 for a diverging beam, -1 for a converging one: the geometry (cone, projection) uses Sign * Directions. */
		double Sign() const { return bFocusAhead ? -1.0 : 1.0; }
	};

	struct FCandidate
	{
		int32 Triangle = INDEX_NONE;
		double Distance = 0.0;
		FPolygon2d Projected;
		FAcousticPlane HitPlane;
	};

	uint64 CombineSequence(uint64 ParentHash, int32 Triangle, bool bRefraction = false)
	{
		// Reflection at a surface and refraction through it are different events of the same surface.
		const int64 Event = bRefraction ? -static_cast<int64>(Triangle) - 2 : static_cast<int64>(Triangle) + 1;
		uint64 Hash = ParentHash ^ (static_cast<uint64>(Event) * 0x9E3779B97F4A7C15ull);
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
		TArray<FPolygon2d> Claimed;
		TArray<FPolygon2d> NextClaimed;
		TArray<FPolygon2d> OutsideOther;
		FPolygon2d InsideOther;
		TArray<FIntVector> Fan;

		/** Shared state of the build (memory guard, log limiter); null for a standalone builder. */
		const FBeamBuildContext* Context = nullptr;

		FBeamBuilder(const FAcousticScene& InScene, const FBeamTracerSettings& InSettings, FBeamTree& InTree, const FBeamBuildContext* InContext = nullptr)
			: Scene(InScene)
			, Settings(InSettings)
			, Tree(InTree)
			, Context(InContext)
		{
		}

		/** Continuations of converging beams beyond their focus, divided right after the beam that produced them. */
		TArray<FPendingBeam> Continuations;

		/**
		 * A ray through the centre of the piece: it must reach the candidate plane before any other triangle. GeometryDirection
		 * is the direction from the apex (towards the entry plane for a converging beam); the ray itself travels along the
		 * propagation direction from the point where the beam entered.
		 */
		bool NearestHitIsPlane(const FPendingBeam& Beam, const FVector3d& GeometryDirection, const FAcousticPlane& HitPlane) const
		{
			FVector3d Origin;
			FVector3d Propagation;
			if (!PropagationRay(Beam, GeometryDirection, Origin, Propagation))
			{
				return true;
			}
			FVector3d PlanePoint;
			double PlaneDistance = 0.0;
			if (!FBeamTracer::RayPlaneHit(Origin, Propagation, HitPlane, PlanePoint, PlaneDistance))
			{
				return true;
			}
			FRayQueryOptions Options;
			Options.IgnoreTriangle = Beam.ReflectionTriangle;
			Options.TMin = 1e-9;
			Options.TMax = PlaneDistance * (1.0 - 1e-6);
			FRayHit Hit;
			const FAcousticRay Ray(Origin, Propagation);
			const bool bHit = Settings.bUseBVH ? Scene.IntersectRay(Ray, Hit, Options) : Scene.IntersectRayBruteForce(Ray, Hit, Options);
			return !bHit;
		}

		/**
		 * The ray the beam travels along in a geometry direction: from the apex for a root beam, from the crossing of the
		 * entry plane otherwise, along the propagation direction. False when the direction misses the entry plane.
		 */
		bool PropagationRay(const FPendingBeam& Beam, const FVector3d& GeometryDirection, FVector3d& OutOrigin, FVector3d& OutDirection) const
		{
			OutDirection = GeometryDirection * Beam.Sign();
			OutOrigin = Beam.Apex;
			if (Beam.bHasReflectionPlane)
			{
				FVector3d Crossing;
				double Distance = 0.0;
				if (FBeamTracer::RayPlaneHit(Beam.Apex, GeometryDirection, Beam.ReflectionPlane, Crossing, Distance))
				{
					OutOrigin = Crossing;
				}
				else if (!Beam.bContinuation)
				{
					return false;
				}
			}
			return true;
		}

		/** Along the beam ray in a direction where both candidates overlap: true when Other is hit before Candidate. */
		bool OtherIsNearer(const FPendingBeam& Beam, const FVector3d& GeometryDirection, const FAcousticPlane& CandidatePlane, const FAcousticPlane& OtherPlane) const
		{
			FVector3d Origin;
			FVector3d Propagation;
			if (!PropagationRay(Beam, GeometryDirection, Origin, Propagation))
			{
				return false;
			}
			FVector3d Point;
			double CandidateDistance = 0.0;
			double OtherDistance = 0.0;
			if (!FBeamTracer::RayPlaneHit(Origin, Propagation, CandidatePlane, Point, CandidateDistance) || !FBeamTracer::RayPlaneHit(Origin, Propagation, OtherPlane, Point, OtherDistance))
			{
				return false;
			}
			return OtherDistance < CandidateDistance * (1.0 - 1e-9);
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
			if (Context && Context->NodeCount.fetch_add(1, std::memory_order_relaxed) >= Context->NodeCap)
			{
				// Memory guard of a parallel build (all subtrees together): cut wherever the workers are.
				Context->bOverflow.store(true, std::memory_order_relaxed);
				Tree.bTruncated = true;
				return;
			}
			const double Sign = Beam.Sign();
			const FVector3d Geometry[3] = { Frame.Unproject(P0), Frame.Unproject(P1), Frame.Unproject(P2) };
			FBeamNode Node;
			Node.Apex = Beam.Apex;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Node.Directions[Index] = Geometry[Index] * Sign;
			}
			Node.ComputeSidePlanes();
			Node.ReflectionPlane = Beam.ReflectionPlane;
			Node.bHasReflectionPlane = Beam.bHasReflectionPlane;
			Node.ReflectionTriangle = Beam.ReflectionTriangle;
			Node.Parent = Beam.Parent;
			Node.RootBeam = Beam.RootBeam;
			Node.Order = Beam.Order;
			Node.SequenceHash = Beam.SequenceHash;
			Node.Medium = Beam.Medium;
			Node.NumRefractions = Beam.NumRefractions;
			Node.bRefracted = Beam.bRefracted;
			Node.bFocusAhead = Beam.bFocusAhead;
			Node.bContinuation = Beam.bContinuation;
			for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
			{
				Node.Factor[Band] = Beam.Factor[Band];
			}
			for (int32 Index = 0; Index < 3; ++Index)
			{
				FVector3d Point;
				double Distance = 0.0;
				if (Beam.bHasReflectionPlane && FBeamTracer::RayPlaneHit(Beam.Apex, Geometry[Index], Beam.ReflectionPlane, Point, Distance))
				{
					Node.StartPoints[Index] = Point;
				}
				else
				{
					Node.StartPoints[Index] = Beam.Apex;
				}
				if (Candidate && FBeamTracer::RayPlaneHit(Beam.Apex, Geometry[Index], Candidate->HitPlane, Point, Distance))
				{
					Node.EndPoints[Index] = Point;
				}
				else if (Beam.bFocusAhead)
				{
					Node.EndPoints[Index] = Beam.Apex; // a converging beam that hits nothing narrows down to its focus
				}
				else
				{
					Node.EndPoints[Index] = Beam.Apex + Geometry[Index] * Tree.EscapeLength;
				}
			}
			if (Candidate)
			{
				Node.HitTriangle = Candidate->Triangle;
				Node.HitPlane = Candidate->HitPlane;
			}
			else if (Beam.bFocusAhead)
			{
				// Beyond the focus the beam diverges again: a continuation node of the same order, entered through the focal plane.
				FPendingBeam Continuation;
				Continuation.Apex = Beam.Apex;
				for (int32 Index = 0; Index < 3; ++Index)
				{
					Continuation.Directions[Index] = Node.Directions[Index];
				}
				Continuation.Order = Beam.Order;
				for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
				{
					Continuation.Factor[Band] = Beam.Factor[Band];
				}
				Continuation.ReflectionPlane = FAcousticPlane(Node.Axis(), Beam.Apex);
				Continuation.bHasReflectionPlane = true;
				Continuation.ReflectionTriangle = INDEX_NONE;
				Continuation.Parent = Tree.Nodes.Num();
				Continuation.RootBeam = Beam.RootBeam;
				Continuation.SequenceHash = CombineSequence(Beam.SequenceHash, INDEX_NONE, true);
				Continuation.Medium = Beam.Medium;
				Continuation.NumRefractions = Beam.NumRefractions;
				Continuation.bContinuation = true;
				Continuations.Add(MoveTemp(Continuation));
				++Tree.NumContinuations;
			}
			Tree.Nodes.Add(MoveTemp(Node));
		}

		/** Divides one beam and the continuations it spawns (SPEC §2.5.3). Returns false when the tree is truncated. */
		bool Divide(const FPendingBeam& Beam)
		{
			Continuations.Reset();
			if (!DivideOne(Beam))
			{
				return false;
			}
			// A continuation is diverging and never spawns another one; the array is consumed in order.
			for (int32 Index = 0; Index < Continuations.Num(); ++Index)
			{
				const FPendingBeam Continuation = Continuations[Index];
				if (!DivideOne(Continuation))
				{
					return false;
				}
			}
			return true;
		}

		bool DivideOne(const FPendingBeam& Beam)
		{
			// Geometry directions: from the apex towards the entry triangle; for a diverging beam these are the propagation
			// directions, for a converging one (focus ahead) their opposites.
			const double Sign = Beam.Sign();
			const FVector3d Geometry[3] = { Beam.Directions[0] * Sign, Beam.Directions[1] * Sign, Beam.Directions[2] * Sign };
			const FBeamFrame Frame(Beam.Apex, Geometry[0], Geometry[1], Geometry[2]);
			FPolygon2d BeamPolygon;
			for (const FVector3d& Direction : Geometry)
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
			// room side of the reflecting plane, projected on the reference plane, nearest first along the propagation
			// (nearest to the apex for a diverging beam, farthest from it for a converging one).
			FVector3d EntryCentroid = Beam.Apex;
			if (Beam.bHasReflectionPlane)
			{
				FVector3d Sum = FVector3d::ZeroVector;
				int32 Count = 0;
				for (const FVector3d& Direction : Geometry)
				{
					FVector3d Point;
					double Distance = 0.0;
					if (FBeamTracer::RayPlaneHit(Beam.Apex, Direction, Beam.ReflectionPlane, Point, Distance))
					{
						Sum += Point;
						++Count;
					}
				}
				if (Count > 0)
				{
					EntryCentroid = Sum / Count;
				}
			}
			const FAcousticFrustum Frustum = FAcousticFrustum::FromTriangularBeam(Beam.Apex, Geometry[0], Geometry[1], Geometry[2]);
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
				// Hit plane oriented towards the side the beam comes from: the apex for a diverging beam, the entry
				// triangle for a converging one (whose apex lies beyond the triangles it hits).
				FVector3d Normal = Triangle.N;
				if (Triangle.SignedDistance(Beam.bFocusAhead ? EntryCentroid : Beam.Apex) < 0.0)
				{
					Normal = -Normal;
				}
				Candidate.HitPlane = FAcousticPlane(Normal, Triangle.V0);
				Candidate.Distance = (Triangle.Centroid() - Beam.Apex).Length() * Sign;
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
						// Hidden surfaces: the sort by centroid distance is only a heuristic (a large triangle sorted later may
						// still lie nearer over part of this overlap), so every other candidate that overlaps the piece and is
						// nearer there (non-intersecting triangles keep one depth order over their whole overlap) cuts its
						// part out; those parts return to the remainder and are claimed when that candidate's turn comes.
						Claimed.Reset();
						Claimed.Add(Inside);
						for (const FCandidate& Other : Prepared)
						{
							if (&Other == &Candidate)
							{
								continue;
							}
							NextClaimed.Reset();
							for (const FPolygon2d& Poly : Claimed)
							{
								SplitPolygon(Poly, Other.Projected, InsideOther, OutsideOther);
								if (InsideOther.Num() >= 3 && !IsSliver(InsideOther) && OtherIsNearer(Beam, Frame.Unproject(Centroid(InsideOther)), Candidate.HitPlane, Other.HitPlane))
								{
									NextPieces.Add(InsideOther);
									for (FPolygon2d& Rest : OutsideOther)
									{
										if (!IsSliver(Rest))
										{
											NextClaimed.Add(MoveTemp(Rest));
										}
										else
										{
											++Tree.NumSlivers;
										}
									}
								}
								else
								{
									NextClaimed.Add(Poly);
								}
							}
							Swap(Claimed, NextClaimed);
						}
						for (const FPolygon2d& Poly : Claimed)
						{
							if (!NearestHitIsPlane(Beam, Frame.Unproject(Centroid(Poly)), Candidate.HitPlane))
							{
								NextPieces.Add(Poly); // a triangle outside the candidates (or dropped as a sliver) is nearer: wait
								continue;
							}
							bClaimed = true;
							FanTriangulate(Poly.Num(), Fan);
							for (const FIntVector& Triangle : Fan)
							{
								AddNode(Beam, Frame, Poly[Triangle.X], Poly[Triangle.Y], Poly[Triangle.Z], &Candidate);
								if (Tree.bTruncated)
								{
									return false;
								}
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
					const bool bLog = Context ? Context->UnresolvedLogs.fetch_add(1, std::memory_order_relaxed) < 5 : Tree.NumUnresolved <= 5;
					if (bLog)
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


void AcousticsBeamBuild::InitContext(FBeamBuildContext& Context, const FAcousticScene& Scene, const FVector3d& Source, const FBeamTracerSettings& Settings, FSolverProgress* Progress)
{
	Context.Scene = &Scene;
	Context.Settings = &Settings;
	Context.Progress = Progress;
	Context.Source = Source;
	Context.SourceMedium = Scene.Media.IsValidIndex(Settings.SourceMedium) ? Settings.SourceMedium : 0;
	Context.NumRootBeams = FBeamTracerSettings::NumRootBeams(Settings.SubdivisionLevel);
	const FBox3d Bounds = Scene.ComputeBounds();
	Context.EscapeLength = Bounds.IsValid ? FMath::Max(10.0, 1.5 * Bounds.GetSize().Length()) : 10.0;
	Context.bRefraction = Settings.bRefraction && Scene.HasInterfaces();
	Context.NodeCap = 2 * static_cast<int64>(Settings.MaxBeamNodes);
	Context.NodeCount.store(0, std::memory_order_relaxed);
	Context.bOverflow.store(false, std::memory_order_relaxed);
	Context.UnresolvedLogs.store(0, std::memory_order_relaxed);

	FBeamTracer::EmissionDirections(Settings.SubdivisionLevel, Context.Directions);

	// Reflection sequences are identified by surface (coplanar triangle group), not by triangle: the two halves of a wall
	// give the same image source, and a receiver on their shared edge would otherwise count the reflection twice.
	TArray<FReflectingSurface> Surfaces;
	FImageSourceSolver::BuildSurfaces(Scene, Surfaces);
	Context.TriangleSurface.Init(INDEX_NONE, Scene.Triangles.Num());
	for (int32 SurfaceIndex = 0; SurfaceIndex < Surfaces.Num(); ++SurfaceIndex)
	{
		for (const int32 TriangleIndex : Surfaces[SurfaceIndex].Triangles)
		{
			Context.TriangleSurface[TriangleIndex] = SurfaceIndex;
		}
	}

	// Order 0: the emission beams (only those inside the cone of a directional source).
	const FVector3d ConeAxis = Settings.SourceDirection.GetSafeNormal();
	const double ConeCos = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Settings.SourceConeHalfAngleDeg, 0.0, 180.0)));
	Context.EmittedRoots.Reset(Context.NumRootBeams);
	for (int32 Beam = 0; Beam < Context.NumRootBeams; ++Beam)
	{
		if (Settings.bDirectionalSource && !ConeAxis.IsNearlyZero())
		{
			const FVector3d Centre = (Context.Directions[Beam * 3] + Context.Directions[Beam * 3 + 1] + Context.Directions[Beam * 3 + 2]).GetSafeNormal();
			if (FVector3d::DotProduct(Centre, ConeAxis) < ConeCos)
			{
				continue;
			}
		}
		Context.EmittedRoots.Add(Beam);
	}
}

void AcousticsBeamBuild::BuildRootSubtree(const FBeamBuildContext& Context, int32 Slot, FBeamTree& Tree)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AcousticsBeamBuild::BuildRootSubtree);
	const FAcousticScene& Scene = *Context.Scene;
	const FBeamTracerSettings& Settings = *Context.Settings;
	FSolverProgress* Progress = Context.Progress;
	const int32 Root = Context.EmittedRoots[Slot];

	Tree = FBeamTree();
	Tree.Source = Context.Source;
	Tree.SourceMedium = Context.SourceMedium;
	Tree.SubdivisionLevel = Settings.SubdivisionLevel;
	Tree.MaxOrder = Settings.MaxBeamOrder;
	Tree.NumRootBeams = Context.NumRootBeams;
	Tree.NumRootBeamsEmitted = 1;
	Tree.EscapeLength = Context.EscapeLength;
	Tree.NumNodesPerOrder.Init(0, Settings.MaxBeamOrder + 1);
	if (Progress && Progress->IsCancelRequested())
	{
		return;
	}

	FBeamBuilder Builder(Scene, Settings, Tree, &Context);
	FPendingBeam Pending;
	Pending.Apex = Context.Source;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Pending.Directions[Index] = Context.Directions[Root * 3 + Index];
	}
	for (double& Value : Pending.Factor)
	{
		Value = 1.0;
	}
	Pending.RootBeam = Root;
	Pending.Medium = Context.SourceMedium;
	Builder.Divide(Pending);
	Tree.NumNodesPerOrder[0] = Tree.Nodes.Num();

	// Reflections, breadth-first within the subtree: every node with a hit triangle spawns a reflected beam (and a
	// refracted one at an interface) while the order allows.
	const bool bRefraction = Context.bRefraction;
	int32 LevelStart = 0;
	for (int32 Order = 1; Order <= Settings.MaxBeamOrder && !Tree.bTruncated; ++Order)
	{
		const int32 LevelEnd = Tree.Nodes.Num();
		for (int32 Index = LevelStart; Index < LevelEnd; ++Index)
		{
			if (Progress && Progress->IsCancelRequested())
			{
				return;
			}
			FPendingBeam Reflected;
			FPendingBeam Refracted;
			bool bHasReflected = false;
			bool bHasRefracted = false;
			{
				const FBeamNode& Node = Tree.Nodes[Index];
				if (Node.HitTriangle == INDEX_NONE)
				{
					continue;
				}
				const FAcousticTriangle& Triangle = Scene.Triangles[Node.HitTriangle];
				const FAcousticMaterialProps& Material = Scene.GetMaterialOf(Triangle);
				const bool bInterface = bRefraction && Triangle.bInterface && Triangle.MediumFront != Triangle.MediumBack;

				// Energy split at the surface: by the material (1 - alpha) for a wall, by the impedances for an interface
				// (SPEC §2.6: R for the reflected beam, T = 1 - R for the refracted one; everything for the reflected beam
				// beyond the critical angle).
				double ReflectionFactor[Acoustics::NumBands];
				double TransmissionFactor = 0.0;
				int32 OtherMedium = Node.Medium;
				FVector3d RefractedDirections[3];
				if (bInterface)
				{
					OtherMedium = Triangle.MediumFront == Node.Medium ? Triangle.MediumBack : Triangle.MediumFront;
					if (Triangle.MediumFront != Node.Medium && Triangle.MediumBack != Node.Medium)
					{
						// The beam believes it is in a medium the triangle does not border (inconsistent scene): decide by side.
						const FVector3d EntryCentre = (Node.EndPoints[0] + Node.EndPoints[1] + Node.EndPoints[2]) / 3.0 - Node.Axis() * 1e-6;
						OtherMedium = Triangle.SignedDistance(EntryCentre) > 0.0 ? Triangle.MediumBack : Triangle.MediumFront;
					}
					const FMedium& From = Scene.GetMedium(Node.Medium);
					const FMedium& To = Scene.GetMedium(OtherMedium);
					// Total internal reflection is decided for the beam by its axis (Sikora 2012, sec. 3.3.4: the incoming angle of
					// the divided beam); guide rays of a refracting beam that lie beyond the critical angle are clamped to grazing.
					FVector3d RefractedAxis;
					const bool bTotalInternal = !FBeamTracer::RefractDirection(Node.Axis(), Node.HitPlane.Normal, From.SpeedOfSound, To.SpeedOfSound, RefractedAxis);
					for (int32 I = 0; I < 3 && !bTotalInternal; ++I)
					{
						FBeamTracer::RefractDirection(Node.Directions[I], Node.HitPlane.Normal, From.SpeedOfSound, To.SpeedOfSound, RefractedDirections[I], FBeamTracer::MaxGuideRaySinRefracted);
					}
					const double R = bTotalInternal ? 1.0 : FMedium::ReflectionFactor(From, To);
					TransmissionFactor = bTotalInternal ? 0.0 : 1.0 - R;
					for (double& Value : ReflectionFactor)
					{
						Value = R;
					}
					if (bTotalInternal)
					{
						++Tree.NumTotalInternalReflections;
						Tree.Nodes[Index].bTotalInternalReflection = true;
					}
				}
				else
				{
					for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
					{
						ReflectionFactor[Band] = Material.Reflection(Band) * (Settings.bScatteringLoss ? 1.0 - Material.Scattering[Band] : 1.0);
					}
				}

				// The reflected beam is valid on the side of the surface the parent came from (HitPlane faces that side).
				double MaxFactor = 0.0;
				for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
				{
					Reflected.Factor[Band] = Node.Factor[Band] * ReflectionFactor[Band];
					MaxFactor = FMath::Max(MaxFactor, Reflected.Factor[Band]);
				}
				if (MaxFactor >= Settings.EnergyThreshold)
				{
					bHasReflected = true;
					Reflected.ReflectionPlane = Node.HitPlane;
					Reflected.bHasReflectionPlane = true;
					Reflected.ReflectionTriangle = Node.HitTriangle;
					Reflected.Apex = FBeamTracer::MirrorPoint(Node.Apex, Node.HitPlane);
					for (int32 I = 0; I < 3; ++I)
					{
						Reflected.Directions[I] = FBeamTracer::ReflectDirection(Node.Directions[I], Node.HitPlane.Normal);
					}
					Reflected.Order = Order;
					Reflected.Parent = Index;
					Reflected.RootBeam = Node.RootBeam;
					Reflected.SequenceHash = CombineSequence(Node.SequenceHash, Context.TriangleSurface[Node.HitTriangle]);
					Reflected.Medium = Node.Medium;
					Reflected.NumRefractions = Node.NumRefractions;
					Reflected.bFocusAhead = Reflected.ReflectionPlane.SignedDistance(Reflected.Apex) > FBeamTracer::MinApexClearance;
				}

				// The refracted beam (SPEC §2.6): guide rays by Snell from the parent's cross-section on the interface,
				// apex by least squares, valid beyond the interface on the other side. The factor carries T and the
				// ratio (r1 / r_in)^2 of the apex distances to the start triangle (Sikora 2012 Eq. 6), so the energy
				// density at the start triangle equals the incoming density times T whatever the new apex is.
				if (bInterface && TransmissionFactor > 0.0)
				{
					const FVector3d Centre = (Node.EndPoints[0] + Node.EndPoints[1] + Node.EndPoints[2]) / 3.0;
					FVector3d Apex;
					const bool bConverged = FBeamTracer::LeastSquaresApex(MakeArrayView(Node.EndPoints, 3), MakeArrayView(RefractedDirections, 3), Apex);
					const FAcousticPlane EntryPlane(-Node.HitPlane.Normal, Centre);
					double ApexDistance = EntryPlane.SignedDistance(Apex);
					if (!bConverged || FMath::Abs(ApexDistance) < FBeamTracer::MinApexClearance)
					{
						// Parallel guide rays (or an apex in the entry plane): a beam from far away.
						const FVector3d Mean = (RefractedDirections[0] + RefractedDirections[1] + RefractedDirections[2]).GetSafeNormal();
						Apex = Centre - Mean * FBeamTracer::ParallelApexDistance;
						ApexDistance = EntryPlane.SignedDistance(Apex);
						++Tree.NumParallelApexes;
					}
					const double RIn = (Node.Apex - Centre).Length();
					const double R1 = (Apex - Centre).Length();
					const double Spread = RIn > 1e-12 ? (R1 * R1) / (RIn * RIn) : 1.0;
					MaxFactor = 0.0;
					for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
					{
						Refracted.Factor[Band] = Node.Factor[Band] * TransmissionFactor * Spread;
						MaxFactor = FMath::Max(MaxFactor, Refracted.Factor[Band]);
					}
					// The threshold compares energy densities at the start triangle, where the spread ratio is already folded in;
					// test the physical transmitted share instead so a far apex (large r1) cannot keep a dead beam alive.
					double TransmittedShare = 0.0;
					for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
					{
						TransmittedShare = FMath::Max(TransmittedShare, Node.Factor[Band] * TransmissionFactor);
					}
					if (TransmittedShare >= Settings.EnergyThreshold && MaxFactor > 0.0)
					{
						bHasRefracted = true;
						Refracted.Apex = Apex;
						for (int32 I = 0; I < 3; ++I)
						{
							Refracted.Directions[I] = RefractedDirections[I];
						}
						Refracted.ReflectionPlane = EntryPlane;
						Refracted.bHasReflectionPlane = true;
						Refracted.ReflectionTriangle = Node.HitTriangle;
						Refracted.Order = Order;
						Refracted.Parent = Index;
						Refracted.RootBeam = Node.RootBeam;
						Refracted.SequenceHash = CombineSequence(Node.SequenceHash, Context.TriangleSurface[Node.HitTriangle], true);
						Refracted.Medium = OtherMedium;
						Refracted.NumRefractions = Node.NumRefractions + 1;
						Refracted.bRefracted = true;
						Refracted.bFocusAhead = ApexDistance > 0.0;
						++Tree.NumRefracted;
					}
				}
			}
			if (bHasReflected && !Builder.Divide(Reflected))
			{
				break;
			}
			if (bHasRefracted && !Builder.Divide(Refracted))
			{
				break;
			}
		}
		Tree.NumNodesPerOrder[Order] = Tree.Nodes.Num() - LevelEnd;
		LevelStart = LevelEnd;
	}
}

TSharedPtr<FBeamTree> AcousticsBeamBuild::MergeSubtrees(const FBeamBuildContext& Context, TArray<FBeamTree>& Subtrees)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AcousticsBeamBuild::MergeSubtrees);
	const FBeamTracerSettings& Settings = *Context.Settings;
	TSharedPtr<FBeamTree> Tree = MakeShared<FBeamTree>();
	Tree->Source = Context.Source;
	Tree->SourceMedium = Context.SourceMedium;
	Tree->SubdivisionLevel = Settings.SubdivisionLevel;
	Tree->MaxOrder = Settings.MaxBeamOrder;
	Tree->NumRootBeams = Context.NumRootBeams;
	Tree->NumRootBeamsEmitted = Context.EmittedRoots.Num();
	Tree->EscapeLength = Context.EscapeLength;
	Tree->NumNodesPerOrder.Init(0, Settings.MaxBeamOrder + 1);

	// Counters (integers: their sum does not depend on the order).
	int64 Total = 0;
	for (const FBeamTree& Subtree : Subtrees)
	{
		Total += Subtree.Nodes.Num();
		Tree->NumEscaped += Subtree.NumEscaped;
		Tree->NumUnresolved += Subtree.NumUnresolved;
		Tree->NumSlivers += Subtree.NumSlivers;
		Tree->NumRefracted += Subtree.NumRefracted;
		Tree->NumTotalInternalReflections += Subtree.NumTotalInternalReflections;
		Tree->NumContinuations += Subtree.NumContinuations;
		Tree->NumParallelApexes += Subtree.NumParallelApexes;
		Tree->bTruncated |= Subtree.bTruncated;
	}
	const bool bOverflow = Context.bOverflow.load(std::memory_order_relaxed);
	const int32 Kept = static_cast<int32>(FMath::Min<int64>(Total, Settings.MaxBeamNodes));
	Tree->bTruncated |= Total > Settings.MaxBeamNodes || bOverflow;
	Tree->Nodes.Reserve(Kept);

	// Breadth-first: order by order, subtree by subtree (root order), each subtree with its nodes of that order in their
	// own order. A subtree is breadth-first itself, so its nodes of one order are contiguous and one cursor per subtree
	// suffices; a parent always precedes its children, so its global index is known when a child is appended.
	TArray<int32> Cursors;
	Cursors.Init(0, Subtrees.Num());
	TArray<TArray<int32>> LocalToGlobal;
	LocalToGlobal.SetNum(Subtrees.Num());
	for (int32 Slot = 0; Slot < Subtrees.Num(); ++Slot)
	{
		LocalToGlobal[Slot].Init(INDEX_NONE, Subtrees[Slot].Nodes.Num());
	}
	for (int32 Order = 0; Order <= Settings.MaxBeamOrder && Tree->Nodes.Num() < Kept; ++Order)
	{
		for (int32 Slot = 0; Slot < Subtrees.Num() && Tree->Nodes.Num() < Kept; ++Slot)
		{
			const TArray<FBeamNode>& Local = Subtrees[Slot].Nodes;
			int32& Cursor = Cursors[Slot];
			while (Cursor < Local.Num() && Local[Cursor].Order == Order && Tree->Nodes.Num() < Kept)
			{
				FBeamNode Node = Local[Cursor];
				if (Node.Parent != INDEX_NONE)
				{
					Node.Parent = LocalToGlobal[Slot][Node.Parent];
					ensureMsgf(Node.Parent != INDEX_NONE, TEXT("beam subtree %d: the parent of node %d was not merged before it"), Slot, Cursor);
				}
				LocalToGlobal[Slot][Cursor] = Tree->Nodes.Num();
				Tree->Nodes.Add(MoveTemp(Node));
				++Tree->NumNodesPerOrder[Order];
				++Cursor;
			}
		}
	}
	if (bOverflow)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("BeamTracer: the subtrees together exceeded %lld nodes; the build stopped wherever the workers were, so this truncated tree depends on the scheduling"), Context.NodeCap);
	}
	return Tree;
}

void AcousticsBeamBuild::QueryReceiver(const FAcousticScene& Scene, const FBeamTree& Tree, const FSolverReceiver& Receiver, const FBeamTracerSettings& Settings, FReceiverResult& Out, TArray<FBeamContribution>& Contributions, TArray<int32>& SegmentMedia)
{
	const double SpeedOfSound = Scene.GetMedium(Tree.SourceMedium).SpeedOfSound;
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
		Reflection.ImageSource = Node.Apex;
		Reflection.BeamNode = Contribution.Node;
		for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
		{
			Reflection.Energy[Band] = Node.Factor[Band] * Spread * (Settings.bAirAbsorption ? FMath::Exp(-Settings.AirAbsorption[Band] * Distance) : 1.0);
		}
		// Delay along the actual path with the speed of sound of every medium (SPEC §2.6); without refraction the path
		// length equals the apex distance (mirror images) and the delay is d / c.
		Tree.TracePath(Contribution.Node, Receiver.Position, Reflection.Path, Reflection.Triangles, &SegmentMedia);
		Reflection.TimeSeconds = Node.NumRefractions > 0 ? FBeamTree::PathDelaySeconds(Scene, Reflection.Path, SegmentMedia) : Distance / SpeedOfSound;
		Out.Histogram.AddEnergy(Reflection.TimeSeconds, Reflection.Energy);
		Out.EarlyReflections.Add(MoveTemp(Reflection));
	}
	Out.EarlyReflections.Sort([](const FEarlyReflection& A, const FEarlyReflection& B) { return A.TimeSeconds < B.TimeSeconds; });

	// Direct sound and metrics as in the ray tracer (SPEC §2.2.7); the occlusion test follows bUseBVH like every other query.
	const FVector3d ConeAxis = Settings.SourceDirection.GetSafeNormal();
	const double ConeCos = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Settings.SourceConeHalfAngleDeg, 0.0, 180.0)));
	Out.DirectDistance = (Out.Position - Tree.Source).Length();
	Out.DirectArrivalSeconds = Out.DirectDistance / SpeedOfSound;
	Out.bDirectOccluded = Scene.IsOccluded(Tree.Source, Out.Position, INDEX_NONE, Settings.bUseBVH);
	if (Settings.bDirectionalSource && !ConeAxis.IsNearlyZero() && FVector3d::DotProduct((Out.Position - Tree.Source).GetSafeNormal(), ConeAxis) < ConeCos)
	{
		Out.bDirectOccluded = true; // outside the cone of a directional source
	}
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

TSharedPtr<FBeamTree> FBeamTracer::BuildTree(const FAcousticScene& Scene, const FVector3d& Source, const FBeamTracerSettings& Settings, FSolverProgress* Progress)
{
	SCOPE_CYCLE_COUNTER(STAT_AcousticsBeamTracerBuild);
	FParallelBeamTracer Tracer(Settings.NumThreads, false);
	return Tracer.BuildTree(Scene, Source, Settings, Progress);
}

void FBeamTracer::QueryReceivers(const FAcousticScene& Scene, const FBeamTree& Tree, TConstArrayView<FSolverReceiver> Receivers, const FBeamTracerSettings& Settings, FSimulationResult& Result)
{
	FParallelBeamTracer Tracer(1, false);
	Tracer.QueryReceivers(Scene, Tree, Receivers, Settings, Result);
}

FSimulationResult FBeamTracer::Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> Receivers, const FBeamTracerSettings& Settings, FSolverProgress* Progress)
{
	SCOPE_CYCLE_COUNTER(STAT_AcousticsBeamTracerRun);
	TRACE_CPUPROFILER_EVENT_SCOPE(FBeamTracer::Run);
	// One thread runs inline on the caller; more threads (0 = all logical cores) use a dedicated pool (M7, FParallelBeamTracer).
	FParallelBeamTracer Tracer(Settings.NumThreads, false);
	return Tracer.Run(Scene, Source, Receivers, Settings, Progress);
}
