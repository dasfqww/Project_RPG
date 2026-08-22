#include "Component/RPGInventoryProjectionComponent.h"

#include "GameFramework/PlayerController.h"
#include "Item/Backend/RPGItemBackendTypes.h"
#include "Item/Backend/RPGItemBackendSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Player/RPGPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGInventoryProjectionComponent)

URPGInventoryProjectionComponent::URPGInventoryProjectionComponent()
	: Projection(this)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URPGInventoryProjectionComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ThisClass, Projection, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, LoadState, COND_OwnerOnly);
}

bool URPGInventoryProjectionComponent::LoadAuthenticatedCharacterItems()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
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
	SetLoadState(ERPGInventoryProjectionLoadState::Loading);
	const TWeakObjectPtr<URPGInventoryProjectionComponent> WeakThis(this);
	return Backend->LoadCharacterItems(
		CharacterId,
		[WeakThis, CharacterId, CharacterGuid, LoadGeneration](
			FRPGItemBackendLoadResult Result)
		{
			URPGInventoryProjectionComponent* Self = WeakThis.Get();
			if (!Self ||
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
				!FGuid::Parse(
					CurrentPlayerState->GetBackendCharacterId(),
					CurrentCharacterGuid) ||
				CurrentCharacterGuid != CharacterGuid ||
				!Result.WasSuccessful())
			{
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

bool URPGInventoryProjectionComponent::ApplyAuthoritativeRecords(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& Records,
	FString* OutError)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
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

	if (!ReconcileProjection(DesiredEntries, OutError))
	{
		return false;
	}
	OnAuthoritativeRecordsChanged.Broadcast();
	return true;
}

bool URPGInventoryProjectionComponent::ApplyAuthoritativeMutationRecords(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& MutationRecords,
	FString* OutError)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
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
	if (!ReconcileProjection(DesiredEntries, OutError))
	{
		return false;
	}
	OnAuthoritativeRecordsChanged.Broadcast();
	return true;
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
	bool bChanged = false;
	if (!Projection.Reconcile(DesiredEntries, bChanged))
	{
		if (OutError)
		{
			*OutError = TEXT("The projected inventory snapshot is invalid.");
		}
		return false;
	}
	if (bChanged)
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

void URPGInventoryProjectionComponent::HandleProjectionReplicated()
{
	OnProjectionChanged.Broadcast();
}

void URPGInventoryProjectionComponent::SetLoadState(
	const ERPGInventoryProjectionLoadState NewState)
{
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
