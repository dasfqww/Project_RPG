#include "Security/RPGBurnTickGuard.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayEffect/RPGBurnEffect.h"
#include "RPGGameplayTags.h"

thread_local FRPGBurnTickGuard* FRPGBurnTickGuard::Current = nullptr;
FRPGBurnTickGuard::FRPGBurnTickGuard(const FGameplayEffectSpec& Spec, UAbilitySystemComponent& Source,
	UAbilitySystemComponent& Target, FActiveGameplayEffectHandle Status, const FRPGApprovedHitContext& Lifetime, int32 Damage)
	: Previous(Current), Context(Spec.GetContext().Get()), SourceASC(&Source), TargetASC(&Target), StatusHandle(Status), Life(Lifetime), TickDamage(Damage)
{ Current = this; }
FRPGBurnTickGuard::~FRPGBurnTickGuard() { Current = Previous; }
int32 FRPGBurnTickGuard::Consume(const FGameplayEffectSpec& Spec, UAbilitySystemComponent* Source, UAbilitySystemComponent* Target)
{
	if (!IsInGameThread() || !Current || Current->bConsumed || Current->Context != Spec.GetContext().Get()
		|| Current->SourceASC != Source || Current->TargetASC != Target) return 0;
	auto& Permit = *Current; Permit.bConsumed = true;
	if (!Source || !Target || Spec.Def != GetDefault<URPGBurnTickEffect>() || Spec.GetPeriod() != 0.f
		|| !Permit.Life.IsCurrent(Source->GetAvatarActor(), Target->GetAvatarActor())
		|| Target->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Invincible)
		|| Permit.TickDamage < 1 || Permit.TickDamage > 10000) return 0;
	const auto* Active = Target->GetActiveGameplayEffect(Permit.StatusHandle);
	if (!Active || Active->bIsInhibited || Active->Spec.Def != GetDefault<URPGBurnEffect>()
		|| Active->GetTimeRemaining(Target->GetWorld()->GetTimeSeconds()) <= 0.f
		|| Active->Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent() != Source) return 0;
	return Permit.TickDamage;
}
