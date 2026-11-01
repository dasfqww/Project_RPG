#include "Security/RPGDamageExecutionGuard.h"
#include "Combat/RPGCombatControlTypes.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffect/GE_ExecCalc_TakeDamage.h"
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "Security/RPGSecurityTypes.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "UObject/UnrealType.h"
#if !UE_BUILD_SHIPPING
#include "Tests/RPGSkillNetworkE2EProbeComponent.h"
#endif

thread_local FRPGDamageExecutionGuard* FRPGDamageExecutionGuard::Current = nullptr;

FRPGDamageExecutionGuard::FRPGDamageExecutionGuard(const FGameplayEffectSpec& Spec,
	UAbilitySystemComponent& Source, UAbilitySystemComponent& Target, const float MaximumFinalDamage)
	: Previous(Current), Context(Spec.GetContext().Get()), SourceASC(&Source), TargetASC(&Target), Cap(MaximumFinalDamage)
{
	Current = this;
}
FRPGDamageExecutionGuard::~FRPGDamageExecutionGuard() { Current = Previous; }

bool FRPGDamageExecutionGuard::Supports(const FGameplayEffectSpec& Spec, const UAbilitySystemComponent& Source)
{
	if (!IsInGameThread() || !Spec.Def || !Spec.GetContext().Get() ||
		Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent() != &Source ||
		Spec.Def->DurationPolicy != EGameplayEffectDurationType::Instant || Spec.GetDuration() != 0.0f ||
		!Spec.Def->Modifiers.IsEmpty() || !Spec.Modifiers.IsEmpty() || Spec.Def->Executions.Num() != 1 ||
		Spec.Def->Executions[0].CalculationClass != UGE_ExecCalc_TakeDamage::StaticClass() ||
		!Spec.Def->Executions[0].CalculationModifiers.IsEmpty() ||
		!Spec.Def->Executions[0].ConditionalGameplayEffects.IsEmpty()) return false;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!Spec.TargetEffectSpecs.IsEmpty() || !Spec.Def->ConditionalGameplayEffects.IsEmpty()) return false;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	// Components are protected on UGameplayEffect. Read the reflected array without mutating
	// the authored CDO. Only exact native tag components may accompany the damage execution.
	// The shared damage GE uses target tag requirements to reject invulnerable targets;
	// this native component gates application without adding a damage execution/effect.
	static const FArrayProperty* Property = FindFProperty<FArrayProperty>(UGameplayEffect::StaticClass(), TEXT("GEComponents"));
	if (!Property) return false;
	FScriptArrayHelper Components(Property, Property->ContainerPtrToValuePtr<void>(Spec.Def.Get()));
	const auto* Inner = CastField<FObjectPropertyBase>(Property->Inner);
	if (!Inner) return false;
	for (int32 I = 0; I < Components.Num(); ++I)
	{
		const UObject* Component = Inner->GetObjectPropertyValue(Components.GetRawPtr(I));
		if (!Component || (Component->GetClass() != UAssetTagsGameplayEffectComponent::StaticClass() &&
			Component->GetClass() != UTargetTagsGameplayEffectComponent::StaticClass() &&
			Component->GetClass() != UTargetTagRequirementsGameplayEffectComponent::StaticClass())) return false;
	}
	return true;
}

bool FRPGDamageExecutionGuard::Approve(const FGameplayEffectSpec& Spec, const UAbilitySystemComponent* Source,
	const UAbilitySystemComponent* Target, const double CalculatedDamage, int32& OutDamage)
{
	OutDamage = 0;
	if (!IsInGameThread()) return false;
	FRPGDamageExecutionGuard* Receipt = Current;
	while (Receipt && (Receipt->Context != Spec.GetContext().Get() ||
		Receipt->SourceASC != Source || Receipt->TargetASC != Target)) Receipt = Receipt->Previous;
	if (Current && !Receipt) return false; // Nested direct ASC calls cannot borrow a different permit or fall back to the global cap.
	if (Receipt)
	{
		if (Receipt->bEvaluated) return false; // A callback cannot replay the same permit.
		Receipt->bEvaluated = true;
	}
	// Recheck at execution, not merely at windup/receipt construction. A retained
	// Pawn or already-created damage spec must not outlive its player's authority.
	if ((Source && (IsDungeonGameplayRevoked(Source->GetAvatarActor()) || RPGCombatControl::IsActionBlocked(Source->GetAvatarActor())))
		|| (Target && IsDungeonGameplayRevoked(Target->GetAvatarActor()))) return false;
	const double Limit = Receipt ? Receipt->Cap : FRPGSkillSecurityProfile::MaximumSupportedFinalDamage;
	if (!FMath::IsFinite(Limit) || Limit < 1.0 || Limit > FRPGSkillSecurityProfile::MaximumSupportedFinalDamage ||
		!FMath::IsFinite(CalculatedDamage) || CalculatedDamage < 1.0 || CalculatedDamage > Limit)
	{
#if !UE_BUILD_SHIPPING
		if (Source && Target) URPGSkillNetworkE2EProbeComponent::ObserveFinalCapExecution(
			*Source, *Target, CalculatedDamage, Limit, Receipt != nullptr, false);
#endif
		return false;
	}
	const int32 IntegerDamage = static_cast<int32>(CalculatedDamage);
	// The meta attribute stores a float, then AttributeSet converts it to int again.
	// Rounding that float upward must never cross an authored fractional/non-exact cap.
	if (static_cast<double>(static_cast<float>(IntegerDamage)) > Limit)
	{
#if !UE_BUILD_SHIPPING
		if (Source && Target) URPGSkillNetworkE2EProbeComponent::ObserveFinalCapExecution(
			*Source, *Target, CalculatedDamage, Limit, Receipt != nullptr, false);
#endif
		return false;
	}
	OutDamage = IntegerDamage;
	if (Receipt) Receipt->bApproved = true;
#if !UE_BUILD_SHIPPING
	if (Source && Target) URPGSkillNetworkE2EProbeComponent::ObserveFinalCapExecution(
		*Source, *Target, CalculatedDamage, Limit, Receipt != nullptr, true);
#endif
	return true;
}
