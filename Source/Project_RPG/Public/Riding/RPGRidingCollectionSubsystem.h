#pragma once
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "Riding/RPGRidingCollectionReader.h"
#include "Riding/RPGRidingCommandCoordinator.h"
#include "Riding/RPGRidingFreshRead.h"
#include "Progression/RPGEquipmentMutationSubsystem.h"
#include "RPGRidingCollectionSubsystem.generated.h"

struct FStreamableHandle;

/** Local owner of the read screen and durable commands; screen closure never disposes a command. */
UCLASS()
class PROJECT_RPG_API URPGRidingCollectionSubsystem final : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    TSharedPtr<FRPGRidingCollectionReader> GetReader();
    void ReleaseReader();
    TSharedPtr<FRPGRidingCommandCoordinator> GetCommands();
    bool BlocksTransitions();
    TSharedPtr<FRPGRidingFreshRead> GetFreshReads();
    bool RefreshFreshAssets();
    FText DescribeMount(const FString& Id) const;
private:
    friend struct FRPGRidingLobbyCommandTestAccess;
    FRPGRidingCollectionContext ReadContext() const;
    FRPGRidingCommandContext ReadCommandContext() const;
    bool TickCollection(float);
    void RefreshPresentationAssets();
    TSharedPtr<FRPGRidingCollectionReader> Reader;
    TSharedPtr<FRPGRidingCommandCoordinator> Commands;
    TSharedPtr<FRPGRidingFreshRead> FreshReads;
    FRPGEquipmentReadTicket InventoryTicket;
    TSharedPtr<FStreamableHandle> PresentationLoad;
    FString PresentationKey;
    FTSTicker::FDelegateHandle TickHandle;
    bool bEnding = false;
};
