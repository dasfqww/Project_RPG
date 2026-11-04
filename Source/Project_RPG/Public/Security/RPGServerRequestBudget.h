#pragma once

#include "CoreMinimal.h"

/** Constant-memory ingress budget. Use a server monotonic clock, never world/client time.
 * Invalid and duplicate requests must consume this before validation or allocation.
 * Exhaustion is load shedding, not evidence sufficient for a cheat penalty. */
struct FRPGServerRequestBudget
{
	explicit FRPGServerRequestBudget(int32 InBurst = 128, double InRate = 64.0)
		: Capacity(InBurst), Rate(InRate), Tokens(InBurst) {}

	bool TryConsume(double Now)
	{
		if (!FMath::IsFinite(Now) || Now < 0.0 || Capacity < 1 ||
			!FMath::IsFinite(Rate) || Rate <= 0.0 || (LastUpdate >= 0.0 && Now < LastUpdate)) return false;
		if (LastUpdate >= 0.0)
		{
			const double RefillSeconds = FMath::Min(Now - LastUpdate, Capacity / Rate);
			Tokens = FMath::Min(static_cast<double>(Capacity), Tokens + RefillSeconds * Rate);
		}
		LastUpdate = Now;
		if (Tokens < 1.0) return false;
		Tokens -= 1.0; // Reserve before callbacks, including reentrant RPC dispatch.
		return true;
	}

private:
	int32 Capacity;
	double Rate;
	double Tokens;
	double LastUpdate = -1.0;
};
