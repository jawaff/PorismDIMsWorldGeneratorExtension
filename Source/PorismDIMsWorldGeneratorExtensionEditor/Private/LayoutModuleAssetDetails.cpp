// Copyright 2026 Spotted Loaf Studio

#include "LayoutModuleAssetDetails.h"

#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "PropertyHandle.h"

TSharedRef<IDetailCustomization> FLayoutModuleAssetDetails::MakeInstance()
{
	return MakeShared<FLayoutModuleAssetDetails>();
}

void FLayoutModuleAssetDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// The legacy leaf-shape fields are no longer exposed as first-class module
	// properties. This customization only keeps the symmetry-driven face rule
	// refresh behavior.
	const TSharedPtr<IPropertyHandle> SymmetryModeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULayoutModuleAsset, FaceSymmetryMode));
	if (SymmetryModeHandle.IsValid())
	{
		const TWeakPtr<IPropertyUtilities> WeakPropertyUtilities = DetailBuilder.GetPropertyUtilities();
		SymmetryModeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakPropertyUtilities]()
		{
			if (const TSharedPtr<IPropertyUtilities> PropertyUtilities = WeakPropertyUtilities.Pin())
			{
				PropertyUtilities->ForceRefresh();
			}
		}));
	}
}
