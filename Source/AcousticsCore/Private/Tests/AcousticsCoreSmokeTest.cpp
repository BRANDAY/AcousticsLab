// M0 smoke test: the test pipeline works and the shared constants are what the SPEC says.
#include "AcousticsConstants.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsCoreSmokeTest, "AcousticsLab.Core.Smoke.Constants",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsCoreSmokeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("v1 is broadband"), Acoustics::NumBands, 1);
	TestTrue(TEXT("speed of sound in air is 343 m/s"), FMath::IsNearlyEqual(Acoustics::SpeedOfSoundAir, 343.0));
	TestTrue(TEXT("sound is faster in water than in air"), Acoustics::SpeedOfSoundWater > Acoustics::SpeedOfSoundAir);
	TestTrue(TEXT("histogram covers at least 1000 bins"), Acoustics::DefaultMaxTimeSeconds / Acoustics::DefaultBinSeconds >= 1000.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
