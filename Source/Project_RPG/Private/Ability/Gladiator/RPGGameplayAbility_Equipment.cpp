#include "Ability/Gladiator/RPGGameplayAbility_Equipment.h"

#include "Attribute/RPGAttributeSet.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "Component/Equipment/RPGEquipComponent.h"
#include "Component/Equipment/RPGEquipmentComponent.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "Item/RPGItemBase.h"
#include "Item/Weapon/RPGWeaponBase.h"
#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGGameplayAbility_Equipment)

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Ability_ActivateFail_MissingEquipment,
	"Ability.ActivateFail.MissingEquipment");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Ability_ActivateFail_WrongEquipment,
	"Ability.ActivateFail.WrongEquipment");

namespace RPGEquipmentAbility
{
	ERPGGladiatorEquipmentType GetEffectiveEquipmentType(
		const FRPGGladiatorEquipmentInfo& EquipmentInfo)
	{
		if (EquipmentInfo.EquipmentType != ERPGGladiatorEquipmentType::Count)
		{
			return EquipmentInfo.EquipmentType;
		}

		// Some original D1 native skill constructors populated only the subtype fields.
		if (EquipmentInfo.RequiredArmorType != ERPGGladiatorArmorType::Count)
		{
			return ERPGGladiatorEquipmentType::Armor;
		}
		if (EquipmentInfo.WeaponHandType != EWeaponHandType::Count ||
			EquipmentInfo.RequiredWeaponType != ERPGGladiatorWeaponType::Count)
		{
			return ERPGGladiatorEquipmentType::Weapon;
		}
		if (EquipmentInfo.RequiredUtilityType != ERPGGladiatorUtilityType::Count)
		{
			return ERPGGladiatorEquipmentType::Utility;
		}

		return ERPGGladiatorEquipmentType::Count;
	}

	EEquipmentSlotType GetArmorSlot(const ERPGGladiatorArmorType ArmorType)
	{
		switch (ArmorType)
		{
		case ERPGGladiatorArmorType::Helmet: return EEquipmentSlotType::Head;
		case ERPGGladiatorArmorType::Chest: return EEquipmentSlotType::Chest;
		case ERPGGladiatorArmorType::Legs: return EEquipmentSlotType::Legs;
		case ERPGGladiatorArmorType::Hands: return EEquipmentSlotType::Hands;
		case ERPGGladiatorArmorType::Foot: return EEquipmentSlotType::Feet;
		default: return EEquipmentSlotType::None;
		}
	}

	void BuildWeaponSlotCandidates(const EWeaponHandType HandType, const EEquipState EquipState,
		TArray<EEquipmentSlotType, TInlineAllocator<4>>& OutSlots)
	{
		const auto AddSet = [&OutSlots, HandType](const bool bPrimary)
		{
			const EEquipmentSlotType LeftSlot = bPrimary
				? EEquipmentSlotType::Weapon_Primary_L
				: EEquipmentSlotType::Weapon_Secondary_L;
			const EEquipmentSlotType RightSlot = bPrimary
				? EEquipmentSlotType::Weapon_Primary_R
				: EEquipmentSlotType::Weapon_Secondary_R;

			if (HandType != EWeaponHandType::RightHand)
			{
				OutSlots.Add(LeftSlot);
			}
			if (HandType != EWeaponHandType::LeftHand)
			{
				OutSlots.Add(RightSlot);
			}
		};

		if (EquipState == EEquipState::WeaponSet_Secondary)
		{
			AddSet(false);
			AddSet(true);
		}
		else
		{
			AddSet(true);
			AddSet(false);
		}
	}

	URPGItemBase* GetItemInSlot(const AActor* AvatarActor, const EEquipmentSlotType SlotType,
		AActor*& OutEquipmentActor)
	{
		OutEquipmentActor = nullptr;
		if (!AvatarActor || SlotType == EEquipmentSlotType::None)
		{
			return nullptr;
		}

		if (const URPGEquipComponent* EquipComponent =
			AvatarActor->FindComponentByClass<URPGEquipComponent>())
		{
			if (URPGItemBase* Item = EquipComponent->GetItemInSlot(SlotType))
			{
				OutEquipmentActor = EquipComponent->GetSpawnedActorInSlot(SlotType);
				return Item;
			}
		}

		if (const URPGEquipmentComponent* EquipmentComponent =
			AvatarActor->FindComponentByClass<URPGEquipmentComponent>())
		{
			return EquipmentComponent->GetItemInSlot(SlotType);
		}

		return nullptr;
	}
}

URPGGameplayAbility_Equipment::URPGGameplayAbility_Equipment(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	bServerRespectsRemoteAbilityCancellation = false;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnlyTermination;
}

void URPGGameplayAbility_Equipment::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bUsedCompatibilityFallback = false;
	for (FRPGGladiatorEquipmentInfo& EquipmentInfo : EquipmentInfos)
	{
		EquipmentInfo.EquipmentActor.Reset();
		EquipmentInfo.ItemInstance.Reset();

		URPGItemBase* ItemInstance = nullptr;
		AActor* EquipmentActor = nullptr;
		bool bUsedFallback = false;
		const ERequirementResult Result = ResolveEquipmentRequirement(
			ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr,
			EquipmentInfo, ItemInstance, EquipmentActor, bUsedFallback);
		if (Result != ERequirementResult::Satisfied)
		{
			CancelAbility(Handle, ActorInfo, ActivationInfo, true);
			return;
		}

		EquipmentInfo.ItemInstance = ItemInstance;
		EquipmentInfo.EquipmentActor = EquipmentActor;
		bUsedCompatibilityFallback |= bUsedFallback;
	}

	SnapshottedAttackRate = DefaultAttackRate;
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		if (const URPGAttributeSet* AttributeSet =
			ActorInfo->AbilitySystemComponent->GetSet<URPGAttributeSet>())
		{
			const float AttackSpeedMultiplier =
				1.0f + AttributeSet->GetAttackSpeedPercent() / 100.0f;
			SnapshottedAttackRate = FMath::Max(0.0f,
				DefaultAttackRate * AttackSpeedMultiplier);
		}
	}
}

bool URPGGameplayAbility_Equipment::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(
		Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!AvatarActor)
	{
		if (OptionalRelevantTags)
		{
			OptionalRelevantTags->AddTag(TAG_Ability_ActivateFail_MissingEquipment);
		}
		return false;
	}

	for (const FRPGGladiatorEquipmentInfo& EquipmentInfo : EquipmentInfos)
	{
		URPGItemBase* ItemInstance = nullptr;
		AActor* EquipmentActor = nullptr;
		bool bUsedFallback = false;
		const ERequirementResult Result = ResolveEquipmentRequirement(
			AvatarActor, EquipmentInfo, ItemInstance, EquipmentActor, bUsedFallback);
		if (Result == ERequirementResult::Satisfied)
		{
			continue;
		}

		if (OptionalRelevantTags)
		{
			OptionalRelevantTags->AddTag(Result == ERequirementResult::WrongType
				? TAG_Ability_ActivateFail_WrongEquipment
				: TAG_Ability_ActivateFail_MissingEquipment);
		}
		return false;
	}

	return true;
}

URPGGameplayAbility_Equipment::ERequirementResult
URPGGameplayAbility_Equipment::ResolveEquipmentRequirement(const AActor* AvatarActor,
	const FRPGGladiatorEquipmentInfo& EquipmentInfo, URPGItemBase*& OutItem,
	AActor*& OutEquipmentActor, bool& bOutUsedCompatibilityFallback) const
{
	OutItem = nullptr;
	OutEquipmentActor = nullptr;
	bOutUsedCompatibilityFallback = false;

	if (!AvatarActor)
	{
		return ERequirementResult::Missing;
	}

	const ERPGGladiatorEquipmentType EquipmentType =
		RPGEquipmentAbility::GetEffectiveEquipmentType(EquipmentInfo);
	if (EquipmentType == ERPGGladiatorEquipmentType::Count)
	{
		return ERequirementResult::WrongType;
	}

	if (EquipmentType == ERPGGladiatorEquipmentType::Armor)
	{
		OutItem = RPGEquipmentAbility::GetItemInSlot(
			AvatarActor,
			RPGEquipmentAbility::GetArmorSlot(EquipmentInfo.RequiredArmorType),
			OutEquipmentActor);
		if (!OutItem)
		{
			return ERequirementResult::Missing;
		}

		if (const FEquipmentFragment* Fragment =
			OutItem->GetItemManifest().GetFragmentOfType<FEquipmentFragment>())
		{
			if (Fragment->GetEquipmentType() != ERPGGladiatorEquipmentType::Count &&
				Fragment->GetEquipmentType() != ERPGGladiatorEquipmentType::Armor)
			{
				return ERequirementResult::WrongType;
			}
		}
		return ERequirementResult::Satisfied;
	}

	if (EquipmentType == ERPGGladiatorEquipmentType::Weapon)
	{
		EEquipmentSlotType ResolvedSlot = EEquipmentSlotType::None;
		if (const URPGEquipComponent* EquipComponent =
			AvatarActor->FindComponentByClass<URPGEquipComponent>())
		{
			EquipComponent->FindEquippedWeapon(
				EquipmentInfo.WeaponHandType, OutItem, OutEquipmentActor, ResolvedSlot);
		}

		if (!OutItem)
		{
			const URPGEquipmentComponent* EquipmentComponent =
				AvatarActor->FindComponentByClass<URPGEquipmentComponent>();
			const URPGEquipComponent* EquipComponent =
				AvatarActor->FindComponentByClass<URPGEquipComponent>();
			TArray<EEquipmentSlotType, TInlineAllocator<4>> CandidateSlots;
			RPGEquipmentAbility::BuildWeaponSlotCandidates(
				EquipmentInfo.WeaponHandType,
				EquipComponent ? EquipComponent->GetCurrentEquipState() : EEquipState::None,
				CandidateSlots);

			for (const EEquipmentSlotType CandidateSlot : CandidateSlots)
			{
				URPGItemBase* CandidateItem = EquipmentComponent
					? EquipmentComponent->GetItemInSlot(CandidateSlot)
					: nullptr;
				if (!CandidateItem)
				{
					continue;
				}

				OutItem = CandidateItem;
				ResolvedSlot = CandidateSlot;
				break;
			}
		}

		if (OutItem)
		{
			const FEquipmentFragment* Fragment =
				OutItem->GetItemManifest().GetFragmentOfType<FEquipmentFragment>();
			if (Fragment)
			{
				if (Fragment->GetEquipmentType() != ERPGGladiatorEquipmentType::Count &&
					Fragment->GetEquipmentType() != ERPGGladiatorEquipmentType::Weapon)
				{
					return ERequirementResult::WrongType;
				}
				if (Fragment->GetWeaponHandType() != EWeaponHandType::Count &&
					Fragment->GetWeaponHandType() != EquipmentInfo.WeaponHandType)
				{
					return ERequirementResult::WrongType;
				}
				if (Fragment->GetWeaponType() != ERPGGladiatorWeaponType::Count &&
					Fragment->GetWeaponType() != EquipmentInfo.RequiredWeaponType)
				{
					return ERequirementResult::WrongType;
				}

				bOutUsedCompatibilityFallback =
					Fragment->GetEquipmentType() == ERPGGladiatorEquipmentType::Count ||
					Fragment->GetWeaponType() == ERPGGladiatorWeaponType::Count;
			}
			else
			{
				bOutUsedCompatibilityFallback = true;
			}
		}

		ARPGWeaponBase* CombatWeapon = nullptr;
		if (const UPawnCombatComponent* CombatComponent =
			AvatarActor->FindComponentByClass<UPawnCombatComponent>())
		{
			CombatWeapon = CombatComponent->GetCharacterCurrentEquippedWeapon();
		}

		if (!OutEquipmentActor && CombatWeapon)
		{
			if (CombatWeapon->GetWeaponType() != ERPGGladiatorWeaponType::Count &&
				CombatWeapon->GetWeaponType() != EquipmentInfo.RequiredWeaponType)
			{
				return ERequirementResult::WrongType;
			}
			if (CombatWeapon->GetWeaponHandType() != EWeaponHandType::Count &&
				CombatWeapon->GetWeaponHandType() != EquipmentInfo.WeaponHandType)
			{
				return ERequirementResult::WrongType;
			}

			OutEquipmentActor = CombatWeapon;
			bOutUsedCompatibilityFallback = true;
		}

		if (!OutItem && !OutEquipmentActor)
		{
			return ERequirementResult::Missing;
		}
		if (bOutUsedCompatibilityFallback && !bAllowIncompleteEquipmentCompatibility)
		{
			return ERequirementResult::Missing;
		}

		return ERequirementResult::Satisfied;
	}

	if (EquipmentType == ERPGGladiatorEquipmentType::Utility)
	{
		const EEquipmentSlotType UtilitySlots[] = {
			EEquipmentSlotType::Utility_1,
			EEquipmentSlotType::Utility_2
		};
		for (const EEquipmentSlotType Slot : UtilitySlots)
		{
			OutItem = RPGEquipmentAbility::GetItemInSlot(
				AvatarActor, Slot, OutEquipmentActor);
			if (OutItem)
			{
				break;
			}
		}

		if (!OutItem)
		{
			return ERequirementResult::Missing;
		}

		const FEquipmentFragment* Fragment =
			OutItem->GetItemManifest().GetFragmentOfType<FEquipmentFragment>();
		if (Fragment)
		{
			if (Fragment->GetEquipmentType() != ERPGGladiatorEquipmentType::Count &&
				Fragment->GetEquipmentType() != ERPGGladiatorEquipmentType::Utility)
			{
				return ERequirementResult::WrongType;
			}
			if (Fragment->GetUtilityType() != ERPGGladiatorUtilityType::Count &&
				Fragment->GetUtilityType() != EquipmentInfo.RequiredUtilityType)
			{
				return ERequirementResult::WrongType;
			}

			bOutUsedCompatibilityFallback =
				Fragment->GetEquipmentType() == ERPGGladiatorEquipmentType::Count ||
				Fragment->GetUtilityType() == ERPGGladiatorUtilityType::Count ||
				!OutEquipmentActor;
		}
		else
		{
			bOutUsedCompatibilityFallback = true;
		}

		if (bOutUsedCompatibilityFallback && !bAllowIncompleteEquipmentCompatibility)
		{
			return ERequirementResult::Missing;
		}

		return ERequirementResult::Satisfied;
	}

	return ERequirementResult::WrongType;
}

AActor* URPGGameplayAbility_Equipment::GetFirstEquipmentActor() const
{
	for (const FRPGGladiatorEquipmentInfo& EquipmentInfo : EquipmentInfos)
	{
		if (EquipmentInfo.EquipmentActor.IsValid())
		{
			return EquipmentInfo.EquipmentActor.Get();
		}
	}
	return nullptr;
}

URPGItemBase* URPGGameplayAbility_Equipment::GetEquipmentItemInstance(
	const AActor* InEquipmentActor) const
{
	if (!InEquipmentActor)
	{
		return nullptr;
	}

	for (const FRPGGladiatorEquipmentInfo& EquipmentInfo : EquipmentInfos)
	{
		if (EquipmentInfo.EquipmentActor.Get() == InEquipmentActor)
		{
			return EquipmentInfo.ItemInstance.Get();
		}
	}
	return nullptr;
}

int32 URPGGameplayAbility_Equipment::GetEquipmentStatValue(const FGameplayTag InStatTag,
	const AActor* InEquipmentActor) const
{
	if (!InStatTag.IsValid())
	{
		return 0;
	}

	if (const URPGItemBase* ItemInstance = GetEquipmentItemInstance(InEquipmentActor))
	{
		if (const FLabeledNumberFragment* StatFragment =
			ItemInstance->GetItemManifest().GetFragmentOfTypeByTag<FLabeledNumberFragment>(InStatTag))
		{
			return FMath::RoundToInt(StatFragment->GetValue());
		}
	}

	return 0;
}
