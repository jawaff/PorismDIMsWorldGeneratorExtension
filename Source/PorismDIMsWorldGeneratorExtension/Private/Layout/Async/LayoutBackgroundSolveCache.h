// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeRWLock.h"

/** Shared immutable-snapshot cache owner for background layout solves. */
class FLayoutBackgroundSolveCache
{
public:
	/** Clears first-slice cache state. Future manifest/noise snapshot caches hang off this owner. */
	void Reset();

private:
	/** Lock reserved for hash-keyed manifest and compiled-noise snapshot cache maps. */
	FRWLock CacheLock;
};
