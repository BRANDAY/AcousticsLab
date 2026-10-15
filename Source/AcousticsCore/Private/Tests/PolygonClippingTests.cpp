// M5 geometry test: Sutherland-Hodgman clipping and the inside / outside split used by the beam division (SPEC §2.5.3).
#include "Geometry/PolygonClipping.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsClipPolygonTest, "AcousticsLab.Core.Geometry.ClipPolygon",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsClipPolygonTest::RunTest(const FString& Parameters)
{
	using namespace AcousticGeometry;

	const FPolygon2d Clip = { FVector2d(0.0, 0.0), FVector2d(4.0, 0.0), FVector2d(0.0, 4.0) };
	TestTrue(TEXT("clip triangle is convex and counter-clockwise"), IsConvexCounterClockwise(Clip));
	TestTrue(TEXT("clip area is 8"), FMath::IsNearlyEqual(SignedArea(Clip), 8.0, 1e-12));

	auto SumAreas = [](const TArray<FPolygon2d>& Pieces)
	{
		double Sum = 0.0;
		for (const FPolygon2d& Piece : Pieces)
		{
			Sum += SignedArea(Piece);
		}
		return Sum;
	};

	// Fully inside: unchanged.
	{
		const FPolygon2d Subject = { FVector2d(1.0, 1.0), FVector2d(2.0, 1.0), FVector2d(1.0, 2.0) };
		const FPolygon2d Clipped = ClipPolygon(Subject, Clip);
		TestEqual(TEXT("inside: 3 vertices"), Clipped.Num(), 3);
		TestTrue(TEXT("inside: area 0.5"), FMath::IsNearlyEqual(SignedArea(Clipped), 0.5, 1e-12));
		FPolygon2d Inside;
		TArray<FPolygon2d> Outside;
		SplitPolygon(Subject, Clip, Inside, Outside);
		TestTrue(TEXT("inside: split keeps the whole subject inside"), FMath::IsNearlyEqual(SignedArea(Inside), 0.5, 1e-12));
		TestEqual(TEXT("inside: no outside pieces"), Outside.Num(), 0);
	}

	// Fully outside: nothing inside, one outside piece.
	{
		const FPolygon2d Subject = { FVector2d(5.0, 5.0), FVector2d(6.0, 5.0), FVector2d(5.0, 6.0) };
		TestEqual(TEXT("outside: clip is empty"), ClipPolygon(Subject, Clip).Num(), 0);
		FPolygon2d Inside;
		TArray<FPolygon2d> Outside;
		SplitPolygon(Subject, Clip, Inside, Outside);
		TestEqual(TEXT("outside: inside part empty"), Inside.Num(), 0);
		TestEqual(TEXT("outside: one piece"), Outside.Num(), 1);
		TestTrue(TEXT("outside: piece area 0.5"), FMath::IsNearlyEqual(SumAreas(Outside), 0.5, 1e-12));
	}

	// Partial overlap: a square cut by the hypotenuse and the corner.
	{
		const FPolygon2d Subject = { FVector2d(1.0, 1.0), FVector2d(5.0, 1.0), FVector2d(5.0, 5.0), FVector2d(1.0, 5.0) };
		const FPolygon2d Clipped = ClipPolygon(Subject, Clip);
		TestEqual(TEXT("partial: triangle (1,1) (3,1) (1,3)"), Clipped.Num(), 3);
		TestTrue(TEXT("partial: inside area 2"), FMath::IsNearlyEqual(SignedArea(Clipped), 2.0, 1e-12));
		FPolygon2d Inside;
		TArray<FPolygon2d> Outside;
		SplitPolygon(Subject, Clip, Inside, Outside);
		TestTrue(TEXT("partial: split inside area 2"), FMath::IsNearlyEqual(SignedArea(Inside), 2.0, 1e-12));
		TestTrue(TEXT("partial: outside pieces sum to 14"), FMath::IsNearlyEqual(SumAreas(Outside), 14.0, 1e-12));
		for (const FPolygon2d& Piece : Outside)
		{
			TestTrue(TEXT("partial: outside piece is convex"), IsConvexCounterClockwise(Piece, 1e-12));
		}
		TestTrue(TEXT("partial: inside is convex"), IsConvexCounterClockwise(Inside, 1e-12));
	}

	// Edge-touching from inside: a smaller triangle sharing the corner and both legs is kept entirely.
	{
		const FPolygon2d Subject = { FVector2d(0.0, 0.0), FVector2d(2.0, 0.0), FVector2d(0.0, 2.0) };
		FPolygon2d Inside;
		TArray<FPolygon2d> Outside;
		SplitPolygon(Subject, Clip, Inside, Outside);
		TestTrue(TEXT("edge inside: area 2 kept"), FMath::IsNearlyEqual(SignedArea(Inside), 2.0, 1e-12));
		TestEqual(TEXT("edge inside: no outside pieces"), Outside.Num(), 0);
	}

	// Edge-touching from outside: a rectangle below the base shares the edge (0,0)-(4,0) and nothing else.
	{
		const FPolygon2d Subject = { FVector2d(0.0, -2.0), FVector2d(4.0, -2.0), FVector2d(4.0, 0.0), FVector2d(0.0, 0.0) };
		TestEqual(TEXT("edge outside: clip is empty"), ClipPolygon(Subject, Clip).Num(), 0);
		FPolygon2d Inside;
		TArray<FPolygon2d> Outside;
		SplitPolygon(Subject, Clip, Inside, Outside);
		TestEqual(TEXT("edge outside: inside empty"), Inside.Num(), 0);
		TestTrue(TEXT("edge outside: outside keeps the area 8"), FMath::IsNearlyEqual(SumAreas(Outside), 8.0, 1e-12));
	}

	// Vertex-touching from outside.
	{
		const FPolygon2d Subject = { FVector2d(4.0, 0.0), FVector2d(6.0, 0.0), FVector2d(6.0, 2.0) };
		TestEqual(TEXT("vertex outside: clip is empty"), ClipPolygon(Subject, Clip).Num(), 0);
	}

	// Both halves of a line cut reconstruct the subject.
	{
		const FPolygon2d Subject = { FVector2d(0.0, 0.0), FVector2d(3.0, 0.0), FVector2d(3.0, 3.0), FVector2d(0.0, 3.0) };
		FPolygon2d Left;
		FPolygon2d Right;
		ClipByLine(Subject, FVector2d(1.0, -1.0), FVector2d(2.0, 4.0), true, Left);
		ClipByLine(Subject, FVector2d(1.0, -1.0), FVector2d(2.0, 4.0), false, Right);
		TestTrue(TEXT("line cut: areas add up"), FMath::IsNearlyEqual(SignedArea(Left) + SignedArea(Right), 9.0, 1e-12));
		TestTrue(TEXT("line cut: both parts counter-clockwise"), SignedArea(Left) > 0.0 && SignedArea(Right) > 0.0);
	}

	// Random convex polygons: the split partitions the subject (areas add up, pieces convex, pieces inside / outside).
	{
		FRandomStream Rng(2026);
		auto RandomTriangle = [&Rng]()
		{
			FPolygon2d Polygon;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Polygon.Emplace(Rng.FRandRange(-3.0f, 3.0f), Rng.FRandRange(-3.0f, 3.0f));
			}
			MakeCounterClockwise(Polygon);
			return Polygon;
		};
		int32 AreaMismatches = 0;
		int32 ConvexityErrors = 0;
		int32 ContainmentErrors = 0;
		int32 Overlaps = 0;
		for (int32 Trial = 0; Trial < 2000; ++Trial)
		{
			const FPolygon2d Subject = RandomTriangle();
			const FPolygon2d ClipRandom = RandomTriangle();
			if (SignedArea(Subject) < 1e-3 || SignedArea(ClipRandom) < 1e-3)
			{
				continue;
			}
			FPolygon2d Inside;
			TArray<FPolygon2d> Outside;
			SplitPolygon(Subject, ClipRandom, Inside, Outside);
			const double Total = (Inside.Num() >= 3 ? SignedArea(Inside) : 0.0) + SumAreas(Outside);
			if (!FMath::IsNearlyEqual(Total, SignedArea(Subject), 1e-9))
			{
				++AreaMismatches;
			}
			if (Inside.Num() >= 3)
			{
				ConvexityErrors += IsConvexCounterClockwise(Inside, 1e-9) ? 0 : 1;
				ContainmentErrors += ContainsPoint(ClipRandom, Centroid(Inside), 1e-9) ? 0 : 1;
				Overlaps += ContainsPoint(Subject, Centroid(Inside), 1e-9) ? 0 : 1;
			}
			for (const FPolygon2d& Piece : Outside)
			{
				ConvexityErrors += IsConvexCounterClockwise(Piece, 1e-9) ? 0 : 1;
				const FVector2d Center = Centroid(Piece);
				ContainmentErrors += ContainsPoint(ClipRandom, Center, -1e-9) ? 1 : 0; // the centre of an outside piece is not inside the clip
				Overlaps += ContainsPoint(Subject, Center, 1e-9) ? 0 : 1;
			}
		}
		TestEqual(TEXT("random: inside + outside areas equal the subject"), AreaMismatches, 0);
		TestEqual(TEXT("random: every piece is convex"), ConvexityErrors, 0);
		TestEqual(TEXT("random: inside centre in clip, outside centres not"), ContainmentErrors, 0);
		TestEqual(TEXT("random: every piece lies in the subject"), Overlaps, 0);
	}

	// Fan triangulation.
	{
		TArray<FIntVector> Fan;
		FanTriangulate(5, Fan);
		TestEqual(TEXT("pentagon fans into 3 triangles"), Fan.Num(), 3);
		TestTrue(TEXT("fan triangles share vertex 0"), Fan[0] == FIntVector(0, 1, 2) && Fan[2] == FIntVector(0, 3, 4));
		FanTriangulate(2, Fan);
		TestEqual(TEXT("segment has no triangles"), Fan.Num(), 0);
	}
	return true;
}

#endif
