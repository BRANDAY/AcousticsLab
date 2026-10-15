// Industry levels (SPEC §3.8): Steam Audio (Valve) and Audio Tracing (Pinpoint) on the amphitheatre scene.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IndustryAudioActor.generated.h"

class UAudioComponent;
class UAuralizationComponent;
class UClickResponseRecorder;
class USoundAttenuation;
class USoundBase;

UENUM(BlueprintType)
enum class EIndustryPlugin : uint8
{
	SteamAudio,
	AudioTracing
};

/** A plugin component created from its class path, with property values in Unreal text form (FProperty::ImportText). */
USTRUCT(BlueprintType)
struct FIndustryComponentSpec
{
	GENERATED_BODY()

	/** e.g. /Script/SteamAudio.SteamAudioSourceComponent; missing classes (plugin not installed) are skipped with a warning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FSoftClassPath ComponentClass;

	/** Property name -> value as text, e.g. bSimulateReflections -> True, ReflectionsType -> REALTIME. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TMap<FName, FString> Properties;
};

/**
 * One per plugin in each industry level (SPEC §3.2 Industry/: SteamAudioLevelSetup and AudioTracingLevelSetup as one
 * class); both industry levels hold a Steam Audio and an Audio Tracing actor, the level's own one starts (bAutoPlay).
 *
 * Both plugins want the project's spatialization slot (SPEC §3.1). UE 5.8 keeps every registered spatializer once a
 * default one is configured and switches at runtime (FAudioDevice::SetCurrentSpatializationPlugin), so the project
 * keeps one configuration and the actor selects its plugin's spatializer whenever its variant is activated; the
 * spatializer of the level start is restored at EndPlay. It owns the plugin-processed audio component (the anechoic
 * clip, routed to the master submix, with the plugin's attenuation asset), follows the acoustic source with SourceId,
 * puts the audio listener on the selected receiver (APlayerController::SetAudioListenerOverride, facing the source) and
 * creates the plugin's runtime components from class paths. The editor button Prepare Level adds the per-mesh plugin
 * components (Steam Audio Geometry, Audio Tracing Sound Material) to every primitive tagged "Acoustic". No compile-time
 * dependency on either plugin.
 *
 * A/B (SPEC §3.7): key 5 = Steam Audio, 6 = Audio Tracing switch between the plugins at runtime (the other plugin's
 * source stops, the spatializer follows) and stop the convolution clip of UAuralizationComponent; keys 1-4 stop the
 * plugin source and hand playback back to the auralization component.
 */
UCLASS(HideCategories = (Rendering, Replication, Collision, Input, HLOD, LOD, Cooking, Physics, Networking))
class ACOUSTICSLAB_API AIndustryAudioActor : public AActor
{
	GENERATED_BODY()

public:
	AIndustryAudioActor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Industry")
	EIndustryPlugin Plugin = EIndustryPlugin::SteamAudio;

	/** A/B key of this plugin (SPEC §3.7): 5 = Steam Audio, 6 = Audio Tracing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Industry", meta = (ClampMin = "5", ClampMax = "6"))
	int32 VariantKey = 5;

	/** Display name of the plugin's spatializer factory (au.spatialization.ListAvailableSpatialPlugins lists them). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Industry")
	FString SpatializationPluginName;

	/** Console variables set at BeginPlay (Audio Tracing: AudioTracing.HardwareRayTracing.MaxReflectionDepth). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Industry")
	TMap<FString, FString> ConsoleVariables;

	/** Anechoic clip routed to the master submix (not through SM_Auralization: the plugin is the room here). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Playback")
	TSoftObjectPtr<USoundBase> Clip;

	/** Attenuation with the plugin's spatialization / occlusion / reverb / source-data-override settings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Playback")
	TSoftObjectPtr<USoundAttenuation> Attenuation;

	/** The actor sits at the AAcousticSource with this id (moved there at BeginPlay). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Playback")
	FName SourceId = TEXT("S1");

	/** Put the audio listener on a receiver instead of the camera, so the plugins are heard at R1..R4 like the solvers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Playback")
	bool bListenerAtReceiver = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Playback")
	FName ListenerReceiverId = TEXT("R1");

	/** Start the plugin variant when the level starts (the level's own plugin; the other one waits for its key). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Playback")
	bool bAutoPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Playback", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float LoopGapSeconds = 0.5f;

	/** Created on this actor at BeginPlay (Steam Audio Source / Listener). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Components")
	TArray<FIndustryComponentSpec> RuntimeComponents;

	/** Added by Prepare Level to the owner of every primitive tagged "Acoustic" (Steam Audio Geometry, Audio Tracing Sound Material). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Components")
	FIndustryComponentSpec MeshComponent;

	/** Attach the mesh component under the tagged primitive (Audio Tracing requires it) instead of only adding it to the actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Components")
	bool bAttachMeshComponentToPrimitive = false;

	/** Property of MeshComponent that receives the plugin material asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Components")
	FName MeshMaterialProperty;

	/** Our material name (tag AcMat:<name>, default Concrete) -> the plugin's material asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Components")
	TMap<FName, FSoftObjectPath> MaterialsByName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics|Industry")
	TObjectPtr<UAudioComponent> PluginAudio;

	/** Optional click measurement of the plugin's response at R1..R4 (SPEC §3.8; console Acoustics.Industry.MeasureClick). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics|Industry")
	TObjectPtr<UClickResponseRecorder> ClickRecorder;

	/** Adds MeshComponent to every primitive tagged "Acoustic" (skips owners that already have one); returns the number added. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Acoustics|Industry")
	int32 PrepareLevel();

	/** Selects the plugin's spatializer and plays its source; stops the other industry actor's source. Keys 5 / 6. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Industry")
	void ActivatePluginVariant();

	/** Stops the plugin source; keys 1-4. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Industry")
	void DeactivatePluginVariant();

	UFUNCTION(BlueprintPure, Category = "Acoustics|Industry")
	bool IsPluginVariantActive() const { return bPluginVariantActive; }

	/** Moves the audio listener to a receiver (R1..R4); returns false when the receiver does not exist. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Industry")
	bool SetListenerReceiver(FName ReceiverId);

	/**
	 * A/B key from the player controller. Keys 5 / 6 activate the industry actor with that VariantKey (switching the
	 * plugin at runtime) and return true; keys 1-4 stop any plugin source and return false so the auralization component
	 * handles them. Returns false when the world has no industry actor.
	 */
	static bool HandleVariantKeyInWorld(UWorld* World, int32 Key, UAuralizationComponent* Auralization);

	/** Spatializer requested / active, ray-tracing state (Audio Tracing), listener receiver, playback; for the HUD and the log. */
	UFUNCTION(BlueprintPure, Category = "Acoustics|Industry")
	FString DescribeState() const;

	UFUNCTION(BlueprintPure, Category = "Acoustics|Industry")
	FString GetPluginDisplayName() const;

	/** The active industry actor of the world (else the level's own one, bAutoPlay), nullptr outside the industry levels. */
	static AIndustryAudioActor* Find(UWorld* World);

	/** Every industry actor of the world, ordered by VariantKey. */
	static TArray<AIndustryAudioActor*> FindAll(UWorld* World);

	/** Creates one component from a spec on Owner (registered, attached to Parent when given); nullptr when the class is missing. */
	static UActorComponent* CreateComponentFromSpec(AActor* Owner, const FIndustryComponentSpec& Spec, USceneComponent* Parent, bool bInstanceComponent, FString& OutError);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SelectSpatializer();
	void ApplyConsoleVariables();
	void FollowSource();
	void CreateRuntimeComponents();
	void ApplyListener();
	void StartClip();

	UFUNCTION()
	void HandleClipFinished();

	/** Device spatializer when the level started; restored at EndPlay. */
	FName LevelStartSpatializer;
	FName ActiveSpatializer;
	bool bSpatializerSelected = false;
	bool bPluginVariantActive = false;
	bool bRayTracingEnabled = false;
	FTimerHandle LoopTimer;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UActorComponent>> CreatedRuntimeComponents;
};
