// Keeps simulation results across level loads for the Compare panel (SPEC §3.5).
#pragma once

#include "CoreMinimal.h"
#include "Simulation/SolverSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AcousticSimulationSubsystem.generated.h"

struct FSimulationResult;

/** Results per (level, solver) and the last settings used, alive for the whole game instance. */
UCLASS()
class ACOUSTICSLAB_API UAcousticSimulationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UAcousticSimulationSubsystem* Get(const UObject* WorldContextObject);

	static FString MakeResultKey(const FString& LevelName, EAcousticSolver Solver);
	static FString SolverName(EAcousticSolver Solver);

	void StoreResult(const FString& LevelName, EAcousticSolver Solver, const TSharedPtr<const FSimulationResult>& Result);
	TSharedPtr<const FSimulationResult> FindResult(const FString& LevelName, EAcousticSolver Solver) const;
	TArray<FString> GetResultKeys() const;
	int32 NumResults() const { return Results.Num(); }

	void RememberSettings(const FSolverSettings& Settings);
	bool GetLastSettings(FSolverSettings& OutSettings) const;

private:
	TMap<FString, TSharedPtr<const FSimulationResult>> Results;

	UPROPERTY(Transient)
	FSolverSettings LastSettings;

	bool bHasLastSettings = false;
};
