#include "Framework/AcousticsHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Framework/AcousticsPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Levels/LevelSetupActor.h"

void AAcousticsHUD::DrawHUD()
{
	Super::DrawHUD();

	const AAcousticsPlayerController* Controller = Cast<AAcousticsPlayerController>(GetOwningPlayerController());
	if (!Canvas || !Controller || Controller->IsInMainMenu())
	{
		return;
	}

	FString Title = UGameplayStatics::GetCurrentLevelName(this, true);
	if (const ALevelSetupActor* Setup = ALevelSetupActor::Find(GetWorld()))
	{
		if (!Setup->DisplayName.IsEmpty())
		{
			Title += FString(TEXT(" - ")) + Setup->DisplayName.ToString();
		}
	}
	const FString Mode = Controller->LevelMode.IsNone() ? FString(TEXT("default")) : Controller->LevelMode.ToString();
	const FString Line1 = FString::Printf(TEXT("%s   |   mode: %s   |   A/B variant: %d"), *Title, *Mode, Controller->SelectedVariant);
	const FString Line2 = Controller->IsUIMode()
		? TEXT("Cursor mode: edit the simulation panel, WASD still flies   Tab: fly mode   1-6: A/B variant   Esc: main menu")
		: TEXT("WASD + mouse: fly   Q/E: down/up   Tab: cursor mode (panel)   1-6: A/B variant   Esc: main menu");

	DrawText(Line1, FLinearColor::White, 24.f, 24.f, GEngine->GetMediumFont());
	DrawText(Line2, FLinearColor(0.75f, 0.75f, 0.75f), 24.f, 48.f, GEngine->GetSmallFont());
}
