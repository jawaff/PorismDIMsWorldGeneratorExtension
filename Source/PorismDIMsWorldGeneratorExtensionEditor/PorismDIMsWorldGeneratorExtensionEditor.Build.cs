// Copyright 2026 Spotted Loaf Studio

using UnrealBuildTool;

public class PorismDIMsWorldGeneratorExtensionEditor : ModuleRules
{
	/// <summary>
	/// Editor-only authoring tools for Porism world generation strategy assets.
	/// </summary>
	public PorismDIMsWorldGeneratorExtensionEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"PorismDIMsWorldGenerator"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AppFramework",
			"EditorFramework",
			"InputCore",
			"LevelEditor",
			"PorismDIMsWorldGeneratorExtension",
			"PropertyEditor",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UnrealEd"
		});
	}
}
