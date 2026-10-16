// Soundwalk (menu 1.5, SPEC §0, §2.5.7, §3.7; S5): the first-person camera is the live receiver of the auralization.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SoundwalkComponent.generated.h"

class AAcousticSimulationManager;
class APlayerController;
class UWorld;

/**
 * Lives on the player controller in ?mode=Soundwalk. Every UpdateIntervalSeconds it takes the camera position, finds the
 * nearest receiver of the last simulation result (the cavea grid cells and R1..R4) and, when that cell changed, hands its
 * impulse response to the convolution (UAuralizationComponent::SelectReceiver synthesizes it from the cell's histogram on
 * first use). Nothing is traced again: the result of the source stays valid while only the listener moves (SPEC §2.5.7).
 * A small hysteresis and a minimum switch interval keep the response from flickering on a cell border.
 * The ambient sources of the level (ASoundwalkAmbientSource, S5) are started with the walk; N toggles them.
 */
UCLASS(ClassGroup = (Acoustics), meta = (BlueprintSpawnableComponent))
class ACOUSTICSLAB_API USoundwalkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USoundwalkComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Soundwalk", meta = (ClampMin = "0.02", ClampMax = "2.0"))
	float UpdateIntervalSeconds = 0.1f;

	/** A new cell must be this much closer than the current one before the response switches, m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Soundwalk", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float HysteresisMeters = 0.3f;

	/** At most one switch per this interval (each switch rebuilds the convolution), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Soundwalk", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float MinSwitchIntervalSeconds = 0.25f;

	/** R1..R4 take part in the nearest-cell search too (otherwise only the grid cells). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Soundwalk")
	bool bIncludePointReceivers = true;

	/** Below this height (meters, world) the walker has left the cavea over the top: it is put back on its start seat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Soundwalk")
	float FallResetHeightMeters = -5.0f;

	/** Ambient sources play during the walk (key N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Soundwalk")
	bool bAmbientEnabled = true;

	/** Index into the last result's receivers whose response is heard, INDEX_NONE before the first result. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	int32 CurrentReceiver = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	FString CurrentReceiverName;

	/** Distance listener - centre of the current cell, m. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	float DistanceMeters = 0.0f;

	/** Listener (camera) position, meters. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	FVector ListenerMeters = FVector::ZeroVector;

	/** Cavea row under the listener (0 = arena, -1 = outside, INDEX_NONE without an amphitheatre). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	int32 ListenerRow = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	int32 NumSwitches = 0;

	/** Times the walker fell off the cavea and was put back. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	int32 NumRespawns = 0;

	/** Toggles the ambient sources of the world (key N). */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Soundwalk")
	void SetAmbientEnabled(bool bEnabled);

	/** Two lines for the canvas HUD: listener, cell, distance, the cell's C80 / T30, switches, ambient state. */
	UFUNCTION(BlueprintPure, Category = "Acoustics|Soundwalk")
	FString DescribeState() const;

	/**
	 * Where the walker starts: on the tread of the R2 seat of DA_AmphitheatrePositions (row 10), facing the arena centre;
	 * capsule centre CapsuleHalfHeightCm above the tread. False when the level has no amphitheatre positions.
	 */
	static bool ComputeStartTransform(UWorld* World, float CapsuleHalfHeightCm, FTransform& OutTransform);

	/** Index of the receiver nearest to a point among Positions (meters), INDEX_NONE for an empty list. Public for tests. */
	static int32 FindNearest(TConstArrayView<FVector3d> Positions, const FVector3d& Point, double& OutDistance);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Update();
	/** Puts a walker that fell below FallResetHeightMeters back on its start seat (the cavea has no railing). */
	void ResetFallenWalker();
	FVector3d GetListenerMeters() const;

	int32 SeenRunCount = INDEX_NONE;
	int32 NearestCell = INDEX_NONE;
	double LastSwitchSeconds = -1.0e9;
	bool bVariantChosen = false;
	FString WaitingReason;
	TArray<FVector3d> CandidatePositions;
	TArray<int32> CandidateIndices;
};
