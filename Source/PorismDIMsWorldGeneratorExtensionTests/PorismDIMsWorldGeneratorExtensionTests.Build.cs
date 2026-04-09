// Copyright 2026 Spotted Loaf Studio

using UnrealBuildTool;

public class PorismDIMsWorldGeneratorExtensionTests : ModuleRules
{
	/// <summary>
	/// Editor-only automation test module that travels with the extension plugin.
	/// </summary>
	public PorismDIMsWorldGeneratorExtensionTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"PorismDIMsWorldGenerator",
			"PorismDIMsWorldGeneratorExtension"
		});
	}
}
