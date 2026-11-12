#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "Auction/RPGAuctionCoordinator.h"
#include "Progression/RPGEquipmentMutationSubsystem.h"
#include "RPGAuctionSubsystem.generated.h"

/** Persistent owner independent of widget lifetime. No automatic mutation or recovery dispatch. */
UCLASS(Config=Game)
class PROJECT_RPG_API URPGAuctionSubsystem final : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    TSharedPtr<FRPGAuctionCoordinator> GetCoordinator();
    bool BlocksTransitions();
private:
    FRPGAuctionLobbyContext ReadContext() const;
    bool TickAuction(float);
    UPROPERTY(Config) bool bAuctionsEnabled = false;
    TSharedPtr<FRPGAuctionCoordinator> Coordinator;
    FTSTicker::FDelegateHandle TickHandle;
    bool bEnding = false;
    TWeakObjectPtr<URPGEquipmentMutationSubsystem> EquipmentReader;
    FRPGEquipmentReadTicket AssetTicket;
};
