#include "Scene/AcousticMediumPresets.h"

namespace AcousticMediumPresets
{
	TArray<FMedium> All()
	{
		return { FMedium::Air(), FMedium::Water(), FMedium::Glycerol(), FMedium::Rubber() };
	}

	bool Find(FName Name, FMedium& OutMedium)
	{
		if (Name.IsNone())
		{
			return false;
		}
		for (const FMedium& Medium : All())
		{
			if (Name == FName(*Medium.Name)) // FName comparison ignores case
			{
				OutMedium = Medium;
				return true;
			}
		}
		return false;
	}

	TArray<FName> Names()
	{
		TArray<FName> Result;
		for (const FMedium& Medium : All())
		{
			Result.Add(FName(*Medium.Name));
		}
		return Result;
	}
}
