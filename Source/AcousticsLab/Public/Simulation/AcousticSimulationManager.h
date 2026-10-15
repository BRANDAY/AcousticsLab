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

	/** Only the ray tracer runs in M2; other choices fall back to it with a note in StatusMessage. */
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

	/** Source position (meters) and receivers of the world; false with a message when one of them is missing. */
	bool CollectSceneActors(FVector3d& OutSource, TArray<FSolverReceiver>& OutReceivers, FString& OutError) const;

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
	double RunStartSeconds = 0.0;
	double RunEndSeconds = 0.0;
	int32 RunCount = 0;
};
