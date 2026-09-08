// Design §9. `Json` is the only dependency beyond the engine defaults: the
// Phase 1 wire contract is protobuf over TCP (ADR-0001), and until that lands
// the client reads the sim's snapshot from disk.

using UnrealBuildTool;

public class LedgerClient : ModuleRules
{
	public LedgerClient(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Json",
			// §6.8 terrain: runtime-generated cube-sphere meshes with async
			// collision cooking. ProceduralMeshComponent is the boring choice
			// and it already does budgeted async cooks, which is the one thing
			// the terrain architecture must get right.
			"ProceduralMeshComponent",
			"PhysicsCore",
			// The frame-time recorder reads the engine's own thread and GPU
			// counters. "It felt smooth" is not a measurement.
			"RenderCore",
			"RHI"
		});
	}
}
