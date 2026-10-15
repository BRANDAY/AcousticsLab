#include "Simulation/AcousticSimulationSubsystem.h"

#include "Solvers/HybridTracer.h"

#include "AcousticsCore.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Results/SimulationResult.h"

UAcousticSimulationSubsystem* UAcousticSimulationSubsystem::Get(const UObject* WorldContextObject)
{
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
	return GameInstance ? GameInstance->GetSubsystem<UAcousticSimulationSubsystem>() : nullptr;
}

FString UAcousticSimulationSubsystem::SolverName(EAcousticSolver Solver)
{
	const UEnum* Enum = StaticEnum<EAcousticSolver>();
	return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Solver)) : FString::FromInt(static_cast<int32>(Solver));
}

FString UAcousticSimulationSubsystem::MakeResultKey(const FString& LevelName, EAcousticSolver Solver)
{
	return LevelName + TEXT("|") + SolverName(Solver);
}

void UAcousticSimulationSubsystem::StoreResult(const FString& LevelName, EAcousticSolver Solver, const TSharedPtr<const FSimulationResult>& Result)
{
	if (!Result.IsValid())
	{
		return;
	}
	const FString Key = MakeResultKey(LevelName, Solver);
	Results.Add(Key, Result);
	UE_LOG(LogAcoustics, Log, TEXT("Simulation subsystem: stored result '%s' (%d stored)"), *Key, Results.Num());
}

void UAcousticSimulationSubsystem::StoreHybridResult(const FString& LevelName, const TSharedPtr<const FHybridResult>& Result)
{
	if (Result.IsValid())
	{
		HybridResults.Add(LevelName, Result);
	}
}

TSharedPtr<const FHybridResult> UAcousticSimulationSubsystem::FindHybridResult(const FString& LevelName) const
{
	const TSharedPtr<const FHybridResult>* Found = HybridResults.Find(LevelName);
	return Found ? *Found : nullptr;
}

TSharedPtr<const FSimulationResult> UAcousticSimulationSubsystem::FindResult(const FString& LevelName, EAcousticSolver Solver) const
{
	const TSharedPtr<const FSimulationResult>* Found = Results.Find(MakeResultKey(LevelName, Solver));
	return Found ? *Found : nullptr;
}

TArray<FString> UAcousticSimulationSubsystem::GetResultKeys() const
{
	TArray<FString> Keys;
	Results.GetKeys(Keys);
	Keys.Sort();
	return Keys;
}

void UAcousticSimulationSubsystem::RememberSettings(const FSolverSettings& Settings)
{
	LastSettings = Settings;
	bHasLastSettings = true;
}

bool UAcousticSimulationSubsystem::GetLastSettings(FSolverSettings& OutSettings) const
{
	if (!bHasLastSettings)
	{
		return false;
	}
	OutSettings = LastSettings;
	return true;
}
