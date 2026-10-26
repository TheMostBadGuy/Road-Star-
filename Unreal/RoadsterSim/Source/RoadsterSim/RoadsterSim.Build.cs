using UnrealBuildTool;
using System.IO;

public class RoadsterSim : ModuleRules
{
	public RoadsterSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "ApplicationCore", "InputCore", "EnhancedInput",
			"ProceduralMeshComponent", "Slate", "SlateCore", "UMG",
			"Json", "JsonUtilities", "AudioMixer", "SignalProcessing", "Niagara",
			"RenderCore", "RHI", "PlanetX"
		});
		PrivateDependencyModuleNames.Add("ImageWriteQueue");
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry", "MeshDescription", "StaticMeshDescription" });
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// SDL is loaded explicitly, so a missing DLL leaves normal input usable.
			string SDLRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/SDL3"));
			PrivateIncludePaths.Add(Path.Combine(SDLRoot, "include"));
			RuntimeDependencies.Add("$(TargetOutputDir)/SDL3.dll", Path.Combine(SDLRoot, "lib/x64/SDL3.dll"));
			RuntimeDependencies.Add("$(TargetOutputDir)/SDL3-LICENSE.txt", Path.Combine(SDLRoot, "LICENSE.txt"), StagedFileType.NonUFS);
		}
	}
}
