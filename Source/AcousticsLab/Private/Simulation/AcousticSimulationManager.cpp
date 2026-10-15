#include "Simulation/AcousticSimulationManager.h"

#include "AcousticsCore.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Geometry/AcousticScene.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Levels/LevelSetupActor.h"
#include "Levels/ShoeboxRoomActor.h"
#include "Results/RoomMetrics.h"
#include "Scene/AcousticReceiver.h"
#include "Scene/AcousticSource.h"
#include "Simulation/AcousticSimulationSubsystem.h"
#include "Solvers/RayTracer.h"

AAcousticSimulationManager::AAcousticSimulationManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAcousticSimulationManager::BeginPlay()
{
	Super::BeginPlay();

	if (UAcousticSimulationSubsystem* Subsystem = UAcousticSimulationSubsystem::Get(this))
	{
		FSolverSettings Remembered;
		if (Subsystem->GetLastSettings(Remembered))
		{
			Settings = Remembered;
		}
	}
	if (bRunOnBeginPlay)
	{
		Run();
	}
}

void AAcousticSimulationManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Task.IsValid() && !Task.IsCompleted())
	{
		if (ProgressState.IsValid())
		{
			ProgressState->RequestCancel();
		}
		Task.Wait();
	}
	Task = UE::Tasks::TTask<void>();
	SceneForTask.Reset();
	Super::EndPlay(EndPlayReason);
}

AAcousticSimulationManager* AAcousticSimulationManager::Find(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AAcousticSimulationManager> It(World);
	return It ? *It : nullptr;
}

AAcousticSimulationManager* AAcousticSimulationManager::FindOrSpawn(UWorld* World)
{
	if (AAcousticSimulationManager* Existing = Find(World))
	{
		return Existing;
	}
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Name = TEXT("SimulationManager");
	Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	AAcousticSimulationManager* Spawned = World->SpawnActor<AAcousticSimulationManager>(AAcousticSimulationManager::StaticClass(), FTransform::Identity, Params);
	if (Spawned)
	{
#if WITH_EDITOR
		Spawned->SetActorLabel(TEXT("SimulationManager"));
#endif
		UE_LOG(LogAcoustics, Log, TEXT("Spawned a simulation manager: the level had none"));
	}
	return Spawned;
}

bool AAcousticSimulationManager::CollectSceneActors(FVector3d& OutSource, TArray<FSolverReceiver>& OutReceivers, FString& OutError) const
{
	UWorld* World = GetWorld();
	const TArray<AAcousticSource*> Sources = AAcousticSource::FindAll(World);
	if (Sources.IsEmpty())
	{
		OutError = TEXT("the level has no AAcousticSource");
		return false;
	}
	const AAcousticSource* Source = Sources[0];
	if (!SourceId.IsNone())
	{
		if (AAcousticSource* const* Named = Sources.FindByPredicate([this](const AAcousticSource* Candidate) { return Candidate->SourceId == SourceId; }))
		{
			Source = *Named;
		}
		else
		{
			UE_LOG(LogAcoustics, Warning, TEXT("Source '%s' not found, using '%s'"), *SourceId.ToString(), *Source->SourceId.ToString());
		}
	}
	OutSource = Source->GetPositionMeters();

	OutReceivers.Reset();
	for (const AAcousticReceiver* Receiver : AAcousticReceiver::FindAll(World))
	{
		const double Radius = Settings.ReceiverRadius > 0.0f ? static_cast<double>(Settings.ReceiverRadius) : static_cast<double>(Receiver->RadiusMeters);
		OutReceivers.Emplace(Receiver->ReceiverId.IsNone() ? Receiver->GetName() : Receiver->ReceiverId.ToString(), Receiver->GetPositionMeters(), Radius);
	}
	if (OutReceivers.IsEmpty())
	{
		OutError = TEXT("the level has no AAcousticReceiver");
		return false;
	}
	return true;
}

bool AAcousticSimulationManager::ExtractScene(FAcousticScene& OutScene, FString& OutError)
{
	UWorld* World = GetWorld();
	const UAcousticSceneExtractorComponent* Extractor = nullptr;
	if (const ALevelSetupActor* Setup = ALevelSetupActor::Find(World))
	{
		Extractor = Setup->SceneExtractor;
	}
	UAcousticSceneExtractorComponent* Temporary = nullptr;
	if (!Extractor)
	{
		Temporary = NewObject<UAcousticSceneExtractorComponent>(this, TEXT("TemporarySceneExtractor"));
		Extractor = Temporary;
		UE_LOG(LogAcoustics, Warning, TEXT("Level has no ALevelSetupActor: extracting with default material bindings"));
	}

	FAcousticExtractionStats Stats;
	const bool bFound = Extractor->ExtractFromWorld(World, OutScene, Stats);
	LastExtraction = Stats;
	if (Temporary)
	{
		Temporary->MarkAsGarbage();
	}
	if (!bFound || OutScene.NumTriangles() == 0)
	{
		OutError = TEXT("no acoustic geometry found (tag meshes with \"Acoustic\")");
		return false;
	}

	if (WallAbsorptionOverride > 0.0f)
	{
		for (FAcousticMaterialProps& Material : OutScene.Materials)
		{
			for (double& Alpha : Material.Absorption)
			{
				Alpha = WallAbsorptionOverride;
			}
		}
	}
	if (!OutScene.HasBVH())
	{
		OutScene.BuildBVH();
	}
	return true;
}

void AAcousticSimulationManager::ComputeAnalyticReference(const FAcousticScene& Scene)
{
	double WeightedAbsorption = 0.0;
	double TotalArea = 0.0;
	for (const FAcousticTriangle& Triangle : Scene.Triangles)
	{
		const double Area = Triangle.Area();
		WeightedAbsorption += Area * Scene.GetMaterialOf(Triangle).Absorption[0];
		TotalArea += Area;
	}
	MeanAbsorption = TotalArea > 0.0 ? WeightedAbsorption / TotalArea : 0.0;

	const AShoeboxRoomActor* Room = AShoeboxRoomActor::Find(GetWorld());
	bHasAnalyticReference = Room != nullptr;
	if (!Room)
	{
		RoomVolumeM3 = RoomSurfaceM2 = SabineT60 = EyringT60 = 0.0;
		return;
	}
	RoomVolumeM3 = Room->GetVolumeM3();
	RoomSurfaceM2 = Room->GetSurfaceAreaM2();
	const double AirAbsorption = Settings.bAirAbsorption ? static_cast<double>(Settings.AirAbsorptionPerMeter) : 0.0;
	SabineT60 = RoomAcoustics::SabineT60(RoomVolumeM3, RoomSurfaceM2, MeanAbsorption, AirAbsorption);
	EyringT60 = RoomAcoustics::EyringT60(RoomVolumeM3, RoomSurfaceM2, MeanAbsorption, AirAbsorption);
}

bool AAcousticSimulationManager::Run()
{
	if (IsRunning())
	{
		UE_LOG(LogAcoustics, Warning, TEXT("Simulation already running"));
		return false;
	}

	SetState(ESimulationState::Extracting, TEXT("Extracting scene..."));
	Progress = 0.0f;
	RunStartSeconds = FPlatformTime::Seconds();
	RunEndSeconds = RunStartSeconds;

	TSharedPtr<FAcousticScene> Scene = MakeShared<FAcousticScene>();
	FString Error;
	if (!ExtractScene(*Scene, Error))
	{
		SetState(ESimulationState::Failed, Error);
		return false;
	}
	ComputeAnalyticReference(*Scene);

	FVector3d Source;
	TArray<FSolverReceiver> Receivers;
	if (!CollectSceneActors(Source, Receivers, Error))
	{
		SetState(ESimulationState::Failed, Error);
		return false;
	}

	FString SolverNote;
	if (Solver != EAcousticSolver::RayTracer)
	{
		SolverNote = FString::Printf(TEXT(" (%s not available yet, using the ray tracer)"), *UAcousticSimulationSubsystem::SolverName(Solver));
	}

	const FRayTracerSettings RayTracerSettings = Settings.ToRayTracerSettings();
	if (!RayTracerSettings.Validate(Error))
	{
		SetState(ESimulationState::Failed, Error);
		return false;
	}

	SceneForTask = Scene;
	ProgressState = MakeShared<FSolverProgress>();
	PendingResult = MakeShared<FSimulationResult>();
	++RunCount;

	TSharedPtr<FSolverProgress> Progress_ = ProgressState;
	TSharedPtr<FSimulationResult> Result = PendingResult;
	Task = UE::Tasks::Launch(UE_SOURCE_LOCATION,
		[Scene, Source, Receivers = MoveTemp(Receivers), RayTracerSettings, Progress_, Result]()
		{
			*Result = FRayTracer::Run(*Scene, Source, Receivers, RayTracerSettings, Progress_.Get());
		});

	if (UAcousticSimulationSubsystem* Subsystem = UAcousticSimulationSubsystem::Get(this))
	{
		Subsystem->RememberSettings(Settings);
	}
	UE_LOG(LogAcoustics, Log, TEXT("Simulation run %d started: %s; scene %s"), RunCount, *Settings.ToString(), *Scene->DescribeStats());
	SetState(ESimulationState::Running, FString::Printf(TEXT("Tracing %d rays...%s"), Settings.NumRays, *SolverNote));
	return true;
}

void AAcousticSimulationManager::Stop()
{
	if (State == ESimulationState::Running && ProgressState.IsValid())
	{
		ProgressState->RequestCancel();
		StatusMessage = TEXT("Cancelling...");
	}
}

float AAcousticSimulationManager::GetElapsedSeconds() const
{
	if (RunCount == 0)
	{
		return 0.0f;
	}
	const double End = IsRunning() ? FPlatformTime::Seconds() : RunEndSeconds;
	return static_cast<float>(End - RunStartSeconds);
}

void AAcousticSimulationManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (State != ESimulationState::Running)
	{
		return;
	}
	if (ProgressState.IsValid())
	{
		Progress = ProgressState->Fraction();
	}
	if (Task.IsValid() && Task.IsCompleted())
	{
		FinishRun();
	}
}

void AAcousticSimulationManager::FinishRun()
{
	RunEndSeconds = FPlatformTime::Seconds();
	LastResult = PendingResult;
	PendingResult.Reset();
	SceneForTask.Reset();
	Task = UE::Tasks::TTask<void>();
	Progress = 1.0f;

	if (!LastResult.IsValid())
	{
		SetState(ESimulationState::Failed, TEXT("no result"));
		return;
	}

	if (LastResult->bSuccess)
	{
		if (UAcousticSimulationSubsystem* Subsystem = UAcousticSimulationSubsystem::Get(this))
		{
			Subsystem->StoreResult(UGameplayStatics::GetCurrentLevelName(this, true), Solver, LastResult);
		}
		SetState(ESimulationState::Finished, FString::Printf(TEXT("Done in %.2f s"), LastResult->Timing.TotalSeconds));
	}
	else if (LastResult->bCancelled)
	{
		SetState(ESimulationState::Cancelled, TEXT("Cancelled"));
	}
	else
	{
		SetState(ESimulationState::Failed, LastResult->Error);
	}
	OnResultReady.Broadcast(this, *LastResult);
}

void AAcousticSimulationManager::SetState(ESimulationState NewState, const FString& Message)
{
	State = NewState;
	StatusMessage = Message;
	if (NewState == ESimulationState::Failed)
	{
		UE_LOG(LogAcoustics, Error, TEXT("Simulation failed: %s"), *Message);
	}
	OnStateChanged.Broadcast(NewState);
}
