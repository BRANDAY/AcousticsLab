using UnrealBuildTool;

// Game module: scene extraction, async solver runs, visualization, audio and UI. No computation here (SPEC §3.2).
public class AcousticsLab : ModuleRules
{
	public AcousticsLab(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore",
			"AcousticsCore"
		});

		// GeometryCore / GeometryFramework: UDynamicMeshComponent and FDynamicMesh3 read by the scene extractor.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GeometryCore",
			"GeometryFramework"
		});
	}
}
