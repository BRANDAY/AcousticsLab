#include "Geometry/AcousticScene.h"

FAcousticScene::FAcousticScene()
{
	Reset();
}

void FAcousticScene::Reset()
{
	Triangles.Reset();
	Materials.Reset();
	Media.Reset();
	Bvh.Reset();
	Materials.Add(FAcousticMaterialProps());
	Media.Add(FMedium::Air());
}

int32 FAcousticScene::AddMaterial(const FAcousticMaterialProps& Props)
{
	return Materials.Add(Props);
}

int32 FAcousticScene::FindMaterial(const FString& Name) const
{
	for (int32 Index = 0; Index < Materials.Num(); ++Index)
	{
		if (Materials[Index].Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FAcousticScene::FindOrAddMaterial(const FAcousticMaterialProps& Props)
{
	const int32 Existing = FindMaterial(Props.Name);
	return Existing != INDEX_NONE ? Existing : AddMaterial(Props);
}

int32 FAcousticScene::AddMedium(const FMedium& Medium)
{
	return Media.Add(Medium);
}

int32 FAcousticScene::FindMedium(const FString& Name) const
{
	for (int32 Index = 0; Index < Media.Num(); ++Index)
	{
		if (Media[Index].Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FAcousticScene::FindOrAddMedium(const FMedium& Medium)
{
	const int32 Existing = FindMedium(Medium.Name);
	return Existing != INDEX_NONE ? Existing : AddMedium(Medium);
}

bool FAcousticScene::HasInterfaces() const
{
	for (const FAcousticTriangle& Triangle : Triangles)
	{
		if (Triangle.bInterface && Triangle.MediumFront != Triangle.MediumBack)
		{
			return true;
		}
	}
	return false;
}

int32 FAcousticScene::AddTriangle(FAcousticTriangle Triangle)
{
	if (!Triangle.ComputeNormal() || Triangle.IsDegenerate(MinTriangleArea))
	{
		return INDEX_NONE;
	}
	return Triangles.Add(Triangle);
}

int32 FAcousticScene::AddTriangle(const FVector3d& A, const FVector3d& B, const FVector3d& C, int32 MaterialId)
{
	return AddTriangle(FAcousticTriangle(A, B, C, MaterialId));
}

void FAcousticScene::AddQuad(const FVector3d& A, const FVector3d& B, const FVector3d& C, const FVector3d& D, int32 MaterialId)
{
	AddTriangle(A, B, C, MaterialId);
	AddTriangle(A, C, D, MaterialId);
}

void FAcousticScene::BuildBVH(int32 MaxLeafSize)
{
	Bvh.Build(Triangles, MaxLeafSize);
}

bool FAcousticScene::IntersectRay(const FAcousticRay& Ray, FRayHit& OutHit, const FRayQueryOptions& Options) const
{
	if (HasBVH())
	{
		return Bvh.IntersectRay(Triangles, Ray, OutHit, Options);
	}
	return IntersectRayBruteForce(Ray, OutHit, Options);
}

bool FAcousticScene::IntersectRayBruteForce(const FAcousticRay& Ray, FRayHit& OutHit, const FRayQueryOptions& Options) const
{
	return AcousticGeometry::IntersectRayBruteForce(Triangles, Ray, OutHit, Options);
}

bool FAcousticScene::IsOccluded(const FVector3d& From, const FVector3d& To, int32 IgnoreTriangle, bool bAllowBVH) const
{
	const FVector3d Delta = To - From;
	const double Distance = Delta.Length();
	if (Distance <= FRayQueryOptions::DefaultTMin)
	{
		return false;
	}
	FRayQueryOptions Options;
	Options.TMax = Distance - FRayQueryOptions::DefaultTMin;
	Options.IgnoreTriangle = IgnoreTriangle;
	FRayHit Hit;
	const FAcousticRay Ray(From, Delta / Distance);
	return bAllowBVH ? IntersectRay(Ray, Hit, Options) : IntersectRayBruteForce(Ray, Hit, Options);
}

double FAcousticScene::TotalArea() const
{
	double Area = 0.0;
	for (const FAcousticTriangle& Triangle : Triangles)
	{
		Area += Triangle.Area();
	}
	return Area;
}

FBox3d FAcousticScene::ComputeBounds() const
{
	FBox3d Bounds(ForceInit);
	for (const FAcousticTriangle& Triangle : Triangles)
	{
		Bounds += Triangle.Bounds();
	}
	return Bounds;
}

const FAcousticMaterialProps& FAcousticScene::GetMaterial(int32 MaterialId) const
{
	static const FAcousticMaterialProps Fallback;
	if (Materials.IsValidIndex(MaterialId))
	{
		return Materials[MaterialId];
	}
	return Materials.Num() > 0 ? Materials[0] : Fallback;
}

const FMedium& FAcousticScene::GetMedium(int32 MediumId) const
{
	static const FMedium Fallback = FMedium::Air();
	if (Media.IsValidIndex(MediumId))
	{
		return Media[MediumId];
	}
	return Media.Num() > 0 ? Media[0] : Fallback;
}

FString FAcousticScene::DescribeStats() const
{
	const FBox3d Bounds = ComputeBounds();
	int32 NumInterfaces = 0;
	for (const FAcousticTriangle& Triangle : Triangles)
	{
		NumInterfaces += Triangle.bInterface ? 1 : 0;
	}
	FString Result = FString::Printf(TEXT("Scene: %d triangles (%d interface), %.3f m^2, %d materials, %d media, bounds (%.2f %.2f %.2f)-(%.2f %.2f %.2f) m; %s"),
		Triangles.Num(), NumInterfaces, TotalArea(), Materials.Num(), Media.Num(),
		Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z, Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z,
		HasBVH() ? *Bvh.DescribeStats() : TEXT("no BVH"));
	for (int32 Index = 0; Index < Media.Num(); ++Index)
	{
		Result += FString::Printf(TEXT("\n  medium %d \"%s\": c %.0f m/s, rho %.0f kg/m^3, Z %.3g"), Index, *Media[Index].Name, Media[Index].SpeedOfSound, Media[Index].Density, Media[Index].Impedance());
	}
	for (int32 Index = 0; Index < Materials.Num(); ++Index)
	{
		int32 Count = 0;
		double Area = 0.0;
		for (const FAcousticTriangle& Triangle : Triangles)
		{
			if (Triangle.MaterialId == Index)
			{
				++Count;
				Area += Triangle.Area();
			}
		}
		const FAcousticMaterialProps& Material = Materials[Index];
		Result += FString::Printf(TEXT("\n  material %d \"%s\": alpha %.3f, s %.3f%s, %d triangles, %.3f m^2"),
			Index, *Material.Name, Material.MeanAbsorption(), Material.MeanScattering(), Material.bInterface ? TEXT(", interface") : TEXT(""), Count, Area);
	}
	return Result;
}

FAcousticScene FAcousticScene::MakeShoebox(const FVector3d& SizeMeters, const FAcousticMaterialProps& WallMaterial)
{
	FAcousticScene Scene;
	const int32 Material = Scene.FindOrAddMaterial(WallMaterial);
	if (Material == 0)
	{
		Scene.Materials[0] = WallMaterial; // the default slot takes the requested coefficients
	}

	const double X0 = -0.5 * SizeMeters.X;
	const double X1 = 0.5 * SizeMeters.X;
	const double Y0 = -0.5 * SizeMeters.Y;
	const double Y1 = 0.5 * SizeMeters.Y;
	const double Z0 = 0.0;
	const double Z1 = SizeMeters.Z;

	// Each quad winds so that (B - A) x (C - A) points into the room.
	Scene.AddQuad(FVector3d(X0, Y0, Z0), FVector3d(X1, Y0, Z0), FVector3d(X1, Y1, Z0), FVector3d(X0, Y1, Z0), Material); // floor, +Z
	Scene.AddQuad(FVector3d(X0, Y0, Z1), FVector3d(X0, Y1, Z1), FVector3d(X1, Y1, Z1), FVector3d(X1, Y0, Z1), Material); // ceiling, -Z
	Scene.AddQuad(FVector3d(X0, Y0, Z0), FVector3d(X0, Y1, Z0), FVector3d(X0, Y1, Z1), FVector3d(X0, Y0, Z1), Material); // wall x = X0, +X
	Scene.AddQuad(FVector3d(X1, Y0, Z0), FVector3d(X1, Y0, Z1), FVector3d(X1, Y1, Z1), FVector3d(X1, Y1, Z0), Material); // wall x = X1, -X
	Scene.AddQuad(FVector3d(X0, Y0, Z0), FVector3d(X0, Y0, Z1), FVector3d(X1, Y0, Z1), FVector3d(X1, Y0, Z0), Material); // wall y = Y0, +Y
	Scene.AddQuad(FVector3d(X0, Y1, Z0), FVector3d(X1, Y1, Z0), FVector3d(X1, Y1, Z1), FVector3d(X0, Y1, Z1), Material); // wall y = Y1, -Y

	Scene.BuildBVH();
	return Scene;
}

FAcousticScene FAcousticScene::Tessellated(int32 Divisions) const
{
	FAcousticScene Result;
	Result.Materials = Materials;
	Result.Media = Media;
	const int32 N = FMath::Max(1, Divisions);
	Result.Triangles.Reserve(Triangles.Num() * N * N);
	for (const FAcousticTriangle& Triangle : Triangles)
	{
		// Grid P(i, j) = V0 + i/N (V1 - V0) + j/N (V2 - V0), i + j <= N; both sub-triangle kinds keep the winding of (V0, V1, V2).
		const FVector3d EdgeI = (Triangle.V1 - Triangle.V0) / N;
		const FVector3d EdgeJ = (Triangle.V2 - Triangle.V0) / N;
		auto Point = [&](int32 I, int32 J) { return Triangle.V0 + EdgeI * I + EdgeJ * J; };
		auto Add = [&](const FVector3d& A, const FVector3d& B, const FVector3d& C)
		{
			FAcousticTriangle Piece = Triangle;
			Piece.V0 = A;
			Piece.V1 = B;
			Piece.V2 = C;
			Result.AddTriangle(Piece);
		};
		for (int32 I = 0; I < N; ++I)
		{
			for (int32 J = 0; I + J < N; ++J)
			{
				Add(Point(I, J), Point(I + 1, J), Point(I, J + 1));
				if (I + J < N - 1)
				{
					Add(Point(I + 1, J), Point(I + 1, J + 1), Point(I, J + 1));
				}
			}
		}
	}
	Result.BuildBVH();
	return Result;
}
