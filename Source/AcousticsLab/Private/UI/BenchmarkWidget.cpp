#include "UI/BenchmarkWidget.h"

#include "AcousticsCore.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Geometry/AcousticScene.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Simulation/AcousticSimulationManager.h"
#include "Solvers/BeamBenchmark.h"
#include "Styling/CoreStyle.h"
#include "UI/SimulationHUDWidget.h"

#define LOCTEXT_NAMESPACE "AcousticsBenchmark"

namespace
{
	const FName FieldLevel(TEXT("BenchLevel"));
	const FName FieldOrder(TEXT("BenchOrder"));
	const FName FieldRepetitions(TEXT("BenchRepetitions"));
	const FName FieldSplit(TEXT("BenchSplit"));

	const FLinearColor PanelColor(0.02f, 0.025f, 0.035f, 0.88f);
	const FLinearColor LabelColor(0.75f, 0.75f, 0.8f);
	const FLinearColor ValueColor(0.95f, 0.95f, 0.95f);
	const FLinearColor AccentColor(0.95f, 0.8f, 0.35f);
	const FLinearColor MutedColor(0.55f, 0.55f, 0.6f);
	const FLinearColor ButtonColor(0.14f, 0.17f, 0.22f);
	const FLinearColor ChartBackground(0.01f, 0.012f, 0.018f, 0.9f);
	const FLinearColor GridColor(1.0f, 1.0f, 1.0f, 0.08f);
	const FLinearColor IdealColor(0.6f, 0.6f, 0.65f, 0.7f);
	const FLinearColor CoreMarkerColor(0.9f, 0.5f, 0.9f, 0.45f);

	FString ThreadList(const TArray<int32>& Counts)
	{
		return FString::JoinBy(Counts, TEXT(", "), [](int32 Count) { return FString::FromInt(Count); });
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Chart

FLinearColor UBenchmarkChartWidget::ModeColor(bool bUseBVH)
{
	return bUseBVH ? FLinearColor(0.35f, 0.85f, 1.0f) : FLinearColor(1.0f, 0.55f, 0.2f);
}

void UBenchmarkChartWidget::SetData(const TArray<FBenchmarkCell>& InCells, const TArray<int32>& InThreadCounts, int32 InPhysicalCores)
{
	Cells = InCells;
	ThreadCounts = InThreadCounts;
	PhysicalCores = InPhysicalCores;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UBenchmarkChartWidget::Clear()
{
	Cells.Reset();
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UBenchmarkChartWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateFontInfo BoldFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), WhiteBrush, ESlateDrawEffect::None, ChartBackground);
	++LayerId;

	const float Left = 36.0f;
	const float Right = FMath::Max(Left + 10.0f, Size.X - 12.0f);
	const float Top = 24.0f;
	const float Bottom = FMath::Max(Top + 10.0f, Size.Y - 38.0f);
	int32 MaxThreads = 1;
	for (const int32 Count : ThreadCounts)
	{
		MaxThreads = FMath::Max(MaxThreads, Count);
	}
	const float MaxX = static_cast<float>(MaxThreads);
	const float MaxY = static_cast<float>(MaxThreads);
	auto XOf = [&](float Threads) { return Left + FMath::Clamp(Threads / MaxX, 0.0f, 1.0f) * (Right - Left); };
	auto YOf = [&](float Speedup) { return Bottom - FMath::Clamp(Speedup / MaxY, 0.0f, 1.0f) * (Bottom - Top); };
	auto Line = [&](const TArray<FVector2f>& Points, const FLinearColor& Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
	};
	auto Text = [&](const FString& String, float X, float Y, const FLinearColor& Color, const FSlateFontInfo& InFont, float AlignX = 0.0f)
	{
		X -= AlignX * static_cast<float>(FontMeasure->Measure(String, InFont).X);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId, AllottedGeometry.ToOffsetPaintGeometry(FVector2f(X, Y)), String, InFont, ESlateDrawEffect::None, Color);
	};

	// Grid: speedup rows every 1 or 2, a column per measured thread count.
	const int32 Step = MaxThreads > 8 ? 2 : 1;
	for (int32 Speedup = 0; Speedup <= MaxThreads; Speedup += Step)
	{
		const float Y = YOf(static_cast<float>(Speedup));
		Line({ FVector2f(Left, Y), FVector2f(Right, Y) }, GridColor, 1.0f);
		Text(FString::Printf(TEXT("%d"), Speedup), Left - 5.0f, Y - 7.0f, LabelColor, Font, 1.0f);
	}
	for (const int32 Count : ThreadCounts)
	{
		const float X = XOf(static_cast<float>(Count));
		Line({ FVector2f(X, Top), FVector2f(X, Bottom) }, GridColor, 1.0f);
		Text(FString::FromInt(Count), X, Bottom + 3.0f, LabelColor, Font, 0.5f);
	}
	Text(TEXT("threads"), 0.5f * (Left + Right), Bottom + 17.0f, MutedColor, Font, 0.5f);
	const FString Title = TEXT("Speedup T1 / Tn");
	Text(Title, Left, 4.0f, AccentColor, BoldFont);
	++LayerId;

	// References: the ideal linear speedup and the physical core count (beyond it only hyperthreads are added).
	Line({ FVector2f(XOf(1.0f), YOf(1.0f)), FVector2f(XOf(MaxX), YOf(MaxY)) }, IdealColor, 1.0f);
	Text(TEXT("ideal"), XOf(MaxX) - 4.0f, YOf(MaxY) + 2.0f, IdealColor, Font, 1.0f);
	if (PhysicalCores > 1 && PhysicalCores < MaxThreads)
	{
		const float X = XOf(static_cast<float>(PhysicalCores));
		Line({ FVector2f(X, Top), FVector2f(X, Bottom) }, CoreMarkerColor, 1.5f);
		Text(FString::Printf(TEXT("%d physical cores"), PhysicalCores), X + 3.0f, Top + 2.0f, CoreMarkerColor, Font);
	}
	++LayerId;

	// One curve per BVH mode, dots at the measured counts; the legend continues the title row (the top right holds "ideal").
	float LegendX = Left + static_cast<float>(FontMeasure->Measure(Title, BoldFont).X) + 16.0f;
	for (const bool bBVH : { true, false })
	{
		TArray<FVector2f> Points;
		for (const FBenchmarkCell& Cell : Cells)
		{
			if (Cell.bUseBVH == bBVH && Cell.Speedup > 0.0)
			{
				Points.Add(FVector2f(XOf(static_cast<float>(Cell.Threads)), YOf(static_cast<float>(Cell.Speedup))));
			}
		}
		if (Points.Num() == 0)
		{
			continue;
		}
		const FLinearColor Color = ModeColor(bBVH);
		if (Points.Num() > 1)
		{
			Line(Points, Color, 2.0f);
		}
		for (const FVector2f& Point : Points)
		{
			Line({ Point - FVector2f(3.0f, 0.0f), Point + FVector2f(3.0f, 0.0f) }, Color, 6.0f);
		}
		const FString Label = bBVH ? TEXT("BVH on") : TEXT("BVH off (brute force)");
		Text(Label, LegendX, 5.0f, Color, Font);
		LegendX += static_cast<float>(FontMeasure->Measure(Label, Font).X) + 14.0f;
	}
	if (Cells.Num() == 0)
	{
		Text(TEXT("no runs yet"), 0.5f * (Left + Right), 0.5f * (Top + Bottom), MutedColor, Font, 0.5f);
	}
	return LayerId + 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Panel

UBenchmarkWidget::UBenchmarkWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UBenchmarkWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultTree();
	}
	return Super::RebuildWidget();
}

UTextBlock* UBenchmarkWidget::MakeText(FName Name, int32 FontSize, const FLinearColor& Color, bool bBold)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Text->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FontSize));
	Text->SetColorAndOpacity(FSlateColor(Color));
	return Text;
}

UButton* UBenchmarkWidget::MakeButton(FName Name, const FText& Label, const FLinearColor& Color)
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

USimulationSettingField* UBenchmarkWidget::MakeField(FName Id, float Width)
{
	USimulationSettingField* Field = WidgetTree->ConstructWidget<USimulationSettingField>(USimulationSettingField::StaticClass());
	Field->FieldId = Id;
	Field->OnFieldCommitted.BindUObject(this, &UBenchmarkWidget::HandleFieldCommitted);
	Field->BindCommit();
	Field->SetMinDesiredWidth(Width);
	Field->SetJustification(ETextJustify::Right);
	Fields.Add(Id, Field);
	return Field;
}

UCheckBox* UBenchmarkWidget::MakeCheck(FName Name, bool bChecked)
{
	UCheckBox* Check = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), Name);
	Check->SetIsChecked(bChecked);
	return Check;
}

void UBenchmarkWidget::BuildDefaultTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = Root;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BenchmarkPanel"));
	Panel->SetBrushColor(PanelColor);
	Panel->SetPadding(FMargin(14.f, 10.f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		PanelSlot->SetAlignment(FVector2D(1.f, 0.f));
		PanelSlot->SetPosition(FVector2D(-24.f, 24.f));
		PanelSlot->SetAutoSize(true);
	}
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BenchmarkColumn"));
	Panel->SetContent(Column);
	auto AddRow = [Column](UWidget* Widget, float Top, float Bottom)
	{
		if (UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Widget))
		{
			RowSlot->SetPadding(FMargin(0.f, Top, 0.f, Bottom));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	};
	auto AddToLine = [](UHorizontalBox* Line, UWidget* Widget, float RightPadding)
	{
		if (UHorizontalBoxSlot* LineSlot = Line->AddChildToHorizontalBox(Widget))
		{
			LineSlot->SetPadding(FMargin(0.f, 0.f, RightPadding, 0.f));
			LineSlot->SetVerticalAlignment(VAlign_Center);
		}
	};

	UTextBlock* Title = MakeText(TEXT("BenchmarkTitle"), 16, AccentColor, true);
	Title->SetText(LOCTEXT("Title", "1.3 Thread-level parallelization: beam tracer benchmark"));
	AddRow(Title, 0.f, 2.f);

	UTextBlock* Note = MakeText(TEXT("BenchmarkNote"), 10, LabelColor);
	Note->SetAutoWrapText(true);
	Note->SetText(LOCTEXT("Note", "Sikora & Mateljan (2013) give independent branches of the beam tree to threads and find the triangles a beam can hit with an octree; "
		"here a BVH plays the role of the octree (SPEC 2.7). One task per root beam on a dedicated pool of N threads; subtrees merged in root order, "
		"so every thread count gives a bit-identical tree and histograms (column 'same')."));
	if (USizeBox* NoteSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("NoteSize")))
	{
		NoteSize->SetWidthOverride(520.f);
		NoteSize->SetContent(Note);
		AddRow(NoteSize, 0.f, 6.f);
	}

	SceneText = MakeText(TEXT("SceneText"), 11, ValueColor);
	AddRow(SceneText, 0.f, 4.f);

	// Matrix settings.
	UHorizontalBox* FieldLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BenchFieldLine"));
	auto AddField = [&](FName Id, const FText& Label)
	{
		UTextBlock* LabelText = MakeText(NAME_None, 11, LabelColor);
		LabelText->SetText(Label);
		AddToLine(FieldLine, LabelText, 4.f);
		AddToLine(FieldLine, MakeField(Id, 38.f), 12.f);
	};
	AddField(FieldLevel, LOCTEXT("Level", "Subdiv"));
	AddField(FieldOrder, LOCTEXT("Order", "Order"));
	AddField(FieldRepetitions, LOCTEXT("Reps", "Reps"));
	AddField(FieldSplit, LOCTEXT("Split", "Split n"));
	AddRow(FieldLine, 0.f, 4.f);

	UHorizontalBox* CheckLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BenchCheckLine"));
	auto AddCheck = [&](UCheckBox* Check, const FText& Label)
	{
		AddToLine(CheckLine, Check, 4.f);
		UTextBlock* LabelText = MakeText(NAME_None, 11, LabelColor);
		LabelText->SetText(Label);
		AddToLine(CheckLine, LabelText, 14.f);
	};
	BvhOnCheck = MakeCheck(TEXT("BvhOnCheck"), Options.bWithBVH);
	BvhOffCheck = MakeCheck(TEXT("BvhOffCheck"), Options.bWithoutBVH);
	GridsCheck = MakeCheck(TEXT("GridsCheck"), Options.bIncludeReceiverGrids);
	AddCheck(BvhOnCheck, LOCTEXT("BvhOn", "BVH on"));
	AddCheck(BvhOffCheck, LOCTEXT("BvhOff", "BVH off (brute force)"));
	AddCheck(GridsCheck, LOCTEXT("Grids", "Receiver grids"));
	AddRow(CheckLine, 0.f, 4.f);

	ThreadsText = MakeText(TEXT("ThreadsText"), 11, LabelColor);
	AddRow(ThreadsText, 0.f, 6.f);

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BenchButtons"));
	RunButton = MakeButton(TEXT("BenchRunButton"), LOCTEXT("Run", "Run benchmark"), FLinearColor(0.15f, 0.4f, 0.2f));
	StopButton = MakeButton(TEXT("BenchStopButton"), LOCTEXT("Stop", "Stop"), FLinearColor(0.4f, 0.15f, 0.15f));
	ExportButton = MakeButton(TEXT("BenchExportButton"), LOCTEXT("Export", "Export CSV"), ButtonColor);
	for (UButton* Button : { RunButton.Get(), StopButton.Get(), ExportButton.Get() })
	{
		if (UHorizontalBoxSlot* ButtonSlot = Buttons->AddChildToHorizontalBox(Button))
		{
			ButtonSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
			ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}
	AddRow(Buttons, 0.f, 6.f);

	StatusText = MakeText(TEXT("BenchStatusText"), 11, LabelColor);
	StatusText->SetText(LOCTEXT("Idle", "Idle"));
	AddRow(StatusText, 0.f, 2.f);
	ProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("BenchProgress"));
	ProgressBar->SetPercent(0.f);
	ProgressBar->SetFillColorAndOpacity(AccentColor);
	USizeBox* ProgressSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BenchProgressSize"));
	ProgressSize->SetHeightOverride(8.f);
	ProgressSize->SetContent(ProgressBar);
	AddRow(ProgressSize, 0.f, 8.f);

	TableText = MakeText(TEXT("BenchTable"), 10, ValueColor);
	TableText->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 10));
	AddRow(TableText, 0.f, 6.f);

	Chart = WidgetTree->ConstructWidget<UBenchmarkChartWidget>(UBenchmarkChartWidget::StaticClass(), TEXT("BenchChart"));
	USizeBox* ChartSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BenchChartSize"));
	ChartSize->SetWidthOverride(520.f);
	ChartSize->SetHeightOverride(252.f);
	ChartSize->SetContent(Chart);
	AddRow(ChartSize, 0.f, 6.f);

	CsvText = MakeText(TEXT("BenchCsvText"), 10, MutedColor);
	CsvText->SetAutoWrapText(true);
	AddRow(CsvText, 0.f, 4.f);

	UTextBlock* Hint = MakeText(TEXT("BenchHint"), 10, MutedColor);
	Hint->SetText(LOCTEXT("Hint", "B: benchmark / simulation panel   Tab: cursor / fly   Enter in a field applies it   Esc: main menu"));
	AddRow(Hint, 0.f, 0.f);
}

void UBenchmarkWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (RunButton)
	{
		RunButton->OnClicked.AddUniqueDynamic(this, &UBenchmarkWidget::HandleRunClicked);
	}
	if (StopButton)
	{
		StopButton->OnClicked.AddUniqueDynamic(this, &UBenchmarkWidget::HandleStopClicked);
	}
	if (ExportButton)
	{
		ExportButton->OnClicked.AddUniqueDynamic(this, &UBenchmarkWidget::HandleExportClicked);
	}
	if (BvhOnCheck)
	{
		BvhOnCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UBenchmarkWidget::HandleBvhOnChanged);
	}
	if (BvhOffCheck)
	{
		BvhOffCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UBenchmarkWidget::HandleBvhOffChanged);
	}
	if (GridsCheck)
	{
		GridsCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UBenchmarkWidget::HandleGridsChanged);
	}

	// Defaults from the level's manager (what a Run of the beam tracer would use).
	if (const AAcousticSimulationManager* Manager = AAcousticSimulationManager::Find(GetWorld()))
	{
		if (Options.SubdivisionLevel < 0)
		{
			Options.SubdivisionLevel = Manager->Settings.SubdivisionLevel;
		}
		if (Options.MaxBeamOrder < 0)
		{
			Options.MaxBeamOrder = Manager->Settings.MaxBeamOrder;
		}
	}
	RefreshFields();
	RefreshSceneText();
	if (Chart)
	{
		Chart->SetData({}, FBenchmarkResult::DefaultThreadCounts(FPlatformMisc::NumberOfCoresIncludingHyperthreads(), FPlatformMisc::NumberOfCores()), FPlatformMisc::NumberOfCores());
	}
}

void UBenchmarkWidget::NativeDestruct()
{
	if (Running.IsValid())
	{
		Running->Progress.RequestCancel();
		Task.Wait();
		Running.Reset();
	}
	Super::NativeDestruct();
}

void UBenchmarkWidget::RefreshFields()
{
	auto SetField = [this](FName Id, int32 Value)
	{
		if (const TObjectPtr<USimulationSettingField>* Field = Fields.Find(Id))
		{
			(*Field)->SetText(FText::AsNumber(Value, &FNumberFormattingOptions::DefaultNoGrouping()));
		}
	};
	SetField(FieldLevel, Options.SubdivisionLevel);
	SetField(FieldOrder, Options.MaxBeamOrder);
	SetField(FieldRepetitions, Options.Repetitions);
	SetField(FieldSplit, Options.SurfaceDivisions);

	const int32 Logical = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
	const int32 Physical = FPlatformMisc::NumberOfCores();
	const TArray<int32> Counts = Options.ThreadCounts.Num() > 0 ? Options.ThreadCounts : FBenchmarkResult::DefaultThreadCounts(Logical, Physical);
	const int32 Modes = (Options.bWithBVH ? 1 : 0) + (Options.bWithoutBVH ? 1 : 0);
	if (ThreadsText)
	{
		ThreadsText->SetText(FText::FromString(FString::Printf(TEXT("Threads %s (%d physical / %d logical cores) x %d BVH mode(s) x %d = %d runs, %d root beams"),
			*ThreadList(Counts), Physical, Logical, Modes, Options.Repetitions, Counts.Num() * Modes * Options.Repetitions, FBeamTracerSettings::NumRootBeams(Options.SubdivisionLevel))));
	}
}

void UBenchmarkWidget::RefreshSceneText()
{
	if (!SceneText)
	{
		return;
	}
	const AAcousticSimulationManager* Manager = AAcousticSimulationManager::Find(GetWorld());
	FString Text = FString::Printf(TEXT("Scene: %s (source and refraction %s as in the simulation panel)"), *UGameplayStatics::GetCurrentLevelName(this, true),
		Manager && Manager->Settings.bRefraction ? TEXT("on") : TEXT("off"));
	if (LastResult.Info.NumTriangles > 0)
	{
		Text = FString::Printf(TEXT("Last run: %s; %d triangles, %d receivers, level %d (%d root beams), order %d"), *LastResult.Info.SceneName, LastResult.Info.NumTriangles,
			LastResult.Info.NumReceivers, LastResult.Info.SubdivisionLevel, LastResult.Info.NumRootBeams, LastResult.Info.MaxBeamOrder);
	}
	SceneText->SetText(FText::FromString(Text));
}

void UBenchmarkWidget::HandleFieldCommitted(FName FieldId, const FText& Text)
{
	const int32 Value = FCString::Atoi(*Text.ToString());
	if (FieldId == FieldLevel)
	{
		Options.SubdivisionLevel = FMath::Clamp(Value, 0, 7);
	}
	else if (FieldId == FieldOrder)
	{
		Options.MaxBeamOrder = FMath::Clamp(Value, 0, 10);
	}
	else if (FieldId == FieldRepetitions)
	{
		Options.Repetitions = FMath::Clamp(Value, 1, 20);
	}
	else if (FieldId == FieldSplit)
	{
		Options.SurfaceDivisions = FMath::Clamp(Value, 1, 32);
	}
	RefreshFields();
}

void UBenchmarkWidget::HandleBvhOnChanged(bool bChecked)
{
	Options.bWithBVH = bChecked;
	RefreshFields();
}

void UBenchmarkWidget::HandleBvhOffChanged(bool bChecked)
{
	Options.bWithoutBVH = bChecked;
	RefreshFields();
}

void UBenchmarkWidget::HandleGridsChanged(bool bChecked)
{
	Options.bIncludeReceiverGrids = bChecked;
}

void UBenchmarkWidget::HandleRunClicked()
{
	StartBenchmark();
}

void UBenchmarkWidget::HandleStopClicked()
{
	StopBenchmark();
}

void UBenchmarkWidget::HandleExportClicked()
{
	ExportCsv();
}

bool UBenchmarkWidget::StartBenchmark()
{
	if (Running.IsValid())
	{
		return false;
	}
	TSharedPtr<FRunningBenchmark> State = MakeShared<FRunningBenchmark>();
	FString Error;
	if (!UAcousticsBenchmarkLibrary::PrepareJob(GetWorld(), Options, State->Job, Error))
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("Cannot run: %s"), *Error)));
		return false;
	}
	State->Job.Info.Note = FString::Printf(TEXT("panel in %s (PIE / game, the renderer keeps running)"), *State->Job.LevelName);
	LastLevelName = State->Job.LevelName;
	Running = State;
	ShownRuns = -1;
	if (Chart)
	{
		Chart->Clear();
	}
	if (CsvText)
	{
		CsvText->SetText(FText::GetEmpty());
	}
	UE_LOG(LogAcoustics, Log, TEXT("Benchmark panel: starting on %s (%s)"), *State->Job.Info.SceneName, *State->Job.Scene->DescribeStats());
	Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [State]()
	{
		State->Result = FBeamBenchmark::Run(*State->Job.Scene, State->Job.Source, State->Job.Receivers, State->Job.Settings, State->Job.Matrix, State->Job.Info, &State->Progress);
		State->bDone.store(true);
	}, UE::Tasks::ETaskPriority::BackgroundNormal); // waits on the beam pool for minutes: keep the foreground workers free
	return true;
}

void UBenchmarkWidget::StopBenchmark()
{
	if (Running.IsValid())
	{
		Running->Progress.RequestCancel();
		StatusText->SetText(LOCTEXT("Stopping", "Stopping..."));
	}
}

bool UBenchmarkWidget::ExportCsv()
{
	FBenchmarkResult ToSave = LastResult;
	if (Running.IsValid())
	{
		ToSave.Info = Running->Job.Info;
		Running->Progress.CopyRuns(ToSave.Runs, ToSave.ThreadCounts);
		ToSave.bCancelled = true; // partial
	}
	if (ToSave.Runs.Num() == 0)
	{
		StatusText->SetText(LOCTEXT("NothingToExport", "Nothing to export yet"));
		return false;
	}
	FString RunsPath;
	FString SummaryPath;
	const bool bSaved = UAcousticsBenchmarkLibrary::SaveResult(ToSave, LastLevelName.IsEmpty() ? FString(TEXT("Level")) : LastLevelName, RunsPath, SummaryPath);
	if (CsvText)
	{
		CsvText->SetText(FText::FromString(bSaved ? FString::Printf(TEXT("CSV: %s\n     %s"), *RunsPath, *SummaryPath) : FString::Printf(TEXT("Could not write %s"), *RunsPath)));
	}
	return bSaved;
}

void UBenchmarkWidget::RefreshResults(const TArray<FBenchmarkRun>& Runs, const TArray<int32>& Threads, const FBenchmarkInfo& Info)
{
	FBenchmarkResult View;
	View.Info = Info;
	View.Runs = Runs;
	View.ThreadCounts = Threads;
	const TArray<FBenchmarkCell> Cells = View.Summarize();
	if (TableText)
	{
		FString Table = Runs.Num() > 0 ? View.DescribeTable() : FString(TEXT("(no runs yet)"));
		if (Runs.Num() > 0)
		{
			Table += FString::Printf(TEXT("%d runs, %d nodes, results identical for all: %s"), Runs.Num(), Runs.Last().NumNodes, View.AllIdentical() ? TEXT("yes") : TEXT("NO"));
		}
		TableText->SetText(FText::FromString(Table));
	}
	if (Chart)
	{
		Chart->SetData(Cells, Threads, Info.PhysicalCores > 0 ? Info.PhysicalCores : FPlatformMisc::NumberOfCores());
	}
}

void UBenchmarkWidget::FinishBenchmark()
{
	Task.Wait();
	LastResult = MoveTemp(Running->Result);
	Running.Reset();
	RefreshResults(LastResult.Runs, LastResult.ThreadCounts, LastResult.Info);
	RefreshSceneText();
	if (ProgressBar)
	{
		ProgressBar->SetPercent(LastResult.bCancelled ? ProgressBar->GetPercent() : 1.0f);
	}
	StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s: %d runs in %.1f s"), LastResult.bCancelled ? TEXT("Cancelled") : TEXT("Finished"), LastResult.Runs.Num(), LastResult.WallSeconds)));
	if (!LastResult.bCancelled && LastResult.Runs.Num() > 0)
	{
		ExportCsv();
	}
}

void UBenchmarkWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!Running.IsValid())
	{
		return;
	}
	if (Running->bDone.load())
	{
		FinishBenchmark();
		return;
	}
	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator < 0.1f)
	{
		return;
	}
	RefreshAccumulator = 0.0f;
	const FBenchmarkProgress& Progress = Running->Progress;
	if (ProgressBar)
	{
		ProgressBar->SetPercent(Progress.Fraction());
	}
	const int32 Completed = Progress.CompletedRuns.load();
	const int32 Total = Progress.TotalRuns.load();
	if (Progress.IsCancelRequested())
	{
		StatusText->SetText(LOCTEXT("Stopping", "Stopping..."));
	}
	else if (Progress.bWarmingUp.load())
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("Warm-up run (%d threads, BVH on)..."), Progress.CurrentThreads.load())));
	}
	else
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("Run %d of %d: %d threads, BVH %s, repetition %d  (%.0f %%)"), FMath::Min(Completed + 1, Total), Total,
			Progress.CurrentThreads.load(), Progress.bCurrentBVH.load() ? TEXT("on") : TEXT("off"), Progress.CurrentRepetition.load() + 1, 100.0f * Progress.Fraction())));
	}
	if (Completed != ShownRuns)
	{
		ShownRuns = Completed;
		TArray<FBenchmarkRun> Runs;
		TArray<int32> Threads;
		Progress.CopyRuns(Runs, Threads);
		RefreshResults(Runs, Threads, Running->Job.Info);
	}
}

#undef LOCTEXT_NAMESPACE
