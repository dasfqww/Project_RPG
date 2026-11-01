#include "Component/Combat/RPGCombatControlComponent.h"
#include "Ability/RPGGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

struct FRPGMontageImmunityRuntime
{
	TWeakObjectPtr<URPGGameplayAbility> Ability;
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	TWeakObjectPtr<UAnimInstance> Anim;
	TWeakObjectPtr<UAnimMontage> Montage;
	FGuid Execution;
	int32 InstanceId = INDEX_NONE, EncounterId = 0;
	FRPGApprovedHitContext Lifetime;
	TArray<FRPGMontageImmunityWindow> Windows;
	TMap<int32, FGuid> Tokens;
	TSet<int32> Attempted;
};

void URPGCombatControlComponent::TrackMontageImmunity(UAbilitySystemComponent& ASC, URPGGameplayAbility& Ability,
	FGuid Execution, UAnimMontage& Montage, TArray<FRPGMontageImmunityWindow> Windows)
{
	auto* Character = Cast<ACharacter>(GetOwner());
	if (bEnding || bClearingAbilityImmunities || !Character || !Character->HasAuthority() || !IsLivingAvatar(Character)
		|| Windows.IsEmpty() || Windows.Num() > 8 || !Execution.IsValid() || !Ability.CanUseMontageImmunity()
		|| Ability.GetCombatExecutionId() != Execution || Ability.GetAvatarActorFromActorInfo() != Character
		|| ASC.GetAvatarActor() != Character || ASC.GetAnimatingAbility() != &Ability || ASC.GetCurrentMontage() != &Montage) return;
	auto* Mesh = Character->GetMesh(); auto* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	auto* Instance = Anim ? Anim->GetActiveInstanceForMontage(&Montage) : nullptr;
	if (!Instance || !ASC.AbilityActorInfo.IsValid() || ASC.AbilityActorInfo->GetAnimInstance() != Anim) return;
	const auto* Boss = Character->FindComponentByClass<URPGBossAIComponent>();
	if (Boss && Boss->GetBossState().EncounterState != ERPGBossEncounterState::InCombat) return;
	InitializeWithAbilitySystem(ASC); StopMontageImmunity();
	// Cleanup callbacks may have replaced the animation/activation.
	if (!Ability.CanUseMontageImmunity() || Ability.GetCombatExecutionId() != Execution || ASC.GetAnimatingAbility() != &Ability
		|| ASC.GetCurrentMontage() != &Montage || Anim != Mesh->GetAnimInstance() || Instance != Anim->GetActiveInstanceForMontage(&Montage)) return;
	auto Runtime = MakeShared<FRPGMontageImmunityRuntime>();
	Runtime->Ability = &Ability; Runtime->Mesh = Mesh; Runtime->Anim = Anim; Runtime->Montage = &Montage;
	Runtime->Execution = Execution; Runtime->InstanceId = Instance->GetInstanceID(); Runtime->Windows = MoveTemp(Windows);
	Runtime->Lifetime = FRPGApprovedHitContext(Character, Character); Runtime->EncounterId = Boss ? Boss->GetBossState().EncounterId : 0;
	MontageImmunityRuntime = Runtime;
	// Gameplay montages must progress on a hidden dedicated-server mesh. Upgrade only the insufficient option;
	// do not restore a cached mesh setting over a later system's choice or alter pose/root-motion manually.
	if (Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered)
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
	AddTickPrerequisiteComponent(Mesh); SetComponentTickInterval(0.f); SetComponentTickEnabled(true);
	RefreshMontageImmunity();
}

void URPGCombatControlComponent::StopMontageImmunity()
{
	const auto Old = MoveTemp(MontageImmunityRuntime);
	if (!Old) return;
	if (Old->Mesh.IsValid()) RemoveTickPrerequisiteComponent(Old->Mesh.Get());
	SetComponentTickInterval(.05f);
	const auto Tokens = MoveTemp(Old->Tokens);
	for (const auto& Entry : Tokens) EndAbilityImmunity(Entry.Value);
}
void URPGCombatControlComponent::EndMontageImmunityForAbility(UGameplayAbility& Ability, FGuid Execution)
{
	if (MontageImmunityRuntime && MontageImmunityRuntime->Ability.Get() == &Ability && MontageImmunityRuntime->Execution == Execution)
		StopMontageImmunity();
}

void URPGCombatControlComponent::RefreshMontageImmunity()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !MontageImmunityRuntime) return;
	const auto Runtime = MontageImmunityRuntime;
	auto* ASC = BoundASC.Get(); auto* Ability = Runtime->Ability.Get(); auto* Anim = Runtime->Anim.Get(); auto* Mesh = Runtime->Mesh.Get();
	auto* Character = Cast<ACharacter>(GetOwner()); const auto* Boss = GetOwner()->FindComponentByClass<URPGBossAIComponent>();
	auto* Instance = Anim ? Anim->GetMontageInstanceForID(Runtime->InstanceId) : nullptr;
	if (bEnding || bClearingAbilityImmunities || !ASC || !Character || !Runtime->Lifetime.IsCurrent(Character, Character)
		|| !Ability || !Ability->CanUseMontageImmunity() || Ability->GetCombatExecutionId() != Runtime->Execution
		|| Ability->GetAbilitySystemComponentFromActorInfo() != ASC || ASC->GetAnimatingAbility() != Ability
		|| ASC->GetCurrentMontage() != Runtime->Montage.Get() || Character->GetMesh() != Mesh || !Mesh || Mesh->GetAnimInstance() != Anim
		|| !ASC->AbilityActorInfo.IsValid() || ASC->AbilityActorInfo->GetAnimInstance() != Anim || !Instance || !Instance->IsActive()
		|| Instance->Montage != Runtime->Montage.Get() || (Boss && (Boss->GetBossState().EncounterId != Runtime->EncounterId
			|| Boss->GetBossState().EncounterState != ERPGBossEncounterState::InCombat)))
	{ StopMontageImmunity(); return; }
	// Reentrant queries may retire out-of-window/pending leases, but cannot recursively grant new effects.
	const bool bCanGrant = !bRefreshingMontageImmunity;
	TGuardValue<bool> Refreshing(bRefreshingMontageImmunity, true);
	for (int32 Index = 0; Index < Runtime->Windows.Num(); ++Index)
	{
		if (MontageImmunityRuntime != Runtime || Ability->GetCombatExecutionId() != Runtime->Execution) return;
		Instance = Anim->GetMontageInstanceForID(Runtime->InstanceId);
		if (!Instance || !Instance->IsActive() || !Mesh || Mesh->GetAnimInstance() != Anim
			|| ASC->GetCurrentMontage() != Runtime->Montage.Get() || ASC->GetAnimatingAbility() != Ability)
		{ StopMontageImmunity(); return; }
		const bool bAdvancing = Instance->IsPlaying() && Mesh->IsComponentTickEnabled() && !Mesh->bPauseAnims
			&& FMath::IsFinite(Instance->GetPlayRate()) && Instance->GetPlayRate() != 0.f
			&& FMath::IsFinite(Mesh->GlobalAnimRateScale) && Mesh->GlobalAnimRateScale != 0.f
			&& FMath::IsFinite(Runtime->Montage->RateScale) && Runtime->Montage->RateScale != 0.f;
		const bool bInside = bAdvancing && Runtime->Windows[Index].Contains(Instance->GetPosition());
		if (!bInside)
		{
			Runtime->Attempted.Remove(Index); FGuid Token;
			if (Runtime->Tokens.RemoveAndCopyValue(Index, Token)) EndAbilityImmunity(Token);
		}
		else if (bCanGrant && !Runtime->Attempted.Contains(Index))
		{
			Runtime->Attempted.Add(Index); const auto Token = FGuid::NewGuid(); Runtime->Tokens.Add(Index, Token);
			const bool bApplied = BeginAbilityImmunity(*Ability, Token, Runtime->Windows[Index].Immunities);
			if (!bApplied || MontageImmunityRuntime != Runtime || Ability->GetCombatExecutionId() != Runtime->Execution)
			{ Runtime->Tokens.Remove(Index); EndAbilityImmunity(Token); }
			// Recheck cancellation, mesh replacement and seeks made by application delegates before returning to a hit approver.
			if (MontageImmunityRuntime == Runtime) RefreshMontageImmunity();
		}
	}
}
