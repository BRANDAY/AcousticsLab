#include "Industry/ClickResponseRecorder.h"

#include "AcousticsCore.h"
#include "Audio/AuralizationComponent.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Industry/IndustryAudioActor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#if WITH_EDITOR
#include "Settings/LevelEditorMiscSettings.h"
#endif
#include "Results/ClickResponse.h"
#include "Results/WavFile.h"
#include "Scene/AcousticReceiver.h"
#include "Simulation/AcousticSimulationManager.h"
#include "Simulation/AcousticSimulationSubsystem.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundSubmix.h"
#include "TimerManager.h"

namespace
{
	FAutoConsoleCommandWithWorld GIndustryMeasureCommand(
		TEXT("Acoustics.Industry.MeasureClick"),
		TEXT("Industry levels: record the click response of the plugin at R1..R4 and compute the SPEC 2.3 metrics (compare panel)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			const AIndustryAudioActor* Industry = AIndustryAudioActor::Find(World);
			UClickResponseRecorder* Recorder = Industry ? Industry->FindComponentByClass<UClickResponseRecorder>() : nullptr;
			if (!Recorder || !Recorder->StartMeasurement())
			{
				UE_LOG(LogAcoustics, Warning, TEXT("Acoustics.Industry.MeasureClick: no industry level, or a measurement is running"));
			}
		}));

	const AAcousticReceiver* FindReceiver(UWorld* World, FName ReceiverId)
	{
		for (const AAcousticReceiver* Receiver : AAcousticReceiver::FindAll(World))
		{
			if (Receiver && Receiver->ReceiverId == ReceiverId)
			{
				return Receiver;
			}
		}
		return nullptr;
	}
}

UClickResponseRecorder::UClickResponseRecorder()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AIndustryAudioActor* UClickResponseRecorder::GetIndustry() const
{
	return Cast<AIndustryAudioActor>(GetOwner());
}

const FReceiverResult* UClickResponseRecorder::FindResult(FName ReceiverId) const
{
	const TSharedPtr<FReceiverResult>* Found = Results.Find(ReceiverId);
	return Found ? Found->Get() : nullptr;
}

void UClickResponseRecorder::BeginPlay()
{
	Super::BeginPlay();
	if (bMeasureOnBeginPlay)
	{
		// After the industry actor's own BeginPlay (spatializer, runtime components, listener).
		Schedule(0.5f, &UClickResponseRecorder::BeginReceiver);
		Step = EStep::Settling;
		ReceiverCursor = 0;
		Measurements.Reset();
		bResumePluginVariant = true;
		HoldUnfocusedVolume();
	}
}

void UClickResponseRecorder::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StepTimer);
		if (Step == EStep::Recording)
		{
			if (FAudioDeviceHandle Device = World->GetAudioDevice())
			{
				float Channels = 0.0f;
				float Rate = 0.0f;
				Device->StopRecording(&Device->GetMainSubmixObject(), Channels, Rate);
			}
		}
	}
	Step = EStep::Idle;
	ReleaseUnfocusedVolume();
	Super::EndPlay(EndPlayReason);
}

void UClickResponseRecorder::HoldUnfocusedVolume()
{
	if (SavedUnfocusedVolume < 0.0f)
	{
		// Game: the platform pump applies FApp::GetUnfocusedVolumeMultiplier() while unfocused
		// (WindowsPlatformApplicationMisc.cpp:259). Editor: UEditorEngine::Tick mutes unless "Allow Background Audio"
		// (EditorEngine.cpp:1906-1925). Both are restored in ReleaseUnfocusedVolume; the editor setting is not saved.
		SavedUnfocusedVolume = FApp::GetUnfocusedVolumeMultiplier();
		FApp::SetUnfocusedVolumeMultiplier(1.0f);
#if WITH_EDITOR
		if (GIsEditor)
		{
			ULevelEditorMiscSettings* Settings = GetMutableDefault<ULevelEditorMiscSettings>();
			bSavedBackgroundAudio = Settings->bAllowBackgroundAudio;
			Settings->bAllowBackgroundAudio = true;
		}
#endif
	}
}

void UClickResponseRecorder::ReleaseUnfocusedVolume()
{
	if (SavedUnfocusedVolume >= 0.0f)
	{
		FApp::SetUnfocusedVolumeMultiplier(SavedUnfocusedVolume);
		SavedUnfocusedVolume = -1.0f;
#if WITH_EDITOR
		if (GIsEditor)
		{
			GetMutableDefault<ULevelEditorMiscSettings>()->bAllowBackgroundAudio = bSavedBackgroundAudio;
		}
#endif
	}
}

void UClickResponseRecorder::Schedule(float Seconds, void (UClickResponseRecorder::*Function)())
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(StepTimer, FTimerDelegate::CreateUObject(this, Function), FMath::Max(0.01f, Seconds), false);
	}
}

bool UClickResponseRecorder::StartMeasurement()
{
	AIndustryAudioActor* Industry = GetIndustry();
	if (IsMeasuring() || !Industry || Receivers.Num() == 0 || !GetWorld() || !GetWorld()->GetAudioDevice())
	{
		return false;
	}
	bResumePluginVariant = Industry->IsPluginVariantActive();
	Measurements.Reset();
	ReceiverCursor = 0;
	HoldUnfocusedVolume();
	BeginReceiver();
	return true;
}

void UClickResponseRecorder::BeginReceiver()
{
	AIndustryAudioActor* Industry = GetIndustry();
	if (!Industry || !Receivers.IsValidIndex(ReceiverCursor))
	{
		Finish(TEXT("nothing to measure"));
		return;
	}
	// Silence everything else on the main submix: the plugin clip and the convolution clip of our solvers.
	Industry->DeactivatePluginVariant();
	if (const AAcousticSimulationManager* Manager = AAcousticSimulationManager::Find(GetWorld()))
	{
		if (Manager->Auralization)
		{
			Manager->Auralization->Stop();
		}
	}
	const FName ReceiverId = Receivers[ReceiverCursor];
	if (!Industry->SetListenerReceiver(ReceiverId))
	{
		++ReceiverCursor;
		Receivers.IsValidIndex(ReceiverCursor) ? BeginReceiver() : Finish(TEXT("done (receivers missing)"));
		return;
	}
	Step = EStep::Settling;
	StatusMessage = FString::Printf(TEXT("Click %s: %s settling %.1f s"), *Industry->GetPluginDisplayName(), *ReceiverId.ToString(), SettleSeconds);
	Schedule(SettleSeconds, &UClickResponseRecorder::StartRecording);
}

void UClickResponseRecorder::StartRecording()
{
	AIndustryAudioActor* Industry = GetIndustry();
	FAudioDeviceHandle Device = GetWorld() ? GetWorld()->GetAudioDevice() : FAudioDeviceHandle();
	USoundBase* Click = ClickSound.LoadSynchronous();
	if (!Industry || !Industry->PluginAudio || !Device || !Click)
	{
		Finish(Click ? TEXT("no audio device") : TEXT("click sound missing"));
		return;
	}
	USoundSubmix& Main = Device->GetMainSubmixObject();
	Main.StartRecordingOutput(GetWorld(), RecordSeconds + 1.0f);
	Industry->PluginAudio->Stop();
	Industry->PluginAudio->SetSound(Click);
	Industry->PluginAudio->Play();
	Step = EStep::Recording;
	StatusMessage = FString::Printf(TEXT("Click %s: recording %s for %.1f s"), *Industry->GetPluginDisplayName(), *Receivers[ReceiverCursor].ToString(), RecordSeconds);
	// The recording starts with the pipeline latency before the click; the analysis drops it (onset detection).
	Schedule(RecordSeconds + 0.5f, &UClickResponseRecorder::FinishReceiver);
}

void UClickResponseRecorder::FinishReceiver()
{
	AIndustryAudioActor* Industry = GetIndustry();
	FAudioDeviceHandle Device = GetWorld() ? GetWorld()->GetAudioDevice() : FAudioDeviceHandle();
	if (!Industry || !Device)
	{
		Finish(TEXT("industry actor or audio device gone"));
		return;
	}
	float NumChannels = 0.0f;
	float SampleRate = 0.0f;
	const Audio::FAlignedFloatBuffer& Buffer = Device->StopRecording(&Device->GetMainSubmixObject(), NumChannels, SampleRate);
	const TArray<float> Recording(Buffer.GetData(), Buffer.Num());
	Industry->PluginAudio->Stop();

	const FName ReceiverId = Receivers[ReceiverCursor];
	const AAcousticReceiver* Receiver = FindReceiver(GetWorld(), ReceiverId);
	FClickMeasurementInfo Info;
	Info.ReceiverId = ReceiverId;
	Info.DistanceMeters = Receiver ? FVector::Dist(Receiver->GetActorLocation(), Industry->GetActorLocation()) / Acoustics::CentimetersPerMeter : 0.0f;

	FClickResponseSettings Settings;
	Settings.DirectDistance = Info.DistanceMeters;
	Settings.MaxTimeSeconds = RecordSeconds;
	TSharedPtr<FReceiverResult> Result = MakeShared<FReceiverResult>();
	Result->Name = ReceiverId.ToString();
	Result->Position = Receiver ? FVector3d(Receiver->GetActorLocation() / Acoustics::CentimetersPerMeter) : FVector3d::ZeroVector;
	FString Error;
	int32 Onset = 0;
	const int32 Channels = FMath::RoundToInt32(NumChannels);
	const int32 Rate = FMath::RoundToInt32(SampleRate);
	if (FClickResponse::Analyze(Recording, Channels, Rate, Settings, *Result, Error, &Onset))
	{
		Info.OnsetSeconds = Rate > 0 ? static_cast<float>(Onset) / Rate : 0.0f;
		Info.Metrics = Result->Metrics.ToString();
		Results.Add(ReceiverId, Result);
		if (UAcousticSimulationSubsystem* Subsystem = UAcousticSimulationSubsystem::Get(this))
		{
			Subsystem->StoreIndustryResult(Industry->GetPluginDisplayName(), ReceiverId, Result);
		}
		if (bExportWav)
		{
			Info.WavPath = UAuralizationComponent::MakeExportPath(UGameplayStatics::GetCurrentLevelName(this, true),
				Industry->GetPluginDisplayName().Replace(TEXT(" "), TEXT("")) + TEXT("Click"), ReceiverId.ToString());
			if (!FWavFile::Write(Info.WavPath, Recording, Rate, Channels, EWavSampleFormat::Float32, &Error))
			{
				Info.WavPath = FString::Printf(TEXT("(export failed: %s)"), *Error);
			}
		}
		UE_LOG(LogAcoustics, Log, TEXT("Click %s at %s (%.1f m): %d frames x %d ch at %d Hz, onset after %.3f s, direct energy %.3g; %s; %s"),
			*Industry->GetPluginDisplayName(), *ReceiverId.ToString(), Info.DistanceMeters, Recording.Num() / FMath::Max(1, Channels), Channels, Rate, Info.OnsetSeconds,
			Result->DirectEnergy, *Info.Metrics, *Info.WavPath);
	}
	else
	{
		Info.Metrics = FString::Printf(TEXT("no response: %s"), *Error);
		UE_LOG(LogAcoustics, Warning, TEXT("Click %s at %s: %s (%d samples, %d ch, %d Hz)"), *Industry->GetPluginDisplayName(), *ReceiverId.ToString(), *Error, Recording.Num(), Channels, Rate);
	}
	Measurements.Add(Info);

	++ReceiverCursor;
	if (Receivers.IsValidIndex(ReceiverCursor))
	{
		BeginReceiver();
	}
	else
	{
		Finish(FString::Printf(TEXT("done, %d of %d receivers measured"), Results.Num(), Receivers.Num()));
	}
}

void UClickResponseRecorder::Finish(const FString& Message)
{
	Step = EStep::Idle;
	ReleaseUnfocusedVolume();
	AIndustryAudioActor* Industry = GetIndustry();
	StatusMessage = FString::Printf(TEXT("Click %s: %s"), Industry ? *Industry->GetPluginDisplayName() : TEXT("?"), *Message);
	UE_LOG(LogAcoustics, Log, TEXT("%s"), *StatusMessage);
	if (Industry && Industry->PluginAudio)
	{
		Industry->PluginAudio->SetSound(Industry->Clip.LoadSynchronous());
		if (bResumePluginVariant)
		{
			Industry->ActivatePluginVariant();
		}
	}
}
