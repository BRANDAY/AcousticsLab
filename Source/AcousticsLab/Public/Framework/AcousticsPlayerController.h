#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AcousticsPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UMainMenuWidget;
class USimulationHUDWidget;
struct FInputActionValue;

/**
 * Player controller for every level. Owns the Enhanced Input setup (actions and mapping context are created in C++,
 * no input assets needed), shows the main menu in Level_MainMenu and returns to it on Esc elsewhere.
 * Scene levels get the simulation panel (USimulationHUDWidget); Tab toggles between cursor (UI) and fly mode.
 * Keys 1-6 select the A/B variant (SPEC §3.7).
 */
UCLASS()
class ACOUSTICSLAB_API AAcousticsPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AAcousticsPlayerController();

	/** Widget class of the main menu. Defaults to the C++ UMainMenuWidget; a Blueprint layout under Content/UI may override it. */
	UPROPERTY(EditDefaultsOnly, Category = "Acoustics|UI")
	TSubclassOf<UMainMenuWidget> MainMenuWidgetClass;

	/** Widget class of the simulation panel shown in scene levels. Defaults to the C++ USimulationHUDWidget. */
	UPROPERTY(EditDefaultsOnly, Category = "Acoustics|UI")
	TSubclassOf<USimulationHUDWidget> SimulationHUDWidgetClass;

	/** Cursor mode: the mouse drives the panel instead of the camera. Tab toggles it; scene levels start in it. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void SetUIMode(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	bool IsUIMode() const { return bUIMode; }

	USimulationHUDWidget* GetSimulationHUD() const { return SimulationHUD; }

	/** Mode requested through the level URL (?mode=Beam|Refraction|Benchmark|Hybrid|Soundwalk|Compare); None when absent. */
	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	FName LevelMode;

	/** A/B variant chosen with keys 1-6 (1 Dry, 2 Ray, 3 Beam, 4 Hybrid, 5 Steam Audio, 6 Audio Tracing); 0 = none yet. */
	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	int32 SelectedVariant = 0;

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void ReturnToMainMenu();

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	bool IsInMainMenu() const { return bIsMainMenu; }

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void BuildInputAssets();
	void ShowMainMenu();
	void ShowSimulationHUD();

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnMoveUp(const FInputActionValue& Value);
	void OnMenu(const FInputActionValue& Value);
	void OnToggleUI(const FInputActionValue& Value);
	void OnSelectVariant(const FInputActionValue& Value, int32 Variant);

	UPROPERTY()
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY()
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY()
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY()
	TObjectPtr<UInputAction> MoveUpAction;

	UPROPERTY()
	TObjectPtr<UInputAction> MenuAction;

	UPROPERTY()
	TObjectPtr<UInputAction> ToggleUIAction;

	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> VariantActions;

	UPROPERTY()
	TObjectPtr<UMainMenuWidget> MainMenuWidget;

	UPROPERTY()
	TObjectPtr<USimulationHUDWidget> SimulationHUD;

	bool bIsMainMenu = false;
	bool bUIMode = false;
};
