// Impulse-response plot (SPEC §3.6): energy per bin in dB, Schroeder curve, -5 / -35 dB and 50 / 80 ms markers.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "IRPlotWidget.generated.h"

struct FReceiverResult;

/** One plotted receiver: copied out of the result so the widget never dereferences a stale result. */
USTRUCT()
struct FIRPlotCurve
{
	GENERATED_BODY()

	UPROPERTY()
	FString Label;

	UPROPERTY()
	FLinearColor Color = FLinearColor::White;

	UPROPERTY()
	bool bValid = false;

	UPROPERTY()
	float BinSeconds = 0.001f;

	UPROPERTY()
	float DirectArrivalSeconds = 0.0f;

	/** Per bin: 10 log10(E / E_peak); bins without energy are below RoomAcoustics::SilenceDb / 2 and are not drawn. */
	UPROPERTY()
	TArray<float> EnergyDb;

	/** Schroeder backward integral per bin, dB. */
	UPROPERTY()
	TArray<float> SchroederDb;

	/** Last bin before the truncation plunge (RoomAcoustics::UsableEndBin), INDEX_NONE for an empty histogram. */
	UPROPERTY()
	int32 UsableEndBin = INDEX_NONE;

	UPROPERTY()
	float T30 = 0.0f;

	UPROPERTY()
	bool bT30Valid = false;

	UPROPERTY()
	float EDT = 0.0f;

	UPROPERTY()
	bool bEDTValid = false;

	UPROPERTY()
	float C80 = 0.0f;

	UPROPERTY()
	float C50 = 0.0f;
};

/**
 * Painted with FSlateDrawElement::MakeLines / MakeText in NativePaint; up to MaxCurves overlaid curves (hybrid /
 * compare panels) in slots 0..2. Time axis from 0 to TimeRangeSeconds (0 = the usable decay of the longest curve),
 * level axis from 0 down to LevelFloorDb.
 */
UCLASS()
class ACOUSTICSLAB_API UIRPlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxCurves = 3;

	UIRPlotWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Plot", meta = (ClampMin = "-120.0", ClampMax = "-10.0"))
	float LevelFloorDb = -60.0f;

	/** 0 = automatic from the curves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Plot", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float TimeRangeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Plot")
	bool bShowEnergy = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Plot")
	bool bShowSchroeder = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Plot")
	bool bShowMarkers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Plot")
	FLinearColor BackgroundColor = FLinearColor(0.02f, 0.025f, 0.035f, 0.85f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Plot")
	FLinearColor GridColor = FLinearColor(0.3f, 0.3f, 0.35f, 0.6f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Plot")
	FLinearColor TextColor = FLinearColor(0.75f, 0.75f, 0.8f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Plot")
	FLinearColor MarkerColor = FLinearColor(0.95f, 0.8f, 0.35f, 0.8f);

	/** Left / top / right / bottom space for the axis labels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics|Plot")
	FMargin PlotPadding = FMargin(40.f, 18.f, 12.f, 20.f);

	/** Copies the histogram of a receiver into a slot (0..MaxCurves - 1). */
	void SetCurve(int32 CurveSlot, const FReceiverResult& Receiver, const FString& Label, const FLinearColor& Color);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Plot")
	void ClearCurve(int32 CurveSlot);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Plot")
	void ClearAll();

	UFUNCTION(BlueprintPure, Category = "Acoustics|Plot")
	bool HasCurve(int32 CurveSlot) const { return Curves.IsValidIndex(CurveSlot) && Curves[CurveSlot].bValid; }

	UFUNCTION(BlueprintPure, Category = "Acoustics|Plot")
	int32 NumCurves() const;

	const FIRPlotCurve* GetCurve(int32 CurveSlot) const { return Curves.IsValidIndex(CurveSlot) ? &Curves[CurveSlot] : nullptr; }

	/** Default colors of slots 0..2. */
	static FLinearColor SlotColor(int32 Slot);

	/** Time axis end used for painting (TimeRangeSeconds or the automatic range). */
	float ResolveTimeRange() const;

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY()
	TArray<FIRPlotCurve> Curves;
};
