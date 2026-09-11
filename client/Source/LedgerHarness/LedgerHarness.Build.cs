// The scripted flight and the frame-time recorder.
//
// A DeveloperTool module, absent from a shipping build entirely. That is the
// point of the split rather than a side effect of it: the flight teleports the
// ship, disables its flight model and overrides its camera boom, and none of
// that belongs anywhere a player can reach.

using UnrealBuildTool;

public class LedgerHarness : ModuleRules
{
	public LedgerHarness(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"LedgerFlight",
			"Core",
			"CoreUObject",
			"Engine",
			"RenderCore",
			"RHI",
			"LedgerCore",
			"LedgerTerrain",
			"LedgerClient"
		});

		// The giant world (T079, T080) draws with procedural meshes and the
		// flat material.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ProceduralMeshComponent",
			"LedgerMaterial"
		});
	}
}
