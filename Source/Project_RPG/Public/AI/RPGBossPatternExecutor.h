#pragma once

#include "AI/RPGBossPatternDefinition.h"
#include "GameplayAbilitySpecHandle.h"
#include "RPGBossPatternExecutor.generated.h"

class UAbilitySystemComponent;
class URPGBossAIComponent;
struct FAbilityEndedData;

/** Server-only pattern orchestration, owned by BossAI. GAS owns execution/costs/cooldowns. */
UCLASS()
class PROJECT_RPG_API URPGBossPatternExecutor : public UObject
{
	GENERATED_BODY()
public:
	FGameplayAbilitySpecHandle Select(FGameplayTag& OutPatternId);
	bool CanActivate(FGameplayTag PatternId, int32 ExpectedRevision);
	bool TryActivate(FGameplayTag PatternId, int32 ExpectedRevision);
	bool CaptureExecution(FGameplayAbilitySpecHandle Handle, URPGBossGameplayAbility* Ability, FRPGBossPatternExecution& Out);
	const URPGBossPatternSettings* GetPendingSettings(FGameplayAbilitySpecHandle Handle) const;
	void Reset();

private:
	URPGBossAIComponent* GetBoss() const;
	UAbilitySystemComponent* GetASC() const;
	bool IsCurrent(int32 Revision) const;
	const FRPGBossPatternDefinition* Find(FGameplayTag PatternId) const;
	bool Query(const FRPGBossPatternDefinition& Entry, int32 Revision, FGameplayAbilitySpecHandle& OutHandle);
	void OnAbilityEnded(const FAbilityEndedData& Data);

	UPROPERTY(Transient) FRPGBossPatternDefinition Pending;
	FGameplayAbilitySpecHandle PendingHandle;
	int32 PendingRevision = INDEX_NONE;
	bool bQuerying = false;
	bool bLaunching = false;
	bool bConsumed = false;
	TWeakObjectPtr<UAbilitySystemComponent> ActiveASC;
	TWeakObjectPtr<URPGBossGameplayAbility> ActiveAbility;
	FGameplayAbilitySpecHandle ActiveHandle;
	FDelegateHandle EndedDelegate;
	uint64 ActiveSerial = 0;
	int32 ActiveEncounter = 0;
	int32 ActivePhase = INDEX_NONE;
	int32 ActiveSequenceIndex = INDEX_NONE;
	int32 NextSequenceIndex = 0;
};
