// Copyright 2026 Spotted Loaf Studio
#pragma once

#include "IDetailCustomization.h"

/** Separates actor/native settings and shared diagnostics; component settings stay in component selection. */
class FChunkWorldExtendedDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

/** Labels component-owned domain categories without changing serialized fields or Blueprint API categories. */
class FChunkWorldComponentDetails : public IDetailCustomization
{
public:
	explicit FChunkWorldComponentDetails(FText InOwnerLabel) : OwnerLabel(MoveTemp(InOwnerLabel)) {}
	static TSharedRef<IDetailCustomization> MakeInstance(FText OwnerLabel);
	virtual void CustomizeDetails(IDetailLayoutBuilder& Details) override;

private:
	FText OwnerLabel;
};
