#pragma once

#include "GameplayAbilitySpec.h"

class APlayerController;
class UAbilitySystemComponent;
class URPGAbilitySystemComponent;
class URPGGameplayAbility_SkillContainer;
class URPGSkillDefinition;
class ARPGSkillDamageNetworkTarget;
struct FRPGHitQueryResult;

/** Opt-in authored BP final-cap oracle. No synthetic damage event, damage helper or new RPC.
 * Only a transient definition copy (cap 625) and a lower-defense native target are fixtures. */
class FRPGSkillFinalCapNetworkProbe
{
public:
    ~FRPGSkillFinalCapNetworkProbe();
    void Tick(APlayerController& Controller, URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle);
    bool ReadyForInput() const { return bClientBaseline && bProfilePrepared; }
    bool Passed() const { return bPassed; }
    const FString& Failure() const { return FailureReason; }
    void ObserveExecution(const UAbilitySystemComponent& Source, const UAbilitySystemComponent& Target,
        double Calculated, double Cap, bool bBound, bool bApproved);
    void ObserveApplication(URPGGameplayAbility_SkillContainer& Skill,
        const TArray<FRPGHitQueryResult>& Hits, int32 AppliedCount, int32 BudgetUsed);
private:
    bool PrepareProfile(URPGGameplayAbility_SkillContainer& Skill, URPGAbilitySystemComponent& ASC, bool bServer);
    void Fail(const TCHAR* Reason);
    TWeakObjectPtr<URPGAbilitySystemComponent> SourceASC;
    TWeakObjectPtr<URPGGameplayAbility_SkillContainer> SourceSkill;
    TWeakObjectPtr<URPGSkillDefinition> OriginalDefinition;
    TWeakObjectPtr<ARPGSkillDamageNetworkTarget> RejectedTarget;
    TWeakObjectPtr<ARPGSkillDamageNetworkTarget> AcceptedTarget;
    FDelegateHandle EffectHandle;
    FDelegateHandle HealthHandle;
    FGameplayAbilitySpecHandle ActiveHandle;
    int16 ActiveOriginal = 0;
    int32 AcceptedExecutions = 0;
    int32 RejectedExecutions = 0;
    int32 EffectCallbacks = 0;
    int32 HealthChanges = 0;
    float OriginalCap = 0;
    double EndedAt = -1;
    bool bProfilePrepared = false;
    bool bClientBaseline = false;
    bool bObservedActive = false;
    bool bObservedApplication = false;
    bool bPassed = false;
    FString FailureReason;
};
