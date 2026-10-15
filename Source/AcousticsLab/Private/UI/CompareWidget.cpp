#include "UI/CompareWidget.h"

#include "AcousticsCore.h"
#include "Audio/AuralizationComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/AcousticsPlayerController.h"
#include "Results/RoomMetrics.h"
#include "Simulation/AcousticSimulationManager.h"
#include "Simulation/AcousticSimulationSubsystem.h"
#include "Solvers/BeamTracer.h"
#include "Solvers/HybridTracer.h"
#include "Styling/CoreStyle.h"
#include "UI/IRPlotWidget.h"
#include "UI/SimulationHUDWidget.h"

#define LOCTEXT_NAMESPACE "AcousticsCompare"

namespace
{
	const FName FieldK(TEXT("CompareK"));
	const FName FieldMix(TEXT("CompareMixMs"));
	const FName FieldRays(TEXT("CompareRays"));
	const FName FieldSubdiv(TEXT("CompareSubdiv"));

	const FLinearColor PanelColor(0.02f, 0.025f, 0.035f, 0.88f);
	const FLinearColor LabelColor(0.75f, 0.75f, 0.8f);
	const FLinearColor ValueColor(0.95f, 0.95f, 0.95f);
	const FLinearColor AccentColor(0.95f, 0.8f, 0.35f);
	const FLinearColor MutedColor(0.55f, 0.55f, 0.6f);
	const FLinearColor ButtonColor(0.14f, 0.17f, 0.22f);
	const FLinearColor SelectedButtonColor(0.42f, 0.35f, 0.12f);

	/** One column of the metrics table; M9 appends Steam Audio and Audio Tracing (listed now as placeholders). */
	struct FCompareColumn
	{
		FString Name;
		const FReceiverResult* Receiver = nullptr;
		double Seconds = 0.0;
		FString Rays;
		FString Beams;
		FString Missing;
	};

	FString DecayCell(double Value, bool bValid)
	{
		return bValid ? FString::Printf(TEXT("%.2f s"), Value) : FString(TEXT("n/a"));
	}

	FString Pad(const FString& Text, int32 Width)
	{
		return Text.Len() >= Width ? Text.Left(Width) : Text + FString::ChrN(Width - Text.Len(), TEXT(' '));
	}
}

UCompareWidget::UCompareWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UCompareWidget::SetManager(AAcousticSimulationManager* InManager)
{
	Manager = InManager;
	ShownRunCount = -1;
	ShownHybrid = nullptr;
	LoadFields();
	RefreshResult();
}

void UCompareWidget::SetMode(FName InMode)
{
	Mode = InMode;
	if (TitleText)
	{
		TitleText->SetText(Mode == FName(TEXT("Compare"))
			? LOCTEXT("TitleCompare", "3. Compare: pure ray / pure beam / hybrid (industry columns: M9)")
			: LOCTEXT("TitleHybrid", "1.4 Hybrid beam/ray tracing: beams early, rays late"));
	}
}

TSharedRef<SWidget> UCompareWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultTree();
	}
	return Super::RebuildWidget();
}

UTextBlock* UCompareWidget::MakeText(FName Name, int32 FontSize, const FLinearColor& Color, bool bBold)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Text->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FontSize));
	Text->SetColorAndOpacity(FSlateColor(Color));
	return Text;
}

UButton* UCompareWidget::MakeButton(FName Name, const FText& Label, const FLinearColor& Color)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	Button->SetBackgroundColor(Color);
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(Label);
	Text->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12));
	Button->AddChild(Text);
	if (UButtonSlot* TextSlot = Cast<UButtonSlot>(Text->Slot))
	{
		TextSlot->SetPadding(FMargin(10.f, 4.f));
		TextSlot->SetHorizontalAlignment(HAlign_Center);
	}
	return Button;
}

USimulationSettingField* UCompareWidget::MakeField(FName Id, float Width)
{
	USimulationSettingField* Field = WidgetTree->ConstructWidget<USimulationSettingField>(USimulationSettingField::StaticClass());
	Field->FieldId = Id;
	Field->OnFieldCommitted.BindUObject(this, &UCompareWidget::HandleFieldCommitted);
	Field->BindCommit();
	Field->SetMinDesiredWidth(Width);
	Field->SetJustification(ETextJustify::Right);
	Fields.Add(Id, Field);
	return Field;
}

void UCompareWidget::BuildDefaultTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CompareRoot"));
	WidgetTree->RootWidget = Root;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ComparePanel"));
	Panel->SetBrushColor(PanelColor);
	Panel->SetPadding(FMargin(14.f, 10.f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		PanelSlot->SetAlignment(FVector2D(1.f, 0.f));
		PanelSlot->SetPosition(FVector2D(-24.f, 24.f));
		PanelSlot->SetAutoSize(true);
	}
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CompareColumn"));
	Panel->SetContent(Column);
	auto AddRow = [Column](UWidget* Widget, float Top, float Bottom)
	{
		if (UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Widget))
		{
			RowSlot->SetPadding(FMargin(0.f, Top, 0.f, Bottom));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	};
	auto AddToLine = [](UHorizontalBox* Line, UWidget* Widget, float RightPadding, bool bFill = false)
	{
		if (UHorizontalBoxSlot* LineSlot = Line->AddChildToHorizontalBox(Widget))
		{
			LineSlot->SetPadding(FMargin(0.f, 0.f, RightPadding, 0.f));
			LineSlot->SetVerticalAlignment(VAlign_Center);
			if (bFill)
			{
				LineSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
		}
	};
	auto Wrapped = [this](UTextBlock* Text, float Width, FName Name)
	{
		Text->SetAutoWrapText(true);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), Name);
		Size->SetWidthOverride(Width);
		Size->SetContent(Text);
		return Size;
	};
	constexpr float PanelWidth = 600.f;

	TitleText = MakeText(TEXT("CompareTitle"), 15, AccentColor, true);
	AddRow(TitleText, 0.f, 2.f);
	SetMode(Mode);

	UTextBlock* Note = MakeText(TEXT("CompareNote"), 10, LabelColor);
	Note->SetText(LOCTEXT("Note", "Beams give the purely specular paths of order 1..K arriving by t_mix (exact delays, never missed); rays give everything else - "
		"a ray deposits unless it is still specular, of order <= K and its image-source delay is <= t_mix (so specular paths after t_mix stay with the rays). "
		"Beams carry (1-alpha)(1-s) per reflection, the specular share of the rays. Pure ray and hybrid share every ray; pure beam is the early part without the t_mix cut."));
	AddRow(Wrapped(Note, PanelWidth, TEXT("CompareNoteSize")), 0.f, 6.f);

	UHorizontalBox* FieldLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CompareFields"));
	auto AddField = [&](FName Id, const FText& Label, float Width)
	{
		UTextBlock* LabelText = MakeText(NAME_None, 11, LabelColor);
		LabelText->SetText(Label);
		AddToLine(FieldLine, LabelText, 4.f);
		AddToLine(FieldLine, MakeField(Id, Width), 12.f);
	};
	AddField(FieldK, LOCTEXT("K", "K"), 30.f);
	AddField(FieldMix, LOCTEXT("Mix", "t_mix, ms"), 44.f);
	AddField(FieldRays, LOCTEXT("Rays", "Rays"), 64.f);
	AddField(FieldSubdiv, LOCTEXT("Subdiv", "Beam subdiv"), 30.f);
	RunButton = MakeButton(TEXT("CompareRun"), LOCTEXT("Run", "Run hybrid"), FLinearColor(0.15f, 0.4f, 0.2f));
	StopButton = MakeButton(TEXT("CompareStop"), LOCTEXT("Stop", "Stop"), FLinearColor(0.4f, 0.15f, 0.15f));
	AddToLine(FieldLine, RunButton, 6.f);
	AddToLine(FieldLine, StopButton, 0.f);
	AddRow(FieldLine, 0.f, 4.f);

	StatusText = MakeText(TEXT("CompareStatus"), 11, LabelColor);
	AddRow(StatusText, 0.f, 2.f);
	ProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("CompareProgress"));
	ProgressBar->SetPercent(0.f);
	ProgressBar->SetFillColorAndOpacity(AccentColor);
	USizeBox* ProgressSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CompareProgressSize"));
	ProgressSize->SetHeightOverride(6.f);
	ProgressSize->SetContent(ProgressBar);
	AddRow(ProgressSize, 0.f, 6.f);

	UHorizontalBox* ReceiverLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CompareReceiverLine"));
	PrevButton = MakeButton(TEXT("ComparePrev"), LOCTEXT("Prev", "<"), ButtonColor);
	NextButton = MakeButton(TEXT("CompareNext"), LOCTEXT("Next", ">"), ButtonColor);
	ReceiverText = MakeText(TEXT("CompareReceiver"), 12, ValueColor, true);
	EarlyButton = MakeButton(TEXT("CompareEarly"), FText::Format(LOCTEXT("Early", "0-{0} ms"), FText::AsNumber(FMath::RoundToInt(EarlyRangeSeconds * 1000.f))), ButtonColor);
	FullButton = MakeButton(TEXT("CompareFull"), LOCTEXT("Full", "Full decay"), ButtonColor);
	AddToLine(ReceiverLine, PrevButton, 4.f);
	AddToLine(ReceiverLine, ReceiverText, 4.f);
	AddToLine(ReceiverLine, NextButton, 16.f);
	UTextBlock* Spacer = MakeText(NAME_None, 10, MutedColor);
	AddToLine(ReceiverLine, Spacer, 0.f, true);
	AddToLine(ReceiverLine, EarlyButton, 4.f);
	AddToLine(ReceiverLine, FullButton, 0.f);
	AddRow(ReceiverLine, 0.f, 4.f);

	Plot = WidgetTree->ConstructWidget<UIRPlotWidget>(UIRPlotWidget::StaticClass(), TEXT("ComparePlot"));
	USizeBox* PlotSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ComparePlotSize"));
	PlotSize->SetWidthOverride(PanelWidth);
	PlotSize->SetHeightOverride(250.f);
	PlotSize->SetContent(Plot);
	AddRow(PlotSize, 0.f, 2.f);

	LegendText = MakeText(TEXT("CompareLegend"), 10, MutedColor);
	LegendText->SetText(LOCTEXT("Legend", "blue = pure ray, orange = pure beam (order <= K), green = hybrid; thin = energy per 1 ms bin, thick = Schroeder curve"));
	AddRow(LegendText, 0.f, 6.f);

	TableText = MakeText(TEXT("CompareTable"), 10, ValueColor);
	TableText->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 10));
	AddRow(TableText, 0.f, 6.f);

	SummaryText = MakeText(TEXT("CompareSummary"), 10, LabelColor);
	AddRow(Wrapped(SummaryText, PanelWidth, TEXT("CompareSummarySize")), 0.f, 6.f);

	UHorizontalBox* AbLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CompareAbLine"));
	DryButton = MakeButton(TEXT("CompareDry"), LOCTEXT("Dry", "1: Dry"), ButtonColor);
	RayButton = MakeButton(TEXT("CompareRay"), LOCTEXT("Ray", "2: Ray"), ButtonColor);
	BeamButton = MakeButton(TEXT("CompareBeam"), LOCTEXT("Beam", "3: Beam <= K"), ButtonColor);
	HybridButton = MakeButton(TEXT("CompareHybrid"), LOCTEXT("Hybrid", "4: Hybrid"), ButtonColor);
	for (UButton* Button : { DryButton.Get(), RayButton.Get(), BeamButton.Get(), HybridButton.Get() })
	{
		AddToLine(AbLine, Button, 6.f, true);
	}
	AddRow(AbLine, 0.f, 3.f);
	AuralizationText = MakeText(TEXT("CompareAuralization"), 10, MutedColor);
	AddRow(Wrapped(AuralizationText, PanelWidth, TEXT("CompareAuralizationSize")), 0.f, 4.f);

	UTextBlock* Hint = MakeText(TEXT("CompareHint"), 10, MutedColor);
	Hint->SetText(LOCTEXT("Hint", "H: compare / simulation panel   B: benchmark   1-4: A/B   Tab: cursor / fly   Enter in a field applies it   Esc: main menu"));
	AddRow(Hint, 0.f, 0.f);
}

void UCompareWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (RunButton)
	{
		RunButton->OnClicked.AddUniqueDynamic(this, &UCompareWidget::HandleRunClicked);
	}
	if (StopButton)
	{
		StopButton->OnClicked.AddUniqueDynamic(this, &UCompareWidget::HandleStopClicked);
	}
	if (PrevButton)
	{
		PrevButton->OnClicked.AddUniqueDynamic(this, &UCompareWidget::HandlePrevClicked);
	}
	if (NextButton)
	{
		NextButton->OnClicked.AddUniqueDynamic(this, &UCompareWidget::HandleNextClicked);
	}
	if (EarlyButton)
	{
		EarlyButton->OnClicked.AddUniqueDynamic(this, &UCompareWidget::HandleEarlyClicked);
	}
	if (FullButton)
	{
		FullButton->OnClicked.AddUniqueDynamic(this, &UCompareWidget::HandleFullClicked);
	}
	if (DryButton)
	{
		DryButton->OnClicked.AddUniqueDynamic(this, &UCompareWidget::HandleDryClicked);
	}
	if (RayButton)
	{
		RayButton->OnClicked.AddUniqueDynamic(this, &UCompareWidget::HandleRayClicked);
	}
	if (BeamButton)
	{
		BeamButton->OnClicked.AddUniqueDynamic(this, &UCompareWidget::HandleBeamClicked);
	}
	if (HybridButton)
	{
		HybridButton->OnClicked.AddUniqueDynamic(this, &UCompareWidget::HandleHybridClicked);
	}
	SetEarlyZoom(bEarlyZoom);
	LoadFields();
	RefreshResult();
}

void UCompareWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator < 0.1f)
	{
		return;
	}
	RefreshAccumulator = 0.0f;
	RefreshStatus();
	const TSharedPtr<const FHybridResult> Hybrid = CurrentHybrid();
	if (Hybrid.Get() != ShownHybrid)
	{
		RefreshResult();
	}
	RefreshAuralization();
}

TSharedPtr<const FHybridResult> UCompareWidget::CurrentHybrid() const
{
	const AAcousticSimulationManager* Current = Manager.Get();
	return Current ? Current->GetLastHybridResult() : nullptr;
}

TArray<int32> UCompareWidget::PointReceivers() const
{
	const AAcousticSimulationManager* Current = Manager.Get();
	const TSharedPtr<const FHybridResult> Hybrid = CurrentHybrid();
	if (!Current || !Hybrid.IsValid() || !Hybrid->IsValid())
	{
		return {};
	}
	// The bindings belong to the last result; when that is not the hybrid one, every receiver of the hybrid run is listed.
	if (Current->GetLastResult() == Hybrid->Hybrid.Get())
	{
		return Current->GetPointReceiverIndices();
	}
	TArray<int32> All;
	for (int32 Index = 0; Index < Hybrid->Hybrid->Receivers.Num(); ++Index)
	{
		All.Add(Index);
	}
	return All;
}

void UCompareWidget::LoadFields()
{
	const AAcousticSimulationManager* Current = Manager.Get();
	if (!Current)
	{
		return;
	}
	auto SetField = [this](const FName& Id, const FString& Value)
	{
		if (const TObjectPtr<USimulationSettingField>* Field = Fields.Find(Id))
		{
			(*Field)->SetText(FText::FromString(Value));
		}
	};
	SetField(FieldK, FString::FromInt(Current->Settings.HybridK));
	SetField(FieldMix, FString::Printf(TEXT("%.0f"), Current->Settings.HybridMixingTimeMs));
	SetField(FieldRays, FString::FromInt(Current->Settings.NumRays));
	SetField(FieldSubdiv, FString::FromInt(Current->Settings.SubdivisionLevel));
}

void UCompareWidget::HandleFieldCommitted(FName FieldId, const FText& Text)
{
	AAcousticSimulationManager* Current = Manager.Get();
	if (!Current)
	{
		return;
	}
	const FString Value = Text.ToString().TrimStartAndEnd();
	FSolverSettings& Settings = Current->Settings;
	if (FieldId == FieldK)
	{
		Settings.HybridK = FMath::Clamp(FCString::Atoi(*Value), 0, 10);
	}
	else if (FieldId == FieldMix)
	{
		Settings.HybridMixingTimeMs = FMath::Clamp(FCString::Atof(*Value), 1.0f, 3000.0f);
	}
	else if (FieldId == FieldRays)
	{
		Settings.NumRays = FMath::Clamp(FCString::Atoi(*Value), 1, 5000000);
	}
	else if (FieldId == FieldSubdiv)
	{
		Settings.SubdivisionLevel = FMath::Clamp(FCString::Atoi(*Value), 0, 7);
	}
	LoadFields();
}

bool UCompareWidget::RunHybrid()
{
	AAcousticSimulationManager* Current = Manager.Get();
	if (!Current || Current->IsRunning())
	{
		return false;
	}
	Current->Solver = EAcousticSolver::Hybrid;
	const bool bStarted = Current->Run();
	RefreshStatus();
	return bStarted;
}

void UCompareWidget::HandleRunClicked()
{
	RunHybrid();
}

void UCompareWidget::HandleStopClicked()
{
	if (AAcousticSimulationManager* Current = Manager.Get())
	{
		Current->Stop();
	}
}

void UCompareWidget::RefreshStatus()
{
	const AAcousticSimulationManager* Current = Manager.Get();
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Current ? FString::Printf(TEXT("%s  (%.1f s)"), *Current->StatusMessage, Current->GetElapsedSeconds()) : FString(TEXT("No simulation manager in this level"))));
	}
	if (ProgressBar)
	{
		ProgressBar->SetPercent(Current ? Current->Progress : 0.0f);
	}
	if (RunButton)
	{
		RunButton->SetIsEnabled(Current && !Current->IsRunning());
	}
	if (StopButton)
	{
		StopButton->SetIsEnabled(Current && Current->IsRunning());
	}
}

void UCompareWidget::SelectReceiver(int32 Index)
{
	const TArray<int32> Points = PointReceivers();
	SelectedPoint = Points.Num() > 0 ? ((Index % Points.Num()) + Points.Num()) % Points.Num() : 0;
	if (Points.IsValidIndex(SelectedPoint))
	{
		const AAcousticSimulationManager* Current = Manager.Get();
		if (Current && Current->Auralization)
		{
			Current->Auralization->SelectReceiver(Points[SelectedPoint]);
		}
	}
	RefreshResult();
}

void UCompareWidget::HandlePrevClicked()
{
	SelectReceiver(SelectedPoint - 1);
}

void UCompareWidget::HandleNextClicked()
{
	SelectReceiver(SelectedPoint + 1);
}

void UCompareWidget::SetEarlyZoom(bool bEarly)
{
	bEarlyZoom = bEarly;
	if (Plot)
	{
		Plot->TimeRangeSeconds = bEarly ? EarlyRangeSeconds : 0.0f;
	}
	if (EarlyButton)
	{
		EarlyButton->SetBackgroundColor(bEarly ? SelectedButtonColor : ButtonColor);
	}
	if (FullButton)
	{
		FullButton->SetBackgroundColor(bEarly ? ButtonColor : SelectedButtonColor);
	}
}

void UCompareWidget::HandleEarlyClicked()
{
	SetEarlyZoom(true);
}

void UCompareWidget::HandleFullClicked()
{
	SetEarlyZoom(false);
}

FString UCompareWidget::BuildMetricsTable() const
{
	const TSharedPtr<const FHybridResult> Hybrid = CurrentHybrid();
	const TArray<int32> Points = PointReceivers();
	if (!Hybrid.IsValid() || !Hybrid->IsValid() || !Points.IsValidIndex(SelectedPoint))
	{
		return TEXT("No hybrid result yet: press Run hybrid.");
	}
	const int32 Index = Points[SelectedPoint];
	if (!Hybrid->Hybrid->Receivers.IsValidIndex(Index))
	{
		return FString();
	}
	const FSimulationResult& Ray = *Hybrid->PureRay;
	const FSimulationResult& Beam = *Hybrid->PureBeam;
	const FSimulationResult& Mixed = *Hybrid->Hybrid;
	const FString RayCount = FString::Printf(TEXT("%d"), Ray.NumRays);
	const FString BeamCount = FString::Printf(TEXT("%d/%d"), Beam.NumRootBeams, Beam.NumBeamNodes);

	TArray<FCompareColumn> Columns;
	Columns.Add({ TEXT("Pure ray"), &Ray.Receivers[Index], Hybrid->RaySeconds, RayCount, TEXT("-"), FString() });
	Columns.Add({ FString::Printf(TEXT("Beam<=%d"), Hybrid->K), &Beam.Receivers[Index], Hybrid->BeamSeconds, TEXT("-"), BeamCount, FString() });
	Columns.Add({ TEXT("Hybrid"), &Mixed.Receivers[Index], Mixed.Timing.TotalSeconds, RayCount, BeamCount, FString() });
	// M9 (SPEC 3.8): metrics from the recorded click response of the industry plugins (Acoustics.Industry.MeasureClick in
	// Level_Amphitheatre_SteamAudio / _AudioTracing), matched by receiver id; "click" = not measured yet.
	TArray<TSharedPtr<const FReceiverResult>> IndustryResults;
	const UAcousticSimulationSubsystem* Subsystem = UAcousticSimulationSubsystem::Get(this);
	const TPair<const TCHAR*, const TCHAR*> IndustryColumns[] = { { TEXT("SteamAud"), TEXT("Steam Audio") }, { TEXT("AudioTr"), TEXT("Audio Tracing") } };
	for (const TPair<const TCHAR*, const TCHAR*>& Industry : IndustryColumns)
	{
		TSharedPtr<const FReceiverResult> Measured = Subsystem ? Subsystem->FindIndustryResult(Industry.Value, FName(*Mixed.Receivers[Index].Name)) : nullptr;
		IndustryResults.Add(Measured);
		Columns.Add({ Industry.Key, Measured.Get(), 0.0, TEXT("-"), TEXT("-"), Measured ? FString() : FString(TEXT("click")) });
	}

	constexpr int32 LabelWidth = 8;
	constexpr int32 CellWidth = 11;
	FString Text = Pad(TEXT(""), LabelWidth);
	for (const FCompareColumn& Column : Columns)
	{
		Text += Pad(Column.Name, CellWidth);
	}
	auto AddLine = [&](const TCHAR* Label, TFunctionRef<FString(const FCompareColumn&)> Cell)
	{
		Text += TEXT("\n") + Pad(Label, LabelWidth);
		for (const FCompareColumn& Column : Columns)
		{
			Text += Pad(Column.Receiver || !Column.Missing.IsEmpty() ? (Column.Receiver ? Cell(Column) : Column.Missing) : FString(TEXT("-")), CellWidth);
		}
	};
	auto Band = [](const FCompareColumn& Column) -> const FBandMetrics& { return Column.Receiver->Metrics.Band(0); };
	AddLine(TEXT("EDT"), [&](const FCompareColumn& C) { return DecayCell(Band(C).EDT, Band(C).bEDTValid); });
	AddLine(TEXT("T20"), [&](const FCompareColumn& C) { return DecayCell(Band(C).T20, Band(C).bT20Valid); });
	AddLine(TEXT("T30"), [&](const FCompareColumn& C) { return DecayCell(Band(C).T30, Band(C).bT30Valid); });
	AddLine(TEXT("C50"), [&](const FCompareColumn& C) { return FString::Printf(TEXT("%.2f dB"), Band(C).C50); });
	AddLine(TEXT("C80"), [&](const FCompareColumn& C) { return FString::Printf(TEXT("%.2f dB"), Band(C).C80); });
	AddLine(TEXT("D50"), [&](const FCompareColumn& C) { return FString::Printf(TEXT("%.3f"), Band(C).D50); });
	AddLine(TEXT("time"), [](const FCompareColumn& C) { return FString::Printf(TEXT("%.3f s"), C.Seconds); });
	AddLine(TEXT("rays"), [](const FCompareColumn& C) { return C.Rays; });
	AddLine(TEXT("beams"), [](const FCompareColumn& C) { return C.Beams; });
	Text += TEXT("\n(beams = root / tree nodes; T20 / T30 n/a for the beam column: orders <= K have no decay tail)");
	return Text;
}

FString UCompareWidget::BuildSplitSummary() const
{
	const TSharedPtr<const FHybridResult> Hybrid = CurrentHybrid();
	const TArray<int32> Points = PointReceivers();
	if (!Hybrid.IsValid() || !Hybrid->IsValid() || !Points.IsValidIndex(SelectedPoint) || !Hybrid->Stats.IsValidIndex(Points[SelectedPoint]))
	{
		return FString();
	}
	const int32 Index = Points[SelectedPoint];
	const FHybridReceiverStats& Stats = Hybrid->Stats[Index];
	const FString& Name = Hybrid->Hybrid->Receivers[Index].Name;
	const FString Limit = Stats.NumBeamDropped > 0
		? FString::Printf(TEXT("t_mix %.0f ms came first: %d beam reflections of order <= %d arrive later and are left to the rays."), Hybrid->MixingTimeSeconds * 1000.0, Stats.NumBeamDropped, Hybrid->K)
		: FString::Printf(TEXT("order K = %d came first: the last early beam reflection arrives at %.1f ms, before t_mix %.0f ms."), Hybrid->K, Stats.LastEarlyArrivalSeconds * 1000.0, Hybrid->MixingTimeSeconds * 1000.0);
	const double BlockError = Stats.RayExcludedEnergy > 0.0 ? (Stats.BeamEarlyEnergy - Stats.RayExcludedEnergy) / Stats.RayExcludedEnergy * 100.0 : 0.0;
	return FString::Printf(TEXT("Early part at %s: %d specular reflections (order 1..%d) from the beams; %s\n"
		"Exp. 3 (SPEC 5, missed reflections): the pure ray run with %d rays found %d of them and missed %d. Energy of this early block: beams %.4g, rays %.4g (%+.1f %%)."),
		*Name, Stats.NumBeamEarly, Hybrid->K, *Limit, Hybrid->PureRay->NumRays, Stats.NumFoundByRays, Stats.NumMissedByRays, Stats.BeamEarlyEnergy, Stats.RayExcludedEnergy, BlockError);
}

void UCompareWidget::RefreshResult()
{
	const TSharedPtr<const FHybridResult> Hybrid = CurrentHybrid();
	ShownHybrid = Hybrid.Get();
	const TArray<int32> Points = PointReceivers();
	SelectedPoint = Points.Num() > 0 ? FMath::Clamp(SelectedPoint, 0, Points.Num() - 1) : 0;
	const bool bValid = Hybrid.IsValid() && Hybrid->IsValid() && Points.IsValidIndex(SelectedPoint) && Hybrid->Hybrid->Receivers.IsValidIndex(Points[SelectedPoint]);
	if (ReceiverText)
	{
		ReceiverText->SetText(bValid ? FText::FromString(FString::Printf(TEXT("%s  (%d / %d)"), *Hybrid->Hybrid->Receivers[Points[SelectedPoint]].Name, SelectedPoint + 1, Points.Num())) : LOCTEXT("NoReceiver", "-"));
	}
	if (Plot)
	{
		if (bValid)
		{
			const int32 Index = Points[SelectedPoint];
			Plot->SetCurve(0, Hybrid->PureRay->Receivers[Index], TEXT("pure ray"), RayColor());
			Plot->SetCurve(1, Hybrid->PureBeam->Receivers[Index], FString::Printf(TEXT("beam <= %d"), Hybrid->K), BeamColor());
			Plot->SetCurve(2, Hybrid->Hybrid->Receivers[Index], TEXT("hybrid"), HybridColor());
		}
		else
		{
			Plot->ClearAll();
		}
	}
	const FString Table = BuildMetricsTable();
	if (TableText)
	{
		TableText->SetText(FText::FromString(Table));
	}
	const FString Summary = BuildSplitSummary();
	if (SummaryText)
	{
		SummaryText->SetText(FText::FromString(Summary));
	}
	if (bValid && ShownRunCount != (Manager.IsValid() ? Manager->GetRunCount() : -1))
	{
		ShownRunCount = Manager.IsValid() ? Manager->GetRunCount() : -1;
		UE_LOG(LogAcoustics, Log, TEXT("Compare panel, %s:\n%s\n%s"), *Hybrid->Hybrid->Receivers[Points[SelectedPoint]].Name, *Table, *Summary);
	}
	RefreshAuralization();
}

void UCompareWidget::SelectVariant(int32 Key)
{
	if (AAcousticsPlayerController* Controller = GetOwningPlayer<AAcousticsPlayerController>())
	{
		Controller->SelectedVariant = Key;
	}
	const AAcousticSimulationManager* Current = Manager.Get();
	if (UAuralizationComponent* Auralization = Current ? Current->Auralization.Get() : nullptr)
	{
		if (Auralization->SetVariantByKey(Key) && !Auralization->IsPlaying() && Auralization->HasImpulseResponse())
		{
			Auralization->Play();
		}
	}
	RefreshAuralization();
}

void UCompareWidget::RefreshAuralization()
{
	const AAcousticSimulationManager* Current = Manager.Get();
	const UAuralizationComponent* Auralization = Current ? Current->Auralization.Get() : nullptr;
	if (AuralizationText)
	{
		AuralizationText->SetText(FText::FromString(Auralization ? Auralization->DescribeState() : FString(TEXT("No auralization component"))));
	}
	const struct
	{
		UButton* Button;
		EAuralizationVariant Variant;
	} Buttons[] = { { DryButton, EAuralizationVariant::Dry }, { RayButton, EAuralizationVariant::Ray }, { BeamButton, EAuralizationVariant::Beam }, { HybridButton, EAuralizationVariant::Hybrid } };
	for (const auto& Entry : Buttons)
	{
		if (!Entry.Button)
		{
			continue;
		}
		Entry.Button->SetBackgroundColor(Auralization && Auralization->Variant == Entry.Variant ? SelectedButtonColor : ButtonColor);
		Entry.Button->SetIsEnabled(Auralization && (Entry.Variant == EAuralizationVariant::Dry || Auralization->HasVariantResult(Entry.Variant)));
	}
}

void UCompareWidget::HandleDryClicked()
{
	SelectVariant(1);
}

void UCompareWidget::HandleRayClicked()
{
	SelectVariant(2);
}

void UCompareWidget::HandleBeamClicked()
{
	SelectVariant(3);
}

void UCompareWidget::HandleHybridClicked()
{
	SelectVariant(4);
}

#undef LOCTEXT_NAMESPACE
