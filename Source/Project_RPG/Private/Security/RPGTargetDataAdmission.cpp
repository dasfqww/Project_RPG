#include "Security/RPGTargetDataAdmission.h"

bool FRPGTargetDataRequestSequence::TryAccept(const int16 PredictionKey, const bool bCancellation)
{
	if (PredictionKey <= 0) return false;
	if (LastPredictionKey != PredictionKey)
	{
		constexpr int32 KeyModulus = 32767;
		const int32 Delta = (static_cast<int32>(PredictionKey) - LastPredictionKey + KeyModulus) % KeyModulus;
		if (LastPredictionKey > 0 && (Delta == 0 || Delta > KeyModulus / 2)) return false;
		LastPredictionKey = PredictionKey;
		AcceptedKinds = 0;
	}
	const uint8 Kind = bCancellation ? 2 : 1;
	if ((AcceptedKinds & Kind) != 0) return false;
	AcceptedKinds |= Kind;
	return true;
}

bool FRPGTargetDataRequestWindow::TryConsume(const double ServerTimeSeconds, const int32 MaximumRequests)
{
	if (!FMath::IsFinite(ServerTimeSeconds) || ServerTimeSeconds < 0.0) return false;
	if (StartedAt < 0.0 || ServerTimeSeconds < StartedAt || ServerTimeSeconds - StartedAt >= 1.0)
	{
		StartedAt = ServerTimeSeconds;
		Requests = 0;
	}
	if (Requests >= FMath::Clamp(MaximumRequests, 1, 256)) return false;
	++Requests;
	return true;
}
