#pragma once
#include "Riding/RPGRidingRegistrationReader.h"
#include "Riding/RPGRidingCommandCoordinator.h"
#include "Item/Persistence/RPGItemRecord.h"

struct FRPGRidingRegistrationRow { FGuid Item; FText Label; bool bNamed = false; };
struct FRPGRidingRegistrationView
{
    bool bAvailable = false, bLoading = false, bCanReload = false, bCanReview = false, bCanConfirm = false;
    bool bCanRecover = false, bCanRetry = false, bCanStorage = false, bCanRefreshAssets = false;
    FGuid Snapshot, Review;
    FText Status, ReviewText;
    TArray<FRPGRidingRegistrationRow> Rows;
};
/** Explicit current reads, versioned display metadata and one-use consent. Commands remain owned by LocalPlayer. */
class PROJECT_RPG_API FRPGRidingRegistrationClient final : public TSharedFromThis<FRPGRidingRegistrationClient>
{
public:
    using FNames = TFunction<bool(const FRPGItemRecord&, const FRPGRidingRegistrationOption&, FText&, FText&)>;
    FRPGRidingRegistrationClient(TSharedRef<FRPGRidingCollectionReader> Collection, TSharedRef<FRPGRidingRegistrationReader> Definitions,
        TSharedRef<IRPGRidingOwnerTransport> Bag, TSharedRef<FRPGRidingCommandCoordinator> Commands,
        FRPGRidingCollectionReader::FContext Context, FNames Names, TFunction<bool()> RefreshAssets);
    ~FRPGRidingRegistrationClient();
    void RefreshContext();
    void Shutdown();
    FRPGRidingRegistrationView GetView();
    bool Reload();
    bool Review(FGuid Snapshot, FGuid Item);
    bool Confirm(FGuid Review);
    void Dismiss();
    bool Recover();
    bool Retry();
    bool RetryStorage();
    bool RefreshAssets();
    FSimpleMulticastDelegate OnChanged;
private:
    friend struct FRPGRidingRegistrationUiTestAccess;
    enum class EPhase : uint8 { Idle, Collection, Definitions, Inventory, Ready, Failed };
    void CollectionChanged();
    void DefinitionsChanged();
    void CommandsChanged();
    void CompleteBag(FGuid Request, FRPGRidingCollectionContext Captured, FRPGRidingOwnerHttpResult Result);
    void Clear();
    void Fail();
    bool Usable();
    bool Ready();
    bool Candidate(const FRPGItemRecord&, const FRPGRidingRegistrationOption*&) const;
    TSharedRef<FRPGRidingCollectionReader> Collection;
    TSharedRef<FRPGRidingRegistrationReader> Definitions;
    TSharedRef<IRPGRidingOwnerTransport> Bag;
    TSharedRef<FRPGRidingCommandCoordinator> Commands;
    FRPGRidingCollectionReader::FContext Context;
    FNames Names;
    TFunction<bool()> RefreshAction;
    FRPGRidingCollectionContext Bound;
    FRPGRidingRegistrationOptions Options;
    TArray<FRPGItemRecord> Records;
    FGuid Snapshot, Pending, Consent, ReviewedItem;
    FText ItemName, MountName, ConsentText;
    double ReceivedAt = 0, ReviewedAt = 0, RetryAt = 0;
    EPhase Phase = EPhase::Idle;
    FDelegateHandle CollectionHandle, DefinitionsHandle, CommandsHandle;
    bool bEnding = false, bRefreshing = false;
};
