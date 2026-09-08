// Game target. Design §9: C++ primary, Blueprint for designer-tunable params only.

using UnrealBuildTool;

public class LedgerTarget : TargetRules
{
	public LedgerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
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
