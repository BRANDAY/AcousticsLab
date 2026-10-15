// M6 tests (SPEC §2.6, §4): Snell refraction and total internal reflection, the least-squares apex of a refracted beam,
// energy bookkeeping across an interface, determinism of the refracted beam tree, focusing of the acoustic lens.
#include "Geometry/AcousticScene.h"
#include "Misc/AutomationTest.h"
#include "Solvers/BeamTracer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AcousticsRefractionTests
{
	/** Angle between two unit vectors, degrees. */
	static double AngleDeg(const FVector3d& A, const FVector3d& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector3d::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0, 1.0)));
	}

	/** Direction arriving at a horizontal surface from above (travelling down) at the given incidence angle, in the XZ plane. */
	static FVector3d Incoming(double IncidenceDeg)
	{
		const double Rad = FMath::DegreesToRadians(IncidenceDeg);
		return FVector3d(FMath::Sin(Rad), 0.0, -FMath::Cos(Rad));
	}

	/** Sum of squared distances from a point to the lines. */
	static double Residual(const FVector3d& X, TConstArrayView<FVector3d> Origins, TConstArrayView<FVector3d> Directions)
	{
		double Sum = 0.0;
		for (int32 Index = 0; Index < Origins.Num(); ++Index)
		{
			const FVector3d D = Directions[Index].GetSafeNormal();
			const FVector3d Delta = X - Origins[Index];
			const FVector3d Perpendicular = Delta - D * FVector3d::DotProduct(Delta, D);
			Sum += Perpendicular.SquaredLength();
		}
		return Sum;
	}

	/**
	 * Air above a horizontal water surface at z = 0: one large interface quad (front normal +Z towards the air, medium 0 air
	 * in front, medium 1 water behind). Open scene otherwise, so every beam either reflects upwards or refracts downwards.
	 */
	static FAcousticScene WaterSurfaceScene(int32& OutWater)
	{
		FAcousticScene Scene;
		OutWater = Scene.AddMedium(FMedium::Water());
		const int32 Material = Scene.AddMaterial(FAcousticMaterialProps(TEXT("WaterSurface"), 0.01, 0.0, true));
		const double H = 200.0;
		FAcousticTriangle A(FVector3d(-H, -H, 0.0), FVector3d(H, -H, 0.0), FVector3d(H, H, 0.0), Material);
		FAcousticTriangle B(FVector3d(-H, -H, 0.0), FVector3d(H, H, 0.0), FVector3d(-H, H, 0.0), Material);
		for (FAcousticTriangle* Triangle : { &A, &B })
		{
			Triangle->bInterface = true;
			Triangle->MediumFront = 0;
			Triangle->MediumBack = OutWater;
			Scene.AddTriangle(*Triangle);
		}
		Scene.BuildBVH();
		return Scene;
	}

	static FBeamTracerSettings RefractionSettings(int32 Level, int32 MaxOrder)
	{
		FBeamTracerSettings Settings;
		Settings.SubdivisionLevel = Level;
		Settings.MaxBeamOrder = MaxOrder;
		Settings.bRefraction = true;
		Settings.MaxTimeSeconds = 1.0;
		return Settings;
	}

	/** Centroid of three points. */
	static FVector3d Centroid(const FVector3d* Points)
	{
		return (Points[0] + Points[1] + Points[2]) / 3.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsSnellTest, "AcousticsLab.Core.Geometry.Snell",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsSnellTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsRefractionTests;
	const double CAir = Acoustics::SpeedOfSoundAir;
	const double CWater = Acoustics::SpeedOfSoundWater;
	const FVector3d Up(0.0, 0.0, 1.0);   // normal of the surface pointing into the air (the incident medium from above)
	const FVector3d Down(0.0, 0.0, -1.0);

	// Normal incidence: unchanged direction, both ways, whichever way the normal is given.
	FVector3d Out;
	TestTrue(TEXT("normal incidence refracts"), FBeamTracer::RefractDirection(Down, Up, CAir, CWater, Out));
	TestTrue(TEXT("normal incidence keeps the direction"), Out.Equals(Down, 1e-12));
	TestTrue(TEXT("normal incidence refracts (normal given the other way)"), FBeamTracer::RefractDirection(Down, Down, CAir, CWater, Out));
	TestTrue(TEXT("normal incidence keeps the direction (normal given the other way)"), Out.Equals(Down, 1e-12));

	// Air -> water, 5 degrees: sin(theta2) = (1480 / 343) sin(5 deg) -> 22.11 degrees, bent away from the normal.
	{
		const FVector3d In = Incoming(5.0);
		TestTrue(TEXT("air->water 5 deg refracts"), FBeamTracer::RefractDirection(In, Up, CAir, CWater, Out));
		const double Expected = FMath::RadiansToDegrees(FMath::Asin(CWater / CAir * FMath::Sin(FMath::DegreesToRadians(5.0))));
		TestTrue(TEXT("air->water 5 deg: angle to the normal"), FMath::IsNearlyEqual(AngleDeg(Out, Down), Expected, 1e-9));
		TestTrue(TEXT("air->water: unit direction"), FMath::IsNearlyEqual(Out.Length(), 1.0, 1e-12));
		TestTrue(TEXT("air->water: in the plane of incidence"), FMath::Abs(Out.Y) < 1e-12 && Out.X > 0.0);
		TestTrue(TEXT("air->water: continues downwards"), Out.Z < 0.0);
		AddInfo(FString::Printf(TEXT("air->water 5 deg -> %.4f deg"), AngleDeg(Out, Down)));
	}
	// Water -> air, 30 degrees: sin(theta2) = (343 / 1480) sin(30 deg) -> 6.65 degrees, bent towards the normal.
	{
		const FVector3d In = Incoming(30.0);
		TestTrue(TEXT("water->air 30 deg refracts"), FBeamTracer::RefractDirection(In, Up, CWater, CAir, Out));
		const double Expected = FMath::RadiansToDegrees(FMath::Asin(CAir / CWater * 0.5));
		TestTrue(TEXT("water->air 30 deg: angle to the normal"), FMath::IsNearlyEqual(AngleDeg(Out, Down), Expected, 1e-9));
		// Reversibility: refracting the result back through the surface (now arriving from below) restores the 30 degrees.
		FVector3d Back;
		TestTrue(TEXT("water->air->water refracts back"), FBeamTracer::RefractDirection(-Out, Down, CAir, CWater, Back));
		TestTrue(TEXT("reversible"), FMath::IsNearlyEqual(AngleDeg(Back, Up), 30.0, 1e-9));
	}
	// Critical angle air -> water: asin(343 / 1480) = 13.40 degrees; 13.0 refracts (to about 76 degrees), 14.0 is total internal reflection.
	{
		const double CriticalDeg = FMath::RadiansToDegrees(FMedium::CriticalAngleRadians(CAir, CWater));
		TestTrue(TEXT("critical angle air->water about 13.40 deg"), FMath::IsNearlyEqual(CriticalDeg, 13.4006, 1e-3));
		TestTrue(TEXT("no critical angle water->air"), FMath::IsNearlyEqual(FMedium::CriticalAngleRadians(CWater, CAir), 0.5 * UE_DOUBLE_PI, 1e-12));
		TestTrue(TEXT("13 deg still refracts"), FBeamTracer::RefractDirection(Incoming(13.0), Up, CAir, CWater, Out));
		TestTrue(TEXT("13 deg refracts to a grazing angle"), AngleDeg(Out, Down) > 70.0 && AngleDeg(Out, Down) < 90.0);
		const FVector3d Untouched(7.0, 8.0, 9.0);
		Out = Untouched;
		TestFalse(TEXT("14 deg is total internal reflection"), FBeamTracer::RefractDirection(Incoming(14.0), Up, CAir, CWater, Out));
		TestTrue(TEXT("TIR leaves the output untouched"), Out.Equals(Untouched, 0.0));
		TestFalse(TEXT("just above the critical angle is TIR"), FBeamTracer::RefractDirection(Incoming(CriticalDeg + 1e-6), Up, CAir, CWater, Out));
		TestTrue(TEXT("just below the critical angle refracts"), FBeamTracer::RefractDirection(Incoming(CriticalDeg - 1e-6), Up, CAir, CWater, Out));
	}
	// Glycerol -> rubber (the lens of Sikora 2012): slower medium, rays bend towards the normal, no TIR at any angle.
	{
		const FMedium Glycerol = FMedium::Glycerol();
		const FMedium Rubber = FMedium::Rubber();
		TestTrue(TEXT("glycerol->rubber 60 deg refracts"), FBeamTracer::RefractDirection(Incoming(60.0), Up, Glycerol.SpeedOfSound, Rubber.SpeedOfSound, Out));
		TestTrue(TEXT("glycerol->rubber bends towards the normal"), AngleDeg(Out, Down) < 60.0);
		TestTrue(TEXT("glycerol->rubber 89 deg refracts"), FBeamTracer::RefractDirection(Incoming(89.0), Up, Glycerol.SpeedOfSound, Rubber.SpeedOfSound, Out));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsLeastSquaresApexTest, "AcousticsLab.Core.Geometry.LeastSquaresApex",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsLeastSquaresApexTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsRefractionTests;

	// Concurrent lines: three rays through one point, origins at different distances along them -> the point exactly.
	{
		const FVector3d Focus(1.5, -2.0, 3.25);
		const FVector3d Directions[3] = { FVector3d(1.0, 0.2, 0.1).GetSafeNormal(), FVector3d(0.9, -0.3, 0.2).GetSafeNormal(), FVector3d(1.0, 0.1, -0.4).GetSafeNormal() };
		const FVector3d Origins[3] = { Focus + Directions[0] * 2.0, Focus + Directions[1] * 5.0, Focus - Directions[2] * 3.0 };
		FVector3d Apex;
		TestTrue(TEXT("concurrent lines converge"), FBeamTracer::LeastSquaresApex(MakeArrayView(Origins, 3), MakeArrayView(Directions, 3), Apex));
		TestTrue(FString::Printf(TEXT("concurrent lines: exact apex (error %.3e)"), (Apex - Focus).Length()), Apex.Equals(Focus, 1e-9));
	}
	// Skew lines: the solution has a smaller residual than any nearby point, and equals the midpoint for two lines.
	{
		const FVector3d Origins[3] = { FVector3d(0.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.3), FVector3d(1.0, 0.0, -0.2) };
		const FVector3d Directions[3] = { FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 0.0, 1.0).GetSafeNormal(), FVector3d(0.0, 1.0, 0.2).GetSafeNormal() };
		FVector3d Apex;
		TestTrue(TEXT("skew lines converge"), FBeamTracer::LeastSquaresApex(MakeArrayView(Origins, 3), MakeArrayView(Directions, 3), Apex));
		const double Best = Residual(Apex, MakeArrayView(Origins, 3), MakeArrayView(Directions, 3));
		bool bMinimum = true;
		const double Step = 1e-3;
		for (const FVector3d& Delta : { FVector3d(Step, 0, 0), FVector3d(-Step, 0, 0), FVector3d(0, Step, 0), FVector3d(0, -Step, 0), FVector3d(0, 0, Step), FVector3d(0, 0, -Step), FVector3d(Step, Step, Step) })
		{
			bMinimum &= Residual(Apex + Delta, MakeArrayView(Origins, 3), MakeArrayView(Directions, 3)) > Best;
		}
		TestTrue(TEXT("skew lines: residual is a minimum"), bMinimum);
		AddInfo(FString::Printf(TEXT("skew apex (%.4f %.4f %.4f), residual %.3e"), Apex.X, Apex.Y, Apex.Z, Best));

		// Two skew lines: the point is the midpoint of their common perpendicular (x axis and the line y = 1 parallel to z).
		const FVector3d TwoOrigins[2] = { FVector3d(0.0, 0.0, 0.0), FVector3d(0.5, 1.0, 0.0) };
		const FVector3d TwoDirections[2] = { FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 0.0, 1.0) };
		TestTrue(TEXT("two skew lines converge"), FBeamTracer::LeastSquaresApex(MakeArrayView(TwoOrigins, 2), MakeArrayView(TwoDirections, 2), Apex));
		TestTrue(TEXT("two skew lines: midpoint of the common perpendicular"), Apex.Equals(FVector3d(0.5, 0.5, 0.0), 1e-9));
	}
	// Parallel lines: no apex; the fallback lies far behind the mean origin along the common direction.
	{
		const FVector3d Origins[3] = { FVector3d(0.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0), FVector3d(1.0, 0.0, 0.0) };
		const FVector3d Directions[3] = { FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, 1.0) };
		FVector3d Apex;
		TestFalse(TEXT("parallel lines have no apex"), FBeamTracer::LeastSquaresApex(MakeArrayView(Origins, 3), MakeArrayView(Directions, 3), Apex, 100.0));
		TestTrue(TEXT("parallel fallback behind the mean origin"), Apex.Equals(FVector3d(1.0 / 3.0, 1.0 / 3.0, -100.0), 1e-9));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsRefractionEnergyTest, "AcousticsLab.Core.Solvers.RefractionEnergy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsRefractionEnergyTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsRefractionTests;

	// R + T = 1 for every ordered pair of media, R symmetric, R(air, water) about 0.9989.
	const FMedium Media[4] = { FMedium::Air(), FMedium::Water(), FMedium::Glycerol(), FMedium::Rubber() };
	for (int32 I = 0; I < 4; ++I)
	{
		for (int32 J = 0; J < 4; ++J)
		{
			const double R = FMedium::ReflectionFactor(Media[I], Media[J]);
			const double T = FMedium::TransmissionFactor(Media[I], Media[J]);
			TestTrue(FString::Printf(TEXT("%s->%s: R + T = 1"), *Media[I].Name, *Media[J].Name), FMath::IsNearlyEqual(R + T, 1.0, 1e-15));
			TestTrue(FString::Printf(TEXT("%s->%s: R in [0, 1]"), *Media[I].Name, *Media[J].Name), R >= 0.0 && R <= 1.0);
			TestTrue(FString::Printf(TEXT("%s->%s: R symmetric"), *Media[I].Name, *Media[J].Name), FMath::IsNearlyEqual(R, FMedium::ReflectionFactor(Media[J], Media[I]), 1e-15));
		}
	}
	TestTrue(TEXT("air->water R about 0.9989"), FMath::IsNearlyEqual(FMedium::ReflectionFactor(Media[0], Media[1]), 0.99889, 1e-4));
	TestTrue(TEXT("glycerol->rubber R about 0.236"), FMath::IsNearlyEqual(FMedium::ReflectionFactor(Media[2], Media[3]), 0.2364, 1e-3));

	// Beam tree over the water surface: every first-order child of a root beam is either the reflected beam (factor R, or 1
	// beyond the critical angle) or the refracted one (factor T (r1 / r_in)^2); beams beyond the critical angle have no
	// refracted child; the refracted beams start at the interface and propagate downwards in water.
	int32 Water = INDEX_NONE;
	const FAcousticScene Scene = WaterSurfaceScene(Water);
	TestTrue(TEXT("scene has interfaces"), Scene.HasInterfaces());
	const FVector3d Source(0.0, 0.0, 1.5);
	const FBeamTracerSettings Settings = RefractionSettings(3, 1);
	const TSharedPtr<FBeamTree> Tree = FBeamTracer::BuildTree(Scene, Source, Settings);
	AddInfo(Tree->DescribeStats());
	const double R = FMedium::ReflectionFactor(Scene.GetMedium(0), Scene.GetMedium(Water));
	const double T = 1.0 - R;
	const double CriticalDeg = FMath::RadiansToDegrees(FMedium::CriticalAngleRadians(Scene.GetMedium(0).SpeedOfSound, Scene.GetMedium(Water).SpeedOfSound));
	int32 NumReflected = 0;
	int32 NumRefracted = 0;
	int32 NumTIR = 0;
	bool bFactorsOk = true;
	bool bMediaOk = true;
	bool bGeometryOk = true;
	double MaxIncidenceRefracted = 0.0;
	double MinIncidenceTIR = 180.0;
	TSet<int32> ParentsWithRefraction;
	for (int32 Index = 0; Index < Tree->Nodes.Num(); ++Index)
	{
		const FBeamNode& Node = Tree->Nodes[Index];
		if (Node.Order != 1)
		{
			continue;
		}
		const FBeamNode& Parent = Tree->Nodes[Node.Parent];
		const FVector3d Centre = Centroid(Parent.EndPoints);
		const double Incidence = AngleDeg(Parent.Axis(), FVector3d(0.0, 0.0, -1.0));
		if (Node.bRefracted)
		{
			++NumRefracted;
			ParentsWithRefraction.Add(Node.Parent);
			const double RIn = (Parent.Apex - Centre).Length();
			const double R1 = (Node.Apex - Centre).Length();
			const double Expected = Parent.Factor[0] * T * (R1 * R1) / (RIn * RIn);
			bFactorsOk &= FMath::IsNearlyEqual(Node.Factor[0], Expected, 1e-12 * FMath::Max(1.0, Expected));
			bMediaOk &= Node.Medium == Water && Node.NumRefractions == 1 && Parent.Medium == 0;
			bGeometryOk &= Node.Axis().Z < 0.0 && Node.StartPoints[0].Z < 1e-9 && Node.StartPoints[0].Z > -1e-9 && !Node.bFocusAhead;
			MaxIncidenceRefracted = FMath::Max(MaxIncidenceRefracted, Incidence);
		}
		else
		{
			++NumReflected;
			bMediaOk &= Node.Medium == 0 && Node.NumRefractions == 0;
			bGeometryOk &= Node.Axis().Z > 0.0;
		}
	}
	for (int32 Index = 0; Index < Tree->Nodes.Num(); ++Index)
	{
		const FBeamNode& Node = Tree->Nodes[Index];
		if (Node.Order != 1 || Node.bRefracted)
		{
			continue;
		}
		const bool bParentRefracted = !Tree->Nodes[Node.Parent].bTotalInternalReflection;
		const double Expected = bParentRefracted ? R : 1.0;
		bFactorsOk &= FMath::IsNearlyEqual(Node.Factor[0], Expected, 1e-12);
		if (!bParentRefracted)
		{
			++NumTIR;
			MinIncidenceTIR = FMath::Min(MinIncidenceTIR, AngleDeg(Tree->Nodes[Node.Parent].Axis(), FVector3d(0.0, 0.0, -1.0)));
		}
	}
	TestTrue(TEXT("refracted beams exist"), NumRefracted > 0);
	TestTrue(TEXT("reflected beams exist"), NumReflected > 0);
	TestTrue(TEXT("beams beyond the critical angle exist"), NumTIR > 0);
	TestTrue(TEXT("factors: R for reflected, T (r1/r_in)^2 for refracted, 1 beyond the critical angle"), bFactorsOk);
	TestTrue(TEXT("media and refraction counts"), bMediaOk);
	TestTrue(TEXT("refracted beams start on the surface and go down, reflected ones go up"), bGeometryOk);
	TestTrue(FString::Printf(TEXT("refracted parents arrive below the critical angle (max %.2f deg, critical %.2f)"), MaxIncidenceRefracted, CriticalDeg), MaxIncidenceRefracted <= CriticalDeg + 1e-9);
	TestTrue(FString::Printf(TEXT("TIR parents arrive beyond the critical angle (min %.2f deg)"), MinIncidenceTIR), MinIncidenceTIR >= CriticalDeg - 1e-9);
	// A refracted beam whose pieces are all slivers leaves no node, so the tree counter may exceed the node count by a few.
	TestTrue(FString::Printf(TEXT("tree counts the refractions (%d nodes, %d created)"), NumRefracted, Tree->NumRefracted), Tree->NumRefracted >= NumRefracted && Tree->NumRefracted <= NumRefracted + 3);
	TestEqual(TEXT("tree counts the total internal reflections"), Tree->NumTotalInternalReflections, NumTIR);

	// A receiver straight below the source: the transmitted arrival at h_air / c_air + h_water / c_water, energy T times the spreading.
	{
		const FVector3d Below(0.0, 0.0, -2.0);
		const TArray<FSolverReceiver> Receivers = { FSolverReceiver(TEXT("Below"), Below, 0.5), FSolverReceiver(TEXT("Above"), FVector3d(3.0, 0.0, 1.0), 0.5) };
		const FSimulationResult Result = FBeamTracer::Run(Scene, Source, Receivers, Settings);
		TestTrue(TEXT("run succeeded"), Result.bSuccess);
		const FReceiverResult& Out = Result.Receivers[0];
		TestTrue(TEXT("direct sound through the surface is blocked"), Out.bDirectOccluded);
		const FEarlyReflection* Transmitted = Out.EarlyReflections.FindByPredicate([](const FEarlyReflection& Reflection) { return Reflection.Order == 1 && Reflection.Path.Num() == 3 && Reflection.Path[2].Z < 0.0; });
		TestNotNull(TEXT("the receiver under water gets a transmitted arrival"), Transmitted);
		if (Transmitted)
		{
			const double Expected = 1.5 / Acoustics::SpeedOfSoundAir + 2.0 / Acoustics::SpeedOfSoundWater;
			TestTrue(FString::Printf(TEXT("transmitted delay %.4f ms vs %.4f ms"), Transmitted->TimeSeconds * 1000.0, Expected * 1000.0), FMath::IsNearlyEqual(Transmitted->TimeSeconds, Expected, 1e-4));
			TestTrue(TEXT("path crosses the surface"), FMath::Abs(Transmitted->Path[1].Z) < 1e-6 && Transmitted->Path[1].Size2D() < 0.05);
			TestTrue(TEXT("transmitted energy positive and below T"), Transmitted->Energy[0] > 0.0 && Transmitted->Energy[0] <= T);
			// Paraxial check (Sikora 2012 Eq. 6): the intensity at the surface is T / (4 pi h_air^2); below it the beam diverges
			// from the apparent source r1 = h_air c_air / c_water above the surface, so the energy at depth h is
			// T r^2 / (4 h_air^2) (r1 / (r1 + h))^2. Finite beam width moves the apparent source closer (ratio below 1).
			const double R1 = 1.5 * Acoustics::SpeedOfSoundAir / Acoustics::SpeedOfSoundWater;
			const double Paraxial = T * 0.25 / (4.0 * 1.5 * 1.5) * FMath::Square(R1 / (R1 + 2.0));
			AddInfo(FString::Printf(TEXT("transmitted energy %.4e, paraxial estimate %.4e (ratio %.3f)"), Transmitted->Energy[0], Paraxial, Transmitted->Energy[0] / Paraxial));
			TestTrue(TEXT("transmitted energy within 25 %% of the paraxial estimate"), FMath::Abs(Transmitted->Energy[0] / Paraxial - 1.0) < 0.25);
		}
		// The receiver above the water sees the direct sound and the surface reflection with factor R, delay by the mirror image.
		const FReceiverResult& Air = Result.Receivers[1];
		TestFalse(TEXT("direct sound above the water"), Air.bDirectOccluded);
		const FEarlyReflection* Reflected = Air.EarlyReflections.FindByPredicate([](const FEarlyReflection& Reflection) { return Reflection.Order == 1; });
		TestNotNull(TEXT("surface reflection above the water"), Reflected);
		if (Reflected)
		{
			const double Mirror = (FVector3d(3.0, 0.0, 1.0) - FVector3d(0.0, 0.0, -1.5)).Length();
			TestTrue(TEXT("surface reflection delay by the mirror image"), FMath::IsNearlyEqual(Reflected->TimeSeconds, Mirror / Acoustics::SpeedOfSoundAir, 1e-9));
			// The mirror path arrives at atan(3 / 2.5) = 50 deg, beyond the critical angle: the whole energy is reflected.
			const double IncidenceDeg = FMath::RadiansToDegrees(FMath::Atan2(3.0, 2.5));
			const double ExpectedEnergy = (IncidenceDeg > CriticalDeg ? 1.0 : R) * 0.25 / (4.0 * Mirror * Mirror);
			TestTrue(FString::Printf(TEXT("surface reflection energy %.4e vs %.4e (incidence %.1f deg)"), Reflected->Energy[0], ExpectedEnergy, IncidenceDeg), FMath::IsNearlyEqual(Reflected->Energy[0], ExpectedEnergy, 1e-12));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsRefractedTreeDeterminismTest, "AcousticsLab.Core.Solvers.RefractedBeamTreeDeterminism",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsRefractedTreeDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsRefractionTests;

	// A pool: shoebox 20 x 10 x 3 m of concrete with the water surface at z = 2 (front = air above, back = water below).
	FAcousticScene Scene = FAcousticScene::MakeShoebox(FVector3d(20.0, 10.0, 3.0), FAcousticMaterialProps(TEXT("Concrete"), 0.03, 0.0));
	const int32 Water = Scene.AddMedium(FMedium::Water());
	const int32 Surface = Scene.AddMaterial(FAcousticMaterialProps(TEXT("WaterSurface"), 0.01, 0.0, true));
	FAcousticTriangle A(FVector3d(-10.0, -5.0, 2.0), FVector3d(10.0, -5.0, 2.0), FVector3d(10.0, 5.0, 2.0), Surface);
	FAcousticTriangle B(FVector3d(-10.0, -5.0, 2.0), FVector3d(10.0, 5.0, 2.0), FVector3d(-10.0, 5.0, 2.0), Surface);
	for (FAcousticTriangle* Triangle : { &A, &B })
	{
		Triangle->bInterface = true;
		Triangle->MediumFront = 0;
		Triangle->MediumBack = Water;
		Scene.AddTriangle(*Triangle);
	}
	Scene.BuildBVH();

	const FVector3d Source(-6.0, 0.0, 2.6);
	const FBeamTracerSettings Settings = RefractionSettings(2, 3);
	const TSharedPtr<FBeamTree> First = FBeamTracer::BuildTree(Scene, Source, Settings);
	const TSharedPtr<FBeamTree> Second = FBeamTracer::BuildTree(Scene, Source, Settings);
	AddInfo(First->DescribeStats());
	TestTrue(TEXT("refracted trees exist"), First->NumRefracted > 0 && First->NumTotalInternalReflections > 0);
	TestTrue(TEXT("two builds are bit-identical"), First->IsIdentical(*Second, 0.0));
	FBeamTracerSettings BruteForce = Settings;
	BruteForce.bUseBVH = false;
	const TSharedPtr<FBeamTree> Third = FBeamTracer::BuildTree(Scene, Source, BruteForce);
	TestTrue(TEXT("brute-force candidate search gives the same tree"), First->IsIdentical(*Third, 1e-12));

	// Underwater beams carry the water medium; their delays to a receiver use both speeds of sound.
	const TArray<FSolverReceiver> Receivers = { FSolverReceiver(TEXT("U1"), FVector3d(-6.0, 0.0, 1.0), 0.5), FSolverReceiver(TEXT("U2"), FVector3d(2.0, 1.0, 0.8), 0.5), FSolverReceiver(TEXT("A1"), FVector3d(4.0, -2.0, 2.5), 0.5) };
	const FSimulationResult All = FBeamTracer::Run(Scene, Source, Receivers, Settings);
	TestTrue(TEXT("run succeeded"), All.bSuccess);
	for (int32 Index = 0; Index < Receivers.Num(); ++Index)
	{
		const FSimulationResult Single = FBeamTracer::Run(Scene, Source, MakeArrayView(&Receivers[Index], 1), Settings);
		TestTrue(FString::Printf(TEXT("%s alone: identical histogram"), *Receivers[Index].Name), Single.Receivers[0].Histogram.IsIdentical(All.Receivers[Index].Histogram));
		TestEqual(FString::Printf(TEXT("%s alone: same reflections"), *Receivers[Index].Name), Single.Receivers[0].EarlyReflections.Num(), All.Receivers[Index].EarlyReflections.Num());
	}
	int32 NumUnderwater = 0;
	bool bDelaysOk = true;
	for (const FEarlyReflection& Reflection : All.Receivers[0].EarlyReflections)
	{
		const FBeamNode& Node = All.BeamTree->Nodes[Reflection.BeamNode];
		FString PathText;
		for (const FVector3d& Point : Reflection.Path)
		{
			PathText += FString::Printf(TEXT(" (%.2f %.2f %.2f)"), Point.X, Point.Y, Point.Z);
		}
		AddInfo(FString::Printf(TEXT("U1 arrival: order %d, node %d, medium %d, %d refractions, %.3f ms, E %.3e, path%s"), Reflection.Order, Reflection.BeamNode, Node.Medium, Node.NumRefractions, Reflection.TimeSeconds * 1000.0, Reflection.Energy[0], *PathText));
		TestTrue(TEXT("every arrival at the receiver under water went through the surface"), Node.NumRefractions > 0 && Node.Medium == Water);
		if (Node.NumRefractions > 0)
		{
			++NumUnderwater;
			// Every underwater arrival is faster than the same path at the speed of air and slower than at the speed of water.
			double Length = 0.0;
			for (int32 Segment = 0; Segment + 1 < Reflection.Path.Num(); ++Segment)
			{
				Length += (Reflection.Path[Segment + 1] - Reflection.Path[Segment]).Length();
			}
			bDelaysOk &= Reflection.TimeSeconds < Length / Acoustics::SpeedOfSoundAir && Reflection.TimeSeconds > Length / Acoustics::SpeedOfSoundWater;
		}
	}
	TestTrue(TEXT("the receiver under the source gets underwater arrivals"), NumUnderwater > 0);
	TestTrue(TEXT("underwater delays lie between the all-air and all-water values"), bDelaysOk);
	AddInfo(FString::Printf(TEXT("U1: %d arrivals (%d through the water), U2: %d, A1: %d"), All.Receivers[0].EarlyReflections.Num(), NumUnderwater, All.Receivers[1].EarlyReflections.Num(), All.Receivers[2].EarlyReflections.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsLensFocusTest, "AcousticsLab.Core.Solvers.LensFocus",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsLensFocusTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsRefractionTests;

	// The acoustic lens of Sikora 2012 (section 4.4) scaled x10: a rubber sphere of radius 0.5 m centred 7.5 m from a
	// directional source in glycerol. Rubber is slower (900 vs 1920 m/s), so the beams converge inside the sphere towards
	// its back (ball lens: focus at n R / (2 (n - 1)) from the centre with n = 2.13 -> 0.47 m, inside the sphere).
	FAcousticScene Scene;
	const int32 Glycerol = Scene.AddMedium(FMedium::Glycerol());
	const int32 Rubber = Scene.AddMedium(FMedium::Rubber());
	const int32 Material = Scene.AddMaterial(FAcousticMaterialProps(TEXT("Lens"), 0.0, 0.0, true));
	const FVector3d Centre(7.5, 0.0, 0.0);
	const double Radius = 0.5;
	const int32 NumPhi = 24;
	const int32 NumTheta = 48;
	// Latitude / longitude sphere with outward normals (front = glycerol outside, back = rubber inside).
	auto Vertex = [&](int32 P, int32 T) -> FVector3d
	{
		const double Phi = UE_DOUBLE_PI * P / NumPhi;
		const double Theta = 2.0 * UE_DOUBLE_PI * T / NumTheta;
		return Centre + FVector3d(FMath::Sin(Phi) * FMath::Cos(Theta), FMath::Sin(Phi) * FMath::Sin(Theta), FMath::Cos(Phi)) * Radius;
	};
	auto AddLensTriangle = [&](const FVector3d& V0, const FVector3d& V1, const FVector3d& V2)
	{
		FAcousticTriangle Triangle(V0, V1, V2, Material);
		Triangle.bInterface = true;
		Triangle.MediumFront = Glycerol;
		Triangle.MediumBack = Rubber;
		if (Triangle.ComputeNormal() && FVector3d::DotProduct(Triangle.N, Triangle.Centroid() - Centre) < 0.0)
		{
			Triangle = FAcousticTriangle(V0, V2, V1, Material);
			Triangle.bInterface = true;
			Triangle.MediumFront = Glycerol;
			Triangle.MediumBack = Rubber;
		}
		Scene.AddTriangle(Triangle);
	};
	for (int32 P = 0; P < NumPhi; ++P)
	{
		for (int32 T = 0; T < NumTheta; ++T)
		{
			const FVector3d A = Vertex(P, T), B = Vertex(P + 1, T), C = Vertex(P + 1, T + 1), D = Vertex(P, T + 1);
			if (P > 0)
			{
				AddLensTriangle(A, B, C);
			}
			if (P + 1 < NumPhi)
			{
				AddLensTriangle(A, C, D);
			}
		}
	}
	Scene.BuildBVH();
	AddInfo(Scene.DescribeStats());

	FBeamTracerSettings Settings = RefractionSettings(6, 3);
	Settings.SourceMedium = Glycerol;
	Settings.bDirectionalSource = true;
	Settings.SourceDirection = FVector3d(1.0, 0.0, 0.0);
	Settings.SourceConeHalfAngleDeg = 4.5;
	Settings.MaxBeamNodes = 2000000;
	const FVector3d Source = FVector3d::ZeroVector;

	// Receivers on the axis inside the back of the sphere (the focus region), off the axis at the same depth, and in front of the lens.
	TArray<FSolverReceiver> Receivers;
	const double FocusX = Centre.X + 0.35;
	Receivers.Emplace(TEXT("Focus"), FVector3d(FocusX, 0.0, 0.0), 0.05);
	Receivers.Emplace(TEXT("OffAxis"), FVector3d(FocusX, 0.0, 0.3), 0.05);
	Receivers.Emplace(TEXT("OffAxis2"), FVector3d(FocusX, 0.3, 0.0), 0.05);
	Receivers.Emplace(TEXT("Front"), FVector3d(Centre.X - Radius - 0.5, 0.0, 0.0), 0.05);
	const FSimulationResult Result = FBeamTracer::Run(Scene, Source, Receivers, Settings);
	TestTrue(TEXT("run succeeded"), Result.bSuccess);
	AddInfo(Result.BeamTree->DescribeStats());
	TestTrue(TEXT("beams were refracted into the lens"), Result.BeamTree->NumRefracted > 0);
	TestTrue(TEXT("only the beams in the cone were emitted"), Result.BeamTree->NumRootBeamsEmitted < Result.BeamTree->NumRootBeams / 100);

	auto TotalEnergy = [](const FReceiverResult& Receiver)
	{
		double Sum = 0.0;
		for (const FEarlyReflection& Reflection : Receiver.EarlyReflections)
		{
			Sum += Reflection.Energy[0];
		}
		return Sum + Receiver.DirectEnergy;
	};
	const double Focus = TotalEnergy(Result.Receivers[0]);
	const double Off = FMath::Max(TotalEnergy(Result.Receivers[1]), TotalEnergy(Result.Receivers[2]));
	const double Front = TotalEnergy(Result.Receivers[3]);
	AddInfo(FString::Printf(TEXT("energy at the focus %.4e (%d arrivals), off axis %.4e, in front of the lens %.4e (direct %.4e)"), Focus, Result.Receivers[0].EarlyReflections.Num(), Off, Front, Result.Receivers[3].DirectEnergy));
	TestTrue(TEXT("the focus receives energy through the lens"), Focus > 0.0 && Result.Receivers[0].EarlyReflections.Num() > 0);
	TestTrue(TEXT("the focus is brighter than points 0.3 m off the axis"), Focus > 2.0 * Off);
	TestTrue(TEXT("the focus is brighter than the free field in front of the lens"), Focus > Front);
	// Every arrival inside the lens went through the interface: order >= 1, one refraction, the rubber medium and the slow speed.
	bool bInsideOk = true;
	for (const FEarlyReflection& Reflection : Result.Receivers[0].EarlyReflections)
	{
		const FBeamNode& Node = Result.BeamTree->Nodes[Reflection.BeamNode];
		bInsideOk &= Node.Medium == Rubber && Node.NumRefractions >= 1 && Reflection.Path.Num() >= 3;
	}
	TestTrue(TEXT("arrivals at the focus are refracted beams inside the rubber"), bInsideOk);
	return true;
}

#endif
