// M9 game-module helpers: amphitheatre positions (DA_AmphitheatrePositions) and the industry component specs (no world needed).
#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "Industry/IndustryAudioActor.h"
#include "Levels/AmphitheatrePositionsAsset.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsAmphitheatrePositionsTest, "AcousticsLab.Core.Levels.AmphitheatrePositions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsAmphitheatrePositionsTest::RunTest(const FString& Parameters)
{
	const UAmphitheatrePositionsAsset* Positions = NewObject<UAmphitheatrePositionsAsset>();
	const FAmphitheatreSeat* R1 = Positions->FindSeat(TEXT("R1"));
	const FAmphitheatreSeat* R2 = Positions->FindSeat(TEXT("R2"));
	const FAmphitheatreSeat* R3 = Positions->FindSeat(TEXT("R3"));
	const FAmphitheatreSeat* R4 = Positions->FindSeat(TEXT("R4"));
	if (!TestTrue(TEXT("default seats R1..R4 exist"), R1 && R2 && R3 && R4))
	{
		return false;
	}
	TestEqual(TEXT("R1 is the 1st row"), R1->Row, 1);
	TestEqual(TEXT("R2 is the 10th row"), R2->Row, 10);
	TestEqual(TEXT("R3 is the last row"), R3->Row, Positions->NumRows);
	TestFalse(TEXT("unknown receiver has no seat"), Positions->FindSeat(TEXT("R9")) != nullptr);

	// SPEC §3.4: source at the arena centre, 1.6 m high (float property: 1e-6 tolerance).
	TestTrue(TEXT("source at the arena centre, 1.6 m"), Positions->GetSourcePositionMeters().Equals(FVector(0.0, 0.0, 1.6), 1e-6));

	const FVector P1 = Positions->GetSeatPositionMeters(*R1);
	const FVector P2 = Positions->GetSeatPositionMeters(*R2);
	const FVector P3 = Positions->GetSeatPositionMeters(*R3);
	const FVector P4 = Positions->GetSeatPositionMeters(*R4);
	// Row 1 on the minor axis: just outside the 20 m semi-axis, one rise + ear height above the floor.
	TestTrue(TEXT("R1 on the minor axis at 20.4 m, 1.85 m high"), P1.Equals(FVector(0.0, 20.0 + 0.4, 0.75 + 1.1), 1e-4));
	// Row 20 = 15 m step (SPEC: cavea ~15 m high).
	TestEqual(TEXT("R3 ear height = 20 rises + 1.1 m"), P3.Z, 20 * 0.75 + 1.1, 1e-4);
	TestTrue(TEXT("rows rise and recede: R1 < R2 < R3"), P1.Z < P2.Z && P2.Z < P3.Z && P1.Y < P2.Y && P2.Y < P3.Y);
	const FVector Source = Positions->GetSourcePositionMeters();
	TestTrue(TEXT("distance to the source grows with the row"), FVector::Dist(Source, P1) < FVector::Dist(Source, P2) && FVector::Dist(Source, P2) < FVector::Dist(Source, P3));
	TestTrue(TEXT("R4 in the side sector on the major axis, row 10"), FMath::Abs(P4.Y) < 1e-6 && P4.X > Positions->ArenaSemiAxesMeters.X && FMath::IsNearlyEqual(P4.Z, P2.Z, 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsIndustryComponentSpecTest, "AcousticsLab.Core.Industry.ComponentSpec",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsIndustryComponentSpecTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>(GetTransientPackage());

	// A class path that does not resolve (plugin not installed) yields no component and an explanation.
	{
		FIndustryComponentSpec Spec;
		Spec.ComponentClass = FSoftClassPath(TEXT("/Script/NoSuchPlugin.NoSuchComponent"));
		FString Error;
		TestNull(TEXT("missing class gives no component"), AIndustryAudioActor::CreateComponentFromSpec(Owner, Spec, nullptr, false, Error));
		TestTrue(TEXT("missing class is explained"), Error.Contains(TEXT("not found")));
	}

	// Property values in text form reach the component; unknown properties are reported, the rest still applies.
	{
		FIndustryComponentSpec Spec;
		Spec.ComponentClass = FSoftClassPath(UAudioComponent::StaticClass());
		Spec.Properties.Add(TEXT("VolumeMultiplier"), TEXT("0.25"));
		Spec.Properties.Add(TEXT("bAllowSpatialization"), TEXT("False"));
		Spec.Properties.Add(TEXT("NoSuchProperty"), TEXT("1"));
		FString Error;
		const UAudioComponent* Audio = Cast<UAudioComponent>(AIndustryAudioActor::CreateComponentFromSpec(Owner, Spec, nullptr, false, Error));
		if (TestNotNull(TEXT("engine class creates a component"), Audio))
		{
			TestEqual(TEXT("float property imported"), Audio->VolumeMultiplier, 0.25f);
			TestFalse(TEXT("bool property imported"), static_cast<bool>(Audio->bAllowSpatialization));
			TestEqual(TEXT("component outer is the owner"), Audio->GetOwner(), Owner);
		}
		TestTrue(TEXT("unknown property reported"), Error.Contains(TEXT("NoSuchProperty")));
	}
	return true;
}

#endif
