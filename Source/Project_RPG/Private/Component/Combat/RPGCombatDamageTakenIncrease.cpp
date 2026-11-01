#include "Component/Combat/RPGCombatControlComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "GameplayEffect/RPGDamageTakenIncreaseEffect.h"

namespace
{
	bool DamageTakenEncounterCurrent(const AActor* Owner, int32 Id)
	{
		const auto* Boss = Owner ? Owner->FindComponentByClass<URPGBossAIComponent>() : nullptr;
		return Boss ? Boss->GetBossState().EncounterId == Id && Boss->GetBossState().EncounterState == ERPGBossEncounterState::InCombat : Id == 0;
	}
}

FActiveGameplayEffectHandle URPGCombatControlComponent::ApplyApprovedDamageTakenIncrease(AActor* Source,
	const FHitResult& Hit, const FGuid& Receipt, const FRPGDamageTakenIncreaseSettings& Settings)
{
	auto* Owner = GetOwner();
	if (bEnding || bApplyingDamageTakenIncrease || bClearingDamageTakenIncreases || !IsValid(Owner) || !Owner->HasAuthority()
		|| !IsValid(Source) || !Source->HasAuthority() || Source == Owner || Source->GetWorld() != GetWorld()
		|| Hit.GetActor() != Owner || !Receipt.IsValid() || !Settings.IsEnabled() || !IsLivingAvatar(Source) || !IsLivingAvatar(Owner)) return {};
	auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner);
	InitializeWithAbilitySystem(*ASC);
	const uint32 Epoch = DamageTakenIncreaseEpoch;
	const FRPGApprovedHitContext Lifetime(Source, Owner);
	if (RecentDamageTakenIncreaseHits.Contains(Receipt)) return {};
	if (RecentDamageTakenIncreaseHits.Num() >= 64) RecentDamageTakenIncreaseHits.RemoveAt(0);
	RecentDamageTakenIncreaseHits.Add(Receipt);
	if (IsImmuneTo(ERPGCombatEffectKind::DamageTakenIncrease) || OwnedDamageTakenIncreases.Num() >= 8) return {};
	if (bEnding || BoundASC.Get() != ASC || DamageTakenIncreaseEpoch != Epoch || !Lifetime.IsCurrent(Source, Owner)) return {};
	const auto* Boss = Owner->FindComponentByClass<URPGBossAIComponent>();
	const int32 EncounterId = Boss ? Boss->GetBossState().EncounterId : 0;
	if (!DamageTakenEncounterCurrent(Owner, EncounterId)) return {};
	TGuardValue<bool> Applying(bApplyingDamageTakenIncrease, true);
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(Source, Source); Context.AddSourceObject(this); Context.AddHitResult(Hit);
	FGameplayEffectSpec Spec(GetDefault<URPGDamageTakenIncreaseEffect>(), Context, 1.f);
	Spec.SetDuration(Settings.Duration, true);
	Spec.SetSetByCallerMagnitude(RPGDamageTakenIncrease::MagnitudeTag(), Settings.Percent);
	const auto Handle = ASC->ApplyGameplayEffectSpecToSelf(Spec);
	const auto* Effect = ASC->GetActiveGameplayEffect(Handle);
	if (!Handle.IsValid() || !Effect) return {};
	if (bEnding || BoundASC.Get() != ASC || DamageTakenIncreaseEpoch != Epoch || !Lifetime.IsCurrent(Source, Owner)
		|| Effect->bIsInhibited || !DamageTakenEncounterCurrent(Owner, EncounterId))
	{ ASC->RemoveActiveGameplayEffect(Handle); return {}; }
	OwnedDamageTakenIncreases.Add(Handle, EncounterId);
	if (auto* Removed = ASC->OnGameplayEffectRemoved_InfoDelegate(Handle)) Removed->AddUObject(this, &ThisClass::HandleDamageTakenIncreaseRemoved);
	RefreshControl();
	return Handle;
}

bool URPGCombatControlComponent::RemoveOwnedDamageTakenIncrease(FActiveGameplayEffectHandle Handle)
{
	return GetOwner() && GetOwner()->HasAuthority() && BoundASC.IsValid() && OwnedDamageTakenIncreases.Contains(Handle)
		&& BoundASC->RemoveActiveGameplayEffect(Handle);
}

void URPGCombatControlComponent::CleanseDamageTakenIncreases() { ClearOwnedDamageTakenIncreases(); }

void URPGCombatControlComponent::ClearOwnedDamageTakenIncreases()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bClearingDamageTakenIncreases) return;
	TGuardValue<bool> Clearing(bClearingDamageTakenIncreases, true);
	++DamageTakenIncreaseEpoch;
	auto* ASC = BoundASC.Get();
	TArray<FActiveGameplayEffectHandle> Handles;
	OwnedDamageTakenIncreases.GetKeys(Handles); OwnedDamageTakenIncreases.Reset();
	// Includes a pending application whose callbacks cleanse/reset before Apply returns.
	if (ASC)
	{
		for (auto Handle : ASC->GetActiveEffects(FGameplayEffectQuery()))
		{
			const auto* Effect = ASC->GetActiveGameplayEffect(Handle);
			if (Effect && Effect->Spec.Def == GetDefault<URPGDamageTakenIncreaseEffect>() && Effect->Spec.GetContext().GetSourceObject() == this)
				Handles.AddUnique(Handle);
		}
		for (auto Handle : Handles) ASC->RemoveActiveGameplayEffect(Handle);
	}
}

void URPGCombatControlComponent::RefreshDamageTakenIncreases()
{
	if (!GetOwner()->HasAuthority() || !BoundASC.IsValid()) return;
	TArray<FActiveGameplayEffectHandle> Retired;
	for (const auto& Entry : OwnedDamageTakenIncreases)
		if (!DamageTakenEncounterCurrent(GetOwner(), Entry.Value)) Retired.Add(Entry.Key);
	for (auto Handle : Retired) RemoveOwnedDamageTakenIncrease(Handle);
}

void URPGCombatControlComponent::HandleDamageTakenIncreaseRemoved(const FGameplayEffectRemovalInfo& Info)
{
	if (Info.ActiveEffect) OwnedDamageTakenIncreases.Remove(Info.ActiveEffect->Handle);
	RefreshControl();
}

double URPGCombatControlComponent::GetDamageTakenMultiplier() const
{
	const auto* Owner = GetOwner();
	if (bEnding || !Owner || !Owner->HasAuthority() || !BoundASC.IsValid() || !IsLivingAvatar(Owner)
		|| BoundRecoveryRevision != RecoveryRevision() || BoundASC->GetAvatarActor() != Owner) return 1.f;
	double Multiplier = 1.0;
	for (const auto& Entry : OwnedDamageTakenIncreases)
	{
		if (!DamageTakenEncounterCurrent(Owner, Entry.Value)) continue;
		const auto* Effect = BoundASC->GetActiveGameplayEffect(Entry.Key);
		if (!Effect || Effect->bIsInhibited || Effect->Spec.Def != GetDefault<URPGDamageTakenIncreaseEffect>()
			|| Effect->Spec.GetContext().GetSourceObject() != this) continue;
		const float Percent = Effect->Spec.GetSetByCallerMagnitude(RPGDamageTakenIncrease::MagnitudeTag(), false, 0.f);
		if (FMath::IsFinite(Percent) && Percent >= 1.f && Percent <= 100.f) Multiplier *= 1.0 + static_cast<double>(Percent) / 100.0;
	}
	return Multiplier; // At most eight validated [1,2] factors; no separate gameplay state.
}
