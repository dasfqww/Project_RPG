#include "Component/Combat/RPGCombatControlComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "GameplayEffect/RPGDefenseReductionEffect.h"

namespace
{
	bool DefenseEncounterCurrent(const AActor* Owner, int32 Id)
	{
		const auto* Boss = Owner ? Owner->FindComponentByClass<URPGBossAIComponent>() : nullptr;
		return Boss ? Boss->GetBossState().EncounterId == Id && Boss->GetBossState().EncounterState == ERPGBossEncounterState::InCombat : Id == 0;
	}
}

FActiveGameplayEffectHandle URPGCombatControlComponent::ApplyApprovedDamageDefenseReduction(AActor* Source,
	const FHitResult& Hit, const FGuid& Receipt, const FRPGDefenseReductionSettings& Settings)
{
	auto* Owner = GetOwner();
	if (bEnding || bApplyingDefenseReduction || bClearingDefenseReductions || !IsValid(Owner) || !Owner->HasAuthority()
		|| !IsValid(Source) || !Source->HasAuthority() || Source == Owner || Source->GetWorld() != GetWorld()
		|| Hit.GetActor() != Owner || !Receipt.IsValid() || !Settings.IsEnabled() || !IsLivingAvatar(Source) || !IsLivingAvatar(Owner)) return {};
	auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner);
	InitializeWithAbilitySystem(*ASC);
	const uint32 Epoch = DefenseReductionEpoch;
	const FRPGApprovedHitContext Lifetime(Source, Owner);
	if (RecentDefenseReductionHits.Contains(Receipt)) return {};
	if (RecentDefenseReductionHits.Num() >= 64) RecentDefenseReductionHits.RemoveAt(0);
	RecentDefenseReductionHits.Add(Receipt);
	if (IsImmuneTo(ERPGCombatEffectKind::DefenseReduction) || OwnedDefenseReductions.Num() >= 8) return {};
	if (bEnding || BoundASC.Get() != ASC || DefenseReductionEpoch != Epoch || !Lifetime.IsCurrent(Source, Owner)) return {};
	const auto* Boss = Owner->FindComponentByClass<URPGBossAIComponent>();
	const int32 EncounterId = Boss ? Boss->GetBossState().EncounterId : 0;
	if (!DefenseEncounterCurrent(Owner, EncounterId)) return {};
	const float Defense = ASC->GetNumericAttribute(URPGAttributeSet::GetDefenseAttribute());
	if (!FMath::IsFinite(Defense) || Defense <= 0.f) return {};
	TGuardValue<bool> Applying(bApplyingDefenseReduction, true);
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(Source, Source); Context.AddSourceObject(this); Context.AddHitResult(Hit);
	FGameplayEffectSpec Spec(GetDefault<URPGDefenseReductionEffect>(), Context, 1.f);
	Spec.SetDuration(Settings.Duration, true);
	Spec.SetSetByCallerMagnitude(RPGDefenseReduction::MagnitudeTag(), 1.f - Settings.Percent / 100.f);
	const auto Handle = ASC->ApplyGameplayEffectSpecToSelf(Spec);
	const auto* Effect = ASC->GetActiveGameplayEffect(Handle);
	if (!Handle.IsValid() || !Effect) return {};
	if (bEnding || BoundASC.Get() != ASC || DefenseReductionEpoch != Epoch || !Lifetime.IsCurrent(Source, Owner)
		|| Effect->bIsInhibited || !DefenseEncounterCurrent(Owner, EncounterId))
	{ ASC->RemoveActiveGameplayEffect(Handle); return {}; }
	OwnedDefenseReductions.Add(Handle, EncounterId);
	if (auto* Removed = ASC->OnGameplayEffectRemoved_InfoDelegate(Handle)) Removed->AddUObject(this, &ThisClass::HandleDefenseReductionRemoved);
	RefreshControl();
	return Handle;
}

bool URPGCombatControlComponent::RemoveOwnedDefenseReduction(FActiveGameplayEffectHandle Handle)
{
	return GetOwner() && GetOwner()->HasAuthority() && BoundASC.IsValid() && OwnedDefenseReductions.Contains(Handle)
		&& BoundASC->RemoveActiveGameplayEffect(Handle);
}

void URPGCombatControlComponent::CleanseDefenseReductions() { ClearOwnedDefenseReductions(); }

void URPGCombatControlComponent::ClearOwnedDefenseReductions()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bClearingDefenseReductions) return;
	TGuardValue<bool> Clearing(bClearingDefenseReductions, true);
	++DefenseReductionEpoch;
	auto* ASC = BoundASC.Get();
	TArray<FActiveGameplayEffectHandle> Handles;
	OwnedDefenseReductions.GetKeys(Handles); OwnedDefenseReductions.Reset();
	// Includes a pending application whose callbacks cleanse/reset before Apply returns.
	if (ASC)
	{
		for (auto Handle : ASC->GetActiveEffects(FGameplayEffectQuery()))
		{
			const auto* Effect = ASC->GetActiveGameplayEffect(Handle);
			if (Effect && Effect->Spec.Def == GetDefault<URPGDefenseReductionEffect>() && Effect->Spec.GetContext().GetSourceObject() == this)
				Handles.AddUnique(Handle);
		}
		for (auto Handle : Handles) ASC->RemoveActiveGameplayEffect(Handle);
	}
}

void URPGCombatControlComponent::RefreshDefenseReductions()
{
	if (!GetOwner()->HasAuthority() || !BoundASC.IsValid()) return;
	TArray<FActiveGameplayEffectHandle> Retired;
	for (const auto& Entry : OwnedDefenseReductions)
		if (!DefenseEncounterCurrent(GetOwner(), Entry.Value)) Retired.Add(Entry.Key);
	for (auto Handle : Retired) RemoveOwnedDefenseReduction(Handle);
}

void URPGCombatControlComponent::HandleDefenseReductionRemoved(const FGameplayEffectRemovalInfo& Info)
{
	if (Info.ActiveEffect) OwnedDefenseReductions.Remove(Info.ActiveEffect->Handle);
	RefreshControl();
}
