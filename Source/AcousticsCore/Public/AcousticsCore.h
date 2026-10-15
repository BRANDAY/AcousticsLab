// AcousticsCore: pure C++ geometric-acoustics solvers. Meters, seconds, double precision; no UObject, no engine.
#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleInterface.h"
#include "Stats/Stats.h"

/** Logging category shared by the solver core and the game module. */
ACOUSTICSCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogAcoustics, Log, All);

/** Stat group of the solver entry points ("stat Acoustics"). */
DECLARE_STATS_GROUP(TEXT("Acoustics"), STATGROUP_Acoustics, STATCAT_Advanced);

class FAcousticsCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
