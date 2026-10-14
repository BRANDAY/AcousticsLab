#include "Geometry/AcousticMaterialProps.h"

FAcousticMaterialProps::FAcousticMaterialProps()
	: Name(TEXT("Concrete"))
{
	SetBroadband(0.03, 0.1);
}

FAcousticMaterialProps::FAcousticMaterialProps(const FString& InName, double Alpha, double Scatter, bool bInInterface)
	: Name(InName)
	, bInterface(bInInterface)
{
	SetBroadband(Alpha, Scatter);
}

void FAcousticMaterialProps::SetBroadband(double Alpha, double Scatter)
{
	for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
	{
		Absorption[Band] = FMath::Clamp(Alpha, 0.0, 1.0);
		Scattering[Band] = FMath::Clamp(Scatter, 0.0, 1.0);
	}
}

double FAcousticMaterialProps::MeanAbsorption() const
{
	double Sum = 0.0;
	for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
	{
		Sum += Absorption[Band];
	}
	return Sum / Acoustics::NumBands;
}

double FAcousticMaterialProps::MeanScattering() const
{
	double Sum = 0.0;
	for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
	{
		Sum += Scattering[Band];
	}
	return Sum / Acoustics::NumBands;
}

bool FAcousticMaterialProps::IsValid() const
{
	for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
	{
		if (Absorption[Band] < 0.0 || Absorption[Band] > 1.0 || Scattering[Band] < 0.0 || Scattering[Band] > 1.0)
		{
			return false;
		}
	}
	return true;
}
