using UnrealBuildTool;

public class RoadsterSimTarget : TargetRules
{
	public RoadsterSimTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("RoadsterSim");
	}
}
