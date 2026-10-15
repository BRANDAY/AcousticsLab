// M5 solver tests (SPEC §4): beam emission, image sources in the shoebox, beams against image sources, receiver-in-beam,
// determinism of the beam tree and the receiver-as-query property (SPEC §2.5.7).
#include "Algo/IsSorted.h"
#include "Geometry/AcousticScene.h"
#include "Misc/AutomationTest.h"
#include "Solvers/BeamTracer.h"
#include "Solvers/ImageSourceSolver.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AcousticsBeamTests
{
	static const FVector3d RoomSize(10.0, 7.0, 3.0);
	static const FVector3d SourcePosition(-3.0, -1.5, 1.5);

	static TArray<FSolverReceiver> ShoeboxReceivers()
	{
		TArray<FSolverReceiver> Receivers;
		Receivers.Emplace(TEXT("R1"), FVector3d(3.0, 1.5, 1.5), 0.5);
		Receivers.Emplace(TEXT("R2"), FVector3d(0.0, 2.5, 1.2), 0.5);
		// R1 is the source mirrored through the room centre: its floor and ceiling reflection points lie on the shared
		// diagonal of the two wall triangles (duplicate-sequence removal). R3 sits behind the panel of the image test;
		// it is kept off the source height and offset, otherwise second-order paths graze the wall-ceiling edges exactly,
		// where the two methods legitimately differ by their tolerances.
		Receivers.Emplace(TEXT("R3"), FVector3d(-2.0, -1.3, 1.4), 0.5);
		Receivers.Emplace(TEXT("R4"), FVector3d(4.3, 2.8, 1.0), 0.5);
		return Receivers;
	}

	static FAcousticScene TestRoom()
	{
		return FAcousticScene::MakeShoebox(RoomSize, FAcousticMaterialProps(TEXT("Test"), 0.1, 0.0));
	}

	static FImageSourceSettings ImageSettings(int32 MaxOrder)
	{
		FImageSourceSettings Settings;
		Settings.MaxOrder = MaxOrder;
		return Settings;
	}

	static FBeamTracerSettings BeamSettings(int32 Level, int32 MaxOrder)
	{
		FBeamTracerSettings Settings;
		Settings.SubdivisionLevel = Level;
		Settings.MaxBeamOrder = MaxOrder;
		return Settings;
	}

	/** Solid angle of a spherical triangle (Van Oosterom & Strackee). */
	static double SolidAngle(const FVector3d& A, const FVector3d& B, const FVector3d& C)
	{
		const double Numerator = FMath::Abs(FVector3d::DotProduct(A, FVector3d::CrossProduct(B, C)));
		const double Denominator = 1.0 + FVector3d::DotProduct(A, B) + FVector3d::DotProduct(B, C) + FVector3d::DotProduct(C, A);
		return 2.0 * FMath::Atan2(Numerator, Denominator);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsBeamEmissionTest, "AcousticsLab.Core.Solvers.BeamEmission",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsBeamEmissionTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsBeamTests;
	const int32 Expected[4] = { 20, 80, 320, 1280 };
	for (int32 Level = 0; Level <= 3; ++Level)
	{
		TArray<FVector3d> Directions;
		FBeamTracer::EmissionDirections(Level, Directions);
		TestEqual(FString::Printf(TEXT("level %d: beam count"), Level), FBeamTracerSettings::NumRootBeams(Level), Expected[Level]);
		TestEqual(FString::Printf(TEXT("level %d: 3 directions per beam"), Level), Directions.Num(), Expected[Level] * 3);
		double TotalSolidAngle = 0.0;
		bool bUnit = true;
		for (int32 Beam = 0; Beam < Directions.Num() / 3; ++Beam)
		{
			for (int32 Index = 0; Index < 3; ++Index)
			{
				bUnit &= FMath::IsNearlyEqual(Directions[Beam * 3 + Index].Length(), 1.0, 1e-12);
			}
			TotalSolidAngle += SolidAngle(Directions[Beam * 3], Directions[Beam * 3 + 1], Directions[Beam * 3 + 2]);
		}
		TestTrue(FString::Printf(TEXT("level %d: unit directions"), Level), bUnit);
		TestTrue(FString::Printf(TEXT("level %d: beams cover the sphere (%.9f sr)"), Level, TotalSolidAngle), FMath::IsNearlyEqual(TotalSolidAngle, 4.0 * UE_DOUBLE_PI, 1e-9));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsReceiverInsideBeamTest, "AcousticsLab.Core.Solvers.ReceiverInsideBeam",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsReceiverInsideBeamTest::RunTest(const FString& Parameters)
{
	FBeamNode Node;
	Node.Apex = FVector3d::ZeroVector;
	Node.Directions[0] = FVector3d(1.0, 0.3, 0.3).GetSafeNormal();
	Node.Directions[1] = FVector3d(1.0, -0.3, 0.3).GetSafeNormal();
	Node.Directions[2] = FVector3d(1.0, 0.0, -0.3).GetSafeNormal();
	Node.ComputeSidePlanes();

	TestTrue(TEXT("point on the axis is inside the cone"), Node.ContainsPoint(FVector3d(5.0, 0.0, 0.0)));
	TestTrue(TEXT("point near a guide ray is inside"), Node.ContainsPoint(FVector3d(5.0, 1.4, 1.4)));
	TestFalse(TEXT("point beside the cone is outside"), Node.ContainsPoint(FVector3d(5.0, 3.0, 0.0)));
	TestFalse(TEXT("point behind the apex (antipodal cone) is outside"), Node.ContainsPoint(FVector3d(-5.0, 0.0, 0.0)));
	TestTrue(TEXT("apex itself counts as inside the cone"), Node.ConeContainsPoint(FVector3d::ZeroVector));

	// A reflected beam: valid only beyond the wall it came from (x = 2) and before the wall it hits next (x = 8).
	Node.bHasReflectionPlane = true;
	Node.ReflectionPlane = FAcousticPlane(FVector3d(1.0, 0.0, 0.0), FVector3d(2.0, 0.0, 0.0));
	TestTrue(TEXT("beyond the reflection plane"), Node.ContainsPoint(FVector3d(5.0, 0.0, 0.0)));
	TestFalse(TEXT("between the mirrored apex and the wall"), Node.ContainsPoint(FVector3d(1.0, 0.0, 0.0)));

	Node.HitTriangle = 7;
	Node.HitPlane = FAcousticPlane(FVector3d(-1.0, 0.0, 0.0), FVector3d(8.0, 0.0, 0.0));
	TestTrue(TEXT("before the hit plane"), Node.ContainsPoint(FVector3d(7.0, 0.0, 0.0)));
	TestFalse(TEXT("behind the hit plane"), Node.ContainsPoint(FVector3d(9.0, 0.0, 0.0)));
	TestTrue(TEXT("on the hit plane within tolerance"), Node.ContainsPoint(FVector3d(8.0, 0.0, 0.0)));

	// The tree query on a tiny hand-made tree: two nodes with the same sequence both containing the point count once.
	FBeamTree Tree;
	Tree.Source = FVector3d::ZeroVector;
	Tree.Nodes.Add(Node);
	FBeamNode Twin = Node;
	Tree.Nodes.Add(Twin);
	TArray<FBeamContribution> Contributions;
	Tree.Query(FVector3d(5.0, 0.0, 0.0), Contributions, true);
	TestEqual(TEXT("duplicate sequence reported once"), Contributions.Num(), 1);
	Tree.Query(FVector3d(5.0, 0.0, 0.0), Contributions, false);
	TestEqual(TEXT("both nodes reported without the duplicate filter"), Contributions.Num(), 2);
	TestTrue(TEXT("distance to the apex"), FMath::IsNearlyEqual(Contributions[0].Distance, 5.0, 1e-12));
	Tree.Query(FVector3d(9.0, 0.0, 0.0), Contributions);
	TestEqual(TEXT("no contribution behind the hit plane"), Contributions.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsImageSourceShoeboxTest, "AcousticsLab.Core.Solvers.ImageSourceShoebox",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsImageSourceShoeboxTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsBeamTests;
	const FAcousticScene Scene = TestRoom();
	TArray<FReflectingSurface> Surfaces;
	FImageSourceSolver::BuildSurfaces(Scene, Surfaces);
	TestEqual(TEXT("shoebox has 6 reflecting surfaces"), Surfaces.Num(), 6);
	for (const FReflectingSurface& Surface : Surfaces)
	{
		TestEqual(TEXT("two triangles per wall"), Surface.Triangles.Num(), 2);
	}

	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();
	const FSimulationResult Result = FImageSourceSolver::Run(Scene, SourcePosition, Receivers, ImageSettings(3));
	TestTrue(TEXT("run succeeds"), Result.bSuccess);
	TestEqual(TEXT("6 + 30 + 126 images (24 of the 150 third-order sequences, e.g. floor-wall-floor, cannot occur: the second image lies behind the third plane)"), Result.NumImageSources, 162);
	TestEqual(TEXT("one result per receiver"), Result.Receivers.Num(), Receivers.Num());

	// R1: every first-order reflection of an empty convex room is valid; delays and energies from the mirrored positions.
	const FReceiverResult& R1 = Result.Receivers[0];
	TArray<double> ExpectedDistances = { FMath::Sqrt(54.0), FMath::Sqrt(54.0), FMath::Sqrt(85.0), FMath::Sqrt(85.0), FMath::Sqrt(109.0), FMath::Sqrt(109.0) };
	TArray<double> FirstOrderDistances;
	for (const FEarlyReflection& Reflection : R1.EarlyReflections)
	{
		if (Reflection.Order == 1)
		{
			FirstOrderDistances.Add(Reflection.Distance);
			TestTrue(TEXT("first-order energy 0.9 r^2 / (4 d^2)"), FMath::IsNearlyEqual(Reflection.Energy[0], 0.9 * 0.25 / (4.0 * Reflection.Distance * Reflection.Distance), 1e-12));
			TestTrue(TEXT("first-order delay d / c"), FMath::IsNearlyEqual(Reflection.TimeSeconds, Reflection.Distance / 343.0, 1e-12));
			TestEqual(TEXT("first-order path has 3 points"), Reflection.Path.Num(), 3);
			TestEqual(TEXT("first-order path hits one triangle"), Reflection.Triangles.Num(), 1);
			TestTrue(TEXT("path starts at the source"), Reflection.Path[0].Equals(SourcePosition, 1e-9));
			TestTrue(TEXT("path ends at the receiver"), Reflection.Path.Last().Equals(R1.Position, 1e-9));
			// The reflection point lies on the wall and the two legs obey the mirror law: |S X| + |X R| = |S' R|.
			const double Legs = (Reflection.Path[1] - SourcePosition).Length() + (R1.Position - Reflection.Path[1]).Length();
			TestTrue(TEXT("mirror law: legs equal the image distance"), FMath::IsNearlyEqual(Legs, Reflection.Distance, 1e-9));
		}
	}
	FirstOrderDistances.Sort();
	TestEqual(TEXT("R1 sees 6 first-order images"), FirstOrderDistances.Num(), 6);
	for (int32 Index = 0; Index < FMath::Min(FirstOrderDistances.Num(), ExpectedDistances.Num()); ++Index)
	{
		TestTrue(FString::Printf(TEXT("first-order distance %d = %.4f m"), Index, ExpectedDistances[Index]), FMath::IsNearlyEqual(FirstOrderDistances[Index], ExpectedDistances[Index], 1e-9));
	}

	// Histogram = direct + every early reflection; direct sound as in the ray tracer.
	TestTrue(TEXT("direct energy r^2 / (4 d^2)"), FMath::IsNearlyEqual(R1.DirectEnergy, 0.25 / (4.0 * 45.0), 1e-12));
	double Sum = R1.DirectEnergy;
	int32 ValidForR1 = 0;
	for (const FEarlyReflection& Reflection : R1.EarlyReflections)
	{
		Sum += Reflection.Energy[0];
		++ValidForR1;
	}
	TestTrue(TEXT("histogram holds direct + reflections"), FMath::IsNearlyEqual(R1.Histogram.TotalEnergy(0), Sum, 1e-12));
	TestTrue(TEXT("reflections sorted by time"), Algo::IsSorted(R1.EarlyReflections, [](const FEarlyReflection& A, const FEarlyReflection& B) { return A.TimeSeconds < B.TimeSeconds; }));
	TestTrue(TEXT("second and third orders present"), ValidForR1 > 6);
	AddInfo(FString::Printf(TEXT("R1: %d valid paths of %d images; total valid paths %d"), ValidForR1, Result.NumImageSources, Result.NumValidImagePaths));

	// A blocked path: a wall segment between source and receiver R3 (1 m in front of the source) occludes the direct sound.
	{
		FAcousticScene Blocked = TestRoom();
		Blocked.AddQuad(FVector3d(-2.5, -1.7, 1.2), FVector3d(-2.5, -1.3, 1.2), FVector3d(-2.5, -1.3, 1.8), FVector3d(-2.5, -1.7, 1.8), 0);
		Blocked.BuildBVH();
		const FSimulationResult BlockedResult = FImageSourceSolver::Run(Blocked, SourcePosition, Receivers, ImageSettings(1));
		TestTrue(TEXT("blocked: direct sound to R3 occluded"), BlockedResult.Receivers[2].bDirectOccluded);
		TestFalse(TEXT("blocked: direct sound to R1 free"), BlockedResult.Receivers[0].bDirectOccluded);
		int32 FirstOrderR3 = 0;
		for (const FEarlyReflection& Reflection : BlockedResult.Receivers[2].EarlyReflections)
		{
			FirstOrderR3 += Reflection.Order == 1 ? 1 : 0;
		}
		TestTrue(TEXT("blocked: R3 still gets wall reflections around the panel"), FirstOrderR3 >= 4);
		AddInfo(FString::Printf(TEXT("blocked panel: R3 has %d first-order paths"), FirstOrderR3));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsBeamVsImageSourceTest, "AcousticsLab.Core.Solvers.BeamVsImageSource",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsBeamVsImageSourceTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsBeamTests;
	const FAcousticScene Scene = TestRoom();
	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();

	struct FCase
	{
		int32 Level;
		int32 MaxOrder;
	};
	const FCase Cases[] = { { 0, 2 }, { 2, 2 }, { 1, 3 }, { 2, 3 } };
	for (const FCase& Case : Cases)
	{
		const FSimulationResult Images = FImageSourceSolver::Run(Scene, SourcePosition, Receivers, ImageSettings(Case.MaxOrder));
		const FSimulationResult Beams = FBeamTracer::Run(Scene, SourcePosition, Receivers, BeamSettings(Case.Level, Case.MaxOrder));
		const FString Label = FString::Printf(TEXT("level %d, order <= %d"), Case.Level, Case.MaxOrder);
		TestTrue(Label + TEXT(": both runs succeed"), Images.bSuccess && Beams.bSuccess);
		TestTrue(Label + TEXT(": beam tree complete"), Beams.BeamTree.IsValid() && !Beams.BeamTree->bTruncated && Beams.BeamTree->NumUnresolved == 0 && Beams.BeamTree->NumEscaped == 0);
		AddInfo(FString::Printf(TEXT("%s: %s; %d image paths, %.3f s build"), *Label, *Beams.BeamTree->DescribeStats(), Images.NumValidImagePaths, Beams.Timing.SetupSeconds));

		for (int32 ReceiverIndex = 0; ReceiverIndex < Receivers.Num(); ++ReceiverIndex)
		{
			const FReceiverResult& I = Images.Receivers[ReceiverIndex];
			const FReceiverResult& B = Beams.Receivers[ReceiverIndex];
			const FString Prefix = FString::Printf(TEXT("%s, %s"), *Label, *I.Name);
			auto Describe = [](const FEarlyReflection& R)
			{
				FString Tris;
				for (const int32 T : R.Triangles) { Tris += FString::Printf(TEXT("%d "), T); }
				FString Path;
				for (const FVector3d& P : R.Path) { Path += FString::Printf(TEXT("(%.3f %.3f %.3f) "), P.X, P.Y, P.Z); }
				return FString::Printf(TEXT("order %d d %.4f E %.3e node %d tris [%s] path %s"), R.Order, R.Distance, R.Energy[0], R.BeamNode, *Tris, *Path);
			};
			TestEqual(Prefix + TEXT(": same number of early reflections"), B.EarlyReflections.Num(), I.EarlyReflections.Num());
			for (int32 Order = 1; Order <= Case.MaxOrder; ++Order)
			{
				int32 FromImages = 0;
				int32 FromBeams = 0;
				for (const FEarlyReflection& R : I.EarlyReflections) { FromImages += R.Order == Order ? 1 : 0; }
				for (const FEarlyReflection& R : B.EarlyReflections) { FromBeams += R.Order == Order ? 1 : 0; }
				TestEqual(FString::Printf(TEXT("%s: order %d count"), *Prefix, Order), FromBeams, FromImages);
			}
			// Match every image reflection to the beam reflection with the same order and image source (reflections of
			// equal delay, e.g. floor and ceiling for a receiver at the source height, have no stable order by time).
			TArray<bool> BeamMatched;
			BeamMatched.Init(false, B.EarlyReflections.Num());
			double MaxTimeError = 0.0;
			double MaxEnergyError = 0.0;
			int32 Unmatched = 0;
			for (const FEarlyReflection& FromImage : I.EarlyReflections)
			{
				int32 Found = INDEX_NONE;
				for (int32 Index = 0; Index < B.EarlyReflections.Num(); ++Index)
				{
					const FEarlyReflection& FromBeam = B.EarlyReflections[Index];
					if (!BeamMatched[Index] && FromBeam.Order == FromImage.Order && FromBeam.ImageSource.Equals(FromImage.ImageSource, 1e-6))
					{
						Found = Index;
						break;
					}
				}
				if (Found == INDEX_NONE)
				{
					++Unmatched;
					AddError(FString::Printf(TEXT("%s: image reflection without a beam: %s"), *Prefix, *Describe(FromImage)));
					continue;
				}
				BeamMatched[Found] = true;
				const FEarlyReflection& FromBeam = B.EarlyReflections[Found];
				MaxTimeError = FMath::Max(MaxTimeError, FMath::Abs(FromImage.TimeSeconds - FromBeam.TimeSeconds));
				MaxEnergyError = FMath::Max(MaxEnergyError, FMath::Abs(FromImage.Energy[0] - FromBeam.Energy[0]) / FMath::Max(FromImage.Energy[0], 1e-300));
				if (FromImage.Path.Num() != FromBeam.Path.Num())
				{
					AddError(FString::Printf(TEXT("%s: path length differs: image %s vs beam %s"), *Prefix, *Describe(FromImage), *Describe(FromBeam)));
					continue;
				}
				for (int32 Point = 0; Point < FromImage.Path.Num(); ++Point)
				{
					if (!FromImage.Path[Point].Equals(FromBeam.Path[Point], 1e-6))
					{
						AddError(FString::Printf(TEXT("%s: path point %d differs: image %s vs beam %s"), *Prefix, Point, *Describe(FromImage), *Describe(FromBeam)));
						break;
					}
				}
			}
			for (int32 Index = 0; Index < B.EarlyReflections.Num(); ++Index)
			{
				if (!BeamMatched[Index])
				{
					++Unmatched;
					AddError(FString::Printf(TEXT("%s: beam reflection without an image: %s"), *Prefix, *Describe(B.EarlyReflections[Index])));
					for (int32 NodeIndex = B.EarlyReflections[Index].BeamNode; NodeIndex != INDEX_NONE; NodeIndex = Beams.BeamTree->Nodes[NodeIndex].Parent)
					{
						const FBeamNode& N = Beams.BeamTree->Nodes[NodeIndex];
						AddInfo(FString::Printf(TEXT("  node %d order %d hit %d refl %d apex (%.4f %.4f %.4f) start (%.3f %.3f %.3f) (%.3f %.3f %.3f) (%.3f %.3f %.3f) end (%.3f %.3f %.3f) (%.3f %.3f %.3f) (%.3f %.3f %.3f) contains %d cone %d"),
							NodeIndex, N.Order, N.HitTriangle, N.ReflectionTriangle, N.Apex.X, N.Apex.Y, N.Apex.Z,
							N.StartPoints[0].X, N.StartPoints[0].Y, N.StartPoints[0].Z, N.StartPoints[1].X, N.StartPoints[1].Y, N.StartPoints[1].Z, N.StartPoints[2].X, N.StartPoints[2].Y, N.StartPoints[2].Z,
							N.EndPoints[0].X, N.EndPoints[0].Y, N.EndPoints[0].Z, N.EndPoints[1].X, N.EndPoints[1].Y, N.EndPoints[1].Z, N.EndPoints[2].X, N.EndPoints[2].Y, N.EndPoints[2].Z,
							N.ContainsPoint(Receivers[ReceiverIndex].Position) ? 1 : 0, N.ConeContainsPoint(Receivers[ReceiverIndex].Position) ? 1 : 0));
					}
				}
			}
			TestEqual(Prefix + TEXT(": every reflection matched by order and image source"), Unmatched, 0);
			TestTrue(FString::Printf(TEXT("%s: delays within 0.1 ms (max %.3g ms)"), *Prefix, MaxTimeError * 1000.0), MaxTimeError <= 1e-4);
			TestTrue(FString::Printf(TEXT("%s: energies within 1 %% (max %.3g %%)"), *Prefix, MaxEnergyError * 100.0), MaxEnergyError <= 0.01);
			TestTrue(Prefix + TEXT(": histogram energies within 1 %"), FMath::IsNearlyEqual(B.Histogram.TotalEnergy(0), I.Histogram.TotalEnergy(0), 0.01 * I.Histogram.TotalEnergy(0)));
			TestTrue(Prefix + TEXT(": same direct sound"), FMath::IsNearlyEqual(B.DirectEnergy, I.DirectEnergy, 1e-12) && B.bDirectOccluded == I.bDirectOccluded);
			// Exactly one order-0 beam contains the receiver (the direct sound).
			int32 DirectBeams = 0;
			for (const int32 NodeIndex : B.ContainingBeamNodes)
			{
				DirectBeams += Beams.BeamTree->Nodes[NodeIndex].Order == 0 ? 1 : 0;
			}
			TestEqual(Prefix + TEXT(": one order-0 beam contains the receiver"), DirectBeams, 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsBeamTreeDeterminismTest, "AcousticsLab.Core.Solvers.BeamTreeDeterminism",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsBeamTreeDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsBeamTests;
	const FAcousticScene Scene = TestRoom();
	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();
	const FBeamTracerSettings Settings = BeamSettings(2, 3);

	const TSharedPtr<FBeamTree> First = FBeamTracer::BuildTree(Scene, SourcePosition, Settings);
	const TSharedPtr<FBeamTree> Second = FBeamTracer::BuildTree(Scene, SourcePosition, Settings);
	TestTrue(TEXT("two builds are bit-identical"), First->IsIdentical(*Second, 0.0));
	TestEqual(TEXT("node orders are non-decreasing (breadth-first)"), Algo::IsSorted(First->Nodes, [](const FBeamNode& A, const FBeamNode& B) { return A.Order < B.Order; }), true);
	int32 Sum = 0;
	for (const int32 Count : First->NumNodesPerOrder)
	{
		Sum += Count;
	}
	TestEqual(TEXT("per-order counts add up"), Sum, First->Nodes.Num());
	TestTrue(TEXT("every root beam produced nodes"), First->NumNodesPerOrder[0] >= First->NumRootBeams);
	AddInfo(First->DescribeStats());

	// Brute force and BVH give the same tree.
	FBeamTracerSettings BruteForce = Settings;
	BruteForce.bUseBVH = false;
	const TSharedPtr<FBeamTree> Third = FBeamTracer::BuildTree(Scene, SourcePosition, BruteForce);
	TestTrue(TEXT("brute-force candidate search gives the same tree"), First->IsIdentical(*Third, 1e-12));

	// Receivers are queries: one tree answers each receiver alone exactly like the run with all of them.
	const FSimulationResult All = FBeamTracer::Run(Scene, SourcePosition, Receivers, Settings);
	for (int32 Index = 0; Index < Receivers.Num(); ++Index)
	{
		const FSimulationResult Single = FBeamTracer::Run(Scene, SourcePosition, MakeArrayView(&Receivers[Index], 1), Settings);
		TestTrue(FString::Printf(TEXT("%s alone: identical histogram"), *Receivers[Index].Name), Single.Receivers[0].Histogram.IsIdentical(All.Receivers[Index].Histogram));
		TestEqual(FString::Printf(TEXT("%s alone: same reflections"), *Receivers[Index].Name), Single.Receivers[0].EarlyReflections.Num(), All.Receivers[Index].EarlyReflections.Num());
	}
	// Moving a receiver costs a query only: the tree of the run is the one we built.
	TestTrue(TEXT("run result carries an identical tree"), All.BeamTree.IsValid() && All.BeamTree->IsIdentical(*First, 0.0));
	return true;
}

#endif
