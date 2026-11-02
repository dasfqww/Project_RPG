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
	// Retained as the startup grant-level source for existing assets. Carry commands are retired.
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
