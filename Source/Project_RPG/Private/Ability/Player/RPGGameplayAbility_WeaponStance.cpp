#include "Ability/Player/RPGGameplayAbility_WeaponStance.h"

#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Item/Weapon/RPGPlayerWeapon.h"

namespace RPGWeaponStance
{
bool IsNative(const AActor* Avatar)
{
	if (!Avatar) return false;
	const auto* Equipment = Avatar->FindComponentByClass<URPGAuthoritativeEquipmentComponent>();
	const auto* Loadout = Avatar->FindComponentByClass<URPGPlayerWeaponLoadoutComponent>();
	return (Equipment && Equipment->HasWeaponSnapshots()) || (Loadout && Loadout->IsNativeManaged());
}
}

URPGGameplayAbility_WeaponStance::URPGGameplayAbility_WeaponStance()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	bServerRespectsRemoteAbilityCancellation = false;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnlyTermination;
}

bool URPGGameplayAbility_WeaponStance::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!RPGWeaponStance::IsNative(Avatar)) return true;
	const auto* Equipment = Avatar->FindComponentByClass<URPGAuthoritativeEquipmentComponent>();
	if (!Equipment || Equipment->GetActiveEquipState() == DesiredWeaponState) return false;
	if (DesiredWeaponState == EEquipState::None) return true;
	if (DesiredWeaponState != EEquipState::WeaponSet_Primary && DesiredWeaponState != EEquipState::WeaponSet_Secondary) return false;
	const bool bSecondary = DesiredWeaponState == EEquipState::WeaponSet_Secondary;
	for (const EEquipmentSlotType Slot : {
		bSecondary ? EEquipmentSlotType::Weapon_Secondary_L : EEquipmentSlotType::Weapon_Primary_L,
		bSecondary ? EEquipmentSlotType::Weapon_Secondary_R : EEquipmentSlotType::Weapon_Primary_R })
	{
		FRPGEquipmentSnapshot Snapshot;
		// Match the loadout/combat selector: a pending first slot cannot borrow the other hand.
		if (Equipment->FindSnapshotInSlot(Slot, Snapshot))
			return IsValid(Cast<ARPGPlayerWeapon>(Snapshot.EquippedActor));
	}
	return false;
}

void URPGGameplayAbility_WeaponStance::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!RPGWeaponStance::IsNative(Avatar))
	{
		Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
		return;
	}
	// Never execute the legacy Blueprint's attachment, input mapping, or GAS grants.
	if (Avatar->HasAuthority())
	{
		if (ShouldApplyServerActivationRateLimit())
			if (auto* Security = Avatar->FindComponentByClass<URPGSecurityValidationComponent>())
				Security->RecordAbilityActivation(GetClass());
		if (auto* Equipment = Avatar->FindComponentByClass<URPGAuthoritativeEquipmentComponent>())
			Equipment->SetActiveEquipState(DesiredWeaponState);
	}
	if (IsActive()) EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
