#pragma once

#include "Abilities/GameplayAbility.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "RPGTargetDataSecurityTestFixture.generated.h"

/** Native automation-only fixture; not a new client-callable RPC or BP API. */
UCLASS(Transient, NotBlueprintable)
class URPGTargetDataSecurityTestASC final : public URPGAbilitySystemComponent
{
	GENERATED_BODY()
public:
	void SubmitGeneric(EAbilityGenericReplicatedEvent::Type Event, FGameplayAbilitySpecHandle Handle,
		FPredictionKey Original, FPredictionKey Message)
	{
		ServerSetReplicatedEvent_Implementation(Event, Handle, Original, Message);
	}
	void SubmitGenericPayload(EAbilityGenericReplicatedEvent::Type Event, FGameplayAbilitySpecHandle Handle,
		FPredictionKey Original, FPredictionKey Message, FVector Payload)
	{
		ServerSetReplicatedEventWithPayload_Implementation(Event, Handle, Original, Message, Payload);
	}
	void Submit(FGameplayAbilitySpecHandle Handle, FPredictionKey OriginalKey,
		const FGameplayAbilityTargetDataHandle& Data, FPredictionKey MessageKey)
	{
		ServerSetReplicatedTargetData_Implementation(Handle, OriginalKey, Data, FGameplayTag(), MessageKey);
	}
	void SubmitCancellation(FGameplayAbilitySpecHandle Handle, FPredictionKey OriginalKey, FPredictionKey MessageKey)
	{
		ServerSetReplicatedTargetDataCancelled_Implementation(Handle, OriginalKey, MessageKey);
	}
	bool HasCache(FGameplayAbilitySpecHandle Handle, FPredictionKey OriginalKey) const
	{
		return AbilityTargetDataMap.Find(FGameplayAbilitySpecHandleAndPredictionKey(Handle, OriginalKey)).IsValid();
	}
	void SetRequestCap(int32 Cap) { MaximumTargetDataRequestsPerSecond = Cap; }
	void SubmitAbilityEnd(FGameplayAbilitySpecHandle Handle, FGameplayAbilityActivationInfo ActivationInfo, FPredictionKey MessageKey)
	{
		ServerEndAbility_Implementation(Handle, ActivationInfo, MessageKey);
	}
	void SubmitEngineEndControl(FGameplayAbilitySpecHandle Handle, FGameplayAbilityActivationInfo ActivationInfo, FPredictionKey MessageKey)
	{
		UAbilitySystemComponent::ServerEndAbility_Implementation(Handle, ActivationInfo, MessageKey);
	}
	void SubmitAbilityCancel(FGameplayAbilitySpecHandle Handle, FGameplayAbilityActivationInfo ActivationInfo)
	{
		ServerCancelAbility_Implementation(Handle, ActivationInfo);
	}
};

/** Supplies only policy lifetime/input-task state, not authored damage or montage assets. */
UCLASS(Transient, NotBlueprintable)
class URPGSkillTerminationSecurityTestAbility final : public URPGGameplayAbility_SkillContainer
{
	GENERATED_BODY()
public:
	void ArmRemoteInputWait();
	void Finish();
protected:
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

UCLASS(Transient, NotBlueprintable)
class URPGTargetDataSecurityTestAbility final : public UGameplayAbility
{
	GENERATED_BODY()
public:
	URPGTargetDataSecurityTestAbility();
	void Finish();
protected:
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
};
