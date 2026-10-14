// M1 acceptance (SPEC §4): the shoebox room extracted through the real component path has 12 triangles and 242 m^2.
// Lives in the game module because it needs a world, actors and a static mesh.
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Geometry/AcousticScene.h"
#include "Levels/LevelSetupActor.h"
#include "Levels/ShoeboxRoomActor.h"
#include "Misc/AutomationTest.h"
#include "Scene/AcousticSceneExtractorComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsShoeboxExtractionTest, "AcousticsLab.Core.Geometry.ShoeboxExtraction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsShoeboxExtractionTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("engine"), GEngine))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("AcousticsShoeboxExtractionWorld"));
	if (!TestNotNull(TEXT("transient world"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	AShoeboxRoomActor* Room = World->SpawnActor<AShoeboxRoomActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	ALevelSetupActor* Setup = World->SpawnActor<ALevelSetupActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	UAcousticSceneExtractorComponent* Extractor = Setup ? Setup->SceneExtractor.Get() : nullptr;
	if (TestNotNull(TEXT("room actor"), Room) && TestNotNull(TEXT("level setup actor"), Setup) && TestNotNull(TEXT("extractor component"), Extractor))
	{
		Room->RoomSizeMeters = FVector(10.0, 7.0, 3.0);
		Room->MaterialName = TEXT("Concrete");
		Room->RebuildRoom();
		TestNotNull(TEXT("wall mesh (engine BasicShapes/Plane)"), Room->WallMesh.Get());

		FAcousticScene Scene;
		FAcousticExtractionStats Stats;
		const bool bExtracted = Extractor->ExtractScene(Scene, Stats);
		AddInfo(Stats.ToString());
		AddInfo(Scene.DescribeStats());

		TestTrue(TEXT("extraction found geometry"), bExtracted);
		TestEqual(TEXT("six wall components"), Stats.NumStaticMeshComponents, 6);
		TestEqual(TEXT("one acoustic actor"), Stats.NumActors, 1);
		TestEqual(TEXT("no warnings"), Stats.Warnings.Num(), 0);
		TestEqual(TEXT("12 triangles"), Scene.NumTriangles(), 12);
		TestTrue(FString::Printf(TEXT("surface 242 m^2 +- 0.1 (got %.4f)"), Scene.TotalArea()), FMath::Abs(Scene.TotalArea() - 242.0) <= 0.1);
		TestTrue(TEXT("BVH built"), Scene.HasBVH());

		const FBox3d Bounds = Scene.ComputeBounds();
		TestTrue(FString::Printf(TEXT("bounds (-5 -3.5 0)-(5 3.5 3) m, got (%.3f %.3f %.3f)-(%.3f %.3f %.3f)"), Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z, Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z),
			Bounds.Min.Equals(FVector3d(-5.0, -3.5, 0.0), 1e-3) && Bounds.Max.Equals(FVector3d(5.0, 3.5, 3.0), 1e-3));

		const FVector3d Center(0.0, 0.0, 1.5);
		int32 Outward = 0;
		int32 NotConcrete = 0;
		for (const FAcousticTriangle& Triangle : Scene.Triangles)
		{
			if (FVector3d::DotProduct(Triangle.N, Center - Triangle.Centroid()) <= 0.0)
			{
				++Outward;
			}
			const FAcousticMaterialProps& Material = Scene.GetMaterialOf(Triangle);
			if (!Material.Name.Equals(TEXT("Concrete"), ESearchCase::IgnoreCase) || !FMath::IsNearlyEqual(Material.Absorption[0], 0.03, 1e-9))
			{
				++NotConcrete;
			}
		}
		TestEqual(TEXT("every wall normal points into the room"), Outward, 0);
		TestEqual(TEXT("every wall is Concrete (alpha 0.03)"), NotConcrete, 0);

		FRayHit Hit;
		const bool bHit = Scene.IntersectRay(FAcousticRay(Center, FVector3d(1.0, 0.0, 0.0)), Hit);
		TestTrue(TEXT("ray from the center hits the front of the +X wall at 5 m"), bHit && !Hit.bBackFace && FMath::IsNearlyEqual(Hit.T, 5.0, 1e-6));

		// Material tag resolution without assets: the Wood preset through "AcMat:Wood".
		Room->MaterialName = TEXT("Wood");
		Room->RebuildRoom();
		Extractor->ExtractScene(Scene, Stats);
		TestEqual(TEXT("still 12 triangles"), Scene.NumTriangles(), 12);
		int32 NotWood = 0;
		for (const FAcousticTriangle& Triangle : Scene.Triangles)
		{
			const FAcousticMaterialProps& Material = Scene.GetMaterialOf(Triangle);
			if (!Material.Name.Equals(TEXT("Wood"), ESearchCase::IgnoreCase) || !FMath::IsNearlyEqual(Material.Absorption[0], 0.1, 1e-9))
			{
				++NotWood;
			}
		}
		TestEqual(TEXT("AcMat:Wood resolves to the Wood preset"), NotWood, 0);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
