#pragma once
#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Combat/RPGApprovedHitContext.h"

class UAbilitySystemComponent;
class URPGCombatControlComponent;
struct FGameplayEffectSpec;

/** No RPC/BP/SetByCaller can issue this one-use, exact-context tick permit. */
class FRPGBurnTickGuard final
{
public:
	~FRPGBurnTickGuard();
	static int32 Consume(const FGameplayEffectSpec& Spec, UAbilitySystemComponent* Source, UAbilitySystemComponent* Target);
private:
	friend class URPGCombatControlComponent;
	FRPGBurnTickGuard(const FGameplayEffectSpec& Spec, UAbilitySystemComponent& Source, UAbilitySystemComponent& Target,
		FActiveGameplayEffectHandle Status, const FRPGApprovedHitContext& Lifetime, int32 Damage);
	FRPGBurnTickGuard(const FRPGBurnTickGuard&) = delete;
	FRPGBurnTickGuard& operator=(const FRPGBurnTickGuard&) = delete;
	static thread_local FRPGBurnTickGuard* Current;
	FRPGBurnTickGuard* Previous;
	const FGameplayEffectContext* Context;
	UAbilitySystemComponent* SourceASC;
	UAbilitySystemComponent* TargetASC;
	FActiveGameplayEffectHandle StatusHandle;
	FRPGApprovedHitContext Life;
	int32 TickDamage;
	bool bConsumed = false;
};
