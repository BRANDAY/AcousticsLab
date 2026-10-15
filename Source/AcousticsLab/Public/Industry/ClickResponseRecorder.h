// SPEC §3.8 (M9, optional): impulse response of an industry plugin from a recorded click.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Results/SimulationResult.h"
#include "ClickResponseRecorder.generated.h"

class AIndustryAudioActor;
class USoundBase;

/** Metrics of one receiver measured by the recorder (HUD / log). */
USTRUCT(BlueprintType)
struct FClickMeasurementInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName ReceiverId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	float DistanceMeters = 0.0f;

	/** Recording latency before the onset of the click, s (audio pipeline, not acoustics). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	float OnsetSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FString Metrics;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FString WavPath;
};

/**
 * Lives on AIndustryAudioActor. The plugins have no impulse-response API (SPEC §3.8), so for every receiver it moves the
 * audio listener there, waits for the plugin's simulation to settle, records the main submix
 * (USoundSubmix::StartRecordingOutput, Runtime/Engine/Classes/Sound/SoundSubmix.h:390; the float buffer is taken with
 * FAudioDevice::StopRecording, Runtime/Engine/Public/AudioDevice.h:1404, instead of StopRecordingOutput, which converts to
 * 16 bit and writes asynchronously), plays a 1-sample click (48 kHz) through the plugin source, and turns the recording into
 * an energy histogram and the SPEC §2.3 metrics (FClickResponse). Results go to UAcousticSimulationSubsystem for the
 * industry columns of the compare panel and to Saved/IR/<Level>_<Plugin>Click_<Receiver>.wav.
 */
UCLASS(ClassGroup = (Acoustics), meta = (BlueprintSpawnableComponent))
class ACOUSTICSLAB_API UClickResponseRecorder : public UActorComponent
{
	GENERATED_BODY()

public:
	UClickResponseRecorder();

	/** One-sample click followed by silence (the source keeps playing while the plugin's reflections ring out). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Click")
	TSoftObjectPtr<USoundBase> ClickSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Click")
	TArray<FName> Receivers = { TEXT("R1"), TEXT("R2"), TEXT("R3"), TEXT("R4") };

	/** Wait after moving the listener before recording (the plugins update their simulation asynchronously), s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Click", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float SettleSeconds = 1.5f;

	/** Recording length after the click, s (the histogram length of the metrics). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Click", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float RecordSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Click")
	bool bExportWav = true;

	/** Start a measurement when the level starts (scripted checks). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Click")
	bool bMeasureOnBeginPlay = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	TArray<FClickMeasurementInfo> Measurements;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|State")
	FString StatusMessage;

	/** Measures every receiver in turn (several seconds each); false when a measurement is running or nothing can be measured. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Click")
	bool StartMeasurement();

	UFUNCTION(BlueprintPure, Category = "Acoustics|Click")
	bool IsMeasuring() const { return Step != EStep::Idle; }

	const FReceiverResult* FindResult(FName ReceiverId) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EStep : uint8 { Idle, Settling, Recording };

	AIndustryAudioActor* GetIndustry() const;
	void BeginReceiver();
	void StartRecording();
	void FinishReceiver();
	void Finish(const FString& Message);
	void Schedule(float Seconds, void (UClickResponseRecorder::*Function)());
	/** The engine mutes an unfocused app (BaseEngine.ini [Audio] UnfocusedVolumeMultiplier=0; in the editor unless
	 *  "Allow Background Audio"), which records silence. */
	void HoldUnfocusedVolume();
	void ReleaseUnfocusedVolume();

	EStep Step = EStep::Idle;
	int32 ReceiverCursor = 0;
	bool bResumePluginVariant = false;
	/** Multiplier saved while a measurement runs; negative when nothing is held. */
	float SavedUnfocusedVolume = -1.0f;
	bool bSavedBackgroundAudio = false;
	FTimerHandle StepTimer;
	TMap<FName, TSharedPtr<FReceiverResult>> Results;
};
