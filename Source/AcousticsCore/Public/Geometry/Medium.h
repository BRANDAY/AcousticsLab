// A propagation medium (air, water) for beam tracing with refraction (SPEC §2.6).
#pragma once

#include "CoreMinimal.h"
#include "AcousticsConstants.h"

struct ACOUSTICSCORE_API FMedium
{
	FString Name;

	/** Speed of sound, m/s. */
	double SpeedOfSound = Acoustics::SpeedOfSoundAir;

	/** Density, kg/m^3. */
	double Density = 1.2;

	FMedium() = default;
	FMedium(const FString& InName, double InSpeedOfSound, double InDensity);

	/** Characteristic impedance Z = rho * c, kg/(m^2 s). */
	double Impedance() const { return Density * SpeedOfSound; }

	static FMedium Air();
	static FMedium Water();

	/** The media of the acoustic-lens test of Sikora 2012 (section 4.4): glycerol around a rubber sphere. */
	static FMedium Glycerol();
	static FMedium Rubber();

	/** Energy reflection factor R = ((Z2 - Z1) / (Z2 + Z1))^2 at normal incidence; transmission is 1 - R. */
	static double ReflectionFactor(const FMedium& From, const FMedium& To);
	static double TransmissionFactor(const FMedium& From, const FMedium& To) { return 1.0 - ReflectionFactor(From, To); }

	/** Critical angle of incidence (radians) going from C1 into C2: asin(C1 / C2) when C2 > C1, pi / 2 otherwise (no total internal reflection). */
	static double CriticalAngleRadians(double C1, double C2);
};
