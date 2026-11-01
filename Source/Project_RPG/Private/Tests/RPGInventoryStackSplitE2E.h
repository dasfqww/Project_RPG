#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class APlayerController;
class URPGInventoryProjectionWidget;
class UWidget;
class UEditableTextBox;
struct FRPGInventoryConcurrencyE2E;
struct FRPGInventoryRecoveryE2E;

/** Opt-in rendered input only. Never grants identity, writes records or seeds projection. */
struct FRPGInventoryStackSplitE2E
{
	int32 Tick(APlayerController& Controller); // 0 running, 1 finished, -1 failed
	FString Error;
private:
	friend struct FRPGInventoryRecoveryE2E;
	TSharedPtr<FRPGInventoryRecoveryE2E> RecoveryProbe;
	TSharedPtr<FRPGInventoryConcurrencyE2E> ConcurrencyProbe;
	int32 Phase = 0;
	FGuid SourceId, CreatedId, RequestId;
	double Deadline = 0.0, RejectionObservedAt = 0.0;
	TWeakObjectPtr<URPGInventoryProjectionWidget> Bag;
	bool bQuantityMode = false;
	bool bMoveSwapMode = false;
	FGuid OtherId, MoveRequestId;
	FString MoveContinueFile;
	FString QuantityContinueFile;
	int32 TickMoveSwap(APlayerController& Controller);
	bool Drag(APlayerController& Controller, int32 DestinationSlot, bool bExpectAccepted, int32 SourceSlot = 0);
	bool Click(APlayerController& Controller, UWidget* Widget, bool bRightMouseButton = false);
	bool TypeQuantity(APlayerController& Controller, UEditableTextBox* Input, const FString& Text);
};
