#include "Scene/AcousticMaterialAsset.h"

#include "AcousticsConstants.h"
#include "Scene/AcousticMaterialPresets.h"

UAcousticMaterialAsset::UAcousticMaterialAsset()
{
	const AcousticMaterialPresets::FPreset* Concrete = AcousticMaterialPresets::Find(AcousticMaterialPresets::DefaultName());
	MaterialName = Concrete ? Concrete->Name : FName(TEXT("Concrete"));
	Absorption.Init(Concrete ? static_cast<float>(Concrete->Absorption) : 0.03f, Acoustics::NumBands);
	Scattering.Init(Concrete ? static_cast<float>(Concrete->Scattering) : 0.1f, Acoustics::NumBands);
	if (Concrete)
	{
		DebugColor = Concrete->DebugColor;
		Description = Concrete->Description;
	}
}

FAcousticMaterialProps UAcousticMaterialAsset::ToProps() const
{
	FAcousticMaterialProps Props;
	Props.Name = MaterialName.ToString();
	Props.bInterface = bInterface;
	for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
	{
		// A shorter array repeats its last value, an empty one keeps the Concrete defaults.
		if (Absorption.Num() > 0)
		{
			Props.Absorption[Band] = FMath::Clamp<double>(Absorption[FMath::Min(Band, Absorption.Num() - 1)], 0.0, 1.0);
		}
		if (Scattering.Num() > 0)
		{
			Props.Scattering[Band] = FMath::Clamp<double>(Scattering[FMath::Min(Band, Scattering.Num() - 1)], 0.0, 1.0);
		}
	}
	return Props;
}

bool UAcousticMaterialAsset::ApplyPreset(FName PresetName)
{
	const AcousticMaterialPresets::FPreset* Preset = AcousticMaterialPresets::Find(PresetName);
	if (!Preset)
	{
		return false;
	}
	Modify();
	MaterialName = Preset->Name;
	Absorption.Init(static_cast<float>(Preset->Absorption), Acoustics::NumBands);
	Scattering.Init(static_cast<float>(Preset->Scattering), Acoustics::NumBands);
	bInterface = Preset->bInterface;
	DebugColor = Preset->DebugColor;
	Description = Preset->Description;
	return true;
}

TArray<FName> UAcousticMaterialAsset::GetPresetNames()
{
	return AcousticMaterialPresets::Names();
}

void UAcousticMaterialAsset::PostLoad()
{
	Super::PostLoad();
	NormalizeBandArrays();
}

#if WITH_EDITOR
void UAcousticMaterialAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	NormalizeBandArrays();
}
#endif

void UAcousticMaterialAsset::NormalizeBandArrays()
{
	auto Normalize = [](TArray<float>& Values, float Default)
	{
		const float Last = Values.Num() > 0 ? Values.Last() : Default;
		while (Values.Num() < Acoustics::NumBands)
		{
			Values.Add(Last);
		}
		if (Values.Num() > Acoustics::NumBands)
		{
			Values.SetNum(Acoustics::NumBands);
		}
	};
	Normalize(Absorption, 0.03f);
	Normalize(Scattering, 0.1f);
}
