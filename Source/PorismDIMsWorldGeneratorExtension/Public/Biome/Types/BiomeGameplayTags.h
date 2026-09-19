// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "NativeGameplayTags.h"

/** Shared gameplay tags used by Porism biome-generation strategy assets and FNE wrappers. */
namespace BiomeGameplayTags
{
	/** Generic foundation biome category used when no narrower foundation biome tag is needed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Foundation);

	/** Primary foundation biome for the main generated terrain body. */
	PORISMDIMSWORLDGENERATOREXTENSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(FoundationMain);

	/** Secondary foundation biome for alternate generated terrain bodies or child/neighbor foundations. */
	PORISMDIMSWORLDGENERATOREXTENSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(FoundationSecondary);

	/** Generic reservation biome category used when no narrower reservation biome tag is needed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reservation);

	/** Center reservation biome for central surface anchors or primary build pockets. */
	PORISMDIMSWORLDGENERATOREXTENSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(ReservationCenter);
}
