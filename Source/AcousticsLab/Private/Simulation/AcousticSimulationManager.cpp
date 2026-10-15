#include "Simulation/AcousticSimulationManager.h"

#include "AcousticsCore.h"
#include "Audio/AuralizationComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Geometry/AcousticScene.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Levels/LevelSetupActor.h"
#include "Levels/ShoeboxRoomActor.h"
#include "Results/RoomMetrics.h"
#include "Scene/AcousticMediumPresets.h"
#include "Scene/AcousticReceiver.h"
#include "Scene/AcousticSource.h"
#include "Scene/ReceiverGrid.h"
#include "Simulation/AcousticSimulationSubsystem.h"
#include "Solvers/BeamTracer.h"
#include "Solvers/ImageSourceSolver.h"
#include "Solvers/RayTracer.h"
#include "Visualization/BeamVisualizerComponent.h"
#include "Visualization/ImageSourceVisualizerComponent.h"
#include "Visualization/RayPathVisualizerComponent.h"

AAcousticSimulationManager::AAcousticSimulationManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	PathVisualizer = CreateDefaultSubobject<URayPathVisualizerComponent>(TEXT("PathVisualizer"));
	Auralization = CreateDefaultSubobject<UAuralizationComponent>(TEXT("Auralization"));
	ImageSourceVisualizer = CreateDefaultSubobject<UImageSourceVisualizerComponent>(TEXT("ImageSourceVisualizer"));
	BeamVisualizer = CreateDefaultSubobject<UBeamVisualizerComponent>(TEXT("BeamVisualizer"));
}

EAcousticSolver AAcousticSimulationManager::GetEffectiveSolver() const
{
	return Solver == EAcousticSolver::Hybrid ? EAcousticSolver::RayTracer : Solver;
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

AAcousticSource* AAcousticSimulationManager::ResolveSourceActor() const
{
	const TArray<AAcousticSource*> Sources = AAcousticSource::FindAll(GetWorld());
	if (Sources.IsEmpty())
	{
		return nullptr;
	}
	if (!SourceId.IsNone())
	{
		if (AAcousticSource* const* Named = Sources.FindByPredicate([this](const AAcousticSource* Candidate) { return Candidate->SourceId == SourceId; }))
		{
			return *Named;
		}
		UE_LOG(LogAcoustics, Warning, TEXT("Source '%s' not found, using '%s'"), *SourceId.ToString(), *Sources[0]->SourceId.ToString());
	}
	return Sources[0];
}

TArray<FName> AAcousticSimulationManager::GetSourceIds() const
{
	TArray<FName> Ids;
	for (const AAcousticSource* Source : AAcousticSource::FindAll(GetWorld()))
	{
		Ids.Add(Source->SourceId);
	}
	return Ids;
}

bool AAcousticSimulationManager::CollectSceneActors(FVector3d& OutSource, TArray<FSolverReceiver>& OutReceivers, TArray<FReceiverBinding>& OutBindings, FString& OutError) const
{
	UWorld* World = GetWorld();
	const AAcousticSource* Source = ResolveSourceActor();
	if (!Source)
	{
		OutError = TEXT("the level has no AAcousticSource");
		return false;
	}
	OutSource = Source->GetPositionMeters();

	OutReceivers.Reset();
	OutBindings.Reset();
	for (AAcousticReceiver* Receiver : AAcousticReceiver::FindAll(World))
	{
		const double Radius = Settings.ReceiverRadius > 0.0f ? static_cast<double>(Settings.ReceiverRadius) : static_cast<double>(Receiver->RadiusMeters);
		OutReceivers.Emplace(Receiver->ReceiverId.IsNone() ? Receiver->GetName() : Receiver->ReceiverId.ToString(), Receiver->GetPositionMeters(), Radius);
		FReceiverBinding& Binding = OutBindings.AddDefaulted_GetRef();
		Binding.Kind = EReceiverBindingKind::Point;
		Binding.Actor = Receiver;
	}
	if (bIncludeReceiverGrids)
	{
		for (AReceiverGrid* Grid : AReceiverGrid::FindAll(World))
		{
			if (!Grid->bEnabled)
			{
				continue;
			}
			Grid->AppendSolverReceivers(OutReceivers, Settings.ReceiverRadius > 0.0f ? static_cast<double>(Settings.ReceiverRadius) : 0.0);
			for (int32 Tile = 0; Tile < Grid->NumTiles(); ++Tile)
			{
				FReceiverBinding& Binding = OutBindings.AddDefaulted_GetRef();
				Binding.Kind = EReceiverBindingKind::GridTile;
				Binding.Actor = Grid;
				Binding.TileIndex = Tile;
			}
		}
	}
	check(OutReceivers.Num() == OutBindings.Num());
	if (OutReceivers.IsEmpty())
	{
		OutError = TEXT("the level has no AAcousticReceiver or AReceiverGrid");
		return false;
	}
	return true;
}

TArray<int32> AAcousticSimulationManager::GetPointReceiverIndices() const
{
	TArray<int32> Indices;
	const int32 NumReceivers = LastResult.IsValid() ? LastResult->Receivers.Num() : 0;
	if (ReceiverBindings.Num() != NumReceivers)
	{
		for (int32 Index = 0; Index < NumReceivers; ++Index)
		{
			Indices.Add(Index);
		}
		return Indices;
	}
	for (int32 Index = 0; Index < NumReceivers; ++Index)
	{
		if (ReceiverBindings[Index].Kind == EReceiverBindingKind::Point)
		{
			Indices.Add(Index);
		}
	}
	return Indices;
}

void AAcousticSimulationManager::UpdateVisualizers()
{
	if (PathVisualizer)
	{
		PathVisualizer->SetResult(LastResult);
	}
	if (ImageSourceVisualizer)
	{
		ImageSourceVisualizer->SetResult(LastResult);
	}
	if (BeamVisualizer)
	{
		BeamVisualizer->SetResult(LastResult);
	}
	const bool bHaveResult = LastResult.IsValid() && LastResult->bSuccess && ReceiverBindings.Num() == LastResult->Receivers.Num();
	for (AReceiverGrid* Grid : AReceiverGrid::FindAll(GetWorld()))
	{
		if (!bHaveResult)
		{
			Grid->ClearResult();
			continue;
		}
		TArray<int32> Indices;
		Indices.Init(INDEX_NONE, Grid->NumTiles());
		for (int32 Index = 0; Index < ReceiverBindings.Num(); ++Index)
		{
			const FReceiverBinding& Binding = ReceiverBindings[Index];
			if (Binding.Kind == EReceiverBindingKind::GridTile && Binding.Actor.Get() == Grid && Indices.IsValidIndex(Binding.TileIndex))
			{
				Indices[Binding.TileIndex] = Index;
			}
		}
		Grid->ApplyResult(*LastResult, Indices);
	}
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
	TArray<FReceiverBinding> Bindings;
	if (!CollectSceneActors(Source, Receivers, Bindings, Error))
	{
		SetState(ESimulationState::Failed, Error);
		return false;
	}
	PendingBindings = MoveTemp(Bindings);

	FString SolverNote;
	const EAcousticSolver Effective = GetEffectiveSolver();
	if (Effective != Solver)
	{
		SolverNote = FString::Printf(TEXT(" (%s not available yet, using the ray tracer)"), *UAcousticSimulationSubsystem::SolverName(Solver));
	}

	const FRayTracerSettings RayTracerSettings = Settings.ToRayTracerSettings();
	const FImageSourceSettings ImageSourceSettings = Settings.ToImageSourceSettings();
	FBeamTracerSettings BeamTracerSettings = Settings.ToBeamTracerSettings();
	FString SourceNote;
	if (const AAcousticSource* SourceActor = ResolveSourceActor())
	{
		// Medium of the source (SPEC §2.6) and its directivity (the unidirectional source of the lens test, Sikora 2012).
		BeamTracerSettings.SourceMedium = 0;
		if (!SourceActor->MediumName.IsNone())
		{
			int32 MediumId = Scene->FindMedium(SourceActor->MediumName.ToString());
			if (MediumId == INDEX_NONE)
			{
				FMedium Medium;
				const ALevelSetupActor* Setup = ALevelSetupActor::Find(GetWorld());
				const bool bKnown = Setup && Setup->SceneExtractor ? Setup->SceneExtractor->FindMedium(SourceActor->MediumName, Medium) : AcousticMediumPresets::Find(SourceActor->MediumName, Medium);
				if (bKnown)
				{
					MediumId = Scene->FindOrAddMedium(Medium);
				}
				else
				{
					UE_LOG(LogAcoustics, Warning, TEXT("Source %s: unknown medium '%s', using %s"), *SourceActor->SourceId.ToString(), *SourceActor->MediumName.ToString(), *Scene->Media[0].Name);
					MediumId = 0;
				}
			}
			BeamTracerSettings.SourceMedium = MediumId;
		}
		BeamTracerSettings.bDirectionalSource = SourceActor->bDirectional;
		BeamTracerSettings.SourceDirection = SourceActor->GetDirection();
		BeamTracerSettings.SourceConeHalfAngleDeg = SourceActor->ConeHalfAngleDeg;
		SourceNote = FString::Printf(TEXT("source %s in %s%s"), *SourceActor->SourceId.ToString(), *Scene->GetMedium(BeamTracerSettings.SourceMedium).Name,
			SourceActor->bDirectional ? *FString::Printf(TEXT(", cone %.1f deg"), SourceActor->ConeHalfAngleDeg) : TEXT(""));
	}
	bool bValid = true;
	switch (Effective)
	{
	case EAcousticSolver::ImageSource: bValid = ImageSourceSettings.Validate(Error); break;
	case EAcousticSolver::BeamTracer: bValid = BeamTracerSettings.Validate(Error); break;
	default: bValid = RayTracerSettings.Validate(Error); break;
	}
	if (!bValid)
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
		[Scene, Source, Receivers = MoveTemp(Receivers), Effective, RayTracerSettings, ImageSourceSettings, BeamTracerSettings, Progress_, Result]()
		{
			switch (Effective)
			{
			case EAcousticSolver::ImageSource:
				*Result = FImageSourceSolver::Run(*Scene, Source, Receivers, ImageSourceSettings, Progress_.Get());
				break;
			case EAcousticSolver::BeamTracer:
				*Result = FBeamTracer::Run(*Scene, Source, Receivers, BeamTracerSettings, Progress_.Get());
				break;
			default:
				*Result = FRayTracer::Run(*Scene, Source, Receivers, RayTracerSettings, Progress_.Get());
				break;
			}
		});

	if (UAcousticSimulationSubsystem* Subsystem = UAcousticSimulationSubsystem::Get(this))
	{
		Subsystem->RememberSettings(Settings);
	}
	int32 NumGridReceivers = 0;
	for (const FReceiverBinding& Binding : PendingBindings)
	{
		NumGridReceivers += Binding.Kind == EReceiverBindingKind::GridTile ? 1 : 0;
	}
	UE_LOG(LogAcoustics, Log, TEXT("Simulation run %d started (%s): %s; %s; %d receivers (%d grid tiles); scene %s"), RunCount, *UAcousticSimulationSubsystem::SolverName(Effective), *Settings.ToString(Effective), *SourceNote, PendingBindings.Num(), NumGridReceivers, *Scene->DescribeStats());
	FString Activity;
	switch (Effective)
	{
	case EAcousticSolver::ImageSource: Activity = FString::Printf(TEXT("Image sources up to order %d..."), Settings.ImageSourceOrder); break;
	case EAcousticSolver::BeamTracer: Activity = FString::Printf(TEXT("Tracing %d beams up to order %d%s..."), FBeamTracerSettings::NumRootBeams(Settings.SubdivisionLevel), Settings.MaxBeamOrder, BeamTracerSettings.bRefraction && Scene->HasInterfaces() ? TEXT(" with refraction") : TEXT("")); break;
	default: Activity = FString::Printf(TEXT("Tracing %d rays..."), Settings.NumRays); break;
	}
	SetState(ESimulationState::Running, Activity + SolverNote);
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
	ReceiverBindings = MoveTemp(PendingBindings);
	PendingBindings.Reset();
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
	UpdateVisualizers();
	if (Auralization)
	{
		Auralization->SetResult(LastResult->bSuccess ? LastResult : nullptr, GetPointReceiverIndices(), UGameplayStatics::GetCurrentLevelName(this, true), LastResult->SolverName);
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
