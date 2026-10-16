// M10: the amphitheatre builder (AmphitheatreGeometry / AAmphitheatreActor) and the soundwalk cell search.
// Lives in the game module: the builder is part of the level code (GeometryCore meshes, actor, extractor).
#include "Audio/SoundwalkComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Geometry/AcousticScene.h"
#include "Levels/AmphitheatreActor.h"
#include "Levels/AmphitheatrePositionsAsset.h"
#include "Levels/LevelSetupActor.h"
#include "Misc/AutomationTest.h"
#include "Scene/AcousticSceneExtractorComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AcousticsAmphitheatreTests
{
	constexpr int32 Segments = 96;

	/** UE front normal of a mesh triangle ((C - A) x (B - A), the convention of the builder), length = 2 * area, cm^2. */
	FVector3d FrontNormal(const UE::Geometry::FDynamicMesh3& Mesh, int32 Triangle)
	{
		const UE::Geometry::FIndex3i T = Mesh.GetTriangle(Triangle);
		const FVector3d A = Mesh.GetVertex(T.A);
		const FVector3d B = Mesh.GetVertex(T.B);
		const FVector3d C = Mesh.GetVertex(T.C);
		return (C - A).Cross(B - A);
	}

	/** Signed volume of a closed mesh with outward front normals, m^3 (divergence theorem, sum of A . N / 6). */
	double SignedVolumeM3(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		double Volume = 0.0;
		for (const int32 Triangle : Mesh.TriangleIndicesItr())
		{
			Volume += Mesh.GetVertex(Mesh.GetTriangle(Triangle).A).Dot(FrontNormal(Mesh, Triangle)) / 6.0;
		}
		return Volume * 1e-6;
	}

	double AreaM2(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		double Area = 0.0;
		for (const int32 Triangle : Mesh.TriangleIndicesItr())
		{
			Area += 0.5 * FrontNormal(Mesh, Triangle).Length();
		}
		return Area * 1e-4;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsAmphitheatreCaveaTest, "AcousticsLab.Core.Amphitheatre.CaveaMesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsAmphitheatreCaveaTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsAmphitheatreTests;
	const FAmphitheatreDimensions Dims; // SPEC §3.4: arena 65 x 40 m, 20 rows of 0.8 x 0.75 m
	UE::Geometry::FDynamicMesh3 Cavea;
	AmphitheatreGeometry::BuildCaveaMesh(Dims, Segments, Cavea);

	// Profile: ground corner + 2 per row + back-wall foot = 2 * rows + 2 corners, one quad (2 triangles) per corner and segment.
	TestEqual(TEXT("cavea triangles = segments x (2 rows + 2) x 2"), Cavea.TriangleCount(), Segments * (2 * Dims.Rows + 2) * 2);
	TestEqual(TEXT("cavea vertices = segments x (2 rows + 2)"), Cavea.VertexCount(), Segments * (2 * Dims.Rows + 2));
	TestTrue(TEXT("cavea is a closed shell (every edge has two triangles)"), Cavea.IsClosed());

	const UE::Geometry::FAxisAlignedBox3d Bounds = Cavea.GetBounds();
	TestTrue(FString::Printf(TEXT("height 0 .. 15 m (got %.3f .. %.3f)"), Bounds.Min.Z / 100.0, Bounds.Max.Z / 100.0),
		FMath::IsNearlyZero(Bounds.Min.Z, 1e-6) && FMath::IsNearlyEqual(Bounds.Max.Z / 100.0, Dims.TopHeight(), 1e-9));
	TestTrue(FString::Printf(TEXT("outer semi-axes 48.5 x 36 m (got %.3f x %.3f)"), Bounds.Max.X / 100.0, Bounds.Max.Y / 100.0),
		FMath::IsNearlyEqual(Bounds.Max.X / 100.0, Dims.A + Dims.OuterOffset(), 1e-6) && FMath::IsNearlyEqual(Bounds.Max.Y / 100.0, Dims.B + Dims.OuterOffset(), 1e-3));

	// Volume of the stepped solid on the polygon rings: row k is a slab of height rise between offset (k-1) d and the back wall.
	double Expected = 0.0;
	for (int32 Row = 1; Row <= Dims.Rows; ++Row)
	{
		Expected += Dims.Rise * (AmphitheatreGeometry::PolygonArea(Dims, Segments, Dims.OuterOffset()) - AmphitheatreGeometry::PolygonArea(Dims, Segments, (Row - 1) * Dims.Depth));
	}
	const double Volume = SignedVolumeM3(Cavea);
	TestTrue(FString::Printf(TEXT("volume %.1f m^3 = stepped rings %.1f m^3 (positive: front normals point outward)"), Volume, Expected),
		Expected > 0.0 && FMath::Abs(Volume - Expected) <= 1e-6 * Expected);

	// Faces: treads up (one ring per row), ground face down, risers / back wall horizontal.
	double UpArea = 0.0, DownArea = 0.0, SideArea = 0.0;
	int32 Tilted = 0;
	for (const int32 Triangle : Cavea.TriangleIndicesItr())
	{
		const FVector3d N = FrontNormal(Cavea, Triangle);
		const double Area = 0.5 * N.Length() * 1e-4;
		const FVector3d Unit = N.GetSafeNormal();
		if (Unit.Z > 1.0 - 1e-9)
		{
			UpArea += Area;
		}
		else if (Unit.Z < -1.0 + 1e-9)
		{
			DownArea += Area;
		}
		else if (FMath::Abs(Unit.Z) < 1e-9)
		{
			SideArea += Area;
		}
		else
		{
			++Tilted;
		}
	}
	const double Ring = AmphitheatreGeometry::PolygonArea(Dims, Segments, Dims.OuterOffset()) - AmphitheatreGeometry::PolygonArea(Dims, Segments, 0.0);
	TestEqual(TEXT("no tilted faces"), Tilted, 0);
	TestTrue(FString::Printf(TEXT("treads (up) %.2f m^2 = ring %.2f m^2"), UpArea, Ring), FMath::IsNearlyEqual(UpArea, Ring, 1e-6 * Ring));
	TestTrue(FString::Printf(TEXT("ground face (down) %.2f m^2 = ring"), DownArea), FMath::IsNearlyEqual(DownArea, Ring, 1e-6 * Ring));
	TestTrue(FString::Printf(TEXT("risers + back wall %.1f m^2 > 0"), SideArea), SideArea > 0.0);

	// Risers face the arena (their plan normal points back toward the centre); the back wall is told apart by its offset.
	int32 RiserTowardArena = 0, RiserTotal = 0;
	for (const int32 Triangle : Cavea.TriangleIndicesItr())
	{
		const FVector3d N = FrontNormal(Cavea, Triangle).GetSafeNormal();
		const UE::Geometry::FIndex3i T = Cavea.GetTriangle(Triangle);
		const FVector3d Centroid = (Cavea.GetVertex(T.A) + Cavea.GetVertex(T.B) + Cavea.GetVertex(T.C)) / 3.0;
		if (FMath::Abs(N.Z) < 1e-9)
		{
			// Back-wall triangles have vertices on the outer ring only; skip them by their plan offset.
			const double X = Centroid.X / 100.0, Y = Centroid.Y / 100.0;
			const bool bBackWall = X * X / FMath::Square(Dims.A + Dims.OuterOffset() - 0.05) + Y * Y / FMath::Square(Dims.B + Dims.OuterOffset() - 0.05) > 1.0;
			if (!bBackWall)
			{
				++RiserTotal;
				RiserTowardArena += FVector3d(Centroid.X, Centroid.Y, 0.0).Dot(N) < 0.0 ? 1 : 0;
			}
		}
	}
	TestEqual(TEXT("riser triangles = rows x segments x 2"), RiserTotal, Dims.Rows * Segments * 2);
	TestEqual(TEXT("every riser faces the arena"), RiserTowardArena, RiserTotal);

	UE::Geometry::FDynamicMesh3 Floor;
	AmphitheatreGeometry::BuildFloorMesh(Dims, Segments, Floor);
	TestEqual(TEXT("floor fan of 96 triangles"), Floor.TriangleCount(), Segments);
	const double FloorArea = AreaM2(Floor);
	TestTrue(FString::Printf(TEXT("floor area %.2f m^2 = arena polygon"), FloorArea), FMath::IsNearlyEqual(FloorArea, AmphitheatreGeometry::PolygonArea(Dims, Segments, 0.0), 1e-6 * FloorArea));
	bool bFloorUp = true;
	for (const int32 Triangle : Floor.TriangleIndicesItr())
	{
		bFloorUp &= FrontNormal(Floor, Triangle).GetSafeNormal().Z > 1.0 - 1e-9;
	}
	TestTrue(TEXT("floor faces up"), bFloorUp);

	UE::Geometry::FDynamicMesh3 Velarium;
	AmphitheatreGeometry::BuildVelariumMesh(Dims, Segments, 18.0, Velarium);
	TestEqual(TEXT("velarium ring: 4 faces x segments x 2"), Velarium.TriangleCount(), 4 * Segments * 2);
	TestTrue(TEXT("velarium is closed"), Velarium.IsClosed());
	const double VelariumVolume = SignedVolumeM3(Velarium);
	TestTrue(FString::Printf(TEXT("velarium volume %.2f m^3 = 0.1 m x ring"), VelariumVolume), FMath::IsNearlyEqual(VelariumVolume, 0.1 * Ring, 1e-6 * Ring));
	TestTrue(TEXT("velarium underside at 18 m"), FMath::IsNearlyEqual(Velarium.GetBounds().Min.Z / 100.0, 18.0, 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsAmphitheatreRowsTest, "AcousticsLab.Core.Amphitheatre.RowsAndGrid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsAmphitheatreRowsTest::RunTest(const FString& Parameters)
{
	const FAmphitheatreDimensions Dims;
	TestEqual(TEXT("arena centre -> 0"), AmphitheatreGeometry::RowAtPlanPoint(Dims, 0.0, 0.0), 0);
	TestEqual(TEXT("beyond the back wall -> -1"), AmphitheatreGeometry::RowAtPlanPoint(Dims, 0.0, Dims.B + Dims.OuterOffset() + 0.5), -1);
	int32 Wrong = 0;
	for (int32 Row = 1; Row <= Dims.Rows; ++Row)
	{
		const double Offset = (Row - 0.5) * Dims.Depth;
		for (const double Angle : { 0.0, 37.0, 90.0, 200.0, 300.0 })
		{
			const double T = FMath::DegreesToRadians(Angle);
			Wrong += AmphitheatreGeometry::RowAtPlanPoint(Dims, (Dims.A + Offset) * FMath::Cos(T), (Dims.B + Offset) * FMath::Sin(T)) == Row ? 0 : 1;
		}
	}
	TestEqual(TEXT("row centre lines map to their rows (5 azimuths x 20 rows)"), Wrong, 0);

	TArray<FVector> Centers;
	TArray<int32> Rows;
	AmphitheatreGeometry::MakeGridPoints(Dims, Centers, Rows);
	TestEqual(TEXT("856 cavea grid points at 2 m (M10 level grid)"), Centers.Num(), 856);
	TestEqual(TEXT("one row per point"), Rows.Num(), Centers.Num());
	TArray<int32> PerRow;
	PerRow.Init(0, Dims.Rows + 1);
	int32 BadHeight = 0;
	for (int32 Index = 0; Index < Centers.Num(); ++Index)
	{
		if (Rows[Index] >= 1 && Rows[Index] <= Dims.Rows)
		{
			++PerRow[Rows[Index]];
		}
		BadHeight += FMath::IsNearlyEqual(Centers[Index].Z, Rows[Index] * Dims.Rise, 1e-9) ? 0 : 1;
	}
	int32 EmptyRows = 0;
	for (int32 Row = 1; Row <= Dims.Rows; ++Row)
	{
		EmptyRows += PerRow[Row] > 0 ? 0 : 1;
	}
	TestEqual(TEXT("every row has grid points"), EmptyRows, 0);
	TestEqual(TEXT("every point sits on its tread (z = row x rise)"), BadHeight, 0);

	// R1..R4 of the positions asset agree with the builder: their seats lie on the rows they name, 1.1 m above the tread.
	const UAmphitheatrePositionsAsset* Positions = GetDefault<UAmphitheatrePositionsAsset>();
	for (const FAmphitheatreSeat& Seat : Positions->Seats)
	{
		const FVector P = Positions->GetSeatPositionMeters(Seat) - Positions->CenterMeters;
		TestEqual(FString::Printf(TEXT("%s on row %d"), *Seat.ReceiverId.ToString(), Seat.Row), AmphitheatreGeometry::RowAtPlanPoint(Dims, P.X, P.Y), Seat.Row);
		TestTrue(FString::Printf(TEXT("%s 1.1 m above its tread"), *Seat.ReceiverId.ToString()), FMath::IsNearlyEqual(P.Z, Seat.Row * Dims.Rise + Positions->SeatedEarHeightMeters, 1e-4));
	}

	// Soundwalk: a listener over a seat hears the nearest grid cell, at most half a diagonal of a 2 m cell away in plan.
	TArray<FVector3d> Cells;
	for (const FVector& Center : Centers)
	{
		Cells.Add(FVector3d(Center.X, Center.Y, Center.Z + Positions->SeatedEarHeightMeters));
	}
	double Distance = 0.0;
	TestEqual(TEXT("FindNearest of an empty list"), USoundwalkComponent::FindNearest(TArray<FVector3d>(), FVector3d::ZeroVector, Distance), INDEX_NONE);
	for (const FAmphitheatreSeat& Seat : Positions->Seats)
	{
		const FVector3d Ear(Positions->GetSeatPositionMeters(Seat) - Positions->CenterMeters);
		const int32 Nearest = USoundwalkComponent::FindNearest(Cells, Ear, Distance);
		TestTrue(FString::Printf(TEXT("%s: nearest cell %d within %.2f m (got %.2f m)"), *Seat.ReceiverId.ToString(), Nearest, Dims.GridStep * 0.7072 + 0.76, Distance),
			Nearest != INDEX_NONE && Distance <= Dims.GridStep * 0.7072 + 0.76); // plan half diagonal + up to one row of height
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsAmphitheatreExtractionTest, "AcousticsLab.Core.Amphitheatre.ActorExtraction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsAmphitheatreExtractionTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsAmphitheatreTests;
	if (!TestNotNull(TEXT("engine"), GEngine))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("AcousticsAmphitheatreWorld"));
	if (!TestNotNull(TEXT("transient world"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	AAmphitheatreActor* Amphitheatre = World->SpawnActor<AAmphitheatreActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	ALevelSetupActor* Setup = World->SpawnActor<ALevelSetupActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	UAcousticSceneExtractorComponent* Extractor = Setup ? Setup->SceneExtractor.Get() : nullptr;
	if (TestNotNull(TEXT("amphitheatre actor"), Amphitheatre) && TestNotNull(TEXT("extractor"), Extractor))
	{
		// Defaults (no data asset assigned and the asset uses the SPEC numbers): 8064 cavea + 96 floor triangles.
		Amphitheatre->SetVelarium(false);
		Amphitheatre->SetAudience(false);
		Amphitheatre->Rebuild();
		const FAmphitheatreDimensions Dims = Amphitheatre->GetDimensions();
		const int32 Expected = Segments * (2 * Dims.Rows + 2) * 2 + Segments;

		auto CountMaterial = [](const FAcousticScene& Scene, const TCHAR* Name)
		{
			int32 Count = 0;
			for (const FAcousticTriangle& Triangle : Scene.Triangles)
			{
				Count += Scene.GetMaterialOf(Triangle).Name.Equals(Name, ESearchCase::IgnoreCase) ? 1 : 0;
			}
			return Count;
		};

		FAcousticScene Scene;
		FAcousticExtractionStats Stats;
		TestTrue(TEXT("empty: extracted"), Extractor->ExtractScene(Scene, Stats));
		AddInfo(Stats.ToString());
		TestEqual(TEXT("empty: floor + cavea triangles"), Scene.NumTriangles(), Expected);
		TestEqual(TEXT("empty: everything is Stone"), CountMaterial(Scene, TEXT("Stone")), Expected);
		double MeshArea = 0.0;
		{
			UE::Geometry::FDynamicMesh3 Floor, Cavea;
			AmphitheatreGeometry::BuildFloorMesh(Dims, Segments, Floor);
			AmphitheatreGeometry::BuildCaveaMesh(Dims, Segments, Cavea);
			MeshArea = AreaM2(Floor) + AreaM2(Cavea);
		}
		TestTrue(FString::Printf(TEXT("empty: area %.1f m^2 = mesh area %.1f m^2"), Scene.TotalArea(), MeshArea), FMath::IsNearlyEqual(Scene.TotalArea(), MeshArea, 1e-4 * MeshArea));
		const FBox3d Bounds = Scene.ComputeBounds();
		TestTrue(FString::Printf(TEXT("empty: top at 15 m (got %.3f)"), Bounds.Max.Z), FMath::IsNearlyEqual(Bounds.Max.Z, Dims.TopHeight(), 1e-3));

		Amphitheatre->SetAudience(true);
		Extractor->ExtractScene(Scene, Stats);
		TestEqual(TEXT("audience: cavea is Audience"), CountMaterial(Scene, TEXT("Audience")), Expected - Segments);
		TestEqual(TEXT("audience: arena stays Stone"), CountMaterial(Scene, TEXT("Stone")), Segments);

		Amphitheatre->SetVelarium(true);
		Extractor->ExtractScene(Scene, Stats);
		TestEqual(TEXT("audience + velarium: + 768 velarium triangles"), Scene.NumTriangles(), Expected + 4 * Segments * 2);
		TestEqual(TEXT("velarium is Absorber"), CountMaterial(Scene, TEXT("Absorber")), 4 * Segments * 2);
		TestTrue(TEXT("velarium tagged Acoustic"), Amphitheatre->Velarium->ComponentHasTag(TEXT("Acoustic")));

		Amphitheatre->SetVelarium(false);
		Amphitheatre->SetAudience(false);
		Extractor->ExtractScene(Scene, Stats);
		TestEqual(TEXT("velarium off again: back to floor + cavea"), Scene.NumTriangles(), Expected);
		TestFalse(TEXT("velarium off: no Acoustic tag"), Amphitheatre->Velarium->ComponentHasTag(TEXT("Acoustic")));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
