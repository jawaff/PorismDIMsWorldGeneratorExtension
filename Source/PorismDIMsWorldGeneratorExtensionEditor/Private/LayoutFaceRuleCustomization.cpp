// Copyright 2026 Spotted Loaf Studio

#include "LayoutFaceRuleCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Layout/Types/LayoutTypes.h"
#include "PropertyHandle.h"

namespace
{
	ELayoutFaceDirection SlotNameToFaceDirection(const FName SlotName)
	{
		if (SlotName == GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, PosX))
		{
			return ELayoutFaceDirection::PosX;
		}
		if (SlotName == GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, NegX))
		{
			return ELayoutFaceDirection::NegX;
		}
		if (SlotName == GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, PosY))
		{
			return ELayoutFaceDirection::PosY;
		}
		if (SlotName == GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, NegY))
		{
			return ELayoutFaceDirection::NegY;
		}
		if (SlotName == GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, PosZ))
		{
			return ELayoutFaceDirection::PosZ;
		}
		if (SlotName == GET_MEMBER_NAME_CHECKED(FLayoutModuleFaceRules, NegZ))
		{
			return ELayoutFaceDirection::NegZ;
		}

		return ELayoutFaceDirection::PosX;
	}
}

TSharedRef<IPropertyTypeCustomization> FLayoutFaceRuleCustomization::MakeInstance()
{
	return MakeShared<FLayoutFaceRuleCustomization>();
}

void FLayoutFaceRuleCustomization::CustomizeHeader(
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

void FLayoutFaceRuleCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& StructBuilder,
	IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	const ELayoutFaceDirection Direction = ResolveFaceDirection(StructPropertyHandle);
	uint32 NumChildren = 0;
	StructPropertyHandle->GetNumChildren(NumChildren);
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		const TSharedPtr<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(ChildIndex);
		if (!ChildHandle.IsValid())
		{
			continue;
		}

		if (ChildHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FLayoutFaceRule, bRequireMatchingYawWithFilledNeighbor)
			&& !SupportsMatchingYawAuthoring(Direction))
		{
			continue;
		}

		StructBuilder.AddProperty(ChildHandle.ToSharedRef());
	}
}

ELayoutFaceDirection FLayoutFaceRuleCustomization::ResolveFaceDirection(const TSharedRef<IPropertyHandle>& StructPropertyHandle)
{
	if (const TSharedPtr<IPropertyHandle> ParentHandle = StructPropertyHandle->GetParentHandle())
	{
		const FName ParentSlotName = ParentHandle->GetProperty()->GetFName();
		if (ParentHandle->GetProperty()->GetOwnerStruct() == FLayoutModuleFaceRules::StaticStruct())
		{
			return SlotNameToFaceDirection(ParentSlotName);
		}
	}

	uint8 DirectionValue = static_cast<uint8>(ELayoutFaceDirection::PosX);
	const TSharedPtr<IPropertyHandle> DirectionHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLayoutFaceRule, Direction));
	if (DirectionHandle.IsValid() && DirectionHandle->GetValue(DirectionValue) == FPropertyAccess::Success)
	{
		return static_cast<ELayoutFaceDirection>(DirectionValue);
	}

	return ELayoutFaceDirection::PosX;
}

bool FLayoutFaceRuleCustomization::SupportsMatchingYawAuthoring(const ELayoutFaceDirection Direction)
{
	return Direction == ELayoutFaceDirection::PosZ
		|| Direction == ELayoutFaceDirection::NegZ;
}
