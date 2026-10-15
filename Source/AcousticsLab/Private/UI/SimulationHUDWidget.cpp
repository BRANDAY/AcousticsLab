#include "UI/SimulationHUDWidget.h"

#include "AcousticsCore.h"
#include "Audio/AuralizationComponent.h"
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
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/AcousticsPlayerController.h"
#include "Results/SimulationResult.h"
#include "Scene/ReceiverGrid.h"
#include "Simulation/AcousticSimulationManager.h"
#include "Simulation/AcousticSimulationSubsystem.h"
#include "Solvers/BeamTracer.h"
#include "Solvers/HybridTracer.h"
#include "Styling/CoreStyle.h"
#include "UI/HeatmapLegendWidget.h"
#include "UI/IRPlotWidget.h"
#include "Visualization/BeamVisualizerComponent.h"
#include "Visualization/HeatmapComponent.h"
#include "Visualization/ImageSourceVisualizerComponent.h"
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
	const FName FieldImageOrder(TEXT("ImageOrder"));
	const FName FieldBeamSubdiv(TEXT("BeamSubdiv"));
	const FName FieldBeamOrder(TEXT("BeamOrder"));
	const FName FieldHybridK(TEXT("HybridK"));
	const FName FieldHybridMix(TEXT("HybridMixMs"));

	const FLinearColor PanelColor(0.02f, 0.025f, 0.035f, 0.85f);
	const FLinearColor LabelColor(0.75f, 0.75f, 0.8f);
	const FLinearColor ValueColor(0.95f, 0.95f, 0.95f);
	const FLinearColor AccentColor(0.95f, 0.8f, 0.35f);
	const FLinearColor MutedColor(0.55f, 0.55f, 0.6f);
	const FLinearColor ButtonColor(0.14f, 0.17f, 0.22f);
	const FLinearColor SelectedButtonColor(0.42f, 0.35f, 0.12f);

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
	return { FieldNumRays, FieldMaxOrder, FieldSeed, FieldReceiverRadius, FieldBinMs, FieldTMax, FieldAirAbsorption, FieldNumThreads, FieldWallAlpha, FieldDrawOrder, FieldImageOrder, FieldBeamSubdiv, FieldBeamOrder, FieldHybridK, FieldHybridMix };
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
	if (PlayClipButton)
	{
		PlayClipButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandlePlayClipClicked);
	}
	if (StopClipButton)
	{
		StopClipButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandleStopClipClicked);
	}
	if (DryButton)
	{
		DryButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandleDryClicked);
	}
	if (RayButton)
	{
		RayButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandleRayClicked);
	}
	if (BeamButton)
	{
		BeamButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandleBeamClicked);
	}
	if (HybridButton)
	{
		HybridButton->OnClicked.AddUniqueDynamic(this, &USimulationHUDWidget::HandleHybridClicked);
	}
	if (SolverCombo)
	{
		if (SolverCombo->GetOptionCount() == 0)
		{
			for (const EAcousticSolver Solver : SolverOptions())
			{
				SolverCombo->AddOption(UAcousticSimulationSubsystem::SolverName(Solver));
			}
		}
		SolverCombo->OnSelectionChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleSolverChanged);
	}
	if (SourceCombo)
	{
		SourceCombo->OnSelectionChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleSourceChanged);
	}
	if (RefractionCheck)
	{
		RefractionCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleRefractionChanged);
	}
	if (ImagesCheck)
	{
		ImagesCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleImagesChanged);
	}
	if (ImagesOnlyValidCheck)
	{
		ImagesOnlyValidCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleImagesOnlyValidChanged);
	}
	if (BeamsCheck)
	{
		BeamsCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleBeamsChanged);
	}
	if (BeamsContainingCheck)
	{
		BeamsContainingCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleBeamsContainingChanged);
	}
	if (BeamOrderSlider)
	{
		BeamOrderSlider->OnValueChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleBeamOrderChanged);
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
	TitleText->SetText(LOCTEXT("Title", "Simulation"));
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
	BuildAuralizationBlock(Column);

	HintText = MakeText(TEXT("HintText"), 10, MutedColor);
	HintText->SetText(LOCTEXT("Hint", "Tab: cursor / fly   Enter in a field applies it   1 / 2 / 3: Dry / Ray / Beam   Esc: main menu"));
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

	// Row 1b: image sources (M5).
	UHorizontalBox* ImageRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ImageRow"));
	ImagesCheck = AddCheck(ImageRow, TEXT("ImagesCheck"), 0.f);
	AddLabel(ImageRow, LOCTEXT("Images", "Image sources"), 4.f);
	ImagesOnlyValidCheck = AddCheck(ImageRow, TEXT("ImagesOnlyValidCheck"), 10.f);
	AddLabel(ImageRow, LOCTEXT("ImagesOnlyValid", "Only valid for receiver"), 4.f);
	AddRow(ImageRow, 0.f, 3.f);

	// Row 1c: beams (M5): toggle, receiver filter, order slider.
	UHorizontalBox* BeamRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BeamRow"));
	BeamsCheck = AddCheck(BeamRow, TEXT("BeamsCheck"), 0.f);
	AddLabel(BeamRow, LOCTEXT("Beams", "Beams"), 4.f);
	BeamsContainingCheck = AddCheck(BeamRow, TEXT("BeamsContainingCheck"), 10.f);
	AddLabel(BeamRow, LOCTEXT("BeamsContaining", "Only with receiver"), 4.f);
	AddLabel(BeamRow, LOCTEXT("BeamOrder", "Order <="), 10.f);
	BeamOrderSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("BeamOrderSlider"));
	BeamOrderSlider->SetMinValue(0.f);
	BeamOrderSlider->SetMaxValue(3.f);
	BeamOrderSlider->SetStepSize(1.f);
	BeamOrderSlider->SetValue(1.f);
	BeamOrderSlider->MouseUsesStep = true;
	USizeBox* SliderSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	SliderSize->SetWidthOverride(80.f);
	SliderSize->SetContent(BeamOrderSlider);
	if (UHorizontalBoxSlot* SliderSlot = BeamRow->AddChildToHorizontalBox(SliderSize))
	{
		SliderSlot->SetVerticalAlignment(VAlign_Center);
		SliderSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
	}
	BeamOrderText = MakeText(TEXT("BeamOrderText"), 11, ValueColor, true);
	BeamOrderText->SetText(FText::FromString(TEXT("1")));
	if (UHorizontalBoxSlot* OrderTextSlot = BeamRow->AddChildToHorizontalBox(BeamOrderText))
	{
		OrderTextSlot->SetVerticalAlignment(VAlign_Center);
	}
	AddRow(BeamRow, 0.f, 3.f);

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

void USimulationHUDWidget::BuildAuralizationBlock(UVerticalBox* Column)
{
	auto AddRow = [Column](UWidget* Widget, float Top, float Bottom)
	{
		if (UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Widget))
		{
			RowSlot->SetPadding(FMargin(0.f, Top, 0.f, Bottom));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	};

	UTextBlock* Title = MakeText(TEXT("AuralizationTitle"), 12, AccentColor, true);
	Title->SetText(LOCTEXT("AuralizationTitle", "Auralization (convolution reverb)"));
	AddRow(Title, 2.f, 2.f);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("AuralizationRow"));
	DryButton = MakeButton(TEXT("DryButton"), LOCTEXT("DryVariant", "1: Dry"), ButtonColor);
	RayButton = MakeButton(TEXT("RayButton"), LOCTEXT("RayVariant", "2: Ray"), ButtonColor);
	BeamButton = MakeButton(TEXT("BeamButton"), LOCTEXT("BeamVariant", "3: Beam"), ButtonColor);
	HybridButton = MakeButton(TEXT("HybridButton"), LOCTEXT("HybridVariant", "4: Hybrid"), ButtonColor);
	PlayClipButton = MakeButton(TEXT("PlayClipButton"), LOCTEXT("PlayClip", "Play"), FLinearColor(0.15f, 0.4f, 0.2f));
	StopClipButton = MakeButton(TEXT("StopClipButton"), LOCTEXT("StopClip", "Stop"), FLinearColor(0.4f, 0.15f, 0.15f));
	for (UButton* Button : { DryButton.Get(), RayButton.Get(), BeamButton.Get(), HybridButton.Get(), PlayClipButton.Get(), StopClipButton.Get() })
	{
		if (UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(Button))
		{
			ButtonSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
			ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}
	AddRow(Row, 0.f, 3.f);

	AuralizationText = MakeText(TEXT("AuralizationText"), 10, MutedColor);
	AuralizationText->SetText(LOCTEXT("NoAuralization", "IR: none yet (Run the simulation)"));
	AddRow(AuralizationText, 0.f, 6.f);
}

void USimulationHUDWidget::BuildSettingsRows()
{
	if (!SettingsBox || !WidgetTree)
	{
		return;
	}
	SettingsBox->ClearChildren();
	for (const FName& Id : { FieldNumRays, FieldMaxOrder, FieldSeed, FieldReceiverRadius, FieldBinMs, FieldTMax, FieldNumThreads, FieldWallAlpha, FieldAirAbsorption, FieldImageOrder, FieldBeamSubdiv, FieldBeamOrder, FieldHybridK, FieldHybridMix })
	{
		Fields.Remove(Id);
	}

	// Solver selector (M5).
	{
		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SolverRow"));
		UTextBlock* Label = MakeText(NAME_None, 11, LabelColor);
		Label->SetText(LOCTEXT("Solver", "Solver"));
		if (UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(Label))
		{
			LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
			LabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}
		SolverCombo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("SolverCombo"));
		for (const EAcousticSolver Solver : SolverOptions())
		{
			SolverCombo->AddOption(UAcousticSimulationSubsystem::SolverName(Solver));
		}
		SolverCombo->SetSelectedIndex(0);
		SolverCombo->OnSelectionChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleSolverChanged);
		USizeBox* ComboSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ComboSize->SetWidthOverride(124.f);
		ComboSize->SetContent(SolverCombo);
		if (UHorizontalBoxSlot* ComboSlot = Line->AddChildToHorizontalBox(ComboSize))
		{
			ComboSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (UVerticalBoxSlot* LineSlot = SettingsBox->AddChildToVerticalBox(Line))
		{
			LineSlot->SetPadding(FMargin(0.f, 1.f, 0.f, 3.f));
			LineSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	// Source selector (M6): levels with several sources (Level_Refraction: Pool, Lens).
	{
		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SourceRow"));
		UTextBlock* Label = MakeText(NAME_None, 11, LabelColor);
		Label->SetText(LOCTEXT("Source", "Source"));
		if (UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(Label))
		{
			LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
			LabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}
		SourceCombo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("SourceCombo"));
		SourceCombo->OnSelectionChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleSourceChanged);
		USizeBox* ComboSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ComboSize->SetWidthOverride(124.f);
		ComboSize->SetContent(SourceCombo);
		if (UHorizontalBoxSlot* ComboSlot = Line->AddChildToHorizontalBox(ComboSize))
		{
			ComboSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (UVerticalBoxSlot* LineSlot = SettingsBox->AddChildToVerticalBox(Line))
		{
			LineSlot->SetPadding(FMargin(0.f, 1.f, 0.f, 3.f));
			LineSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		RefreshSourceOptions();
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
		{ FieldImageOrder, LOCTEXT("ImageOrder", "Image sources: order (<= 3)") },
		{ FieldBeamSubdiv, LOCTEXT("BeamSubdiv", "Beams: subdivision (0=20, 2=320, 6=81920)") },
		{ FieldBeamOrder, LOCTEXT("BeamOrder", "Beams: max order") },
		{ FieldHybridK, LOCTEXT("HybridK", "Hybrid: beams up to order K") },
		{ FieldHybridMix, LOCTEXT("HybridMix", "Hybrid: t_mix, ms") },
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

	// Refraction (M6): the beam tracer splits beams at interface triangles; the apex of the refracted beam is an approximation.
	{
		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RefractionRow"));
		UTextBlock* Label = MakeText(NAME_None, 11, LabelColor);
		Label->SetText(LOCTEXT("Refraction", "Beams: refraction at interfaces"));
		if (UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(Label))
		{
			LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
			LabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}
		RefractionCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("RefractionCheck"));
		RefractionCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USimulationHUDWidget::HandleRefractionChanged);
		if (UHorizontalBoxSlot* CheckSlot = Line->AddChildToHorizontalBox(RefractionCheck))
		{
			CheckSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (UVerticalBoxSlot* LineSlot = SettingsBox->AddChildToVerticalBox(Line))
		{
			LineSlot->SetPadding(FMargin(0.f, 1.f));
			LineSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		RefractionNoteText = MakeText(TEXT("RefractionNoteText"), 9, MutedColor);
		RefractionNoteText->SetAutoWrapText(true);
		RefractionNoteText->SetText(LOCTEXT("RefractionNote", "Refracted beam apex = least-squares fit of the 3 refracted guide rays (approximation: a refracted beam has no exact apex, Sikora 2012 sec. 3.1.2 / Sikora 2010). Energy split R / T by impedances at normal incidence; beyond the critical angle only the reflected beam."));
		if (UVerticalBoxSlot* NoteSlot = SettingsBox->AddChildToVerticalBox(RefractionNoteText))
		{
			NoteSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
			NoteSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}
}

void USimulationHUDWidget::RefreshSourceOptions()
{
	if (!SourceCombo)
	{
		return;
	}
	const AAcousticSimulationManager* Current = Manager.Get();
	const TArray<FName> Ids = Current ? Current->GetSourceIds() : TArray<FName>();
	bool bSame = SourceCombo->GetOptionCount() == Ids.Num();
	for (int32 Index = 0; bSame && Index < Ids.Num(); ++Index)
	{
		bSame = SourceCombo->GetOptionAtIndex(Index) == Ids[Index].ToString();
	}
	if (!bSame)
	{
		SourceCombo->ClearOptions();
		for (const FName& Id : Ids)
		{
			SourceCombo->AddOption(Id.ToString());
		}
	}
	SourceCombo->SetIsEnabled(Ids.Num() > 1);
	if (Current)
	{
		int32 Selected = Current->SourceId.IsNone() ? 0 : Ids.IndexOfByKey(Current->SourceId);
		if (Selected == INDEX_NONE)
		{
			Selected = 0;
		}
		if (Ids.IsValidIndex(Selected) && SourceCombo->GetSelectedIndex() != Selected)
		{
			SourceCombo->SetSelectedIndex(Selected);
		}
	}
}

void USimulationHUDWidget::HandleSourceChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	AAcousticSimulationManager* Current = Manager.Get();
	if (!Current || SelectedItem.IsEmpty())
	{
		return;
	}
	const FName Id(*SelectedItem);
	if (Current->SourceId != Id)
	{
		Current->SourceId = Id;
		UE_LOG(LogAcoustics, Log, TEXT("Source selected: %s"), *SelectedItem);
	}
}

void USimulationHUDWidget::HandleRefractionChanged(bool bChecked)
{
	if (AAcousticSimulationManager* Current = Manager.Get())
	{
		Current->Settings.bRefraction = bChecked;
	}
	if (RefractionNoteText)
	{
		RefractionNoteText->SetVisibility(bChecked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
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

UAuralizationComponent* USimulationHUDWidget::GetAuralization() const
{
	const AAcousticSimulationManager* Current = Manager.Get();
	return Current ? Current->Auralization.Get() : nullptr;
}

void USimulationHUDWidget::HandlePlayClipClicked()
{
	if (UAuralizationComponent* Auralization = GetAuralization())
	{
		Auralization->Play();
	}
	RefreshAuralization();
}

void USimulationHUDWidget::HandleStopClipClicked()
{
	if (UAuralizationComponent* Auralization = GetAuralization())
	{
		Auralization->Stop();
	}
	RefreshAuralization();
}

void USimulationHUDWidget::HandleDryClicked()
{
	SelectVariant(1);
}

void USimulationHUDWidget::HandleRayClicked()
{
	SelectVariant(2);
}

void USimulationHUDWidget::HandleBeamClicked()
{
	SelectVariant(3);
}

void USimulationHUDWidget::HandleHybridClicked()
{
	SelectVariant(4);
}

TArray<EAcousticSolver> USimulationHUDWidget::SolverOptions()
{
	return { EAcousticSolver::RayTracer, EAcousticSolver::ImageSource, EAcousticSolver::BeamTracer, EAcousticSolver::Hybrid };
}

UImageSourceVisualizerComponent* USimulationHUDWidget::GetImageSourceVisualizer() const
{
	const AAcousticSimulationManager* Current = Manager.Get();
	return Current ? Current->ImageSourceVisualizer.Get() : nullptr;
}

UBeamVisualizerComponent* USimulationHUDWidget::GetBeamVisualizer() const
{
	const AAcousticSimulationManager* Current = Manager.Get();
	return Current ? Current->BeamVisualizer.Get() : nullptr;
}

void USimulationHUDWidget::HandleSolverChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	const TArray<EAcousticSolver> Options = SolverOptions();
	const int32 Index = SolverCombo ? SolverCombo->GetSelectedIndex() : INDEX_NONE;
	AAcousticSimulationManager* Current = Manager.Get();
	if (Current && Options.IsValidIndex(Index) && Current->Solver != Options[Index])
	{
		Current->Solver = Options[Index];
		UE_LOG(LogAcoustics, Log, TEXT("Solver selected: %s"), *UAcousticSimulationSubsystem::SolverName(Current->Solver));
	}
	RefreshTitle();
}

void USimulationHUDWidget::RefreshTitle()
{
	if (!TitleText)
	{
		return;
	}
	const AAcousticSimulationManager* Current = Manager.Get();
	const FString Solver = Current ? UAcousticSimulationSubsystem::SolverName(Current->Solver) : TEXT("ray tracer");
	TitleText->SetText(FText::Format(LOCTEXT("TitleFormat", "Simulation: {0}"), FText::FromString(Solver)));
}

void USimulationHUDWidget::HandleImagesChanged(bool bChecked)
{
	if (UImageSourceVisualizerComponent* Images = GetImageSourceVisualizer())
	{
		Images->SetEnabled(bChecked);
	}
	RefreshVisualization();
}

void USimulationHUDWidget::HandleImagesOnlyValidChanged(bool bChecked)
{
	if (UImageSourceVisualizerComponent* Images = GetImageSourceVisualizer())
	{
		Images->SetReceiverFilter(SelectedReceiver);
		Images->SetOnlyValidForReceiver(bChecked);
	}
	RefreshVisualization();
}

void USimulationHUDWidget::HandleBeamsChanged(bool bChecked)
{
	if (UBeamVisualizerComponent* Beams = GetBeamVisualizer())
	{
		Beams->SetEnabled(bChecked);
	}
	RefreshVisualization();
}

void USimulationHUDWidget::HandleBeamsContainingChanged(bool bChecked)
{
	if (UBeamVisualizerComponent* Beams = GetBeamVisualizer())
	{
		Beams->SetReceiverFilter(SelectedReceiver);
		Beams->SetOnlyBeamsContainingReceiver(bChecked);
	}
	RefreshVisualization();
}

void USimulationHUDWidget::HandleBeamOrderChanged(float Value)
{
	const int32 Order = FMath::RoundToInt(Value);
	if (BeamOrderText)
	{
		BeamOrderText->SetText(FText::AsNumber(Order));
	}
	if (UBeamVisualizerComponent* Beams = GetBeamVisualizer())
	{
		Beams->SetMaxDrawOrder(Order);
	}
	RefreshVisualization();
}

void USimulationHUDWidget::SelectVariant(int32 Key)
{
	if (AAcousticsPlayerController* Controller = GetOwningPlayer<AAcousticsPlayerController>())
	{
		Controller->SelectedVariant = Key;
	}
	if (UAuralizationComponent* Auralization = GetAuralization())
	{
		if (Auralization->SetVariantByKey(Key) && !Auralization->IsPlaying() && Auralization->HasImpulseResponse())
		{
			Auralization->Play();
		}
	}
	RefreshAuralization();
}

void USimulationHUDWidget::RefreshAuralization()
{
	const UAuralizationComponent* Auralization = GetAuralization();
	if (AuralizationText)
	{
		AuralizationText->SetText(FText::FromString(Auralization ? Auralization->DescribeState() : FString(TEXT("No auralization component in this level"))));
	}
	const bool bPlaying = Auralization && Auralization->IsPlaying();
	if (PlayClipButton)
	{
		PlayClipButton->SetIsEnabled(Auralization != nullptr && !bPlaying);
	}
	if (StopClipButton)
	{
		StopClipButton->SetIsEnabled(bPlaying);
	}
	if (DryButton)
	{
		DryButton->SetBackgroundColor(Auralization && Auralization->Variant == EAuralizationVariant::Dry ? SelectedButtonColor : ButtonColor);
	}
	if (RayButton)
	{
		RayButton->SetBackgroundColor(Auralization && Auralization->Variant == EAuralizationVariant::Ray ? SelectedButtonColor : ButtonColor);
		RayButton->SetIsEnabled(Auralization && Auralization->HasVariantResult(EAuralizationVariant::Ray));
	}
	if (BeamButton)
	{
		BeamButton->SetBackgroundColor(Auralization && Auralization->Variant == EAuralizationVariant::Beam ? SelectedButtonColor : ButtonColor);
		BeamButton->SetIsEnabled(Auralization && Auralization->HasVariantResult(EAuralizationVariant::Beam));
	}
	if (HybridButton)
	{
		HybridButton->SetBackgroundColor(Auralization && Auralization->Variant == EAuralizationVariant::Hybrid ? SelectedButtonColor : ButtonColor);
		HybridButton->SetIsEnabled(Auralization && Auralization->HasVariantResult(EAuralizationVariant::Hybrid));
	}
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
	else if (FieldId == FieldImageOrder)
	{
		Settings.ImageSourceOrder = FMath::Clamp(IntValue, 0, 6);
	}
	else if (FieldId == FieldBeamSubdiv)
	{
		Settings.SubdivisionLevel = FMath::Clamp(IntValue, 0, 7);
	}
	else if (FieldId == FieldBeamOrder)
	{
		Settings.MaxBeamOrder = FMath::Clamp(IntValue, 0, 10);
	}
	else if (FieldId == FieldHybridK)
	{
		Settings.HybridK = FMath::Clamp(IntValue, 0, 10);
	}
	else if (FieldId == FieldHybridMix)
	{
		Settings.HybridMixingTimeMs = FMath::Clamp(FloatValue, 1.0f, 3000.0f);
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
	if (InManager && InManager->ImageSourceVisualizer)
	{
		InManager->ImageSourceVisualizer->SetReceiverFilter(SelectedReceiver);
	}
	if (InManager && InManager->BeamVisualizer)
	{
		InManager->BeamVisualizer->SetReceiverFilter(SelectedReceiver);
	}
	if (InManager && InManager->Auralization)
	{
		InManager->Auralization->SelectReceiver(SelectedReceiver);
	}
	RefreshMetrics();
	RefreshReference();
	RefreshPlot();
	RefreshVisualization();
	RefreshAuralization();
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
	if (UImageSourceVisualizerComponent* Images = GetImageSourceVisualizer())
	{
		Images->SetReceiverFilter(SelectedReceiver);
	}
	if (UBeamVisualizerComponent* Beams = GetBeamVisualizer())
	{
		Beams->SetReceiverFilter(SelectedReceiver); // the tree is not rebuilt: beams containing the new receiver are looked up
	}
	if (UAuralizationComponent* Auralization = GetAuralization())
	{
		Auralization->SelectReceiver(SelectedReceiver); // swaps the cached impulse response, no solver run
	}
	RefreshMetrics();
	RefreshReference();
	RefreshPlot();
	RefreshVisualization();
	RefreshAuralization();
}

void USimulationHUDWidget::RefreshAll()
{
	LoadFieldsFromManager();
	RefreshStatus();
	RefreshMetrics();
	RefreshReference();
	RefreshPlot();
	RefreshVisualization();
	RefreshAuralization();
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
	SetField(FieldImageOrder, FString::FromInt(Settings.ImageSourceOrder));
	SetField(FieldBeamSubdiv, FString::FromInt(Settings.SubdivisionLevel));
	SetField(FieldBeamOrder, FString::FromInt(Settings.MaxBeamOrder));
	SetField(FieldHybridK, FString::FromInt(Settings.HybridK));
	SetField(FieldHybridMix, FString::Printf(TEXT("%.0f"), Settings.HybridMixingTimeMs));
	if (AirAbsorptionCheck)
	{
		AirAbsorptionCheck->SetIsChecked(Settings.bAirAbsorption);
	}
	if (RefractionCheck)
	{
		RefractionCheck->SetIsChecked(Settings.bRefraction);
	}
	if (RefractionNoteText)
	{
		RefractionNoteText->SetVisibility(Settings.bRefraction ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	RefreshSourceOptions();
	if (SolverCombo)
	{
		const int32 SolverIndex = SolverOptions().IndexOfByKey(Current->GetEffectiveSolver());
		if (SolverIndex != INDEX_NONE && SolverCombo->GetSelectedIndex() != SolverIndex)
		{
			SolverCombo->SetSelectedIndex(SolverIndex);
		}
		RefreshTitle();
	}
	if (const UImageSourceVisualizerComponent* Images = Current->ImageSourceVisualizer)
	{
		if (ImagesCheck)
		{
			ImagesCheck->SetIsChecked(Images->bEnabled);
		}
		if (ImagesOnlyValidCheck)
		{
			ImagesOnlyValidCheck->SetIsChecked(Images->bOnlyValidForReceiver);
		}
	}
	if (const UBeamVisualizerComponent* Beams = Current->BeamVisualizer)
	{
		if (BeamsCheck)
		{
			BeamsCheck->SetIsChecked(Beams->bEnabled);
		}
		if (BeamsContainingCheck)
		{
			BeamsContainingCheck->SetIsChecked(Beams->bOnlyBeamsContainingReceiver);
		}
		if (BeamOrderSlider)
		{
			BeamOrderSlider->SetMaxValue(static_cast<float>(FMath::Max(1, Beams->GetTreeMaxOrder() >= 0 ? Beams->GetTreeMaxOrder() : Settings.MaxBeamOrder)));
			BeamOrderSlider->SetValue(static_cast<float>(Beams->MaxDrawOrder));
		}
		if (BeamOrderText)
		{
			BeamOrderText->SetText(FText::AsNumber(Beams->MaxDrawOrder));
		}
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
	RefreshTitle();
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
	RefreshAuralization(); // playback state and variant change outside the panel (keys 1 / 2)
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
		FString Text;
		const TSharedPtr<const FHybridResult> Hybrid = Current->GetLastHybridResult();
		if (Hybrid.IsValid() && Hybrid->Hybrid.Get() == Result && Hybrid->Stats.IsValidIndex(SelectedReceiver))
		{
			const FHybridReceiverStats& Stats = Hybrid->Stats[SelectedReceiver];
			Text = FString::Printf(TEXT("Hybrid K=%d, t_mix %.0f ms: %d early beam reflections at %s (+%d after t_mix left to the rays), last at %.1f ms; rays found %d, missed %d\nbeams: %d root / %d nodes %.3f s; rays: %d, mean order %.1f, %.3f s; total %.3f s"),
				Hybrid->K, Hybrid->MixingTimeSeconds * 1000.0, Stats.NumBeamEarly, *Receiver.Name, Stats.NumBeamDropped, Stats.LastEarlyArrivalSeconds * 1000.0, Stats.NumFoundByRays, Stats.NumMissedByRays,
				Result->NumRootBeams, Result->NumBeamNodes, Hybrid->BeamSeconds, Result->NumRays, Result->MeanOrder, Hybrid->RaySeconds, Result->Timing.TotalSeconds);
		}
		else if (Result->NumBeamNodes > 0)
		{
			Text = FString::Printf(TEXT("%s: %d root beams, %d beam nodes, %d triangles, %d beams contain %s\n%.3f s total (tree %.3f s, receivers %.3f s)"),
				*Result->SolverName, Result->NumRootBeams, Result->NumBeamNodes, Result->NumTriangles, Receiver.ContainingBeamNodes.Num(), *Receiver.Name,
				Result->Timing.TotalSeconds, Result->Timing.SetupSeconds, Result->Timing.TraceSeconds);
			if (Result->BeamTree.IsValid() && (Result->BeamTree->NumRefracted > 0 || Result->BeamTree->NumTotalInternalReflections > 0))
			{
				const FBeamTree& Tree = *Result->BeamTree;
				int32 NumRefractedArrivals = 0;
				for (const FEarlyReflection& Reflection : Receiver.EarlyReflections)
				{
					NumRefractedArrivals += Tree.Nodes.IsValidIndex(Reflection.BeamNode) && Tree.Nodes[Reflection.BeamNode].NumRefractions > 0 ? 1 : 0;
				}
				Text += FString::Printf(TEXT("\nRefraction: %d refracted beams, %d total internal reflections, %d continued beyond the focus, %d parallel apexes; %d refracted arrivals at %s (apex approximated by least squares)"),
					Tree.NumRefracted, Tree.NumTotalInternalReflections, Tree.NumContinuations, Tree.NumParallelApexes, NumRefractedArrivals, *Receiver.Name);
			}
		}
		else if (Result->NumImageSources > 0)
		{
			Text = FString::Printf(TEXT("%s: %d image sources, %d valid paths (%d to %s), %d triangles\n%.3f s total (images %.3f s, receivers %.3f s)"),
				*Result->SolverName, Result->NumImageSources, Result->NumValidImagePaths, Receiver.EarlyReflections.Num(), *Receiver.Name, Result->NumTriangles,
				Result->Timing.TotalSeconds, Result->Timing.SetupSeconds, Result->Timing.TraceSeconds);
		}
		else
		{
			Text = FString::Printf(TEXT("%d rays, %d triangles, mean order %.1f, %lld segments\n%.3f s total (trace %.3f s on %d workers)"),
				Result->NumRays, Result->NumTriangles, Result->MeanOrder, Result->NumSegments, Result->Timing.TotalSeconds, Result->Timing.TraceSeconds, Result->Timing.NumWorkers);
		}
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
		if (Result && Result->bSuccess && Result->NumBeamNodes > 0)
		{
			const UBeamVisualizerComponent* Beams = Current->BeamVisualizer;
			Text = FString::Printf(TEXT("Beams: %d drawn of %d matching (order <= %d, %d triangles); %d image spheres, %d paths"),
				Beams ? Beams->GetNumDrawnBeams() : 0, Beams ? Beams->GetNumMatchingBeams() : 0, Beams ? Beams->MaxDrawOrder : 0, Beams ? Beams->GetNumTriangles() : 0,
				Current->ImageSourceVisualizer ? Current->ImageSourceVisualizer->GetNumDrawnImages() : 0, Current->ImageSourceVisualizer ? Current->ImageSourceVisualizer->GetNumDrawnPaths() : 0);
		}
		else if (Result && Result->bSuccess && Result->NumImageSources > 0)
		{
			const UImageSourceVisualizerComponent* Images = Current->ImageSourceVisualizer;
			Text = FString::Printf(TEXT("Image sources: %d spheres drawn (%d valid for the receiver) of %d, %d paths, order <= %d"),
				Images ? Images->GetNumDrawnImages() : 0, Images ? Images->GetNumValidImages() : 0, Result->NumImageSources, Images ? Images->GetNumDrawnPaths() : 0, Images ? Images->MaxDrawOrder : 0);
		}
		else if (Paths && Result && Result->bSuccess)
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
	if (BeamOrderSlider && Current && Current->BeamVisualizer)
	{
		const int32 TreeOrder = Current->BeamVisualizer->GetTreeMaxOrder();
		BeamOrderSlider->SetMaxValue(static_cast<float>(FMath::Max(1, TreeOrder >= 0 ? TreeOrder : Current->Settings.MaxBeamOrder)));
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
