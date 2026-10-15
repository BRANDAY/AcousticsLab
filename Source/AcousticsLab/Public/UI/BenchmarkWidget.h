// Benchmark panel of menu item 1.3 (SPEC §2.7, §0): the parallel beam tracer on the current level for every thread
// count x {BVH on, BVH off} x repetitions, a table, the speedup chart and the CSV export (M7).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Results/BenchmarkResult.h"
#include "Simulation/AcousticsBenchmarkLibrary.h"
#include "Tasks/Task.h"
#include "BenchmarkWidget.generated.h"

class UButton;
class UCheckBox;
class UProgressBar;
class USimulationSettingField;
class UTextBlock;

/**
 * Speedup T1/Tn against the thread count, painted in NativePaint with FSlateDrawElement::MakeLines: one curve per BVH
 * mode, the ideal linear speedup as reference, a marker at the physical core count.
 */
UCLASS()
class ACOUSTICSLAB_API UBenchmarkChartWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetData(const TArray<FBenchmarkCell>& InCells, const TArray<int32>& InThreadCounts, int32 InPhysicalCores);
	void Clear();

	static FLinearColor ModeColor(bool bUseBVH);

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TArray<FBenchmarkCell> Cells;
	TArray<int32> ThreadCounts;
	int32 PhysicalCores = 0;
};

/**
 * Built in C++ like the simulation panel (no Blueprint layout needed). Run prepares the job on the game thread (scene,
 * source, receivers exactly as a beam-tracer Run of the level's simulation manager) and runs the matrix in a background
 * task; the table and the chart fill in live after every run; Stop cancels between and inside runs; when the matrix
 * finishes the CSVs go to Saved/Benchmarks/<Level>_<timestamp>.csv automatically (Export CSV writes them again, also for
 * a cancelled matrix).
 */
UCLASS()
class ACOUSTICSLAB_API UBenchmarkWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UBenchmarkWidget(const FObjectInitializer& ObjectInitializer);

	/** The options the next Run uses (the panel fields edit them). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Benchmark")
	FBeamBenchmarkOptions Options;

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Benchmark")
	bool StartBenchmark();

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Benchmark")
	void StopBenchmark();

	UFUNCTION(BlueprintPure, Category = "Acoustics|Benchmark")
	bool IsBenchmarkRunning() const { return Running.IsValid(); }

	/** Writes the CSVs of the last (or the running) matrix; false when there is nothing to write. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Benchmark")
	bool ExportCsv();

	const FBenchmarkResult& GetLastResult() const { return LastResult; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** Shared with the background task. */
	struct FRunningBenchmark
	{
		FBeamBenchmarkJob Job;
		FBenchmarkProgress Progress;
		FBenchmarkResult Result;
		std::atomic<bool> bDone{ false };
	};

	void BuildDefaultTree();
	UTextBlock* MakeText(FName Name, int32 FontSize, const FLinearColor& Color, bool bBold = false);
	UButton* MakeButton(FName Name, const FText& Label, const FLinearColor& Color);
	USimulationSettingField* MakeField(FName Id, float Width);
	UCheckBox* MakeCheck(FName Name, bool bChecked);

	void HandleFieldCommitted(FName FieldId, const FText& Text);
	void RefreshFields();
	void RefreshSceneText();
	void RefreshResults(const TArray<FBenchmarkRun>& Runs, const TArray<int32>& Threads, const FBenchmarkInfo& Info);
	void FinishBenchmark();

	UFUNCTION()
	void HandleRunClicked();

	UFUNCTION()
	void HandleStopClicked();

	UFUNCTION()
	void HandleExportClicked();

	UFUNCTION()
	void HandleBvhOnChanged(bool bChecked);

	UFUNCTION()
	void HandleBvhOffChanged(bool bChecked);

	UFUNCTION()
	void HandleGridsChanged(bool bChecked);

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UTextBlock> SceneText;

	UPROPERTY()
	TObjectPtr<UTextBlock> ThreadsText;

	UPROPERTY()
	TObjectPtr<UTextBlock> TableText;

	UPROPERTY()
	TObjectPtr<UTextBlock> CsvText;

	UPROPERTY()
	TObjectPtr<UProgressBar> ProgressBar;

	UPROPERTY()
	TObjectPtr<UButton> RunButton;

	UPROPERTY()
	TObjectPtr<UButton> StopButton;

	UPROPERTY()
	TObjectPtr<UButton> ExportButton;

	UPROPERTY()
	TObjectPtr<UCheckBox> BvhOnCheck;

	UPROPERTY()
	TObjectPtr<UCheckBox> BvhOffCheck;

	UPROPERTY()
	TObjectPtr<UCheckBox> GridsCheck;

	UPROPERTY()
	TObjectPtr<UBenchmarkChartWidget> Chart;

	UPROPERTY()
	TMap<FName, TObjectPtr<USimulationSettingField>> Fields;

	TSharedPtr<FRunningBenchmark> Running;
	UE::Tasks::FTask Task;
	FBenchmarkResult LastResult;
	FString LastLevelName;
	int32 ShownRuns = -1;
	float RefreshAccumulator = 0.0f;
};
