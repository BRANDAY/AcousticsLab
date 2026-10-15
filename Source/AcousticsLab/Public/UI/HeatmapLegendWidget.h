// Color bar of the heatmap: the viridis gradient with the metric range (SPEC §3.6, HUD metric selector).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HeatmapLegendWidget.generated.h"

/** Paints UHeatmapComponent::Viridis as a horizontal bar with the min / max labels and the metric name. */
UCLASS()
class ACOUSTICSLAB_API UHeatmapLegendWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UHeatmapLegendWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Heatmap")
	void SetRange(const FText& InLabel, float InMin, float InMax, const FString& InUnits, bool bInHasData);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Heatmap")
	FLinearColor TextColor = FLinearColor(0.75f, 0.75f, 0.8f);

	/** Height of the color bar, Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Heatmap", meta = (ClampMin = "4.0", ClampMax = "40.0"))
	float BarHeight = 10.0f;

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY()
	FText Label;

	UPROPERTY()
	float MinValue = 0.0f;

	UPROPERTY()
	float MaxValue = 1.0f;

	UPROPERTY()
	FString Units;

	UPROPERTY()
	bool bHasData = false;
};
