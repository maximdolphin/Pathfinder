// The flight model: gravity, drag, ground contact, and — from M05 and M06 —
// the component graph and the thruster allocator.
//
// Engine dependencies only. That is the point: the model integrates a struct
// and knows nothing about actors, worlds or frames, so it can be tested ten
// thousand steps at a time in a millisecond.

using UnrealBuildTool;

public class LedgerFlight : ModuleRules
{
	public LedgerFlight(ReadOnlyTargetRules Target) : base(Target)
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
