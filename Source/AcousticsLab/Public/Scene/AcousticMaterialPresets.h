// Built-in acoustic material presets (SPEC §3.3): the source of the DataAssets under Content/Acoustics/Materials
// and the fallback of the scene extractor when a tag names a preset that has no asset.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Geometry/AcousticMaterialProps.h"

namespace AcousticMaterialPresets
{
	struct FPreset
	{
		FName Name;
		double Absorption = 0.0;
		double Scattering = 0.0;
		bool bInterface = false;
		FLinearColor DebugColor = FLinearColor::Gray;
		FString Description;
	};

	/** Concrete, Plaster, Wood, Audience, WaterSurface, Absorber, Interface. */
	ACOUSTICSLAB_API TConstArrayView<FPreset> All();

	/** Case-insensitive lookup; nullptr when unknown. */
	ACOUSTICSLAB_API const FPreset* Find(FName Name);

	/** "Concrete": the material of every surface that names nothing else. */
	ACOUSTICSLAB_API FName DefaultName();

	ACOUSTICSLAB_API TArray<FName> Names();

	ACOUSTICSLAB_API FAcousticMaterialProps ToProps(const FPreset& Preset);
}
