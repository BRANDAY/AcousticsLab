using UnrealBuildTool;

// Pure C++ solver core: geometry, BVH, ray/image-source/beam tracers, histograms, metrics.
// Meters and seconds inside, no UObject, no engine types (SPEC §3.2). Only Core is allowed here.
public class AcousticsCore : ModuleRules
{
	public AcousticsCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
