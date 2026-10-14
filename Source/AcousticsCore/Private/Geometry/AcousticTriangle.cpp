#include "Geometry/AcousticTriangle.h"

FAcousticTriangle::FAcousticTriangle(const FVector3d& InV0, const FVector3d& InV1, const FVector3d& InV2, int32 InMaterialId)
	: V0(InV0)
	, V1(InV1)
	, V2(InV2)
	, MaterialId(InMaterialId)
{
	ComputeNormal();
}

bool FAcousticTriangle::ComputeNormal()
{
	const FVector3d Cross = FVector3d::CrossProduct(V1 - V0, V2 - V0);
	const double Length = Cross.Length();
	if (Length <= 1e-18)
	{
		return false;
	}
	N = Cross / Length;
	return true;
}

double FAcousticTriangle::Area() const
{
	return 0.5 * FVector3d::CrossProduct(V1 - V0, V2 - V0).Length();
}

FVector3d FAcousticTriangle::Centroid() const
{
	return (V0 + V1 + V2) / 3.0;
}

FBox3d FAcousticTriangle::Bounds() const
{
	FBox3d Box(ForceInit);
	Box += V0;
	Box += V1;
	Box += V2;
	return Box;
}
