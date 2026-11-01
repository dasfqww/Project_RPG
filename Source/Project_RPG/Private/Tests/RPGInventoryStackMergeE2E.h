#pragma once

#include "CoreMinimal.h"

class APlayerController;
class URPGInventoryProjectionWidget;

/** Opt-in rendered client probe. Never writes records or grants identity. */
struct FRPGInventoryStackMergeE2E
{
	// 0 running, 1 finished, -1 failed (the parent probe reports Error).
	int32 Tick(APlayerController& Controller);
	FString Error;
private:
	int32 Phase = 0;
	FGuid SourceId, DestinationId, ExtraId, PartialRequest, FullRequest;
	FString ContinueFile;
	double Deadline = 0.0;
	double RejectionObservedAt = 0.0;
	TWeakObjectPtr<URPGInventoryProjectionWidget> Bag;
	bool Drag(APlayerController& Controller, int32 SourceSlot, int32 DestinationSlot, bool bExpectAccepted);
};
