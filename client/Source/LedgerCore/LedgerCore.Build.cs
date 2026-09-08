// The bottom of the stack: logging, and the coherent noise every generator in
// the project is written in terms of.
//
// No gameplay, no rendering, and — the point of the module — nothing above it
// may be referenced from in here. Its dependency list is the engine and
// nothing else, and it is the only module in the project of which that is true.

using UnrealBuildTool;

public class LedgerCore : ModuleRules
{
	public LedgerCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});
	}
}
