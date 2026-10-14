#include "Geometry/Medium.h"

FMedium::FMedium(const FString& InName, double InSpeedOfSound, double InDensity)
	: Name(InName)
	, SpeedOfSound(InSpeedOfSound)
	, Density(InDensity)
{
}

FMedium FMedium::Air()
{
	return FMedium(TEXT("Air"), Acoustics::SpeedOfSoundAir, 1.2);
}

FMedium FMedium::Water()
{
	return FMedium(TEXT("Water"), Acoustics::SpeedOfSoundWater, 1000.0);
}

double FMedium::ReflectionFactor(const FMedium& From, const FMedium& To)
{
	const double Z1 = From.Impedance();
	const double Z2 = To.Impedance();
	const double Sum = Z1 + Z2;
	if (Sum <= 0.0)
	{
		return 1.0;
	}
	const double R = (Z2 - Z1) / Sum;
	return R * R;
}
