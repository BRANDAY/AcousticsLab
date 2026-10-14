using UnrealBuildTool;
using System.Collections.Generic;

public class AcousticsLabEditorTarget : TargetRules
{
	public AcousticsLabEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new string[] { "AcousticsCore", "AcousticsLab" });
	}
}
