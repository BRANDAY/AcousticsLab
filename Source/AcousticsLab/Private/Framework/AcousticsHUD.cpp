#include "Framework/AcousticsHUD.h"

#include "Audio/AuralizationComponent.h"
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
	const FString Variant = Controller->SelectedVariant > 0
		? FString::Printf(TEXT("%d (%s)"), Controller->SelectedVariant, *UAuralizationComponent::VariantName(UAuralizationComponent::VariantFromKey(Controller->SelectedVariant)))
		: FString(TEXT("none (1 = Dry, 2 = Ray, 3 = Beam, 4 = Hybrid)"));
	const FString Line1 = FString::Printf(TEXT("%s   |   mode: %s   |   A/B variant: %s"), *Title, *Mode, *Variant);
	const FString Line2 = Controller->IsUIMode()
		? TEXT("Cursor mode: edit the panel, WASD still flies   Tab: fly mode   1: Dry  2: Ray  3: Beam  4: Hybrid (convolution)   B: benchmark   H: compare panel   Esc: main menu")
		: TEXT("WASD + mouse: fly   Q/E: down/up   Tab: cursor mode (panel)   1: Dry  2: Ray  3: Beam  4: Hybrid (convolution)   B: benchmark   H: compare panel   Esc: main menu");

	DrawText(Line1, FLinearColor::White, 24.f, 24.f, GEngine->GetMediumFont());
	DrawText(Line2, FLinearColor(0.75f, 0.75f, 0.75f), 24.f, 48.f, GEngine->GetSmallFont());
}
