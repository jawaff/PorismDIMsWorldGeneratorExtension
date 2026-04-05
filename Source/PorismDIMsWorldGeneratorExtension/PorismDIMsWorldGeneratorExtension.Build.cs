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
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"Niagara",
			"PorismDIMsWorldGenerator"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"PorismDIMsWorldGenerator"
		});
	}
}
