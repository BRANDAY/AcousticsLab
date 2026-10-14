#include "Geometry/Frustum.h"

FAcousticPlane::FAcousticPlane(const FVector3d& InNormal, const FVector3d& PointOnPlane)
	: Normal(InNormal.GetSafeNormal())
	, Offset(FVector3d::DotProduct(Normal, PointOnPlane))
{
}

bool FAcousticPlane::IsBoxOutside(const FBox3d& Box) const
{
	// The box corner farthest along the normal; if even that is behind the plane, the box is outside.
	const FVector3d PVertex(
		Normal.X >= 0.0 ? Box.Max.X : Box.Min.X,
		Normal.Y >= 0.0 ? Box.Max.Y : Box.Min.Y,
		Normal.Z >= 0.0 ? Box.Max.Z : Box.Min.Z);
	return SignedDistance(PVertex) < 0.0;
}

FAcousticFrustum FAcousticFrustum::FromTriangularBeam(const FVector3d& Apex, const FVector3d& D0, const FVector3d& D1, const FVector3d& D2)
{
	FAcousticFrustum Frustum;
	const FVector3d Directions[3] = { D0, D1, D2 };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FVector3d& A = Directions[Index];
		const FVector3d& B = Directions[(Index + 1) % 3];
		const FVector3d& C = Directions[(Index + 2) % 3];
		FVector3d Normal = FVector3d::CrossProduct(A, B);
		const double Length = Normal.Length();
		if (Length <= 1e-15)
		{
			continue; // degenerate edge: no plane, the query stays conservative
		}
		Normal /= Length;
		if (FVector3d::DotProduct(Normal, C) < 0.0)
		{
			Normal = -Normal;
		}
		Frustum.Planes.Add(FAcousticPlane(Normal, Apex));
	}

	const FVector3d Axis = D0 + D1 + D2;
	if (Axis.Length() > 1e-15)
	{
		Frustum.Planes.Add(FAcousticPlane(Axis.GetSafeNormal(), Apex));
	}
	return Frustum;
}

bool FAcousticFrustum::ContainsPoint(const FVector3d& P, double Tolerance) const
{
	for (const FAcousticPlane& Plane : Planes)
	{
		if (Plane.SignedDistance(P) < -Tolerance)
		{
			return false;
		}
	}
	return true;
}

bool FAcousticFrustum::IntersectsBox(const FBox3d& Box) const
{
	for (const FAcousticPlane& Plane : Planes)
	{
		if (Plane.IsBoxOutside(Box))
		{
			return false;
		}
	}
	return true;
}
