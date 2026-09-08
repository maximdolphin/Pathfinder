// Game target. Design §9: C++ primary, Blueprint for designer-tunable params only.

using UnrealBuildTool;

public class LedgerTarget : TargetRules
{
	public LedgerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("LedgerClient");
	}
}
