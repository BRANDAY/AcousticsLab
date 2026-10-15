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

FMedium FMedium::Glycerol()
{
	return FMedium(TEXT("Glycerol"), 1920.0, 1260.0);
}

FMedium FMedium::Rubber()
{
	return FMedium(TEXT("Rubber"), 900.0, 930.0);
}

double FMedium::CriticalAngleRadians(double C1, double C2)
{
	if (!(C2 > C1) || !(C1 > 0.0))
	{
		return 0.5 * UE_DOUBLE_PI;
	}
	return FMath::Asin(C1 / C2);
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
