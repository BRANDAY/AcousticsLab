#include "UI/MainMenuWidget.h"

#include "AcousticsCore.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Levels/AcousticsLevels.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "AcousticsMainMenu"

void UMainMenuButton::BindClick()
{
	OnClicked.AddUniqueDynamic(this, &UMainMenuButton::HandleClicked);
}

void UMainMenuButton::HandleClicked()
{
	OnEntryClicked.ExecuteIfBound(Entry);
}

UMainMenuWidget::UMainMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Entries = MakeDefaultEntries();
}

TArray<FMainMenuEntry> UMainMenuWidget::MakeDefaultEntries()
{
	auto Header = [](const FText& Label)
	{
		FMainMenuEntry Entry;
		Entry.Label = Label;
		Entry.bIsHeader = true;
		return Entry;
	};
	auto Level = [](const FText& Label, const TCHAR* LevelName, const TCHAR* Mode)
	{
		FMainMenuEntry Entry;
		Entry.Label = Label;
		Entry.LevelName = LevelName;
		Entry.Options = FString::Printf(TEXT("%s=%s"), AcousticsLevels::ModeOption, Mode);
		return Entry;
	};

	TArray<FMainMenuEntry> Result;
	Result.Add(Header(LOCTEXT("Section1", "1. Methods of M. Sikora")));
	Result.Add(Level(LOCTEXT("Item11a", "1.1 Image sources & beam tracing - Shoebox"), AcousticsLevels::Shoebox, TEXT("Beam")));
	Result.Add(Level(LOCTEXT("Item11b", "1.1 Image sources & beam tracing - Amphitheatre"), AcousticsLevels::Amphitheatre, TEXT("Beam")));
	Result.Add(Level(LOCTEXT("Item12", "1.2 Beam tracing with refraction"), AcousticsLevels::Refraction, TEXT("Refraction")));
	Result.Add(Level(LOCTEXT("Item13", "1.3 Thread-level parallelization (benchmark)"), AcousticsLevels::Shoebox, TEXT("Benchmark")));
	Result.Add(Level(LOCTEXT("Item14", "1.4 Hybrid beam/ray tracing"), AcousticsLevels::Shoebox, TEXT("Hybrid")));
	Result.Add(Level(LOCTEXT("Item15", "1.5 Audio AR soundwalk (optional)"), AcousticsLevels::Amphitheatre, TEXT("Soundwalk")));
	Result.Add(Header(LOCTEXT("Section2", "2. Industry")));
	Result.Add(Level(LOCTEXT("Item21", "2.1 Steam Audio (Valve)"), AcousticsLevels::AmphitheatreSteamAudio, TEXT("SteamAudio")));
	Result.Add(Level(LOCTEXT("Item22", "2.2 Audio Tracing (Pinpoint, Fab)"), AcousticsLevels::AmphitheatreAudioTracing, TEXT("AudioTracing")));
	Result.Add(Header(LOCTEXT("Section3", "3. Compare")));
	Result.Add(Level(LOCTEXT("Item3", "Metrics table & A/B listening"), AcousticsLevels::Shoebox, TEXT("Compare")));

	FMainMenuEntry Quit;
	Quit.Label = LOCTEXT("Quit", "Quit");
	Quit.bIsQuit = true;
	Result.Add(Quit);
	return Result;
}

TSharedRef<SWidget> UMainMenuWidget::RebuildWidget()
{
	// No Blueprint layout: build the tree in C++ so the menu works without any asset.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultTree();
	}
	return Super::RebuildWidget();
}

void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Entries.IsEmpty())
	{
		Entries = MakeDefaultEntries();
	}
	PopulateMenu();
}

void UMainMenuWidget::BuildDefaultTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = Root;

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.02f, 0.025f, 0.035f, 0.92f));
	if (UCanvasPanelSlot* BackdropSlot = Root->AddChildToCanvas(Backdrop))
	{
		BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackdropSlot->SetOffsets(FMargin(0.f));
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuColumn"));
	if (UCanvasPanelSlot* ColumnSlot = Root->AddChildToCanvas(Column))
	{
		ColumnSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		ColumnSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ColumnSlot->SetAutoSize(true);
	}

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetText(LOCTEXT("Title", "ACOUSTICS LAB"));
	TitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 40));
	TitleText->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	UTextBlock* Subtitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SubtitleText"));
	Subtitle->SetText(LOCTEXT("Subtitle", "Geometric acoustics after M. Sikora in Unreal Engine 5"));
	Subtitle->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 14));
	Subtitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.7f)));
	Subtitle->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* SubtitleSlot = Column->AddChildToVerticalBox(Subtitle))
	{
		SubtitleSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 12.f));
		SubtitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	MenuBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuBox"));
	Column->AddChildToVerticalBox(MenuBox);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetText(LOCTEXT("Status", "M0 project skeleton: every item opens a placeholder level, Esc returns here"));
	StatusText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12));
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.6f)));
	if (UVerticalBoxSlot* StatusSlot = Column->AddChildToVerticalBox(StatusText))
	{
		StatusSlot->SetPadding(FMargin(0.f, 16.f, 0.f, 0.f));
	}
}

void UMainMenuWidget::PopulateMenu()
{
	if (!MenuBox || !WidgetTree)
	{
		return;
	}
	MenuBox->ClearChildren();

	for (const FMainMenuEntry& Entry : Entries)
	{
		if (Entry.bIsHeader)
		{
			UTextBlock* Header = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Header->SetText(Entry.Label);
			Header->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 18));
			Header->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.8f, 0.35f)));
			if (UVerticalBoxSlot* HeaderSlot = MenuBox->AddChildToVerticalBox(Header))
			{
				HeaderSlot->SetPadding(FMargin(0.f, 14.f, 0.f, 4.f));
			}
			continue;
		}

		UMainMenuButton* Button = WidgetTree->ConstructWidget<UMainMenuButton>(UMainMenuButton::StaticClass());
		Button->Entry = Entry;
		Button->OnEntryClicked.BindUObject(this, &UMainMenuWidget::HandleEntryClicked);
		Button->BindClick();
		Button->SetBackgroundColor(Entry.bIsQuit ? FLinearColor(0.35f, 0.12f, 0.12f) : FLinearColor(0.14f, 0.17f, 0.22f));

		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(Entry.Label);
		Label->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 16));
		Button->AddChild(Label);
		if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(Label->Slot))
		{
			LabelSlot->SetPadding(FMargin(18.f, 7.f));
			LabelSlot->SetHorizontalAlignment(HAlign_Left);
		}

		if (UVerticalBoxSlot* ButtonSlot = MenuBox->AddChildToVerticalBox(Button))
		{
			ButtonSlot->SetPadding(FMargin(Entry.bIsQuit ? 0.f : 20.f, Entry.bIsQuit ? 20.f : 3.f, 0.f, 3.f));
			ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}
}

void UMainMenuWidget::HandleEntryClicked(const FMainMenuEntry& Entry)
{
	if (Entry.bIsQuit)
	{
		UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
		return;
	}
	if (Entry.LevelName.IsNone())
	{
		return;
	}

	const FString MapPath = AcousticsLevels::MapPath(Entry.LevelName.ToString());
	UE_LOG(LogAcoustics, Log, TEXT("Main menu: opening %s?%s"), *MapPath, *Entry.Options);
	if (StatusText)
	{
		StatusText->SetText(FText::Format(LOCTEXT("Loading", "Loading {0}..."), FText::FromName(Entry.LevelName)));
	}
	UGameplayStatics::OpenLevel(this, FName(*MapPath), true, Entry.Options);
}

#undef LOCTEXT_NAMESPACE
