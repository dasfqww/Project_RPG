// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/RPGAbilitySystemComponent.h"
#include "Ability/RPGGameplayAbility.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Ability/RPGPlayerGameplayAbility.h"
#include "RPGGameplayTags.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "Component/RPGPlayerMovementComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Player/RPGPlayerState.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "PlayerClass/RPGPlayerClassComponent.h"

#include "RPGDebugHelper.h"

bool URPGAbilitySystemComponent::IsLocalAbilityActorInfoReady() const
{
	const UWorld* World = GetWorld();
	const FGameplayAbilityActorInfo* Info = AbilityActorInfo.Get();
	const APawn* Pawn = Info ? Cast<APawn>(Info->AvatarActor.Get()) : nullptr;
	const APlayerController* Controller = Info ? Info->PlayerController.Get() : nullptr;
	return World && !World->bIsTearingDown && Info && Info->OwnerActor.IsValid()
		&& Pawn && !Pawn->IsActorBeingDestroyed() && Controller
		&& Controller->IsLocalController() && !Controller->IsActorBeingDestroyed()
		&& Controller->GetWorld() == World && Controller->GetPawn() == Pawn
		&& Pawn->GetController() == Controller && Info->IsLocallyControlled()
		&& Pawn->GetLocalRole() != ROLE_SimulatedProxy;
}

void URPGAbilitySystemComponent::ClientTryActivateAbility_Implementation(
	FGameplayAbilitySpecHandle AbilityToActivate)
{
	const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(AbilityToActivate);
	if (GetOwner() && !GetOwner()->HasAuthority() && Spec && Spec->Ability
		&& Spec->Ability->GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalOnly
		&& !IsLocalAbilityActorInfoReady())
	{
		// This is an existing granted spec and an actual server RPC, not a client
		// auto-activation on replication. Merge repeated pre-ready requests by handle.
		FDeferredLocalOnlyActivation& Pending = DeferredLocalOnlyActivations.FindOrAdd(AbilityToActivate);
		Pending.Owner = GetOwnerActor();
		Pending.Avatar = GetAvatarActor();
		Pending.World = GetWorld();
		return;
	}
	DeferredLocalOnlyActivations.Remove(AbilityToActivate);
	Super::ClientTryActivateAbility_Implementation(AbilityToActivate);
}

void URPGAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	if (AActor* OldAvatar = GetAvatarActor(); IsValid(OldAvatar) && OldAvatar != InAvatarActor)
		if (auto* Control = OldAvatar->FindComponentByClass<URPGCombatControlComponent>()) Control->ResetForAvatarReuse();
	if (GetOwner() && (InOwnerActor != GetOwner() || InAvatarActor != GetOwner()))
		if (auto* ClassOwner = GetOwner()->FindComponentByClass<URPGPlayerClassComponent>()) ClassOwner->ReleaseClassAbilities();
	if (AbilityActorInfo.IsValid()
		&& ((AbilityActorInfo->OwnerActor.IsValid() && AbilityActorInfo->OwnerActor.Get() != InOwnerActor)
			|| (AbilityActorInfo->AvatarActor.IsValid() && AbilityActorInfo->AvatarActor.Get() != InAvatarActor)))
	{
		DeferredLocalOnlyActivations.Reset();
	}
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);
	if (IsValid(InAvatarActor))
		if (auto* Control = InAvatarActor->FindComponentByClass<URPGCombatControlComponent>()) Control->InitializeWithAbilitySystem(*this);
	FlushDeferredLocalOnlyActivations();
	if (GetOwner())
		if (auto* ClassOwner = GetOwner()->FindComponentByClass<URPGPlayerClassComponent>()) ClassOwner->RefreshAppliedClass();
}

void URPGAbilitySystemComponent::ClearActorInfo()
{
	if (AActor* Avatar = GetAvatarActor(); IsValid(Avatar))
		if (auto* Control = Avatar->FindComponentByClass<URPGCombatControlComponent>()) Control->ResetForAvatarReuse();
	if (GetOwner())
		if (auto* ClassOwner = GetOwner()->FindComponentByClass<URPGPlayerClassComponent>()) ClassOwner->ReleaseClassAbilities();
	DeferredLocalOnlyActivations.Reset();
	Super::ClearActorInfo();
}

void URPGAbilitySystemComponent::RefreshLocalAbilityActorInfo()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn) { return; }
	if (GetOwnerActor() != Pawn || GetAvatarActor() != Pawn)
	{
		InitAbilityActorInfo(Pawn, Pawn);
		return;
	}
	RefreshAbilityActorInfo();
	FlushDeferredLocalOnlyActivations();
}

void URPGAbilitySystemComponent::FlushDeferredLocalOnlyActivations()
{
	if (!IsLocalAbilityActorInfoReady()) { return; }
	const TMap<FGameplayAbilitySpecHandle, FDeferredLocalOnlyActivation> Pending = MoveTemp(DeferredLocalOnlyActivations);
	DeferredLocalOnlyActivations.Reset();
	for (const auto& Entry : Pending)
	{
		// Activation can synchronously remove specs, clear actor info or change Pawn.
		if (!IsLocalAbilityActorInfoReady()) { return; }
		if (Entry.Value.Owner.Get() != GetOwnerActor() || Entry.Value.Avatar.Get() != GetAvatarActor()
			|| Entry.Value.World.Get() != GetWorld()) { continue; }
		const FGameplayAbilitySpecHandle Handle = Entry.Key;
		const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		if (!Spec || !Spec->Ability || Spec->IsActive()
			|| Spec->Ability->GetNetExecutionPolicy() != EGameplayAbilityNetExecutionPolicy::LocalOnly) { continue; }
		Super::ClientTryActivateAbility_Implementation(Handle);
	}
}

bool URPGAbilitySystemComponent::AreAbilityTagsBlocked(const FGameplayTagContainer& Tags) const
{
	return bDungeonGameplayAuthorityRevoked || IsDungeonGameplayRevoked(GetAvatarActor())
		|| Super::AreAbilityTagsBlocked(Tags);
}

bool URPGAbilitySystemComponent::RevokeDungeonGameplayAuthority(const ARPGPlayerState& RevokedState)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* Controller = RevokedState.GetPlayerController();
	if (bDungeonGameplayAuthorityRevoked || !RevokedState.HasAuthority()
		|| !RevokedState.IsAuthenticatedIdentityRevoked() || !IsValid(Pawn) || !Pawn->HasAuthority()
		|| !IsValid(Controller) || Controller->GetPlayerState<ARPGPlayerState>() != &RevokedState
		|| Controller->GetPawn() != Pawn || Pawn->GetController() != Controller
		|| Pawn->GetPlayerState<ARPGPlayerState>() != &RevokedState
		|| (GetAvatarActor() && GetAvatarActor() != Pawn)) return false;

	bDungeonGameplayAuthorityRevoked = true; // Survives unpossess/PlayerState replacement.
	TargetDataRequestSequences.Reset();
	if (auto* Security = Pawn->FindComponentByClass<URPGSecurityValidationComponent>())
		Security->CancelMovementAuthorization(NAME_None);
	if (auto* Combat = Pawn->FindComponentByClass<UPawnCombatComponent>())
		Combat->ToggleWeaponCollision(false);
	if (auto* Movement = Pawn->FindComponentByClass<URPGPlayerMovementComponent>())
		Movement->StopRevokedMovement();

	// Only this player's active executions. Do not clear specs, permanent effects,
	// health/death state or any other party member's ASC. Reentrant activation is blocked.
	ABILITYLIST_SCOPE_LOCK();
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		Spec.InputPressed = false;
		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			if (Instance && Instance->IsActive())
			{
				ConsumeAllReplicatedData(Spec.Handle, Instance->GetCurrentActivationInfo().GetActivationPredictionKey());
				Instance->SetCanBeCanceled(true);
			}
		}
	}
	CancelAllAbilities();
	return true;
}

FActiveGameplayEffect* URPGAbilitySystemComponent::GetActiveGameplayEffectMutable(const FActiveGameplayEffectHandle Handle)
{
	return ActiveGameplayEffects.GetActiveGameplayEffect(Handle);
}

TArray<FActiveGameplayEffectHandle> URPGAbilitySystemComponent::GetAllActiveEffectHandles() const
{
	return ActiveGameplayEffects.GetAllActiveEffectHandles();
}

void URPGAbilitySystemComponent::MarkActiveGameplayEffectDirty(FActiveGameplayEffect* ActiveGameplayEffect)
{
	if (ActiveGameplayEffect)
	{
		ActiveGameplayEffects.MarkItemDirty(*ActiveGameplayEffect);
	}
}

void URPGAbilitySystemComponent::CheckActiveEffectDuration(const FActiveGameplayEffectHandle Handle)
{
	ActiveGameplayEffects.CheckDuration(Handle);
}

void URPGAbilitySystemComponent::OnAbilityInputPressed(const FGameplayTag& InInputTag)
{
	if (IsDungeonGameplayRevoked(GetOwner()) || !InInputTag.IsValid())
	{
		return;
	}

	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (!AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InInputTag)) continue;
		if (RPGCombatControl::IsActionBlocked(GetAvatarActor()) && RPGCombatControl::IsStunAffectedAbility(AbilitySpec.Ability)) continue;

		if (InInputTag.MatchesTag(RPGGameplayTags::InputTag_Toggleable)&& AbilitySpec.IsActive())
		{
			CancelAbilityHandle(AbilitySpec.Handle);
		}
		else
		{
			OnAbilitySpecInputPressed(AbilitySpec.Handle);
		}
	}
}

void URPGAbilitySystemComponent::OnAbilityInputReleased(const FGameplayTag& InInputTag)
{
	if (IsDungeonGameplayRevoked(GetOwner()) || !InInputTag.IsValid())
	{
		return;
	}

	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InInputTag))
		{
			OnAbilitySpecInputReleased(AbilitySpec.Handle);
		}
	}
}

void URPGAbilitySystemComponent::OnAbilitySpecInputPressed(
	const FGameplayAbilitySpecHandle SpecHandle)
{
	if (IsDungeonGameplayRevoked(GetOwner())) return;
	FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle);
	if (!AbilitySpec)
	{
		return;
	}

	if (RPGCombatControl::IsActionBlocked(GetAvatarActor()) && RPGCombatControl::IsStunAffectedAbility(AbilitySpec->Ability)) return;
	AbilitySpec->InputPressed = true;
	if (AbilitySpec->IsActive())
	{
		AbilitySpecInputPressed(*AbilitySpec);
		return;
	}

	TryActivateAbility(AbilitySpec->Handle);
}

void URPGAbilitySystemComponent::OnAbilitySpecInputReleased(
	const FGameplayAbilitySpecHandle SpecHandle)
{
	if (IsDungeonGameplayRevoked(GetOwner())) return;
	FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle);
	if (!AbilitySpec)
	{
		return;
	}

	AbilitySpec->InputPressed = false;
	if (AbilitySpec->IsActive())
	{
		AbilitySpecInputReleased(*AbilitySpec);
	}
}

FGameplayAbilitySpecHandle URPGAbilitySystemComponent::FindUniqueAbilitySpecHandleByTag(
	const FGameplayTag& AbilityTag)
{
	if (!AbilityTag.IsValid())
	{
		return FGameplayAbilitySpecHandle();
	}

	TArray<FGameplayAbilitySpec*> MatchingAbilitySpecs;
	GetActivatableGameplayAbilitySpecsByAllMatchingTags(
		AbilityTag.GetSingleTagContainer(), MatchingAbilitySpecs);
	if (MatchingAbilitySpecs.Num() != 1 || !MatchingAbilitySpecs[0])
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Expected exactly one ability for tag %s, found %d."),
			*AbilityTag.ToString(),
			MatchingAbilitySpecs.Num());
		return FGameplayAbilitySpecHandle();
	}

	return MatchingAbilitySpecs[0]->Handle;
}

bool URPGAbilitySystemComponent::IsAbilitySpecInputPressed(
	const FGameplayAbilitySpecHandle SpecHandle) const
{
	const FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle);
	return AbilitySpec && AbilitySpec->InputPressed;
}

void URPGAbilitySystemComponent::AbilitySpecInputPressed(FGameplayAbilitySpec& Spec)
{
	UGameplayAbility* AbilityInstance = Spec.GetPrimaryInstance();
	if (AbilityInstance)
	{
		bool bShouldReplicateInput = true;
		if (URPGGameplayAbility* RPGAbility =
			Cast<URPGGameplayAbility>(AbilityInstance))
		{
			bShouldReplicateInput =
				RPGAbility->PreReplicateAbilityInputPressed();
		}

		// Notify GAS input tasks first. A policy may end the ability from its
		// direct InputPressed callback, which would otherwise destroy the task
		// before it can forward the event to a remote server.
		if (bShouldReplicateInput)
		{
			InvokeReplicatedEvent(
				EAbilityGenericReplicatedEvent::InputPressed,
				Spec.Handle,
				AbilityInstance->GetCurrentActivationInfo()
					.GetActivationPredictionKey());
		}
	}

	Super::AbilitySpecInputPressed(Spec);
}

void URPGAbilitySystemComponent::AbilitySpecInputReleased(FGameplayAbilitySpec& Spec)
{
	UGameplayAbility* AbilityInstance = Spec.GetPrimaryInstance();
	if (AbilityInstance)
	{
		bool bShouldReplicateInput = true;
		if (URPGGameplayAbility* RPGAbility =
			Cast<URPGGameplayAbility>(AbilityInstance))
		{
			bShouldReplicateInput =
				RPGAbility->PreReplicateAbilityInputReleased();
		}

		// The replicated task must observe release before a local policy can
		// synchronously end the ability and tear that task down.
		if (bShouldReplicateInput)
		{
			InvokeReplicatedEvent(
				EAbilityGenericReplicatedEvent::InputReleased,
				Spec.Handle,
				AbilityInstance->GetCurrentActivationInfo()
					.GetActivationPredictionKey());
		}
	}

	Super::AbilitySpecInputReleased(Spec);
}

void URPGAbilitySystemComponent::GrantPlayerWeaponAbilities(const TArray<FRPGPlayerAbilitySet> InDefaultWeaponAbilities, 
	const TArray<FRPGPlayerSkillSet>& InSkillAbilities, int32 ApplyLevel, 
	TArray<FGameplayAbilitySpecHandle>& OutGrantedAbilitySpecHandles)
{
	if (!IsOwnerActorAuthoritative()) return;
	const auto* ClassOwner = GetAvatarActor() ? GetAvatarActor()->FindComponentByClass<URPGPlayerClassComponent>() : nullptr;

	for (const FRPGPlayerAbilitySet& AbilitySet : InDefaultWeaponAbilities)
	{
		if (!AbilitySet.IsValid()) continue;
		if (ClassOwner && ClassOwner->RoutesAbilityToClass(AbilitySet.AbilityToGrant.Get(), AbilitySet.InputTag)) continue;

		FGameplayAbilitySpec AbilitySpec;
		if (!AbilitySet.BuildAbilitySpec(ApplyLevel, GetAvatarActor(), AbilitySpec)) continue;

		OutGrantedAbilitySpecHandles.AddUnique(GiveAbility(AbilitySpec));
	}
	
	for (const FRPGPlayerSkillSet& AbilitySet : InSkillAbilities)
	{
		if (!AbilitySet.IsValid()) continue;
		if (ClassOwner && ClassOwner->RoutesAbilityToClass(AbilitySet.AbilityToGrant.Get(), AbilitySet.InputTag)) continue;

		FGameplayAbilitySpec AbilitySpec;
		if (!AbilitySet.BuildAbilitySpec(ApplyLevel, GetAvatarActor(), AbilitySpec)) continue;

		OutGrantedAbilitySpecHandles.AddUnique(GiveAbility(AbilitySpec));
	}
}

void URPGAbilitySystemComponent::RemovedGrantedPlayerWeaponAbilities(UPARAM(ref) TArray<FGameplayAbilitySpecHandle>& InSpecHandlesToRemove)
{
	if (InSpecHandlesToRemove.IsEmpty())
	{
		return;
	}

	for (const FGameplayAbilitySpecHandle& SpecHandle : InSpecHandlesToRemove)
	{
		const auto* Spec = FindAbilitySpecFromHandle(SpecHandle);
		const auto* ClassOwner = GetAvatarActor() ? GetAvatarActor()->FindComponentByClass<URPGPlayerClassComponent>() : nullptr;
		if (Spec && Spec->Ability && ClassOwner && ClassOwner->OwnsAbilitySpec(SpecHandle, Spec->Ability->GetClass())) continue;
		if (SpecHandle.IsValid())
		{
			ClearAbility(SpecHandle);
		}
	}

	InSpecHandlesToRemove.Empty();
}

bool URPGAbilitySystemComponent::TryActivateAbilityByTag(FGameplayTag AbilityTagToActivate)
{
	const FGameplayAbilitySpecHandle SpecHandle =
		FindUniqueAbilitySpecHandleByTag(AbilityTagToActivate);
	return SpecHandle.IsValid() && TryActivateAbility(SpecHandle);
}
