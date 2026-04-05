// Copyright 2026 Spotted Loaf Studio

#include "BlockCustomDataLayout.h"

#include "BlockTypeSchemaRegistry.h"
#include "UObject/UnrealType.h"

namespace
{
	bool IsPackableScalarProperty(const FProperty* Property)
	{
		return CastField<FBoolProperty>(Property) != nullptr
			|| CastField<FEnumProperty>(Property) != nullptr
			|| CastField<FNumericProperty>(Property) != nullptr;
	}

	bool PackScalarProperty(const FProperty* Property, const void* StructMemory, TArray<int32>& OutPackedValues)
	{
		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			OutPackedValues.Add(BoolProperty->GetPropertyValue_InContainer(StructMemory) ? 1 : 0);
			return true;
		}

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
			if (UnderlyingProperty == nullptr)
			{
				return false;
			}

			const void* ValuePtr = UnderlyingProperty->ContainerPtrToValuePtr<void>(StructMemory);
			OutPackedValues.Add(static_cast<int32>(UnderlyingProperty->GetSignedIntPropertyValue(ValuePtr)));
			return true;
		}

		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(StructMemory);
			if (NumericProperty->IsInteger())
			{
				OutPackedValues.Add(static_cast<int32>(NumericProperty->GetSignedIntPropertyValue(ValuePtr)));
			}
			else
			{
				OutPackedValues.Add(FMath::RoundToInt(NumericProperty->GetFloatingPointPropertyValue(ValuePtr)));
			}
			return true;
		}

		return false;
	}

	bool UnpackScalarProperty(const FProperty* Property, void* StructMemory, const TArray<int32>& PackedValues, int32& InOutIndex)
	{
		if (!PackedValues.IsValidIndex(InOutIndex))
		{
			return false;
		}

		const int32 PackedValue = PackedValues[InOutIndex++];

		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			BoolProperty->SetPropertyValue_InContainer(StructMemory, PackedValue != 0);
			return true;
		}

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
			if (UnderlyingProperty == nullptr)
			{
				return false;
			}

			void* ValuePtr = UnderlyingProperty->ContainerPtrToValuePtr<void>(StructMemory);
			UnderlyingProperty->SetIntPropertyValue(ValuePtr, static_cast<int64>(PackedValue));
			return true;
		}

		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(StructMemory);
			if (NumericProperty->IsInteger())
			{
				NumericProperty->SetIntPropertyValue(ValuePtr, static_cast<int64>(PackedValue));
			}
			else
			{
				NumericProperty->SetFloatingPointPropertyValue(ValuePtr, static_cast<double>(PackedValue));
			}
			return true;
		}

		return false;
	}

	bool CountStructSlots(const UStruct* StructType, int32& OutSlotCount)
	{
		if (StructType == nullptr)
		{
			return false;
		}

		const UStruct* SuperStruct = StructType->GetSuperStruct();
		if (SuperStruct != nullptr && SuperStruct->IsChildOf(FBlockCustomDataBase::StaticStruct()))
		{
			if (!CountStructSlots(SuperStruct, OutSlotCount))
			{
				return false;
			}
		}

		for (TFieldIterator<FProperty> PropertyIt(StructType, EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (!CountStructSlots(StructProperty->Struct, OutSlotCount))
				{
					return false;
				}
				continue;
			}

			if (!IsPackableScalarProperty(Property))
			{
				return false;
			}

			++OutSlotCount;
		}

		return true;
	}

	bool PackStructProperties(const UStruct* StructType, const void* StructMemory, TArray<int32>& OutPackedValues)
	{
		if (StructType == nullptr || StructMemory == nullptr)
		{
			return false;
		}

		const UStruct* SuperStruct = StructType->GetSuperStruct();
		if (SuperStruct != nullptr && SuperStruct->IsChildOf(FBlockCustomDataBase::StaticStruct()))
		{
			if (!PackStructProperties(SuperStruct, StructMemory, OutPackedValues))
			{
				return false;
			}
		}

		for (TFieldIterator<FProperty> PropertyIt(StructType, EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				const void* NestedMemory = StructProperty->ContainerPtrToValuePtr<void>(StructMemory);
				if (!PackStructProperties(StructProperty->Struct, NestedMemory, OutPackedValues))
				{
					return false;
				}
				continue;
			}

			if (!PackScalarProperty(Property, StructMemory, OutPackedValues))
			{
				return false;
			}
		}

		return true;
	}

	bool UnpackStructProperties(const UStruct* StructType, void* StructMemory, const TArray<int32>& PackedValues, int32& InOutIndex)
	{
		if (StructType == nullptr || StructMemory == nullptr)
		{
			return false;
		}

		const UStruct* SuperStruct = StructType->GetSuperStruct();
		if (SuperStruct != nullptr && SuperStruct->IsChildOf(FBlockCustomDataBase::StaticStruct()))
		{
			if (!UnpackStructProperties(SuperStruct, StructMemory, PackedValues, InOutIndex))
			{
				return false;
			}
		}

		for (TFieldIterator<FProperty> PropertyIt(StructType, EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				void* NestedMemory = StructProperty->ContainerPtrToValuePtr<void>(StructMemory);
				if (!UnpackStructProperties(StructProperty->Struct, NestedMemory, PackedValues, InOutIndex))
				{
					return false;
				}
				continue;
			}

			if (!UnpackScalarProperty(Property, StructMemory, PackedValues, InOutIndex))
			{
				return false;
			}
		}

		return true;
	}
}

bool FBlockCustomDataLayout::Build(const UScriptStruct* InStructType)
{
	StructType = InStructType;
	ValueSlotCount = 0;

	if (StructType == nullptr)
	{
		return false;
	}

	return CountStructSlots(StructType, ValueSlotCount);
}

bool FBlockCustomDataLayout::Pack(const void* StructMemory, TArray<int32>& OutPackedValues) const
{
	OutPackedValues.Reset();

	if (StructType == nullptr || StructMemory == nullptr)
	{
		return false;
	}

	if (!PackStructProperties(StructType, StructMemory, OutPackedValues))
	{
		return false;
	}

	return OutPackedValues.Num() == ValueSlotCount;
}

bool FBlockCustomDataLayout::Unpack(const TArray<int32>& PackedValues, void* StructMemory) const
{
	if (StructType == nullptr || StructMemory == nullptr || PackedValues.Num() < ValueSlotCount)
	{
		return false;
	}

	int32 ValueIndex = 0;
	if (!UnpackStructProperties(StructType, StructMemory, PackedValues, ValueIndex))
	{
		return false;
	}

	return ValueIndex == ValueSlotCount;
}
