// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Types/LayoutTypes.h"

class IDetailChildrenBuilder;
class IPropertyHandle;

/**
 * Details customization that draws module face-rule slots with symmetry-inferred slots disabled in place.
 */
class FLayoutModuleFaceRulesCustomization : public IPropertyTypeCustomization
{
public:
	/** Creates the customization instance for PropertyEditor. */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** Draws the normal face-rule header. */
	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	/** Draws face-rule child slots and disables slots inferred by the owning module's symmetry mode. */
	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& StructBuilder,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	/** Returns the first owning module symmetry mode found for this face-rule struct. */
	static ELayoutFaceSymmetryMode ResolveOwningSymmetryMode(const TSharedRef<IPropertyHandle>& StructPropertyHandle);

	/** Returns true when this slot is directly authored instead of inferred from another face. */
	static bool IsAuthoritativeSlot(ELayoutFaceSymmetryMode SymmetryMode, FName SlotName);
};
