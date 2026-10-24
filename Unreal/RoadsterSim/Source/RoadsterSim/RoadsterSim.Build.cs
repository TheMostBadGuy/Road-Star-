using UnrealBuildTool;

public class RoadsterSim : ModuleRules
{
	public RoadsterSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"ProceduralMeshComponent", "Slate", "SlateCore", "UMG",
			"Json", "JsonUtilities", "AudioMixer", "SignalProcessing", "Niagara",
			"RenderCore", "RHI"
		});
	}
}
