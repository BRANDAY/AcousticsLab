using System.IO;
using UnrealBuildTool;

// Game module: scene extraction, async solver runs, visualization, audio and UI. No computation here (SPEC §3.2).
public class AcousticsLab : ModuleRules
{
	public AcousticsLab(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// The UI widgets keep their style constants in anonymous namespaces with the same names; unity files merged them
		// once M9 added files and regrouped the units (C2374). One translation unit per .cpp avoids the whole class of clashes.
		bUseUnity = false;

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
		// Synthesis: UAudioImpulseResponse and USubmixEffectConvolutionReverbPreset used by the auralization component (M4).
		// RenderCore: IsRayTracingEnabled() for the Audio Tracing level (M9).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GeometryCore",
			"GeometryFramework",
			"Synthesis",
			"RenderCore"
		});

		// Steam Audio (M9) is an optional project plugin installed by Tools/Plugins/install_steam_audio.ps1. Only the
		// static-geometry export helper links against it; everything else reaches both industry plugins through class
		// paths and reflection, so the project builds without them (WITH_STEAM_AUDIO = 0).
		bool bHasSteamAudio = File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Plugins", "SteamAudio", "SteamAudio.uplugin"));
		if (bHasSteamAudio)
		{
			PrivateDependencyModuleNames.Add("SteamAudio");
		}
		PublicDefinitions.Add("WITH_STEAM_AUDIO=" + (bHasSteamAudio ? "1" : "0"));

		// Editor builds only: the click recorder lifts ULevelEditorMiscSettings::bAllowBackgroundAudio while it measures.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
