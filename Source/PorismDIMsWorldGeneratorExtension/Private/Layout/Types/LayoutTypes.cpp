// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutTypes.h"

void FLayoutValidationResult::AddWarning(const FString& InMessage)
{
	FLayoutValidationMessage& Message = Messages.AddDefaulted_GetRef();
	Message.Severity = ELayoutValidationSeverity::Warning;
	Message.Message = InMessage;
}

void FLayoutValidationResult::AddError(const FString& InMessage)
{
	FLayoutValidationMessage& Message = Messages.AddDefaulted_GetRef();
	Message.Severity = ELayoutValidationSeverity::Error;
	Message.Message = InMessage;
}

bool FLayoutValidationResult::IsValid() const
{
	for (const FLayoutValidationMessage& Message : Messages)
	{
		if (Message.Severity == ELayoutValidationSeverity::Error)
		{
			return false;
		}
	}

	return true;
}

bool FLayoutValidationResult::HasWarnings() const
{
	for (const FLayoutValidationMessage& Message : Messages)
	{
		if (Message.Severity == ELayoutValidationSeverity::Warning)
		{
			return true;
		}
	}

	return false;
}

bool FLayoutFaceRule::IsTraversable() const
{
	return !ConnectedTraversalChannels.IsEmpty();
}

FGameplayTagContainer FLayoutFaceRule::GetEffectiveConnectionTags() const
{
	FGameplayTagContainer Result;
	if (ConnectionTag.IsValid())
	{
		Result.AddTag(ConnectionTag);
	}
	return Result;
}

FGameplayTag FLayoutFaceRule::GetEffectiveConnectionTag() const
{
	return ConnectionTag;
}

const FGameplayTagContainer& FLayoutFaceRule::GetEffectiveAllowedConnectionTags() const
{
	return AllowedConnectionTags;
}

FLayoutModuleFaceRules::FLayoutModuleFaceRules()
{
	NormalizeDirections();
}

void FLayoutModuleFaceRules::NormalizeDirections()
{
	PosX.Direction = ELayoutFaceDirection::PosX;
	NegX.Direction = ELayoutFaceDirection::NegX;
	PosY.Direction = ELayoutFaceDirection::PosY;
	NegY.Direction = ELayoutFaceDirection::NegY;
	PosZ.Direction = ELayoutFaceDirection::PosZ;
	NegZ.Direction = ELayoutFaceDirection::NegZ;
}

FLayoutFaceRule* FLayoutModuleFaceRules::FindRule(const ELayoutFaceDirection Direction)
{
	switch (Direction)
	{
	case ELayoutFaceDirection::PosX:
		return &PosX;
	case ELayoutFaceDirection::NegX:
		return &NegX;
	case ELayoutFaceDirection::PosY:
		return &PosY;
	case ELayoutFaceDirection::NegY:
		return &NegY;
	case ELayoutFaceDirection::PosZ:
		return &PosZ;
	case ELayoutFaceDirection::NegZ:
	default:
		return &NegZ;
	}
}

const FLayoutFaceRule* FLayoutModuleFaceRules::FindRule(const ELayoutFaceDirection Direction) const
{
	switch (Direction)
	{
	case ELayoutFaceDirection::PosX:
		return &PosX;
	case ELayoutFaceDirection::NegX:
		return &NegX;
	case ELayoutFaceDirection::PosY:
		return &PosY;
	case ELayoutFaceDirection::NegY:
		return &NegY;
	case ELayoutFaceDirection::PosZ:
		return &PosZ;
	case ELayoutFaceDirection::NegZ:
	default:
		return &NegZ;
	}
}

void FLayoutModuleFaceRules::SetRule(const FLayoutFaceRule& Rule)
{
	if (FLayoutFaceRule* const TargetRule = FindRule(Rule.Direction))
	{
		*TargetRule = Rule;
	}

	NormalizeDirections();
}

TArray<FLayoutFaceRule> FLayoutModuleFaceRules::ToArray() const
{
	return {PosX, NegX, PosY, NegY, PosZ, NegZ};
}

ELayoutFaceDirection FLayoutDirectionUtils::GetOpposite(const ELayoutFaceDirection Direction)
{
	switch (Direction)
	{
	case ELayoutFaceDirection::PosX:
		return ELayoutFaceDirection::NegX;
	case ELayoutFaceDirection::NegX:
		return ELayoutFaceDirection::PosX;
	case ELayoutFaceDirection::PosY:
		return ELayoutFaceDirection::NegY;
	case ELayoutFaceDirection::NegY:
		return ELayoutFaceDirection::PosY;
	case ELayoutFaceDirection::PosZ:
		return ELayoutFaceDirection::NegZ;
	case ELayoutFaceDirection::NegZ:
	default:
		return ELayoutFaceDirection::PosZ;
	}
}

FIntVector FLayoutDirectionUtils::ToCellDelta(const ELayoutFaceDirection Direction)
{
	switch (Direction)
	{
	case ELayoutFaceDirection::PosX:
		return FIntVector(1, 0, 0);
	case ELayoutFaceDirection::NegX:
		return FIntVector(-1, 0, 0);
	case ELayoutFaceDirection::PosY:
		return FIntVector(0, 1, 0);
	case ELayoutFaceDirection::NegY:
		return FIntVector(0, -1, 0);
	case ELayoutFaceDirection::PosZ:
		return FIntVector(0, 0, 1);
	case ELayoutFaceDirection::NegZ:
	default:
		return FIntVector(0, 0, -1);
	}
}

ELayoutFaceDirection FLayoutDirectionUtils::RotateYaw(const ELayoutFaceDirection Direction, const int32 YawRotationSteps)
{
	int32 NormalizedSteps = YawRotationSteps % 4;
	if (NormalizedSteps < 0)
	{
		NormalizedSteps += 4;
	}

	ELayoutFaceDirection Result = Direction;
	for (int32 Step = 0; Step < NormalizedSteps; ++Step)
	{
		switch (Result)
		{
		case ELayoutFaceDirection::PosX:
			Result = ELayoutFaceDirection::PosY;
			break;
		case ELayoutFaceDirection::PosY:
			Result = ELayoutFaceDirection::NegX;
			break;
		case ELayoutFaceDirection::NegX:
			Result = ELayoutFaceDirection::NegY;
			break;
		case ELayoutFaceDirection::NegY:
			Result = ELayoutFaceDirection::PosX;
			break;
		case ELayoutFaceDirection::PosZ:
		case ELayoutFaceDirection::NegZ:
		default:
			return Result;
		}
	}

	return Result;
}
