#pragma once

#include "CoreMinimal.h"
#include "Item/Backend/RPGItemBackendGateway.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGItemBackendSubsystem.generated.h"

class FRPGPickupGrantJournal;
class FRPGInventoryCommandJournal;

/** Dedicated-server owner of the Item V2 HTTP gateway. */
UCLASS(Config = Game, DefaultConfig)
class RPGINVENTORYRUNTIME_API URPGItemBackendSubsystem final
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool IsAvailable() const;
	/** Server-only allocation context; callers must derive identity from their authenticated PlayerState. */
	TSharedPtr<FRPGInventoryCommandJournal> OpenInventoryJournal(const FString& SessionId, const FString& CharacterId, const FString& SteamId);

	bool LoadCharacterItems(
		const FString& CharacterId,
		FRPGItemBackendLoadCompletion Completion);

	bool Commit(
		const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion);

	/** Native pickup-only entry: persist v2 origin before prepare/atomic consume. */
	bool CommitPickup(const FGuid& DropId, const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion, TFunction<bool()> CanContinue = {});

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FRPGPickupJournalSubsystemTestAccess;
#endif
	bool HasRestoredPickup(const FRPGItemOwnerRef& Owner) const;
	void ReconcileRestoredPickups(const FRPGItemOwnerRef& Owner, TFunction<void(bool)> Completion);

	UPROPERTY(Config, EditAnywhere, Category = "Item Backend")
	FString ApiUrl = TEXT("http://localhost:3000/api");
	/** Stable server-owned mounted volume across process restarts; empty uses Saved/ItemInventoryJournal. */
	UPROPERTY(Config, EditAnywhere, Category = "Item Backend")
	FString InventoryJournalDirectory;

	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Item Backend",
		meta = (ClampMin = "1.0"))
	float RequestTimeoutSeconds = 10.0f;

	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Item Backend",
		meta = (ClampMin = "1", ClampMax = "5"))
	int32 MaximumAttempts = 2;

	FString ServiceToken;
	TSharedPtr<FRPGItemBackendGateway> Gateway;
	TSharedPtr<FRPGPickupGrantJournal> PickupJournal;
	TSet<FGuid> RestoredPickupIds;
	TMap<FString, TSharedPtr<FRPGInventoryCommandJournal>> InventoryJournals;
};
