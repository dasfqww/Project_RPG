#include "Ability/Player/RPGGameplayAbility_Equipment.h"

#include "Attribute/RPGAttributeSet.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Item/Weapon/RPGWeaponBase.h"
#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGGameplayAbility_Equipment)

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Ability_ActivateFail_MissingEquipment,
	"Ability.ActivateFail.MissingEquipment");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Ability_ActivateFail_WrongEquipment,
	"Ability.ActivateFail.WrongEquipment");

namespace RPGEquipmentAbility
{
	ERPGGladiatorEquipmentType GetEffectiveEquipmentType(const FRPGGladiatorEquipmentInfo& Info)
	{
		if (Info.EquipmentType != ERPGGladiatorEquipmentType::Count) return Info.EquipmentType;
		if (Info.RequiredArmorType != ERPGGladiatorArmorType::Count) return ERPGGladiatorEquipmentType::Armor;
		if (Info.WeaponHandType != EWeaponHandType::Count ||
			Info.RequiredWeaponType != ERPGGladiatorWeaponType::Count) return ERPGGladiatorEquipmentType::Weapon;
		if (Info.RequiredUtilityType != ERPGGladiatorUtilityType::Count) return ERPGGladiatorEquipmentType::Utility;
		return ERPGGladiatorEquipmentType::Count;
	}

	EEquipmentSlotType GetArmorSlot(const ERPGGladiatorArmorType Type)
	{
		switch (Type)
		{
		case ERPGGladiatorArmorType::Helmet: return EEquipmentSlotType::Head;
		case ERPGGladiatorArmorType::Chest: return EEquipmentSlotType::Chest;
		case ERPGGladiatorArmorType::Legs: return EEquipmentSlotType::Legs;
		case ERPGGladiatorArmorType::Hands: return EEquipmentSlotType::Hands;
		case ERPGGladiatorArmorType::Foot: return EEquipmentSlotType::Feet;
		default: return EEquipmentSlotType::None;
		}
	}

	bool HasKnownMismatch(const ERPGGladiatorWeaponType Actual, const ERPGGladiatorWeaponType Required)
	{
		return Actual != ERPGGladiatorWeaponType::Count && Required != ERPGGladiatorWeaponType::Count
			&& Actual != Required;
	}
}

URPGGameplayAbility_Equipment::URPGGameplayAbility_Equipment(const FObjectInitializer& ObjectInitializer)
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
	bUsedCompatibilityFallback = false;
	for (FRPGGladiatorEquipmentInfo& Info : EquipmentInfos)
	{
		Info.EquipmentActor.Reset();
		Info.ItemInstance.Reset();
		Info.Snapshot = {};
	}
	for (FRPGGladiatorEquipmentInfo& Info : EquipmentInfos)
	{
		bool bFallback = false;
		const ERequirementResult Result = ResolveEquipmentRequirement(
			ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr, Info, Info.Snapshot, bFallback);
		if (Result != ERequirementResult::Satisfied)
		{
			for (FRPGGladiatorEquipmentInfo& Previous : EquipmentInfos)
			{
				Previous.Snapshot = {};
				Previous.EquipmentActor.Reset();
			}
			bUsedCompatibilityFallback = false;
			CancelAbility(Handle, ActorInfo, ActivationInfo, true);
			return;
		}
		Info.EquipmentActor = Info.Snapshot.EquippedActor;
		bUsedCompatibilityFallback |= bFallback;
	}
	SnapshottedAttackRate = DefaultAttackRate;
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		if (const URPGAttributeSet* Attributes = ActorInfo->AbilitySystemComponent->GetSet<URPGAttributeSet>())
		{
			SnapshottedAttackRate = FMath::Max(0.f, DefaultAttackRate *
				(1.f + Attributes->GetAttackSpeedPercent() / 100.f));
		}
	}
	// Blueprint activation sees the current invocation's snapshots, never the previous invocation.
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

bool URPGGameplayAbility_Equipment::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!Avatar)
	{
		if (OptionalRelevantTags) OptionalRelevantTags->AddTag(TAG_Ability_ActivateFail_MissingEquipment);
		return false;
	}
	for (const FRPGGladiatorEquipmentInfo& Info : EquipmentInfos)
	{
		FRPGEquipmentSnapshot Snapshot;
		bool bFallback = false;
		const ERequirementResult Result = ResolveEquipmentRequirement(Avatar, Info, Snapshot, bFallback);
		if (Result == ERequirementResult::Satisfied) continue;
		if (OptionalRelevantTags) OptionalRelevantTags->AddTag(Result == ERequirementResult::WrongType
			? TAG_Ability_ActivateFail_WrongEquipment : TAG_Ability_ActivateFail_MissingEquipment);
		return false;
	}
	return true;
}

URPGGameplayAbility_Equipment::ERequirementResult
URPGGameplayAbility_Equipment::ResolveEquipmentRequirement(const AActor* Avatar,
	const FRPGGladiatorEquipmentInfo& Info, FRPGEquipmentSnapshot& OutSnapshot,
	bool& bOutUsedCompatibilityFallback) const
{
	OutSnapshot = {};
	bOutUsedCompatibilityFallback = false;
	if (!Avatar) return ERequirementResult::Missing;
	const URPGAuthoritativeEquipmentComponent* Equipment =
		Avatar->FindComponentByClass<URPGAuthoritativeEquipmentComponent>();
	const ERPGGladiatorEquipmentType Type = RPGEquipmentAbility::GetEffectiveEquipmentType(Info);
	if (Type == ERPGGladiatorEquipmentType::Armor)
	{
		return Equipment && Equipment->FindSnapshotInSlot(
			RPGEquipmentAbility::GetArmorSlot(Info.RequiredArmorType), OutSnapshot)
			? ERequirementResult::Satisfied : ERequirementResult::Missing;
	}
	if (Type == ERPGGladiatorEquipmentType::Utility)
	{
		if (!Equipment) return ERequirementResult::Missing;
		for (const EEquipmentSlotType Slot : { EEquipmentSlotType::Utility_1, EEquipmentSlotType::Utility_2 })
		{
			FRPGEquipmentSnapshot Candidate;
			if (!Equipment->FindSnapshotInSlot(Slot, Candidate)) continue;
			if (Info.RequiredUtilityType != ERPGGladiatorUtilityType::Count
				&& Candidate.UtilityType != ERPGGladiatorUtilityType::Count
				&& Candidate.UtilityType != Info.RequiredUtilityType) continue;
			const bool bFallback = Candidate.UtilityType == ERPGGladiatorUtilityType::Count
				&& Info.RequiredUtilityType != ERPGGladiatorUtilityType::Count;
			if (bFallback && !bAllowIncompleteEquipmentCompatibility) continue;
			OutSnapshot = MoveTemp(Candidate);
			bOutUsedCompatibilityFallback = bFallback;
			return ERequirementResult::Satisfied;
		}
		return ERequirementResult::Missing;
	}
	if (Type != ERPGGladiatorEquipmentType::Weapon) return ERequirementResult::WrongType;

	if (Equipment && Equipment->FindWeaponSnapshot(Info.WeaponHandType, OutSnapshot))
	{
		if (RPGEquipmentAbility::HasKnownMismatch(OutSnapshot.WeaponType, Info.RequiredWeaponType))
			return ERequirementResult::WrongType;
		const bool bFallback =
			(OutSnapshot.WeaponType == ERPGGladiatorWeaponType::Count &&
				Info.RequiredWeaponType != ERPGGladiatorWeaponType::Count) ||
			(OutSnapshot.WeaponHandType == EWeaponHandType::Count && Info.WeaponHandType != EWeaponHandType::Count);
		// A confirmed native item with a pending/missing actor must never borrow a startup weapon actor.
		if (!IsValid(OutSnapshot.EquippedActor) || (bFallback && !bAllowIncompleteEquipmentCompatibility))
			return ERequirementResult::Missing;
		// The item is authoritative, but explicit contradictory actor authoring is still rejected.
		if (const ARPGWeaponBase* Weapon = Cast<ARPGWeaponBase>(OutSnapshot.EquippedActor))
		{
			if (RPGEquipmentAbility::HasKnownMismatch(Weapon->GetWeaponType(), Info.RequiredWeaponType)
				|| (Weapon->GetWeaponHandType() != EWeaponHandType::Count
					&& Info.WeaponHandType != EWeaponHandType::Count
					&& Weapon->GetWeaponHandType() != Info.WeaponHandType))
				return ERequirementResult::WrongType;
		}
		bOutUsedCompatibilityFallback = bFallback;
		return ERequirementResult::Satisfied;
	}
	if (Equipment && Equipment->HasWeaponSnapshots()) return ERequirementResult::Missing;

	// Narrow actor-only compatibility for existing class startup loadouts, not legacy item lists.
	const UPawnCombatComponent* Combat = Avatar->FindComponentByClass<UPawnCombatComponent>();
	ARPGWeaponBase* Weapon = Combat ? Combat->GetCharacterCurrentEquippedWeapon() : nullptr;
	if (!Weapon) return ERequirementResult::Missing;
	if (RPGEquipmentAbility::HasKnownMismatch(Weapon->GetWeaponType(), Info.RequiredWeaponType)
		|| (Weapon->GetWeaponHandType() != EWeaponHandType::Count && Info.WeaponHandType != EWeaponHandType::Count
			&& Weapon->GetWeaponHandType() != Info.WeaponHandType))
		return ERequirementResult::WrongType;
	if (!bAllowIncompleteEquipmentCompatibility) return ERequirementResult::Missing;
	OutSnapshot.EquippedActor = Weapon;
	bOutUsedCompatibilityFallback = true;
	return ERequirementResult::Satisfied;
}

AActor* URPGGameplayAbility_Equipment::GetFirstEquipmentActor() const
{
	for (const FRPGGladiatorEquipmentInfo& Info : EquipmentInfos)
	{
		if (Info.EquipmentActor.IsValid()) return Info.EquipmentActor.Get();
	}
	return nullptr;
}

URPGItemBase* URPGGameplayAbility_Equipment::GetEquipmentItemInstance(const AActor*) const
{
	return nullptr; // Serialized Blueprint signature only; do not create a mutable legacy mirror.
}

FGuid URPGGameplayAbility_Equipment::GetEquipmentItemId(const AActor* Actor) const
{
	if (!Actor) return {};
	for (const FRPGGladiatorEquipmentInfo& Info : EquipmentInfos)
	{
		if (Info.EquipmentActor.Get() == Actor) return Info.Snapshot.ItemId;
	}
	return {};
}

float URPGGameplayAbility_Equipment::GetEquipmentStatValueByItemId(const FGameplayTag Tag,
	const FGuid ItemId) const
{
	if (!ItemId.IsValid()) return 0.f;
	for (const FRPGGladiatorEquipmentInfo& Info : EquipmentInfos)
	{
		if (Info.Snapshot.ItemId == ItemId) return Info.Snapshot.GetStatValue(Tag);
	}
	return 0.f;
}

int32 URPGGameplayAbility_Equipment::GetEquipmentStatValue(const FGameplayTag Tag, const AActor* Actor) const
{
	const float Value = GetEquipmentStatValueByItemId(Tag, GetEquipmentItemId(Actor));
	if (static_cast<double>(Value) >= static_cast<double>(MAX_int32)) return MAX_int32;
	if (static_cast<double>(Value) <= static_cast<double>(MIN_int32)) return MIN_int32;
	return FMath::RoundToInt(Value);
}
