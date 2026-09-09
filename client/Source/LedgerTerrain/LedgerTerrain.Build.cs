// The cube-sphere quadtree: LOD, patch generation, streaming and the cache.
//
// Three project dependencies would be two too many. It has one — LedgerCore —
// because the planet receives its materials from whoever spawned it rather than
// creating them, which is what keeps this module from depending on
// LedgerMaterial and turning the two into a pair.

using UnrealBuildTool;

public class LedgerTerrain : ModuleRules
{
	public LedgerTerrain(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"LedgerCore",
			// Runtime-generated meshes with async collision cooking (§6.8).
			"ProceduralMeshComponent",
			"PhysicsCore",
			// The other candidate component type, measured in T423: a static
			// mesh built per patch at runtime. See LedgerPatchComponents.h.
			"MeshDescription",
			"StaticMeshDescription"
		});
	}
}
