// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class IDetailCategoryBuilder;
class SVerticalBox;
class UBiomeStrategyData;
struct FFoundationProviderDefinition;

/**
 * Details customization that renders simple move controls beside biome strategy hierarchy items.
 */
class FBiomeStrategyDataDetails : public IDetailCustomization
{
public:
	/** Creates the customization instance for PropertyEditor. */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** Adds the hierarchy action panel to biome strategy assets. */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** Adds one provider row and recursively adds prototype/child provider rows. */
	void AddProviderRows(
		const TSharedRef<SVerticalBox>& Container,
		UBiomeStrategyData& Strategy,
		FFoundationProviderDefinition& Provider,
		const FString& Path,
		int32 Depth,
		bool bIsRootOrPrototype);

	/** Adds reservation rows owned by one provider. */
	void AddReservationRows(
		const TSharedRef<SVerticalBox>& Container,
		UBiomeStrategyData& Strategy,
		FFoundationProviderDefinition& Provider,
		const FString& ProviderPath,
		int32 Depth);

	/** Adds the full hierarchy panel to the details category. */
	void BuildHierarchyPanel(IDetailCategoryBuilder& Category, UBiomeStrategyData& Strategy);

	/** Refreshes the details panel after a successful authoring action. */
	void RefreshDetails() const;

	IDetailLayoutBuilder* CachedDetailBuilder = nullptr;
};
