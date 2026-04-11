// Copyright 2026 Spotted Loaf Studio

using UnrealBuildTool;

public class PorismDIMsWorldGeneratorExtension : ModuleRules
{
	/// <summary>
	/// Sets up the extension module and its dependency on PorismDIMsWorldGenerator.
	/// </summary>
	public PorismDIMsWorldGeneratorExtension(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Chaos",
			"Core",
			"CoreUObject",
			"Engine",
			"FieldSystemEngine",
			"GameplayTags",
			"GeometryCollectionEngine",
			"Niagara",
			"PorismDIMsWorldGenerator"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"NetCore",
			"PhysicsCore",
			"PorismDIMsWorldGenerator"
		});
	}
}
