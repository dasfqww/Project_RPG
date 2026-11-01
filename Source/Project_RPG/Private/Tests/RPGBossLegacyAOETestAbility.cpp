#include "Tests/RPGBossLegacyAOETestAbility.h"
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "RPGGameplayTags.h"

URPGBossLegacyAOETestAbility::URPGBossLegacyAOETestAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	bUseLegacyManualManaCost = false;
	EventTag = RPGGameplayTags::Player_Event_AOE;
	AOETraceType = EAOETraceType::Sphere;
	SkillDamage = 10.f;
	DamageEffectClass = URPGSkillDamageSecurityTestEffect::StaticClass();
}

void URPGBossLegacyAOETestAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	Super::ActivateAbility(Handle, Info, Activation, Event);
	if (IsActive()) ExecWaitGameplayEvent();
#else
	EndAbility(Handle, Info, Activation, false, true);
#endif
}

void URPGBossLegacyAOETestAbility::Finish()
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
#endif
}

void URPGBossLegacyAOETestAbility::DispatchAOE()
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	FGameplayEventData Event;
	Event.EventTag = RPGGameplayTags::Player_Event_AOE;
	HandleApplyAOEDamage(Event);
#endif
}
