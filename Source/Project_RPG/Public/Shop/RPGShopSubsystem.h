#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "Shop/RPGShopCoordinator.h"
#include "Progression/RPGEquipmentMutationSubsystem.h"
#include "RPGShopSubsystem.generated.h"

UCLASS()
class PROJECT_RPG_API URPGShopSubsystem final : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase&) override;
    virtual void Deinitialize() override;
    TSharedPtr<FRPGShopCoordinator> GetCoordinator();
    bool BlocksTransitions();
private:
    FRPGShopContext ReadContext() const;
    bool TickShop(float);
    TSharedPtr<FRPGShopCoordinator> Coordinator;
    FTSTicker::FDelegateHandle TickHandle;
    TWeakObjectPtr<URPGEquipmentMutationSubsystem> Equipment;
    FRPGEquipmentReadTicket AssetTicket;
    bool bEnding=false;
};
