#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/Combat/RPGShieldComponent.h"
#include "Ability/Skill/RPGGreatSwordResolveAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBaseCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayEffect/RPGStunEffect.h"
#include "AIController.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "Component/Combat/RPGCombatContributionComponent.h"

URPGCombatControlComponent::URPGCombatControlComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = .05f;
	StunVisual = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/RPGEffects/ParticlesNiagara/StatusEffects/Stun/NS_Status_Stun.NS_Status_Stun")));
}

void URPGCombatControlComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, StunState);
	DOREPLIFETIME(ThisClass, StaggerState);
	DOREPLIFETIME(ThisClass, KnockbackState);
	DOREPLIFETIME(ThisClass, AirborneState);
	DOREPLIFETIME(ThisClass, SlowState);
	DOREPLIFETIME(ThisClass, BurnState);
}

void URPGCombatControlComponent::BeginPlay()
{
	Super::BeginPlay();
	if (auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner())) InitializeWithAbilitySystem(*ASC);
}

bool URPGCombatControlComponent::IsLivingAvatar(const AActor* Actor) const
{
	const auto* ASC = IsValid(Actor) ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Actor)) : nullptr;
	const auto* Attributes = ASC ? ASC->GetSet<URPGAttributeSet>() : nullptr;
	return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && ASC && ASC->GetAvatarActor() == Actor && Attributes
		&& FMath::IsFinite(Attributes->GetCurrentHealth()) && Attributes->GetCurrentHealth() > 0.f
		&& !ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death) && !IsDungeonGameplayRevoked(Actor);
}

uint32 URPGCombatControlComponent::RecoveryRevision() const
{
	const auto* Character = Cast<ARPGBaseCharacter>(GetOwner());
	return Character ? Character->GetCombatPresentationRevision() : 0;
}

void URPGCombatControlComponent::Unbind()
{
	if (!BoundASC.IsValid()) return;
	BoundASC->RegisterGameplayTagEvent(RPGCombatControl::StunTag()).Remove(StunTagHandle);
	BoundASC->RegisterGameplayTagEvent(RPGStagger::StatusTag()).Remove(StaggerTagHandle);
	BoundASC->RegisterGameplayTagEvent(RPGSlow::StatusTag()).Remove(SlowTagHandle);
	BoundASC->RegisterGameplayTagEvent(RPGKnockback::StatusTag()).Remove(KnockbackTagHandle);
	BoundASC->RegisterGameplayTagEvent(RPGAirborne::SequenceTag()).Remove(AirborneTagHandle);
	BoundASC->RegisterGameplayTagEvent(RPGGameplayTags::Shared_Status_Death).Remove(DeathTagHandle);
	BoundASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(HealthHandle);
	for (const auto& Handle : OwnedStuns)
	{
		if (auto* Delegate = BoundASC->OnGameplayEffectRemoved_InfoDelegate(Handle)) Delegate->RemoveAll(this);
		if (auto* Delegate = BoundASC->OnGameplayEffectInhibitionChangedDelegate(Handle)) Delegate->RemoveAll(this);
	}
}

void URPGCombatControlComponent::InitializeWithAbilitySystem(UAbilitySystemComponent& ASC)
{
	if (bEnding || ASC.GetAvatarActor() != GetOwner()
		|| UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()) != &ASC) return;
	if (BoundASC.Get() != &ASC)
	{
		++LifetimeEpoch;
	URPGGreatSwordResolveAbility::RevokeFor(GetOwner());
	if (auto* Shield = GetOwner()->FindComponentByClass<URPGShieldComponent>()) Shield->ClearShields();
		if (auto* Meter = GetOwner()->FindComponentByClass<URPGCombatContributionComponent>()) Meter->ResetForAvatarReuse();
		Unbind();
		EndKnockback(ERPGKnockbackEndReason::LifeChanged);
		EndAirborne(ERPGAirborneEndReason::LifeChanged);
		ClearOwnedStuns();
		ClearOwnedStaggers();
		ClearOwnedSlows();
		ClearOwnedBurns();
		ClearOwnedDefenseReductions();
		ClearOwnedDamageTakenIncreases();
		ClearAbilityImmunities();
		BoundASC = &ASC;
		BoundRecoveryRevision = RecoveryRevision();
		StunTagHandle = ASC.RegisterGameplayTagEvent(RPGCombatControl::StunTag()).AddUObject(this, &ThisClass::HandleTagChanged);
		StaggerTagHandle = ASC.RegisterGameplayTagEvent(RPGStagger::StatusTag()).AddUObject(this, &ThisClass::HandleTagChanged);
		SlowTagHandle = ASC.RegisterGameplayTagEvent(RPGSlow::StatusTag()).AddUObject(this, &ThisClass::HandleTagChanged);
		KnockbackTagHandle = ASC.RegisterGameplayTagEvent(RPGKnockback::StatusTag()).AddUObject(this, &ThisClass::HandleTagChanged);
		AirborneTagHandle = ASC.RegisterGameplayTagEvent(RPGAirborne::SequenceTag()).AddUObject(this, &ThisClass::HandleTagChanged);
		DeathTagHandle = ASC.RegisterGameplayTagEvent(RPGGameplayTags::Shared_Status_Death).AddUObject(this, &ThisClass::HandleTagChanged);
		HealthHandle = ASC.GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).AddUObject(this, &ThisClass::HandleHealthChanged);
	}
	RefreshControl();
}

bool URPGCombatControlComponent::HasUncancellableCombatAbility() const
{
	if (!BoundASC.IsValid()) return false;
	for (const auto& Spec : BoundASC->GetActivatableAbilities())
	{
		if (!Spec.IsActive() || !RPGCombatControl::IsStunAffectedAbility(Spec.Ability)) continue;
		for (const UGameplayAbility* Instance : Spec.GetAbilityInstances())
			if (Instance && Instance->IsActive() && !Instance->CanBeCanceled()) return true;
	}
	return false;
}

void URPGCombatControlComponent::CancelCombatAbilities()
{
	if (!BoundASC.IsValid() || !GetOwner()->HasAuthority()) return;
	TArray<FGameplayAbilitySpecHandle> Handles;
	for (auto& Spec : BoundASC->GetActivatableAbilities())
	{
		if (!RPGCombatControl::IsStunAffectedAbility(Spec.Ability)) continue;
		Spec.InputPressed = false;
		if (Spec.IsActive()) Handles.Add(Spec.Handle);
	}
	// Tags already block reentrant activation. Never cancel UI, passives or death.
	for (auto Handle : Handles)
		if (BoundASC.IsValid() && BoundASC->GetAvatarActor() == GetOwner()) BoundASC->CancelAbilityHandle(Handle);
}

FActiveGameplayEffectHandle URPGCombatControlComponent::ApplyApprovedDamageStun(AActor* Source,
	const FHitResult& Hit, const FGuid& Receipt, float Duration)
{
	AActor* Owner = GetOwner();
	if (bEnding || !IsValid(Owner) || !Owner->HasAuthority() || !Source || !Source->HasAuthority()
		|| Source == Owner || Source->GetWorld() != GetWorld() || Hit.GetActor() != Owner || !Receipt.IsValid()
		|| !RPGCombatControl::IsValidStunDuration(Duration) || Duration <= 0.f
		|| !IsLivingAvatar(Source) || !IsLivingAvatar(Owner)) return {};
	auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner);
	if (!ASC) return {};
	InitializeWithAbilitySystem(*ASC);
	if (RecentApprovedHits.Contains(Receipt)) return {};
	if (RecentApprovedHits.Num() >= 64) RecentApprovedHits.RemoveAt(0);
	RecentApprovedHits.Add(Receipt); // A rejected approved hit cannot apply later when immunity ends.
	if (bApplying || IsImmuneTo(ERPGCombatEffectKind::Stun)
		|| HasUncancellableCombatAbility()) return {};
	TGuardValue<bool> Guard(bApplying, true);
	const uint32 Revision = RecoveryRevision();
	const uint32 Epoch = LifetimeEpoch;
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(Source, Source);
	Context.AddSourceObject(this); // Exact ownership also covers reentrant removal before Apply returns.
	Context.AddHitResult(Hit);
	FGameplayEffectSpec Spec(GetDefault<URPGStunEffect>(), Context, 1.f);
	Spec.SetDuration(Duration, true);
	const FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectSpecToSelf(Spec);
	// Tag/cancellation callbacks can kill, reset, cleanse or replace ActorInfo.
	if (!Handle.IsValid() || !ASC->GetActiveGameplayEffect(Handle)) { RefreshControl(); return {}; }
	if (bEnding || BoundASC.Get() != ASC || !IsLivingAvatar(Owner) || RecoveryRevision() != Revision || LifetimeEpoch != Epoch
		|| HasUncancellableCombatAbility())
	{
		ASC->RemoveActiveGameplayEffect(Handle);
		return {};
	}
	OwnedStuns.Add(Handle);
	if (auto* Removed = ASC->OnGameplayEffectRemoved_InfoDelegate(Handle)) Removed->AddUObject(this, &ThisClass::HandleStunRemoved);
	if (auto* Inhibited = ASC->OnGameplayEffectInhibitionChangedDelegate(Handle)) Inhibited->AddUObject(this, &ThisClass::HandleStunInhibited);
	BoundRecoveryRevision = Revision;
	RefreshControl();
	return Handle;
}

bool URPGCombatControlComponent::RemoveOwnedStun(FActiveGameplayEffectHandle Handle)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !BoundASC.IsValid() || !OwnedStuns.Contains(Handle)) return false;
	return BoundASC->RemoveActiveGameplayEffect(Handle);
}

void URPGCombatControlComponent::ClearOwnedStuns()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	auto Handles = OwnedStuns;
	if (BoundASC.IsValid())
		for (auto Handle : BoundASC->GetActiveEffects(FGameplayEffectQuery()))
		{
			const auto* Effect = BoundASC->GetActiveGameplayEffect(Handle);
			if (Effect && Effect->Spec.Def->GetClass() == URPGStunEffect::StaticClass()
				&& Effect->Spec.GetContext().GetSourceObject() == this) Handles.Add(Handle);
		}
	OwnedStuns.Reset();
	for (auto Handle : Handles) if (BoundASC.IsValid()) BoundASC->RemoveActiveGameplayEffect(Handle);
}

void URPGCombatControlComponent::ResetForAvatarReuse()
{
	++LifetimeEpoch;
	URPGGreatSwordResolveAbility::RevokeFor(GetOwner());
	if (auto* Shield = GetOwner()->FindComponentByClass<URPGShieldComponent>()) Shield->ClearShields();
	if (auto* Meter = GetOwner()->FindComponentByClass<URPGCombatContributionComponent>()) Meter->ResetForAvatarReuse();
	EndKnockback(ERPGKnockbackEndReason::LifeChanged);
	EndAirborne(ERPGAirborneEndReason::LifeChanged);
	RecentAirborneHits.Reset();
	RecentKnockbackHits.Reset();
	ClearOwnedStuns();
	ClearOwnedStaggers();
	RecentApprovedHits.Reset();
	RecentStaggerHits.Reset();
	ClearOwnedSlows();
	ClearOwnedBurns();
	ClearOwnedDefenseReductions();
	ClearOwnedDamageTakenIncreases();
	RecentSlowHits.Reset();
	RecentBurnHits.Reset();
	RecentDefenseReductionHits.Reset();
	RecentDamageTakenIncreaseHits.Reset();
	ClearAbilityImmunities();
	BoundRecoveryRevision = RecoveryRevision();
	RefreshControl();
}

void URPGCombatControlComponent::HandleStunRemoved(const FGameplayEffectRemovalInfo& Info)
{
	if (Info.ActiveEffect) OwnedStuns.Remove(Info.ActiveEffect->Handle);
	RefreshControl();
}

void URPGCombatControlComponent::HandleStunInhibited(FActiveGameplayEffectHandle, bool) { RefreshControl(); }
void URPGCombatControlComponent::HandleTagChanged(FGameplayTag, int32) { RefreshControl(); }
void URPGCombatControlComponent::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	if (GetOwner()->HasAuthority() && Data.OldValue > 0.f && Data.NewValue <= 0.f)
	{
		++LifetimeEpoch;
	URPGGreatSwordResolveAbility::RevokeFor(GetOwner());
	if (auto* Shield = GetOwner()->FindComponentByClass<URPGShieldComponent>()) Shield->ClearShields();
		if (auto* Meter = GetOwner()->FindComponentByClass<URPGCombatContributionComponent>()) Meter->ResetForAvatarReuse();
	}
	RefreshControl();
}

void URPGCombatControlComponent::RefreshControl()
{
	if (bEnding || bRefreshing || !BoundASC.IsValid()) return;
	TGuardValue<bool> Guard(bRefreshing, true);
	const bool bLiving = IsLivingAvatar(GetOwner());
	if (GetOwner()->HasAuthority() && (!bLiving || BoundRecoveryRevision != RecoveryRevision()))
	{
		EndKnockback(ERPGKnockbackEndReason::LifeChanged);
		EndAirborne(ERPGAirborneEndReason::LifeChanged);
		ClearOwnedStuns();
		ClearOwnedStaggers();
		ClearOwnedSlows();
		ClearOwnedBurns();
		ClearOwnedDefenseReductions();
		ClearOwnedDamageTakenIncreases();
		ClearAbilityImmunities();
	}
	if (GetOwner()->HasAuthority() && RPGStagger::HasHigherControl(GetOwner())) ClearOwnedStaggers();
	bool bNow = bLiving && RPGCombatControl::IsActionBlocked(GetOwner());
	const bool bEntering = bNow && !bActiveControl;
	bActiveControl = bNow;
	if (bEntering && GetOwner()->HasAuthority())
	{
		CancelCombatAbilities();
		if (bEnding) return;
		if (auto* Pawn = Cast<APawn>(GetOwner()))
			if (auto* AI = Cast<AAIController>(Pawn->GetController())) AI->StopMovement();
		if (auto* Character = Cast<ACharacter>(GetOwner()))
		{
			Character->GetCharacterMovement()->Velocity.X = Character->GetCharacterMovement()->Velocity.Y = 0.f;
			Character->ConsumeMovementInputVector();
		}
		if (!IsLivingAvatar(GetOwner()) || BoundRecoveryRevision != RecoveryRevision()) { ClearOwnedStuns(); ClearOwnedStaggers(); }
		bNow = IsLivingAvatar(GetOwner()) && RPGCombatControl::IsActionBlocked(GetOwner());
		bActiveControl = bNow;
	}
	if (GetOwner()->HasAuthority())
	{
		const bool bStunned = IsLivingAvatar(GetOwner()) && RPGCombatControl::IsStunned(GetOwner());
		double End = 0.0;
		if (bStunned)
		{
			const float Now = GetWorld()->GetTimeSeconds();
			for (auto Handle : BoundASC->GetActiveEffects(FGameplayEffectQuery()))
			{
				const auto* Effect = BoundASC->GetActiveGameplayEffect(Handle);
				if (!Effect || Effect->bIsInhibited) continue;
				FGameplayTagContainer Granted;
				Effect->Spec.GetAllGrantedTags(Granted);
				if (Granted.HasTagExact(RPGCombatControl::StunTag()) && Effect->GetDuration() > 0.f)
					End = FMath::Max(End, double(Now + Effect->GetTimeRemaining(Now)));
			}
		}
		if (StunState.bActive != bStunned || FMath::Abs(StunState.EndServerTime - End) > .02 || StunState.RecoveryRevision != RecoveryRevision())
		{
			StunState.bActive = bStunned;
			StunState.EndServerTime = End;
			StunState.RecoveryRevision = RecoveryRevision();
			++StunState.Revision;
			GetOwner()->ForceNetUpdate();
			OnStunStateChanged.Broadcast(StunState);
		}
	}
	RefreshStaggerState();
	RefreshDefenseReductions();
	RefreshDamageTakenIncreases();
	RefreshSlowState();
	RefreshBurnState();
	RefreshAbilityImmunities();
	SetComponentTickEnabled(bNow || !OwnedStuns.IsEmpty() || StunState.bActive || KnockbackState.bActive || AirborneState.IsActive()
		|| !OwnedSlows.IsEmpty() || SlowState.bActive || !AbilityImmunities.IsEmpty() || !OwnedBurns.IsEmpty() || BurnState.Stacks > 0 || HasMontageImmunityTracking() || StaggerState.bActive || !OwnedStaggers.IsEmpty() || !OwnedDefenseReductions.IsEmpty() || !OwnedDamageTakenIncreases.IsEmpty());
	UpdateStaggerPose();
	UpdateVisual();
	UpdateSlowVisual();
	UpdateBurnVisual();
}

void URPGCombatControlComponent::OnRep_StunState()
{
	OnStunStateChanged.Broadcast(StunState);
	SetComponentTickEnabled(StunState.bActive || KnockbackState.bActive || AirborneState.IsActive() || SlowState.bActive || BurnState.Stacks > 0 || HasMontageImmunityTracking() || StaggerState.bActive || !OwnedStaggers.IsEmpty() || !OwnedDefenseReductions.IsEmpty() || !OwnedDamageTakenIncreases.IsEmpty());
	UpdateStaggerPose();
	UpdateVisual();
}

void URPGCombatControlComponent::UpdateVisual()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	const auto CanShow = [this]()
	{
		const auto* State = GetWorld()->GetGameState();
		const double Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
		return !bEnding && IsLivingAvatar(GetOwner()) && StunState.bActive
			&& StunState.RecoveryRevision == RecoveryRevision() && (StunState.EndServerTime == 0.0 || Now < StunState.EndServerTime);
	};
	if (!CanShow())
	{
		if (StunVisualInstance) { StunVisualInstance->DestroyComponent(); StunVisualInstance = nullptr; }
		return;
	}
	if (StunVisualInstance || StunVisual.IsNull()) return;
	auto* Character = Cast<ACharacter>(GetOwner());
	UNiagaraSystem* System = StunVisual.LoadSynchronous();
	if (Character && System && CanShow())
		StunVisualInstance = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Character->GetRootComponent(), NAME_None,
			FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f), FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, false, true, ENCPoolMethod::None, false);
}

void URPGCombatControlComponent::TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Function)
{
	Super::TickComponent(DeltaTime, Type, Function);
	RefreshMontageImmunity();
	RefreshControl();
	TickKnockback();
	TickAirborne();
	TickBurns();
}

void URPGCombatControlComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEnding = true;
	EndKnockback(ERPGKnockbackEndReason::LifeChanged);
	EndAirborne(ERPGAirborneEndReason::LifeChanged);
	Unbind();
	ClearOwnedStuns();
	ClearOwnedStaggers();
	ClearOwnedSlows();
	ClearOwnedBurns();
	ClearOwnedDefenseReductions();
	ClearOwnedDamageTakenIncreases();
	ClearAbilityImmunities();
	UpdateSlowVisual();
	UpdateBurnVisual();
	StopStaggerPose();
	if (StunVisualInstance) { StunVisualInstance->DestroyComponent(); StunVisualInstance = nullptr; }
	BoundASC.Reset();
	Super::EndPlay(Reason);
}
