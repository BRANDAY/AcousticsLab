#include "Framework/AcousticsHUD.h"

#include "Audio/AuralizationComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Framework/AcousticsPlayerController.h"
#include "Industry/IndustryAudioActor.h"
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
	// Industry levels (M9): the plugin keys and which one plays; short, so the panel on the right does not cover it
	// (the full state is in the panel's "Industry plugins" block).
	const TArray<AIndustryAudioActor*> Industries = AIndustryAudioActor::FindAll(GetWorld());
	if (!Industries.IsEmpty())
	{
		FString Line3 = TEXT("Plugins:");
		for (const AIndustryAudioActor* Actor : Industries)
		{
			Line3 += FString::Printf(TEXT("   %d: %s%s"), Actor->VariantKey, *Actor->GetPluginDisplayName(), Actor->IsPluginVariantActive() ? TEXT(" [PLAYING]") : TEXT(""));
		}
		Line3 += TEXT("   (switch while playing; listener: console Acoustics.Industry.Receiver R1..R4)");
		DrawText(Line3, FLinearColor(1.0f, 0.85f, 0.4f), 24.f, 66.f, GEngine->GetSmallFont());
	}
	// A paused PIE world (editor toolbar Pause, Pause key) ignores the keys and holds the audio: say so.
	if (GetWorld() && GetWorld()->IsPaused())
	{
		DrawText(TEXT("GAME PAUSED - keys and sound wait until play resumes (editor toolbar Resume, or the Pause key)"), FLinearColor(1.0f, 0.3f, 0.3f), 24.f, 88.f, GEngine->GetMediumFont());
	}
}
