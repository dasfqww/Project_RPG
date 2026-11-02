#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"

#include "Character/RPGBaseCharacter.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "GameplayEffect/RPGEquipmentProgressionEffect.h"
#include "Item/Equipment/RPGEquipmentProgressionRules.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Character.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Manager/DataManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

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
	SetIsReplicatedByDefault(true);
}

void URPGAuthoritativeEquipmentComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ThisClass, EquipmentSnapshots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, ActiveEquipState, COND_OwnerOnly);
}

bool URPGAuthoritativeEquipmentComponent::FindSnapshotInSlot(
	const EEquipmentSlotType Slot, FRPGEquipmentSnapshot& OutSnapshot) const
{
	OutSnapshot = {};
	for (const FRPGEquipmentSnapshot& Snapshot : EquipmentSnapshots)
	{
		if (Snapshot.SlotType == Slot)
		{
			OutSnapshot = Snapshot;
			return true;
		}
	}
	return false;
}

bool URPGAuthoritativeEquipmentComponent::HasWeaponSnapshots() const
{
	return EquipmentSnapshots.ContainsByPredicate([](const FRPGEquipmentSnapshot& Snapshot)
	{
		return FRPGEquipmentSnapshot::IsWeaponSlot(Snapshot.SlotType);
	});
}

bool URPGAuthoritativeEquipmentComponent::FindWeaponSnapshot(
	const EWeaponHandType Hand, FRPGEquipmentSnapshot& OutSnapshot) const
{
	OutSnapshot = {};
	if (static_cast<uint8>(Hand) > static_cast<uint8>(EWeaponHandType::Count)
		|| (ActiveEquipState != EEquipState::WeaponSet_Primary
			&& ActiveEquipState != EEquipState::WeaponSet_Secondary)) return false;
	const bool bSecondary = ActiveEquipState == EEquipState::WeaponSet_Secondary;
	const EEquipmentSlotType Slots[] = {
		bSecondary ? EEquipmentSlotType::Weapon_Secondary_L : EEquipmentSlotType::Weapon_Primary_L,
		bSecondary ? EEquipmentSlotType::Weapon_Secondary_R : EEquipmentSlotType::Weapon_Primary_R
	};
	for (const EEquipmentSlotType Slot : Slots)
	{
		if ((Hand == EWeaponHandType::LeftHand && Slot == Slots[1])
			|| (Hand == EWeaponHandType::RightHand && Slot == Slots[0])) continue;
		FRPGEquipmentSnapshot Candidate;
		if (!FindSnapshotInSlot(Slot, Candidate)) continue;
		// TwoHand must not accidentally select a known one-handed item in the first slot.
		if (Hand != EWeaponHandType::Count && Candidate.WeaponHandType != EWeaponHandType::Count
			&& Candidate.WeaponHandType != Hand) continue;
		OutSnapshot = MoveTemp(Candidate);
		return true; // Presence matters even while its actor is loading.
	}
	return false;
}

bool URPGAuthoritativeEquipmentComponent::GetEffectContext(const EEquipmentSlotType Slot, FRPGEquipmentEffectContext& Out) const
{
	Out = {};
	const auto* Applied = AppliedBySlot.Find(Slot);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Applied) return false;
	Out.ItemId = Applied->ItemId;
	Out.DefinitionId = Applied->DefinitionId;
	Out.DefinitionVersion = Applied->DefinitionVersion;
	Out.GrantId = Applied->EffectGrantId;
	Out.ActiveStateGeneration = EffectActiveStateGeneration;
	return Out.IsValid();
}

bool URPGAuthoritativeEquipmentComponent::IsAbilityFromEffectContext(const FGameplayAbilitySpecHandle Handle,
	const FRPGEquipmentEffectContext& Context) const
{
	for (const auto& Pair : AppliedBySlot)
	{
		FRPGEquipmentEffectContext Current;
		if (!GetEffectContext(Pair.Key, Current) || !Current.Matches(Context)) continue;
		for (const auto& Granted : Pair.Value.GrantedAbilitySetHandles)
			if (Granted.ContainsAbility(Handle)) return true;
	}
	return false;
}

void URPGAuthoritativeEquipmentComponent::SetActiveEquipState(const EEquipState State)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()
		|| static_cast<uint8>(State) >= static_cast<uint8>(EEquipState::Count)
		|| ActiveEquipState == State) return;
	const EEquipState PreviousState = ActiveEquipState;
	FRPGEquipmentProgressionContribution Contribution;
	if (!RPGEquipmentProgression::TrySum(EquipmentSnapshots, State, Contribution)
		|| !ApplyProgressionStats(Contribution.Attack, Contribution.MaximumHealth)) return;
	ActiveEquipState = State;
	++EffectActiveStateGeneration;
	UpdateActorVisibility();
	GetOwner()->ForceNetUpdate();
	OnActiveEquipStateChanged.Broadcast(PreviousState, ActiveEquipState);
}

void URPGAuthoritativeEquipmentComponent::OnRep_ActiveEquipState(const EEquipState PreviousState)
{
	// Actor visibility is replicated from the server. This only notifies read consumers.
	if (PreviousState != ActiveEquipState)
	{
		OnActiveEquipStateChanged.Broadcast(PreviousState, ActiveEquipState);
	}
}

void URPGAuthoritativeEquipmentComponent::OnRep_EquipmentSnapshots()
{
	OnEquipmentSnapshotsChanged.Broadcast();
}

void URPGAuthoritativeEquipmentComponent::UpdateActorVisibility()
{
	const bool bSecondary = ActiveEquipState == EEquipState::WeaponSet_Secondary;
	for (const auto& Pair : AppliedBySlot)
	{
		if (!IsValid(Pair.Value.SpawnedActor) || !FRPGEquipmentSnapshot::IsWeaponSlot(Pair.Key)) continue;
		const bool bSecondarySlot = Pair.Key == EEquipmentSlotType::Weapon_Secondary_L
			|| Pair.Key == EEquipmentSlotType::Weapon_Secondary_R;
		Pair.Value.SpawnedActor->SetActorHiddenInGame(
			(ActiveEquipState != EEquipState::WeaponSet_Primary
				&& ActiveEquipState != EEquipState::WeaponSet_Secondary) || bSecondarySlot != bSecondary);
	}
}

void URPGAuthoritativeEquipmentComponent::RefreshSnapshotActors()
{
	for (FRPGEquipmentSnapshot& Snapshot : EquipmentSnapshots)
	{
		const FRPGAppliedEquipmentRuntime* Applied = AppliedBySlot.Find(Snapshot.SlotType);
		Snapshot.EquippedActor = Applied && Applied->ItemId == Snapshot.ItemId
			? Applied->SpawnedActor : nullptr;
	}
	UpdateActorVisibility();
	if (GetOwner()) GetOwner()->ForceNetUpdate();
	OnEquipmentSnapshotsChanged.Broadcast();
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
	TArray<FRPGEquipmentSnapshot> DesiredSnapshots;
	if (AppliedOwner.IsValid() && AppliedOwner != ExpectedOwner)
	{
		ResetApplied(); // Do not expose the previous character's equipment after an owner switch.
	}
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

		FRPGEquipmentSnapshot Snapshot;
		if (!FRPGEquipmentSnapshot::TryBuild(Record, *Definition, Snapshot))
		{
			RPGAuthoritativeEquipment::SetError(OutError,
				TEXT("Equipment snapshot metadata is invalid or incompatible with the slot."));
			return false;
		}
		DesiredSnapshots.Add(MoveTemp(Snapshot));

		DesiredBySlot.Add(SlotType, &Record);
		DesiredItemIds.Add(Record.GetItemId());
	}

	FRPGEquipmentProgressionContribution Contribution;
	if (!RPGEquipmentProgression::TrySum(DesiredSnapshots, ActiveEquipState, Contribution))
	{
		RPGAuthoritativeEquipment::SetError(OutError, TEXT("Unsupported explicit equipment progression."));
		return false;
	}
	if (Contribution.Attack != 0.f || Contribution.MaximumHealth != 0.f)
	{
		const auto* ASC = OwnerActor->FindComponentByClass<URPGAbilitySystemComponent>();
		if (!ASC || !ASC->IsOwnerActorAuthoritative()
			|| !ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetAttackAttribute())
			|| !ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetMaxHealthAttribute()))
		{
			RPGAuthoritativeEquipment::SetError(OutError, TEXT("Equipment progression requires initialized authoritative attributes."));
			return false;
		}
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
			ResetApplied(); // Fail closed, including any partially applied resources.
			OwnerActor->ForceNetUpdate();
			OnEquipmentSnapshotsChanged.Broadcast();
			return false;
		}
	}
	if (!ApplyProgressionStats(Contribution.Attack, Contribution.MaximumHealth))
	{
		ResetApplied();
		RPGAuthoritativeEquipment::SetError(OutError, TEXT("Equipment progression effect could not be applied."));
		return false;
	}
	DesiredSnapshots.Sort([](const FRPGEquipmentSnapshot& A, const FRPGEquipmentSnapshot& B)
	{
		return static_cast<uint8>(A.SlotType) < static_cast<uint8>(B.SlotType);
	});
	EquipmentSnapshots = MoveTemp(DesiredSnapshots);
	AppliedOwner = ExpectedOwner;
	RefreshSnapshotActors();
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryEquipmentRecoveryServerE2E")))
	{
		FRPGEquipmentSnapshot Head;
		const bool bHead = FindSnapshotInSlot(EEquipmentSlotType::Head, Head);
		UE_LOG(LogTemp, Display, TEXT("RPG_EQUIPMENT_RECOVERY_STATE Head=%d Item=%s Revision=%lld Applied=%d Total=%d"),
			bHead ? 1 : 0, *Head.ItemId.ToString(EGuidFormats::DigitsWithHyphensLower), Head.Revision,
			bHead && IsItemApplied(EEquipmentSlotType::Head, Head.ItemId) ? 1 : 0, NumAppliedItems());
	}
#endif
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
	Applied.EffectGrantId = FGuid::NewGuid();
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
#if !UE_BUILD_SHIPPING
	if (SlotType == EEquipmentSlotType::Head && FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryEquipmentRecoveryServerE2E")))
		UE_LOG(LogTemp, Display, TEXT("RPG_EQUIPMENT_RECOVERY_RESOURCE APPLY Item=%s Revision=%lld"),
			*Record.GetItemId().ToString(EGuidFormats::DigitsWithHyphensLower), Record.GetRevision());
#endif
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
#if !UE_BUILD_SHIPPING
	if (SlotType == EEquipmentSlotType::Head && FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryEquipmentRecoveryServerE2E")))
		UE_LOG(LogTemp, Display, TEXT("RPG_EQUIPMENT_RECOVERY_RESOURCE REMOVE Item=%s"), *Applied->ItemId.ToString(EGuidFormats::DigitsWithHyphensLower));
#endif
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
	EquipmentSnapshots.Reset();
	AppliedOwner = {};
	ApplyProgressionStats(0.f, 0.f);
	if (GetOwner() && GetOwner()->HasAuthority()) GetOwner()->ForceNetUpdate();
	OnEquipmentSnapshotsChanged.Broadcast();
}

bool URPGAuthoritativeEquipmentComponent::ApplyProgressionStats(const float Attack, const float MaximumHealth)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return Attack == 0.f && MaximumHealth == 0.f;
	auto* ASC = GetOwner() ? GetOwner()->FindComponentByClass<URPGAbilitySystemComponent>() : nullptr;
	const bool bHasContribution = Attack != 0.f || MaximumHealth != 0.f;
	if (bHasContribution && (!ASC || !ASC->IsOwnerActorAuthoritative()
		|| !ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetAttackAttribute())
		|| !ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetMaxHealthAttribute()))) return false;
	if (ProgressionAbilitySystem == ASC && Attack == AppliedProgressionAttack && MaximumHealth == AppliedProgressionHealth
		&& (!bHasContribution || (ProgressionEffect.IsValid() && ASC->GetActiveGameplayEffect(ProgressionEffect)))) return true;
	FActiveGameplayEffectHandle Next;
	if (bHasContribution)
	{
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(this);
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(URPGEquipmentProgressionEffect::StaticClass(), 1.f, Context);
		if (!Spec.IsValid()) return false;
		Spec.Data->SetSetByCallerMagnitude(URPGEquipmentProgressionEffect::AttackMagnitude(), Attack);
		Spec.Data->SetSetByCallerMagnitude(URPGEquipmentProgressionEffect::HealthMagnitude(), MaximumHealth);
		Next = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		if (!Next.IsValid() || !ASC->GetActiveGameplayEffect(Next)) return false;
	}
	if (auto* PreviousASC = ProgressionAbilitySystem.Get(); PreviousASC && ProgressionEffect.IsValid())
		PreviousASC->RemoveActiveGameplayEffect(ProgressionEffect);
	ProgressionEffect = Next;
	ProgressionAbilitySystem = ASC;
	AppliedProgressionAttack = Attack;
	AppliedProgressionHealth = MaximumHealth;
	// Replace the complete aggregate before clamping, so swapping two chests
	// does not transiently clamp health against an unequipped base maximum.
	if (ASC && ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute()))
	{
		const float Health = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		const float Max = ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute());
		if (Health > Max) ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), FMath::Max(0.f, Max));
	}
	return true;
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
	RefreshSnapshotActors();
}
