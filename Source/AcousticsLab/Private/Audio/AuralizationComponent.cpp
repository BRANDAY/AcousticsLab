#include "Audio/AuralizationComponent.h"

#include "AcousticsCore.h"
#include "Components/AudioComponent.h"
#include "EffectConvolutionReverb.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Results/SimulationResult.h"
#include "Results/WavFile.h"
#include "Sound/SoundBase.h"
#include "Simulation/AcousticSimulationManager.h"
#include "Sound/SoundSubmix.h"
#include "SubmixEffects/SubmixEffectConvolutionReverb.h"
#include "TimerManager.h"

namespace
{
	UAuralizationComponent* FindAuralization(UWorld* World, const TCHAR* Command)
	{
		const AAcousticSimulationManager* Manager = AAcousticSimulationManager::Find(World);
		UAuralizationComponent* Auralization = Manager ? Manager->Auralization.Get() : nullptr;
		if (!Auralization)
		{
			UE_LOG(LogAcoustics, Warning, TEXT("%s: no simulation manager with an auralization component in world %s"), Command, *GetNameSafe(World));
		}
		return Auralization;
	}

	// Console commands for scripted checks and for the owner: Acoustics.Auralization.Variant 1|2, .Receiver R3|<index>, .Play, .Stop.
	FAutoConsoleCommandWithWorldAndArgs GAuralizationVariantCommand(
		TEXT("Acoustics.Auralization.Variant"),
		TEXT("A/B variant of the auralization: 1 = Dry, 2 = Ray (convolution)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UAuralizationComponent* Auralization = FindAuralization(World, TEXT("Acoustics.Auralization.Variant"));
			if (!Auralization || Args.Num() < 1)
			{
				return;
			}
			if (Auralization->SetVariantByKey(FCString::Atoi(*Args[0])) && !Auralization->IsPlaying() && Auralization->HasImpulseResponse())
			{
				Auralization->Play();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GAuralizationReceiverCommand(
		TEXT("Acoustics.Auralization.Receiver"),
		TEXT("Receiver whose impulse response is heard: a receiver id (R3) or an index into the result."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UAuralizationComponent* Auralization = FindAuralization(World, TEXT("Acoustics.Auralization.Receiver"));
			if (!Auralization || Args.Num() < 1)
			{
				return;
			}
			int32 Index = INDEX_NONE;
			for (const FAuralizationReceiverInfo& Info : Auralization->ReceiverInfos)
			{
				if (Info.ReceiverName.Equals(Args[0], ESearchCase::IgnoreCase))
				{
					Index = Info.ReceiverIndex;
				}
			}
			if (Index == INDEX_NONE && Args[0].IsNumeric())
			{
				Index = FCString::Atoi(*Args[0]);
			}
			if (Index == INDEX_NONE)
			{
				UE_LOG(LogAcoustics, Warning, TEXT("Acoustics.Auralization.Receiver: no receiver '%s' in the last result"), *Args[0]);
				return;
			}
			UE_LOG(LogAcoustics, Log, TEXT("Acoustics.Auralization.Receiver %s -> index %d: %s"), *Args[0], Index, Auralization->SelectReceiver(Index) ? TEXT("assigned") : TEXT("not assigned"));
		}));

	FAutoConsoleCommandWithWorld GAuralizationPlayCommand(
		TEXT("Acoustics.Auralization.Play"),
		TEXT("Plays the anechoic clip through the auralization submix."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UAuralizationComponent* Auralization = FindAuralization(World, TEXT("Acoustics.Auralization.Play")))
			{
				Auralization->Play();
			}
		}));

	FAutoConsoleCommandWithWorld GAuralizationStopCommand(
		TEXT("Acoustics.Auralization.Stop"),
		TEXT("Stops the anechoic clip."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UAuralizationComponent* Auralization = FindAuralization(World, TEXT("Acoustics.Auralization.Stop")))
			{
				Auralization->Stop();
			}
		}));

	FAutoConsoleCommandWithWorld GAcousticsRunCommand(
		TEXT("Acoustics.Run"),
		TEXT("Starts a simulation run of the level's AAcousticSimulationManager."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (AAcousticSimulationManager* Manager = AAcousticSimulationManager::Find(World))
			{
				Manager->Run();
			}
			else
			{
				UE_LOG(LogAcoustics, Warning, TEXT("Acoustics.Run: no simulation manager in world %s"), *GetNameSafe(World));
			}
		}));
}

UAuralizationComponent::UAuralizationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	AnechoicClip = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Anechoic/A_Anechoic_TestSignal.A_Anechoic_TestSignal")));
	AuralizationSubmix = TSoftObjectPtr<USoundSubmix>(FSoftObjectPath(TEXT("/Game/Audio/SM_Auralization.SM_Auralization")));
	ConvolutionPreset = TSoftObjectPtr<USubmixEffectConvolutionReverbPreset>(FSoftObjectPath(TEXT("/Game/Audio/SFX_AuralizationConvolution.SFX_AuralizationConvolution")));
}

void UAuralizationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (EnsureRouting())
	{
		ApplyVariantRouting();
	}
}

void UAuralizationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stop();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoopTimer);
	}
	// The preset and the submix are shared assets (in PIE also with the editor): leave them as they were loaded.
	if (Preset && CurrentIR)
	{
		Preset->SetImpulseResponse(nullptr);
	}
	if (Submix && bRoutingReady)
	{
		Submix->SetSubmixWetLevel(this, 1.0f);
		Submix->SetSubmixDryLevel(this, 0.0f);
	}
	CurrentIR = nullptr;
	ImpulseResponses.Empty();
	CurrentResult.Reset();
	AssignedResultKey = nullptr;
	Super::EndPlay(EndPlayReason);
}

FString UAuralizationComponent::VariantName(EAuralizationVariant InVariant)
{
	switch (InVariant)
	{
	case EAuralizationVariant::Dry: return TEXT("Dry");
	case EAuralizationVariant::Ray: return TEXT("Ray");
	case EAuralizationVariant::Beam: return TEXT("Beam (M5)");
	case EAuralizationVariant::Hybrid: return TEXT("Hybrid (M8)");
	case EAuralizationVariant::SteamAudio: return TEXT("Steam Audio (M9)");
	case EAuralizationVariant::AudioTracing: return TEXT("Audio Tracing (M9)");
	default: return TEXT("none");
	}
}

EAuralizationVariant UAuralizationComponent::VariantFromKey(int32 Key)
{
	return Key >= 1 && Key <= 6 ? static_cast<EAuralizationVariant>(Key) : EAuralizationVariant::None;
}

FString UAuralizationComponent::MakeExportPath(const FString& InLevelName, const FString& InSolverName, const FString& ReceiverId)
{
	auto Clean = [](const FString& Value)
	{
		FString Out = Value;
		for (TCHAR& Char : Out)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('-') && Char != TEXT('_') && Char != TEXT('.'))
			{
				Char = TEXT('_');
			}
		}
		return Out;
	};
	return FPaths::ProjectSavedDir() / TEXT("IR") / FString::Printf(TEXT("%s_%s_%s.wav"), *Clean(InLevelName), *Clean(InSolverName), *Clean(ReceiverId));
}

FIRSynthesisSettings UAuralizationComponent::MakeSynthesisSettings() const
{
	FIRSynthesisSettings Settings;
	Settings.SampleRate = SampleRate;
	Settings.Seed = static_cast<uint32>(FMath::Max(0, Seed));
	Settings.PeakDbFS = PeakDbFS;
	Settings.TruncateBelowDb = TruncateBelowDb;
	Settings.MaxSeconds = 0.0; // the histogram length (T_max of the run)
	return Settings;
}

float UAuralizationComponent::NormalizationVolumeDbFor(const FSynthesizedIR& IR) const
{
	const double Energy = IR.Energy();
	const double Db = Energy > 0.0 ? -10.0 * FMath::LogX(10.0, Energy) : 0.0;
	return FMath::Clamp(static_cast<float>(Db) + ExtraGainDb, -60.0f, 15.0f);
}

const FAuralizationReceiverInfo* UAuralizationComponent::FindInfo(int32 ReceiverIndex) const
{
	return ReceiverInfos.FindByPredicate([ReceiverIndex](const FAuralizationReceiverInfo& Info) { return Info.ReceiverIndex == ReceiverIndex; });
}

bool UAuralizationComponent::EnsureRouting()
{
	if (bRoutingReady)
	{
		return true;
	}
	if (!Submix)
	{
		Submix = AuralizationSubmix.LoadSynchronous();
	}
	if (!Preset)
	{
		Preset = ConvolutionPreset.LoadSynchronous();
	}
	if (!Submix || !Preset)
	{
		StatusMessage = FString::Printf(TEXT("auralization assets missing (%s, %s)"), *AuralizationSubmix.ToString(), *ConvolutionPreset.ToString());
		if (!bWarnedMissingAssets)
		{
			bWarnedMissingAssets = true;
			UE_LOG(LogAcoustics, Warning, TEXT("Auralization: %s; run Tools/Editor/create_m4_content.py"), *StatusMessage);
		}
		return false;
	}
	bool bInChain = false;
	for (const TObjectPtr<USoundEffectSubmixPreset>& Entry : Submix->SubmixEffectChain)
	{
		bInChain |= Entry.Get() == Preset;
	}
	if (!bInChain)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("Auralization: preset %s is not in the effect chain of %s; the convolution will not be heard"), *Preset->GetPathName(), *Submix->GetPathName());
	}
	bRoutingReady = true;
	return true;
}

UAudioComponent* UAuralizationComponent::EnsureAudioComponent()
{
	if (AudioComponent)
	{
		return AudioComponent;
	}
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	USoundBase* Clip = AnechoicClip.LoadSynchronous();
	if (!Clip)
	{
		StatusMessage = FString::Printf(TEXT("anechoic clip %s not found"), *AnechoicClip.ToString());
		UE_LOG(LogAcoustics, Warning, TEXT("Auralization: %s"), *StatusMessage);
		return nullptr;
	}
	if (Submix && Clip->GetSoundSubmix() != Submix)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("Auralization: the clip %s is routed to %s, not to %s; set its Base Submix or Dry and Ray will sound the same"),
			*Clip->GetPathName(), *GetPathNameSafe(Clip->GetSoundSubmix()), *Submix->GetPathName());
	}
	AudioComponent = NewObject<UAudioComponent>(Owner, TEXT("AuralizationAudio"), RF_Transient);
	AudioComponent->bAutoActivate = false;
	AudioComponent->bAutoDestroy = false;
	AudioComponent->bAllowSpatialization = false;
	AudioComponent->bIsUISound = false;
	AudioComponent->SetupAttachment(Owner->GetRootComponent());
	AudioComponent->RegisterComponent();
	AudioComponent->SetSound(Clip);
	AudioComponent->SetVolumeMultiplier(ClipVolume);
	AudioComponent->OnAudioFinished.AddDynamic(this, &UAuralizationComponent::HandleClipFinished);
	return AudioComponent;
}

void UAuralizationComponent::SetResult(const TSharedPtr<const FSimulationResult>& Result, const TArray<int32>& PointReceiverIndices, const FString& InLevelName, const FString& InSolverName)
{
	ImpulseResponses.Empty();
	ReceiverInfos.Reset();
	CurrentResult = Result;
	AssignedResultKey = nullptr;
	LevelName = InLevelName;
	SolverName = InSolverName;
	if (!Result.IsValid() || !Result->bSuccess)
	{
		StatusMessage = TEXT("no result to auralize");
		return;
	}

	const FIRSynthesisSettings Settings = MakeSynthesisSettings();
	const double Start = FPlatformTime::Seconds();
	for (const int32 Index : PointReceiverIndices)
	{
		if (!Result->Receivers.IsValidIndex(Index))
		{
			continue;
		}
		const FReceiverResult& Receiver = Result->Receivers[Index];
		FSynthesizedIR IR = FIRSynthesizer::SynthesizeForReceiver(Receiver, Settings);
		FAuralizationReceiverInfo& Info = ReceiverInfos.AddDefaulted_GetRef();
		Info.ReceiverIndex = Index;
		Info.ReceiverName = Receiver.Name;
		Info.NumSamples = IR.Samples.Num();
		Info.DurationSeconds = static_cast<float>(IR.DurationSeconds());
		Info.bTruncatedByLevel = IR.bTruncatedByLevel;
		Info.PeakDbFS = static_cast<float>(IR.PeakDbFS());
		Info.NormalizationVolumeDb = NormalizationVolumeDbFor(IR);
		if (!IR.IsValid())
		{
			UE_LOG(LogAcoustics, Warning, TEXT("Auralization: receiver %s has no energy, no impulse response"), *Receiver.Name);
			continue;
		}
		if (bExportWav)
		{
			const FString Path = MakeExportPath(LevelName, SolverName, Receiver.Name);
			FString Error;
			const EWavSampleFormat Format = ExportFormat == EIRExportFormat::PCM16 ? EWavSampleFormat::PCM16 : EWavSampleFormat::Float32;
			if (FWavFile::Write(Path, IR.Samples, IR.SampleRate, 1, Format, &Error))
			{
				Info.WavPath = Path;
				UE_LOG(LogAcoustics, Log, TEXT("Auralization: %s -> %s: %s"), *Receiver.Name, *FPaths::ConvertRelativePathToFull(Path), *IR.Describe());
			}
			else
			{
				UE_LOG(LogAcoustics, Warning, TEXT("Auralization: could not write %s: %s"), *Path, *Error);
			}
		}
		else
		{
			UE_LOG(LogAcoustics, Log, TEXT("Auralization: %s: %s"), *Receiver.Name, *IR.Describe());
		}
		ImpulseResponses.Add(Index, MoveTemp(IR));
	}
	UE_LOG(LogAcoustics, Log, TEXT("Auralization: %d impulse responses for %s / %s synthesized in %.1f ms"), ImpulseResponses.Num(), *LevelName, *SolverName, (FPlatformTime::Seconds() - Start) * 1000.0);

	int32 Target = INDEX_NONE;
	if (ImpulseResponses.Contains(SelectedReceiver))
	{
		Target = SelectedReceiver;
	}
	else
	{
		for (const FAuralizationReceiverInfo& Info : ReceiverInfos)
		{
			if (ImpulseResponses.Contains(Info.ReceiverIndex))
			{
				Target = Info.ReceiverIndex;
				break;
			}
		}
	}
	if (Target == INDEX_NONE)
	{
		StatusMessage = TEXT("no receiver with energy");
		return;
	}
	AssignImpulseResponse(Target);
	if (bAutoPlayOnResult && !IsPlaying())
	{
		Play();
	}
}

bool UAuralizationComponent::SelectReceiver(int32 ResultReceiverIndex)
{
	if (!ImpulseResponses.Contains(ResultReceiverIndex))
	{
		SelectedReceiver = ResultReceiverIndex; // used when the next result arrives
		return false;
	}
	if (ResultReceiverIndex == SelectedReceiver && CurrentIR && AssignedResultKey == CurrentResult.Get())
	{
		return true; // already assigned
	}
	return AssignImpulseResponse(ResultReceiverIndex);
}

bool UAuralizationComponent::AssignImpulseResponse(int32 ReceiverIndex)
{
	const FSynthesizedIR* IR = ImpulseResponses.Find(ReceiverIndex);
	SelectedReceiver = ReceiverIndex;
	if (!IR || !IR->IsValid())
	{
		StatusMessage = FString::Printf(TEXT("no impulse response for receiver %d"), ReceiverIndex);
		return false;
	}
	if (!EnsureRouting())
	{
		return false;
	}
	const FAuralizationReceiverInfo* Info = FindInfo(ReceiverIndex);
	const FString ReceiverName = Info ? Info->ReceiverName : FString::FromInt(ReceiverIndex);

	UAudioImpulseResponse* NewIR = NewObject<UAudioImpulseResponse>(this, NAME_None, RF_Transient);
	NewIR->ImpulseResponse = IR->Samples;
	NewIR->NumChannels = 1;
	NewIR->SampleRate = IR->SampleRate;
	NewIR->NormalizationVolumeDb = NormalizationVolumeDbFor(*IR);
	NewIR->bTrueStereo = false;
	Preset->SetImpulseResponse(NewIR); // copies NormalizationVolumeDb into the settings and rebuilds the convolution on the audio render thread
	CurrentIR = NewIR;
	AssignedResultKey = CurrentResult.Get();

	StatusMessage = FString::Printf(TEXT("IR of %s assigned"), *ReceiverName);
	if (bLogIRAssignment)
	{
		int32 ChainIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Submix->SubmixEffectChain.Num(); ++Index)
		{
			if (Submix->SubmixEffectChain[Index].Get() == Preset)
			{
				ChainIndex = Index;
			}
		}
		UE_LOG(LogAcoustics, Log, TEXT("Auralization: IR of %s assigned to preset %s (submix %s, effect %d of %d): %d samples at %d Hz = %.3f s (cut by %s), peak %.2f dBFS, normalization %.1f dB; variant %s"),
			*ReceiverName, *Preset->GetName(), *Submix->GetName(), ChainIndex, Submix->SubmixEffectChain.Num(),
			NewIR->ImpulseResponse.Num(), NewIR->SampleRate, IR->DurationSeconds(), IR->bTruncatedByLevel ? TEXT("level") : TEXT("length"),
			IR->PeakDbFS(), NewIR->NormalizationVolumeDb, *VariantName(Variant));
	}
	return true;
}

void UAuralizationComponent::SetVariant(EAuralizationVariant NewVariant)
{
	Variant = NewVariant;
	ApplyVariantRouting();
}

bool UAuralizationComponent::SetVariantByKey(int32 Key)
{
	const EAuralizationVariant Requested = VariantFromKey(Key);
	if (Requested == EAuralizationVariant::Dry || Requested == EAuralizationVariant::Ray)
	{
		SetVariant(Requested);
		return true;
	}
	UE_LOG(LogAcoustics, Log, TEXT("Auralization: variant %d (%s) has no signal path yet; routing stays %s"), Key, *VariantName(Requested), *VariantName(Variant));
	return false;
}

void UAuralizationComponent::ApplyVariantRouting()
{
	if (!EnsureRouting())
	{
		return;
	}
	// Wet = output of the effect chain (the convolution), dry = the submix input as it came in.
	const bool bWet = Variant == EAuralizationVariant::Ray;
	const float WetLevel = bWet ? 1.0f : 0.0f;
	const float DryLevel = bWet ? 0.0f : 1.0f;
	Submix->SetSubmixWetLevel(this, WetLevel);
	Submix->SetSubmixDryLevel(this, DryLevel);
	UE_LOG(LogAcoustics, Log, TEXT("Auralization: variant %s -> submix %s wet %.0f, dry %.0f (%s)"), *VariantName(Variant), *Submix->GetName(), WetLevel, DryLevel,
		bWet ? TEXT("convolved clip") : TEXT("clip as recorded"));
}

void UAuralizationComponent::Play()
{
	UAudioComponent* Audio = EnsureAudioComponent();
	if (!Audio)
	{
		return;
	}
	bWantPlaying = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoopTimer);
	}
	Audio->SetVolumeMultiplier(ClipVolume);
	Audio->Play();
	UE_LOG(LogAcoustics, Log, TEXT("Auralization: playing %s (variant %s, receiver %d)"), *GetNameSafe(Audio->Sound), *VariantName(Variant), SelectedReceiver);
}

void UAuralizationComponent::Stop()
{
	bWantPlaying = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoopTimer);
	}
	if (AudioComponent && AudioComponent->IsPlaying())
	{
		AudioComponent->Stop();
		UE_LOG(LogAcoustics, Log, TEXT("Auralization: stopped"));
	}
}

bool UAuralizationComponent::IsPlaying() const
{
	return bWantPlaying && AudioComponent && (AudioComponent->IsPlaying() || LoopTimer.IsValid());
}

void UAuralizationComponent::HandleClipFinished()
{
	if (!bWantPlaying)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (LoopGapSeconds <= 0.0f)
	{
		RestartClip();
		return;
	}
	World->GetTimerManager().SetTimer(LoopTimer, this, &UAuralizationComponent::RestartClip, LoopGapSeconds, false);
}

void UAuralizationComponent::RestartClip()
{
	if (bWantPlaying && AudioComponent)
	{
		AudioComponent->Play();
	}
}

FString UAuralizationComponent::DescribeState() const
{
	const FAuralizationReceiverInfo* Info = FindInfo(SelectedReceiver);
	FString Text = FString::Printf(TEXT("Variant: %s   Receiver: %s   Clip: %s"), *VariantName(Variant), Info ? *Info->ReceiverName : TEXT("-"),
		IsPlaying() ? TEXT("playing") : (AudioComponent ? TEXT("stopped") : TEXT("not started")));
	if (!bRoutingReady)
	{
		Text += TEXT("\n") + (StatusMessage.IsEmpty() ? FString(TEXT("Routing: submix / preset not loaded")) : StatusMessage);
		return Text;
	}
	if (Info && CurrentIR)
	{
		Text += FString::Printf(TEXT("\nIR: %.3f s (%d samples, cut at %s), peak %.1f dBFS, normalization %.1f dB"), Info->DurationSeconds, Info->NumSamples,
			Info->bTruncatedByLevel ? *FString::Printf(TEXT("%.0f dB"), TruncateBelowDb) : TEXT("T_max"), Info->PeakDbFS, Info->NormalizationVolumeDb);
		if (!Info->WavPath.IsEmpty())
		{
			FString Relative = Info->WavPath;
			FPaths::MakePathRelativeTo(Relative, *FPaths::ProjectDir());
			Text += TEXT("\nWAV: ") + Relative;
		}
	}
	else
	{
		Text += TEXT("\nIR: none yet (Run the simulation)");
	}
	return Text;
}
