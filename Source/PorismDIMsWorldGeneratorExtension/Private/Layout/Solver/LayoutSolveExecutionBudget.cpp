// Copyright 2026 Spotted Loaf Studio

#include "LayoutSolveExecutionBudget.h"

LayoutSolveExecution::FWorkLedger*& LayoutSolveExecution::CurrentThreadLedger()
{
	static thread_local FWorkLedger* Ledger = nullptr;
	return Ledger;
}
