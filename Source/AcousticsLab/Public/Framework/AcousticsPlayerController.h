#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AcousticsPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UBenchmarkWidget;
class UCompareWidget;
class UMainMenuWidget;
class USimulationHUDWidget;
class USoundwalkComponent;
struct FInputActionValue;

/**
 * Player controller for every level. Owns the Enhanced Input setup (actions and mapping context are created in C++,
 * no input assets needed), shows the main menu in Level_MainMenu and returns to it on Esc elsewhere.
 * Scene levels get the simulation panel (USimulationHUDWidget); Tab toggles between cursor (UI) and fly mode.
 * The benchmark panel (UBenchmarkWidget, M7) replaces it in ?mode=Benchmark (menu 1.3); B toggles the two in any scene level.
 * The comparison panel (UCompareWidget, M8) replaces it in ?mode=Hybrid (menu 1.4) and ?mode=Compare (menu 3), with the
 * hybrid solver selected; H toggles the comparison and the simulation panel in any scene level.
 * Keys 1-6 select the A/B variant (SPEC §3.7).
 * ?mode=Soundwalk (menu 1.5, M10): the game mode spawns a walker on the cavea, this controller adds a USoundwalkComponent
 * (camera = live receiver), the walk starts without the panel; Tab shows the panel with the cursor, N toggles the
 * ambient sources.
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

	/** Widget class of the benchmark panel (M7). Defaults to the C++ UBenchmarkWidget. */
	UPROPERTY(EditDefaultsOnly, Category = "Acoustics|UI")
	TSubclassOf<UBenchmarkWidget> BenchmarkWidgetClass;

	/** Shows the benchmark panel instead of the simulation panel (true) or the other way round; creates it on first use. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void ShowBenchmarkPanel(bool bShow);

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	bool IsBenchmarkPanelShown() const;

	UBenchmarkWidget* GetBenchmarkPanel() const { return BenchmarkPanel; }

	/** Widget class of the comparison panel (M8). Defaults to the C++ UCompareWidget. */
	UPROPERTY(EditDefaultsOnly, Category = "Acoustics|UI")
	TSubclassOf<UCompareWidget> CompareWidgetClass;

	/** Shows the comparison panel instead of the simulation panel (true) or the other way round; creates it on first use. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void ShowComparePanel(bool bShow);

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	bool IsComparePanelShown() const;

	UCompareWidget* GetComparePanel() const { return ComparePanel; }

	/** Cursor mode: the mouse drives the panel instead of the camera. Tab toggles it; scene levels start in it. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void SetUIMode(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	bool IsUIMode() const { return bUIMode; }

	USimulationHUDWidget* GetSimulationHUD() const { return SimulationHUD; }

	/** The soundwalk of ?mode=Soundwalk, null in the other modes. */
	USoundwalkComponent* GetSoundwalk() const { return Soundwalk; }

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	bool IsSoundwalk() const { return Soundwalk != nullptr; }

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
	void OnToggleBenchmark(const FInputActionValue& Value);
	void OnToggleCompare(const FInputActionValue& Value);
	void OnToggleAmbient(const FInputActionValue& Value);
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
	TObjectPtr<UInputAction> ToggleBenchmarkAction;

	UPROPERTY()
	TObjectPtr<UInputAction> ToggleCompareAction;

	UPROPERTY()
	TObjectPtr<UInputAction> ToggleAmbientAction;

	UPROPERTY()
	TObjectPtr<USoundwalkComponent> Soundwalk;

	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> VariantActions;

	UPROPERTY()
	TObjectPtr<UMainMenuWidget> MainMenuWidget;

	UPROPERTY()
	TObjectPtr<USimulationHUDWidget> SimulationHUD;

	UPROPERTY()
	TObjectPtr<UBenchmarkWidget> BenchmarkPanel;

	UPROPERTY()
	TObjectPtr<UCompareWidget> ComparePanel;

	bool bIsMainMenu = false;
	bool bUIMode = false;
};
