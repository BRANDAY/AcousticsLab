#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "MainMenuWidget.generated.h"

class UTextBlock;
class UVerticalBox;

/** One row of the main menu (SPEC §0): a section header or an entry that opens a level. */
USTRUCT(BlueprintType)
struct FMainMenuEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
	FText Label;

	/** Short map name (e.g. Level_Shoebox). None for headers and Quit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
	FName LevelName;

	/** URL options appended to the level, e.g. "mode=Benchmark". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
	FString Options;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
	bool bIsHeader = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
	bool bIsQuit = false;
};

DECLARE_DELEGATE_OneParam(FOnMainMenuEntryClicked, const FMainMenuEntry&);

/** Button that knows which menu entry it represents (UButton::OnClicked carries no sender). */
UCLASS()
class UMainMenuButton : public UButton
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FMainMenuEntry Entry;

	FOnMainMenuEntryClicked OnEntryClicked;

	void BindClick();

private:
	UFUNCTION()
	void HandleClicked();
};

/**
 * Main menu (SPEC §0). Logic lives here. The layout is optional: a Blueprint subclass under Content/UI may provide
 * MenuBox / TitleText / StatusText; without one the widget builds its own tree in C++, so it works with no assets.
 */
UCLASS()
class ACOUSTICSLAB_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMainMenuWidget(const FObjectInitializer& ObjectInitializer);

	/** Menu rows; the SPEC §0 tree by default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Menu")
	TArray<FMainMenuEntry> Entries;

	static TArray<FMainMenuEntry> MakeDefaultEntries();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

	/** Container the rows are added to. */
	UPROPERTY(BlueprintReadOnly, Category = "Menu", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> MenuBox;

	UPROPERTY(BlueprintReadOnly, Category = "Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

private:
	void BuildDefaultTree();
	void PopulateMenu();
	void HandleEntryClicked(const FMainMenuEntry& Entry);
};
