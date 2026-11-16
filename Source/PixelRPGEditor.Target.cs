using UnrealBuildTool;

public class PixelRPGEditorTarget : TargetRules
{
	public PixelRPGEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("PixelRPG");
	}
}
