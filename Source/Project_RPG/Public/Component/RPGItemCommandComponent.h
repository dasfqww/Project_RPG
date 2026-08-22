#pragma once

#include "Components/ActorComponent.h"
#include "Item/Transaction/RPGItemAsyncCommandOrchestrator.h"
#include "RPGItemCommandComponent.generated.h"

class URPGInventoryProjectionComponent;

UENUM(BlueprintType)
enum class ERPGItemCommandResultCode : uint8
{
	Succeeded,
	AlreadyApplied,
	InvalidRequest,
	Forbidden,
	Conflict,
	BackendRejected,
	ProtocolError,
	ServerStateError,
	Busy
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemCommandClientResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	UPROPERTY(BlueprintReadOnly)
	FName Operation;

	UPROPERTY(BlueprintReadOnly)
	ERPGItemCommandResultCode Result =
		ERPGItemCommandResultCode::ServerStateError;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGItemCommandCompleted,
	FRPGItemCommandClientResult,
	Result);

/** Owner RPC facade for the asynchronous Item V2 command pipeline. */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGItemCommandComponent final
	: public UActorComponent
{
	GENERATED_BODY()

public:
	URPGItemCommandComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(
		const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerEquipItem(
		FGuid RequestId,
		FGuid ItemId,
		int64 ExpectedRevision,
		EEquipmentSlotType SlotType);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerUnequipItem(
		FGuid RequestId,
		FGuid ItemId,
		int64 ExpectedRevision,
		int32 InventorySlotIndex);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerConsumeItem(
		FGuid RequestId,
		FGuid ItemId,
		int64 ExpectedRevision);

	UPROPERTY(BlueprintAssignable, Category = "RPG|Item")
	FRPGItemCommandCompleted OnItemCommandCompleted;

	/** Called by the owning controller after possession changes. */
	void HandlePawnChanged();

	bool ApplyCommitAndReconcile(
		const FRPGItemOwnerRef& ExpectedOwner,
		const FRPGItemBackendCommitResult& Result,
		FString& OutError);
	bool EnqueueFirstCommitEffect(
		const FRPGItemBackendCommitResult& Result,
		FString& OutError);

private:
	bool EnsureOrchestrator();
	bool TryGetAuthenticatedOwner(FRPGItemOwnerRef& OutOwner) const;
	bool BeginRequest(const FGuid& RequestId, FName Operation);
	void HandleOutcome(
		FGuid RequestId,
		FName Operation,
		FRPGItemAsyncCommandOutcome Outcome);
	void HandleAuthoritativeRecordsChanged();
	bool ReconcileEquipmentFromProjection(FString* OutError = nullptr);
	void ProcessPendingFirstCommitEffects();

	UFUNCTION(Client, Reliable)
	void ClientNotifyItemCommand(FRPGItemCommandClientResult Result);

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Item",
		meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaximumInFlightCommands = 8;

	TSet<FGuid> InFlightRequestIds;
	TSet<FGuid> AppliedFirstCommitEffects;
	TMap<FGuid, FRPGItemBackendCommitResult> PendingFirstCommitEffects;
	FDelegateHandle ProjectionChangedHandle;

	TUniquePtr<IRPGItemAsyncCommitter> CommitterAdapter;
	TUniquePtr<IRPGItemCommitSink> CommitSinkAdapter;
	TUniquePtr<IRPGItemFirstCommitSink> FirstCommitSinkAdapter;
	TSharedPtr<
		FRPGItemAsyncCommandOrchestrator,
		ESPMode::ThreadSafe> Orchestrator;
};
