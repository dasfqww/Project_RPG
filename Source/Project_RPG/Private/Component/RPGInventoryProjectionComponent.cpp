#include "Component/RPGInventoryProjectionComponent.h"

#include "HAL/PlatformTime.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Item/Backend/RPGItemBackendTypes.h"
#include "Item/Backend/RPGItemBackendSubsystem.h"
#include "Item/Projection/RPGInventoryProjectionMapper.h"
#include "Net/UnrealNetwork.h"
#include "Player/RPGPlayerState.h"
#include "Security/RPGDungeonGameplayAuthority.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGInventoryProjectionComponent)

URPGInventoryProjectionComponent::URPGInventoryProjectionComponent()
	: Projection(FSimpleDelegate::CreateUObject(this, &ThisClass::HandleProjectionReplicated))
	, MailProjection(FSimpleDelegate::CreateUObject(this, &ThisClass::HandleProjectionReplicated))
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URPGInventoryProjectionComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ThisClass, Projection, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, MailProjection, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, LoadState, COND_OwnerOnly);
}

void URPGInventoryProjectionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bAuthenticatedIdentityRevoked = true;
	++ActiveLoadGeneration;
	++AuthoritativeSnapshotGeneration;
	Super::EndPlay(EndPlayReason);
}

void URPGInventoryProjectionComponent::InvalidateForRevokedIdentity(const ARPGPlayerState& RevokedState)
{
	const auto* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !RevokedState.IsAuthenticatedIdentityRevoked()
		|| Controller->GetPlayerState<ARPGPlayerState>() != &RevokedState || bAuthenticatedIdentityRevoked) return;
	bAuthenticatedIdentityRevoked = true;
	++ActiveLoadGeneration;
	++AuthoritativeSnapshotGeneration;
	AuthoritativeStore = FRPGInventoryProjectionStore();
	bool bChanged = false, bMailChanged = false;
	Projection.Reconcile({}, bChanged);
	MailProjection.Reconcile({}, bMailChanged);
	SetLoadState(ERPGInventoryProjectionLoadState::Uninitialized);
	OnAuthoritativeRecordsChanged.Broadcast();
	OnProjectionChanged.Broadcast();
	GetOwner()->ForceNetUpdate();
}

void URPGInventoryProjectionComponent::ServerRetryLoadAuthenticatedCharacterItems_Implementation()
{
	if (bAuthenticatedIdentityRevoked || !GetOwner() || !GetOwner()->HasAuthority()
		|| LoadState != ERPGInventoryProjectionLoadState::Failed) return;
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const ARPGPlayerState* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
	if (!State || !State->HasAuthenticatedCharacter() || State->IsAuthenticatedIdentityRevoked()
		|| IsDungeonGameplayRevoked(Controller)) return;
	const double Now = FPlatformTime::Seconds();
	if (LastRetryLoadSeconds >= 0.0 && Now - LastRetryLoadSeconds < 3.0) return;
	LastRetryLoadSeconds = Now;
	LoadAuthenticatedCharacterItems();
}

bool URPGInventoryProjectionComponent::LoadAuthenticatedCharacterItems()
{
	if (bAuthenticatedIdentityRevoked || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	const APlayerController* PlayerController =
		Cast<APlayerController>(GetOwner());
	const ARPGPlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<ARPGPlayerState>()
		: nullptr;
	if (!PlayerState || !PlayerState->HasAuthenticatedCharacter())
	{
		SetLoadState(ERPGInventoryProjectionLoadState::Failed);
		return false;
	}

	UGameInstance* GameInstance = GetWorld()
		? GetWorld()->GetGameInstance()
		: nullptr;
	URPGItemBackendSubsystem* Backend = GameInstance
		? GameInstance->GetSubsystem<URPGItemBackendSubsystem>()
		: nullptr;
	if (!Backend || !Backend->IsAvailable())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Item projection load rejected because the backend gateway is unavailable."));
		SetLoadState(ERPGInventoryProjectionLoadState::Failed);
		return false;
	}

	FGuid CharacterGuid;
	if (!FGuid::Parse(
		PlayerState->GetBackendCharacterId(),
		CharacterGuid))
	{
		SetLoadState(ERPGInventoryProjectionLoadState::Failed);
		return false;
	}
	const FString CharacterId = CharacterGuid.ToString(
		EGuidFormats::DigitsWithHyphensLower);
	const uint32 LoadGeneration = ++ActiveLoadGeneration;
	const TWeakObjectPtr<const ARPGPlayerState> ExpectedState(PlayerState);
	const FString ExpectedSessionId = PlayerState->GetBackendDungeonSessionId();
	const FString ExpectedSteamId = PlayerState->GetAuthenticatedSteamId();
	SetLoadState(ERPGInventoryProjectionLoadState::Loading);
	if (bAuthenticatedIdentityRevoked || ActiveLoadGeneration != LoadGeneration || !ExpectedState.IsValid()
		|| !ExpectedState->HasAuthenticatedCharacter()
		|| ExpectedState->GetBackendDungeonSessionId() != ExpectedSessionId
		|| ExpectedState->GetAuthenticatedSteamId() != ExpectedSteamId) return false;
	const TWeakObjectPtr<URPGInventoryProjectionComponent> WeakThis(this);
	return Backend->LoadCharacterItems(
		CharacterId,
		[WeakThis, CharacterId, CharacterGuid, LoadGeneration, ExpectedState, ExpectedSessionId, ExpectedSteamId](
			FRPGItemBackendLoadResult Result)
		{
			URPGInventoryProjectionComponent* Self = WeakThis.Get();
			if (!Self || Self->bAuthenticatedIdentityRevoked ||
				Self->ActiveLoadGeneration != LoadGeneration ||
				!Self->GetOwner() ||
				!Self->GetOwner()->HasAuthority())
			{
				return;
			}

			const APlayerController* CurrentController =
				Cast<APlayerController>(Self->GetOwner());
			const ARPGPlayerState* CurrentPlayerState = CurrentController
				? CurrentController->GetPlayerState<ARPGPlayerState>()
				: nullptr;
			FGuid CurrentCharacterGuid;
			if (!CurrentPlayerState ||
				!CurrentPlayerState->HasAuthenticatedCharacter() || CurrentPlayerState != ExpectedState.Get() ||
				CurrentPlayerState->GetBackendDungeonSessionId() != ExpectedSessionId ||
				CurrentPlayerState->GetAuthenticatedSteamId() != ExpectedSteamId ||
				!FGuid::Parse(
					CurrentPlayerState->GetBackendCharacterId(),
					CurrentCharacterGuid) ||
				CurrentCharacterGuid != CharacterGuid ||
				!Result.WasSuccessful())
			{
				if (!Result.WasSuccessful())
				{
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("Item projection backend load failed. Status=%d Http=%d Error=%s"),
						static_cast<int32>(Result.Status),
						Result.HttpStatusCode,
						*Result.Error);
				}
				Self->SetLoadState(
					ERPGInventoryProjectionLoadState::Failed);
				return;
			}

			FRPGItemOwnerRef ExpectedOwner;
			ExpectedOwner.Type = ERPGItemOwnerType::Character;
			ExpectedOwner.OwnerId = CharacterId;
			FString ProjectionError;
			if (!Self->ApplyAuthoritativeRecords(
				ExpectedOwner,
				Result.Records,
				&ProjectionError))
			{
				Self->SetLoadState(
					ERPGInventoryProjectionLoadState::Failed);
				return;
			}

			Self->SetLoadState(ERPGInventoryProjectionLoadState::Ready);
		});
}

bool URPGInventoryProjectionComponent::ApplyCompletedRewardSnapshot(const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& ActiveRecords, const uint64 ExpectedGeneration, FString* OutError)
{
	if (AuthoritativeSnapshotGeneration != ExpectedGeneration || !AuthoritativeStore.IsInitialized()
		|| AuthoritativeStore.GetOwner() != ExpectedOwner) return false;
	TArray<FRPGItemRecord> Combined = ActiveRecords;
	TSet<FGuid> ActiveIds;
	for (const FRPGItemRecord& Record : ActiveRecords)
	{
		if (!Record.IsActive()) return false;
		ActiveIds.Add(Record.GetItemId());
	}
	// Retain known tombstones needed by late idempotency receipts/first-commit effects.
	TArray<FRPGItemRecord> Previous;
	AuthoritativeStore.FindByOwner(ExpectedOwner, Previous);
	for (const FRPGItemRecord& Record : Previous)
		if (!Record.IsActive() && !ActiveIds.Contains(Record.GetItemId())) Combined.Add(Record);
	if (!ApplyAuthoritativeRecords(ExpectedOwner, Combined, OutError)) return false;
	++ActiveLoadGeneration; // An older full-load callback must not undo this refresh.
	SetLoadState(ERPGInventoryProjectionLoadState::Ready);
	return true;
}

bool URPGInventoryProjectionComponent::ApplyAuthoritativeRecords(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& Records,
	FString* OutError)
{
	if (bAuthenticatedIdentityRevoked || !GetOwner() || !GetOwner()->HasAuthority())
	{
		if (OutError)
		{
			*OutError = TEXT(
				"Only authority may update an inventory projection.");
		}
		return false;
	}

	TArray<FRPGInventoryProjectionEntry> DesiredEntries;
	if (!AuthoritativeStore.Replace(
		ExpectedOwner,
		Records,
		DesiredEntries,
		OutError))
	{
		return false;
	}

	++AuthoritativeSnapshotGeneration;
	if (!ReconcileProjection(DesiredEntries, OutError))
	{
		return false;
	}
	OnAuthoritativeRecordsChanged.Broadcast();
	return !bAuthenticatedIdentityRevoked;
}

bool URPGInventoryProjectionComponent::ApplyAuthoritativeMutationRecords(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& MutationRecords,
	FString* OutError)
{
	if (bAuthenticatedIdentityRevoked || !GetOwner() || !GetOwner()->HasAuthority())
	{
		if (OutError)
		{
			*OutError = TEXT(
				"Only authority may update an inventory projection.");
		}
		return false;
	}

	TArray<FRPGInventoryProjectionEntry> DesiredEntries;
	if (!AuthoritativeStore.ApplyMutations(
		ExpectedOwner,
		MutationRecords,
		DesiredEntries,
		OutError))
	{
		return false;
	}
	++AuthoritativeSnapshotGeneration;
	if (!ReconcileProjection(DesiredEntries, OutError))
	{
		return false;
	}
	OnAuthoritativeRecordsChanged.Broadcast();
	return !bAuthenticatedIdentityRevoked;
}

bool URPGInventoryProjectionComponent::ApplyAuthoritativeCommitResult(
	const FRPGItemOwnerRef& ExpectedOwner,
	const FRPGItemBackendCommitResult& CommitResult,
	FString* OutError)
{
	if (!CommitResult.WasSuccessful())
	{
		if (OutError)
		{
			*OutError = TEXT(
				"Only a successful backend commit may update the projection.");
		}
		return false;
	}
	if (CommitResult.Actor != ExpectedOwner)
	{
		if (OutError)
		{
			*OutError = TEXT(
				"The backend commit actor does not own this projection.");
		}
		return false;
	}

	return ApplyAuthoritativeMutationRecords(
		ExpectedOwner,
		CommitResult.Records,
		OutError);
}

bool URPGInventoryProjectionComponent::ReconcileProjection(
	const TArray<FRPGInventoryProjectionEntry>& DesiredEntries,
	FString* OutError)
{
	TArray<FRPGItemRecord> Records;
	AuthoritativeStore.FindByOwner(AuthoritativeStore.GetOwner(), Records);
	TArray<FRPGInventoryProjectionEntry> MailEntries;
	if (!FRPGInventoryProjectionMapper::BuildMailSnapshot(
		AuthoritativeStore.GetOwner(), Records, MailEntries, OutError))
	{
		return false;
	}
	bool bChanged = false;
	if (!Projection.Reconcile(DesiredEntries, bChanged))
	{
		if (OutError)
		{
			*OutError = TEXT("The projected inventory snapshot is invalid.");
		}
		return false;
	}
	bool bMailChanged = false;
	if (!MailProjection.Reconcile(MailEntries, bMailChanged))
	{
		if (OutError)
		{
			*OutError = TEXT("The projected mailbox snapshot is invalid.");
		}
		return false;
	}
	if (bChanged || bMailChanged)
	{
		OnProjectionChanged.Broadcast();
		GetOwner()->ForceNetUpdate();
	}
	return true;
}

TArray<FRPGInventoryProjectionEntry>
URPGInventoryProjectionComponent::GetProjectedItems() const
{
	TArray<FRPGInventoryProjectionEntry> Result = Projection.GetEntries();
	Result.Sort(
		[](const FRPGInventoryProjectionEntry& Left,
			const FRPGInventoryProjectionEntry& Right)
		{
			if (Left.GetContainerType() != Right.GetContainerType())
			{
				return static_cast<uint8>(Left.GetContainerType()) <
					static_cast<uint8>(Right.GetContainerType());
			}
			return Left.GetSlotIndex() < Right.GetSlotIndex();
		});
	return Result;
}

bool URPGInventoryProjectionComponent::FindProjectedItem(
	const FGuid& ItemId,
	FRPGInventoryProjectionEntry& OutEntry) const
{
	const FRPGInventoryProjectionEntry* Found = Projection.Find(ItemId);
	if (!Found)
	{
		return false;
	}
	OutEntry = *Found;
	return true;
}

TArray<FRPGInventoryProjectionEntry>
URPGInventoryProjectionComponent::GetMailItems() const
{
	TArray<FRPGInventoryProjectionEntry> Result = MailProjection.GetEntries();
	Result.Sort([](const FRPGInventoryProjectionEntry& Left,
		const FRPGInventoryProjectionEntry& Right)
	{
		return Left.GetItemId().ToString(EGuidFormats::Digits) <
			Right.GetItemId().ToString(EGuidFormats::Digits);
	});
	return Result;
}

bool URPGInventoryProjectionComponent::FindMailItem(
	const FGuid& ItemId, FRPGInventoryProjectionEntry& OutEntry) const
{
	const FRPGInventoryProjectionEntry* Entry = MailProjection.Find(ItemId);
	if (!Entry)
	{
		return false;
	}
	OutEntry = *Entry;
	return true;
}

void URPGInventoryProjectionComponent::HandleProjectionReplicated()
{
	OnProjectionChanged.Broadcast();
}

void URPGInventoryProjectionComponent::SetLoadState(
	const ERPGInventoryProjectionLoadState NewState)
{
	if (bAuthenticatedIdentityRevoked && NewState != ERPGInventoryProjectionLoadState::Uninitialized) return;
	if (LoadState == NewState)
	{
		return;
	}
	LoadState = NewState;
	OnLoadStateChanged.Broadcast(LoadState);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		GetOwner()->ForceNetUpdate();
	}
}

void URPGInventoryProjectionComponent::OnRep_LoadState()
{
	OnLoadStateChanged.Broadcast(LoadState);
}
