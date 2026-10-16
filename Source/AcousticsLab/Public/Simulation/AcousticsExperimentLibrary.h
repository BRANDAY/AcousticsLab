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

	/**
	 * SPEC §5.2: ray-tracer convergence over the ray count (RayCounts) with NumSeeds seeds each: the shoebox of E1
	 * (alpha 0.1, s 0.2, R1 / R2) and the amphitheatre of the loaded level with audience (R1..R4). Per receiver and N: mean
	 * and standard deviation of T30, C80 and EDT over the seeds, and the deviation of the mean from the largest N.
	 * Writes Saved/Experiments/E2_convergence.csv; returns a Markdown table.
	 */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Experiments", meta = (WorldContext = "WorldContextObject"))
	static FString RunRayConvergence(UObject* WorldContextObject, const TArray<int32>& RayCounts, int32 NumSeeds, FString& OutCsvPath);

	/**
	 * SPEC §5.3 on the amphitheatre of the loaded level (empty, R1..R4): hybrid runs (beams of order <= K, t_mix 80 ms)
	 * with the ray counts of RayCounts (t_mix = MixingTimeMs after the emission, beam tree capped at MaxBeamNodes, 0 = default); per receiver the early specular reflections of the beams and how many of them the
	 * pure ray run found / missed, plus the early energy beams vs rays. SubdivisionLevel < 0 = the manager setting.
	 * Writes Saved/Experiments/E3_missed_reflections.csv.
	 */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Experiments", meta = (WorldContext = "WorldContextObject"))
	static FString RunMissedReflections(UObject* WorldContextObject, const TArray<int32>& RayCounts, int32 K, int32 SubdivisionLevel, float MixingTimeMs, int32 MaxBeamNodes, FString& OutCsvPath);

	/**
	 * Known issue of the M10 fast pass (empty amphitheatre: no T30 within 3 s): the empty amphitheatre with the reflection
	 * order cap, T_max and the scattering of the stone varied; C80 / T30 / EDT at R1..R4 and the share of rays still
	 * alive at the order cap. Writes Saved/Experiments/E5b_empty_decay.csv.
	 */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Experiments", meta = (WorldContext = "WorldContextObject"))
	static FString RunEmptyDecayCheck(UObject* WorldContextObject, int32 NumRays, FString& OutCsvPath);
};
