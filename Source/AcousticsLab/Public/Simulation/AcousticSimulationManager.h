// One per level: extracts the scene, runs a solver on a background task and publishes the result (SPEC §3.5).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Results/SimulationResult.h"
#include "Scene/AcousticSceneExtractorComponent.h"
#include "Simulation/SolverSettings.h"
#include "Tasks/Task.h"
#include "AcousticSimulationManager.generated.h"

struct FAcousticScene;
class AReceiverGrid;
class UAuralizationComponent;
class UBeamVisualizerComponent;
class UImageSourceVisualizerComponent;
class URayPathVisualizerComponent;

/** What a receiver of the result corresponds to in the level. */
UENUM(BlueprintType)
enum class EReceiverBindingKind : uint8
{
	/** An AAcousticReceiver actor (shown in the HUD selector). */
	Point,
	/** One tile of an AReceiverGrid (feeds its heatmap). */
	GridTile
};

/** Receivers[i] of the last result <-> ReceiverBindings[i]. */
USTRUCT(BlueprintType)
struct FReceiverBinding
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	EReceiverBindingKind Kind = EReceiverBindingKind::Point;

	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	TWeakObjectPtr<AActor> Actor;

	/** Tile index for GridTile bindings. */
	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	int32 TileIndex = INDEX_NONE;
};

UENUM(BlueprintType)
enum class ESimulationState : uint8
{
	Idle,
	Extracting,
	Running,
	Finished,
	Cancelled,
	Failed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSimulationStateChanged, ESimulationState, NewState);

class AAcousticSimulationManager;
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSimulationResultReady, AAcousticSimulationManager*, const FSimulationResult&);

/**
 * Run(): extracts the level through ALevelSetupActor::SceneExtractor (a temporary extractor when the level has none),
 * collects AAcousticSource / AAcousticReceiver, launches the solver with UE::Tasks::Launch and polls it in Tick.
 * The result is published on the game thread (OnResultReady, OnStateChanged) and stored in UAcousticSimulationSubsystem.
 * For the shoebox the Sabine / Eyring reference is computed from AShoeboxRoomActor and the area-weighted mean absorption.
 */
UCLASS(Blueprintable, HideCategories = (Rendering, Replication, Collision, Input, HLOD, LOD, Cooking, Physics, Networking))
class ACOUSTICSLAB_API AAcousticSimulationManager : public AActor
{
	GENERATED_BODY()

public:
	AAcousticSimulationManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FSolverSettings Settings;

	/** Ray tracer, image sources or beam tracer (M5); the hybrid falls back to the ray tracer with a note in StatusMessage until M8. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	EAcousticSolver Solver = EAcousticSolver::RayTracer;

	/** > 0 replaces the absorption of every extracted material (shoebox validation against Sabine / Eyring); 0 keeps the materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float WallAbsorptionOverride = 0.0f;

	/** Source to emit from when the level has several (by SourceId); None = the first one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FName SourceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	bool bRunOnBeginPlay = false;

	/** Receiver grids (AReceiverGrid, heatmap) add their receivers to every run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	bool bIncludeReceiverGrids = true;

	/** Draws the sampled ray paths of the last result (M3). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<URayPathVisualizerComponent> PathVisualizer;

	/** Impulse responses, WAV export and convolution reverb of the last result (M4). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UAuralizationComponent> Auralization;

	/** Image-source spheres and specular paths of the last result (M5). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UImageSourceVisualizerComponent> ImageSourceVisualizer;

	/** Beam frustums of the last beam-tracer result (M5). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UBeamVisualizerComponent> BeamVisualizer;

	/** One entry per receiver of the last result: point receiver actor or grid tile. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	TArray<FReceiverBinding> ReceiverBindings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	ESimulationState State = ESimulationState::Idle;

	/** 0..1 while running. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	float Progress = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	FString StatusMessage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	FAcousticExtractionStats LastExtraction;

	/** Analytic reference of the last run (shoebox levels only). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|Reference")
	bool bHasAnalyticReference = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|Reference")
	double RoomVolumeM3 = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|Reference")
	double RoomSurfaceM2 = 0.0;

	/** Area-weighted mean absorption of the extracted scene (after the override). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|Reference")
	double MeanAbsorption = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|Reference")
	double SabineT60 = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|Reference")
	double EyringT60 = 0.0;

	UPROPERTY(BlueprintAssignable, Category = "Acoustics")
	FOnSimulationStateChanged OnStateChanged;

	/** Game thread, after every finished / cancelled / failed run. */
	FOnSimulationResultReady OnResultReady;

	/** Starts a run; false when one is already running or the level has no geometry / source / receiver. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	bool Run();

	/** Asks the running solver to stop; the state becomes Cancelled once the task returns. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void Stop();

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	bool IsRunning() const { return State == ESimulationState::Extracting || State == ESimulationState::Running; }

	/** Seconds since Run() for the current or the last run. */
	UFUNCTION(BlueprintPure, Category = "Acoustics")
	float GetElapsedSeconds() const;

	/** Number of Run() calls so far; the HUD uses it to notice a new result. */
	UFUNCTION(BlueprintPure, Category = "Acoustics")
	int32 GetRunCount() const { return RunCount; }

	const FSimulationResult* GetLastResult() const { return LastResult.Get(); }
	TSharedPtr<const FSimulationResult> GetLastResultShared() const { return LastResult; }

	/** The level's manager, or nullptr. */
	static AAcousticSimulationManager* Find(UWorld* World);

	/** The level's manager, spawned at the origin when the level has none. */
	static AAcousticSimulationManager* FindOrSpawn(UWorld* World);

	/** Source position (meters), receivers of the world (point receivers first, then grid tiles) and their bindings; false with a message when one of them is missing. */
	bool CollectSceneActors(FVector3d& OutSource, TArray<FSolverReceiver>& OutReceivers, TArray<FReceiverBinding>& OutBindings, FString& OutError) const;

	/** Indices into the last result's receivers that are point receivers (every receiver when the bindings are unknown). */
	TArray<int32> GetPointReceiverIndices() const;

	/** Pushes the last result to the path / image-source / beam visualizers and to every receiver grid of the level. */
	void UpdateVisualizers();

	/** Solver that actually runs for the current Solver choice (the hybrid falls back to the ray tracer until M8). */
	EAcousticSolver GetEffectiveSolver() const;

	/** Extracts the acoustic geometry of the world (meters, BVH built) and applies WallAbsorptionOverride. */
	bool ExtractScene(FAcousticScene& OutScene, FString& OutError);

	/** Sabine / Eyring for the shoebox room of the level; clears the reference elsewhere. */
	void ComputeAnalyticReference(const FAcousticScene& Scene);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetState(ESimulationState NewState, const FString& Message);
	void FinishRun();

	TSharedPtr<FAcousticScene> SceneForTask;
	TSharedPtr<FSolverProgress> ProgressState;
	TSharedPtr<FSimulationResult> PendingResult;
	UE::Tasks::TTask<void> Task;

	TSharedPtr<const FSimulationResult> LastResult;
	TArray<FReceiverBinding> PendingBindings;
	double RunStartSeconds = 0.0;
	double RunEndSeconds = 0.0;
	int32 RunCount = 0;
};
