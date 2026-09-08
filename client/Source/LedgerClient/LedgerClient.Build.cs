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
			"Json"
		});
	}
}
