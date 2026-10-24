using UnrealBuildTool;

public class RoadsterSimEditorTarget : TargetRules
{
	public RoadsterSimEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("RoadsterSim");
	}
}
