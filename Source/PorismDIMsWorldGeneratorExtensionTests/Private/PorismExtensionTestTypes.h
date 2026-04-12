// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Block/BlockTypeSchemaRegistry.h"

#include "PorismExtensionTestTypes.generated.h"

UENUM()
enum class EPorismExtensionTestQuality : uint8
{
	Low = 1,
	High = 3
};

USTRUCT()
struct FPorismExtensionNestedCustomData : public FBlockCustomDataBase
{
	GENERATED_BODY()

	UPROPERTY()
	bool bHasDust = false;

	UPROPERTY()
	int32 DustAmount = 0;
};

USTRUCT()
struct FPorismExtensionDerivedDamageCustomData : public FBlockHealthCustomData
{
	GENERATED_BODY()

	UPROPERTY()
	int32 BonusHealth = 0;

	UPROPERTY()
	FPorismExtensionNestedCustomData Nested;
};

USTRUCT()
struct FPorismExtensionUnsupportedCustomData : public FBlockCustomDataBase
{
	GENERATED_BODY()

	UPROPERTY()
	FString UnsupportedText;
};

USTRUCT()
struct FPorismExtensionDerivedDamageDefinition : public FBlockHealthDefinition
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Hardness = 0;

	UPROPERTY()
	EPorismExtensionTestQuality Quality = EPorismExtensionTestQuality::Low;
};
