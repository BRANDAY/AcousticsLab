// Surface material of the acoustic scene: absorption and scattering per band (SPEC §2.1, §3.3).
#pragma once

#include "CoreMinimal.h"
#include "AcousticsConstants.h"

/** Per-band absorption alpha and scattering s in [0, 1]. Arrays are sized Acoustics::NumBands from the start. */
struct ACOUSTICSCORE_API FAcousticMaterialProps
{
	FString Name;
	double Absorption[Acoustics::NumBands];
	double Scattering[Acoustics::NumBands];

	/** True for the boundary between two media (water surface): energy split by impedances, not by alpha (SPEC §2.6). */
	bool bInterface = false;

	/** Default = Concrete (alpha 0.03, s 0.1), the SPEC default material. */
	FAcousticMaterialProps();
	FAcousticMaterialProps(const FString& InName, double Alpha, double Scatter, bool bInInterface = false);

	/** Same value in every band. */
	void SetBroadband(double Alpha, double Scatter);

	double Reflection(int32 Band) const { return 1.0 - Absorption[Band]; }
	double MeanAbsorption() const;
	double MeanScattering() const;

	/** All coefficients within [0, 1]. */
	bool IsValid() const;
};
