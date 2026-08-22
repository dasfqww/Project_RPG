#include "Component/RPGItemCommandComponent.h"

#include "Async/Async.h"
#include "Character/RPGBaseCharacter.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "Item/Backend/RPGItemBackendSubsystem.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Manager/DataManager.h"
#include "Player/RPGPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemCommandComponent)

namespace RPGItemCommandComponent
{
const FName EquipOperation(TEXT("Item.Equip"));
const FName UnequipOperation(TEXT("Item.Unequip"));
const FName ConsumeOperation(TEXT("Item.Consume"));

class FBackendCommitter final : public IRPGItemAsyncCommitter
{
public:
	explicit FBackendCommitter(URPGItemBackendSubsystem& InBackend)
		: Backend(&InBackend)
	{
	}

	virtual bool Commit(
		const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion) override
	{
		URPGItemBackendSubsystem* BackendSubsystem = Backend.Get();
		if (!BackendSubsystem)
		{
			FRPGItemBackendCommitResult Result;
			Result.Status = ERPGItemBackendStatus::Unavailable;
			Result.RequestId = Request.RequestId;
			Result.Operation = Request.Operation;
			Result.CommandFingerprint = Request.CommandFingerprint;
			Result.Actor = Request.Actor;
			Result.Error = TEXT("The item backend subsystem no longer exists.");
			Completion(MoveTemp(Result));
			return false;
		}

		return BackendSubsystem->Commit(
			Request,
			[Completion = MoveTemp(Completion)](
				FRPGItemBackendCommitResult Result) mutable
			{
				if (IsInGameThread())
				{
					Completion(MoveTemp(Result));
					return;
				}
				AsyncTask(
					ENamedThreads::GameThread,
					[Completion = MoveTemp(Completion),
						Result = MoveTemp(Result)]() mutable
					{
						Completion(MoveTemp(Result));
					});
			});
	}

private:
	TWeakObjectPtr<URPGItemBackendSubsystem> Backend;
};

class FCommitSink final : public IRPGItemCommitSink
{
public:
	explicit FCommitSink(URPGItemCommandComponent& InComponent)
		: Component(&InComponent)
	{
	}

	virtual bool ApplyCommit(
		const FRPGItemOwnerRef& ExpectedOwner,
		const FRPGItemBackendCommitResult& Result,
		FString& OutError) override
	{
		URPGItemCommandComponent* ItemCommands = Component.Get();
		return ItemCommands && ItemCommands->ApplyCommitAndReconcile(
			ExpectedOwner,
			Result,
			OutError);
	}

private:
	TWeakObjectPtr<URPGItemCommandComponent> Component;
};

class FFirstCommitSink final : public IRPGItemFirstCommitSink
{
public:
	explicit FFirstCommitSink(URPGItemCommandComponent& InComponent)
		: Component(&InComponent)
	{
	}

	virtual bool EnqueueFirstCommit(
		const FRPGItemBackendCommitResult& Result,
		FString& OutError) override
	{
		URPGItemCommandComponent* ItemCommands = Component.Get();
		return ItemCommands && ItemCommands->EnqueueFirstCommitEffect(
			Result,
			OutError);
	}

private:
	TWeakObjectPtr<URPGItemCommandComponent> Component;
};
}

URPGItemCommandComponent::URPGItemCommandComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URPGItemCommandComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (URPGInventoryProjectionComponent* Projection =
		GetOwner()->FindComponentByClass<
			URPGInventoryProjectionComponent>())
	{
		ProjectionChangedHandle =
			Projection->OnAuthoritativeRecordsChanged.AddUObject(
				this,
				&ThisClass::HandleAuthoritativeRecordsChanged);
	}
}

void URPGItemCommandComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (URPGInventoryProjectionComponent* Projection = GetOwner()
		? GetOwner()->FindComponentByClass<
			URPGInventoryProjectionComponent>()
		: nullptr)
	{
		Projection->OnAuthoritativeRecordsChanged.Remove(
			ProjectionChangedHandle);
	}
	Orchestrator.Reset();
	FirstCommitSinkAdapter.Reset();
	CommitSinkAdapter.Reset();
	CommitterAdapter.Reset();
	InFlightRequestIds.Reset();
	PendingFirstCommitEffects.Reset();
	Super::EndPlay(EndPlayReason);
}

void URPGItemCommandComponent::ServerEquipItem_Implementation(
	const FGuid RequestId,
	const FGuid ItemId,
	const int64 ExpectedRevision,
	const EEquipmentSlotType SlotType)
{
	if (!BeginRequest(
		RequestId,
		RPGItemCommandComponent::EquipOperation))
	{
		return;
	}
	FRPGItemOwnerRef Owner;
	if (!TryGetAuthenticatedOwner(Owner) || !EnsureOrchestrator())
	{
		HandleOutcome(
			RequestId,
			RPGItemCommandComponent::EquipOperation,
			{});
		return;
	}

	FRPGItemEquipRequest Request;
	Request.RequestId = RequestId;
	Request.Actor = Owner;
	Request.ItemId = ItemId;
	Request.ExpectedRevision = ExpectedRevision;
	Request.EquipmentContainerId = Owner.OwnerId;
	Request.SlotType = SlotType;
	const TWeakObjectPtr<URPGItemCommandComponent> WeakThis(this);
	Orchestrator->Equip(
		Request,
		[WeakThis, RequestId](FRPGItemAsyncCommandOutcome Outcome)
		{
			if (URPGItemCommandComponent* Self = WeakThis.Get())
			{
				Self->HandleOutcome(
					RequestId,
					RPGItemCommandComponent::EquipOperation,
					MoveTemp(Outcome));
			}
		});
}

void URPGItemCommandComponent::ServerUnequipItem_Implementation(
	const FGuid RequestId,
	const FGuid ItemId,
	const int64 ExpectedRevision,
	const int32 InventorySlotIndex)
{
	if (!BeginRequest(
		RequestId,
		RPGItemCommandComponent::UnequipOperation))
	{
		return;
	}
	FRPGItemOwnerRef Owner;
	if (!TryGetAuthenticatedOwner(Owner) || !EnsureOrchestrator())
	{
		HandleOutcome(
			RequestId,
			RPGItemCommandComponent::UnequipOperation,
			{});
		return;
	}

	FRPGItemUnequipRequest Request;
	Request.RequestId = RequestId;
	Request.Actor = Owner;
	Request.ItemId = ItemId;
	Request.ExpectedRevision = ExpectedRevision;
	Request.InventoryDestination.ContainerType =
		ERPGItemContainerType::Inventory;
	Request.InventoryDestination.ContainerId = Owner.OwnerId;
	Request.InventoryDestination.SlotIndex = InventorySlotIndex;
	const TWeakObjectPtr<URPGItemCommandComponent> WeakThis(this);
	Orchestrator->Unequip(
		Request,
		[WeakThis, RequestId](FRPGItemAsyncCommandOutcome Outcome)
		{
			if (URPGItemCommandComponent* Self = WeakThis.Get())
			{
				Self->HandleOutcome(
					RequestId,
					RPGItemCommandComponent::UnequipOperation,
					MoveTemp(Outcome));
			}
		});
}

void URPGItemCommandComponent::ServerConsumeItem_Implementation(
	const FGuid RequestId,
	const FGuid ItemId,
	const int64 ExpectedRevision)
{
	if (!BeginRequest(
		RequestId,
		RPGItemCommandComponent::ConsumeOperation))
	{
		return;
	}
	FRPGItemOwnerRef Owner;
	if (!TryGetAuthenticatedOwner(Owner) || !EnsureOrchestrator())
	{
		HandleOutcome(
			RequestId,
			RPGItemCommandComponent::ConsumeOperation,
			{});
		return;
	}

	FRPGItemConsumeRequest Request;
	Request.RequestId = RequestId;
	Request.Actor = Owner;
	Request.ItemId = ItemId;
	Request.ExpectedRevision = ExpectedRevision;
	const TWeakObjectPtr<URPGItemCommandComponent> WeakThis(this);
	Orchestrator->Consume(
		Request,
		[WeakThis, RequestId](FRPGItemAsyncCommandOutcome Outcome)
		{
			if (URPGItemCommandComponent* Self = WeakThis.Get())
			{
				Self->HandleOutcome(
					RequestId,
					RPGItemCommandComponent::ConsumeOperation,
					MoveTemp(Outcome));
			}
		});
}

void URPGItemCommandComponent::ClientNotifyItemCommand_Implementation(
	const FRPGItemCommandClientResult Result)
{
	OnItemCommandCompleted.Broadcast(Result);
}

void URPGItemCommandComponent::HandlePawnChanged()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	FString Error;
	if (!ReconcileEquipmentFromProjection(&Error) && !Error.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Equipment reconciliation after possession failed: %s"),
			*Error);
	}
	ProcessPendingFirstCommitEffects();
}

bool URPGItemCommandComponent::ApplyCommitAndReconcile(
	const FRPGItemOwnerRef& ExpectedOwner,
	const FRPGItemBackendCommitResult& Result,
	FString& OutError)
{
	URPGInventoryProjectionComponent* Projection = GetOwner()
		? GetOwner()->FindComponentByClass<
			URPGInventoryProjectionComponent>()
		: nullptr;
	if (!Projection ||
		!Projection->ApplyAuthoritativeCommitResult(
			ExpectedOwner,
			Result,
			&OutError))
	{
		if (!Projection && OutError.IsEmpty())
		{
			OutError = TEXT("The inventory projection is unavailable.");
		}
		return false;
	}
	FString EquipmentError;
	if (!ReconcileEquipmentFromProjection(&EquipmentError) &&
		!EquipmentError.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Post-commit equipment reconciliation is pending: %s"),
			*EquipmentError);
	}
	return true;
}

bool URPGItemCommandComponent::EnqueueFirstCommitEffect(
	const FRPGItemBackendCommitResult& Result,
	FString& OutError)
{
	if (Result.Operation != RPGItemCommandComponent::ConsumeOperation ||
		AppliedFirstCommitEffects.Contains(Result.RequestId))
	{
		return true;
	}
	if (Result.Records.IsEmpty())
	{
		OutError = TEXT("A consume receipt contains no item record.");
		return false;
	}

	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	URPGAbilitySystemComponent* AbilitySystem = Pawn
		? Pawn->FindComponentByClass<URPGAbilitySystemComponent>()
		: nullptr;
	if (!AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative())
	{
		PendingFirstCommitEffects.Add(Result.RequestId, Result);
		return true;
	}

	const UGameInstance* GameInstance = GetWorld()
		? GetWorld()->GetGameInstance()
		: nullptr;
	const UDataManager* DataManager = GameInstance
		? GameInstance->GetSubsystem<UDataManager>()
		: nullptr;
	const URPGItemDefinition* Definition = DataManager
		? DataManager->FindNativeItemDefinition(
			Result.Records[0].GetDefinitionId())
		: nullptr;
	const URPGItemConsumableDefinitionFragment* Fragment = Definition
		? Definition->FindFragmentByClass<
			URPGItemConsumableDefinitionFragment>()
		: nullptr;
	if (!Definition || !Fragment || !Fragment->GameplayEffect)
	{
		PendingFirstCommitEffects.Add(Result.RequestId, Result);
		return true;
	}

	const UGameplayEffect* Effect =
		Fragment->GameplayEffect->GetDefaultObject<UGameplayEffect>();
	AbilitySystem->ApplyGameplayEffectToSelf(
		Effect,
		1.0f,
		AbilitySystem->MakeEffectContext());
	AppliedFirstCommitEffects.Add(Result.RequestId);
	PendingFirstCommitEffects.Remove(Result.RequestId);
	return true;
}

bool URPGItemCommandComponent::EnsureOrchestrator()
{
	if (Orchestrator.IsValid())
	{
		return true;
	}
	UGameInstance* GameInstance = GetWorld()
		? GetWorld()->GetGameInstance()
		: nullptr;
	URPGItemBackendSubsystem* Backend = GameInstance
		? GameInstance->GetSubsystem<URPGItemBackendSubsystem>()
		: nullptr;
	UDataManager* DataManager = GameInstance
		? GameInstance->GetSubsystem<UDataManager>()
		: nullptr;
	URPGInventoryProjectionComponent* Projection = GetOwner()
		? GetOwner()->FindComponentByClass<
			URPGInventoryProjectionComponent>()
		: nullptr;
	const IRPGItemRecordSource* Records = Projection
		? Projection->GetAuthoritativeRecordSource()
		: nullptr;
	if (!Backend || !Backend->IsAvailable() ||
		!DataManager || !Projection || !Records)
	{
		return false;
	}

	CommitterAdapter = MakeUnique<
		RPGItemCommandComponent::FBackendCommitter>(*Backend);
	CommitSinkAdapter = MakeUnique<
		RPGItemCommandComponent::FCommitSink>(*this);
	FirstCommitSinkAdapter = MakeUnique<
		RPGItemCommandComponent::FFirstCommitSink>(*this);
	Orchestrator = MakeShared<
		FRPGItemAsyncCommandOrchestrator,
		ESPMode::ThreadSafe>(
		*Records,
		DataManager->GetItemDefinitionCatalog(),
		DataManager->GetItemActionPolicyCatalog(),
		*CommitterAdapter,
		*CommitSinkAdapter,
		*FirstCommitSinkAdapter);
	return true;
}

bool URPGItemCommandComponent::TryGetAuthenticatedOwner(
	FRPGItemOwnerRef& OutOwner) const
{
	const APlayerController* Controller =
		Cast<APlayerController>(GetOwner());
	const ARPGPlayerState* PlayerState = Controller
		? Controller->GetPlayerState<ARPGPlayerState>()
		: nullptr;
	FGuid CharacterGuid;
	if (!PlayerState ||
		!PlayerState->HasAuthenticatedCharacter() ||
		!FGuid::Parse(
			PlayerState->GetBackendCharacterId(),
			CharacterGuid))
	{
		return false;
	}
	OutOwner.Type = ERPGItemOwnerType::Character;
	OutOwner.OwnerId = CharacterGuid.ToString(
		EGuidFormats::DigitsWithHyphensLower);
	return true;
}

bool URPGItemCommandComponent::BeginRequest(
	const FGuid& RequestId,
	const FName Operation)
{
	if (!RequestId.IsValid() ||
		InFlightRequestIds.Contains(RequestId) ||
		InFlightRequestIds.Num() >= MaximumInFlightCommands)
	{
		FRPGItemCommandClientResult Result;
		Result.RequestId = RequestId;
		Result.Operation = Operation;
		Result.Result = InFlightRequestIds.Num() >=
			MaximumInFlightCommands
			? ERPGItemCommandResultCode::Busy
			: ERPGItemCommandResultCode::InvalidRequest;
		ClientNotifyItemCommand(Result);
		return false;
	}
	InFlightRequestIds.Add(RequestId);
	return true;
}

void URPGItemCommandComponent::HandleOutcome(
	const FGuid RequestId,
	const FName Operation,
	FRPGItemAsyncCommandOutcome Outcome)
{
	InFlightRequestIds.Remove(RequestId);
	FRPGItemCommandClientResult Result;
	Result.RequestId = RequestId;
	Result.Operation = Operation;
	if (Outcome.Stage == ERPGItemAsyncCommandStage::Completed)
	{
		Result.Result = Outcome.BackendResult.Status ==
			ERPGItemBackendStatus::AlreadyApplied
			? ERPGItemCommandResultCode::AlreadyApplied
			: ERPGItemCommandResultCode::Succeeded;
	}
	else if (Outcome.Stage == ERPGItemAsyncCommandStage::PlanRejected)
	{
		switch (Outcome.PlanFailureStatus)
		{
		case ERPGItemTransactionStatus::Forbidden:
			Result.Result = ERPGItemCommandResultCode::Forbidden;
			break;
		case ERPGItemTransactionStatus::RevisionConflict:
		case ERPGItemTransactionStatus::LocationOccupied:
			Result.Result = ERPGItemCommandResultCode::Conflict;
			break;
		default:
			Result.Result = ERPGItemCommandResultCode::InvalidRequest;
			break;
		}
	}
	else if (Outcome.Stage == ERPGItemAsyncCommandStage::ProtocolRejected)
	{
		Result.Result = ERPGItemCommandResultCode::ProtocolError;
	}
	else if (Outcome.Stage == ERPGItemAsyncCommandStage::BackendRejected)
	{
		Result.Result = Outcome.BackendResult.Status ==
			ERPGItemBackendStatus::RevisionConflict ||
			Outcome.BackendResult.Status ==
				ERPGItemBackendStatus::LocationConflict
			? ERPGItemCommandResultCode::Conflict
			: ERPGItemCommandResultCode::BackendRejected;
	}
	else
	{
		Result.Result = ERPGItemCommandResultCode::ServerStateError;
	}
	ClientNotifyItemCommand(Result);
}

void URPGItemCommandComponent::HandleAuthoritativeRecordsChanged()
{
	FString Error;
	if (!ReconcileEquipmentFromProjection(&Error) && !Error.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Authoritative equipment reconciliation failed: %s"),
			*Error);
	}
}

bool URPGItemCommandComponent::ReconcileEquipmentFromProjection(
	FString* OutError)
{
	FRPGItemOwnerRef Owner;
	if (!TryGetAuthenticatedOwner(Owner))
	{
		return true;
	}
	URPGInventoryProjectionComponent* Projection = GetOwner()
		? GetOwner()->FindComponentByClass<
			URPGInventoryProjectionComponent>()
		: nullptr;
	const IRPGItemRecordSource* Records = Projection
		? Projection->GetAuthoritativeRecordSource()
		: nullptr;
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	URPGAuthoritativeEquipmentComponent* Equipment = Pawn
		? Pawn->FindComponentByClass<
			URPGAuthoritativeEquipmentComponent>()
		: nullptr;
	UGameInstance* GameInstance = GetWorld()
		? GetWorld()->GetGameInstance()
		: nullptr;
	UDataManager* DataManager = GameInstance
		? GameInstance->GetSubsystem<UDataManager>()
		: nullptr;
	if (!Equipment)
	{
		return true;
	}
	if (!Records || !DataManager)
	{
		if (OutError)
		{
			*OutError = TEXT(
				"The authoritative item source or definition manager is unavailable.");
		}
		return false;
	}

	TArray<FRPGItemRecord> OwnerRecords;
	Records->FindByOwner(Owner, OwnerRecords);
	return Equipment->Reconcile(
		Owner,
		OwnerRecords,
		*DataManager,
		OutError);
}

void URPGItemCommandComponent::ProcessPendingFirstCommitEffects()
{
	TArray<FGuid> RequestIds;
	PendingFirstCommitEffects.GenerateKeyArray(RequestIds);
	for (const FGuid& RequestId : RequestIds)
	{
		const FRPGItemBackendCommitResult* Pending =
			PendingFirstCommitEffects.Find(RequestId);
		if (!Pending)
		{
			continue;
		}
		const FRPGItemBackendCommitResult PendingCopy = *Pending;
		FString Error;
		if (!EnqueueFirstCommitEffect(PendingCopy, Error) &&
			!Error.IsEmpty())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Pending item effect failed: %s"),
				*Error);
		}
	}
}
