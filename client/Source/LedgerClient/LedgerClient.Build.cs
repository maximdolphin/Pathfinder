// The composition root, for now. Design §9: `Json` beyond the engine defaults
// because the Phase 1 wire contract is protobuf over TCP (ADR-0001) and until
// that lands the client reads the sim's snapshot from disk.
//
// This is the one module allowed a long dependency list (ARCH Rule 2) — it is
// what wires the others together. It will shrink as LedgerSky, LedgerProcGen,
// LedgerFlight and LedgerPawn are carved out of it; what is left at the end is
// LedgerGame and nothing else.

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
			"LedgerCore",
			"LedgerMaterial",
			"LedgerTerrain",
			// The frame-time recorder reads the engine's own thread and GPU
			// counters. "It felt smooth" is not a measurement.
			"RenderCore",
			"RHI"
		});
	}
}
