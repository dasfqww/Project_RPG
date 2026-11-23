#pragma once
#include "Riding/RPGRidingCollectionReader.h"
#include "Riding/RPGRidingCommandCoordinator.h"

struct FRPGRidingPreferencesChoice { FString Id; FText Label; };
struct FRPGRidingPreferencesView
{
    bool bAvailable = false, bLoading = false, bCanReload = false, bCanEdit = false, bDirty = false, bCanReview = false, bCanConfirm = false;
    bool bCanRecover = false, bCanRetry = false, bCanStorage = false, bCanRefreshAssets = false;
    FGuid Snapshot, Review;
    FRPGRidingPreferencesCommand Draft;
    TArray<FRPGRidingPreferencesChoice> Choices;
    FText Status, ReviewText, FavoritesText;
};
/** Ephemeral owned-mount draft and one-use consent. Only the existing durable coordinator sends a PUT. */
class PROJECT_RPG_API FRPGRidingPreferencesClient final : public TSharedFromThis<FRPGRidingPreferencesClient>
{
public:
    using FNames = TFunction<FText(const FRPGRidingOwnedMount&)>;
    FRPGRidingPreferencesClient(TSharedRef<FRPGRidingCollectionReader> Collection, TSharedRef<FRPGRidingCommandCoordinator> Commands,
        FRPGRidingCollectionReader::FContext Context, FNames Names, TFunction<bool()> RefreshAssets);
    ~FRPGRidingPreferencesClient();
    void RefreshContext();
    void Shutdown();
    FRPGRidingPreferencesView GetView();
    bool Reload();
    bool Select(FGuid Snapshot, const FString& Mount);
    bool ToggleFavorite(FGuid Snapshot, const FString& Mount);
    bool SetMode(FGuid Snapshot, ERPGRidingSelectorKind Mode);
    bool AssignQuickSlot(FGuid Snapshot, const FString& Mount);
    bool SetSpecific(FGuid Snapshot, const FString& Mount);
    bool ClearAll(FGuid Snapshot);
    bool ResetDraft(FGuid Snapshot);
    bool Review(FGuid Snapshot);
    bool Confirm(FGuid Review);
    void Dismiss();
    bool Recover();
    bool Retry();
    bool RetryStorage();
    bool RefreshAssets();
    FSimpleMulticastDelegate OnChanged;
private:
    friend struct FRPGRidingPreferencesUiTestAccess;
    void CollectionChanged();
    void CommandsChanged();
    void Clear();
    void Changed();
    bool Available();
    bool Editable(FGuid Snapshot);
    bool Owned(const FString& Id, bool bAllowEmpty = true) const;
    bool ValidDraft() const;
    bool Dirty() const;
    bool Describe(FText& Out) const;
    TSharedRef<FRPGRidingCollectionReader> Collection;
    TSharedRef<FRPGRidingCommandCoordinator> Commands;
    FRPGRidingCollectionReader::FContext Context;
    FNames Names;
    TFunction<bool()> RefreshAction;
    FRPGRidingCollectionContext Bound;
    FRPGRidingCollection Baseline;
    FRPGRidingPreferencesCommand Draft;
    FGuid Snapshot, Consent;
    FText ConsentText, Error;
    double ReceivedAt = 0;
    FDelegateHandle CollectionHandle, CommandsHandle;
    bool bEnding = false, bRefreshing = false;
};
