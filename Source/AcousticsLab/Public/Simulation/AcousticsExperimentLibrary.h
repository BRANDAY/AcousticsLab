// Headless experiments of SPEC §5 (M10), called from Tools/Experiments/run_m10_experiments.py through the python
// commandlet (editor closed). Blocking: they run the core solvers on the calling thread's task workers.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AcousticsExperimentLibrary.generated.h"

UCLASS()
class ACOUSTICSLAB_API UAcousticsExperimentLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * SPEC §5.1: shoebox 10 x 7 x 3 m, alpha 0.05 / 0.1 / 0.2 / 0.3 / 0.5, scattering 0.1 and 1.0; ray-tracer T30 at two
	 * receivers vs Sabine / Eyring. Writes Saved/Experiments/E1_shoebox_alpha.csv; returns a Markdown table.
	 */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Experiments")
	static FString RunShoeboxAlphaSweep(int32 NumRays, FString& OutCsvPath);

	/**
	 * SPEC §5.5: the amphitheatre of the loaded level empty / audience / velarium / both; one ray-tracer run each with R1..R4
	 * and the cavea grid; C80 / T30 / EDT per receiver and averaged per row. Writes Saved/Experiments/E5_amphitheatre*.csv.
	 */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Experiments", meta = (WorldContext = "WorldContextObject"))
	static FString RunAmphitheatreConditions(UObject* WorldContextObject, int32 NumRays, FString& OutCsvPath);
};
