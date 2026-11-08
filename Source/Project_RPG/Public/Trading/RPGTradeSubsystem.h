#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "Trading/RPGTradeLobbyOwner.h"
#include "RPGTradeSubsystem.generated.h"

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
private:
    FRPGTradeLobbyContext ReadContext() const;
    bool TickTrade(float);
    UPROPERTY(Config) bool bTradingUIEnabled = false;
    TSharedPtr<FRPGTradeLobbyOwner> Owner;
    FTSTicker::FDelegateHandle TickHandle;
    bool bEnding = false;
};
