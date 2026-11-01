#include "Tests/RPGSkillDamageSecurityTestFixture.h"

#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/UI/PawnUIComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameplayEffect/GE_ExecCalc_TakeDamage.h"
#include "RPGGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillDamageSecurityTestFixture)

ARPGSkillDamageSecurityTestPawn::ARPGSkillDamageSecurityTestPawn()
{
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("TestCapsule"));
	SetRootComponent(Capsule);
	Capsule->InitCapsuleSize(20.0f, 60.0f);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Overlap);
	TestASC = CreateDefaultSubobject<URPGAbilitySystemComponent>(TEXT("TestASC"));
	TestAttributes = CreateDefaultSubobject<URPGAttributeSet>(TEXT("TestAttributes"));
	TestUI = CreateDefaultSubobject<UPawnUIComponent>(TEXT("TestUI"));
}

void ARPGSkillDamageSecurityTestPawn::BeginPlay()
{
	Super::BeginPlay();
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	TestASC->InitAbilityActorInfo(this, this);
	TestASC->AddAttributeSetSubobject(TestAttributes.Get());
	TestAttributes->SetMaxHealth(1000.0f);
	TestAttributes->SetCurrentHealth(1000.0f);
	TestAttributes->SetAttack(1.0f);
	TestAttributes->SetDefense(1.0f);
	TestAttributes->SetCriticalChance(0.0f);
	TestASC->AddLooseGameplayTag(RPGGameplayTags::Player_Status_Rage_Active);
#endif
}

UAbilitySystemComponent* ARPGSkillDamageSecurityTestPawn::GetAbilitySystemComponent() const { return TestASC; }
UPawnUIComponent* ARPGSkillDamageSecurityTestPawn::GetPawnUIComponent() const { return TestUI; }

URPGSkillDamageSecurityTestEffect::URPGSkillDamageSecurityTestEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition Execution;
	Execution.CalculationClass = UGE_ExecCalc_TakeDamage::StaticClass();
	Executions.Add(Execution);
}

void URPGSkillDamageSecurityTestAbility::Configure(const FRPGSkillRuntimeSpec& Spec, const FGameplayTag DamageEvent)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	ActiveSkillSpec = Spec;
	ActiveTargetResult.bIsValid = true;
	ActiveTargetResult.HitQueryTransform = FTransform::Identity;
	ActiveTargetResult.SourceLocation = FVector::ZeroVector;
	ActiveTargetResult.TargetLocation = FVector(500.0, 0.0, 0.0);
	ActiveTargetResult.AimDirection = FVector::ForwardVector;
	EventTag = DamageEvent;
	DamageEffectClass = URPGSkillDamageSecurityTestEffect::StaticClass();
#endif
}

URPGSkillDamageSecurityExtraModifierEffect::URPGSkillDamageSecurityExtraModifierEffect()
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition Execution;
	Execution.CalculationClass = UGE_ExecCalc_TakeDamage::StaticClass();
	Executions.Add(Execution);
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = URPGAttributeSet::GetCurrentHealthAttribute();
	Modifier.ModifierOp = EGameplayModOp::Override;
	Modifier.ModifierMagnitude = FScalableFloat(0.0f);
	Modifiers.Add(Modifier);
#endif
}

bool URPGSkillDamageSecurityTestAbility::CanActivateAbility(FGameplayAbilitySpecHandle,
	const FGameplayAbilityActorInfo*, const FGameplayTagContainer*, const FGameplayTagContainer*, FGameplayTagContainer*) const
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	return true;
#else
	return false;
#endif
}

void URPGSkillDamageSecurityTestAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	++SkillDamageActivationSerial;
	ServerAppliedHitCount = 0;
	CaptureSkillBossEncounters(Info ? Cast<APawn>(Info->AvatarActor.Get()) : nullptr);
	UGameplayAbility::ActivateAbility(Handle, Info, Activation, Event);
	ExecWaitGameplayEvent();
#endif
}

void URPGSkillDamageSecurityTestAbility::Finish()
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
#endif
}
