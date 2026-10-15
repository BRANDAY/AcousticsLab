#include "UI/SimulationHUDWidget.h"

#include "AcousticsCore.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Results/SimulationResult.h"
#include "Scene/ReceiverGrid.h"
#include "Simulation/AcousticSimulationManager.h"
#include "Styling/CoreStyle.h"
#include "UI/HeatmapLegendWidget.h"
#include "UI/IRPlotWidget.h"
#include "Visualization/HeatmapComponent.h"
#include "Visualization/RayPathVisualizerComponent.h"

#define LOCTEXT_NAMESPACE "AcousticsSimulationHUD"

namespace
{
	const FName FieldNumRays(TEXT("NumRays"));
	const FName FieldMaxOrder(TEXT("MaxOrder"));
	const FName FieldSeed(TEXT("Seed"));
	const FName FieldReceiverRadius(TEXT("ReceiverRadius"));
	const FName FieldBinMs(TEXT("BinMs"));
	const FName FieldTMax(TEXT("TMax"));
	const FName FieldAirAbsorption(TEXT("AirAbsorption"));
	const FName FieldNumThreads(TEXT("NumThreads"));
	const FName FieldWallAlpha(TEXT("WallAlpha"));
	const FName FieldDrawOrder(TEXT("DrawOrder"));

	const FLinearColor PanelColor(0.02f, 0.025f, 0.035f, 0.85f);
	const FLinearColor LabelColor(0.75f, 0.75f, 0.8f);
	const FLinearColor ValueColor(0.95f, 0.95f, 0.95f);
	const FLinearColor AccentColor(0.95f, 0.8f, 0.35f);
	const FLinearColor MutedColor(0.55f, 0.55f, 0.6f);

	FString DecayText(double Value, bool bValid)
	{
		return bValid ? FString::Printf(TEXT("%.3f s"), Value) : FString(TEXT("n/a"));
	}
}

void USimulationSettingField::BindCommit()
{
	OnTextCommitted.AddUniqueDynamic(this, &USimulationSettingField::HandleCommitted);
}

void USimulationSettingField::HandleCommitted(const FText& InText, ETextCommit::Type InCommitMethod)
{
	if (InCommitMethod == ETextCommit::OnEnter || InCommitMethod == ETextCommit::OnUserMovedFocus)
	{
		OnFieldCommitted.ExecuteIfBound(FieldId, InText);
	}
}

USimulationHUDWidget::USimulationHUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TArray<FName> USimulationHUDWidget::SettingFieldIds()
{
	return { FieldNumRays, FieldMaxOrder, FieldSeed, FieldReceiverRadius, FieldBinMs, FieldTMax, FieldAirAbsorption, FieldNumThreads, FieldWallAlpha, FieldDrawOrder };
}

void USimulationHUDWidget::SetManager(AAcousticSimulationManager* InManager)
{
	if (AAcousticSimulationManager* Previous = Manager.Get())
	{
		Previous->OnResultReady.Remove(ResultHandle);
	}
	Manager = InManager;
	ShownRunCount = -1;
	if (InManager)
	{
		ResultHandle = InManager->OnResultReady.AddUObject(this, &USimulationHUDWidget::HandleResultReady);
	}
	RefreshAll();
}

TSharedRef<SWidget> USimulationHUDWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultTree();
	}
	return Super::RebuildWidget();
}

void USimulationHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (RunButton)
	{
		RunButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandleRunClicked);
	}
	if (StopButton)
	{
		StopButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandleStopClicked);
	}
	if (PrevReceiverButton)
	{
		PrevReceiverButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandlePrevReceiverClicked);
	}
	if (NextReceiverButton)
	{
		NextReceiverButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandleNextReceiverClicked);
	}
	if (AirAbsorptionCheck)
	{
		AirAbsorptionCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleAirAbsorptionChanged);
	}
	if (PathsCheck)
	{
		PathsCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandlePathsChanged);
	}
	if (OnlyReachingCheck)
	{
		OnlyReachingCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleOnlyReachingChanged);
	}
	if (HeatmapCheck)
	{
		HeatmapCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleHeatmapChanged);
	}
	if (MetricCombo)
	{
		if (MetricCombo->GetOptionCount() == 0)
		{
			for (const EHeatmapMetric Metric : UHeatmapComponent::AllMetrics())
			{
				MetricCombo->AddOption(UHeatmapComponent::MetricLabel(Metric).ToString());
			}
		}
		MetricCombo->OnSelectionChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleMetricChanged);
	}
	if (PinCurveButton)
	{
		PinCurveButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandlePinCurveClicked);
	}
	if (ClearCurvesButton)
	{
		ClearCurvesButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandleClearCurvesClicked);
	}
	if (SettingsBox && Fields.IsEmpty())
	{
		BuildSettingsRows();
	}
	RefreshAll();
}

void USimulationHUDWidget::NativeDestruct()
{
	if (AAcousticSimulationManager* Current = Manager.Get())
	{
		Current->OnResultReady.Remove(ResultHandle);
	}
	Super::NativeDestruct();
}

void USimulationHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	StatusRefreshAccumulator += InDeltaTime;
	if (StatusRefreshAccumulator >= 0.1f)
	{
		StatusRefreshAccumulator = 0.0f;
		RefreshStatus();
	}
}

UTextBlock* USimulationHUDWidget::MakeText(FName Name, int32 FontSize, const FLinearColor& Color, bool bBold)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Text->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FontSize));
	Text->SetColorAndOpacity(FSlateColor(Color));
	return Text;
}

UButton* USimulationHUDWidget::MakeButton(FName Name, const FText& Label, const FLinearColor& Color)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	Button->SetBackgroundColor(Color);
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(Label);
	Text->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 13));
	Button->AddChild(Text);
	if (UButtonSlot* TextSlot = Cast<UButtonSlot>(Text->Slot))
	{
		TextSlot->SetPadding(FMargin(14.f, 5.f));
		TextSlot->SetHorizontalAlignment(HAlign_Center);
	}
	return Button;
}

USimulationSettingField* USimulationHUDWidget::MakeField(FName Id, float Width)
{
	USimulationSettingField* Field = WidgetTree->ConstructWidget<USimulationSettingField>(USimulationSettingField::StaticClass());
	Field->FieldId = Id;
	Field->OnFieldCommitted.BindUObject(this, &USimulationHUDWidget::HandleFieldCommitted);
	Field->BindCommit();
	Field->SetMinDesiredWidth(Width);
	Field->SetJustification(ETextJustify::Right);
	Fields.Add(Id, Field);
	return Field;
}

void USimulationHUDWidget::BuildDefaultTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = Root;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(PanelColor);
	Panel->SetPadding(FMargin(14.f, 10.f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		PanelSlot->SetAlignment(FVector2D(1.f, 0.f));
		PanelSlot->SetPosition(FVector2D(-24.f, 24.f));
		PanelSlot->SetAutoSize(true);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Panel->SetContent(Column);

	auto AddRow = [Column](UWidget* Widget, float Top, float Bottom)
	{
		if (UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Widget))
		{
			RowSlot->SetPadding(FMargin(0.f, Top, 0.f, Bottom));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	};

	TitleText = MakeText(TEXT("TitleText"), 16, AccentColor, true);
	TitleText->SetText(LOCTEXT("Title", "Simulation: ray tracer"));
	AddRow(TitleText, 0.f, 2.f);

	StatusText = MakeText(TEXT("StatusText"), 11, LabelColor);
	StatusText->SetText(LOCTEXT("StatusIdle", "Idle"));
	AddRow(StatusText, 0.f, 2.f);

	ProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ProgressBar"));
	ProgressBar->SetPercent(0.f);
	ProgressBar->SetFillColorAndOpacity(AccentColor);
	USizeBox* ProgressSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ProgressSize"));
	ProgressSize->SetHeightOverride(8.f);
	ProgressSize->SetContent(ProgressBar);
	AddRow(ProgressSize, 0.f, 8.f);

	SettingsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SettingsBox"));
	AddRow(SettingsBox, 0.f, 6.f);

	// Run / Stop.
	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Buttons"));
	RunButton = MakeButton(TEXT("RunButton"), LOCTEXT("Run", "Run"), FLinearColor(0.15f, 0.4f, 0.2f));
	StopButton = MakeButton(TEXT("StopButton"), LOCTEXT("Stop", "Stop"), FLinearColor(0.4f, 0.15f, 0.15f));
	if (UHorizontalBoxSlot* RunSlot = Buttons->AddChildToHorizontalBox(RunButton))
	{
		RunSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		RunSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	if (UHorizontalBoxSlot* StopSlot = Buttons->AddChildToHorizontalBox(StopButton))
	{
		StopSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	AddRow(Buttons, 0.f, 8.f);

	// Receiver selector.
	UHorizontalBox* ReceiverRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ReceiverRow"));
	PrevReceiverButton = MakeButton(TEXT("PrevReceiverButton"), LOCTEXT("Prev", "<"), FLinearColor(0.14f, 0.17f, 0.22f));
	NextReceiverButton = MakeButton(TEXT("NextReceiverButton"), LOCTEXT("Next", ">"), FLinearColor(0.14f, 0.17f, 0.22f));
	ReceiverText = MakeText(TEXT("ReceiverText"), 13, ValueColor, true);
	ReceiverText->SetJustification(ETextJustify::Center);
	ReceiverRow->AddChildToHorizontalBox(PrevReceiverButton);
	if (UHorizontalBoxSlot* TextSlot = ReceiverRow->AddChildToHorizontalBox(ReceiverText))
	{
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		TextSlot->SetPadding(FMargin(8.f, 0.f));
	}
	ReceiverRow->AddChildToHorizontalBox(NextReceiverButton);
	AddRow(ReceiverRow, 0.f, 4.f);

	MetricsText = MakeText(TEXT("MetricsText"), 12, ValueColor);
	MetricsText->SetText(LOCTEXT("NoMetrics", "No result yet"));
	AddRow(MetricsText, 0.f, 6.f);

	TimingText = MakeText(TEXT("TimingText"), 11, LabelColor);
	AddRow(TimingText, 0.f, 6.f);

	ReferenceText = MakeText(TEXT("ReferenceText"), 11, AccentColor);
	AddRow(ReferenceText, 0.f, 6.f);

	BuildVisualizationBlock(Column);

	HintText = MakeText(TEXT("HintText"), 10, MutedColor);
	HintText->SetText(LOCTEXT("Hint", "Tab: cursor / fly   Enter in a field applies it   Esc: main menu"));
	AddRow(HintText, 0.f, 0.f);

	// Impulse-response panel, bottom-left.
	UBorder* PlotPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PlotPanel"));
	PlotPanel->SetBrushColor(PanelColor);
	PlotPanel->SetPadding(FMargin(10.f, 8.f));
	if (UCanvasPanelSlot* PlotSlot = Root->AddChildToCanvas(PlotPanel))
	{
		PlotSlot->SetAnchors(FAnchors(0.f, 1.f, 0.f, 1.f));
		PlotSlot->SetAlignment(FVector2D(0.f, 1.f));
		PlotSlot->SetPosition(FVector2D(24.f, -24.f));
		PlotSlot->SetAutoSize(true);
	}
	UVerticalBox* PlotColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlotColumn"));
	PlotPanel->SetContent(PlotColumn);

	UHorizontalBox* PlotHeader = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PlotHeader"));
	UTextBlock* PlotTitle = MakeText(TEXT("PlotTitle"), 13, AccentColor, true);
	PlotTitle->SetText(LOCTEXT("PlotTitle", "Impulse response: energy (thin) and Schroeder decay (thick)"));
	if (UHorizontalBoxSlot* TitleSlot = PlotHeader->AddChildToHorizontalBox(PlotTitle))
	{
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
	}
	PinCurveButton = MakeButton(TEXT("PinCurveButton"), LOCTEXT("Pin", "Pin"), FLinearColor(0.14f, 0.17f, 0.22f));
	ClearCurvesButton = MakeButton(TEXT("ClearCurvesButton"), LOCTEXT("ClearCurves", "Clear"), FLinearColor(0.14f, 0.17f, 0.22f));
	if (UHorizontalBoxSlot* PinSlot = PlotHeader->AddChildToHorizontalBox(PinCurveButton))
	{
		PinSlot->SetPadding(FMargin(8.f, 0.f, 4.f, 0.f));
	}
	PlotHeader->AddChildToHorizontalBox(ClearCurvesButton);
	if (UVerticalBoxSlot* HeaderSlot = PlotColumn->AddChildToVerticalBox(PlotHeader))
	{
		HeaderSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	IRPlot = WidgetTree->ConstructWidget<UIRPlotWidget>(UIRPlotWidget::StaticClass(), TEXT("IRPlot"));
	USizeBox* PlotSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PlotSize"));
	PlotSize->SetWidthOverride(460.f);
	PlotSize->SetHeightOverride(210.f);
	PlotSize->SetContent(IRPlot);
	PlotColumn->AddChildToVerticalBox(PlotSize);
}

void USimulationHUDWidget::BuildVisualizationBlock(UVerticalBox* Column)
{
	auto AddRow = [Column](UWidget* Widget, float Top, float Bottom)
	{
		if (UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Widget))
		{
			RowSlot->SetPadding(FMargin(0.f, Top, 0.f, Bottom));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	};
	auto AddLabel = [this](UHorizontalBox* Row, const FText& Label, float LeftPadding)
	{
		UTextBlock* Text = MakeText(NAME_None, 11, LabelColor);
		Text->SetText(Label);
		if (UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text))
		{
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetPadding(FMargin(LeftPadding, 0.f, 4.f, 0.f));
		}
	};
	auto AddCheck = [this](UHorizontalBox* Row, FName Name, float LeftPadding) -> UCheckBox*
	{
		UCheckBox* Check = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), Name);
		if (UHorizontalBoxSlot* CheckSlot = Row->AddChildToHorizontalBox(Check))
		{
			CheckSlot->SetVerticalAlignment(VAlign_Center);
			CheckSlot->SetPadding(FMargin(LeftPadding, 0.f, 0.f, 0.f));
		}
		return Check;
	};

	UTextBlock* Title = MakeText(TEXT("VisualizationTitle"), 12, AccentColor, true);
	Title->SetText(LOCTEXT("VisualizationTitle", "Visualization"));
	AddRow(Title, 2.f, 2.f);

	// Row 1: paths.
	UHorizontalBox* PathRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PathRow"));
	PathsCheck = AddCheck(PathRow, TEXT("PathsCheck"), 0.f);
	AddLabel(PathRow, LOCTEXT("Paths", "Ray paths"), 4.f);
	OnlyReachingCheck = AddCheck(PathRow, TEXT("OnlyReachingCheck"), 10.f);
	AddLabel(PathRow, LOCTEXT("OnlyReaching", "Only to receiver"), 4.f);
	AddLabel(PathRow, LOCTEXT("DrawOrder", "Order <="), 10.f);
	USimulationSettingField* OrderField = MakeField(FieldDrawOrder, 40.f);
	USizeBox* OrderSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	OrderSize->SetWidthOverride(44.f);
	OrderSize->SetContent(OrderField);
	if (UHorizontalBoxSlot* OrderSlot = PathRow->AddChildToHorizontalBox(OrderSize))
	{
		OrderSlot->SetVerticalAlignment(VAlign_Center);
	}
	AddRow(PathRow, 0.f, 3.f);

	// Row 2: heatmap and metric.
	UHorizontalBox* HeatRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeatRow"));
	HeatmapCheck = AddCheck(HeatRow, TEXT("HeatmapCheck"), 0.f);
	AddLabel(HeatRow, LOCTEXT("Heatmap", "Heatmap"), 4.f);
	AddLabel(HeatRow, LOCTEXT("Metric", "Metric"), 10.f);
	MetricCombo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("MetricCombo"));
	for (const EHeatmapMetric Metric : UHeatmapComponent::AllMetrics())
	{
		MetricCombo->AddOption(UHeatmapComponent::MetricLabel(Metric).ToString());
	}
	MetricCombo->SetSelectedIndex(1);
	if (UHorizontalBoxSlot* ComboSlot = HeatRow->AddChildToHorizontalBox(MetricCombo))
	{
		ComboSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ComboSlot->SetVerticalAlignment(VAlign_Center);
	}
	AddRow(HeatRow, 0.f, 3.f);

	HeatmapLegend = WidgetTree->ConstructWidget<UHeatmapLegendWidget>(UHeatmapLegendWidget::StaticClass(), TEXT("HeatmapLegend"));
	USizeBox* LegendSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LegendSize"));
	LegendSize->SetHeightOverride(26.f);
	LegendSize->SetContent(HeatmapLegend);
	AddRow(LegendSize, 0.f, 3.f);

	VisualizationText = MakeText(TEXT("VisualizationText"), 10, MutedColor);
	AddRow(VisualizationText, 0.f, 6.f);
}

void USimulationHUDWidget::BuildSettingsRows()
{
	if (!SettingsBox || !WidgetTree)
	{
		return;
	}
	SettingsBox->ClearChildren();
	for (const FName& Id : { FieldNumRays, FieldMaxOrder, FieldSeed, FieldReceiverRadius, FieldBinMs, FieldTMax, FieldNumThreads, FieldWallAlpha, FieldAirAbsorption })
	{
		Fields.Remove(Id);
	}

	struct FRowSpec
	{
		FName Id;
		FText Label;
	};
	const FRowSpec Rows[] = {
		{ FieldNumRays, LOCTEXT("NumRays", "Rays") },
		{ FieldMaxOrder, LOCTEXT("MaxOrder", "Max order") },
		{ FieldSeed, LOCTEXT("Seed", "Seed") },
		{ FieldReceiverRadius, LOCTEXT("ReceiverRadius", "Receiver r, m (0 = own)") },
		{ FieldBinMs, LOCTEXT("BinMs", "Bin, ms") },
		{ FieldTMax, LOCTEXT("TMax", "T max, s") },
		{ FieldNumThreads, LOCTEXT("NumThreads", "Threads (0 = all)") },
		{ FieldWallAlpha, LOCTEXT("WallAlpha", "Wall alpha override (0 = off)") },
		{ FieldAirAbsorption, LOCTEXT("AirAbsorption", "Air m, 1/m") },
	};

	for (const FRowSpec& Row : Rows)
	{
		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		UTextBlock* Label = MakeText(NAME_None, 11, LabelColor);
		Label->SetText(Row.Label);
		if (UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(Label))
		{
			LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
			LabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}

		USimulationSettingField* Field = MakeField(Row.Id, 84.f);

		USizeBox* FieldSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		FieldSize->SetWidthOverride(84.f);
		FieldSize->SetContent(Field);
		if (UHorizontalBoxSlot* FieldSlot = Line->AddChildToHorizontalBox(FieldSize))
		{
			FieldSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (Row.Id == FieldAirAbsorption)
		{
			AirAbsorptionCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("AirAbsorptionCheck"));
			AirAbsorptionCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleAirAbsorptionChanged);
			if (UHorizontalBoxSlot* CheckSlot = Line->AddChildToHorizontalBox(AirAbsorptionCheck))
			{
				CheckSlot->SetVerticalAlignment(VAlign_Center);
				CheckSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
			}
		}

		if (UVerticalBoxSlot* LineSlot = SettingsBox->AddChildToVerticalBox(Line))
		{
			LineSlot->SetPadding(FMargin(0.f, 1.f));
			LineSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}
}

void USimulationHUDWidget::HandleRunClicked()
{
	if (AAcousticSimulationManager* Current = Manager.Get())
	{
		Current->Run();
		RefreshStatus();
	}
}

void USimulationHUDWidget::HandleStopClicked()
{
	if (AAcousticSimulationManager* Current = Manager.Get())
	{
		Current->Stop();
		RefreshStatus();
	}
}

void USimulationHUDWidget::HandlePrevReceiverClicked()
{
	const AAcousticSimulationManager* Current = Manager.Get();
	const int32 Position = Current ? Current->GetPointReceiverIndices().IndexOfByKey(SelectedReceiver) : INDEX_NONE;
	SelectReceiver((Position == INDEX_NONE ? 0 : Position) - 1);
}

void USimulationHUDWidget::HandleNextReceiverClicked()
{
	const AAcousticSimulationManager* Current = Manager.Get();
	const int32 Position = Current ? Current->GetPointReceiverIndices().IndexOfByKey(SelectedReceiver) : INDEX_NONE;
	SelectReceiver((Position == INDEX_NONE ? 0 : Position) + 1);
}

void USimulationHUDWidget::HandleAirAbsorptionChanged(bool bChecked)
{
	if (AAcousticSimulationManager* Current = Manager.Get())
	{
		Current->Settings.bAirAbsorption = bChecked;
	}
}

void USimulationHUDWidget::HandlePathsChanged(bool bChecked)
{
	if (AAcousticSimulationManager* Current = Manager.Get())
	{
		if (Current->PathVisualizer)
		{
			Current->PathVisualizer->SetEnabled(bChecked);
		}
	}
	RefreshVisualization();
}

void USimulationHUDWidget::HandleOnlyReachingChanged(bool bChecked)
{
	if (AAcousticSimulationManager* Current = Manager.Get())
	{
		if (Current->PathVisualizer)
		{
			Current->PathVisualizer->SetReceiverFilter(SelectedReceiver);
			Current->PathVisualizer->SetOnlyPathsReachingReceiver(bChecked);
		}
	}
	RefreshVisualization();
}

void USimulationHUDWidget::HandleHeatmapChanged(bool bChecked)
{
	for (AReceiverGrid* Grid : FindGrids())
	{
		if (Grid->Heatmap)
		{
			Grid->Heatmap->SetHiddenInGame(!bChecked);
			Grid->Heatmap->SetVisibility(bChecked);
		}
	}
	RefreshVisualization();
}

void USimulationHUDWidget::HandleMetricChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	const TArray<EHeatmapMetric> Metrics = UHeatmapComponent::AllMetrics();
	const int32 Index = MetricCombo ? MetricCombo->GetSelectedIndex() : INDEX_NONE;
	if (!Metrics.IsValidIndex(Index))
	{
		return;
	}
	for (AReceiverGrid* Grid : FindGrids())
	{
		if (Grid->Heatmap)
		{
			Grid->Heatmap->SetMetric(Metrics[Index]);
		}
	}
	RefreshVisualization();
}

void USimulationHUDWidget::HandlePinCurveClicked()
{
	PinCurrentCurve();
}

void USimulationHUDWidget::HandleClearCurvesClicked()
{
	ClearPinnedCurves();
}

void USimulationHUDWidget::PinCurrentCurve()
{
	const AAcousticSimulationManager* Current = Manager.Get();
	const FSimulationResult* Result = Current ? Current->GetLastResult() : nullptr;
	if (!IRPlot || !Result || !Result->bSuccess || !Result->Receivers.IsValidIndex(SelectedReceiver))
	{
		return;
	}
	const int32 CurveSlot = FMath::Clamp(NextPinSlot, 1, UIRPlotWidget::MaxCurves - 1);
	const FReceiverResult& Receiver = Result->Receivers[SelectedReceiver];
	IRPlot->SetCurve(CurveSlot, Receiver, FString::Printf(TEXT("%s #%d"), *Receiver.Name, Current->GetRunCount()), UIRPlotWidget::SlotColor(CurveSlot));
	NextPinSlot = CurveSlot + 1 < UIRPlotWidget::MaxCurves ? CurveSlot + 1 : 1;
}

void USimulationHUDWidget::ClearPinnedCurves()
{
	if (IRPlot)
	{
		for (int32 CurveSlot = 1; CurveSlot < UIRPlotWidget::MaxCurves; ++CurveSlot)
		{
			IRPlot->ClearCurve(CurveSlot);
		}
	}
	NextPinSlot = 1;
}

TArray<AReceiverGrid*> USimulationHUDWidget::FindGrids() const
{
	return AReceiverGrid::FindAll(GetWorld());
}

void USimulationHUDWidget::HandleFieldCommitted(FName FieldId, const FText& Text)
{
	AAcousticSimulationManager* Current = Manager.Get();
	if (!Current)
	{
		return;
	}
	const FString Value = Text.ToString().TrimStartAndEnd();
	const int32 IntValue = FCString::Atoi(*Value);
	const float FloatValue = FCString::Atof(*Value);
	FSolverSettings& Settings = Current->Settings;

	if (FieldId == FieldNumRays)
	{
		Settings.NumRays = FMath::Clamp(IntValue, 1, 5000000);
	}
	else if (FieldId == FieldMaxOrder)
	{
		Settings.MaxOrder = FMath::Clamp(IntValue, 0, 100000);
	}
	else if (FieldId == FieldSeed)
	{
		Settings.Seed = FMath::Max(0, IntValue);
	}
	else if (FieldId == FieldReceiverRadius)
	{
		Settings.ReceiverRadius = FMath::Clamp(FloatValue, 0.0f, 5.0f);
	}
	else if (FieldId == FieldBinMs)
	{
		Settings.BinMs = FMath::Clamp(FloatValue, 0.1f, 100.0f);
	}
	else if (FieldId == FieldTMax)
	{
		Settings.TMax = FMath::Clamp(FloatValue, 0.1f, 30.0f);
	}
	else if (FieldId == FieldAirAbsorption)
	{
		Settings.AirAbsorptionPerMeter = FMath::Clamp(FloatValue, 0.0f, 1.0f);
	}
	else if (FieldId == FieldNumThreads)
	{
		Settings.NumThreads = FMath::Clamp(IntValue, 0, 256);
	}
	else if (FieldId == FieldWallAlpha)
	{
		Current->WallAbsorptionOverride = FMath::Clamp(FloatValue, 0.0f, 0.99f);
	}
	else if (FieldId == FieldDrawOrder)
	{
		if (Current->PathVisualizer)
		{
			Current->PathVisualizer->SetMaxDrawOrder(FMath::Clamp(IntValue, 0, 100000));
		}
		RefreshVisualization();
	}
	LoadFieldsFromManager();
}

void USimulationHUDWidget::HandleResultReady(AAcousticSimulationManager* InManager, const FSimulationResult& Result)
{
	// Keep the selection on a point receiver of the new result.
	const TArray<int32> Points = InManager ? InManager->GetPointReceiverIndices() : TArray<int32>();
	if (Points.Num() > 0 && !Points.Contains(SelectedReceiver))
	{
		SelectedReceiver = Points[0];
	}
	if (InManager && InManager->PathVisualizer)
	{
		InManager->PathVisualizer->SetReceiverFilter(SelectedReceiver);
	}
	RefreshMetrics();
	RefreshReference();
	RefreshPlot();
	RefreshVisualization();
	RefreshStatus();
}

void USimulationHUDWidget::SelectReceiver(int32 Index)
{
	const AAcousticSimulationManager* Current = Manager.Get();
	const TArray<int32> Points = Current ? Current->GetPointReceiverIndices() : TArray<int32>();
	const int32 Count = Points.Num();
	SelectedReceiver = Count > 0 ? Points[((Index % Count) + Count) % Count] : 0;
	if (Current && Current->PathVisualizer)
	{
		Current->PathVisualizer->SetReceiverFilter(SelectedReceiver);
	}
	RefreshMetrics();
	RefreshReference();
	RefreshPlot();
	RefreshVisualization();
}

void USimulationHUDWidget::RefreshAll()
{
	LoadFieldsFromManager();
	RefreshStatus();
	RefreshMetrics();
	RefreshReference();
	RefreshPlot();
	RefreshVisualization();
}

void USimulationHUDWidget::LoadFieldsFromManager()
{
	const AAcousticSimulationManager* Current = Manager.Get();
	if (!Current)
	{
		return;
	}
	const FSolverSettings& Settings = Current->Settings;
	auto SetField = [this](const FName& Id, const FString& Value)
	{
		if (const TObjectPtr<USimulationSettingField>* Field = Fields.Find(Id))
		{
			(*Field)->SetText(FText::FromString(Value));
		}
	};
	SetField(FieldNumRays, FString::FromInt(Settings.NumRays));
	SetField(FieldMaxOrder, FString::FromInt(Settings.MaxOrder));
	SetField(FieldSeed, FString::FromInt(Settings.Seed));
	SetField(FieldReceiverRadius, FString::Printf(TEXT("%.2f"), Settings.ReceiverRadius));
	SetField(FieldBinMs, FString::Printf(TEXT("%.1f"), Settings.BinMs));
	SetField(FieldTMax, FString::Printf(TEXT("%.1f"), Settings.TMax));
	SetField(FieldAirAbsorption, FString::Printf(TEXT("%.4f"), Settings.AirAbsorptionPerMeter));
	SetField(FieldNumThreads, FString::FromInt(Settings.NumThreads));
	SetField(FieldWallAlpha, FString::Printf(TEXT("%.2f"), Current->WallAbsorptionOverride));
	if (AirAbsorptionCheck)
	{
		AirAbsorptionCheck->SetIsChecked(Settings.bAirAbsorption);
	}
	if (const URayPathVisualizerComponent* Paths = Current->PathVisualizer)
	{
		SetField(FieldDrawOrder, FString::FromInt(Paths->MaxDrawOrder));
		if (PathsCheck)
		{
			PathsCheck->SetIsChecked(Paths->bEnabled);
		}
		if (OnlyReachingCheck)
		{
			OnlyReachingCheck->SetIsChecked(Paths->bOnlyPathsReachingReceiver);
		}
	}
	const TArray<AReceiverGrid*> Grids = FindGrids();
	const UHeatmapComponent* Heatmap = Grids.Num() > 0 ? Grids[0]->Heatmap.Get() : nullptr;
	if (HeatmapCheck)
	{
		HeatmapCheck->SetIsChecked(Heatmap ? Heatmap->IsVisible() : false);
		HeatmapCheck->SetIsEnabled(Heatmap != nullptr);
	}
	if (MetricCombo && Heatmap)
	{
		const int32 MetricIndex = UHeatmapComponent::AllMetrics().IndexOfByKey(Heatmap->Metric);
		if (MetricIndex != INDEX_NONE && MetricCombo->GetSelectedIndex() != MetricIndex)
		{
			MetricCombo->SetSelectedIndex(MetricIndex);
		}
	}
}

void USimulationHUDWidget::RefreshStatus()
{
	const AAcousticSimulationManager* Current = Manager.Get();
	if (!Current)
	{
		if (StatusText)
		{
			StatusText->SetText(LOCTEXT("NoManager", "No simulation manager in this level"));
		}
		return;
	}

	FString Status;
	switch (Current->State)
	{
	case ESimulationState::Idle: Status = TEXT("Idle"); break;
	case ESimulationState::Extracting: Status = TEXT("Extracting scene..."); break;
	case ESimulationState::Running: Status = FString::Printf(TEXT("Running %d %%  (%.1f s)"), FMath::RoundToInt(Current->Progress * 100.f), Current->GetElapsedSeconds()); break;
	case ESimulationState::Finished: Status = TEXT("Finished"); break;
	case ESimulationState::Cancelled: Status = TEXT("Cancelled"); break;
	case ESimulationState::Failed: Status = TEXT("Failed"); break;
	default: break;
	}
	if (!Current->StatusMessage.IsEmpty() && Current->State != ESimulationState::Running)
	{
		Status += TEXT(": ") + Current->StatusMessage;
	}
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Status));
	}
	if (ProgressBar)
	{
		ProgressBar->SetPercent(Current->IsRunning() ? Current->Progress : (Current->State == ESimulationState::Finished ? 1.f : 0.f));
	}
	if (RunButton)
	{
		RunButton->SetIsEnabled(!Current->IsRunning());
	}
	if (StopButton)
	{
		StopButton->SetIsEnabled(Current->State == ESimulationState::Running);
	}
	if (Current->GetRunCount() != ShownRunCount && !Current->IsRunning())
	{
		ShownRunCount = Current->GetRunCount();
		RefreshMetrics();
		RefreshReference();
		RefreshPlot();
		RefreshVisualization();
	}
}

void USimulationHUDWidget::RefreshMetrics()
{
	const AAcousticSimulationManager* Current = Manager.Get();
	const FSimulationResult* Result = Current ? Current->GetLastResult() : nullptr;
	if (!Result || !Result->bSuccess || Result->Receivers.IsEmpty())
	{
		if (ReceiverText)
		{
			ReceiverText->SetText(LOCTEXT("NoReceiver", "-"));
		}
		if (MetricsText)
		{
			MetricsText->SetText(Result && !Result->bSuccess ? FText::FromString(Result->Describe()) : LOCTEXT("NoMetrics", "No result yet"));
		}
		if (TimingText)
		{
			TimingText->SetText(FText::GetEmpty());
		}
		return;
	}

	SelectedReceiver = FMath::Clamp(SelectedReceiver, 0, Result->Receivers.Num() - 1);
	const FReceiverResult& Receiver = Result->Receivers[SelectedReceiver];
	const FBandMetrics& Band = Receiver.Metrics.Band(0);

	if (ReceiverText)
	{
		const TArray<int32> Points = Current->GetPointReceiverIndices();
		const int32 Position = Points.IndexOfByKey(SelectedReceiver);
		ReceiverText->SetText(FText::FromString(FString::Printf(TEXT("%s  (%d / %d)"), *Receiver.Name, Position == INDEX_NONE ? SelectedReceiver + 1 : Position + 1, Points.Num() > 0 ? Points.Num() : Result->Receivers.Num())));
	}
	if (MetricsText)
	{
		const FString Text = FString::Printf(
			TEXT("Direct: %.2f m, %.2f ms%s\nEDT  %s\nT20  %s\nT30  %s\nC50  %.1f dB\nC80  %.1f dB\nD50  %.3f\nTs   %.1f ms\nEnergy %.3g (direct %.3g), %lld ray hits, range %.0f dB"),
			Receiver.DirectDistance, Receiver.DirectArrivalSeconds * 1000.0, Receiver.bDirectOccluded ? TEXT(" (occluded)") : TEXT(""),
			*DecayText(Band.EDT, Band.bEDTValid), *DecayText(Band.T20, Band.bT20Valid), *DecayText(Band.T30, Band.bT30Valid),
			Band.C50, Band.C80, Band.D50, Band.Ts * 1000.0, Band.TotalEnergy, Receiver.DirectEnergy, Receiver.NumRayHits, Band.DecayRangeDb);
		MetricsText->SetText(FText::FromString(Text));
	}
	if (TimingText)
	{
		const FString Text = FString::Printf(TEXT("%d rays, %d triangles, mean order %.1f, %lld segments\n%.3f s total (trace %.3f s on %d workers)"),
			Result->NumRays, Result->NumTriangles, Result->MeanOrder, Result->NumSegments, Result->Timing.TotalSeconds, Result->Timing.TraceSeconds, Result->Timing.NumWorkers);
		TimingText->SetText(FText::FromString(Text));
	}
}

void USimulationHUDWidget::RefreshReference()
{
	if (!ReferenceText)
	{
		return;
	}
	const AAcousticSimulationManager* Current = Manager.Get();
	if (!Current || Current->GetRunCount() == 0)
	{
		ReferenceText->SetText(LOCTEXT("NoReference", "Sabine / Eyring: run once to compute"));
		return;
	}
	if (!Current->bHasAnalyticReference)
	{
		ReferenceText->SetText(FText::FromString(FString::Printf(TEXT("Mean absorption %.3f (no shoebox room: no analytic reference)"), Current->MeanAbsorption)));
		return;
	}

	FString Text = FString::Printf(TEXT("Shoebox V %.0f m3, S %.0f m2, mean alpha %.3f\nSabine %.3f s   Eyring %.3f s"),
		Current->RoomVolumeM3, Current->RoomSurfaceM2, Current->MeanAbsorption, Current->SabineT60, Current->EyringT60);
	const FSimulationResult* Result = Current->GetLastResult();
	if (Result && Result->bSuccess && Result->Receivers.IsValidIndex(SelectedReceiver) && Current->EyringT60 > 0.0)
	{
		const FBandMetrics& Band = Result->Receivers[SelectedReceiver].Metrics.Band(0);
		if (Band.bT30Valid)
		{
			Text += FString::Printf(TEXT("\nT30 %.3f s = %+.1f %% vs Eyring, %+.1f %% vs Sabine"), Band.T30,
				(Band.T30 - Current->EyringT60) / Current->EyringT60 * 100.0, (Band.T30 - Current->SabineT60) / Current->SabineT60 * 100.0);
		}
	}
	ReferenceText->SetText(FText::FromString(Text));
}

void USimulationHUDWidget::RefreshPlot()
{
	if (!IRPlot)
	{
		return;
	}
	const AAcousticSimulationManager* Current = Manager.Get();
	const FSimulationResult* Result = Current ? Current->GetLastResult() : nullptr;
	if (!Result || !Result->bSuccess || !Result->Receivers.IsValidIndex(SelectedReceiver))
	{
		IRPlot->ClearCurve(0);
		return;
	}
	const FReceiverResult& Receiver = Result->Receivers[SelectedReceiver];
	IRPlot->SetCurve(0, Receiver, FString::Printf(TEXT("%s #%d"), *Receiver.Name, Current->GetRunCount()), UIRPlotWidget::SlotColor(0));
}

void USimulationHUDWidget::RefreshVisualization()
{
	const AAcousticSimulationManager* Current = Manager.Get();
	const URayPathVisualizerComponent* Paths = Current ? Current->PathVisualizer.Get() : nullptr;
	const FSimulationResult* Result = Current ? Current->GetLastResult() : nullptr;

	if (VisualizationText)
	{
		FString Text;
		if (Paths && Result && Result->bSuccess)
		{
			Text = FString::Printf(TEXT("Paths: %d drawn of %d matching (%d sampled, every %d-th ray), %d segments"),
				Paths->GetNumDrawnPaths(), Paths->GetNumMatchingPaths(), Result->Paths.Num(), Result->PathSampleStride, Paths->GetNumDrawnSegments());
		}
		else
		{
			Text = TEXT("Paths: no result yet");
		}
		VisualizationText->SetText(FText::FromString(Text));
	}

	if (HeatmapLegend)
	{
		const TArray<AReceiverGrid*> Grids = FindGrids();
		const UHeatmapComponent* Heatmap = Grids.Num() > 0 ? Grids[0]->Heatmap.Get() : nullptr;
		if (Heatmap)
		{
			const bool bHasData = Heatmap->HasSamples() && Heatmap->NumValidTiles > 0;
			const FText Label = FText::Format(LOCTEXT("LegendLabel", "{0} ({1} tiles)"), UHeatmapComponent::MetricLabel(Heatmap->Metric), Heatmap->NumValidTiles);
			HeatmapLegend->SetRange(Label, Heatmap->ShownMin, Heatmap->ShownMax, UHeatmapComponent::MetricUnits(Heatmap->Metric), bHasData);
		}
		else
		{
			HeatmapLegend->SetRange(LOCTEXT("NoGrid", "No receiver grid in this level"), 0.f, 0.f, FString(), false);
		}
	}
}

#undef LOCTEXT_NAMESPACE
