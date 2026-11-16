using UnrealBuildTool;

public class PixelRPG : ModuleRules
{
	public PixelRPG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"Landscape"
		});

		PublicIncludePaths.Add(ModuleDirectory);
	}
}
