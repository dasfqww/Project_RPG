#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "Trading/RPGTradeLobbyOwner.h"
#include "Progression/RPGEquipmentMutationSubsystem.h"
#include "RPGTradeSubsystem.generated.h"
class URPGEquipmentMutationSubsystem;

/** One primary local-player owner; closing/recreating a widget never resets the coordinator. */
UCLASS(Config=Game)
class PROJECT_RPG_API URPGTradeSubsystem final : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    TSharedPtr<FRPGTradeLobbyOwner> GetTradeOwner();
    bool IsAvailable();
    bool BlocksTransitions();
    // Shared definition/version and localized option labels for read-only item presentation.
    bool DescribeItem(const FRPGTradeItemDetails& Item,FText& Name,FText& Description) const { return ResolveItem(Item,Name,Description); }
private:
    FRPGTradeLobbyContext ReadContext() const;
    bool TickTrade(float);
    UPROPERTY(Config) bool bTradingUIEnabled = false;
    // Explicit gameplay labels. Unknown metadata blocks consent instead of printing internal tags.
    UPROPERTY(Config) TMap<FString, FText> TradeStatLabels;
    UPROPERTY(Config) TMap<FString, FText> TradeTagLabels;
    UPROPERTY(Config) TMap<FString, FText> TradeGrowthLabels;
    bool ResolveItem(const FRPGTradeItemDetails& Item, FText& Name, FText& Description) const;
    TSharedPtr<FRPGTradeLobbyOwner> Owner;
    FTSTicker::FDelegateHandle TickHandle;
    bool bEnding = false;
    bool ReloadTradeBag();
    FRPGTradeBagSnapshot ReadTradeBag();
    void UpdateTradeBag();
    UFUNCTION() void HandleTradeBagChanged();
    TWeakObjectPtr<URPGEquipmentMutationSubsystem> EquipmentReader;
    FRPGTradeBagSnapshot CachedBag;
    bool bBagWaiting = false, bBagSawLoading = false;
    double BagReadDeadline = 0;
    FRPGEquipmentReadTicket AssetTicket;
};
