// Simulation panel (SPEC §3.6 HUD): solver settings, Run / Stop, metrics of the selected receiver, timing, Sabine / Eyring.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/EditableTextBox.h"
#include "SimulationHUDWidget.generated.h"

class AAcousticSimulationManager;
class UButton;
class UCheckBox;
class UProgressBar;
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
 * slots below, otherwise the widget builds its own panel in C++ (top-right corner), like UMainMenuWidget.
 * It polls the manager every tick (cheap) and rebuilds the metrics text when a run finishes.
 */
UCLASS()
class ACOUSTICSLAB_API USimulationHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	USimulationHUDWidget(const FObjectInitializer& ObjectInitializer);

	void SetManager(AAcousticSimulationManager* InManager);
	AAcousticSimulationManager* GetManager() const { return Manager.Get(); }

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void SelectReceiver(int32 Index);

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	int32 GetSelectedReceiver() const { return SelectedReceiver; }

	/** Reloads the settings fields, the status, the metrics and the reference block from the manager. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void RefreshAll();

	/** Setting fields by id: NumRays, MaxOrder, Seed, ReceiverRadius, BinMs, TMax, AirAbsorption, NumThreads, WallAlpha. */
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

private:
	void BuildDefaultTree();
	void BuildSettingsRows();
	UTextBlock* MakeText(FName Name, int32 FontSize, const FLinearColor& Color, bool bBold = false);
	UButton* MakeButton(FName Name, const FText& Label, const FLinearColor& Color);

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

	void HandleFieldCommitted(FName FieldId, const FText& Text);
	void HandleResultReady(AAcousticSimulationManager* InManager, const FSimulationResult& Result);

	void LoadFieldsFromManager();
	void RefreshStatus();
	void RefreshMetrics();
	void RefreshReference();

	TWeakObjectPtr<AAcousticSimulationManager> Manager;
	FDelegateHandle ResultHandle;

	UPROPERTY()
	TMap<FName, TObjectPtr<USimulationSettingField>> Fields;

	int32 SelectedReceiver = 0;
	int32 ShownRunCount = -1;
	float StatusRefreshAccumulator = 0.0f;
};
