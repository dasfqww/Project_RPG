#pragma once

#include "CoreMinimal.h"
#include "UObject/PrimaryAssetId.h"

class APlayerController;
class ARPGNonPlayerCharacter;
class ARPGPickUpBase;
class URPGAbilitySystemComponent;
class UWorld;
struct FRPGItemCommandClientResult;

// Explicit opt-in: saved NPC death -> transient drop row -> normal pickup RPC.
// No new RPC, client grant, saved asset/CDO edit, or production drop content.
class FRPGNPCPickupNetworkProbe
{
public:
	~FRPGNPCPickupNetworkProbe();
	void Tick(APlayerController& Controller);
	void HandleResult(const FRPGItemCommandClientResult& Result);
	void Stop();
private:
#if !UE_BUILD_SHIPPING
	struct FObservation { int32 Deaths = 0; int32 Drops = 0; int32 Ends = 0; bool Claimed = false; };
	void TickServer(APlayerController& Controller);
	void TickClient(APlayerController& Controller);
	void TickRestartRecovery(APlayerController& Controller);
	bool ConfigureParty();
	bool GatherParty(APlayerController& Controller, TArray<APlayerController*>& Party);
	void Fail(const TCHAR* Reason);
	TSharedRef<FObservation> Observation = MakeShared<FObservation>();
	TWeakObjectPtr<UWorld> ObservedWorld;
	TWeakObjectPtr<ARPGNonPlayerCharacter> NPC;
	TWeakObjectPtr<ARPGPickUpBase> Pickup;
	TWeakObjectPtr<URPGAbilitySystemComponent> ObservedASC;
	TWeakObjectPtr<APlayerController> ObservedController;
	FDelegateHandle ActivatedHandle, EndedHandle, SpawnHandle;
	FPrimaryAssetId DefinitionId;
	FGuid SuccessRequestId, RejectedRequestId;
	FGuid RestartOwnerId, RestartItemId, RestartRequestId;
	TArray<FGuid> RejectedRequests;
	TArray<FString> ExpectedCharacters;
	uint64 NPCNetId = 0, PickupNetId = 0;
	int32 SpawnCount = 0, Results = 0, Stage = 0;
	double Deadline = 0.0;
	bool bFinished = false;
	bool bConfigured = false;
	bool bObservedUnknownAck = false;
#endif
};
