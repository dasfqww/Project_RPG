#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class APlayerController;

/** Opt-in authenticated client ingress probe. Never edits projection or grants identity. */
struct FRPGInventoryConcurrencyE2E
{
	~FRPGInventoryConcurrencyE2E();
	int32 Tick(APlayerController& Controller);
	FString Error;
private:
	int32 Phase = 0, Role = 0;
	double Deadline = 0.0;
	FGuid OwnA, OwnB, ForeignA, ForeignB, BadRequest, RequestA, RequestB, WinnerRequest, WinnerItem;
	FString StartGate;
	TStrongObjectPtr<UObject> ModelOwner;
	FDelegateHandle ResultHandle;
	TMap<FGuid, uint8> Results;
};
