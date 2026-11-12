#include "Component/RPGItemCommandComponent.h"

#include "Async/Async.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBaseCharacter.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Component/RPGPickupGrantComponent.h"
#include "Riding/RPGRidingConsumeLease.h"
#include "Riding/RPGRidingInteractionScope.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "Item/Backend/RPGItemBackendSubsystem.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Item/Transaction/RPGItemPickupGrantPlanner.h"
#include "Item/Transaction/RPGInventoryCommandJournal.h"
#include "Manager/DataManager.h"
#include "Player/RPGPlayerState.h"
#include "RPGGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"
#if !UE_BUILD_SHIPPING
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemCommandComponent)

struct URPGItemCommandComponent::FConsumeEffectTarget
{
    TSharedPtr<FRPGRidingConsumeLease> RidingLease;
	TWeakObjectPtr<APawn> Pawn;
	TWeakObjectPtr<URPGAbilitySystemComponent> AbilitySystem;
	FGameplayEffectSpecHandle Effect;
	TStrongObjectPtr<UGameplayEffect> EffectDefinition;
	FRPGItemBackendCommitResult Expected;
	uint32 RecoveryRevision = 0;
	bool bApplying = false;
};

namespace RPGItemCommandComponent
{
const FName MoveOperation(TEXT("Item.Move"));
const FName SwapOperation(TEXT("Item.Swap"));
const FName StackTransferOperation(TEXT("Item.StackTransfer"));
const FName SplitOperation(TEXT("Item.Split"));
const FName EquipOperation(TEXT("Item.Equip"));
const FName UnequipOperation(TEXT("Item.Unequip"));
const FName ConsumeOperation(TEXT("Item.Consume"));

bool IsLivingConsumeAvatar(const APawn* Pawn, const URPGAbilitySystemComponent* AbilitySystem)
{
	const auto* Attributes = AbilitySystem ? AbilitySystem->GetSet<URPGAttributeSet>() : nullptr;
	return IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && Attributes &&
		FMath::IsFinite(Attributes->GetCurrentHealth()) && Attributes->GetCurrentHealth() > 0.f &&
		!AbilitySystem->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death);
}

uint32 ConsumeRecoveryRevision(const APawn* Pawn)
{
	const auto* Character = Cast<ARPGBaseCharacter>(Pawn);
	return Character ? Character->GetCombatPresentationRevision() : 0;
}

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

/** One shared pre-HTTP guard for every inventory mutation, including frozen retries. */
class FInventorySlotCommitter final : public IRPGItemAsyncCommitter
{
public:
	FInventorySlotCommitter(URPGItemCommandComponent& InCommands, TUniquePtr<IRPGItemAsyncCommitter> InInner)
		: Commands(&InCommands), Inner(MoveTemp(InInner)) {}
	virtual bool Commit(const FRPGItemRepositoryCommitRequest& Request, FRPGItemBackendCommitCompletion Completion) override
	{
		auto* Current = Commands.Get();
		if (!Current || !Current->TrackInventorySlotRequest(Request))
		{
			FRPGItemBackendCommitResult Result;
			// A local refusal is not an upstream LocationConflict receipt or a successful commit.
			Result.Status = ERPGItemBackendStatus::ValidationFailed;
			Result.RequestId = Request.RequestId; Result.Operation = Request.Operation;
			Result.CommandFingerprint = Request.CommandFingerprint; Result.Actor = Request.Actor;
			Result.Error = TEXT("An item destination is reserved or the original local request does not match. No HTTP request was sent.");
			Completion(MoveTemp(Result));
			return false;
		}
		return Inner->Commit(Request, MoveTemp(Completion));
	}
private:
	TWeakObjectPtr<URPGItemCommandComponent> Commands;
	TUniquePtr<IRPGItemAsyncCommitter> Inner;
};

class FPickupBackendCommitter final : public IRPGItemAsyncCommitter
{
public:
	FPickupBackendCommitter(URPGItemBackendSubsystem& InBackend, URPGItemCommandComponent& InCommands,
		const FGuid& InDrop) : Backend(&InBackend), Commands(&InCommands), Drop(InDrop) {}
	virtual bool Commit(const FRPGItemRepositoryCommitRequest& Request, FRPGItemBackendCommitCompletion Completion) override
	{
		if (auto* Current = Backend.Get())
		{
			const auto WeakCommands = Commands;
			return Current->CommitPickup(Drop, Request, MoveTemp(Completion), [WeakCommands, Owner = Request.Actor]()
			{
				const auto* Claimant = WeakCommands.Get();
				return Claimant && Claimant->IsAuthenticatedItemOwner(Owner);
			});
		}
		FRPGItemBackendCommitResult Result;
		Result.Status = ERPGItemBackendStatus::Unavailable; Result.RequestId = Request.RequestId;
		Result.Operation = Request.Operation; Result.CommandFingerprint = Request.CommandFingerprint; Result.Actor = Request.Actor;
		if (Completion) Completion(MoveTemp(Result));
		return false;
	}
private:
	TWeakObjectPtr<URPGItemBackendSubsystem> Backend;
	TWeakObjectPtr<URPGItemCommandComponent> Commands;
	FGuid Drop;
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
	virtual bool SupportsScopedConsumeRecovery() const override { return true; }
	virtual bool PrepareConsume(const FRPGItemRepositoryCommitRequest& Request, FString& OutError) override
	{
		auto* Commands = Component.Get();
		return Commands && Commands->PrepareConsumeEffect(Request, OutError);
	}
	virtual void DiscardConsume(const FGuid& RequestId) override
	{
		if (auto* Commands = Component.Get()) Commands->DiscardConsumeEffect(RequestId);
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

void URPGItemCommandComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ThisClass, InventoryRecovery, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, bAuthenticatedIdentityRevoked, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, bInventoryRecoveryStorageBlocked, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, PendingPickupGrantCount, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, PendingPickupInventorySlots, COND_OwnerOnly);
}

void URPGItemCommandComponent::OnRep_InventoryRecovery()
{
	OnInventoryRecoveryChanged.Broadcast();
}

void URPGItemCommandComponent::SetInventoryRecovery(
	const FGuid RequestId, const FName Operation, const bool bUncertain, const bool bRetrying)
{
	if (bUncertain)
	{
		auto* State = InventoryRecovery.FindByPredicate([RequestId](const auto& Entry) { return Entry.RequestId == RequestId; });
		if (!State) State = &InventoryRecovery.AddDefaulted_GetRef();
		State->RequestId = RequestId; State->Operation = Operation; State->bRetrying = bRetrying;
	}
	else InventoryRecovery.RemoveAll([RequestId](const auto& Entry) { return Entry.RequestId == RequestId; });
	OnRep_InventoryRecovery();
	if (GetOwner()) GetOwner()->ForceNetUpdate();
}

void URPGItemCommandComponent::ServerRetryUncertainInventoryCommand_Implementation(const FGuid RequestId)
{
	FRPGItemOwnerRef Owner;
	FName Operation;
	if (!TryGetAuthenticatedOwner(Owner) || !IsAuthenticatedItemOwner(Owner) || !Orchestrator ||
		!Orchestrator->FindUncertainInventoryCommand(RequestId, Owner, Operation)) return;
	if (FPlatformTime::Seconds() < NextInventoryRecoveryRetryTime)
	{
		// Acknowledge only an already-known owner request. Silence would strand the UI's local in-flight state.
		FRPGItemCommandClientResult Result;
		Result.RequestId = RequestId; Result.Operation = Operation; Result.Result = ERPGItemCommandResultCode::Uncertain;
		ClientNotifyItemCommand(Result);
		return;
	}
	if (!BeginRequest(RequestId, Operation, true)) return;
	NextInventoryRecoveryRetryTime = FPlatformTime::Seconds() + 3.0;
	SetInventoryRecovery(RequestId, Operation, true, true);
	const TWeakObjectPtr<URPGItemCommandComponent> WeakThis(this);
	Orchestrator->RetryInventoryCommand(RequestId, Owner, [WeakThis, RequestId, Operation](FRPGItemAsyncCommandOutcome Outcome)
	{
		if (auto* Self = WeakThis.Get()) Self->HandleOutcome(RequestId, Operation, MoveTemp(Outcome));
	});
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
	HandleAuthoritativeRecordsChanged();
}

void URPGItemCommandComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	if (URPGInventoryProjectionComponent* Projection = GetOwner()
		? GetOwner()->FindComponentByClass<
			URPGInventoryProjectionComponent>()
		: nullptr)
	{
		Projection->OnAuthoritativeRecordsChanged.Remove(
			ProjectionChangedHandle);
	}
	Orchestrator.Reset();
	InventoryJournal.Reset(); // Disk evidence intentionally survives normal disconnect/world teardown.
	FirstCommitSinkAdapter.Reset();
	CommitSinkAdapter.Reset();
	CommitterAdapter.Reset();
	InFlightRequestIds.Reset();
	PendingPickupLocations.Reset();
	PendingInventorySlotRequests.Reset();
	PendingPickupGrantCount = 0;
	PendingPickupInventorySlots.Reset();
	PendingFirstCommitEffects.Reset();
	ConsumeEffectTargets.Reset();
	InventoryRecovery.Reset();
	Super::EndPlay(EndPlayReason);
}

bool URPGItemCommandComponent::IsAuthenticatedItemOwner(const FRPGItemOwnerRef& Owner) const
{
	FRPGItemOwnerRef CurrentOwner;
	const auto* Controller = Cast<APlayerController>(GetOwner());
	const auto* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
	return !bEndingPlay && GetOwner() && GetOwner()->HasAuthority() &&
		TryGetAuthenticatedOwner(CurrentOwner) && CurrentOwner == Owner && State &&
		(!InventoryJournal || InventoryJournal->MatchesIdentity(State->GetBackendDungeonSessionId(), CurrentOwner.OwnerId, State->GetAuthenticatedSteamId()));
}

void URPGItemCommandComponent::InvalidateForRevokedIdentity(const ARPGPlayerState& RevokedState)
{
	const auto* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !RevokedState.IsAuthenticatedIdentityRevoked()
		|| Controller->GetPlayerState<ARPGPlayerState>() != &RevokedState || bAuthenticatedIdentityRevoked) return;
#if !UE_BUILD_SHIPPING
	// Opt-in observation only: the production identity/receipt path still owns revocation.
	const bool bObserveRecovery = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRevocationE2E"));
	const TArray<FRPGInventoryCommandRecovery> PreviousRecovery = bObserveRecovery ? InventoryRecovery : TArray<FRPGInventoryCommandRecovery>();
	const bool bHadUncertain = Orchestrator && Orchestrator->HasUncertainInventoryCommands();
#endif
	bAuthenticatedIdentityRevoked = true;
	if (InventoryJournal && !InventoryJournal->Quarantine()) bInventoryRecoveryStorageBlocked = true;
	InventoryRecovery.Reset();
	Orchestrator.Reset(); // Deferred callbacks hold only a weak orchestrator.
	// Keep adapters alive until EndPlay: a re-entrant receipt may still own a
	// pinned orchestrator whose sinks refer to them. Every sink checks the barrier.
	InFlightRequestIds.Reset();
	PendingPickupLocations.Reset();
	PendingInventorySlotRequests.Reset();
	PendingPickupGrantCount = 0; // Presentation only; the claimed pickup/journal are not released.
	PendingPickupInventorySlots.Reset();
	PendingFirstCommitEffects.Reset();
	ConsumeEffectTargets.Reset();
	// Publish only after every local barrier is raised, including re-entrant BP observers.
	OnRep_InventoryRecovery();
	GetOwner()->ForceNetUpdate();
#if !UE_BUILD_SHIPPING
	if (bObserveRecovery)
		for (const auto& Entry : PreviousRecovery)
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_REVOKED Request=%s WasUncertain=%d Pending=%d Recovery=%d Revoked=%d NoRollback=1"),
				*Entry.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower), bHadUncertain ? 1 : 0,
				HasInFlightCommands() ? 1 : 0, InventoryRecovery.Num(), IsItemCommandAuthorityRevoked() ? 1 : 0);
#endif
}

TUniquePtr<IRPGItemAsyncCommitter> URPGItemCommandComponent::MakeSlotAwareCommitter(TUniquePtr<IRPGItemAsyncCommitter> Inner)
{
	check(Inner);
	return MakeUnique<RPGItemCommandComponent::FInventorySlotCommitter>(*this, MoveTemp(Inner));
}

bool URPGItemCommandComponent::IsInventoryLocationReserved(const FRPGItemLocation& Location) const
{
	for (const auto& Pending : PendingPickupLocations)
		if (Pending.Value == Location) return true;
	for (const auto& Pending : PendingInventorySlotRequests)
		for (const auto& Mutation : Pending.Value.Mutations)
			if (Mutation.NewRecord.IsActive() && Mutation.NewRecord.GetLocation() == Location) return true;
	return false;
}

bool URPGItemCommandComponent::TrackInventorySlotRequest(const FRPGItemRepositoryCommitRequest& Request)
{
	if (!Request.RequestId.IsValid() || !IsAuthenticatedItemOwner(Request.Actor) || Request.Mutations.IsEmpty()) return false;
	if (const auto* Existing = PendingInventorySlotRequests.Find(Request.RequestId))
	{
		FString Before, Retry;
		return FRPGItemBackendJsonCodec::SerializeCommitRequest(*Existing, Before) &&
			FRPGItemBackendJsonCodec::SerializeCommitRequest(Request, Retry) && Before == Retry;
	}
	const auto* Projection = GetOwner()->FindComponentByClass<URPGInventoryProjectionComponent>();
	const auto* Records = Projection ? Projection->GetAuthoritativeRecordSource() : nullptr;
	for (const auto& Mutation : Request.Mutations)
	{
		const auto& Record = Mutation.NewRecord;
		if (Record.GetOwner() != Request.Actor) return false;
		const auto& Location = Record.GetLocation();
		if (Record.IsActive() && Location.ContainerType == ERPGItemContainerType::Inventory &&
			(Location.ContainerId != Request.Actor.OwnerId || Location.SlotIndex < 0 ||
			 Location.SlotIndex >= RPGItemInventoryPolicy::SlotCapacity || IsInventoryLocationReserved(Location))) return false;
		FRPGItemRecord Source;
		if (Records && Records->Find(Record.GetItemId(), Source))
			for (const auto& Pickup : PendingPickupLocations)
				if (Source.GetOwner() == Request.Actor && Source.GetLocation() == Pickup.Value) return false;
	}
	PendingInventorySlotRequests.Add(Request.RequestId, Request);
	return true;
}

bool URPGItemCommandComponent::TrackPickupGrant(const FRPGItemRepositoryCommitRequest& Request)
{
	if (!Request.RequestId.IsValid() || Request.Operation != TEXT("Item.Pickup") ||
		Request.Mutations.Num() != 1 || Request.Mutations[0].ExpectedRevision != 0 ||
		PendingPickupLocations.Contains(Request.RequestId) || !IsAuthenticatedItemOwner(Request.Actor)) return false;
	const auto& Record = Request.Mutations[0].NewRecord;
	const auto& Location = Record.GetLocation();
	if (Record.GetOwner() != Request.Actor || Location.ContainerType != ERPGItemContainerType::Inventory ||
		Location.ContainerId != Request.Actor.OwnerId || Location.SlotIndex < 0 ||
		Location.SlotIndex >= RPGItemInventoryPolicy::SlotCapacity) return false;
	if (IsInventoryLocationReserved(Location)) return false;
	PendingPickupLocations.Add(Request.RequestId, Location);
	RefreshPickupGrantCount();
	return true;
}

bool URPGItemCommandComponent::TryPlanPickupGrant(const FRPGItemPickupGrantRequest& Request,
	const URPGItemDefinition& Definition, const IRPGItemRecordSource& Records,
	const IRPGItemDefinitionCatalog& Definitions, FRPGItemRepositoryCommitRequest& OutCommit) const
{
	OutCommit = {};
	if (!IsAuthenticatedItemOwner(Request.Owner)) return false;
	for (const auto& Pending : PendingInventorySlotRequests) if (Pending.Value.Operation == TEXT("Item.Sort")) return false;
	auto ScopedRequest = Request;
	ScopedRequest.ReservedInventorySlots.Reset(); // Only this controller's server-owned reservations count.
	for (const auto& Pending : PendingPickupLocations)
		if (Pending.Value.ContainerId == Request.Owner.OwnerId)
			ScopedRequest.ReservedInventorySlots.Add(Pending.Value.SlotIndex);
	for (const auto& Pending : PendingInventorySlotRequests)
		if (Pending.Value.Actor == Request.Owner)
			for (const auto& Mutation : Pending.Value.Mutations)
				if (Mutation.NewRecord.IsActive() && Mutation.NewRecord.GetLocation().ContainerType == ERPGItemContainerType::Inventory)
					ScopedRequest.ReservedInventorySlots.Add(Mutation.NewRecord.GetLocation().SlotIndex);
	return FRPGItemPickupGrantPlanner::TryPlan(ScopedRequest, Definition, Records, Definitions, OutCommit);
}

void URPGItemCommandComponent::RefreshPickupGrantCount()
{
	FRPGItemOwnerRef Owner;
	const bool bCurrent = TryGetAuthenticatedOwner(Owner) && IsAuthenticatedItemOwner(Owner);
	int32 Count = 0;
	TArray<int32> Slots;
	for (const auto& Pending : PendingPickupLocations)
		if (bCurrent && Pending.Value.ContainerId == Owner.OwnerId) { ++Count; Slots.AddUnique(Pending.Value.SlotIndex); }
	Slots.Sort();
	if (PendingPickupGrantCount == Count && PendingPickupInventorySlots == Slots) return;
	PendingPickupGrantCount = Count;
	PendingPickupInventorySlots = MoveTemp(Slots);
	OnRep_InventoryRecovery();
	if (GetOwner()) GetOwner()->ForceNetUpdate();
}

void URPGItemCommandComponent::CompletePickupGrant(
	const FRPGItemRepositoryCommitRequest& Request,
	const ERPGItemCommandResultCode ResultCode)
{
	InFlightRequestIds.Remove(Request.RequestId);
	// Unknown teardown is not a rejection/receipt. Reusing that slot could strand a later pickup.
	if (ResultCode == ERPGItemCommandResultCode::Succeeded || ResultCode == ERPGItemCommandResultCode::AlreadyApplied ||
		ResultCode == ERPGItemCommandResultCode::Conflict || ResultCode == ERPGItemCommandResultCode::BackendRejected)
		PendingPickupLocations.Remove(Request.RequestId);
	RefreshPickupGrantCount();
	if (!IsAuthenticatedItemOwner(Request.Actor)) return;
	FRPGItemCommandClientResult Result;
	Result.RequestId = Request.RequestId;
	Result.Operation = TEXT("Item.Pickup");
	Result.Result = ResultCode;
	ClientNotifyItemCommand(Result);
}

void URPGItemCommandComponent::ServerPickupItem_Implementation(ARPGPickUpBase* Pickup)
{
	const FGuid RequestId = FGuid::NewGuid();
	const FName Operation(TEXT("Item.Pickup"));
	if (!BeginRequest(RequestId, Operation)) return;
	auto Reject = [this, RequestId, Operation](const ERPGItemCommandResultCode Code)
	{
		InFlightRequestIds.Remove(RequestId);
		FRPGItemCommandClientResult Result;
		Result.RequestId = RequestId;
		Result.Operation = Operation;
		Result.Result = Code;
		ClientNotifyItemCommand(Result);
	};
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!IsValid(Pickup) || !Pickup->HasAuthority() || Pickup->GetWorld() != GetWorld() ||
		!Pickup->IsAvailableForPickup() || Pickup->IsHidden() || !IsValid(Pawn) ||
		!FMath::IsFinite(MaximumPickupDistance) || MaximumPickupDistance <= 0.f ||
		!(FVector::DistSquared(Pawn->GetActorLocation(), Pickup->GetActorLocation()) <=
			FMath::Square(MaximumPickupDistance)))
	{
		Reject(ERPGItemCommandResultCode::InvalidRequest);
		return;
	}
	FRPGItemOwnerRef Owner;
	URPGInventoryProjectionComponent* Projection =
		GetOwner()->FindComponentByClass<URPGInventoryProjectionComponent>();
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UDataManager* DataManager = GameInstance ? GameInstance->GetSubsystem<UDataManager>() : nullptr;
	URPGItemBackendSubsystem* Backend = GameInstance
		? GameInstance->GetSubsystem<URPGItemBackendSubsystem>() : nullptr;
	const IRPGItemRecordSource* Records = Projection ? Projection->GetAuthoritativeRecordSource() : nullptr;
	if (!TryGetAuthenticatedOwner(Owner) || !Records || !DataManager || !Backend ||
		!Backend->IsAvailable() || Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready)
	{
		Reject(ERPGItemCommandResultCode::ServerStateError);
		return;
	}
	const FItemManifest Manifest = Pickup->GetItemManifest();
	const URPGItemDefinition* Definition = DataManager->FindNativeItemDefinition(
		URPGItemDefinition::MakePrimaryAssetIdForTag(Manifest.GetItemTag()));
	URPGPickupGrantComponent* Grant = Pickup->FindComponentByClass<URPGPickupGrantComponent>();
	if (!Definition || !Grant || !Pickup->GetPickupDropId().IsValid() || Definition->ItemCategory != Manifest.GetItemCategory())
	{
		Reject(ERPGItemCommandResultCode::InvalidRequest);
		return;
	}
	const FStackableFragment* Stack = Manifest.GetFragmentOfType<FStackableFragment>();
	FRPGItemPickupGrantRequest Request;
	Request.RequestId = RequestId;
	Request.ItemId = FGuid::NewGuid();
	Request.Owner = Owner;
	Request.Quantity = Stack ? Stack->GetQuantity() : 1;
	Request.GenerationSeed = static_cast<int32>(GetTypeHash(Request.ItemId));
	Request.CreationSource = TEXT("WorldPickup");
	FRPGItemRepositoryCommitRequest Commit;
	if (!TryPlanPickupGrant(Request, *Definition, *Records,
		DataManager->GetItemDefinitionCatalog(), Commit))
	{
		Reject(ERPGItemCommandResultCode::InvalidRequest);
		return;
	}
	const auto* State = Controller->GetPlayerState<ARPGPlayerState>();
	if (!IsValid(State)) { Reject(ERPGItemCommandResultCode::ServerStateError); return; }
	const int64 Generation = State->GetBackendConnectionGeneration();
	const FString Steam = State->GetAuthenticatedSteamId(), Session = State->GetBackendDungeonSessionId();
	const FGuid DropId = Pickup->GetPickupDropId();
	const int32 DefinitionVersion = Definition->GetDefinitionVersion();
	const auto Validate = [&]()
	{
		FRPGItemOwnerRef CurrentOwner;
		if (!IsValid(this) || !IsValid(Controller) || Controller->GetPawn() != Pawn
			|| Controller->GetPlayerState<ARPGPlayerState>() != State || !IsValid(State)
			|| State->GetBackendConnectionGeneration() != Generation || State->GetAuthenticatedSteamId() != Steam
			|| State->GetBackendDungeonSessionId() != Session || !TryGetAuthenticatedOwner(CurrentOwner)
			|| !(CurrentOwner == Owner) || !IsAuthenticatedItemOwner(Owner)
			|| !IsValid(Pickup) || !Pickup->HasAuthority() || Pickup->GetWorld() != GetWorld()
			|| Pickup->GetPickupDropId() != DropId || !Pickup->IsAvailableForPickup() || Pickup->IsHidden()
			|| !IsValid(Grant) || Pickup->FindComponentByClass<URPGPickupGrantComponent>() != Grant || Grant->IsPending()
			|| !IsValid(Pawn) || !RPGItemCommandComponent::IsLivingConsumeAvatar(Pawn, Pawn->FindComponentByClass<URPGAbilitySystemComponent>())
			|| !FMath::IsFinite(MaximumPickupDistance) || MaximumPickupDistance <= 0.f
			|| !(FVector::DistSquared(Pawn->GetActorLocation(), Pickup->GetActorLocation()) <= FMath::Square(MaximumPickupDistance))
			|| !IsValid(Projection) || Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready
			|| !Projection->GetAuthoritativeRecordSource() || !IsValid(DataManager) || !IsValid(Backend) || !Backend->IsAvailable()
			|| !IsValid(Definition) || Definition->GetDefinitionVersion() != DefinitionVersion
			|| DataManager->FindNativeItemDefinition(Definition->GetPrimaryAssetId()) != Definition) return false;
		const auto& CurrentManifest = Pickup->GetItemManifest();
		const auto* CurrentStack = CurrentManifest.GetFragmentOfType<FStackableFragment>();
		return CurrentManifest.GetItemTag() == Manifest.GetItemTag() && CurrentManifest.GetItemCategory() == Manifest.GetItemCategory()
			&& (CurrentStack ? CurrentStack->GetQuantity() : 1) == Request.Quantity;
	};
	ERPGItemCommandResultCode Failure = ERPGItemCommandResultCode::InvalidRequest;
	if (!FRPGRidingInteractionScope::TryExecute(*Pawn, *Pickup, Validate, [&]()
		{
			// Dismount listeners can change the inventory. Re-plan against the current
			// records, retaining this exact pickup generation and request identity.
			FRPGItemRepositoryCommitRequest CurrentCommit;
			if (!TryPlanPickupGrant(Request, *Definition, *Projection->GetAuthoritativeRecordSource(),
				DataManager->GetItemDefinitionCatalog(), CurrentCommit)) return false;
			Failure = ERPGItemCommandResultCode::Busy;
			return Grant->BeginGrant(CurrentCommit, *this,
				MakeShared<RPGItemCommandComponent::FPickupBackendCommitter>(*Backend, *this, DropId), true);
		}))
	{
		Reject(Failure);
	}
}

void URPGItemCommandComponent::ServerMoveItem_Implementation(
	const FGuid RequestId,
	const FGuid ItemId,
	const int64 ExpectedRevision,
	const int32 InventorySlotIndex)
{
	if (!BeginRequest(RequestId, RPGItemCommandComponent::MoveOperation))
	{
		return;
	}
	FRPGItemOwnerRef Owner;
	if (!TryGetAuthenticatedOwner(Owner) || !EnsureOrchestrator())
	{
		HandleOutcome(RequestId, RPGItemCommandComponent::MoveOperation, {});
		return;
	}

	FRPGItemMoveRequest Request;
	Request.RequestId = RequestId;
	Request.Actor = Owner;
	Request.ItemId = ItemId;
	Request.ExpectedRevision = ExpectedRevision;
	Request.Destination.ContainerType = ERPGItemContainerType::Inventory;
	Request.Destination.ContainerId = Owner.OwnerId;
	Request.Destination.SlotIndex = InventorySlotIndex;
	const TWeakObjectPtr<URPGItemCommandComponent> WeakThis(this);
	Orchestrator->Move(Request,
		[WeakThis, RequestId](FRPGItemAsyncCommandOutcome Outcome)
		{
			if (URPGItemCommandComponent* Self = WeakThis.Get())
			{
				Self->HandleOutcome(RequestId, RPGItemCommandComponent::MoveOperation,
					MoveTemp(Outcome));
			}
		});
}

void URPGItemCommandComponent::ServerSwapInventoryItems_Implementation(
	const FGuid RequestId, const FGuid SourceItemId, const int64 ExpectedSourceRevision,
	const FGuid DestinationItemId, const int64 ExpectedDestinationRevision)
{
	if (!BeginRequest(RequestId, RPGItemCommandComponent::SwapOperation))
	{
		return;
	}
	FRPGItemOwnerRef Owner;
	if (!TryGetAuthenticatedOwner(Owner) || !EnsureOrchestrator())
	{
		HandleOutcome(RequestId, RPGItemCommandComponent::SwapOperation, {});
		return;
	}

	FRPGItemSwapRequest Request;
	Request.RequestId = RequestId;
	Request.Actor = Owner;
	Request.SourceItemId = SourceItemId;
	Request.ExpectedSourceRevision = ExpectedSourceRevision;
	Request.DestinationItemId = DestinationItemId;
	Request.ExpectedDestinationRevision = ExpectedDestinationRevision;
	Request.InventoryContainerId = Owner.OwnerId;
	const TWeakObjectPtr<URPGItemCommandComponent> WeakThis(this);
	Orchestrator->Swap(Request,
		[WeakThis, RequestId](FRPGItemAsyncCommandOutcome Outcome)
		{
			if (URPGItemCommandComponent* Self = WeakThis.Get())
			{
				Self->HandleOutcome(RequestId, RPGItemCommandComponent::SwapOperation,
					MoveTemp(Outcome));
			}
		});
}

void URPGItemCommandComponent::ServerSplitInventoryItem_Implementation(
	const FGuid RequestId, const FGuid ItemId, const int64 ExpectedRevision,
	const int32 InventorySlotIndex, const int32 Quantity)
{
	if (!BeginRequest(RequestId, RPGItemCommandComponent::SplitOperation)) return;
	FRPGItemOwnerRef Owner;
	if (!TryGetAuthenticatedOwner(Owner) || !EnsureOrchestrator())
	{
		HandleOutcome(RequestId, RPGItemCommandComponent::SplitOperation, {});
		return;
	}
	FRPGItemSplitRequest Request;
	Request.RequestId = RequestId;
	Request.Actor = Owner;
	Request.ItemId = ItemId;
	Request.ExpectedRevision = ExpectedRevision;
	Request.Destination.ContainerType = ERPGItemContainerType::Inventory;
	Request.Destination.ContainerId = Owner.OwnerId;
	Request.Destination.SlotIndex = InventorySlotIndex;
	Request.Quantity = Quantity;
	const TWeakObjectPtr<URPGItemCommandComponent> WeakThis(this);
	Orchestrator->Split(Request, [WeakThis, RequestId](FRPGItemAsyncCommandOutcome Outcome)
	{
		if (auto* Self = WeakThis.Get()) Self->HandleOutcome(RequestId, RPGItemCommandComponent::SplitOperation, MoveTemp(Outcome));
	});
}

void URPGItemCommandComponent::ServerMergeInventoryItems_Implementation(
	const FGuid RequestId, const FGuid SourceItemId, const int64 ExpectedSourceRevision,
	const FGuid DestinationItemId, const int64 ExpectedDestinationRevision)
{
	if (!BeginRequest(RequestId, RPGItemCommandComponent::StackTransferOperation))
	{
		return;
	}
	FRPGItemOwnerRef Owner;
	if (!TryGetAuthenticatedOwner(Owner) || !EnsureOrchestrator())
	{
		HandleOutcome(RequestId, RPGItemCommandComponent::StackTransferOperation, {});
		return;
	}
	FRPGItemStackTransferRequest Request;
	Request.RequestId = RequestId;
	Request.Actor = Owner;
	Request.SourceItemId = SourceItemId;
	Request.ExpectedSourceRevision = ExpectedSourceRevision;
	Request.DestinationItemId = DestinationItemId;
	Request.ExpectedDestinationRevision = ExpectedDestinationRevision;
	// Fixed merge-all intent makes an identical RPC replay retain its fingerprint
	// even after the source has become a consumed tombstone. No client quantity is trusted.
	Request.RequestedQuantity = MAX_int32;
	const TWeakObjectPtr<URPGItemCommandComponent> WeakThis(this);
	Orchestrator->TransferStack(Request,
		[WeakThis, RequestId](FRPGItemAsyncCommandOutcome Outcome)
		{
			if (URPGItemCommandComponent* Self = WeakThis.Get())
			{
				Self->HandleOutcome(RequestId, RPGItemCommandComponent::StackTransferOperation, MoveTemp(Outcome));
			}
		});
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

void URPGItemCommandComponent::ServerClaimMailItem_Implementation(
	const FGuid RequestId, const FGuid ItemId,
	const int64 ExpectedRevision, const int32 InventorySlotIndex)
{
	const FName Operation(TEXT("Item.ClaimMail"));
	if (!BeginRequest(RequestId, Operation))
	{
		return;
	}
	FRPGItemOwnerRef Owner;
	if (!TryGetAuthenticatedOwner(Owner) || !EnsureOrchestrator())
	{
		HandleOutcome(RequestId, Operation, {});
		return;
	}
	FRPGItemClaimMailRequest Request;
	Request.RequestId = RequestId;
	Request.Actor = Owner;
	Request.ItemId = ItemId;
	Request.ExpectedRevision = ExpectedRevision;
	Request.InventoryDestination.ContainerType = ERPGItemContainerType::Inventory;
	Request.InventoryDestination.ContainerId = Owner.OwnerId;
	Request.InventoryDestination.SlotIndex = InventorySlotIndex;
	const TWeakObjectPtr<URPGItemCommandComponent> WeakThis(this);
	Orchestrator->ClaimMail(Request,
		[WeakThis, RequestId, Operation](FRPGItemAsyncCommandOutcome Outcome)
		{
			if (URPGItemCommandComponent* Self = WeakThis.Get())
			{
				Self->HandleOutcome(RequestId, Operation, MoveTemp(Outcome));
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
	// An old consume must never migrate to a newly possessed avatar. Recovery is
	// explicit and checks the original prepared Pawn/ASC; possession never replays it.
}

bool URPGItemCommandComponent::ApplyCommitAndReconcile(
	const FRPGItemOwnerRef& ExpectedOwner,
	const FRPGItemBackendCommitResult& Result,
	FString& OutError)
{
	if (!IsAuthenticatedItemOwner(ExpectedOwner))
	{
		OutError = TEXT("Item commit application requires the current authenticated owner.");
		return false;
	}
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

bool URPGItemCommandComponent::PrepareConsumeEffect(const FRPGItemRepositoryCommitRequest& Request, FString& OutError)
{
	const auto* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const auto* Data = Instance ? Instance->GetSubsystem<UDataManager>() : nullptr;
	const auto* Definition = Data && Request.Mutations.Num() == 1
		? Data->FindNativeItemDefinition(Request.Mutations[0].NewRecord.GetDefinitionId()) : nullptr;
	return PrepareConsumeEffectFromDefinition(Request, Definition, OutError);
}

bool URPGItemCommandComponent::PrepareConsumeEffectFromDefinition(const FRPGItemRepositoryCommitRequest& Request,
	const URPGItemDefinition* Definition, FString& OutError)
{
	if (bAuthenticatedIdentityRevoked || !IsAuthenticatedItemOwner(Request.Actor) ||
		Request.Operation != RPGItemCommandComponent::ConsumeOperation || !Request.RequestId.IsValid() ||
		Request.CommandFingerprint.IsEmpty() || Request.Mutations.Num() != 1 || Request.AffectedQuantity <= 0)
	{
		OutError = TEXT("Consume preparation requires the original authenticated server plan.");
		return false;
	}
	if (AppliedFirstCommitEffects.Contains(Request.RequestId)) return true;
	if (ConsumeEffectTargets.Contains(Request.RequestId))
	{
		OutError = TEXT("The original consume effect target cannot be replaced.");
		return false;
	}
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	auto* AbilitySystem = Pawn ? Pawn->FindComponentByClass<URPGAbilitySystemComponent>() : nullptr;
	if (!IsValid(Pawn) || !AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative() || AbilitySystem->GetAvatarActor() != Pawn)
	{
		OutError = TEXT("The original consume avatar and authoritative ability system must be ready before commit.");
		return false;
	}
	if (!RPGItemCommandComponent::IsLivingConsumeAvatar(Pawn, AbilitySystem))
	{
		OutError = TEXT("Ordinary consumables require a living avatar before commit.");
		return false;
	}
	const auto& Mutation = Request.Mutations[0];
	const auto& Record = Mutation.NewRecord;
	const auto* Fragment = Definition ? Definition->FindFragmentByClass<URPGItemConsumableDefinitionFragment>() : nullptr;
	if (!Definition || Definition->GetPrimaryAssetId() != Record.GetDefinitionId() || Definition->GetDefinitionVersion() != Record.GetDefinitionVersion() || !Fragment || !Fragment->GameplayEffect ||
		Fragment->QuantityPerUse != Request.AffectedQuantity || Mutation.ExpectedRevision <= 0 || Mutation.ExpectedRevision == MAX_int64 ||
		Record.GetRevision() != Mutation.ExpectedRevision || Record.GetOwner() != Request.Actor || !Record.IsStructurallyValid())
	{
		OutError = TEXT("The original consume definition, quantity or revision is not ready.");
		return false;
	}
    const auto RidingLease = FRPGRidingConsumeLease::TryAcquire(*Pawn);
    if (!RidingLease)
    {
        OutError = TEXT("Consumables require an on-foot avatar and a free preparation reservation.");
        return false;
    }
    FGameplayEffectSpecHandle EffectSpec = AbilitySystem->MakeOutgoingSpec(
		Fragment->GameplayEffect, 1.0f, AbilitySystem->MakeEffectContext());
	if (!EffectSpec.IsValid())
	{
		OutError = TEXT("The consumable Gameplay Effect spec could not be created.");
		return false;
	}
	for (const auto& Pair : Fragment->SetByCallerMagnitudes)
	{
		if (!Pair.Key.IsValid() || !FMath::IsFinite(Pair.Value))
		{
			OutError = TEXT("The consumable Gameplay Effect has invalid authored magnitudes.");
			return false;
		}
		EffectSpec.Data->SetSetByCallerMagnitude(Pair.Key, Pair.Value);
	}
	auto Target = MakeShared<FConsumeEffectTarget>();
    Target->RidingLease = RidingLease;
	Target->Pawn = Pawn; Target->AbilitySystem = AbilitySystem; Target->Effect = MoveTemp(EffectSpec);
	Target->RecoveryRevision = RPGItemCommandComponent::ConsumeRecoveryRevision(Pawn);
	Target->EffectDefinition.Reset(const_cast<UGameplayEffect*>(Target->Effect.Data->Def.Get()));
	Target->Expected.RequestId = Request.RequestId; Target->Expected.Operation = Request.Operation;
	Target->Expected.Actor = Request.Actor; Target->Expected.CommandFingerprint = Request.CommandFingerprint;
	Target->Expected.AffectedQuantity = Request.AffectedQuantity;
	Target->Expected.Records = {Record.CopyWithRevision(Mutation.ExpectedRevision + 1)};
	ConsumeEffectTargets.Add(Request.RequestId, MoveTemp(Target));
	return true;
}

void URPGItemCommandComponent::DiscardConsumeEffect(const FGuid& RequestId)
{
	ConsumeEffectTargets.Remove(RequestId);
	PendingFirstCommitEffects.Remove(RequestId);
}

bool URPGItemCommandComponent::EnqueueFirstCommitEffect(const FRPGItemBackendCommitResult& Result, FString& OutError)
{
	if (bAuthenticatedIdentityRevoked || !IsAuthenticatedItemOwner(Result.Actor))
	{
		OutError = TEXT("A first-commit effect requires the current authenticated owner.");
		return false;
	}
	if (Result.Operation != RPGItemCommandComponent::ConsumeOperation) return true;
	if (!Result.WasSuccessful()) { OutError = TEXT("A consume effect requires a successful validated receipt."); return false; }
	if (AppliedFirstCommitEffects.Contains(Result.RequestId)) return true;
	const auto Target = ConsumeEffectTargets.FindRef(Result.RequestId); // pin across re-entrant gameplay/identity callbacks
	if (!Target || Result.RequestId != Target->Expected.RequestId || Result.Operation != Target->Expected.Operation ||
		Result.Actor != Target->Expected.Actor || Result.CommandFingerprint != Target->Expected.CommandFingerprint ||
		Result.AffectedQuantity != Target->Expected.AffectedQuantity || Result.Records.Num() != 1 ||
		!FRPGItemRecord::StaticStruct()->CompareScriptStruct(&Target->Expected.Records[0], &Result.Records[0], 0))
	{
		OutError = TEXT("The consume receipt does not match the original prepared effect.");
		return false;
	}
	if (Target->bApplying) { OutError = TEXT("The original consume effect is already executing."); return false; }
	PendingFirstCommitEffects.Add(Result.RequestId, Result);
	const auto* Controller = Cast<APlayerController>(GetOwner());
	auto* AbilitySystem = Target->AbilitySystem.Get();
	if (!Controller || !Target->Pawn.IsValid() || Target->Pawn->IsActorBeingDestroyed() || Controller->GetPawn() != Target->Pawn.Get() || !AbilitySystem ||
		!AbilitySystem->IsOwnerActorAuthoritative() || AbilitySystem->GetAvatarActor() != Target->Pawn.Get() || !Target->Effect.IsValid())
	{
		OutError = TEXT("The original consume avatar is unavailable; no effect was applied to a replacement.");
		return false;
	}
	const auto* Attributes = AbilitySystem->GetSet<URPGAttributeSet>();
	const bool bDead = AbilitySystem->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death) ||
		(Attributes && FMath::IsFinite(Attributes->GetCurrentHealth()) && Attributes->GetCurrentHealth() <= 0.f);
	if (bDead ||
		RPGItemCommandComponent::ConsumeRecoveryRevision(Target->Pawn.Get()) != Target->RecoveryRevision)
	{
		// The exact original receipt above proves consumption, not permission to
		// revive or heal another life. Resolve it once without a GE or a refund.
		// This cache also seals consumed effects that expired without delivery.
		AppliedFirstCommitEffects.Add(Result.RequestId);
		DiscardConsumeEffect(Result.RequestId);
		return true;
	}
	if (!RPGItemCommandComponent::IsLivingConsumeAvatar(Target->Pawn.Get(), AbilitySystem))
	{
		// Missing/non-finite health is not proof of death or completed delivery.
		OutError = TEXT("The original consume avatar health is unavailable; recovery remains pending.");
		return false;
	}
	Target->bApplying = true;
	const auto Handle = AbilitySystem->ApplyGameplayEffectSpecToSelf(*Target->Effect.Data.Get());
	Target->bApplying = false;
	// Instant effects deliberately have no active handle. Their execution flag is the success signal.
	if (!Handle.WasSuccessfullyApplied())
	{
		OutError = TEXT("The committed consumable effect was not applied; original recovery remains pending.");
		return false;
	}
	AppliedFirstCommitEffects.Add(Result.RequestId);
	DiscardConsumeEffect(Result.RequestId);
	return true;
}

void URPGItemCommandComponent::BlockInventoryRecoveryStorage()
{
	if (bInventoryRecoveryStorageBlocked) return;
	bInventoryRecoveryStorageBlocked = true;
	OnRep_InventoryRecovery();
	if (GetOwner()) GetOwner()->ForceNetUpdate();
}

bool URPGItemCommandComponent::EnsureOrchestrator()
{
	if (bEndingPlay || bAuthenticatedIdentityRevoked || bInventoryRecoveryStorageBlocked) return false;
	if (Orchestrator.IsValid())
	{
		FRPGItemOwnerRef Owner;
		if (!TryGetAuthenticatedOwner(Owner) || !IsAuthenticatedItemOwner(Owner)) { BlockInventoryRecoveryStorage(); return false; }
		if (Orchestrator->IsInventoryPersistenceBlocked()) { BlockInventoryRecoveryStorage(); return false; }
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
	FRPGItemOwnerRef Owner;
	const auto* Controller = Cast<APlayerController>(GetOwner());
	const auto* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
	if (!State || !TryGetAuthenticatedOwner(Owner) || !IsAuthenticatedItemOwner(Owner)) return false;
	InventoryJournal = Backend->OpenInventoryJournal(State->GetBackendDungeonSessionId(), Owner.OwnerId, State->GetAuthenticatedSteamId());
	if (!InventoryJournal || !InventoryJournal->IsHealthy() || InventoryJournal->IsRevoked())
	{
		BlockInventoryRecoveryStorage();
		return false;
	}

	CommitterAdapter = MakeSlotAwareCommitter(MakeUnique<RPGItemCommandComponent::FBackendCommitter>(*Backend));
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
		*FirstCommitSinkAdapter,
		InventoryJournal);
	// Re-publish original IDs before accepting a new command. Restoring never dispatches HTTP.
	for (const auto& Entry : InventoryJournal->GetPending())
	{
		SetInventoryRecovery(Entry.Key, Entry.Value.Operation, true);
		if (!IsAuthenticatedItemOwner(Owner) || !Orchestrator) return false;
	}
	return true;
}

bool URPGItemCommandComponent::TryGetAuthenticatedOwner(
	FRPGItemOwnerRef& OutOwner) const
{
	if (bEndingPlay || bAuthenticatedIdentityRevoked) return false;
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
	const FName Operation,
	const bool bRecoveryRetry)
{
	FRPGItemOwnerRef Owner;
	if (bEndingPlay || bAuthenticatedIdentityRevoked) return false;
	const bool bHasOwner = TryGetAuthenticatedOwner(Owner);
	if (!bHasOwner || !IsAuthenticatedItemOwner(Owner))
	{
		if (bHasOwner && InventoryJournal) BlockInventoryRecoveryStorage();
		// Keep the normal unauthenticated failure notification. Revoked/ended
		// components above are silent, and neither path admits a backend command.
		FRPGItemCommandClientResult Result;
		Result.RequestId = RequestId;
		Result.Operation = Operation;
		Result.Result = ERPGItemCommandResultCode::ServerStateError;
		ClientNotifyItemCommand(Result);
		return false;
	}
	if (bInventoryRecoveryStorageBlocked)
	{
		FRPGItemCommandClientResult Result;
		Result.RequestId = RequestId; Result.Operation = Operation; Result.Result = ERPGItemCommandResultCode::ServerStateError;
		ClientNotifyItemCommand(Result);
		return false;
	}
	bool bSortBlocked = false;
	if (!bRecoveryRetry) for (const auto& Pending : PendingInventorySlotRequests) bSortBlocked |= Pending.Value.Operation == TEXT("Item.Sort");
	const bool bRecoveryBlocked = bSortBlocked || (!bRecoveryRetry && Orchestrator && Orchestrator->HasUncertainInventoryCommands());
	if (!RequestId.IsValid() || bRecoveryBlocked ||
		InFlightRequestIds.Contains(RequestId) ||
		InFlightRequestIds.Num() >= MaximumInFlightCommands)
	{
		FRPGItemCommandClientResult Result;
		Result.RequestId = RequestId;
		Result.Operation = Operation;
		Result.Result = bRecoveryBlocked || InFlightRequestIds.Num() >=
			MaximumInFlightCommands
			? ERPGItemCommandResultCode::Busy
			: ERPGItemCommandResultCode::InvalidRequest;
		ClientNotifyItemCommand(Result);
		return false;
	}

	const APlayerController* Controller =
		Cast<APlayerController>(GetOwner());
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	URPGSecurityValidationComponent* Security = Pawn
		? Pawn->FindComponentByClass<URPGSecurityValidationComponent>()
		: nullptr;
	FString SecurityReason;
	if (!Security || !Security->CanPerformProtectedAction(
		Operation,
		true,
		SecurityReason))
	{
		FRPGItemCommandClientResult Result;
		Result.RequestId = RequestId;
		Result.Operation = Operation;
		Result.Result = Security
			? ERPGItemCommandResultCode::Forbidden
			: ERPGItemCommandResultCode::ServerStateError;
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
	FRPGItemOwnerRef Owner;
	if (bEndingPlay || bAuthenticatedIdentityRevoked || !TryGetAuthenticatedOwner(Owner) || !IsAuthenticatedItemOwner(Owner)) return;
	if (Orchestrator && Orchestrator->IsInventoryPersistenceBlocked()) bInventoryRecoveryStorageBlocked = true;
#if !UE_BUILD_SHIPPING
	// Read-only opt-in evidence: distinguish a durable conflict from an earlier planner rejection.
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryConcurrencyServerE2E")))
	{
		const TCHAR* Stage = Outcome.Stage == ERPGItemAsyncCommandStage::Completed ? TEXT("Completed")
			: Outcome.Stage == ERPGItemAsyncCommandStage::Uncertain ? TEXT("Uncertain")
			: Outcome.Stage == ERPGItemAsyncCommandStage::BackendRejected ? TEXT("BackendRejected") : TEXT("Other");
		const TCHAR* Backend = Outcome.BackendResult.Status == ERPGItemBackendStatus::LocationConflict ? TEXT("LocationConflict")
			: Outcome.BackendResult.Status == ERPGItemBackendStatus::Unauthorized ? TEXT("Unauthorized")
			: Outcome.BackendResult.Status == ERPGItemBackendStatus::AlreadyApplied ? TEXT("AlreadyApplied")
			: Outcome.BackendResult.Status == ERPGItemBackendStatus::Succeeded ? TEXT("Succeeded") : TEXT("Other");
		UE_LOG(LogTemp, Display, TEXT("RPG_ITEM_CONCURRENCY_SERVER OUTCOME Request=%s Stage=%s Backend=%s Pending=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphensLower), Stage, Backend,
			Orchestrator && Orchestrator->HasUncertainInventoryCommands() ? 1 : 0);
	}
#endif
	InFlightRequestIds.Remove(RequestId);
	FName FrozenOperation;
	const bool bUncertain = Orchestrator && Orchestrator->FindUncertainInventoryCommand(RequestId, Owner, FrozenOperation);
	if (!bUncertain) PendingInventorySlotRequests.Remove(RequestId);
	SetInventoryRecovery(RequestId, Operation, bUncertain);
	FRPGItemCommandClientResult Result;
	Result.RequestId = RequestId;
	Result.Operation = Operation;
	if (Outcome.Stage == ERPGItemAsyncCommandStage::Uncertain)
	{
		Result.Result = ERPGItemCommandResultCode::Uncertain;
	}
	else if (Outcome.Stage == ERPGItemAsyncCommandStage::Completed)
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
		case ERPGItemTransactionStatus::NoChange:
			Result.Result = ERPGItemCommandResultCode::NoChange;
			break;
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
	FRPGItemOwnerRef Owner;
	if (GetOwner() && GetOwner()->HasAuthority() && TryGetAuthenticatedOwner(Owner)) EnsureOrchestrator();
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

void URPGItemCommandComponent::ServerSortInventoryTab_Implementation(const FGuid RequestId, const EItemCategory Category)
{
	const FName Operation(TEXT("Item.Sort"));
	// A sort spans a tab: begin only from a quiet authoritative snapshot.
	if (HasInFlightCommands() || !PendingPickupLocations.IsEmpty() || !PendingInventorySlotRequests.IsEmpty())
	{
		FRPGItemCommandClientResult Result;
		Result.RequestId = RequestId; Result.Operation = Operation; Result.Result = ERPGItemCommandResultCode::Busy;
		ClientNotifyItemCommand(Result); return;
	}
	if (!BeginRequest(RequestId, Operation)) return;
	FRPGItemOwnerRef Owner;
	if (!RPGItemInventoryPolicy::IsTab(Category) || !TryGetAuthenticatedOwner(Owner) || !EnsureOrchestrator())
	{ HandleOutcome(RequestId, Operation, {}); return; }
	const TWeakObjectPtr<URPGItemCommandComponent> WeakThis(this);
	Orchestrator->Sort(RequestId, Owner, Category, [WeakThis, RequestId, Operation](FRPGItemAsyncCommandOutcome Outcome)
	{ if (auto* Self = WeakThis.Get()) Self->HandleOutcome(RequestId, Operation, MoveTemp(Outcome)); });
}
