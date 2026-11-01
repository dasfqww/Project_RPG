#pragma once

#include "CoreMinimal.h"

/** Reliable ASC RPCs are ordered. Track one 15-bit prediction-key sequence,
 * not an ever-growing set of client-selected message keys. */
struct PROJECT_RPG_API FRPGTargetDataRequestSequence
{
	bool TryAccept(int16 PredictionKey, bool bCancellation);
	int16 LastPredictionKey = 0;
	uint8 AcceptedKinds = 0;
};

/** Counts all requests, including malformed/duplicate ones, before any cache allocation. */
struct PROJECT_RPG_API FRPGTargetDataRequestWindow
{
	bool TryConsume(double ServerTimeSeconds, int32 MaximumRequests);
	double StartedAt = -1.0;
	int32 Requests = 0;
};
