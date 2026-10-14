#include "Geometry/RayIntersection.h"

bool AcousticGeometry::IntersectRayTriangle(const FAcousticRay& Ray, const FAcousticTriangle& Triangle, double& OutT, double& OutU, double& OutV, bool& bOutBackFace)
{
	const FVector3d E1 = Triangle.V1 - Triangle.V0;
	const FVector3d E2 = Triangle.V2 - Triangle.V0;
	const FVector3d P = FVector3d::CrossProduct(Ray.Direction, E2);
	const double Det = FVector3d::DotProduct(E1, P);
	if (FMath::Abs(Det) < ParallelEpsilon)
	{
		return false;
	}

	const double InvDet = 1.0 / Det;
	const FVector3d T = Ray.Origin - Triangle.V0;
	const double U = FVector3d::DotProduct(T, P) * InvDet;
	if (U < 0.0 || U > 1.0)
	{
		return false;
	}

	const FVector3d Q = FVector3d::CrossProduct(T, E1);
	const double V = FVector3d::DotProduct(Ray.Direction, Q) * InvDet;
	if (V < 0.0 || U + V > 1.0)
	{
		return false;
	}

	const double Distance = FVector3d::DotProduct(E2, Q) * InvDet;
	if (Distance < 0.0)
	{
		return false;
	}

	OutT = Distance;
	OutU = U;
	OutV = V;
	// Det = E1 . (D x E2) = -D . (E1 x E2): negative when the ray travels along the front normal, i.e. hits the back face.
	bOutBackFace = Det < 0.0;
	return true;
}

FVector3d AcousticGeometry::SafeInverse(const FVector3d& Direction)
{
	auto Inverse = [](double D) -> double
	{
		if (FMath::Abs(D) < 1e-200)
		{
			return D >= 0.0 ? TNumericLimits<double>::Max() : -TNumericLimits<double>::Max();
		}
		return 1.0 / D;
	};
	return FVector3d(Inverse(Direction.X), Inverse(Direction.Y), Inverse(Direction.Z));
}

bool AcousticGeometry::IntersectRayBox(const FVector3d& Origin, const FVector3d& Direction, const FVector3d& InvDirection, const FBox3d& Box, double TMin, double TMax, double& OutEntry)
{
	double T0 = TMin;
	double T1 = TMax;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (FMath::Abs(Direction[Axis]) < 1e-200)
		{
			// Parallel to this slab: inside it or never.
			if (Origin[Axis] < Box.Min[Axis] || Origin[Axis] > Box.Max[Axis])
			{
				return false;
			}
			continue;
		}
		double TNear = (Box.Min[Axis] - Origin[Axis]) * InvDirection[Axis];
		double TFar = (Box.Max[Axis] - Origin[Axis]) * InvDirection[Axis];
		if (TNear > TFar)
		{
			Swap(TNear, TFar);
		}
		T0 = FMath::Max(T0, TNear);
		T1 = FMath::Min(T1, TFar);
		if (T0 > T1)
		{
			return false;
		}
	}
	OutEntry = T0;
	return true;
}

bool AcousticGeometry::IntersectRayBruteForce(TConstArrayView<FAcousticTriangle> Triangles, const FAcousticRay& Ray, FRayHit& OutHit, const FRayQueryOptions& Options)
{
	OutHit.Reset();
	double BestT = Options.TMax;
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		if (Index == Options.IgnoreTriangle)
		{
			continue;
		}
		double T, U, V;
		bool bBackFace;
		if (!IntersectRayTriangle(Ray, Triangles[Index], T, U, V, bBackFace))
		{
			continue;
		}
		if (T < Options.TMin || T >= BestT || (Options.bCullBackFaces && bBackFace))
		{
			continue;
		}
		BestT = T;
		OutHit.T = T;
		OutHit.TriangleIndex = Index;
		OutHit.U = U;
		OutHit.V = V;
		OutHit.bBackFace = bBackFace;
	}
	return OutHit.IsHit();
}
