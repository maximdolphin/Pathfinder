using UnrealBuildTool;

public class LedgerEditorTarget : TargetRules
{
	public LedgerEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] {
			"LedgerCore", "LedgerMaterial", "LedgerTerrain",
			"LedgerFlight", "LedgerClient" });
		if (Configuration != UnrealTargetConfiguration.Shipping)
		{
			// The scripted flight is a fixture. A shipping build has no
			// business containing a camera that teleports the ship.
			ExtraModuleNames.Add("LedgerHarness");
		}
	}
}
