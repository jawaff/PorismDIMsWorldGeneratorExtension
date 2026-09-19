// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IDetailChildrenBuilder;
class IPropertyHandle;

/**
 * Details customization that only exposes matching-yaw authoring on vertical
 * module faces.
 */
class FLayoutFaceRuleCustomization : public IPropertyTypeCustomization
{
public:
	/** Creates the customization instance for PropertyEditor. */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** Draws the normal face-rule header. */
	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	/** Hides matching-yaw authoring on horizontal face rules. */
	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& StructBuilder,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	/** Resolves the effective authored face direction for this property row. */
	static ELayoutFaceDirection ResolveFaceDirection(const TSharedRef<IPropertyHandle>& StructPropertyHandle);

	/** Returns true when matching-yaw authoring should remain visible for the face. */
	static bool SupportsMatchingYawAuthoring(ELayoutFaceDirection Direction);
};
