#include "UI/IRPlotWidget.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Results/RoomMetrics.h"
#include "Results/SimulationResult.h"
#include "Styling/CoreStyle.h"

namespace
{
	/** Bins without energy sit at RoomAcoustics::SilenceDb; anything below this is "no line". */
	constexpr float NoLineDb = -500.0f;

	float NiceStep(float Range, int32 TargetDivisions)
	{
		const float Raw = FMath::Max(Range / FMath::Max(1, TargetDivisions), 1e-6f);
		const float Magnitude = FMath::Pow(10.0f, FMath::FloorToFloat(FMath::LogX(10.0f, Raw)));
		const float Normalized = Raw / Magnitude;
		const float Step = Normalized < 1.5f ? 1.0f : (Normalized < 3.5f ? 2.0f : (Normalized < 7.5f ? 5.0f : 10.0f));
		return Step * Magnitude;
	}
}

UIRPlotWidget::UIRPlotWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Curves.SetNum(MaxCurves);
}

FLinearColor UIRPlotWidget::SlotColor(int32 Slot)
{
	switch (Slot)
	{
	case 0: return FLinearColor(0.35f, 0.85f, 1.0f);
	case 1: return FLinearColor(1.0f, 0.55f, 0.2f);
	default: return FLinearColor(0.6f, 1.0f, 0.4f);
	}
}

int32 UIRPlotWidget::NumCurves() const
{
	int32 Count = 0;
	for (const FIRPlotCurve& Curve : Curves)
	{
		Count += Curve.bValid ? 1 : 0;
	}
	return Count;
}

void UIRPlotWidget::SetCurve(int32 CurveSlot, const FReceiverResult& Receiver, const FString& Label, const FLinearColor& Color)
{
	if (!Curves.IsValidIndex(CurveSlot))
	{
		return;
	}
	FIRPlotCurve& Curve = Curves[CurveSlot];
	Curve = FIRPlotCurve();
	Curve.Label = Label;
	Curve.Color = Color;

	const FEnergyHistogram& Histogram = Receiver.Histogram;
	if (!Histogram.IsInitialized())
	{
		return;
	}
	Curve.BinSeconds = static_cast<float>(Histogram.BinSeconds);
	Curve.DirectArrivalSeconds = static_cast<float>(Receiver.DirectArrivalSeconds);

	const double Peak = Histogram.PeakEnergy(0);
	Curve.EnergyDb.SetNumUninitialized(Histogram.NumBins);
	for (int32 Bin = 0; Bin < Histogram.NumBins; ++Bin)
	{
		Curve.EnergyDb[Bin] = static_cast<float>(RoomAcoustics::LevelDb(Histogram.At(Bin, 0), Peak));
	}

	TArray<double> Schroeder;
	RoomAcoustics::SchroederCurveDb(Histogram, 0, Schroeder);
	Curve.SchroederDb.SetNumUninitialized(Schroeder.Num());
	for (int32 Bin = 0; Bin < Schroeder.Num(); ++Bin)
	{
		Curve.SchroederDb[Bin] = static_cast<float>(Schroeder[Bin]);
	}
	Curve.UsableEndBin = RoomAcoustics::UsableEndBin(Schroeder, Histogram.BinSeconds);

	const FBandMetrics& Band = Receiver.Metrics.Band(0);
	Curve.T30 = static_cast<float>(Band.T30);
	Curve.bT30Valid = Band.bT30Valid;
	Curve.EDT = static_cast<float>(Band.EDT);
	Curve.bEDTValid = Band.bEDTValid;
	Curve.C80 = static_cast<float>(Band.C80);
	Curve.C50 = static_cast<float>(Band.C50);
	Curve.bValid = Peak > 0.0;
}

void UIRPlotWidget::ClearCurve(int32 CurveSlot)
{
	if (Curves.IsValidIndex(CurveSlot))
	{
		Curves[CurveSlot] = FIRPlotCurve();
	}
}

void UIRPlotWidget::ClearAll()
{
	for (FIRPlotCurve& Curve : Curves)
	{
		Curve = FIRPlotCurve();
	}
}

float UIRPlotWidget::ResolveTimeRange() const
{
	if (TimeRangeSeconds > 0.0f)
	{
		return TimeRangeSeconds;
	}
	float End = 0.0f;
	for (const FIRPlotCurve& Curve : Curves)
	{
		if (!Curve.bValid)
		{
			continue;
		}
		const int32 LastBin = Curve.UsableEndBin != INDEX_NONE ? Curve.UsableEndBin : Curve.EnergyDb.Num() - 1;
		End = FMath::Max(End, (LastBin + 1) * Curve.BinSeconds * 1.05f);
	}
	return End > 0.0f ? FMath::Max(End, 0.1f) : 1.0f;
}

int32 UIRPlotWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateFontInfo BoldFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	// Background.
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), WhiteBrush, ESlateDrawEffect::None, BackgroundColor);
	++LayerId;

	const float Left = PlotPadding.Left;
	const float Top = PlotPadding.Top;
	const float Right = FMath::Max(Left + 10.0f, Size.X - PlotPadding.Right);
	const float Bottom = FMath::Max(Top + 10.0f, Size.Y - PlotPadding.Bottom);
	const float Width = Right - Left;
	const float Height = Bottom - Top;
	const float TimeRange = ResolveTimeRange();
	const float Floor = FMath::Min(LevelFloorDb, -10.0f);

	auto XOf = [&](float Time) { return Left + FMath::Clamp(Time / TimeRange, 0.0f, 1.0f) * Width; };
	auto YOf = [&](float Db) { return Top + FMath::Clamp(-Db / -Floor, 0.0f, 1.0f) * Height; };
	auto Line = [&](const FVector2f& A, const FVector2f& B, const FLinearColor& Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), TArray<FVector2f>{ A, B }, ESlateDrawEffect::None, Color, true, Thickness);
	};
	auto Text = [&](const FString& String, float X, float Y, const FLinearColor& Color, const FSlateFontInfo& InFont, bool bRightAlign = false)
	{
		if (bRightAlign)
		{
			X -= static_cast<float>(FontMeasure->Measure(String, InFont).X);
		}
		FSlateDrawElement::MakeText(OutDrawElements, LayerId, AllottedGeometry.ToOffsetPaintGeometry(FVector2f(X, Y)), String, InFont, ESlateDrawEffect::None, Color);
	};

	// Grid: 10 dB rows, "nice" time columns.
	for (float Db = 0.0f; Db >= Floor - 0.01f; Db -= 10.0f)
	{
		const float Y = YOf(Db);
		Line(FVector2f(Left, Y), FVector2f(Right, Y), GridColor, 1.0f);
		Text(FString::Printf(TEXT("%.0f"), Db), Left - 4.0f, Y - 7.0f, TextColor, Font, true);
	}
	const float TimeStep = NiceStep(TimeRange, 6);
	for (float Time = 0.0f; Time <= TimeRange + 1e-4f; Time += TimeStep)
	{
		const float X = XOf(Time);
		Line(FVector2f(X, Top), FVector2f(X, Bottom), GridColor, 1.0f);
		Text(TimeStep >= 1.0f ? FString::Printf(TEXT("%.0f s"), Time) : FString::Printf(TEXT("%.2g s"), Time), X - 8.0f, Bottom + 3.0f, TextColor, Font);
	}
	Text(TEXT("dB"), 2.0f, Top - 14.0f, TextColor, Font);
	++LayerId;

	// Markers: -5 / -35 dB (T30 range) and direct + 50 / 80 ms (C50 / C80 windows) of the first valid curve.
	const FIRPlotCurve* Reference = nullptr;
	for (const FIRPlotCurve& Curve : Curves)
	{
		if (Curve.bValid)
		{
			Reference = &Curve;
			break;
		}
	}
	if (bShowMarkers && Reference)
	{
		for (const float Db : { -5.0f, -35.0f })
		{
			if (Db >= Floor)
			{
				const float Y = YOf(Db);
				Line(FVector2f(Left, Y), FVector2f(Right, Y), MarkerColor, 1.0f);
				Text(FString::Printf(TEXT("%.0f dB"), Db), Right - 2.0f, Y - 12.0f, MarkerColor, Font, true);
			}
		}
		for (const float Window : { 0.050f, 0.080f })
		{
			const float Time = Reference->DirectArrivalSeconds + Window;
			if (Time <= TimeRange)
			{
				const float X = XOf(Time);
				Line(FVector2f(X, Top), FVector2f(X, Bottom), MarkerColor, 1.0f);
				Text(FString::Printf(TEXT("%.0f ms"), Window * 1000.0f), X + 2.0f, Top + 1.0f, MarkerColor, Font);
			}
		}
		++LayerId;
	}

	// Curves: one sample per pixel column (max energy in the column, Schroeder value at the column).
	const int32 NumColumns = FMath::Max(2, FMath::FloorToInt(Width));
	int32 LegendRow = 0;
	for (const FIRPlotCurve& Curve : Curves)
	{
		if (!Curve.bValid || Curve.EnergyDb.Num() == 0)
		{
			continue;
		}
		const int32 NumBins = Curve.EnergyDb.Num();
		const float BinsPerColumn = TimeRange / Curve.BinSeconds / NumColumns;

		if (bShowEnergy)
		{
			FLinearColor EnergyColor = Curve.Color;
			EnergyColor.A = 0.55f;
			TArray<FVector2f> Points;
			auto FlushPoints = [&]()
			{
				if (Points.Num() >= 2)
				{
					FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, EnergyColor, false, 1.0f);
				}
				else if (Points.Num() == 1)
				{
					FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), TArray<FVector2f>{ Points[0], Points[0] + FVector2f(1.0f, 0.0f) }, ESlateDrawEffect::None, EnergyColor, false, 1.0f);
				}
				Points.Reset();
			};
			for (int32 Column = 0; Column < NumColumns; ++Column)
			{
				const int32 FirstBin = FMath::FloorToInt(Column * BinsPerColumn);
				const int32 EndBin = FMath::Max(FirstBin + 1, FMath::FloorToInt((Column + 1) * BinsPerColumn));
				if (FirstBin >= NumBins)
				{
					break;
				}
				float MaxDb = NoLineDb;
				for (int32 Bin = FirstBin; Bin < EndBin && Bin < NumBins; ++Bin)
				{
					MaxDb = FMath::Max(MaxDb, Curve.EnergyDb[Bin]);
				}
				if (MaxDb <= NoLineDb || MaxDb < Floor)
				{
					FlushPoints();
					continue;
				}
				Points.Emplace(Left + Column + 0.5f, YOf(MaxDb));
			}
			FlushPoints();
		}

		if (bShowSchroeder && Curve.SchroederDb.Num() == NumBins)
		{
			TArray<FVector2f> Points;
			Points.Reserve(NumColumns);
			for (int32 Column = 0; Column < NumColumns; ++Column)
			{
				const int32 Bin = FMath::FloorToInt(Column * BinsPerColumn);
				if (Bin >= NumBins)
				{
					break;
				}
				const float Db = Curve.SchroederDb[Bin];
				if (Db <= NoLineDb || Db < Floor)
				{
					break;
				}
				Points.Emplace(Left + Column + 0.5f, YOf(Db));
			}
			if (Points.Num() >= 2)
			{
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Curve.Color, true, 2.0f);
			}
		}

		// Legend line: label, T30, C80.
		const FString Legend = FString::Printf(TEXT("%s   T30 %s   C80 %.1f dB"), *Curve.Label,
			Curve.bT30Valid ? *FString::Printf(TEXT("%.2f s"), Curve.T30) : TEXT("n/a"), Curve.C80);
		Text(Legend, Left + 6.0f, Top + 4.0f + LegendRow * 14.0f, Curve.Color, BoldFont);
		++LegendRow;
	}
	if (LegendRow == 0)
	{
		Text(TEXT("Impulse response: run a simulation"), Left + 6.0f, Top + 4.0f, TextColor, BoldFont);
	}
	return LayerId + 1;
}
