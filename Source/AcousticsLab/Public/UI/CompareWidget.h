// Comparison panel of menu items 1.4 (hybrid beam/ray) and 3 (Compare), SPEC §2.8, §3.6, §3.7, §5 exp. 3 (M8):
// three overlaid impulse responses (pure ray, pure beam up to K, hybrid), a metrics table and A/B listening.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CompareWidget.generated.h"

class AAcousticSimulationManager;
class UButton;
class UIRPlotWidget;
class UProgressBar;
class USimulationSettingField;
class UTextBlock;
struct FHybridResult;

/**
 * Built in C++ like the benchmark panel (no Blueprint layout needed). Run sets the level's simulation manager to the
 * hybrid solver with the panel's K / t_mix / rays / subdivision and starts it; one hybrid run gives all three curves
 * (FHybridResult: pure ray and hybrid share every ray, pure beam is the early part without the t_mix cut). The table
 * columns come from a list: the industry columns of M9 (Steam Audio, Audio Tracing) are listed already and show "M9"
 * until their results exist. Receiver < > switches the plotted receiver and the auralized response; keys / buttons 1-4
 * select Dry / Ray / Beam / Hybrid.
 */
UCLASS()
class ACOUSTICSLAB_API UCompareWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCompareWidget(const FObjectInitializer& ObjectInitializer);

	void SetManager(AAcousticSimulationManager* InManager);

	/** "1.4 Hybrid beam/ray tracing" (Hybrid) or "3. Compare" (Compare) in the title. */
	void SetMode(FName InMode);

	/** Starts a hybrid run of the level's manager with the panel settings. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Compare")
	bool RunHybrid();

	/** Index-th point receiver of the last result (wraps around). */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Compare")
	void SelectReceiver(int32 Index);

	/** Time axis of the plot: the early part (0..EarlyRangeSeconds) or the whole decay. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Compare")
	void SetEarlyZoom(bool bEarly);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Compare", meta = (ClampMin = "0.02", ClampMax = "1.0"))
	float EarlyRangeSeconds = 0.2f;

	/** Monospaced table of the selected receiver (also logged after every run). */
	FString BuildMetricsTable() const;

	/** Exp. 3 / split summary of the selected receiver. */
	FString BuildSplitSummary() const;

	UIRPlotWidget* GetPlot() const { return Plot; }

	static FLinearColor RayColor() { return FLinearColor(0.35f, 0.75f, 1.0f); }
	static FLinearColor BeamColor() { return FLinearColor(1.0f, 0.55f, 0.2f); }
	static FLinearColor HybridColor() { return FLinearColor(0.45f, 1.0f, 0.45f); }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildDefaultTree();
	UTextBlock* MakeText(FName Name, int32 FontSize, const FLinearColor& Color, bool bBold = false);
	UButton* MakeButton(FName Name, const FText& Label, const FLinearColor& Color);
	USimulationSettingField* MakeField(FName Id, float Width);
	void HandleFieldCommitted(FName FieldId, const FText& Text);
	void LoadFields();
	void RefreshStatus();
	void RefreshResult();
	void RefreshAuralization();
	void SelectVariant(int32 Key);
	TSharedPtr<const FHybridResult> CurrentHybrid() const;
	TArray<int32> PointReceivers() const;

	UFUNCTION()
	void HandleRunClicked();

	UFUNCTION()
	void HandleStopClicked();

	UFUNCTION()
	void HandlePrevClicked();

	UFUNCTION()
	void HandleNextClicked();

	UFUNCTION()
	void HandleEarlyClicked();

	UFUNCTION()
	void HandleFullClicked();

	UFUNCTION()
	void HandleDryClicked();

	UFUNCTION()
	void HandleRayClicked();

	UFUNCTION()
	void HandleBeamClicked();

	UFUNCTION()
	void HandleHybridClicked();

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UProgressBar> ProgressBar;

	UPROPERTY()
	TObjectPtr<UTextBlock> ReceiverText;

	UPROPERTY()
	TObjectPtr<UTextBlock> LegendText;

	UPROPERTY()
	TObjectPtr<UTextBlock> TableText;

	UPROPERTY()
	TObjectPtr<UTextBlock> SummaryText;

	UPROPERTY()
	TObjectPtr<UTextBlock> AuralizationText;

	UPROPERTY()
	TObjectPtr<UIRPlotWidget> Plot;

	UPROPERTY()
	TObjectPtr<UButton> RunButton;

	UPROPERTY()
	TObjectPtr<UButton> StopButton;

	UPROPERTY()
	TObjectPtr<UButton> PrevButton;

	UPROPERTY()
	TObjectPtr<UButton> NextButton;

	UPROPERTY()
	TObjectPtr<UButton> EarlyButton;

	UPROPERTY()
	TObjectPtr<UButton> FullButton;

	UPROPERTY()
	TObjectPtr<UButton> DryButton;

	UPROPERTY()
	TObjectPtr<UButton> RayButton;

	UPROPERTY()
	TObjectPtr<UButton> BeamButton;

	UPROPERTY()
	TObjectPtr<UButton> HybridButton;

	UPROPERTY()
	TMap<FName, TObjectPtr<USimulationSettingField>> Fields;

	TWeakObjectPtr<AAcousticSimulationManager> Manager;
	FName Mode;
	int32 SelectedPoint = 0;
	int32 ShownRunCount = -1;
	const void* ShownHybrid = nullptr;
	bool bEarlyZoom = true;
	float RefreshAccumulator = 0.0f;
};
