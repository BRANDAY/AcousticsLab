#include "UI/HeatmapLegendWidget.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Visualization/HeatmapComponent.h"

UHeatmapLegendWidget::UHeatmapLegendWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UHeatmapLegendWidget::SetRange(const FText& InLabel, float InMin, float InMax, const FString& InUnits, bool bInHasData)
{
	Label = InLabel;
	MinValue = InMin;
	MaxValue = InMax;
	Units = InUnits;
	bHasData = bInHasData;
}

int32 UHeatmapLegendWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	constexpr int32 NumSteps = 48;
	const float BarTop = 0.0f;
	const float StepWidth = Size.X / NumSteps;
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		const float T = (Step + 0.5f) / NumSteps;
		FLinearColor Color = UHeatmapComponent::Viridis(T);
		if (!bHasData)
		{
			Color = Color.Desaturate(0.7f) * 0.5f;
			Color.A = 1.0f;
		}
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2f(StepWidth + 0.5f, BarHeight), FSlateLayoutTransform(FVector2f(Step * StepWidth, BarTop))),
			WhiteBrush, ESlateDrawEffect::None, Color);
	}
	++LayerId;

	const float TextY = BarTop + BarHeight + 2.0f;
	const FString MinText = bHasData ? FString::Printf(TEXT("%.1f %s"), MinValue, *Units) : FString(TEXT("-"));
	const FString MaxText = bHasData ? FString::Printf(TEXT("%.1f %s"), MaxValue, *Units) : FString(TEXT("-"));
	const FString LabelText = Label.ToString();
	FSlateDrawElement::MakeText(OutDrawElements, LayerId, AllottedGeometry.ToOffsetPaintGeometry(FVector2f(0.0f, TextY)), MinText, Font, ESlateDrawEffect::None, TextColor);
	const float MaxWidth = static_cast<float>(FontMeasure->Measure(MaxText, Font).X);
	FSlateDrawElement::MakeText(OutDrawElements, LayerId, AllottedGeometry.ToOffsetPaintGeometry(FVector2f(Size.X - MaxWidth, TextY)), MaxText, Font, ESlateDrawEffect::None, TextColor);
	const float LabelWidth = static_cast<float>(FontMeasure->Measure(LabelText, Font).X);
	FSlateDrawElement::MakeText(OutDrawElements, LayerId, AllottedGeometry.ToOffsetPaintGeometry(FVector2f(0.5f * (Size.X - LabelWidth), TextY)), LabelText, Font, ESlateDrawEffect::None, TextColor);
	return LayerId + 1;
}
