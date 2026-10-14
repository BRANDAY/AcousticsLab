#include "Framework/AcousticsPlayerController.h"

#include "AcousticsCore.h"
#include "Blueprint/UserWidget.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/AcousticsExplorerPawn.h"
#include "GameFramework/GameModeBase.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "Levels/AcousticsLevels.h"
#include "UI/MainMenuWidget.h"

AAcousticsPlayerController::AAcousticsPlayerController()
{
	MainMenuWidgetClass = UMainMenuWidget::StaticClass();
}

void AAcousticsPlayerController::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	bIsMainMenu = AcousticsLevels::IsMainMenuWorld(World);

	if (World)
	{
		// The level URL carries "?mode=..." from the main menu (see UMainMenuWidget::HandleEntryClicked).
		const FString Mode = World->URL.GetOption(*FString::Printf(TEXT("%s="), AcousticsLevels::ModeOption), TEXT(""));
		LevelMode = Mode.IsEmpty() ? FName() : FName(*Mode);
	}

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->ClearAllMappings();
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}

	if (bIsMainMenu)
	{
		ShowMainMenu();
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		UE_LOG(LogAcoustics, Log, TEXT("Level %s started, mode=%s"), *UGameplayStatics::GetCurrentLevelName(this, true), *LevelMode.ToString());
	}
}

void AAcousticsPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	BuildInputAssets();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!ensureMsgf(Input, TEXT("InputComponent is not a UEnhancedInputComponent; check DefaultInputComponentClass in Config/DefaultInput.ini")))
	{
		return;
	}

	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AAcousticsPlayerController::OnMove);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AAcousticsPlayerController::OnLook);
	Input->BindAction(MoveUpAction, ETriggerEvent::Triggered, this, &AAcousticsPlayerController::OnMoveUp);
	Input->BindAction(MenuAction, ETriggerEvent::Started, this, &AAcousticsPlayerController::OnMenu);
	for (int32 Index = 0; Index < VariantActions.Num(); ++Index)
	{
		Input->BindAction(VariantActions[Index], ETriggerEvent::Started, this, &AAcousticsPlayerController::OnSelectVariant, Index + 1);
	}
}

void AAcousticsPlayerController::BuildInputAssets()
{
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_AcousticsLab"));

	MoveAction = NewObject<UInputAction>(this, TEXT("IA_Move"));
	MoveAction->ValueType = EInputActionValueType::Axis2D;
	LookAction = NewObject<UInputAction>(this, TEXT("IA_Look"));
	LookAction->ValueType = EInputActionValueType::Axis2D;
	MoveUpAction = NewObject<UInputAction>(this, TEXT("IA_MoveUp"));
	MoveUpAction->ValueType = EInputActionValueType::Axis1D;
	MenuAction = NewObject<UInputAction>(this, TEXT("IA_MainMenu"));
	MenuAction->ValueType = EInputActionValueType::Boolean;

	// Maps a key to an action. bSwizzle routes the key to the Y axis (forward), bNegate flips the sign.
	auto MapAxisKey = [this](UInputAction* Action, const FKey& Key, bool bNegate, bool bSwizzle)
	{
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(Action, Key);
		if (bSwizzle)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this)); // default order YXZ: key value -> Y axis
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(this));
		}
	};

	// WASD / arrows: X = right, Y = forward.
	MapAxisKey(MoveAction, EKeys::W, false, true);
	MapAxisKey(MoveAction, EKeys::S, true, true);
	MapAxisKey(MoveAction, EKeys::D, false, false);
	MapAxisKey(MoveAction, EKeys::A, true, false);
	MapAxisKey(MoveAction, EKeys::Up, false, true);
	MapAxisKey(MoveAction, EKeys::Down, true, true);
	MapAxisKey(MoveAction, EKeys::Right, false, false);
	MapAxisKey(MoveAction, EKeys::Left, true, false);

	// E/Q and Space/C: up and down.
	MapAxisKey(MoveUpAction, EKeys::E, false, false);
	MapAxisKey(MoveUpAction, EKeys::Q, true, false);
	MapAxisKey(MoveUpAction, EKeys::SpaceBar, false, false);
	MapAxisKey(MoveUpAction, EKeys::C, true, false);

	// Mouse look. Y is negated so that pushing the mouse forward looks up, as in the engine templates.
	{
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(LookAction, EKeys::Mouse2D);
		UInputModifierNegate* NegateY = NewObject<UInputModifierNegate>(this);
		NegateY->bX = false;
		NegateY->bY = true;
		NegateY->bZ = false;
		Mapping.Modifiers.Add(NegateY);
	}

	MappingContext->MapKey(MenuAction, EKeys::Escape);

	// Keys 1-6 are reserved for the A/B variants (SPEC §3.7).
	constexpr int32 NumVariants = 6;
	const FKey VariantKeys[NumVariants] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six };
	VariantActions.Reset();
	for (int32 Index = 0; Index < NumVariants; ++Index)
	{
		UInputAction* Action = NewObject<UInputAction>(this, *FString::Printf(TEXT("IA_Variant%d"), Index + 1));
		Action->ValueType = EInputActionValueType::Boolean;
		MappingContext->MapKey(Action, VariantKeys[Index]);
		VariantActions.Add(Action);
	}
}

void AAcousticsPlayerController::ShowMainMenu()
{
	if (!MainMenuWidgetClass)
	{
		MainMenuWidgetClass = UMainMenuWidget::StaticClass();
	}
	MainMenuWidget = CreateWidget<UMainMenuWidget>(this, MainMenuWidgetClass);
	if (!MainMenuWidget)
	{
		UE_LOG(LogAcoustics, Error, TEXT("Could not create the main menu widget"));
		return;
	}
	MainMenuWidget->AddToViewport();

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MainMenuWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void AAcousticsPlayerController::ReturnToMainMenu()
{
	UGameplayStatics::OpenLevel(this, FName(*AcousticsLevels::MapPath(AcousticsLevels::MainMenu)));
}

void AAcousticsPlayerController::OnMove(const FInputActionValue& Value)
{
	if (AAcousticsExplorerPawn* Explorer = Cast<AAcousticsExplorerPawn>(GetPawn()))
	{
		Explorer->Move(Value.Get<FVector2D>());
	}
}

void AAcousticsPlayerController::OnLook(const FInputActionValue& Value)
{
	if (AAcousticsExplorerPawn* Explorer = Cast<AAcousticsExplorerPawn>(GetPawn()))
	{
		Explorer->Look(Value.Get<FVector2D>());
	}
}

void AAcousticsPlayerController::OnMoveUp(const FInputActionValue& Value)
{
	if (AAcousticsExplorerPawn* Explorer = Cast<AAcousticsExplorerPawn>(GetPawn()))
	{
		Explorer->MoveUp(Value.Get<float>());
	}
}

void AAcousticsPlayerController::OnMenu(const FInputActionValue& Value)
{
	if (!bIsMainMenu)
	{
		ReturnToMainMenu();
	}
}

void AAcousticsPlayerController::OnSelectVariant(const FInputActionValue& Value, int32 Variant)
{
	SelectedVariant = Variant;
	UE_LOG(LogAcoustics, Log, TEXT("A/B variant %d selected (auralization arrives in M4)"), Variant);
}
