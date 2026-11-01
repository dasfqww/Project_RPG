#pragma once
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
class APlayerController;
class APawn;
class URPGAbilitySystemComponent;
class URPGInventoryProjectionWidget;
class URPGInventoryProjectionViewModel;
struct FRPGInventoryRecoveryE2E
{
	~FRPGInventoryRecoveryE2E();
	int32 Tick(APlayerController& Controller);
	FString Error;
private:
	int32 Phase = 0;
	double Deadline = 0.0;
	FGuid Source, Other, Request, Created;
	FString ContinueFile;
	TWeakObjectPtr<URPGInventoryProjectionWidget> Bag;
	TWeakObjectPtr<URPGInventoryProjectionWidget> Mail;
	TWeakObjectPtr<URPGInventoryProjectionWidget> Equipment;
	TWeakObjectPtr<URPGInventoryProjectionViewModel> BoundModel;
	FDelegateHandle ResultHandle;
	int32 Acknowledgements = 0, BeforeRetryAcknowledgements = 0;
	bool bRetryFailure = false;
	bool bCredentialExpiry = false;
	bool bRestored = false;
	bool bSwap = false;
	bool bMerge = false;
	bool bSplit = false;
	bool bClaimMail = false;
	bool bEquip = false;
	bool bUnequip = false;
	bool bConsume = false;
	bool bConsumeQuickSlot = false;
	bool bConsumeShortcutAssigned = false;
	bool bConsumeDeath = false;
	bool bConsumeDeathObserved = false;
	TWeakObjectPtr<APawn> ConsumePawn;
	TWeakObjectPtr<URPGAbilitySystemComponent> ConsumeAbilities;
	float ConsumeHealthBefore = 0.f;
	void UnbindResult();
};
