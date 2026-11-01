#include "Component/Combat/RPGCombatControlComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayEffect/RPGBurnEffect.h"
#include "Security/RPGBurnTickGuard.h"
#include "UI/Combat/RPGBurnStatusWidget.h"

namespace
{
	int32 Encounter(const AActor* Actor)
	{
		const auto* Boss = IsValid(Actor) ? Actor->FindComponentByClass<URPGBossAIComponent>() : nullptr;
		return Boss ? Boss->GetBossState().EncounterId : 0;
	}
	bool EncounterCurrent(const AActor* Actor, int32 Id)
	{
		const auto* Boss = IsValid(Actor) ? Actor->FindComponentByClass<URPGBossAIComponent>() : nullptr;
		return Boss ? Boss->GetBossState().EncounterId == Id && Boss->GetBossState().EncounterState == ERPGBossEncounterState::InCombat : Id == 0;
	}
}

FActiveGameplayEffectHandle URPGCombatControlComponent::ApplyApprovedDamageBurn(AActor* Source, const FHitResult& Hit,
	const FGuid& Receipt, const FRPGBurnSettings& Settings)
{
	if (bEnding || bApplyingBurn || bClearingBurns || !GetOwner() || !GetOwner()->HasAuthority() || !IsValid(Source)
		|| !Source->HasAuthority() || Source == GetOwner() || Source->GetWorld() != GetWorld() || Hit.GetActor() != GetOwner()
		|| !Receipt.IsValid() || !Settings.IsEnabled() || !IsLivingAvatar(Source) || !IsLivingAvatar(GetOwner())) return {};
	auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	InitializeWithAbilitySystem(*ASC);
	if (RecentBurnHits.Contains(Receipt)) return {};
	if (RecentBurnHits.Num() >= 64) RecentBurnHits.RemoveAt(0);
	RecentBurnHits.Add(Receipt);
	if (IsImmuneTo(ERPGCombatEffectKind::Burn) || OwnedBurns.Num() >= 8) return {};
	TGuardValue<bool> Applying(bApplyingBurn, true);
	const uint32 Epoch = BurnEpoch;
	FBurnLease Lease; Lease.Source = Source; Lease.SourceASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Source);
	if (auto* Pawn = Cast<APawn>(Source)) Lease.SourceController = Pawn->GetController();
	Lease.Lifetime = FRPGApprovedHitContext(Source, GetOwner()); Lease.Settings = Settings;
	Lease.EncounterId = Encounter(GetOwner()); Lease.SourceEncounterId = Encounter(Source);
	if (!EncounterCurrent(GetOwner(), Lease.EncounterId) || !EncounterCurrent(Source, Lease.SourceEncounterId)) return {};
	auto Context = Lease.SourceASC->MakeEffectContext(); Context.AddSourceObject(this);
	FGameplayEffectSpec Spec(GetDefault<URPGBurnEffect>(), Context, 1.f); Spec.SetDuration(Settings.Duration, true);
	const auto Handle = ASC->ApplyGameplayEffectSpecToSelf(Spec);
	const auto* Active = ASC->GetActiveGameplayEffect(Handle);
	if (!Active || !Handle.IsValid()) return {};
	if (bEnding || Epoch != BurnEpoch || BoundASC.Get() != ASC || Active->bIsInhibited || !Lease.Lifetime.IsCurrent(Source, GetOwner())
		|| !EncounterCurrent(GetOwner(), Lease.EncounterId) || !EncounterCurrent(Source, Lease.SourceEncounterId)
		|| (!Lease.SourceController.IsExplicitlyNull() && !Lease.SourceController.IsValid())
		|| (Cast<APawn>(Source) && Cast<APawn>(Source)->GetController() != Lease.SourceController.Get()))
	{ ASC->RemoveActiveGameplayEffect(Handle); return {}; }
	Lease.NextTick = GetWorld()->GetTimeSeconds() + Settings.Interval;
	OwnedBurns.Add(Handle, Lease);
	if (auto* Removed = ASC->OnGameplayEffectRemoved_InfoDelegate(Handle)) Removed->AddUObject(this, &ThisClass::HandleBurnRemoved);
	if (auto* Inhibited = ASC->OnGameplayEffectInhibitionChangedDelegate(Handle)) Inhibited->AddUObject(this, &ThisClass::HandleBurnInhibited);
	RefreshControl(); return Handle;
}

bool URPGCombatControlComponent::RemoveOwnedBurn(FActiveGameplayEffectHandle Handle)
{
	return GetOwner() && GetOwner()->HasAuthority() && BoundASC.IsValid() && OwnedBurns.Contains(Handle)
		&& BoundASC->RemoveActiveGameplayEffect(Handle);
}
void URPGCombatControlComponent::CleanseBurns() { ClearOwnedBurns(); RefreshControl(); }
void URPGCombatControlComponent::ClearOwnedBurns()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bClearingBurns) return;
	TGuardValue<bool> Clearing(bClearingBurns, true); ++BurnEpoch;
	OwnedBurns.Reset();
	if (BoundASC.IsValid()) for (auto Handle : BoundASC->GetActiveEffects(FGameplayEffectQuery()))
	{
		const auto* Effect = BoundASC->GetActiveGameplayEffect(Handle);
		if (Effect && Effect->Spec.Def == GetDefault<URPGBurnEffect>() && Effect->Spec.GetContext().GetSourceObject() == this)
			BoundASC->RemoveActiveGameplayEffect(Handle);
	}
}
void URPGCombatControlComponent::HandleBurnRemoved(const FGameplayEffectRemovalInfo& Info)
{
	if (Info.ActiveEffect) OwnedBurns.Remove(Info.ActiveEffect->Handle);
	RefreshControl();
}
void URPGCombatControlComponent::HandleBurnInhibited(FActiveGameplayEffectHandle, bool) { RefreshControl(); }

void URPGCombatControlComponent::TickBurns()
{
	if (bEnding || bTickingBurn || !GetOwner()->HasAuthority() || !BoundASC.IsValid()) return;
	TGuardValue<bool> Ticking(bTickingBurn, true);
	TArray<FActiveGameplayEffectHandle> Handles; OwnedBurns.GenerateKeyArray(Handles);
	const double Now = GetWorld()->GetTimeSeconds();
	for (auto Handle : Handles) // At most 8 effects, one due tick each; missed slots never accumulate.
	{
		auto* Lease = OwnedBurns.Find(Handle); auto* ASC = BoundASC.Get();
		if (!Lease || !ASC || bEnding) continue;
		auto* Source = Lease->Source.Get();
		const auto* Pawn = Cast<APawn>(Source);
		const auto* Active = ASC->GetActiveGameplayEffect(Handle);
		if (!Active || !Lease->Lifetime.IsCurrent(Source, GetOwner())
			|| (!Lease->SourceController.IsExplicitlyNull() && !Lease->SourceController.IsValid())
			|| (Pawn && Pawn->GetController() != Lease->SourceController.Get())
			|| !EncounterCurrent(GetOwner(), Lease->EncounterId) || !EncounterCurrent(Source, Lease->SourceEncounterId))
		{ OwnedBurns.Remove(Handle); if (Active) ASC->RemoveActiveGameplayEffect(Handle); continue; }
		if (Now < Lease->NextTick) continue;
		const FBurnLease Frozen = *Lease;
		Lease->NextTick += (FMath::FloorToDouble((Now - Lease->NextTick) / Lease->Settings.Interval) + 1.) * Lease->Settings.Interval;
		if (Active->bIsInhibited || Active->GetTimeRemaining(float(Now)) <= 0.f) continue;
		auto* SourceASC = Frozen.SourceASC.Get(); if (!SourceASC) continue;
		auto Context = SourceASC->MakeEffectContext(); Context.AddSourceObject(this);
		FGameplayEffectSpec TickSpec(GetDefault<URPGBurnTickEffect>(), Context, 1.f);
		const float Before = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		{
			FRPGBurnTickGuard Permit(TickSpec, *SourceASC, *ASC, Handle, Frozen.Lifetime, Frozen.Settings.TickDamage);
			SourceASC->ApplyGameplayEffectSpecToTarget(TickSpec, ASC);
		}
		// Existing boss threat accepts actual health loss and the captured encounter; death/reset rejects old attribution.
		if (Frozen.Lifetime.IsCurrent(Source, GetOwner()) && (!Pawn || Pawn->GetController() == Frozen.SourceController.Get()))
			if (auto* Threat = GetOwner()->FindComponentByClass<URPGBossThreatComponent>())
				Threat->AddThreat(Cast<APawn>(Source), FMath::Clamp(Before - ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()), 0.f, Before), Frozen.EncounterId);
	}
	RefreshControl();
}
void URPGCombatControlComponent::RefreshBurnState()
{
	if (!GetOwner()->HasAuthority()) return;
	int32 Stacks = 0; double End = 0.;
	if (BoundASC.IsValid() && IsLivingAvatar(GetOwner())) for (const auto& Entry : OwnedBurns)
	{
		const auto* Active = BoundASC->GetActiveGameplayEffect(Entry.Key);
		if (!Active || Active->bIsInhibited || Active->GetTimeRemaining(GetWorld()->GetTimeSeconds()) <= 0.f) continue;
		++Stacks; End = FMath::Max(End, double(GetWorld()->GetTimeSeconds() + Active->GetTimeRemaining(GetWorld()->GetTimeSeconds())));
	}
	if (BurnState.Stacks != Stacks || FMath::Abs(BurnState.EndServerTime - End) > .02 || BurnState.RecoveryRevision != RecoveryRevision())
	{
		BurnState.Stacks = Stacks; BurnState.EndServerTime = End; BurnState.RecoveryRevision = RecoveryRevision(); ++BurnState.Revision;
		GetOwner()->ForceNetUpdate(); OnBurnStateChanged.Broadcast(BurnState);
	}
}
void URPGCombatControlComponent::OnRep_BurnState() { RefreshControl(); OnBurnStateChanged.Broadcast(BurnState); UpdateBurnVisual(); }
void URPGCombatControlComponent::UpdateBurnVisual()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	const auto* Clock = GetWorld()->GetGameState();
	const double Now = Clock ? Clock->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	if (bEnding || !bShowBurnStatus || BurnState.Stacks == 0 || !IsLivingAvatar(GetOwner())
		|| BurnState.RecoveryRevision != RecoveryRevision() || Now >= BurnState.EndServerTime)
	{ if (BurnStatusWidget) { BurnStatusWidget->DestroyComponent(); BurnStatusWidget = nullptr; } return; }
	if (BurnStatusWidget) return;
	auto* Character = Cast<ACharacter>(GetOwner()); if (!Character) return;
	BurnStatusWidget = NewObject<UWidgetComponent>(GetOwner()); BurnStatusWidget->SetupAttachment(Character->GetRootComponent());
	BurnStatusWidget->SetRelativeLocation(FVector(0,0,Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+70));
	BurnStatusWidget->SetWidgetSpace(EWidgetSpace::Screen); BurnStatusWidget->SetDrawSize(FVector2D(200,30));
	BurnStatusWidget->SetWidgetClass(URPGBurnStatusWidget::StaticClass()); BurnStatusWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BurnStatusWidget->RegisterComponent(); BurnStatusWidget->InitWidget();
	if (auto* Widget = Cast<URPGBurnStatusWidget>(BurnStatusWidget->GetUserWidgetObject())) Widget->SetControl(this);
}
