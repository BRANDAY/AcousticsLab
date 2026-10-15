// Built-in propagation media (SPEC §2.6): the names the tags "AcMediumFront:<name>" / "AcMediumBack:<name>" and
// AAcousticSource::MediumName resolve to when the scene extractor has no FAcousticMediumSpec of that name.
#pragma once

#include "CoreMinimal.h"
#include "Geometry/Medium.h"

namespace AcousticMediumPresets
{
	/** Air, Water, Glycerol, Rubber (the last two from the lens test of Sikora 2012). */
	ACOUSTICSLAB_API TArray<FMedium> All();

	/** Case-insensitive lookup; false when unknown. */
	ACOUSTICSLAB_API bool Find(FName Name, FMedium& OutMedium);

	ACOUSTICSLAB_API TArray<FName> Names();
}
