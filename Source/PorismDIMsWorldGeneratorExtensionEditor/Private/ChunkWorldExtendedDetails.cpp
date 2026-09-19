// Copyright 2026 Spotted Loaf Studio
#include "ChunkWorldExtendedDetails.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"

#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ChunkWorldExtendedDetails"

TSharedRef<IDetailCustomization> FChunkWorldExtendedDetails::MakeInstance()
{
	return MakeShared<FChunkWorldExtendedDetails>();
}

void FChunkWorldExtendedDetails::CustomizeDetails(IDetailLayoutBuilder& Details)
{
	// Component settings belong to component selection, not a second inline object editor.
	for (const FName Property : { FName(TEXT("BlockTypeSchemaComponent")), FName(TEXT("BlockFeedbackComponent")),
		FName(TEXT("BlockSwapScannerComponent")), FName(TEXT("BlockSwapComponent")), FName(TEXT("LayoutRuntimeComponent")) })
	{
		Details.HideProperty(Property, AChunkWorldExtended::StaticClass());
	}
	Details.HideProperty(GET_MEMBER_NAME_CHECKED(AChunkWorld, DebugTemplateCenterLifetime), AChunkWorld::StaticClass());
	TArray<FName> Categories;
	Details.GetCategoryNames(Categories);
	for (const FName Category : Categories)
	{
		const FString Name = Category.ToString();
		if (Name == TEXT("Block") || Name.StartsWith(TEXT("Block|")) || Name == TEXT("Layout") || Name.StartsWith(TEXT("Layout|")))
		{
			Details.HideCategory(Category);
		}
	}
	Details.EditCategory(TEXT("PorismExtensionActor"), LOCTEXT("Actor", "Porism Extension - Chunk World Extended"))
		.AddProperty(Details.GetProperty(TEXT("BlockTypeSchemaRegistry"), AChunkWorldExtended::StaticClass()));
	Details.EditCategory(TEXT("WorldGenDef"), LOCTEXT("WorldGen", "Porism Terrain - World Definition"));
	Details.EditCategory(TEXT("ChunkWorldFeatures"), LOCTEXT("Features", "Porism Terrain - Features"));
	Details.EditCategory(TEXT("ChunkWorldCache"), LOCTEXT("Cache", "Porism Terrain - Save and Cache"));
	Details.EditCategory(TEXT("ChunkWorldMemory"), LOCTEXT("Memory", "Porism Terrain - Memory"));
	Details.EditCategory(TEXT("ChunkWorldThreads"), LOCTEXT("Threads", "Porism Terrain - Workers"));
	Details.EditCategory(TEXT("ChunkWorldPer"), LOCTEXT("Streaming", "Porism Terrain - Streaming"));
	IDetailCategoryBuilder& Debug = Details.EditCategory(TEXT("Debug"), LOCTEXT("Debug", "Porism Extension - Shared Diagnostics"), ECategoryPriority::Important);
	Debug.AddProperty(Details.GetProperty(GET_MEMBER_NAME_CHECKED(AChunkWorldCore, ShowDebugData), AChunkWorldCore::StaticClass()))
		.DisplayName(LOCTEXT("Stats", "Show Debug Stats"))
		.ToolTip(LOCTEXT("StatsTip", "Shared terrain and layout statistics switch. Does not enable detailed planning traces or ownership probes. Native terrain text follows engine screen-message settings; editor layout text uses On Screen Debug. Changing this switch does not restart generation."));
	Debug.AddProperty(Details.GetProperty(TEXT("bDetailedDiagnostics")));

	IDetailGroup& Visuals = Debug.AddGroup(TEXT("AdvancedVisualizations"), LOCTEXT("Visuals", "Advanced Visualizations"));
	Visuals.AddPropertyRow(Details.GetProperty(GET_MEMBER_NAME_CHECKED(AChunkWorldCore, ShowDebugChunkLines), AChunkWorldCore::StaticClass()))
		.DisplayName(LOCTEXT("Boundaries", "Chunk Boundaries"));
	Visuals.AddPropertyRow(Details.GetProperty(GET_MEMBER_NAME_CHECKED(AChunkWorldCore, ShowDebugChunkCollision), AChunkWorldCore::StaticClass()))
		.DisplayName(LOCTEXT("Collision", "Collision Shapes"));
	Visuals.AddPropertyRow(Details.GetProperty(GET_MEMBER_NAME_CHECKED(AChunkWorld, ShowDebugTemplateCenterBlocks), AChunkWorld::StaticClass()))
		.DisplayName(LOCTEXT("Templates", "Template Candidates"));
	Visuals.AddPropertyRow(Details.GetProperty(GET_MEMBER_NAME_CHECKED(AChunkWorld, DebugTemplateCenterViewDistance), AChunkWorld::StaticClass()));
	Visuals.AddPropertyRow(Details.GetProperty(GET_MEMBER_NAME_CHECKED(AChunkWorld, DebugTemplateCenterFarMarkerMinBlocks), AChunkWorld::StaticClass()));
	Visuals.AddPropertyRow(Details.GetProperty(GET_MEMBER_NAME_CHECKED(AChunkWorldCore, DebugColors), AChunkWorldCore::StaticClass()))
		.DisplayName(LOCTEXT("Colors", "Chunk Material Colors"));
}

TSharedRef<IDetailCustomization> FChunkWorldComponentDetails::MakeInstance(FText OwnerLabel)
{
	return MakeShared<FChunkWorldComponentDetails>(MoveTemp(OwnerLabel));
}

void FChunkWorldComponentDetails::CustomizeDetails(IDetailLayoutBuilder& Details)
{
	// Nested default categories can expose the same field at several levels. Rehome
	// each editable domain property once; leave standard Unreal component controls alone.
	TArray<FName> Categories;
	Details.GetCategoryNames(Categories);
	for (const FName Category : Categories)
	{
		const FString Name = Category.ToString();
		if (Name == TEXT("Layout") || Name.StartsWith(TEXT("Layout|")) || Name == TEXT("Block") || Name.StartsWith(TEXT("Block|")))
		{
			Details.HideCategory(Category);
		}
	}
	// Unreal also invokes component customizations while building an actor's details.
	// Do not republish inline settings there; the component tree is their sole editor.
	for (const TWeakObjectPtr<UObject>& Selected : Details.GetSelectedObjects())
	{
		if (Selected.IsValid() && Selected->IsA<AChunkWorldExtended>()) return;
	}
	TArray<TWeakObjectPtr<UObject>> Objects;
	Details.GetObjectsBeingCustomized(Objects);
	if (Objects.IsEmpty() || !Objects[0].IsValid()) return;
	for (TFieldIterator<FProperty> It(Objects[0]->GetClass()); It; ++It)
	{
		const FString Category = It->GetMetaData(TEXT("Category"));
		if (!It->HasAnyPropertyFlags(CPF_Edit) ||
			!(Category == TEXT("Layout") || Category.StartsWith(TEXT("Layout|")) || Category.StartsWith(TEXT("Block|ChunkWorld")))) continue;
		FString Section;
		if (Category.StartsWith(TEXT("Layout|"))) Section = Category.RightChop(7);
		else if (Category.EndsWith(TEXT("|Pooling"))) Section = TEXT("Pooling");
		Details.EditCategory(FName(*(TEXT("PorismExtensionComponent") + Section)),
			FText::Format(LOCTEXT("ComponentCategory", "Porism Extension - {0}{1}"), OwnerLabel,
				FText::FromString(Section.IsEmpty() ? FString() : TEXT(" - ") + Section)))
			.AddProperty(Details.GetProperty(It->GetFName()));
	}
}

#undef LOCTEXT_NAMESPACE
