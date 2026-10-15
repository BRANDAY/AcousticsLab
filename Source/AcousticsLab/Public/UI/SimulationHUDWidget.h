// Simulation panel (SPEC §3.6 HUD): solver settings, Run / Stop, metrics of the selected receiver, timing, Sabine / Eyring,
// visualization toggles (ray paths, heatmap metric) and the impulse-response plot.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/EditableTextBox.h"
#include "Simulation/SolverSettings.h"
#include "Types/SlateEnums.h"
#include "SimulationHUDWidget.generated.h"

class AAcousticSimulationManager;
class AReceiverGrid;
class UAuralizationComponent;
class UBeamVisualizerComponent;
class UButton;
class UCheckBox;
class UComboBoxString;
class UHeatmapLegendWidget;
class UImageSourceVisualizerComponent;
class UIRPlotWidget;
class UProgressBar;
class USlider;
class UTextBlock;
class UVerticalBox;
struct FSimulationResult;

DECLARE_DELEGATE_TwoParams(FOnSettingFieldCommitted, FName, const FText&);

/** Text box that knows which setting it edits (UEditableTextBox::OnTextCommitted carries no sender). */
UCLASS()
class USimulationSettingField : public UEditableTextBox
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName FieldId;

	FOnSettingFieldCommitted OnFieldCommitted;

	void BindCommit();

private:
	UFUNCTION()
	void HandleCommitted(const FText& InText, ETextCommit::Type InCommitMethod);
};

/**
 * Logic lives here; the layout is optional. A Blueprint subclass under Content/UI may provide the BindWidgetOptional
 * slots below, otherwise the widget builds its own panels in C++ (settings top-right, impulse response bottom-left),
 * like UMainMenuWidget. It polls the manager every tick (cheap) and rebuilds the metrics text when a run finishes.
 * The receiver selector cycles the point receivers (AAcousticReceiver) only; grid tiles feed the heatmap.
 */
UCLASS()
class ACOUSTICSLAB_API USimulationHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	USimulationHUDWidget(const FObjectInitializer& ObjectInitializer);

	void SetManager(AAcousticSimulationManager* InManager);
	AAcousticSimulationManager* GetManager() const { return Manager.Get(); }

	/** Selects the Index-th point receiver (wraps around). */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void SelectReceiver(int32 Index);

	/** Index of the selected receiver in the result's receiver array. */
	UFUNCTION(BlueprintPure, Category = "Acoustics")
	int32 GetSelectedReceiver() const { return SelectedReceiver; }

	/** Keeps the curve of the selected receiver in plot slot 1 or 2 (alternating) for comparison. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void PinCurrentCurve();

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void ClearPinnedCurves();

	/** Reloads the settings fields, the status, the metrics, the reference block, the plot and the visualization block from the manager. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void RefreshAll();

	UIRPlotWidget* GetIRPlot() const { return IRPlot; }

	/** Setting fields by id: NumRays, MaxOrder, Seed, ReceiverRadius, BinMs, TMax, AirAbsorption, NumThreads, WallAlpha, DrawOrder, ImageOrder, BeamSubdiv, BeamOrder. */
	static TArray<FName> SettingFieldIds();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> SettingsBox;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RunButton;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UButton> StopButton;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PrevReceiverButton;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UButton> NextReceiverButton;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ReceiverText;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MetricsText;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TimingText;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ReferenceText;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> AirAbsorptionCheck;

	/** Solver selector (M5): Ray tracer / Image sources / Beam tracer. */
	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> SolverCombo;

	/** Source selector (M6): the AAcousticSource ids of the level (Level_Refraction has Pool and Lens). */
	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> SourceCombo;

	/** Beam tracer refraction (M6, FSolverSettings::bRefraction) and the note on the apex approximation. */
	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> RefractionCheck;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RefractionNoteText;

	// Visualization block (M3).
	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> PathsCheck;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> OnlyReachingCheck;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> HeatmapCheck;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> MetricCombo;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UHeatmapLegendWidget> HeatmapLegend;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> VisualizationText;

	// Image sources and beams (M5).
	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> ImagesCheck;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> ImagesOnlyValidCheck;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> BeamsCheck;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> BeamsContainingCheck;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<USlider> BeamOrderSlider;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BeamOrderText;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UIRPlotWidget> IRPlot;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PinCurveButton;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ClearCurvesButton;

	// Auralization block (M4).
	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AuralizationText;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlayClipButton;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UButton> StopClipButton;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UButton> DryButton;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RayButton;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BeamButton;

private:
	void BuildDefaultTree();
	void BuildSettingsRows();
	void BuildVisualizationBlock(UVerticalBox* Column);
	void BuildAuralizationBlock(UVerticalBox* Column);
	UTextBlock* MakeText(FName Name, int32 FontSize, const FLinearColor& Color, bool bBold = false);
	UButton* MakeButton(FName Name, const FText& Label, const FLinearColor& Color);
	USimulationSettingField* MakeField(FName Id, float Width);

	UFUNCTION()
	void HandleRunClicked();

	UFUNCTION()
	void HandleStopClicked();

	UFUNCTION()
	void HandlePrevReceiverClicked();

	UFUNCTION()
	void HandleNextReceiverClicked();

	UFUNCTION()
	void HandleAirAbsorptionChanged(bool bChecked);

	UFUNCTION()
	void HandlePathsChanged(bool bChecked);

	UFUNCTION()
	void HandleOnlyReachingChanged(bool bChecked);

	UFUNCTION()
	void HandleHeatmapChanged(bool bChecked);

	UFUNCTION()
	void HandleMetricChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandlePinCurveClicked();

	UFUNCTION()
	void HandleClearCurvesClicked();

	UFUNCTION()
	void HandlePlayClipClicked();

	UFUNCTION()
	void HandleStopClipClicked();

	UFUNCTION()
	void HandleDryClicked();

	UFUNCTION()
	void HandleRayClicked();

	UFUNCTION()
	void HandleBeamClicked();

	UFUNCTION()
	void HandleSolverChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleSourceChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleRefractionChanged(bool bChecked);

	UFUNCTION()
	void HandleImagesChanged(bool bChecked);

	UFUNCTION()
	void HandleImagesOnlyValidChanged(bool bChecked);

	UFUNCTION()
	void HandleBeamsChanged(bool bChecked);

	UFUNCTION()
	void HandleBeamsContainingChanged(bool bChecked);

	UFUNCTION()
	void HandleBeamOrderChanged(float Value);

	void HandleFieldCommitted(FName FieldId, const FText& Text);
	UImageSourceVisualizerComponent* GetImageSourceVisualizer() const;
	UBeamVisualizerComponent* GetBeamVisualizer() const;
	static TArray<EAcousticSolver> SolverOptions();
	void RefreshSourceOptions();
	void HandleResultReady(AAcousticSimulationManager* InManager, const FSimulationResult& Result);
	void SelectVariant(int32 Key);
	UAuralizationComponent* GetAuralization() const;

	void LoadFieldsFromManager();
	void RefreshStatus();
	void RefreshMetrics();
	void RefreshReference();
	void RefreshPlot();
	void RefreshAuralization();
	void RefreshTitle();
	void RefreshVisualization();
	TArray<AReceiverGrid*> FindGrids() const;

	TWeakObjectPtr<AAcousticSimulationManager> Manager;
	FDelegateHandle ResultHandle;

	UPROPERTY()
	TMap<FName, TObjectPtr<USimulationSettingField>> Fields;

	int32 SelectedReceiver = 0;
	int32 ShownRunCount = -1;
	int32 NextPinSlot = 1;
	float StatusRefreshAccumulator = 0.0f;
};
