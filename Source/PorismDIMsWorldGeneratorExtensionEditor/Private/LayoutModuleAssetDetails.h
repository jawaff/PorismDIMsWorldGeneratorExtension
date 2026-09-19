// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

/**
 * Details customization that keeps symmetry-dependent module authoring UI refreshed.
 */
class FLayoutModuleAssetDetails : public IDetailCustomization
{
public:
	/** Creates the customization instance for PropertyEditor. */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** Refreshes dependent details when face symmetry changes. */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
