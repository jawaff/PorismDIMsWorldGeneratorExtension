// Copyright 2026 Spotted Loaf Studio

#include "LayoutModuleFaceRulesCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "LayoutModuleFaceRulesCustomization"

TSharedRef<IPropertyTypeCustomization> FLayoutModuleFaceRulesCustomization::MakeInstance()
{
	return MakeShared<FLayoutModuleFaceRulesCustomization>();
}

void FLayoutModuleFaceRulesCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(280.0f)
	[
		StructPropertyHandle->CreatePropertyValueWidget()
	];
}

void FLayoutModuleFaceRulesCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& StructBuilder,
	IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	const ELayoutFaceSymmetryMode SymmetryMode = ResolveOwningSymmetryMode(StructPropertyHandle);
	const FName SlotNames[] =
	{
		GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, PosX),
		GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, NegX),
		GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, PosY),
		GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, NegY),
		GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, PosZ),
		GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, NegZ)
	};

	for (const FName SlotName : SlotNames)
	{
		const TSharedPtr<IPropertyHandle> SlotHandle = StructPropertyHandle->GetChildHandle(SlotName);
		if (!SlotHandle.IsValid())
		{
			continue;
		}

		StructBuilder.AddProperty(SlotHandle.ToSharedRef())
			.IsEnabled(IsAuthoritativeSlot(SymmetryMode, SlotName));
	}
}

ELayoutFaceSymmetryMode FLayoutModuleFaceRulesCustomization::ResolveOwningSymmetryMode(const TSharedRef<IPropertyHandle>& StructPropertyHandle)
{
	TArray<UObject*> OuterObjects;
	StructPropertyHandle->GetOuterObjects(OuterObjects);
	for (const UObject* OuterObject : OuterObjects)
	{
		if (const ULayoutModuleAsset* Module = Cast<ULayoutModuleAsset>(OuterObject))
		{
			return Module->FaceSymmetryMode;
		}
	}

	return ELayoutFaceSymmetryMode::Independent;
}

bool FLayoutModuleFaceRulesCustomization::IsAuthoritativeSlot(const ELayoutFaceSymmetryMode SymmetryMode, const FName SlotName)
{
	if (SlotName == GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, PosZ)
		|| SlotName == GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, NegZ))
	{
		return true;
	}

	switch (SymmetryMode)
	{
	case ELayoutFaceSymmetryMode::MirrorXFromPosX:
		return SlotName != GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, NegX);
	case ELayoutFaceSymmetryMode::MirrorYFromPosY:
		return SlotName != GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, NegY);
	case ELayoutFaceSymmetryMode::MirrorXYFromPositive:
		return SlotName != GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, NegX)
			&& SlotName != GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, NegY);
	case ELayoutFaceSymmetryMode::RadialHorizontalFromPosX:
		return SlotName == GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, PosX);
	case ELayoutFaceSymmetryMode::Independent:
	default:
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
