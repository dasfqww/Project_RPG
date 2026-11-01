#include "Tests/RPGCombatControlTestFixture.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/UI/PawnUIComponent.h"
#include "RPGGameplayTags.h"

ARPGStunTestCharacter::ARPGStunTestCharacter()
{
	TestPawnUI = CreateDefaultSubobject<UPawnUIComponent>(TEXT("TestPawnUI"));
}

void ARPGStunTestCharacter::PossessedBy(AController* NewController)
{
	ACharacter::PossessedBy(NewController);
	GetRPGAbilitySystemComponent()->InitAbilityActorInfo(this, this);
}

URPGStunTestCombatAbility::URPGStunTestCombatAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
}

URPGStunTestPassiveAbility::URPGStunTestPassiveAbility()
{
	AbilityActivationPolicy = ERPGAbilityActivationPolicy::OnGiven;
}

URPGStunTestDeathAbility::URPGStunTestDeathAbility()
{
	SetAssetTags(FGameplayTagContainer(RPGGameplayTags::Shared_Ability_Death));
}

bool URPGImmunityTestCombatAbility::RouteLegacy(AActor* Target, const FRPGStaggerSettings& Stagger)
{
	AActor* Source = GetAvatarActorFromActorInfo();
	return TryRouteApprovedHitReaction(FHitResult(Target, nullptr, Target->GetActorLocation(), FVector::ForwardVector),
		ERPGAttackHitReactionPolicy::Legacy, 0.f, FRPGKnockbackSettings(), FRPGSlowSettings(), FRPGApprovedHitContext(Source, Target), FRPGBurnSettings(), Stagger);
}

URPGImmunityTestReactionAbility::URPGImmunityTestReactionAbility()
{
	SetAssetTags(FGameplayTagContainer(RPGGameplayTags::Shared_Ability_HitReact));
	for (FGameplayTag Tag : {RPGGameplayTags::Shared_Ability_HitReact.GetTag(), RPGGameplayTags::Shared_Event_HitReact.GetTag()})
	{
		FAbilityTriggerData Trigger; Trigger.TriggerTag = Tag; Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
		AbilityTriggers.Add(Trigger);
	}
}
