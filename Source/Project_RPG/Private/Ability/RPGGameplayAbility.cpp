// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/RPGGameplayAbility.h"
#include "AI/RPGBossAIComponent.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "Component/Combat/RPGCombatReactionComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "RPGGameplayTags.h"
#include "Character/RPGBaseCharacter.h"
#include "Controller/RPGPlayerController.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include"Attribute/RPGAttributeSet.h"

bool URPGGameplayAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (ActorInfo && IsHitReactionAbility()
		&& RPGCombatImmunity::IsImmune(ActorInfo->AvatarActor.Get(), ERPGCombatEffectKind::HitReaction)) return false;
	if (AutomaticallyApplyActivationImmunity() && ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		const auto Settings = ResolveActivationImmunity(ActorInfo->AbilitySystemComponent->FindAbilitySpecFromHandle(Handle));
		if (!Settings.IsValid() || (Settings.IsEnabled() && (GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::NonInstanced
			|| GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalOnly))) return false;
	}
	if (ActorInfo && RPGCombatControl::IsActionBlocked(ActorInfo->AvatarActor.Get())
		&& RPGCombatControl::IsStunAffectedAbility(this)) return false;
	if (!Super::CanActivateAbility(
		Handle,
		ActorInfo,
		SourceTags,
		TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (const auto* ClassOwner = AvatarActor ? AvatarActor->FindComponentByClass<URPGPlayerClassComponent>() : nullptr)
	{
		const auto* ASC = ActorInfo->AbilitySystemComponent.Get();
		const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
		const FGameplayTag IdentityInput = Spec && Spec->GetDynamicSpecSourceTags().HasTagExact(RPGGameplayTags::InputTag_IdentitySkill)
			? RPGGameplayTags::InputTag_IdentitySkill.GetTag() : FGameplayTag();
		if (ClassOwner->RoutesAbilityToClass(GetClass(), IdentityInput) && !ClassOwner->CanUseAbilitySpec(Handle, GetClass())) return false;
	}
	if (!AvatarActor || !AvatarActor->HasAuthority())
	{
		return true;
	}
	if (!ShouldApplyServerInputEnforcement())
	{
		return true;
	}

	URPGSecurityValidationComponent* Security =
		AvatarActor->FindComponentByClass<URPGSecurityValidationComponent>();
	if (!Security)
	{
		return true;
	}
	FString RejectionReason;
	if (!Security->CanPerformProtectedAction(
		TEXT("AbilityActivation"),
		true,
		RejectionReason))
	{
		return false;
	}
	return !ShouldApplyServerActivationRateLimit()
		|| Security->CanAcceptAbilityActivation(
		GetClass(),
		RejectionReason);
}

void URPGGameplayAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (IsInstantiated())
	{
		CombatExecutionId = FGuid::NewGuid();
		const FGuid InitializingExecution = CombatExecutionId;
		bMontageImmunityReady = false;
		AActor* ExecutionAvatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
		// OnSpawn may run inside ASC ActorInfo setup, before its ordinary control binding finishes.
		if (ExecutionAvatar && ActorInfo->AbilitySystemComponent.IsValid())
			if (auto* Control = ExecutionAvatar->FindComponentByClass<URPGCombatControlComponent>())
				Control->InitializeWithAbilitySystem(*ActorInfo->AbilitySystemComponent);
		if (CombatExecutionId != InitializingExecution) return;
		CombatExecutionLifetime = FRPGApprovedHitContext(ExecutionAvatar, ExecutionAvatar);
		const auto* ExecutionBoss = ExecutionAvatar ? ExecutionAvatar->FindComponentByClass<URPGBossAIComponent>() : nullptr;
		CombatExecutionBossEncounter = ExecutionBoss ? ExecutionBoss->GetBossState().EncounterId : 0;
		bMontageImmunityReady = AutomaticallyApplyActivationImmunity();
	}
	const FGuid ExpectedExecution = CombatExecutionId;
	RefreshAttributeSetFromActorInfo(ActorInfo);
	const auto* Spec = ActorInfo && ActorInfo->AbilitySystemComponent.IsValid() ? ActorInfo->AbilitySystemComponent->FindAbilitySpecFromHandle(Handle) : nullptr;
	FGuid ExpectedImmunityToken;
	if (AutomaticallyApplyActivationImmunity())
	{
		const auto Settings = ResolveActivationImmunity(Spec);
		if (Settings.IsEnabled() && IsInstantiated() && ActorInfo && ActorInfo->IsNetAuthority())
		{
			if (ActivationImmunityToken.IsValid()) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
			ExpectedImmunityToken = FGuid::NewGuid();
			ActivationImmunityToken = ExpectedImmunityToken;
		}
		const bool bStarted = StartActivationImmunity(Settings);
		// An apply callback may have ended this cast and reactivated the same instance.
		if (ExpectedImmunityToken.IsValid() && ActivationImmunityToken != ExpectedImmunityToken) return;
		if (!bStarted) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
	}
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (IsInstantiated() && CombatExecutionId != ExpectedExecution) return;
	if (ExpectedImmunityToken.IsValid() && ActivationImmunityToken != ExpectedImmunityToken) return;

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (AvatarActor && AvatarActor->HasAuthority() &&
		ShouldApplyServerActivationRateLimit())
	{
		if (URPGSecurityValidationComponent* Security =
			AvatarActor->FindComponentByClass<URPGSecurityValidationComponent>())
		{
			Security->RecordAbilityActivation(GetClass());
		}
	}
}

bool URPGGameplayAbility::CanUseMontageImmunity() const
{
	if (!IsInstantiated() || !IsActive() || !CombatExecutionId.IsValid() || !bMontageImmunityReady
		|| GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalOnly) return false;
	auto* Avatar = GetAvatarActorFromActorInfo();
	if (!CombatExecutionLifetime.IsCurrent(Avatar, Avatar)) return false;
	const auto* Boss = Avatar->FindComponentByClass<URPGBossAIComponent>();
	return !Boss || (Boss->GetBossState().EncounterId == CombatExecutionBossEncounter
		&& Boss->GetBossState().EncounterState == ERPGBossEncounterState::InCombat);
}

bool URPGGameplayAbility::ShouldApplyServerActivationRateLimit() const
{
	return bCountTowardServerActivationRateLimit &&
		ShouldApplyServerInputEnforcement();
}

bool URPGGameplayAbility::ShouldApplyServerInputEnforcement() const
{
	return AbilityActivationPolicy == ERPGAbilityActivationPolicy::OnTriggered &&
		ActivationPolicy != ERPGGladiatorAbilityActivationPolicy::OnSpawn &&
		(NetExecutionPolicy ==
			EGameplayAbilityNetExecutionPolicy::LocalPredicted ||
		 NetExecutionPolicy ==
			EGameplayAbilityNetExecutionPolicy::ServerOnly);
}

void URPGGameplayAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);

	if (AbilityActivationPolicy == ERPGAbilityActivationPolicy::OnGiven)
	{
		// The granting server initiates this one-shot, including remote local /
		// predicted execution through GAS. Replicating the spec to a ready new
		// Pawn must not start a second client prediction beside that request.
		if (ActorInfo && ActorInfo->IsNetAuthority() && !Spec.IsActive())
		{
			ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle);
		}
	}
}

void URPGGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsEndAbilityValid(Handle, ActorInfo)) return;
	if (ScopeLockCount > 0)
	{
		WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this, &URPGGameplayAbility::EndAbility,
			Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled));
		return;
	}
	const FGuid EndingExecution = CombatExecutionId;
	CombatExecutionId.Invalidate();
	bMontageImmunityReady = false;
	if (auto* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr)
		if (auto* Control = Avatar->FindComponentByClass<URPGCombatControlComponent>())
		{
			TGuardValue<bool> EndingGuard(bIsAbilityEnding, true);
			Control->EndMontageImmunityForAbility(*this, EndingExecution);
		}
	const auto Token = ActivationImmunityToken;
	const auto Owner = ActivationImmunityOwner;
	ActivationImmunityToken.Invalidate();
	ActivationImmunityOwner.Reset();
	if (Owner.IsValid())
	{
		// Effect removal delegates may try to end this execution again.
		TGuardValue<bool> Guard(bIsAbilityEnding, true);
		Owner->EndAbilityImmunity(Token);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	if (AbilityActivationPolicy == ERPGAbilityActivationPolicy::OnGiven)
	{
		if (ActorInfo && ActorInfo->IsNetAuthority())
		{
			ActorInfo->AbilitySystemComponent->ClearAbility(Handle);
		}
	}
}

FRPGCombatImmunitySettings URPGGameplayAbility::ResolveActivationImmunity(const FGameplayAbilitySpec* Spec) const
{
	const auto Settings = RPGCombatImmunity::GetGrantedSettings(Spec);
	return Settings.Flags == -1 ? ActivationImmunities : Settings;
}

bool URPGGameplayAbility::StartActivationImmunity(const FRPGCombatImmunitySettings& Settings)
{
	if (!Settings.IsValid()) return false;
	if (!Settings.IsEnabled()) return true;
	if (!IsInstantiated() || !CurrentActorInfo || !IsActive()) return false;
	if (!CurrentActorInfo->IsNetAuthority()) return true;
	AActor* Avatar = GetAvatarActorFromActorInfo();
	auto* Control = Avatar ? Avatar->FindComponentByClass<URPGCombatControlComponent>() : nullptr;
	if (!Control || ActivationImmunityOwner.IsValid()) return false;
	// The generic activation path reserves its token before callbacks; committed skills reserve here.
	if (!ActivationImmunityToken.IsValid()) ActivationImmunityToken = FGuid::NewGuid();
	ActivationImmunityOwner = Control;
	return Control->BeginAbilityImmunity(*this, ActivationImmunityToken, Settings);
}

bool URPGGameplayAbility::IsHitReactionAbility() const
{
	if (GetAssetTags().HasTag(RPGGameplayTags::Shared_Ability_HitReact)
		|| GetAssetTags().HasTag(RPGGameplayTags::Shared_Event_HitReact)) return true;
	for (const auto& Trigger : AbilityTriggers)
		if (Trigger.TriggerTag.MatchesTag(RPGGameplayTags::Shared_Ability_HitReact)
			|| Trigger.TriggerTag.MatchesTag(RPGGameplayTags::Shared_Event_HitReact)) return true;
	return false;
}

void URPGGameplayAbility::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	// OnSpawn can activate synchronously below. Per-execution instances also refresh on activation.
	RefreshAttributeSetFromActorInfo(ActorInfo);
	Super::OnAvatarSet(ActorInfo, Spec);

	if (ActivationPolicy == ERPGGladiatorAbilityActivationPolicy::OnSpawn && ActorInfo
		&& ActorInfo->AbilitySystemComponent.IsValid() && !Spec.IsActive())
	{
		ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle);
	}
}

const URPGAttributeSet* URPGGameplayAbility::RefreshAttributeSetFromActorInfo(const FGameplayAbilityActorInfo* ActorInfo)
{
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const URPGAttributeSet* Attributes = ActorInfo && ActorInfo->AvatarActor.IsValid() && IsValid(ASC)
		? ASC->GetSet<URPGAttributeSet>() : nullptr;
	CachedAttributeSet = IsInstantiated() ? Attributes : nullptr;
	return Attributes;
}

UPawnCombatComponent* URPGGameplayAbility::GetPawnCombatComponentFromActorInfo() const
{
	return GetAvatarActorFromActorInfo()->FindComponentByClass<UPawnCombatComponent>();
}

URPGAbilitySystemComponent* URPGGameplayAbility::GetRPGAbilitySystemComponentFromActorInfo() const
{
	return Cast<URPGAbilitySystemComponent>(CurrentActorInfo->AbilitySystemComponent);
}

ARPGPlayerController* URPGGameplayAbility::GetLyraPlayerControllerFromActorInfo() const
{
	return CurrentActorInfo ? Cast<ARPGPlayerController>(CurrentActorInfo->PlayerController.Get()) : nullptr;
}

FActiveGameplayEffectHandle URPGGameplayAbility::NativeApplyEffectSpecHandleToTarget(AActor* TargetActor, const FGameplayEffectSpecHandle& InSpecHandle)
{
	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!AvatarActor || !AvatarActor->HasAuthority() ||
		!IsValid(TargetActor) || !InSpecHandle.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!TargetASC)
	{
		return FActiveGameplayEffectHandle();
	}
	URPGAbilitySystemComponent* SourceASC =
		GetRPGAbilitySystemComponentFromActorInfo();
	return SourceASC
		? SourceASC->ApplyGameplayEffectSpecToTarget(
			*InSpecHandle.Data,
			TargetASC)
		: FActiveGameplayEffectHandle();
}

FActiveGameplayEffectHandle URPGGameplayAbility::BP_ApplyEffectSpecHandleToTarget
	(AActor* TargetActor, const FGameplayEffectSpecHandle& InSpecHandle, ERPGSuccessType& OutSuccessType,
	 bool& bOutUseLegacyHitReaction)
{
	bOutUseLegacyHitReaction = true;
	AActor* SourceActor = GetAvatarActorFromActorInfo();
	FActiveGameplayEffectHandle ActiveGameplayEffectHandle;
	bool bApplied = false;
	const ERPGAttackHitReactionPolicy ReactionPolicy = GetGrantedHitReactionPolicy();
	const float StunDuration = GetGrantedStunDuration();
	const FRPGKnockbackSettings Knockback = GetGrantedKnockback();
	const FRPGSlowSettings Slow = GetGrantedSlow();
	const FRPGBurnSettings Burn = GetGrantedBurn();
	const FRPGDefenseReductionSettings DefenseReduction = GetGrantedDefenseReduction();
	const FRPGDamageTakenIncreaseSettings DamageTakenIncrease = GetGrantedDamageTakenIncrease();
	const FRPGStaggerSettings Stagger = GetGrantedStagger();
	if (IsValid(SourceActor) && IsValid(TargetActor))
	{
		const FVector ImpactPoint = TargetActor->GetActorLocation();
		const FHitResult ServerHit(
			TargetActor,
			nullptr,
			ImpactPoint,
			(ImpactPoint - SourceActor->GetActorLocation()).GetSafeNormal());
		const FRPGSkillSecurityProfile CompatibilityProfile;
		const FRPGApprovedHitContext HitContext(SourceActor, TargetActor);
		bApplied =
			URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(
				SourceActor,
				ServerHit,
				InSpecHandle,
				CompatibilityProfile,
				&ActiveGameplayEffectHandle);
		// The approval and its presentation route stay in this native invocation.
		// BP may skip its old event only from this successful damage result.
		if (bApplied) bOutUseLegacyHitReaction = !TryRouteApprovedHitReaction(ServerHit, ReactionPolicy, StunDuration, Knockback, Slow, HitContext, Burn, Stagger, DefenseReduction, DamageTakenIncrease);
	}
	OutSuccessType = bApplied
		? ERPGSuccessType::Successful
		: ERPGSuccessType::Failed;
	return ActiveGameplayEffectHandle;
}

void URPGGameplayAbility::ApplyGameplayEffectSpecHandleToHitResults(const FGameplayEffectSpecHandle& InSpecHandle, const TArray<FHitResult>& InHitResults)
{
	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!AvatarActor || !AvatarActor->HasAuthority() ||
		!InSpecHandle.IsValid() || InHitResults.IsEmpty())
	{
		return;
	}

	APawn* OwningPawn = CastChecked<APawn>(GetAvatarActorFromActorInfo());
	const ERPGAttackHitReactionPolicy ReactionPolicy = GetGrantedHitReactionPolicy();
	const float StunDuration = GetGrantedStunDuration();
	const FRPGKnockbackSettings Knockback = GetGrantedKnockback();
	const FRPGSlowSettings Slow = GetGrantedSlow();
	const FRPGBurnSettings Burn = GetGrantedBurn();
	const FRPGDefenseReductionSettings DefenseReduction = GetGrantedDefenseReduction();
	const FRPGDamageTakenIncreaseSettings DamageTakenIncrease = GetGrantedDamageTakenIncrease();
	const FRPGStaggerSettings Stagger = GetGrantedStagger();

	for (const FHitResult& Hit : InHitResults)
	{
		if (APawn* HitPawn = Cast<APawn>(Hit.GetActor()))
		{
			if (URPGCombatFunctionLibrary::IsTargetPawnHostile(OwningPawn, HitPawn))
			{
				const FRPGSkillSecurityProfile CompatibilityProfile;
				const FRPGApprovedHitContext HitContext(OwningPawn, HitPawn);
				if (URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(
					OwningPawn,
					Hit,
					InSpecHandle,
					CompatibilityProfile))
				{
					FGameplayEventData Data;
					Data.Instigator = OwningPawn;
					Data.Target = HitPawn;

					if (!TryRouteApprovedHitReaction(Hit, ReactionPolicy, StunDuration, Knockback, Slow, HitContext, Burn, Stagger, DefenseReduction, DamageTakenIncrease))
					{
						UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
							HitPawn,
							RPGGameplayTags::Shared_Ability_HitReact,
							Data
						);
					}
				}
			}
		}
	}
}

ERPGAttackHitReactionPolicy URPGGameplayAbility::GetGrantedHitReactionPolicy() const
{
	if (!IsInstantiated() || !CurrentActorInfo || !CurrentActorInfo->AbilitySystemComponent.IsValid())
		return ERPGAttackHitReactionPolicy::None;
	const FGameplayAbilitySpec* Spec = GetCurrentAbilitySpec();
	return Spec ? RPGHitReaction::GetGrantedPolicy(Spec) : ERPGAttackHitReactionPolicy::None;
}

float URPGGameplayAbility::GetGrantedStunDuration() const
{
	if (!IsInstantiated() || !CurrentActorInfo || !CurrentActorInfo->AbilitySystemComponent.IsValid()) return 0.f;
	return RPGCombatControl::GetGrantedStunDuration(GetCurrentAbilitySpec());
}

FRPGKnockbackSettings URPGGameplayAbility::GetGrantedKnockback() const
{
	return IsInstantiated() && CurrentActorInfo ? RPGKnockback::GetGrantedSettings(GetCurrentAbilitySpec()) : FRPGKnockbackSettings();
}

FRPGSlowSettings URPGGameplayAbility::GetGrantedSlow() const
{
	return IsInstantiated() && CurrentActorInfo ? RPGSlow::GetGrantedSettings(GetCurrentAbilitySpec()) : FRPGSlowSettings();
}

FRPGBurnSettings URPGGameplayAbility::GetGrantedBurn() const
{
	return IsInstantiated() && CurrentActorInfo ? RPGBurn::GetGrantedSettings(GetCurrentAbilitySpec()) : FRPGBurnSettings();
}

FRPGDefenseReductionSettings URPGGameplayAbility::GetGrantedDefenseReduction() const
{
	return IsInstantiated() && CurrentActorInfo ? RPGDefenseReduction::GetGrantedSettings(GetCurrentAbilitySpec()) : FRPGDefenseReductionSettings();
}

FRPGDamageTakenIncreaseSettings URPGGameplayAbility::GetGrantedDamageTakenIncrease() const
{
	return IsInstantiated() && CurrentActorInfo ? RPGDamageTakenIncrease::GetGrantedSettings(GetCurrentAbilitySpec()) : FRPGDamageTakenIncreaseSettings();
}

FRPGStaggerSettings URPGGameplayAbility::GetGrantedStagger() const
{
	return IsInstantiated() && CurrentActorInfo ? RPGStagger::GetGrantedSettings(GetCurrentAbilitySpec()) : FRPGStaggerSettings();
}

bool URPGGameplayAbility::TryRouteApprovedHitReaction(const FHitResult& ServerHit, ERPGAttackHitReactionPolicy Policy,
	float StunDuration, const FRPGKnockbackSettings& Knockback, const FRPGSlowSettings& Slow, const FRPGApprovedHitContext& HitContext, const FRPGBurnSettings& Burn, const FRPGStaggerSettings& Stagger, const FRPGDefenseReductionSettings& DefenseReduction, const FRPGDamageTakenIncreaseSettings& DamageTakenIncrease)
{
	AActor* Source = CurrentActorInfo ? CurrentActorInfo->AvatarActor.Get() : nullptr;
	AActor* Target = ServerHit.GetActor();
	if (!HitContext.IsCurrent(Source, Target)) return true;
	const FGuid Receipt = FGuid::NewGuid();
	if (Slow.IsEnabled())
		if (auto* Control = Target->FindComponentByClass<URPGCombatControlComponent>())
			Control->ApplyApprovedDamageSlow(Source, ServerHit, Receipt, Slow);
	if (!HitContext.IsCurrent(Source, Target)) return true;
	if (Burn.IsEnabled())
		if (auto* Control = Target->FindComponentByClass<URPGCombatControlComponent>())
			Control->ApplyApprovedDamageBurn(Source, ServerHit, Receipt, Burn);
	if (!HitContext.IsCurrent(Source, Target)) return true;
	if (DefenseReduction.IsEnabled())
		if (auto* Control = Target->FindComponentByClass<URPGCombatControlComponent>())
			Control->ApplyApprovedDamageDefenseReduction(Source, ServerHit, Receipt, DefenseReduction);
	if (!HitContext.IsCurrent(Source, Target)) return true;
	if (DamageTakenIncrease.IsEnabled())
		if (auto* Control = Target->FindComponentByClass<URPGCombatControlComponent>())
			Control->ApplyApprovedDamageTakenIncrease(Source, ServerHit, Receipt, DamageTakenIncrease);
	if (!HitContext.IsCurrent(Source, Target)) return true;
	// A slow alone preserves the attack's existing legacy/cosmetic policy.
	if (Policy == ERPGAttackHitReactionPolicy::Legacy && StunDuration <= 0.f && !Knockback.IsEnabled() && !Stagger.IsEnabled())
		return RPGCombatImmunity::IsImmune(Target, ERPGCombatEffectKind::HitReaction);
	if (Knockback.IsEnabled())
		if (auto* Control = Target->FindComponentByClass<URPGCombatControlComponent>())
			Control->ApplyApprovedDamageKnockback(Source, ServerHit, Receipt, Knockback, HitContext.AwayDirection);
	if (StunDuration > 0.f)
		if (auto* Control = Target->FindComponentByClass<URPGCombatControlComponent>())
			if (HitContext.IsCurrent(Source, Target)) Control->ApplyApprovedDamageStun(Source, ServerHit, Receipt, StunDuration);
	if (Stagger.IsEnabled() && HitContext.IsCurrent(Source, Target))
		if (auto* Control=Target->FindComponentByClass<URPGCombatControlComponent>())
			Control->ApplyApprovedDamageStagger(Source,ServerHit,Receipt,Stagger,HitContext.AwayDirection);
	if (!HitContext.IsCurrent(Source, Target)) return true;
	if (Policy == ERPGAttackHitReactionPolicy::Cosmetic && !RPGCombatControl::IsActionBlocked(Target))
		if (auto* Receiver = Target->FindComponentByClass<URPGCombatReactionComponent>())
			Receiver->PresentApprovedDamageHit(Source, ServerHit, Receipt);
	// An unsupported cosmetic profile must never turn into gameplay interruption.
	return true;
}

////������ ��Ʈ UIŬ������ ��� �����غ���.
//void URPGGameplayAbility::DisplayDamageEffect
//	(AActor* InCachedTargetActor, float InWeaponBaseDamage, bool bCritical)
//{
//	//���߿� �����丵 �� ��
//	if (InCachedTargetActor)
//	{
//		ARPGBaseCharacter* TargetCharacter = Cast<ARPGBaseCharacter>(InCachedTargetActor);
//
//		if (TargetCharacter)
//		{
//			// ������ ��Ʈ�� ǥ���ϴ� �Լ� ȣ��
//			TargetCharacter->
//				ShowDamageFont(InWeaponBaseDamage, InCachedTargetActor->GetActorLocation(), bCritical);
//		}
//	}
//}
//
void URPGGameplayAbility::DisplayInvincibleEffect(AActor* InCachedTargetActor)
{
	if (InCachedTargetActor)
	{
		ARPGBaseCharacter* TargetCharacter = Cast<ARPGBaseCharacter>(InCachedTargetActor);

		if (TargetCharacter)
		{
			TargetCharacter->ShowInvincibleFont(InCachedTargetActor->GetActorLocation());
		}
	}
}
