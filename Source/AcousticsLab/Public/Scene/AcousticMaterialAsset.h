// DataAsset describing an acoustic surface material (SPEC §3.3). Presets live under Content/Acoustics/Materials.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Geometry/AcousticMaterialProps.h"
#include "AcousticMaterialAsset.generated.h"

/**
 * Absorption and scattering per band, plus the interface flag for the water surface.
 * Matched to scene components through the tag "AcMat:<MaterialName>" (UAcousticSceneExtractorComponent).
 */
UCLASS(BlueprintType)
class ACOUSTICSLAB_API UAcousticMaterialAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UAcousticMaterialAsset();

	/** Name used by the component tag AcMat:<MaterialName>. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName MaterialName;

	/** Absorption coefficient alpha per band (Acoustics::NumBands entries; one broadband value in v1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize, Category = "Acoustics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	TArray<float> Absorption;

	/** Scattering coefficient s per band: probability of a diffuse (Lambert) reflection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize, Category = "Acoustics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	TArray<float> Scattering;

	/** Boundary between two media (water surface): energy split by impedances, see SPEC §2.6. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	bool bInterface = false;

	/** Color of the surface in debug drawings and visualizers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FLinearColor DebugColor = FLinearColor::Gray;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (MultiLine = "true"))
	FString Description;

	/** The core representation handed to the solvers. */
	FAcousticMaterialProps ToProps() const;

	/** Copies a built-in preset (Concrete, Plaster, Wood, Audience, WaterSurface, Absorber) into this asset. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	bool ApplyPreset(FName PresetName);

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	static TArray<FName> GetPresetNames();

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** Keeps both arrays at exactly Acoustics::NumBands entries. */
	void NormalizeBandArrays();
};
