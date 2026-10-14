// M1 geometry tests (SPEC §4): ray-triangle, BVH against brute force on 10k random rays, AABB/frustum queries, shoebox helper.
#include "Geometry/AcousticScene.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AcousticsGeometryTests
{
	static double RandRange(FRandomStream& Rng, double Min, double Max)
	{
		return Min + (Max - Min) * static_cast<double>(Rng.GetFraction());
	}

	static FVector3d RandomPointInBox(FRandomStream& Rng, const FBox3d& Box)
	{
		return FVector3d(RandRange(Rng, Box.Min.X, Box.Max.X), RandRange(Rng, Box.Min.Y, Box.Max.Y), RandRange(Rng, Box.Min.Z, Box.Max.Z));
	}

	static FVector3d RandomUnitVector(FRandomStream& Rng)
	{
		return FVector3d(Rng.VRand()).GetSafeNormal();
	}

	/** Appends Count random triangles (edges up to MaxEdge) inside Region with material 0. */
	static void AddRandomTriangles(FAcousticScene& Scene, FRandomStream& Rng, int32 Count, const FBox3d& Region, double MaxEdge)
	{
		int32 Added = 0;
		while (Added < Count)
		{
			const FVector3d Center = RandomPointInBox(Rng, Region);
			const FVector3d A = Center + RandomUnitVector(Rng) * RandRange(Rng, 0.05, MaxEdge);
			const FVector3d B = Center + RandomUnitVector(Rng) * RandRange(Rng, 0.05, MaxEdge);
			const FVector3d C = Center + RandomUnitVector(Rng) * RandRange(Rng, 0.05, MaxEdge);
			if (Scene.AddTriangle(A, B, C, 0) != INDEX_NONE)
			{
				++Added;
			}
		}
	}

	/** Same hit = same miss/hit state and same distance; coincident hits may legitimately name different triangles. */
	static bool SameHit(const FRayHit& A, const FRayHit& B)
	{
		if (A.IsHit() != B.IsHit())
		{
			return false;
		}
		if (!A.IsHit())
		{
			return true;
		}
		return FMath::IsNearlyEqual(A.T, B.T, 1e-9 * FMath::Max(1.0, A.T));
	}

	static FString DescribeHit(const FRayHit& Hit)
	{
		return Hit.IsHit() ? FString::Printf(TEXT("triangle %d at t = %.12f%s"), Hit.TriangleIndex, Hit.T, Hit.bBackFace ? TEXT(" (back face)") : TEXT("")) : FString(TEXT("miss"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsRayTriangleTest, "AcousticsLab.Core.Geometry.RayTriangle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsRayTriangleTest::RunTest(const FString& Parameters)
{
	using namespace AcousticGeometry;

	const FAcousticTriangle Triangle(FVector3d(0.0, 0.0, 0.0), FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0));
	TestTrue(TEXT("normal of a right-handed triangle in XY is +Z"), Triangle.N.Equals(FVector3d(0.0, 0.0, 1.0), 1e-12));
	TestTrue(TEXT("area is 0.5"), FMath::IsNearlyEqual(Triangle.Area(), 0.5, 1e-12));

	double T = 0.0, U = 0.0, V = 0.0;
	bool bBackFace = false;

	TestTrue(TEXT("hit from the front"), IntersectRayTriangle(FAcousticRay(FVector3d(0.25, 0.25, 2.0), FVector3d(0.0, 0.0, -1.0)), Triangle, T, U, V, bBackFace));
	TestTrue(TEXT("front hit distance"), FMath::IsNearlyEqual(T, 2.0, 1e-12));
	TestTrue(TEXT("front hit barycentrics"), FMath::IsNearlyEqual(U, 0.25, 1e-12) && FMath::IsNearlyEqual(V, 0.25, 1e-12));
	TestFalse(TEXT("front hit is not a back face"), bBackFace);

	TestTrue(TEXT("hit from behind"), IntersectRayTriangle(FAcousticRay(FVector3d(0.25, 0.25, -1.0), FVector3d(0.0, 0.0, 1.0)), Triangle, T, U, V, bBackFace));
	TestTrue(TEXT("back hit is flagged"), bBackFace);
	TestTrue(TEXT("back hit distance"), FMath::IsNearlyEqual(T, 1.0, 1e-12));

	TestFalse(TEXT("miss beyond the hypotenuse"), IntersectRayTriangle(FAcousticRay(FVector3d(0.8, 0.8, 2.0), FVector3d(0.0, 0.0, -1.0)), Triangle, T, U, V, bBackFace));
	TestFalse(TEXT("miss at negative u"), IntersectRayTriangle(FAcousticRay(FVector3d(-0.1, 0.5, 2.0), FVector3d(0.0, 0.0, -1.0)), Triangle, T, U, V, bBackFace));
	TestFalse(TEXT("parallel ray never hits"), IntersectRayTriangle(FAcousticRay(FVector3d(0.25, 0.25, 1.0), FVector3d(1.0, 0.0, 0.0)), Triangle, T, U, V, bBackFace));
	TestFalse(TEXT("ray pointing away"), IntersectRayTriangle(FAcousticRay(FVector3d(0.25, 0.25, 2.0), FVector3d(0.0, 0.0, 1.0)), Triangle, T, U, V, bBackFace));

	TestTrue(TEXT("edge hit counts"), IntersectRayTriangle(FAcousticRay(FVector3d(0.5, 0.0, 2.0), FVector3d(0.0, 0.0, -1.0)), Triangle, T, U, V, bBackFace));
	TestTrue(TEXT("vertex hit counts"), IntersectRayTriangle(FAcousticRay(FVector3d(0.0, 0.0, 2.0), FVector3d(0.0, 0.0, -1.0)), Triangle, T, U, V, bBackFace));

	const FVector3d Target(0.2, 0.3, 0.0);
	const FVector3d Origin(2.0, 3.0, 4.0);
	TestTrue(TEXT("oblique hit"), IntersectRayTriangle(FAcousticRay(Origin, (Target - Origin).GetSafeNormal()), Triangle, T, U, V, bBackFace));
	TestTrue(TEXT("oblique distance"), FMath::IsNearlyEqual(T, (Target - Origin).Length(), 1e-9));
	TestTrue(TEXT("oblique barycentrics"), FMath::IsNearlyEqual(U, 0.2, 1e-9) && FMath::IsNearlyEqual(V, 0.3, 1e-9));

	// Closest hit, ignore list, back-face culling and TMax through the brute-force path.
	FAcousticScene Scene;
	Scene.AddTriangle(FVector3d(-1.0, -1.0, 0.0), FVector3d(3.0, -1.0, 0.0), FVector3d(-1.0, 3.0, 0.0), 0);
	Scene.AddTriangle(FVector3d(-1.0, -1.0, 1.0), FVector3d(3.0, -1.0, 1.0), FVector3d(-1.0, 3.0, 1.0), 0);
	const FAcousticRay Down(FVector3d(0.0, 0.0, 5.0), FVector3d(0.0, 0.0, -1.0));
	FRayHit Hit;
	TestTrue(TEXT("stacked: hit"), Scene.IntersectRayBruteForce(Down, Hit));
	TestEqual(TEXT("stacked: nearest triangle"), Hit.TriangleIndex, 1);
	TestTrue(TEXT("stacked: nearest distance"), FMath::IsNearlyEqual(Hit.T, 4.0, 1e-12));

	FRayQueryOptions Ignore;
	Ignore.IgnoreTriangle = 1;
	TestTrue(TEXT("stacked: ignored triangle is skipped"), Scene.IntersectRayBruteForce(Down, Hit, Ignore) && Hit.TriangleIndex == 0);

	FRayQueryOptions Cull;
	Cull.bCullBackFaces = true;
	TestFalse(TEXT("stacked: back faces culled"), Scene.IntersectRayBruteForce(FAcousticRay(FVector3d(0.0, 0.0, -5.0), FVector3d(0.0, 0.0, 1.0)), Hit, Cull));

	FRayQueryOptions Limited;
	Limited.TMax = 3.0;
	TestFalse(TEXT("stacked: nothing within TMax"), Scene.IntersectRayBruteForce(Down, Hit, Limited));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsBvhMatchesBruteForceTest, "AcousticsLab.Core.Geometry.BvhMatchesBruteForce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsBvhMatchesBruteForceTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsGeometryTests;

	FRandomStream Rng(20261014);
	FAcousticScene Scene = FAcousticScene::MakeShoebox(FVector3d(10.0, 7.0, 3.0));
	const FBox3d Region(FVector3d(-6.0, -5.0, -1.0), FVector3d(6.0, 5.0, 4.0));
	AddRandomTriangles(Scene, Rng, 4000, Region, 1.5);
	Scene.BuildBVH();
	AddInfo(Scene.Bvh.DescribeStats());

	TestTrue(TEXT("BVH built for the scene"), Scene.HasBVH());
	TestEqual(TEXT("BVH covers every triangle"), Scene.Bvh.NumTriangles(), Scene.NumTriangles());

	int32 MinLeaf = TNumericLimits<int32>::Max();
	int32 MaxLeaf = 0;
	for (const FBVH::FNode& Node : Scene.Bvh.GetNodes())
	{
		if (Node.IsLeaf())
		{
			MinLeaf = FMath::Min(MinLeaf, Node.Count);
			MaxLeaf = FMath::Max(MaxLeaf, Node.Count);
		}
	}
	TestTrue(FString::Printf(TEXT("leaves hold 4-8 triangles (min %d, max %d)"), MinLeaf, MaxLeaf), MinLeaf >= 4 && MaxLeaf <= 8);

	TArray<int32> Seen;
	Seen.Init(0, Scene.NumTriangles());
	for (const int32 Index : Scene.Bvh.GetTriangleIndices())
	{
		++Seen[Index];
	}
	TestTrue(TEXT("every triangle sits in exactly one leaf"), !Seen.Contains(0) && !Seen.ContainsByPredicate([](int32 Count) { return Count > 1; }));

	const int32 NumRays = 10000;
	int32 Mismatches = 0;
	int32 Hits = 0;
	for (int32 RayIndex = 0; RayIndex < NumRays; ++RayIndex)
	{
		const FAcousticRay Ray(RandomPointInBox(Rng, Region), RandomUnitVector(Rng));
		FRayQueryOptions Options;
		switch (RayIndex % 4)
		{
		case 1: Options.bCullBackFaces = true; break;
		case 2: Options.TMax = 2.0; break;
		case 3: Options.IgnoreTriangle = RayIndex % Scene.NumTriangles(); break;
		default: break;
		}
		FRayHit BvhHit;
		FRayHit BruteHit;
		Scene.Bvh.IntersectRay(Scene.Triangles, Ray, BvhHit, Options);
		Scene.IntersectRayBruteForce(Ray, BruteHit, Options);
		Hits += BruteHit.IsHit() ? 1 : 0;
		if (!SameHit(BvhHit, BruteHit))
		{
			if (++Mismatches <= 5)
			{
				AddError(FString::Printf(TEXT("ray %d (options %d): BVH %s, brute force %s"), RayIndex, RayIndex % 4, *DescribeHit(BvhHit), *DescribeHit(BruteHit)));
			}
		}
	}
	TestEqual(TEXT("BVH and brute force agree on every ray"), Mismatches, 0);
	TestTrue(FString::Printf(TEXT("most rays hit something (%d of %d)"), Hits, NumRays), Hits > NumRays / 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsBvhQueriesTest, "AcousticsLab.Core.Geometry.BvhQueriesMatchBruteForce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsBvhQueriesTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsGeometryTests;

	FRandomStream Rng(777);
	FAcousticScene Scene = FAcousticScene::MakeShoebox(FVector3d(10.0, 7.0, 3.0));
	const FBox3d Region(FVector3d(-6.0, -5.0, -1.0), FVector3d(6.0, 5.0, 4.0));
	AddRandomTriangles(Scene, Rng, 3000, Region, 1.0);
	Scene.BuildBVH();

	int32 BoxMismatches = 0;
	for (int32 Query = 0; Query < 500; ++Query)
	{
		const FVector3d Center = RandomPointInBox(Rng, Region);
		const FVector3d Extent(RandRange(Rng, 0.1, 3.0), RandRange(Rng, 0.1, 3.0), RandRange(Rng, 0.1, 3.0));
		const FBox3d Box(Center - Extent, Center + Extent);
		TArray<int32> FromBvh;
		TArray<int32> FromBruteForce;
		Scene.Bvh.QueryAABB(Scene.Triangles, Box, FromBvh);
		FBVH::QueryAABBBruteForce(Scene.Triangles, Box, FromBruteForce);
		FromBvh.Sort();
		FromBruteForce.Sort();
		if (FromBvh != FromBruteForce)
		{
			++BoxMismatches;
		}
	}
	TestEqual(TEXT("QueryAABB matches brute force"), BoxMismatches, 0);

	int32 FrustumMismatches = 0;
	int32 NonEmpty = 0;
	int32 ContainmentErrors = 0;
	for (int32 Query = 0; Query < 500; ++Query)
	{
		const FVector3d Apex = RandomPointInBox(Rng, Region);
		const FVector3d Axis = RandomUnitVector(Rng);
		FVector3d Guides[3];
		for (FVector3d& Guide : Guides)
		{
			Guide = (Axis + RandomUnitVector(Rng) * 0.5).GetSafeNormal();
		}
		const FAcousticFrustum Frustum = FAcousticFrustum::FromTriangularBeam(Apex, Guides[0], Guides[1], Guides[2]);
		const FVector3d Sum = Guides[0] + Guides[1] + Guides[2];
		if (!Frustum.ContainsPoint(Apex + Sum * 3.0) || Frustum.ContainsPoint(Apex - Sum * 3.0))
		{
			++ContainmentErrors;
		}
		TArray<int32> FromBvh;
		TArray<int32> FromBruteForce;
		Scene.Bvh.QueryFrustum(Scene.Triangles, Frustum, FromBvh);
		FBVH::QueryFrustumBruteForce(Scene.Triangles, Frustum, FromBruteForce);
		FromBvh.Sort();
		FromBruteForce.Sort();
		if (FromBvh != FromBruteForce)
		{
			++FrustumMismatches;
		}
		NonEmpty += FromBruteForce.Num() > 0 ? 1 : 0;
	}
	TestEqual(TEXT("beam frustum contains its axis and excludes the opposite direction"), ContainmentErrors, 0);
	TestEqual(TEXT("QueryFrustum matches brute force"), FrustumMismatches, 0);
	TestTrue(FString::Printf(TEXT("frustum queries find geometry (%d of 500 non-empty)"), NonEmpty), NonEmpty > 100);

	// A triangle ahead of the apex is found, one behind it is not.
	FAcousticScene Small;
	Small.AddTriangle(FVector3d(2.0, -0.1, -0.1), FVector3d(2.0, 0.1, -0.1), FVector3d(2.0, 0.0, 0.1), 0);
	Small.AddTriangle(FVector3d(-2.0, -0.1, -0.1), FVector3d(-2.0, 0.1, -0.1), FVector3d(-2.0, 0.0, 0.1), 0);
	Small.BuildBVH();
	const FAcousticFrustum Beam = FAcousticFrustum::FromTriangularBeam(FVector3d::ZeroVector,
		FVector3d(1.0, 0.3, 0.3).GetSafeNormal(), FVector3d(1.0, -0.3, 0.3).GetSafeNormal(), FVector3d(1.0, 0.0, -0.3).GetSafeNormal());
	TArray<int32> InBeam;
	Small.Bvh.QueryFrustum(Small.Triangles, Beam, InBeam);
	TestTrue(TEXT("triangle ahead of the beam apex is found"), InBeam.Contains(0));
	TestFalse(TEXT("triangle behind the beam apex is rejected"), InBeam.Contains(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsShoeboxSceneTest, "AcousticsLab.Core.Geometry.ShoeboxScene",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsShoeboxSceneTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsGeometryTests;

	const FAcousticScene Scene = FAcousticScene::MakeShoebox(FVector3d(10.0, 7.0, 3.0));
	TestEqual(TEXT("12 triangles"), Scene.NumTriangles(), 12);
	TestTrue(TEXT("surface 242 m^2"), FMath::IsNearlyEqual(Scene.TotalArea(), 242.0, 1e-9));
	TestTrue(TEXT("BVH built"), Scene.HasBVH());
	TestEqual(TEXT("one material"), Scene.Materials.Num(), 1);
	TestEqual(TEXT("default material is Concrete"), Scene.Materials[0].Name, FString(TEXT("Concrete")));

	const FBox3d Bounds = Scene.ComputeBounds();
	TestTrue(TEXT("bounds"), Bounds.Min.Equals(FVector3d(-5.0, -3.5, 0.0), 1e-9) && Bounds.Max.Equals(FVector3d(5.0, 3.5, 3.0), 1e-9));

	const FVector3d Center(0.0, 0.0, 1.5);
	int32 Outward = 0;
	for (const FAcousticTriangle& Triangle : Scene.Triangles)
	{
		if (FVector3d::DotProduct(Triangle.N, Center - Triangle.Centroid()) <= 0.0)
		{
			++Outward;
		}
	}
	TestEqual(TEXT("every normal points into the room"), Outward, 0);

	struct FAxisRay
	{
		FVector3d Direction;
		double Distance;
	};
	const FAxisRay AxisRays[] = {
		{ FVector3d(1.0, 0.0, 0.0), 5.0 }, { FVector3d(-1.0, 0.0, 0.0), 5.0 },
		{ FVector3d(0.0, 1.0, 0.0), 3.5 }, { FVector3d(0.0, -1.0, 0.0), 3.5 },
		{ FVector3d(0.0, 0.0, 1.0), 1.5 }, { FVector3d(0.0, 0.0, -1.0), 1.5 },
	};
	for (const FAxisRay& AxisRay : AxisRays)
	{
		FRayHit Hit;
		const bool bHit = Scene.IntersectRay(FAcousticRay(Center, AxisRay.Direction), Hit);
		TestTrue(FString::Printf(TEXT("axis ray (%.0f %.0f %.0f) hits the front of a wall at %.1f m"), AxisRay.Direction.X, AxisRay.Direction.Y, AxisRay.Direction.Z, AxisRay.Distance),
			bHit && !Hit.bBackFace && FMath::IsNearlyEqual(Hit.T, AxisRay.Distance, 1e-9));
	}

	FRandomStream Rng(42);
	int32 Bad = 0;
	for (int32 Index = 0; Index < 500; ++Index)
	{
		FRayHit Hit;
		if (!Scene.IntersectRay(FAcousticRay(FVector3d(1.0, -2.0, 1.0), RandomUnitVector(Rng)), Hit) || Hit.bBackFace || Hit.T <= 0.0 || Hit.T > 13.0)
		{
			++Bad;
		}
	}
	TestEqual(TEXT("rays from inside always hit a front face within the room"), Bad, 0);

	TestFalse(TEXT("source to receiver is unoccluded"), Scene.IsOccluded(FVector3d(-3.0, -1.5, 1.5), FVector3d(3.0, 1.5, 1.5)));
	TestTrue(TEXT("a point outside the room is occluded"), Scene.IsOccluded(Center, FVector3d(20.0, 0.0, 1.5)));

	const FAcousticScene Wooden = FAcousticScene::MakeShoebox(FVector3d(4.0, 4.0, 4.0), FAcousticMaterialProps(TEXT("Wood"), 0.1, 0.15));
	TestEqual(TEXT("wooden room material name"), Wooden.GetMaterialOf(Wooden.Triangles[0]).Name, FString(TEXT("Wood")));
	TestTrue(TEXT("wooden room absorption"), FMath::IsNearlyEqual(Wooden.GetMaterialOf(Wooden.Triangles[0]).Absorption[0], 0.1, 1e-12));
	TestTrue(TEXT("cube surface 96 m^2"), FMath::IsNearlyEqual(Wooden.TotalArea(), 96.0, 1e-9));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
