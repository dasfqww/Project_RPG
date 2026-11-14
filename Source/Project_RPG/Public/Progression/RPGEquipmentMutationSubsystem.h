#pragma once

#include "CoreMinimal.h"
#include "Http.h"
#include "Progression/RPGEquipmentMutationJournal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Progression/RPGEquipmentReadGuard.h"
#include "Economy/Backend/RPGEconomyBackendTypes.h"
#include "RPGEquipmentMutationSubsystem.generated.h"

class UHttpWebManager;
struct FRPGEquipmentReadTicket { FRPGEquipmentMutationScope Scope; uint64 Generation=0; };
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRPGEquipmentMutationsChanged);

/** Player-only narrow commands. Backend ownership/lease/CAS remain authoritative.
 * Blueprint callers supply expectations, never replacement records or prices.
 * A true return means a durable intent exists, NOT that payment succeeded.
 */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGEquipmentMutationSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	UPROPERTY(BlueprintAssignable, Category="RPG|Equipment") FOnRPGEquipmentMutationsChanged OnEquipmentMutationsChanged;
	/** Call on screen/character entry to restore any original pending intent. */
	UFUNCTION(BlueprintCallable, Category="RPG|Equipment") FRPGEquipmentMutationView OpenCharacter(FGuid CharacterId);
	UFUNCTION(BlueprintPure, Category="RPG|Equipment") FRPGEquipmentMutationView GetState(FGuid CharacterId) const;
	UFUNCTION(BlueprintPure, Category="RPG|Equipment") bool IsRequestInFlight() const { return ActiveRequest.IsValid(); }
	/** Fresh player GET into the existing read-only projection entries; no item mutation. */
	UFUNCTION(BlueprintCallable, Category="RPG|Equipment") bool RefreshItems(FGuid CharacterId);
	UFUNCTION(BlueprintPure, Category="RPG|Equipment") bool IsItemsReadInFlight() const { return ActiveReadRequest.IsValid(); }
	// Native-only read projection import. Never dispatches, clears a journal or changes server assets.
	bool BeginExternalRead(FGuid Character, FRPGEquipmentReadTicket& Out);
	bool IsExternalReadCurrent(const FRPGEquipmentReadTicket& Ticket) const;
    bool ApplyExternalInventoryRead(FRPGEquipmentReadTicket& Ticket, const TArray<FRPGInventoryProjectionEntry>& Items,
        const TArray<FRPGInventoryProjectionEntry>& Mail);
	bool ApplyExternalRead(FRPGEquipmentReadTicket& Ticket, const TArray<FRPGInventoryProjectionEntry>& Items,
		const TArray<FRPGInventoryProjectionEntry>& Mail, const FRPGEconomyWalletResult& Wallet);
	UFUNCTION(BlueprintCallable, Category="RPG|Equipment") bool UpgradeEquipment(FGuid CharacterId,
		const FRPGEquipmentItemExpectation& Equipment, const TArray<FRPGEquipmentCostExpectation>& OreStacks);
	UFUNCTION(BlueprintCallable, Category="RPG|Equipment") bool TransferEquipment(FGuid CharacterId,
		const FRPGEquipmentItemExpectation& Source, const FRPGEquipmentItemExpectation& Target);
	UFUNCTION(BlueprintCallable, Category="RPG|Equipment") bool CraftEquipment(FGuid CharacterId,
		const FString& TargetDefinitionName, const TArray<FRPGEquipmentCostExpectation>& TokenStacks);
	UFUNCTION(BlueprintCallable, Category="RPG|Equipment") bool DismantleEquipment(FGuid CharacterId,
		const FRPGEquipmentItemExpectation& Equipment);
	/** Explicit retry only. Never replaces the stored request ID/body or selects new costs. */
	UFUNCTION(BlueprintCallable, Category="RPG|Equipment") bool RetryPending(FGuid CharacterId);
private:
	friend struct FRPGEquipmentMutationTestAccess;
	UHttpWebManager* Web() const;
	bool ScopeFor(FGuid CharacterId, FRPGEquipmentMutationScope& OutScope) const;
	bool Begin(FRPGEquipmentMutationCommand Command);
	bool Dispatch(const FRPGEquipmentPendingMutation& Pending);
	void Complete(uint64 Attempt, FRPGEquipmentMutationScope Scope, FRPGEquipmentMutationCommand Command, FRPGEquipmentMutationHttpResult Result);
	void Publish(const FRPGEquipmentMutationScope& Scope, FRPGEquipmentMutationView View, bool bExternalAssets = false);
	void CancelItemsRead();
	void CompleteItems(uint64 Serial, FRPGEquipmentMutationScope Scope, FRPGEquipmentMutationHttpResult Result);
	FString JournalRoot;
	TMap<FString, FRPGEquipmentMutationView> Views;
	FHttpRequestPtr ActiveRequest;
	FHttpRequestPtr ActiveReadRequest;
	uint64 ReadSerial = 0;
	FRPGEquipmentMutationScope ActiveScope;
	uint64 AttemptSerial = 0;
	bool bShuttingDown = false;
	FRPGEquipmentReadGuard ReadGuard;
#if WITH_DEV_AUTOMATION_TESTS
	TWeakObjectPtr<UHttpWebManager> TestManager;
	TFunction<bool(const FHttpRequestRef&)> TestDispatch;
#endif
};
