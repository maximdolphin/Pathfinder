// Generated materials and textures.
//
// Depends on LedgerCore for the noise the detail textures are built from, and
// on nothing else in the project. It is deliberately below the terrain: the
// planet is handed its materials rather than making them, so the terrain module
// does not need to know this one exists.

using UnrealBuildTool;

public class LedgerMaterial : ModuleRules
{
	public LedgerMaterial(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"LedgerCore"
		});
	}
}
