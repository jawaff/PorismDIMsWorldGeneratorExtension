// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutBackgroundSolveCache.h"

void FLayoutBackgroundSolveCache::Reset()
{
	FWriteScopeLock Lock(CacheLock);
}
