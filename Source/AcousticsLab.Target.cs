using UnrealBuildTool;
using System.Collections.Generic;

public class AcousticsLabTarget : TargetRules
{
	public AcousticsLabTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new string[] { "AcousticsCore", "AcousticsLab" });
	}
}
