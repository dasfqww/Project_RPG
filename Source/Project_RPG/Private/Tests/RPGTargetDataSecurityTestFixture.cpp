#include "Tests/RPGTargetDataSecurityTestFixture.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Skill/RPGSkillExecutionPolicy.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGTargetDataSecurityTestFixture)

URPGTargetDataSecurityTestAbility::URPGTargetDataSecurityTestAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

bool URPGTargetDataSecurityTestAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
#else
	return false;
#endif
}

void URPGTargetDataSecurityTestAbility::Finish()
{
	if (IsActive()) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
}

bool URPGSkillTerminationSecurityTestAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	return UGameplayAbility::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
#else
	return false;
#endif
}

void URPGSkillTerminationSecurityTestAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	UGameplayAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ActiveSkillSpec.ExecutionPolicyClass = URPGSkillExecutionPolicy_Instant::StaticClass();
	ActiveExecutionPolicy = NewObject<URPGSkillExecutionPolicy_Instant>(this);
	ActiveExecutionPolicy->Initialize(*this);
}

void URPGSkillTerminationSecurityTestAbility::ArmRemoteInputWait()
{
	ActiveInputPressedTask = UAbilityTask_WaitInputPress::WaitInputPress(this, false);
	ActiveInputPressedTask->ReadyForActivation();
	ActiveInputPressedTask->SetWaitingOnRemotePlayerData();
}

void URPGSkillTerminationSecurityTestAbility::Finish()
{
	if (IsActive()) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
}
