using UnrealBuildTool;

public class LedgerEditorTarget : TargetRules
{
	public LedgerEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("LedgerClient");
	}
}
