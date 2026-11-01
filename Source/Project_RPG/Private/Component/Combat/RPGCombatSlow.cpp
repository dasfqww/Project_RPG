#include "Component/Combat/RPGCombatControlComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Combat/RPGApprovedHitContext.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameplayEffect/RPGSlowEffect.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/Combat/RPGSlowStatusWidget.h"

FActiveGameplayEffectHandle URPGCombatControlComponent::ApplyApprovedDamageSlow(AActor* Source, const FHitResult& Hit,
	const FGuid& Receipt, const FRPGSlowSettings& Settings)
{
	auto* Character = Cast<ACharacter>(GetOwner());
	if (bEnding || bApplyingSlow || !Character || !Character->HasAuthority() || !Source || !Source->HasAuthority()
		|| Source == Character || Source->GetWorld() != GetWorld() || Hit.GetActor() != Character || !Receipt.IsValid()
		|| !Settings.IsEnabled() || !IsLivingAvatar(Source) || !IsLivingAvatar(Character)
		|| !Cast<URPGCharacterMovementComponent>(Character->GetCharacterMovement())) return {};
	auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Character);
	InitializeWithAbilitySystem(*ASC);
	if (RecentSlowHits.Contains(Receipt)) return {};
	if (RecentSlowHits.Num() >= 64) RecentSlowHits.RemoveAt(0);
	RecentSlowHits.Add(Receipt);
	if (IsImmuneTo(ERPGCombatEffectKind::Slow) || OwnedSlows.Num() >= 32) return {};
	TGuardValue<bool> Applying(bApplyingSlow, true);
	const FRPGApprovedHitContext Lifetime(Source, Character);
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(Source, Source); Context.AddSourceObject(this); Context.AddHitResult(Hit);
	FGameplayEffectSpec Spec(GetDefault<URPGSlowEffect>(), Context, 1.f);
	Spec.SetDuration(Settings.Duration, true);
	Spec.SetSetByCallerMagnitude(RPGSlow::MagnitudeTag(), 1.f - Settings.Percent / 100.f);
	const auto Handle = ASC->ApplyGameplayEffectSpecToSelf(Spec);
	const auto* Effect = ASC->GetActiveGameplayEffect(Handle);
	if (!Effect || !Handle.IsValid()) return {};
	if (bEnding || BoundASC.Get() != ASC || !Lifetime.IsCurrent(Source, Character) || Effect->bIsInhibited)
	{ ASC->RemoveActiveGameplayEffect(Handle); return {}; }
	OwnedSlows.Add(Handle);
	if (auto* Removed = ASC->OnGameplayEffectRemoved_InfoDelegate(Handle)) Removed->AddUObject(this, &ThisClass::HandleSlowRemoved);
	if (auto* Inhibited = ASC->OnGameplayEffectInhibitionChangedDelegate(Handle)) Inhibited->AddUObject(this, &ThisClass::HandleSlowInhibited);
	RefreshControl(); return Handle;
}

bool URPGCombatControlComponent::RemoveOwnedSlow(FActiveGameplayEffectHandle Handle)
{
	return GetOwner() && GetOwner()->HasAuthority() && BoundASC.IsValid() && OwnedSlows.Contains(Handle)
		&& BoundASC->RemoveActiveGameplayEffect(Handle);
}

void URPGCombatControlComponent::ClearOwnedSlows()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	auto Handles = OwnedSlows;
	if (BoundASC.IsValid())
		for (auto Handle : BoundASC->GetActiveEffects(FGameplayEffectQuery()))
		{
			const auto* Effect = BoundASC->GetActiveGameplayEffect(Handle);
			if (Effect && Effect->Spec.Def->GetClass() == URPGSlowEffect::StaticClass()
				&& Effect->Spec.GetContext().GetSourceObject() == this) Handles.Add(Handle);
		}
	OwnedSlows.Reset();
	for (auto Handle : Handles) if (BoundASC.IsValid()) BoundASC->RemoveActiveGameplayEffect(Handle);
}

void URPGCombatControlComponent::HandleSlowRemoved(const FGameplayEffectRemovalInfo& Info)
{
	if (Info.ActiveEffect) OwnedSlows.Remove(Info.ActiveEffect->Handle);
	RefreshControl();
}
void URPGCombatControlComponent::HandleSlowInhibited(FActiveGameplayEffectHandle, bool) { RefreshControl(); }

void URPGCombatControlComponent::RefreshSlowState()
{
	if (!GetOwner()->HasAuthority()) return;
	float Percent = 0.f; double End = 0.;
	const float Now = GetWorld()->GetTimeSeconds();
	if (IsLivingAvatar(GetOwner()) && BoundASC.IsValid())
		for (auto Handle : BoundASC->GetActiveEffects(FGameplayEffectQuery()))
		{
			const auto* Effect = BoundASC->GetActiveGameplayEffect(Handle);
			if (!Effect || Effect->bIsInhibited || Effect->Spec.Def->GetClass() != URPGSlowEffect::StaticClass()) continue;
			const float Scale = Effect->Spec.GetSetByCallerMagnitude(RPGSlow::MagnitudeTag(), false, 1.f);
			if (!FMath::IsFinite(Scale) || Scale < .099f || Scale > .951f || Effect->GetDuration() <= 0.f) continue;
			const float Strength = (1.f - Scale) * 100.f;
			const double Expires = Now + Effect->GetTimeRemaining(Now);
			if (Strength > Percent + .01f) { Percent = Strength; End = Expires; }
			else if (FMath::IsNearlyEqual(Strength, Percent, .01f)) End = FMath::Max(End, Expires);
		}
	if (SlowState.bActive != (Percent > 0.f) || !FMath::IsNearlyEqual(SlowState.Percent, Percent, .01f)
		|| FMath::Abs(SlowState.EndServerTime - End) > .02 || SlowState.RecoveryRevision != RecoveryRevision())
	{
		SlowState.bActive = Percent > 0.f; SlowState.Percent = Percent; SlowState.EndServerTime = End;
		SlowState.RecoveryRevision = RecoveryRevision(); ++SlowState.Revision;
		GetOwner()->ForceNetUpdate(); OnSlowStateChanged.Broadcast(SlowState);
	}
}

void URPGCombatControlComponent::OnRep_SlowState()
{
	OnSlowStateChanged.Broadcast(SlowState);
	SetComponentTickEnabled(SlowState.bActive || StunState.bActive || KnockbackState.bActive || AirborneState.IsActive() || BurnState.Stacks > 0 || StaggerState.bActive || !OwnedDefenseReductions.IsEmpty() || !OwnedDamageTakenIncreases.IsEmpty());
	UpdateSlowVisual();
}

void URPGCombatControlComponent::UpdateSlowVisual()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	const auto* Clock = GetWorld()->GetGameState();
	const double Now = Clock ? Clock->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const bool bShow = !bEnding && bShowSlowStatus && SlowState.bActive && IsLivingAvatar(GetOwner())
		&& SlowState.RecoveryRevision == RecoveryRevision() && Now < SlowState.EndServerTime;
	if (!bShow) { if (SlowStatusWidget) { SlowStatusWidget->DestroyComponent(); SlowStatusWidget = nullptr; } return; }
	if (SlowStatusWidget) return;
	auto* Character = Cast<ACharacter>(GetOwner()); if (!Character) return;
	SlowStatusWidget = NewObject<UWidgetComponent>(GetOwner());
	SlowStatusWidget->SetupAttachment(Character->GetRootComponent());
	SlowStatusWidget->SetRelativeLocation(FVector(0,0,Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+40));
	SlowStatusWidget->SetWidgetSpace(EWidgetSpace::Screen); SlowStatusWidget->SetDrawSize(FVector2D(200,30));
	SlowStatusWidget->SetWidgetClass(URPGSlowStatusWidget::StaticClass()); SlowStatusWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SlowStatusWidget->RegisterComponent(); SlowStatusWidget->InitWidget();
	if (auto* Widget = Cast<URPGSlowStatusWidget>(SlowStatusWidget->GetUserWidgetObject())) Widget->SetControl(this);
}
