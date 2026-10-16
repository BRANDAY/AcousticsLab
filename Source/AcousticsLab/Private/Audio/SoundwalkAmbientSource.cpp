#include "Audio/SoundwalkAmbientSource.h"

#include "AcousticsConstants.h"
#include "AcousticsCore.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"
#include "Sound/SoundBase.h"

ASoundwalkAmbientSource::ASoundwalkAmbientSource()
{
	PrimaryActorTick.bCanEverTick = false;
	Audio = CreateDefaultSubobject<UAudioComponent>(TEXT("Audio"));
	RootComponent = Audio;
	Audio->bAutoActivate = false;
	Audio->bAllowSpatialization = true;
	Audio->bOverrideAttenuation = true;
#if WITH_EDITORONLY_DATA
	Audio->bVisualizeComponent = true;
#endif
}

void ASoundwalkAmbientSource::BeginPlay()
{
	Super::BeginPlay();
	const float Cm = static_cast<float>(Acoustics::CentimetersPerMeter);
	FSoundAttenuationSettings& Attenuation = Audio->AttenuationOverrides;
	Attenuation.bAttenuate = true;
	Attenuation.bSpatialize = true;
	Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation.AttenuationShapeExtents = FVector(InnerRadiusMeters * Cm, 0.f, 0.f);
	Attenuation.FalloffDistance = FalloffMeters * Cm;
	Audio->SetSound(Sound);
	Audio->SetVolumeMultiplier(Volume);
	if (bPlayOutsideSoundwalk)
	{
		SetPlaying(true);
	}
}

void ASoundwalkAmbientSource::SetPlaying(bool bPlay)
{
	if (!Audio || !Sound)
	{
		if (bPlay)
		{
			UE_LOG(LogAcoustics, Warning, TEXT("Ambient source %s has no sound (run Tools/Editor/create_m10_polish.py)"), *GetActorNameOrLabel());
		}
		return;
	}
	if (bPlay && !Audio->IsPlaying())
	{
		Audio->Play(FMath::FRandRange(0.0f, 2.0f)); // different loop phases for sources sharing a clip
		UE_LOG(LogAcoustics, Log, TEXT("Ambient source %s (%s) playing"), *GetActorNameOrLabel(), *Label);
	}
	else if (!bPlay && Audio->IsPlaying())
	{
		Audio->Stop();
	}
}

bool ASoundwalkAmbientSource::IsPlaying() const
{
	return Audio && Audio->IsPlaying();
}

TArray<ASoundwalkAmbientSource*> ASoundwalkAmbientSource::FindAll(UWorld* World)
{
	TArray<ASoundwalkAmbientSource*> Out;
	if (World)
	{
		for (TActorIterator<ASoundwalkAmbientSource> It(World); It; ++It)
		{
			Out.Add(*It);
		}
	}
	return Out;
}
