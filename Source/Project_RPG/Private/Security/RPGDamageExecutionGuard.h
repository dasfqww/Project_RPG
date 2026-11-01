#pragma once

#include "CoreMinimal.h"

class UAbilitySystemComponent;
struct FGameplayEffectSpec;
struct FGameplayEffectContext;

/** Native, synchronous server receipt. No client/BP/SetByCaller field can author the cap.
 * GAS copies specs but retains their context handle during instant execution. Bind both ASCs
 * and that context, support only a single native damage execution, and restore nested scopes.
 * Not a permit for delayed/periodic effects or arbitrary direct ASC application outside this API. */
class FRPGDamageExecutionGuard final
{
public:
	FRPGDamageExecutionGuard(const FGameplayEffectSpec& Spec, UAbilitySystemComponent& Source,
		UAbilitySystemComponent& Target, float MaximumFinalDamage);
	~FRPGDamageExecutionGuard();
	FRPGDamageExecutionGuard(const FRPGDamageExecutionGuard&) = delete;
	FRPGDamageExecutionGuard& operator=(const FRPGDamageExecutionGuard&) = delete;
	bool WasApproved() const { return bApproved; }
	static bool Supports(const FGameplayEffectSpec& Spec, const UAbilitySystemComponent& Source);
	/** Validate before integer conversion and before reward/presentation/output modifiers. */
	static bool Approve(const FGameplayEffectSpec& Spec, const UAbilitySystemComponent* Source,
		const UAbilitySystemComponent* Target, double CalculatedDamage, int32& OutDamage);
private:
	static thread_local FRPGDamageExecutionGuard* Current;
	FRPGDamageExecutionGuard* Previous = nullptr;
	const FGameplayEffectContext* Context = nullptr;
	const UAbilitySystemComponent* SourceASC = nullptr;
	const UAbilitySystemComponent* TargetASC = nullptr;
	const float Cap;
	bool bEvaluated = false;
	bool bApproved = false;
};
