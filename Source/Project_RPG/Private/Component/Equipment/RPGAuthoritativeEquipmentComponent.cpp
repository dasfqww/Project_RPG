#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"

#include "Character/RPGBaseCharacter.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Character.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Manager/DataManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGAuthoritativeEquipmentComponent)

namespace RPGAuthoritativeEquipment
{
void SetError(FString* OutError, const TCHAR* Message)
{
	if (OutError)
	{
		*OutError = Message;
	}
}
}

URPGAuthoritativeEquipmentComponent::
	URPGAuthoritativeEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void URPGAuthoritativeEquipmentComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ResetApplied();
	Super::EndPlay(EndPlayReason);
}

bool URPGAuthoritativeEquipmentComponent::Reconcile(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& Records,
	const UDataManager& DataManager,
	FString* OutError)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor ||
		!OwnerActor->HasAuthority() ||
		!ExpectedOwner.IsValid() ||
		ExpectedOwner.Type != ERPGItemOwnerType::Character)
	{
		RPGAuthoritativeEquipment::SetError(
			OutError,
			TEXT("Authoritative equipment requires a server-owned character."));
		return false;
	}

	TMap<EEquipmentSlotType, const FRPGItemRecord*> DesiredBySlot;
	TSet<FGuid> DesiredItemIds;
	for (const FRPGItemRecord& Record : Records)
	{
		if (!Record.IsStructurallyValid() ||
			Record.GetOwner() != ExpectedOwner)
		{
			RPGAuthoritativeEquipment::SetError(
				OutError,
				TEXT("Equipment reconciliation received an invalid or foreign record."));
			return false;
		}
		if (!Record.IsActive() ||
			Record.GetLocation().ContainerType !=
				ERPGItemContainerType::Equipment)
		{
			continue;
		}

		const int32 SlotIndex = Record.GetLocation().SlotIndex;
		if (SlotIndex < 0 ||
			SlotIndex >= static_cast<int32>(EEquipmentSlotType::Count) ||
			Record.GetQuantity() != 1)
		{
			RPGAuthoritativeEquipment::SetError(
				OutError,
				TEXT("An authoritative equipment record has an invalid slot or quantity."));
			return false;
		}
		const EEquipmentSlotType SlotType =
			static_cast<EEquipmentSlotType>(SlotIndex);
		if (DesiredBySlot.Contains(SlotType) ||
			DesiredItemIds.Contains(Record.GetItemId()))
		{
			RPGAuthoritativeEquipment::SetError(
				OutError,
				TEXT("Authoritative equipment contains duplicate slot or item identity."));
			return false;
		}

		const URPGItemDefinition* Definition =
			DataManager.FindNativeItemDefinition(
				Record.GetDefinitionId());
		const URPGItemEquipmentDefinitionFragment* Fragment = Definition
			? Definition->FindFragmentByClass<
				URPGItemEquipmentDefinitionFragment>()
			: nullptr;
		if (!Definition ||
			Definition->GetDefinitionVersion() !=
				Record.GetDefinitionVersion() ||
			!Fragment ||
			!Fragment->CanEquipInSlot(SlotType))
		{
			RPGAuthoritativeEquipment::SetError(
				OutError,
				TEXT("An equipment definition is missing, stale, or incompatible."));
			return false;
		}
		for (const URPGAbilitySet* AbilitySet : Fragment->GrantedAbilitySets)
		{
			if (!IsValid(AbilitySet))
			{
				RPGAuthoritativeEquipment::SetError(
					OutError,
					TEXT("An equipment definition contains an invalid ability set."));
				return false;
			}
		}

		DesiredBySlot.Add(SlotType, &Record);
		DesiredItemIds.Add(Record.GetItemId());
	}

	TArray<EEquipmentSlotType> SlotsToRemove;
	for (const TPair<
		EEquipmentSlotType,
		FRPGAppliedEquipmentRuntime>& Pair : AppliedBySlot)
	{
		const FRPGItemRecord* const* Desired =
			DesiredBySlot.Find(Pair.Key);
		if (!Desired ||
			(*Desired)->GetItemId() != Pair.Value.ItemId ||
			(*Desired)->GetDefinitionId() != Pair.Value.DefinitionId ||
			(*Desired)->GetDefinitionVersion() !=
				Pair.Value.DefinitionVersion)
		{
			SlotsToRemove.Add(Pair.Key);
		}
	}
	for (const EEquipmentSlotType SlotType : SlotsToRemove)
	{
		RemoveApplied(SlotType);
	}

	for (const TPair<
		EEquipmentSlotType,
		const FRPGItemRecord*>& Pair : DesiredBySlot)
	{
		if (FRPGAppliedEquipmentRuntime* Existing =
			AppliedBySlot.Find(Pair.Key))
		{
			Existing->Revision = Pair.Value->GetRevision();
			continue;
		}
		if (!ApplyRecord(
			Pair.Key,
			*Pair.Value,
			DataManager,
			OutError))
		{
			return false;
		}
	}
	return true;
}

bool URPGAuthoritativeEquipmentComponent::IsItemApplied(
	const EEquipmentSlotType SlotType,
	const FGuid& ItemId) const
{
	const FRPGAppliedEquipmentRuntime* Applied =
		AppliedBySlot.Find(SlotType);
	return Applied && Applied->ItemId == ItemId;
}

bool URPGAuthoritativeEquipmentComponent::ApplyRecord(
	const EEquipmentSlotType SlotType,
	const FRPGItemRecord& Record,
	const UDataManager& DataManager,
	FString* OutError)
{
	const URPGItemDefinition* Definition =
		DataManager.FindNativeItemDefinition(Record.GetDefinitionId());
	const URPGItemEquipmentDefinitionFragment* Fragment = Definition
		? Definition->FindFragmentByClass<
			URPGItemEquipmentDefinitionFragment>()
		: nullptr;
	if (!Definition || !Fragment)
	{
		RPGAuthoritativeEquipment::SetError(
			OutError,
			TEXT("The equipment definition is unavailable during apply."));
		return false;
	}

	URPGAbilitySystemComponent* AbilitySystem =
		GetOwner()->FindComponentByClass<URPGAbilitySystemComponent>();
	if (!Fragment->GrantedAbilitySets.IsEmpty() &&
		(!AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative()))
	{
		RPGAuthoritativeEquipment::SetError(
			OutError,
			TEXT("The authoritative ability system is not ready."));
		return false;
	}

	FRPGAppliedEquipmentRuntime& Applied =
		AppliedBySlot.Add(SlotType);
	Applied.ItemId = Record.GetItemId();
	Applied.DefinitionId = Record.GetDefinitionId();
	Applied.DefinitionVersion = Record.GetDefinitionVersion();
	Applied.Revision = Record.GetRevision();
	for (const URPGAbilitySet* AbilitySet : Fragment->GrantedAbilitySets)
	{
		FRPGAbilitySet_GrantedHandles& Handles =
			Applied.GrantedAbilitySetHandles.AddDefaulted_GetRef();
		AbilitySet->GiveToAbilitySystem(
			AbilitySystem,
			&Handles,
			const_cast<URPGItemDefinition*>(Definition));
	}

	RequestOrSpawnActor(SlotType, Applied.ItemId, DataManager);
	return true;
}

void URPGAuthoritativeEquipmentComponent::RemoveApplied(
	const EEquipmentSlotType SlotType)
{
	FRPGAppliedEquipmentRuntime* Applied = AppliedBySlot.Find(SlotType);
	if (!Applied)
	{
		return;
	}
	if (Applied->PendingActorLoad.IsValid())
	{
		Applied->PendingActorLoad->CancelHandle();
		Applied->PendingActorLoad.Reset();
	}
	if (IsValid(Applied->SpawnedActor))
	{
		Applied->SpawnedActor->Destroy();
	}
	if (URPGAbilitySystemComponent* AbilitySystem =
		GetOwner()->FindComponentByClass<URPGAbilitySystemComponent>())
	{
		for (FRPGAbilitySet_GrantedHandles& Handles :
			Applied->GrantedAbilitySetHandles)
		{
			Handles.TakeFromAbilitySystem(AbilitySystem);
		}
	}
	AppliedBySlot.Remove(SlotType);
}

void URPGAuthoritativeEquipmentComponent::ResetApplied()
{
	TArray<EEquipmentSlotType> Slots;
	AppliedBySlot.GenerateKeyArray(Slots);
	for (const EEquipmentSlotType SlotType : Slots)
	{
		RemoveApplied(SlotType);
	}
}

void URPGAuthoritativeEquipmentComponent::RequestOrSpawnActor(
	const EEquipmentSlotType SlotType,
	const FGuid& ItemId,
	const UDataManager& DataManager)
{
	FRPGAppliedEquipmentRuntime* Applied = AppliedBySlot.Find(SlotType);
	const URPGItemDefinition* Definition = Applied
		? DataManager.FindNativeItemDefinition(Applied->DefinitionId)
		: nullptr;
	const URPGItemEquipmentDefinitionFragment* Fragment = Definition
		? Definition->FindFragmentByClass<
			URPGItemEquipmentDefinitionFragment>()
		: nullptr;
	if (!Applied || !Fragment || Fragment->EquippedActorClass.IsNull())
	{
		return;
	}
	if (Fragment->EquippedActorClass.Get())
	{
		HandleActorClassLoaded(SlotType, ItemId);
		return;
	}

	Applied->PendingActorLoad = UAssetManager::GetStreamableManager()
		.RequestAsyncLoad(
			Fragment->EquippedActorClass.ToSoftObjectPath(),
			FStreamableDelegate::CreateUObject(
				this,
				&ThisClass::HandleActorClassLoaded,
				SlotType,
				ItemId));
}

void URPGAuthoritativeEquipmentComponent::HandleActorClassLoaded(
	const EEquipmentSlotType SlotType,
	const FGuid ItemId)
{
	FRPGAppliedEquipmentRuntime* Applied = AppliedBySlot.Find(SlotType);
	if (!Applied || Applied->ItemId != ItemId || IsValid(Applied->SpawnedActor))
	{
		return;
	}
	Applied->PendingActorLoad.Reset();

	const UGameInstance* GameInstance = GetWorld()
		? GetWorld()->GetGameInstance()
		: nullptr;
	const UDataManager* DataManager = GameInstance
		? GameInstance->GetSubsystem<UDataManager>()
		: nullptr;
	const URPGItemDefinition* Definition = DataManager
		? DataManager->FindNativeItemDefinition(Applied->DefinitionId)
		: nullptr;
	const URPGItemEquipmentDefinitionFragment* Fragment = Definition
		? Definition->FindFragmentByClass<
			URPGItemEquipmentDefinitionFragment>()
		: nullptr;
	UClass* ActorClass = Fragment
		? Fragment->EquippedActorClass.Get()
		: nullptr;
	if (!ActorClass || !GetWorld() || !GetOwner())
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = GetOwner();
	SpawnParameters.Instigator = Cast<APawn>(GetOwner());
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* SpawnedActor = GetWorld()->SpawnActor<AActor>(
		ActorClass,
		GetOwner()->GetActorTransform(),
		SpawnParameters);
	if (!SpawnedActor)
	{
		return;
	}
	if (!SpawnedActor->GetIsReplicated())
	{
		SpawnedActor->SetReplicates(true);
	}

	USceneComponent* AttachTarget = GetOwner()->GetRootComponent();
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		AttachTarget = Character->GetMesh();
	}
	if (AttachTarget)
	{
		SpawnedActor->AttachToComponent(
			AttachTarget,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			Fragment->AttachSocket);
		SpawnedActor->SetActorRelativeTransform(
			Fragment->EquippedActorRelativeTransform);
	}
	Applied->SpawnedActor = SpawnedActor;
}
