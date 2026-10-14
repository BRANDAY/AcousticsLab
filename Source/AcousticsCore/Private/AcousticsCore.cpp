#include "AcousticsCore.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAcoustics);

void FAcousticsCoreModule::StartupModule()
{
	UE_LOG(LogAcoustics, Log, TEXT("AcousticsCore module started"));
}

void FAcousticsCoreModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FAcousticsCoreModule, AcousticsCore)
