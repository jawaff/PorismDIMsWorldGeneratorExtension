// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"

class FProperty;
class UScriptStruct;
class UStruct;

/**
 * Runtime layout for one authored block custom-data struct family.
 *
 * The layout is derived from the struct type once, cached by the schema component, and reused
 * for both packing and unpacking so the same struct type always maps to the same slot order.
 */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FBlockCustomDataLayout
{
	/**
	 * Struct type this layout was built from.
	 */
	const UScriptStruct* StructType = nullptr;

	/**
	 * Number of runtime value slots required by the authored struct fields, excluding the reserved
	 * initialization marker slot that the schema component appends at the end.
	 */
	int32 ValueSlotCount = 0;

	/**
	 * Returns the number of authored value slots in this layout.
	 */
	int32 GetValueSlotCount() const { return ValueSlotCount; }

	/**
	 * Rebuilds the layout from a struct type. Returns false when the struct contains unsupported fields.
	 */
	bool Build(const UScriptStruct* InStructType);

	/**
	 * Packs one struct instance into a flat array of runtime integer values.
	 */
	bool Pack(const void* StructMemory, TArray<int32>& OutPackedValues) const;

	/**
	 * Unpacks one flat runtime value array back into a struct instance.
	 */
	bool Unpack(const TArray<int32>& PackedValues, void* StructMemory) const;
};
