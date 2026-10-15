#include "Industry/IndustryAudioActor.h"

#include "AcousticsCore.h"
#include "Audio/AuralizationComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Industry/ClickResponseRecorder.h"
#include "Kismet/GameplayStatics.h"
#include "RenderUtils.h"
#include "Scene/AcousticReceiver.h"
#include "Scene/AcousticSource.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	const FName AcousticTag(TEXT("Acoustic"));
	const FString MaterialTagPrefix(TEXT("AcMat:"));
	const FName DefaultMaterialName(TEXT("Concrete"));

	/** Material name of a primitive: AcMat:<name> on the component, then on its actor, else Concrete (as the scene extractor). */
	FName MaterialNameOf(const UPrimitiveComponent* Primitive)
	{
		auto FromTags = [](const TArray<FName>& Tags) -> FName
		{
			for (const FName& Tag : Tags)
			{
				const FString Text = Tag.ToString();
				if (Text.StartsWith(MaterialTagPrefix))
				{
					return FName(*Text.RightChop(MaterialTagPrefix.Len()));
				}
			}
			return NAME_None;
		};
		FName Name = FromTags(Primitive->ComponentTags);
		if (Name.IsNone() && Primitive->GetOwner())
		{
			Name = FromTags(Primitive->GetOwner()->Tags);
		}
		return Name.IsNone() ? DefaultMaterialName : Name;
	}

	bool IsAcousticPrimitive(const UPrimitiveComponent* Primitive)
	{
		return Primitive && (Primitive->ComponentHasTag(AcousticTag) || (Primitive->GetOwner() && Primitive->GetOwner()->ActorHasTag(AcousticTag)));
	}

	FAutoConsoleCommandWithWorldAndArgs GIndustryReceiverCommand(
		TEXT("Acoustics.Industry.Receiver"),
		TEXT("Industry levels: put the audio listener on a receiver (R1..R4)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AIndustryAudioActor* Industry = AIndustryAudioActor::Find(World))
			{
				if (Args.Num() > 0)
				{
					Industry->SetListenerReceiver(FName(*Args[0]));
				}
			}
		}));

	FAutoConsoleCommandWithWorld GIndustryStateCommand(
		TEXT("Acoustics.Industry.State"),
		TEXT("Industry levels: log the plugin, spatializer, ray-tracing and listener state."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (const AIndustryAudioActor* Industry = AIndustryAudioActor::Find(World))
			{
				UE_LOG(LogAcoustics, Log, TEXT("%s"), *Industry->DescribeState());
			}
			else
			{
				UE_LOG(LogAcoustics, Log, TEXT("Industry: no industry actor in this level"));
			}
		}));
}

AIndustryAudioActor::AIndustryAudioActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	PluginAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("PluginAudio"));
	PluginAudio->SetupAttachment(RootComponent);
	PluginAudio->bAutoActivate = false;
	PluginAudio->bAllowSpatialization = true;
	ClickRecorder = CreateDefaultSubobject<UClickResponseRecorder>(TEXT("ClickRecorder"));
}

FString AIndustryAudioActor::GetPluginDisplayName() const
{
	return Plugin == EIndustryPlugin::SteamAudio ? TEXT("Steam Audio") : TEXT("Audio Tracing");
}

AIndustryAudioActor* AIndustryAudioActor::Find(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AIndustryAudioActor> It(World);
	return It ? *It : nullptr;
}

UActorComponent* AIndustryAudioActor::CreateComponentFromSpec(AActor* Owner, const FIndustryComponentSpec& Spec, USceneComponent* Parent, bool bInstanceComponent, FString& OutError)
{
	UClass* Class = Spec.ComponentClass.TryLoadClass<UActorComponent>();
	if (!Owner || !Class)
	{
		OutError = FString::Printf(TEXT("component class %s not found (plugin not installed or not enabled?)"), *Spec.ComponentClass.ToString());
		return nullptr;
	}
	const FName BaseName(*Class->GetName().Replace(TEXT("Component"), TEXT("")));
	UActorComponent* Component = NewObject<UActorComponent>(Owner, Class, MakeUniqueObjectName(Owner, Class, BaseName), RF_Transactional);
	for (const TPair<FName, FString>& Pair : Spec.Properties)
	{
		FProperty* Property = Class->FindPropertyByName(Pair.Key);
		if (!Property || !Property->ImportText_InContainer(*Pair.Value, Component, Component, PPF_None))
		{
			OutError += FString::Printf(TEXT("%s%s.%s = %s not applied"), OutError.IsEmpty() ? TEXT("") : TEXT("; "), *Class->GetName(), *Pair.Key.ToString(), *Pair.Value);
		}
	}
	if (USceneComponent* Scene = Cast<USceneComponent>(Component))
	{
		USceneComponent* AttachTo = Parent ? Parent : Owner->GetRootComponent();
		if (AttachTo)
		{
			Scene->SetupAttachment(AttachTo);
		}
	}
	if (bInstanceComponent)
	{
		Component->CreationMethod = EComponentCreationMethod::Instance;
		Owner->AddInstanceComponent(Component);
	}
	if (Owner->GetWorld())
	{
		Component->RegisterComponent();
	}
	return Component;
}

int32 AIndustryAudioActor::PrepareLevel()
{
	UWorld* World = GetWorld();
	if (!World || MeshComponent.ComponentClass.IsNull())
	{
		return 0;
	}
	UClass* Class = MeshComponent.ComponentClass.TryLoadClass<UActorComponent>();
	if (!Class)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("Industry %s: Prepare Level skipped, %s not found"), *GetPluginDisplayName(), *MeshComponent.ComponentClass.ToString());
		return 0;
	}
	int32 NumAdded = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == this)
		{
			continue;
		}
		TArray<UPrimitiveComponent*> Primitives;
		Actor->GetComponents<UPrimitiveComponent>(Primitives);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			if (!IsAcousticPrimitive(Primitive))
			{
				continue;
			}
			// One plugin component per primitive when attached under it, one per actor otherwise.
			bool bExists = false;
			TArray<UActorComponent*> Existing;
			Actor->GetComponents(Class, Existing);
			for (UActorComponent* Component : Existing)
			{
				const USceneComponent* Scene = Cast<USceneComponent>(Component);
				bExists |= !bAttachMeshComponentToPrimitive || (Scene && Scene->GetAttachParent() == Primitive);
			}
			if (bExists)
			{
				continue;
			}
			FIndustryComponentSpec Spec = MeshComponent;
			const FName MaterialName = MaterialNameOf(Primitive);
			if (!MeshMaterialProperty.IsNone())
			{
				const FSoftObjectPath* Material = MaterialsByName.Find(MaterialName);
				if (!Material)
				{
					Material = MaterialsByName.Find(DefaultMaterialName);
				}
				if (Material)
				{
					Spec.Properties.Add(MeshMaterialProperty, Material->ToString());
				}
			}
			Actor->Modify();
			FString Error;
			if (CreateComponentFromSpec(Actor, Spec, bAttachMeshComponentToPrimitive ? Primitive : nullptr, true, Error))
			{
				++NumAdded;
				UE_LOG(LogAcoustics, Log, TEXT("Industry %s: %s added to %s (%s, material %s)%s%s"), *GetPluginDisplayName(), *Class->GetName(), *Actor->GetActorNameOrLabel(),
					*Primitive->GetName(), *MaterialName.ToString(), Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error);
			}
			else
			{
				UE_LOG(LogAcoustics, Warning, TEXT("Industry %s: %s"), *GetPluginDisplayName(), *Error);
			}
		}
	}
	UE_LOG(LogAcoustics, Log, TEXT("Industry %s: Prepare Level added %d %s components"), *GetPluginDisplayName(), NumAdded, *Class->GetName());
	return NumAdded;
}

void AIndustryAudioActor::BeginPlay()
{
	Super::BeginPlay();
	bRayTracingEnabled = IsRayTracingEnabled();
	FollowSource();
	SelectSpatializer();
	ApplyConsoleVariables();
	CreateRuntimeComponents();
	if (PluginAudio)
	{
		PluginAudio->Sound = Clip.LoadSynchronous();
		if (USoundAttenuation* Settings = Attenuation.LoadSynchronous())
		{
			PluginAudio->AttenuationSettings = Settings;
		}
		PluginAudio->OnAudioFinished.AddDynamic(this, &AIndustryAudioActor::HandleClipFinished);
	}
	ApplyListener();
	if (Plugin == EIndustryPlugin::AudioTracing && !bRayTracingEnabled)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("Industry Audio Tracing: hardware ray tracing is not available (r.RayTracing off or GPU without DXR tier 1.1); the plugin cannot trace and the source plays without its reflections"));
	}
	UE_LOG(LogAcoustics, Log, TEXT("%s"), *DescribeState());
	if (bAutoPlay)
	{
		ActivatePluginVariant();
	}
}

void AIndustryAudioActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoopTimer);
	}
	bPluginVariantActive = false;
	if (PluginAudio)
	{
		PluginAudio->OnAudioFinished.RemoveAll(this);
		PluginAudio->Stop();
	}
	if (APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		Controller->ClearAudioListenerOverride();
	}
	// The spatializer is a device-wide choice: hand the previous one back (the menu and the Sikora levels expect the default).
	if (bSpatializerSelected && !PreviousSpatializer.IsNone() && PreviousSpatializer != ActiveSpatializer)
	{
		UGameplayStatics::SetActiveSpatialPluginByName(this, PreviousSpatializer);
		UE_LOG(LogAcoustics, Log, TEXT("Industry %s: spatializer restored to %s"), *GetPluginDisplayName(), *PreviousSpatializer.ToString());
	}
	Super::EndPlay(EndPlayReason);
}

void AIndustryAudioActor::SelectSpatializer()
{
	PreviousSpatializer = UGameplayStatics::GetActiveSpatialPluginName(this);
	ActiveSpatializer = PreviousSpatializer;
	const TArray<FName> Available = UGameplayStatics::GetAvailableSpatialPluginNames(this);
	FString List;
	for (const FName& Name : Available)
	{
		List += (List.IsEmpty() ? TEXT("") : TEXT(", ")) + Name.ToString();
	}
	if (SpatializationPluginName.IsEmpty())
	{
		UE_LOG(LogAcoustics, Log, TEXT("Industry %s: no spatializer requested, active %s (available: %s)"), *GetPluginDisplayName(), *ActiveSpatializer.ToString(), *List);
		return;
	}
	bSpatializerSelected = UGameplayStatics::SetActiveSpatialPluginByName(this, FName(*SpatializationPluginName));
	ActiveSpatializer = UGameplayStatics::GetActiveSpatialPluginName(this);
	UE_LOG(LogAcoustics, Log, TEXT("Industry %s: spatializer %s -> %s (%s; available: %s)"), *GetPluginDisplayName(), *PreviousSpatializer.ToString(), *ActiveSpatializer.ToString(),
		bSpatializerSelected ? TEXT("switched") : TEXT("requested one not registered"), *List);
	if (!bSpatializerSelected)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("Industry %s: spatializer \"%s\" is not registered with the audio device (plugin missing, or no spatialization plugin set in the project settings)"),
			*GetPluginDisplayName(), *SpatializationPluginName);
	}
}

void AIndustryAudioActor::ApplyConsoleVariables()
{
	for (const TPair<FString, FString>& Pair : ConsoleVariables)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Pair.Key))
		{
			Variable->Set(*Pair.Value, ECVF_SetByCode);
			UE_LOG(LogAcoustics, Log, TEXT("Industry %s: %s = %s"), *GetPluginDisplayName(), *Pair.Key, *Variable->GetString());
		}
		else
		{
			UE_LOG(LogAcoustics, Warning, TEXT("Industry %s: console variable %s not found (plugin not loaded?)"), *GetPluginDisplayName(), *Pair.Key);
		}
	}
}

void AIndustryAudioActor::FollowSource()
{
	for (const AAcousticSource* Source : AAcousticSource::FindAll(GetWorld()))
	{
		if (Source && Source->SourceId == SourceId)
		{
			SetActorLocation(Source->GetActorLocation());
			return;
		}
	}
	UE_LOG(LogAcoustics, Warning, TEXT("Industry %s: source %s not found, the plugin source stays at %s"), *GetPluginDisplayName(), *SourceId.ToString(), *GetActorLocation().ToString());
}

void AIndustryAudioActor::CreateRuntimeComponents()
{
	for (const FIndustryComponentSpec& Spec : RuntimeComponents)
	{
		FString Error;
		UActorComponent* Component = CreateComponentFromSpec(this, Spec, nullptr, false, Error);
		if (Component)
		{
			CreatedRuntimeComponents.Add(Component);
		}
		if (Component && Error.IsEmpty())
		{
			UE_LOG(LogAcoustics, Log, TEXT("Industry %s: runtime component %s created"), *GetPluginDisplayName(), *Spec.ComponentClass.GetAssetName());
		}
		else
		{
			UE_LOG(LogAcoustics, Warning, TEXT("Industry %s: runtime component %s %s: %s"), *GetPluginDisplayName(), *Spec.ComponentClass.GetAssetName(),
				Component ? TEXT("created") : TEXT("not created"), *Error);
		}
	}
}

bool AIndustryAudioActor::SetListenerReceiver(FName ReceiverId)
{
	for (const AAcousticReceiver* Receiver : AAcousticReceiver::FindAll(GetWorld()))
	{
		if (Receiver && Receiver->ReceiverId == ReceiverId)
		{
			ListenerReceiverId = ReceiverId;
			ApplyListener();
			return true;
		}
	}
	UE_LOG(LogAcoustics, Warning, TEXT("Industry %s: receiver %s not found"), *GetPluginDisplayName(), *ReceiverId.ToString());
	return false;
}

void AIndustryAudioActor::ApplyListener()
{
	APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Controller)
	{
		return;
	}
	if (!bListenerAtReceiver)
	{
		Controller->ClearAudioListenerOverride();
		return;
	}
	for (const AAcousticReceiver* Receiver : AAcousticReceiver::FindAll(GetWorld()))
	{
		if (Receiver && Receiver->ReceiverId == ListenerReceiverId)
		{
			// Face the source, so the binaural rendering puts the direct sound in front of the listener.
			const FVector Location = Receiver->GetActorLocation();
			const FRotator Rotation = (GetActorLocation() - Location).Rotation();
			Controller->SetAudioListenerOverride(nullptr, Location, Rotation);
			UE_LOG(LogAcoustics, Log, TEXT("Industry %s: listener at %s %s cm, facing the source (%.1f m)"), *GetPluginDisplayName(), *ListenerReceiverId.ToString(), *Location.ToCompactString(),
				FVector::Dist(Location, GetActorLocation()) / 100.0);
			return;
		}
	}
	UE_LOG(LogAcoustics, Warning, TEXT("Industry %s: listener receiver %s not found, the camera stays the listener"), *GetPluginDisplayName(), *ListenerReceiverId.ToString());
}

void AIndustryAudioActor::ActivatePluginVariant()
{
	bPluginVariantActive = true;
	StartClip();
}

void AIndustryAudioActor::DeactivatePluginVariant()
{
	bPluginVariantActive = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoopTimer);
	}
	if (PluginAudio && PluginAudio->IsPlaying())
	{
		PluginAudio->Stop();
	}
	UE_LOG(LogAcoustics, Log, TEXT("Industry %s: plugin source stopped"), *GetPluginDisplayName());
}

void AIndustryAudioActor::StartClip()
{
	if (!PluginAudio || !PluginAudio->Sound)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("Industry %s: no clip to play"), *GetPluginDisplayName());
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoopTimer);
	}
	PluginAudio->Play();
	UE_LOG(LogAcoustics, Log, TEXT("Industry %s: playing %s through %s (attenuation %s)"), *GetPluginDisplayName(), *GetNameSafe(PluginAudio->Sound), *ActiveSpatializer.ToString(),
		*GetNameSafe(PluginAudio->AttenuationSettings));
}

void AIndustryAudioActor::HandleClipFinished()
{
	if (!bPluginVariantActive)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(LoopTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (bPluginVariantActive)
			{
				StartClip();
			}
		}), FMath::Max(0.01f, LoopGapSeconds), false);
	}
}

bool AIndustryAudioActor::HandleVariantKey(int32 Key, UAuralizationComponent* Auralization)
{
	if (Key == VariantKey)
	{
		if (Auralization)
		{
			Auralization->Stop();
		}
		ActivatePluginVariant();
		UE_LOG(LogAcoustics, Log, TEXT("Industry %s: A/B variant %d -> plugin source at %s, listener %s"), *GetPluginDisplayName(), Key, *SourceId.ToString(), *ListenerReceiverId.ToString());
		return true;
	}
	if (Key == 5 || Key == 6)
	{
		UE_LOG(LogAcoustics, Log, TEXT("Industry: variant %d is not available in this level (%s level: key %d)"), Key, *GetPluginDisplayName(), VariantKey);
		return true;
	}
	if (bPluginVariantActive)
	{
		DeactivatePluginVariant();
	}
	return false;
}

FString AIndustryAudioActor::DescribeState() const
{
	FString State = FString::Printf(TEXT("Industry %s (key %d): spatializer %s"), *GetPluginDisplayName(), VariantKey, *ActiveSpatializer.ToString());
	if (!SpatializationPluginName.IsEmpty() && ActiveSpatializer != FName(*SpatializationPluginName))
	{
		State += FString::Printf(TEXT(" (requested %s: not registered)"), *SpatializationPluginName);
	}
	if (Plugin == EIndustryPlugin::AudioTracing)
	{
		State += bRayTracingEnabled ? TEXT(", hardware ray tracing on") : TEXT(", hardware ray tracing OFF (GPU without DXR 1.1 or r.RayTracing=0): Audio Tracing cannot trace here");
	}
	State += FString::Printf(TEXT(", source %s, listener %s, %s, clip %s, attenuation %s, %d runtime components"),
		*SourceId.ToString(), bListenerAtReceiver ? *ListenerReceiverId.ToString() : TEXT("camera"),
		bPluginVariantActive ? TEXT("playing") : TEXT("stopped"), *GetNameSafe(PluginAudio ? PluginAudio->Sound : nullptr),
		*GetNameSafe(PluginAudio ? PluginAudio->AttenuationSettings : nullptr), CreatedRuntimeComponents.Num());
	return State;
}
