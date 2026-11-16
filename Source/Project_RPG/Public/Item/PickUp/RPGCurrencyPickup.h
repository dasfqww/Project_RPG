#pragma once
#include "Item/PickUp/RPGPickUpBase.h"
#include "Economy/Backend/RPGEconomyBackendTypes.h"
#include "Item/Loot/RPGCurrencyLootJournal.h"
#include "RPGCurrencyPickup.generated.h"
class FRPGCurrencyLootJournal;
class ARPGPlayerState;
/** Shared visual pickup, dedicated-server wallet transaction. An uncertain grant never changes recipient. */
UCLASS(Blueprintable)
class PROJECT_RPG_API ARPGCurrencyPickup : public ARPGPickUpBase
{
    GENERATED_BODY()
public:
    ARPGCurrencyPickup();
    virtual ~ARPGCurrencyPickup() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual bool CanInitializeServerDropQuantity(int32 Quantity) const override;
    virtual bool InitializeServerDropQuantity(int32 Quantity) override;
    bool TryCollect(APlayerController* Controller);
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Currency") FName CurrencyCode;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Currency", meta=(ClampMin="1")) int64 AmountPerUnit = 1;
    UPROPERTY(ReplicatedUsing=OnRep_Amount, VisibleInstanceOnly, BlueprintReadOnly, Category="Loot|Currency") int64 Amount = 0;
protected:
    virtual void TakePickup(APlayerController* Controller) override;
private:
    friend struct FRPGLootSourceTestAccess;
    void Send();
    void Receive(FRPGEconomyCommitResult Result);
    void ScheduleRetry();
    bool IsClaimantCurrent() const;
    UFUNCTION() void OnRep_Amount();
    FRPGEconomyTransactionRequest Frozen;
    TWeakObjectPtr<APlayerController> Claimant;
    TWeakObjectPtr<ARPGPlayerState> ClaimantState;
    int64 ConnectionGeneration = 0;
    FString ClaimantAccount;
    TUniquePtr<FRPGCurrencyLootJournal> Journal;
    FTimerHandle RetryTimer;
    int32 Attempts = 0;
    bool bInFlight = false, bConfirmed = false, bEnding = false;
};
