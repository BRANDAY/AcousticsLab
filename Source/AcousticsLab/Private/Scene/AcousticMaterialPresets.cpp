#include "Scene/AcousticMaterialPresets.h"

namespace AcousticMaterialPresets
{
	static FPreset MakePreset(const TCHAR* Name, double Absorption, double Scattering, bool bInterface, const FLinearColor& Color, const TCHAR* Description)
	{
		FPreset Preset;
		Preset.Name = FName(Name);
		Preset.Absorption = Absorption;
		Preset.Scattering = Scattering;
		Preset.bInterface = bInterface;
		Preset.DebugColor = Color;
		Preset.Description = Description;
		return Preset;
	}

	TConstArrayView<FPreset> All()
	{
		// Built lazily: FName must not be constructed during static initialization.
		static const TArray<FPreset> Presets = {
			MakePreset(TEXT("Concrete"), 0.03, 0.10, false, FLinearColor(0.55f, 0.55f, 0.55f), TEXT("Concrete / stone (SPEC alpha 0.02-0.05). Default material.")),
			MakePreset(TEXT("Stone"), 0.02, 0.20, false, FLinearColor(0.75f, 0.68f, 0.55f), TEXT("Limestone masonry of an ancient amphitheatre (SPEC alpha 0.02-0.05); stepped rows scatter (M10).")),
			MakePreset(TEXT("Plaster"), 0.05, 0.10, false, FLinearColor(0.85f, 0.80f, 0.70f), TEXT("Plaster on masonry.")),
			MakePreset(TEXT("Wood"), 0.10, 0.15, false, FLinearColor(0.55f, 0.35f, 0.15f), TEXT("Wood panelling / stage floor.")),
			MakePreset(TEXT("Audience"), 0.70, 0.60, false, FLinearColor(0.60f, 0.10f, 0.10f), TEXT("Seated audience (SPEC 0.6-0.8); strongly scattering.")),
			MakePreset(TEXT("WaterSurface"), 0.01, 0.05, true, FLinearColor(0.10f, 0.40f, 0.90f), TEXT("Air/water interface: from the air side almost total reflection (R = 0.999 by impedances); refraction uses the media (M6).")),
			MakePreset(TEXT("Absorber"), 0.90, 0.20, false, FLinearColor(0.10f, 0.10f, 0.10f), TEXT("Porous absorber / velarium canopy.")),
			MakePreset(TEXT("Interface"), 0.00, 0.00, true, FLinearColor(0.30f, 0.80f, 1.00f), TEXT("Boundary between two media (the lens of M6): energy split by the impedances only, no absorption.")),
		};
		return Presets;
	}

	const FPreset* Find(FName Name)
	{
		if (Name.IsNone())
		{
			return nullptr;
		}
		for (const FPreset& Preset : All())
		{
			if (Preset.Name == Name) // FName comparison ignores case
			{
				return &Preset;
			}
		}
		return nullptr;
	}

	FName DefaultName()
	{
		return All()[0].Name;
	}

	TArray<FName> Names()
	{
		TArray<FName> Result;
		for (const FPreset& Preset : All())
		{
			Result.Add(Preset.Name);
		}
		return Result;
	}

	FAcousticMaterialProps ToProps(const FPreset& Preset)
	{
		return FAcousticMaterialProps(Preset.Name.ToString(), Preset.Absorption, Preset.Scattering, Preset.bInterface);
	}
}
