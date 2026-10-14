#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AcousticsHUD.generated.h"

/** Minimal canvas HUD: level, mode, selected A/B variant and key hints. UMG panels take over from M2 on. */
UCLASS()
class ACOUSTICSLAB_API AAcousticsHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
