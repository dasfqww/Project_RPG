#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Ability/Skill/RPGGreatSwordResolveAbility.h"
#if !UE_BUILD_SHIPPING
#include "Tests/RPGSkillNetworkE2EProbeComponent.h"
#include "Tests/RPGSkillDamageNetworkProbe.h"
#include "Tests/RPGSkillEndNetworkObservation.h"
#endif

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Ability/AbilityTask/RPGAbilityTask_PlayScaledSkillMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "GameplayPrediction.h"
#include "Engine/World.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/HitQuery/RPGHitQuerySubsystem.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Controller/RPGPlayerController.h"
#include "FunctionLibrary/RPGSecurityBlueprintLibrary.h"
#include "GameFramework/PlayerController.h"
#include "Interface/PawnUIInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGSkillAction.h"
#include "Skill/RPGClassSkillEligibility.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillTargetingPolicy.h"
#include "TimerManager.h"
#include "Component/Combat/RPGCombatContributionComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGGameplayAbility_SkillContainer)

URPGGameplayAbility_SkillContainer::URPGGameplayAbility_SkillContainer()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	bUseLegacyManualManaCost = false;
}

UGameplayEffect* URPGGameplayAbility_SkillContainer::GetCooldownGameplayEffect() const
{
	// The player base still applies/checks the per-definition GAS cooldown.
	// Returning no legacy effect also excludes its tags from readiness/UI queries.
	return nullptr;
}

float URPGGameplayAbility_SkillContainer::GetRPGCooldownDuration(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	return ActiveSkillSpec.GetCooldownDuration(MinimumRepeatCooldown);
}

const UObject*
URPGGameplayAbility_SkillContainer::GetRPGCooldownSourceObject() const
{
	return SkillDefinition ? SkillDefinition.Get() : Super::GetRPGCooldownSourceObject();
}

bool URPGGameplayAbility_SkillContainer::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	return SkillDefinition && SkillDefinition->ActivationImmunities.IsValid() && RPGClassSkillEligibility::CanActivate(
		ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr, SkillDefinition->SkillTag, SkillDefinition, GetClass(), Handle)
		&& Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void URPGGameplayAbility_SkillContainer::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Recheck before Blueprint/parent callbacks or spending resources, including direct/event starts.
	if (!SkillDefinition || !SkillDefinition->ActivationImmunities.IsValid() || !RPGClassSkillEligibility::CanActivate(
		ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr, SkillDefinition->SkillTag, SkillDefinition, GetClass(), Handle))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	const uint64 ActivationSerial = ++SkillDamageActivationSerial;
	ServerAppliedHitCount = 0;
	CommittedResolveDamageMultiplier = 1.f;
	bCommittedDuringBerserk = false;
	bSkillCommitted = false;
	bResolveHitAwarded = false;
	bFinisherHitAttempted = false;
	SkillMontageAnimInstance.Reset();
	SkillMontageInstanceId = INDEX_NONE;
	CaptureSkillBossEncounters(ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr);
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive() || SkillDamageActivationSerial != ActivationSerial)
	{
		return;
	}

	if (!SkillDefinition)
	{
		UE_LOG(LogTemp, Error, TEXT("SkillDefinition is missing in %s"), *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FRPGSkillSaveData SaveData;
	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	URPGPlayerSkillComponent* SkillComponent = AvatarActor
		? AvatarActor->FindComponentByClass<URPGPlayerSkillComponent>()
		: nullptr;
	if (!SkillComponent || !SkillComponent->TryGetSkillSaveData(SkillDefinition->SkillTag, SaveData))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	SkillDefinition->BuildRuntimeSpec(AvatarActor, SaveData, ActiveSkillSpec);
	if (const URPGAbilitySystemComponent* ASC =
		Cast<URPGAbilitySystemComponent>(
			GetAbilitySystemComponentFromActorInfo()))
	{
		bSkillInputPressed =
			ASC->IsAbilitySpecInputPressed(CurrentSpecHandle);
	}
	else
	{
		bSkillInputPressed = false;
	}

	if (!ActiveSkillSpec.ExecutionPolicyClass && !ActiveSkillSpec.ActionClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("No execution policy or legacy action found for Definition %s"),
			*SkillDefinition->GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (ActiveSkillSpec.ExecutionPolicyClass)
	{
		ActiveExecutionPolicy = NewObject<URPGSkillExecutionPolicy>(
			this,
			ActiveSkillSpec.ExecutionPolicyClass);
		FText ValidationError;
		if (!ActiveExecutionPolicy ||
			!ActiveExecutionPolicy->ValidateExecutionConfig(
				ActiveSkillSpec.ExecutionConfig,
				ValidationError))
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Invalid execution config for Definition %s: %s"),
				*SkillDefinition->GetName(),
				*ValidationError.ToString());
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		ActiveExecutionPolicy->Initialize(*this);
		if (!ActiveExecutionPolicy->ValidateRuntimeSpec(ValidationError))
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Invalid runtime execution data for Definition %s: %s"),
				*SkillDefinition->GetName(),
				*ValidationError.ToString());
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	if (!InitializeTargetingPolicy())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Unable to initialize skill targeting for Definition %s"),
			*SkillDefinition->GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (ShouldWaitForRemoteTargetData())
	{
		if (!WaitForInitialReplicatedTargetData())
		{
			if (IsActive() && SkillDamageActivationSerial == ActivationSerial) EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		}
		return;
	}

	if (!RefreshSkillTarget() || !StartSkillAfterTargetReady())
	{
		if (IsActive() && SkillDamageActivationSerial == ActivationSerial) EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
	return;
}

bool URPGGameplayAbility_SkillContainer::StartSkillAfterTargetReady()
{
	if (!ActiveTargetResult.bIsValid)
	{
		return false;
	}

	if (ActiveSkillSpec.ExecutionPolicyClass)
	{
		if (!StartPolicyEventTask())
		{
			return false;
		}
		// Execution-window and damage events are independent. Existing BP notifies
		// use virtual damage handlers below, with this activation's frozen query.
		if (EventTag.IsValid())
		{
			ExecWaitGameplayEvent();
		}
		StartExecutionInputTasks();
		if (!CommitSkillAndNotifyCooldown())
		{
			return false;
		}
		return ActiveExecutionPolicy && ActiveExecutionPolicy->StartExecution();
	}

	if (!CommitSkillAndNotifyCooldown())
	{
		return false;
	}
	if (EventTag.IsValid())
	{
		ExecWaitGameplayEvent();
	}

	// Compatibility path while existing Action-based assets are migrated.
	ActiveAction = NewObject<URPGSkillAction>(this, ActiveSkillSpec.ActionClass);
	if (!ActiveAction)
	{
		return false;
	}

	ActiveAction->Initialize(this, SkillDefinition, ActiveSkillSpec);
	StartExecutionInputTasks();
	ActiveAction->StartAction();
	return true;
}

bool URPGGameplayAbility_SkillContainer::CommitSkillAndNotifyCooldown()
{
	const uint64 ActivationSerial = SkillDamageActivationSerial;
	if (bSkillCommitted) return IsActive();
	if (!URPGGreatSwordResolveAbility::CanCommitSkill(*this)) return false;
	// Reserve before GAS delegates can reenter this same commit.
	bSkillCommitted = true;
	// Do not call the legacy CoolDown(): it commits again before broadcasting.
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		return false;
	}
	if (!IsActive() || SkillDamageActivationSerial != ActivationSerial) return false;
	// Freeze cost, damage and gain suppression immediately after the accepted commit.
	if (!URPGGreatSwordResolveAbility::ConsumeForCommittedSkill(*this, CommittedResolveDamageMultiplier, bCommittedDuringBerserk)) return false;
	if (!IsActive() || SkillDamageActivationSerial != ActivationSerial) return false;
	if (!StartActivationImmunity(ActiveSkillSpec.ActivationImmunities)
		|| !IsActive() || SkillDamageActivationSerial != ActivationSerial) return false;
	// Cancellation/misses never refund it; all hits of this cast use the same value.
	bMontageImmunityReady = true;
	if (!IsActive() || SkillDamageActivationSerial != ActivationSerial) return false;
	if (!CurrentActorInfo || !CurrentActorInfo->IsLocallyControlled()) return true;
	IPawnUIInterface* PawnUI = Cast<IPawnUIInterface>(CurrentActorInfo->AvatarActor.Get());
	UPlayerUIComponent* UI = PawnUI ? PawnUI->GetPlayerUIComponent() : nullptr;
	if (!UI) return true;

	// Prefer this grant's slot mapping over a possibly stale Blueprint default.
	FGameplayTag CooldownInputTag = InputTag;
	if (const FGameplayAbilitySpec* Spec = GetCurrentAbilitySpec())
	{
		const FGameplayTag InputRoot = FGameplayTag::RequestGameplayTag(TEXT("InputTag"), false);
		FGameplayTagContainer SlotTags;
		for (const FGameplayTag& Tag : Spec->GetDynamicSpecSourceTags())
		{
			if (InputRoot.IsValid() && Tag.MatchesTag(InputRoot)) SlotTags.AddTag(Tag);
		}
		if (SlotTags.Num() == 1) CooldownInputTag = SlotTags.First();
		else if (SlotTags.Num() > 1 && !SlotTags.HasTagExact(CooldownInputTag)) return true;
	}
	float Remaining = 0.0f, Duration = 0.0f;
	GetCooldownTimeRemainingAndDuration(CurrentSpecHandle, CurrentActorInfo, Remaining, Duration);
	if (CooldownInputTag.IsValid() && FMath::IsFinite(Duration) && Duration > 0.0f &&
		FMath::IsFinite(Remaining) && Remaining > 0.0f)
	{
		// Presentation only: no extra effect, timer, cost, RPC or D1 mutation.
		UI->OnAbilityCooldownBegin.Broadcast(CooldownInputTag, Duration, Remaining);
	}
	// UI callbacks may cancel the ability. Never start a disposed policy/action.
	return IsActive();
}

void URPGGameplayAbility_SkillContainer::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	// Ignore late/reentrant ends before touching activation-local policy state.
	if (!IsEndAbilityValid(Handle, ActorInfo)) return;
	bMontageImmunityReady = false;
	// Invalidate before policy/action cancellation callbacks can dispatch damage.
	CaptureSkillBossEncounters(nullptr);
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGSkillNetE2E")))
	{
		FRPGSkillEndNetworkObservation::ObserveEnd(*this,
			ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr, Handle, ActivationInfo,
			bReplicateEndAbility, bWasCancelled);
		UE_LOG(LogTemp, Display, TEXT("%sRPG_SKILL_END_TRACE Avatar=%s Ability=%s Original=%d Cancelled=%d Replicate=%d Active=%d Ending=%d NetSecurity=%d"),
			*FRPGSkillDamageNetworkProbe::LogContext(GetAvatarActorFromActorInfo()),
			*GetNameSafe(GetAvatarActorFromActorInfo()), *GetName(), ActivationInfo.GetActivationPredictionKey().Current,
			bWasCancelled, bReplicateEndAbility, IsActive(), bIsAbilityEnding, static_cast<int32>(NetSecurityPolicy));
	}
#endif
	RemoveReplicatedTargetDataDelegate();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(
			InitialTargetDataTimeoutHandle);
	}
	bWaitingForInitialTargetData = false;
	bPreparedTargetForInputReplication = false;
	bReleaseTargetRefreshFailed = false;
	ResetRemotePolicyEventSync();

	if (ActivePolicyEventTask)
	{
		ActivePolicyEventTask->EndTask();
		ActivePolicyEventTask = nullptr;
	}
	if (ActiveInputPressedTask)
	{
		ActiveInputPressedTask->EndTask();
		ActiveInputPressedTask = nullptr;
	}
	if (ActiveInputReleasedTask)
	{
		ActiveInputReleasedTask->EndTask();
		ActiveInputReleasedTask = nullptr;
	}

	if (ActiveExecutionPolicy)
	{
		if (bWasCancelled)
		{
			ActiveExecutionPolicy->CancelExecution();
		}
		else
		{
			ActiveExecutionPolicy->EndExecution();
		}
		ActiveExecutionPolicy = nullptr;
	}
	ActiveTargetingPolicy = nullptr;

	if (ActiveAction)
	{
		if (bWasCancelled)
		{
			ActiveAction->CancelAction();
		}
		else
		{
			ActiveAction->EndAction();
		}
		ActiveAction = nullptr;
	}
	StopSkillPersistentVFX();
	ActiveMontageTask = nullptr;
	ActiveTargetResult.Reset();
	ActiveSkillSpec.Reset();
	ServerAppliedHitCount = 0;
	CommittedResolveDamageMultiplier = 1.f;
	bCommittedDuringBerserk = false;
	bSkillCommitted = false;
	bResolveHitAwarded = false;
	bSkillInputPressed = false;

	Super::EndAbility(
		Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPGGameplayAbility_SkillContainer::CancelAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateCancel)
{
	Super::CancelAbility(Handle, ActorInfo, ActivationInfo, bReplicateCancel);
}

void URPGGameplayAbility_SkillContainer::InputPressed(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputPressed(Handle, ActorInfo, ActivationInfo);
	DispatchExecutionInputPressed();
	bPreparedTargetForInputReplication = false;
}

void URPGGameplayAbility_SkillContainer::InputReleased(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);
	DispatchExecutionInputReleased();
	bPreparedTargetForInputReplication = false;
}

bool URPGGameplayAbility_SkillContainer::PreReplicateAbilityInputPressed()
{
	// Instant execution ignores subsequent input. Replicate the input state,
	// but do not send a redundant aim sample that can outlive a killed target.
	if (ActiveExecutionPolicy && !ActiveExecutionPolicy->RequiresInputTargetRefresh()) { return true; }
	if (IsActive() && !ShouldWaitForRemoteTargetData())
	{
		bPreparedTargetForInputReplication = RefreshSkillTarget();
		return bPreparedTargetForInputReplication;
	}
	return true;
}

bool URPGGameplayAbility_SkillContainer::PreReplicateAbilityInputReleased()
{
	if (ActiveExecutionPolicy && !ActiveExecutionPolicy->RequiresInputTargetRefresh()) { return true; }
	if (IsActive() && !ShouldWaitForRemoteTargetData())
	{
		bPreparedTargetForInputReplication = RefreshSkillTarget();
		bReleaseTargetRefreshFailed = !bPreparedTargetForInputReplication;
		if (bReleaseTargetRefreshFailed)
		{
			// Never retain the previous aim when a release cannot resolve a target.
			ActiveTargetResult.Reset();
		}
		return bPreparedTargetForInputReplication;
	}
	return true;
}

void URPGGameplayAbility_SkillContainer::DispatchExecutionInputPressed()
{
	bSkillInputPressed = true;
	if (ActiveExecutionPolicy)
	{
		ActiveExecutionPolicy->OnInputPressed();
	}
	else if (ActiveAction)
	{
		ActiveAction->OnInputPressed();
	}
}

void URPGGameplayAbility_SkillContainer::DispatchExecutionInputReleased()
{
	bSkillInputPressed = false;
	if (bReleaseTargetRefreshFailed)
	{
		bReleaseTargetRefreshFailed = false;
		// Natural client ends are intentionally ignored by the server for policy
		// skills. Use GAS's explicit cancellation path instead of swallowing release
		// and leaving the remote execution waiting on input that can no longer arrive.
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
		return;
	}
	if (ActiveExecutionPolicy)
	{
		ActiveExecutionPolicy->OnInputReleased();
	}
	else if (ActiveAction)
	{
		ActiveAction->OnInputReleased();
	}
}

void URPGGameplayAbility_SkillContainer::OnActionEnded()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

UWorld* URPGGameplayAbility_SkillContainer::GetSkillExecutionWorld() const
{
	return GetWorld();
}

const FRPGSkillRuntimeSpec&
URPGGameplayAbility_SkillContainer::GetSkillRuntimeSpec() const
{
	return ActiveSkillSpec;
}

const FRPGSkillTargetResult&
URPGGameplayAbility_SkillContainer::GetSkillTargetResult() const
{
	return ActiveTargetResult;
}

bool URPGGameplayAbility_SkillContainer::RefreshSkillTarget()
{
	return RefreshSkillTargetWithApplicationTag(FGameplayTag());
}

bool URPGGameplayAbility_SkillContainer::RefreshSkillTargetWithApplicationTag(
	const FGameplayTag ApplicationTag)
{
	if (ShouldWaitForRemoteTargetData())
	{
		return ActiveTargetResult.bIsValid;
	}
	if (!ApplicationTag.IsValid() && bPreparedTargetForInputReplication)
	{
		return ActiveTargetResult.bIsValid;
	}

	if (!ActiveTargetingPolicy)
	{
		if (!ResolveLegacyTarget())
		{
			return false;
		}
		return SendActiveTargetDataToServer(ApplicationTag);
	}

	FRPGSkillTargetResult ResolvedTarget;
	if (!ActiveTargetingPolicy->ResolveTarget(ResolvedTarget) ||
		!ResolvedTarget.bIsValid)
	{
		return false;
	}

	ActiveTargetResult = MoveTemp(ResolvedTarget);
	ApplyResolvedAimRotation();
	return SendActiveTargetDataToServer(ApplicationTag);
}

bool URPGGameplayAbility_SkillContainer::IsSkillInputPressed() const
{
	return bSkillInputPressed;
}

bool URPGGameplayAbility_SkillContainer::PlaySkillMontage(
	const FName StartSection)
{
	if (!ActiveSkillSpec.Montage || ActiveMontageTask)
	{
		return false;
	}
	if (!StartSection.IsNone() &&
		ActiveSkillSpec.Montage->GetSectionIndex(StartSection) == INDEX_NONE)
	{
		return false;
	}

	float MovementScale = 1.f;
	if (!ActiveSkillSpec.TryGetMontageMovementScale(MovementScale)) return false;
	ActiveMontageTask = MovementScale == 1.f
		? UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("SkillExecutionMontage"),
			ActiveSkillSpec.Montage,
			FMath::Max(AttackSpeed, 0.01f),
			StartSection, true, 1.f, 0.f, !ActiveSkillSpec.ContributionHitSection.IsNone())
		: URPGAbilityTask_PlayScaledSkillMontageAndWait::PlayScaledSkillMontage(
			this, ActiveSkillSpec.Montage, FMath::Max(AttackSpeed, 0.01f), StartSection);
	if (!ActiveMontageTask)
	{
		return false;
	}

	ActiveMontageTask->OnCompleted.AddDynamic(
		this, &ThisClass::HandlePolicyMontageCompleted);
	ActiveMontageTask->OnInterrupted.AddDynamic(
		this, &ThisClass::HandlePolicyMontageInterrupted);
	ActiveMontageTask->OnCancelled.AddDynamic(
		this, &ThisClass::HandlePolicyMontageInterrupted);
	const uint64 MontageActivation = SkillDamageActivationSerial;
	ActiveMontageTask->ReadyForActivation();
	if (!ActiveSkillSpec.ContributionHitSection.IsNone())
	{
		if (!IsActive() || bIsAbilityEnding || SkillDamageActivationSerial != MontageActivation) return false;
		const auto* Info = GetCurrentActorInfo();
		UAnimInstance* Anim = Info ? Info->GetAnimInstance() : nullptr;
		const FAnimMontageInstance* Instance = Anim ? Anim->GetActiveInstanceForMontage(ActiveSkillSpec.Montage) : nullptr;
		if (!Instance) return false;
		SkillMontageAnimInstance = Anim;
		SkillMontageInstanceId = Instance->GetInstanceID();
	}
	return true;
}

bool URPGGameplayAbility_SkillContainer::JumpToSkillMontageSection(
	const FName SectionName)
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance =
		ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	if (!AnimInstance || !ActiveSkillSpec.Montage || SectionName.IsNone() ||
		ActiveSkillSpec.Montage->GetSectionIndex(SectionName) == INDEX_NONE ||
		!AnimInstance->Montage_IsPlaying(ActiveSkillSpec.Montage))
	{
		return false;
	}

	AnimInstance->Montage_JumpToSection(SectionName, ActiveSkillSpec.Montage);
	return true;
}

void URPGGameplayAbility_SkillContainer::FinishSkillExecution(
	const bool bWasCancelled)
{
	if (IsActive())
	{
		EndAbility(
			CurrentSpecHandle,
			CurrentActorInfo,
			CurrentActivationInfo,
			true,
			bWasCancelled);
	}
}

void URPGGameplayAbility_SkillContainer::ShowSkillProgress()
{
	ShowProgressBar_Internal();
}

void URPGGameplayAbility_SkillContainer::HideSkillProgress()
{
	HiddenProgressBar_Internal();
}

void URPGGameplayAbility_SkillContainer::UpdateSkillProgress(
	const float Current,
	const float Maximum)
{
	const float SafeMaximum = FMath::Max(Maximum, KINDA_SMALL_NUMBER);
	FString TimeText = FString::Printf(TEXT("%.1fs"), Current);
	UpdateProgressBar_Internal(TimeText, Current, SafeMaximum);
}

void URPGGameplayAbility_SkillContainer::NotifySkillProgressCompleted()
{
	ProgressCompleted_Internal();
}

void URPGGameplayAbility_SkillContainer::StartSkillPersistentVFX()
{
	if (PersistentSkillVFX || !ActiveSkillSpec.VFX)
	{
		return;
	}

	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	USceneComponent* AttachComponent =
		AvatarActor ? AvatarActor->GetRootComponent() : nullptr;
	if (!AttachComponent)
	{
		return;
	}

	PersistentSkillVFX = UNiagaraFunctionLibrary::SpawnSystemAttached(
		ActiveSkillSpec.VFX,
		AttachComponent,
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		true);
}

void URPGGameplayAbility_SkillContainer::StopSkillPersistentVFX()
{
	if (PersistentSkillVFX)
	{
		PersistentSkillVFX->DestroyComponent();
		PersistentSkillVFX = nullptr;
	}
}

UWorld* URPGGameplayAbility_SkillContainer::GetSkillTargetingWorld() const
{
	return GetWorld();
}

AActor* URPGGameplayAbility_SkillContainer::GetSkillSourceActor() const
{
	return GetAvatarActorFromActorInfo();
}

bool URPGGameplayAbility_SkillContainer::GetSkillCameraAimRay(
	FVector& OutOrigin,
	FVector& OutDirection) const
{
	OutOrigin = FVector::ZeroVector;
	OutDirection = FVector::ZeroVector;

	APlayerController* PlayerController =
		CurrentActorInfo
			? CurrentActorInfo->PlayerController.Get()
			: nullptr;
	if (PlayerController)
	{
		FRotator ViewRotation;
		PlayerController->GetPlayerViewPoint(OutOrigin, ViewRotation);
		OutDirection = ViewRotation.Vector().GetSafeNormal();
		return !OutDirection.IsNearlyZero();
	}

	AActor* SourceActor = GetSkillSourceActor();
	if (!SourceActor)
	{
		return false;
	}

	FRotator ViewRotation;
	SourceActor->GetActorEyesViewPoint(OutOrigin, ViewRotation);
	OutDirection = ViewRotation.Vector().GetSafeNormal();
	return !OutDirection.IsNearlyZero();
}

AActor* URPGGameplayAbility_SkillContainer::GetSkillLockedTarget() const
{
	const ARPGPlayerController* PlayerController =
		CurrentActorInfo
			? Cast<ARPGPlayerController>(
				CurrentActorInfo->PlayerController.Get())
			: nullptr;
	return PlayerController
		? PlayerController->GetSkillLockedTarget()
		: nullptr;
}

const FRPGHitQueryFilter&
URPGGameplayAbility_SkillContainer::GetSkillTargetValidationFilter() const
{
	return ActiveSkillSpec.TargetingProfile.Filter;
}

bool URPGGameplayAbility_SkillContainer::InitializeTargetingPolicy()
{
	ActiveTargetingPolicy = nullptr;
	ActiveTargetResult.Reset();

	if (!ActiveSkillSpec.TargetingPolicyClass)
	{
		if (ActiveSkillSpec.TargetingConfig.IsValid())
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Targeting config has no policy in Definition %s"),
				*GetNameSafe(SkillDefinition));
			return false;
		}
		return true;
	}

	ActiveTargetingPolicy = NewObject<URPGSkillTargetingPolicy>(
		this,
		ActiveSkillSpec.TargetingPolicyClass);
	FText ValidationError;
	if (!ActiveTargetingPolicy ||
		!ActiveTargetingPolicy->ValidateTargetingConfig(
			ActiveSkillSpec.TargetingConfig,
			ValidationError))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Invalid targeting config for Definition %s: %s"),
			*GetNameSafe(SkillDefinition),
			*ValidationError.ToString());
		ActiveTargetingPolicy = nullptr;
		return false;
	}

	ActiveTargetingPolicy->Initialize(
		*this,
		ActiveSkillSpec.TargetingConfig);
	return true;
}

bool URPGGameplayAbility_SkillContainer::ShouldWaitForRemoteTargetData() const
{
	return HasAuthority(&CurrentActivationInfo) && !IsLocallyControlled();
}

bool URPGGameplayAbility_SkillContainer::WaitForInitialReplicatedTargetData()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || ReplicatedTargetDataDelegateHandle.IsValid())
	{
		return false;
	}

	const FPredictionKey ActivationPredictionKey =
		CurrentActivationInfo.GetActivationPredictionKey();
	ReplicatedTargetDataDelegateHandle =
		ASC->AbilityTargetDataSetDelegate(
			CurrentSpecHandle,
			ActivationPredictionKey)
		.AddUObject(
			this,
			&ThisClass::HandleReplicatedTargetData);
	bWaitingForInitialTargetData = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			InitialTargetDataTimeoutHandle,
			this,
			&ThisClass::HandleInitialTargetDataTimeout,
			FMath::Max(InitialTargetDataTimeout, 0.1f),
			false);
	}

	ASC->CallReplicatedTargetDataDelegatesIfSet(
		CurrentSpecHandle,
		ActivationPredictionKey);
	return true;
}

bool URPGGameplayAbility_SkillContainer::SendActiveTargetDataToServer(
	const FGameplayTag ApplicationTag)
{
	if (!IsPredictingClient())
	{
		return ActiveTargetResult.bIsValid;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const FGameplayAbilityTargetDataHandle TargetData =
		FRPGSkillTargetDataCodec::Encode(ActiveTargetResult);
	if (!ASC || !TargetData.IsValid(0))
	{
		return false;
	}

	FScopedPredictionWindow ScopedPrediction(ASC, true);
#if !UE_BUILD_SHIPPING
	if (auto* RPGASC = Cast<URPGAbilitySystemComponent>(ASC))
		URPGSkillNetworkE2EProbeComponent::ObserveTargetData(*RPGASC, CurrentSpecHandle,
			CurrentActivationInfo.GetActivationPredictionKey(), TargetData, ApplicationTag, ASC->ScopedPredictionKey, true, false);
#endif
	ASC->CallServerSetReplicatedTargetData(
		CurrentSpecHandle,
		CurrentActivationInfo.GetActivationPredictionKey(),
		TargetData,
		ApplicationTag,
		ASC->ScopedPredictionKey);
	return true;
}

void URPGGameplayAbility_SkillContainer::HandleReplicatedTargetData(
	const FGameplayAbilityTargetDataHandle& TargetData,
	const FGameplayTag ApplicationTag)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	FRPGSkillTargetResult SubmittedResult;
	FRPGSkillTargetResult ValidatedResult;
	FText ValidationError;
	const FGameplayTag ExecutionEventTag = ActiveExecutionPolicy
		? ActiveExecutionPolicy->GetExecutionEventTag()
		: FGameplayTag();
	const bool bKnownApplicationTag =
		!ApplicationTag.IsValid() ||
		(!bWaitingForInitialTargetData &&
		 ExecutionEventTag.IsValid() &&
		 ApplicationTag == ExecutionEventTag);
	const bool bDecoded =
		FRPGSkillTargetDataCodec::Decode(TargetData, SubmittedResult);
	if (!bKnownApplicationTag)
	{
		ValidationError = FText::FromString(
			TEXT("TargetData contains an unexpected execution event tag."));
	}
	const bool bValidated = bKnownApplicationTag && bDecoded &&
		(ActiveTargetingPolicy
			? ActiveTargetingPolicy->ValidateReplicatedTarget(
				SubmittedResult,
				ValidatedResult,
				ValidationError)
			: ValidateLegacyReplicatedTarget(
				SubmittedResult,
				ValidatedResult,
				ValidationError));

#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGSkillTargetDataAbuseE2E")))
	{
		UE_LOG(LogTemp, Display, TEXT("%sRPG_SKILL_ABUSE_CONSUMER Handle=%s Original=%d KnownTag=%d Decoded=%d Validated=%d Waiting=%d"),
			*FRPGSkillDamageNetworkProbe::LogContext(GetAvatarActorFromActorInfo()),
			*CurrentSpecHandle.ToString(), CurrentActivationInfo.GetActivationPredictionKey().Current,
			bKnownApplicationTag ? 1 : 0, bDecoded ? 1 : 0, bValidated ? 1 : 0, bWaitingForInitialTargetData ? 1 : 0);
	}
#endif

	ASC->ConsumeClientReplicatedTargetData(
		CurrentSpecHandle,
		CurrentActivationInfo.GetActivationPredictionKey());

	if (!bValidated)
	{
		ActiveTargetResult.Reset();
		if (AActor* SourceActor = GetSkillSourceActor())
		{
			if (URPGSecurityValidationComponent* Security =
				SourceActor->FindComponentByClass<
					URPGSecurityValidationComponent>())
			{
				Security->ReportInvalidTargetData(
					bDecoded
						? ValidationError.ToString()
						: TEXT("Malformed GAS TargetData"));
			}
		}
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Rejected replicated target data for %s: %s"),
			*GetNameSafe(SkillDefinition),
			bDecoded
				? *ValidationError.ToString()
				: TEXT("Malformed GAS TargetData"));
		if (bWaitingForInitialTargetData && IsActive())
		{
			EndAbility(
				CurrentSpecHandle,
				CurrentActorInfo,
				CurrentActivationInfo,
				true,
				true);
		}
		return;
	}

	ActiveTargetResult = MoveTemp(ValidatedResult);
	ApplyResolvedAimRotation();
	if (!bWaitingForInitialTargetData)
	{
		if (ApplicationTag.IsValid() && ShouldWaitForRemoteTargetData())
		{
			bRemotePolicyEventDataReady = true;
			RemotePolicyEventDataTime = GetWorld()
				? GetWorld()->GetTimeSeconds()
				: 0.0;
			TryDispatchRemotePolicyExecutionEvent();
		}
		return;
	}

	bWaitingForInitialTargetData = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(
			InitialTargetDataTimeoutHandle);
	}
	if (!StartSkillAfterTargetReady() && IsActive())
	{
		EndAbility(
			CurrentSpecHandle,
			CurrentActorInfo,
			CurrentActivationInfo,
			true,
			true);
	}
}

void URPGGameplayAbility_SkillContainer::HandleInitialTargetDataTimeout()
{
	if (bWaitingForInitialTargetData && IsActive())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Timed out waiting for initial target data for %s"),
			*GetNameSafe(SkillDefinition));
		EndAbility(
			CurrentSpecHandle,
			CurrentActorInfo,
			CurrentActivationInfo,
			true,
			true);
	}
}

void URPGGameplayAbility_SkillContainer::RemoveReplicatedTargetDataDelegate()
{
	if (!ReplicatedTargetDataDelegateHandle.IsValid())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC =
		GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AbilityTargetDataSetDelegate(
			CurrentSpecHandle,
			CurrentActivationInfo.GetActivationPredictionKey())
		.Remove(ReplicatedTargetDataDelegateHandle);
	}
	ReplicatedTargetDataDelegateHandle.Reset();
}

bool URPGGameplayAbility_SkillContainer::ValidateLegacyReplicatedTarget(
	const FRPGSkillTargetResult& SubmittedResult,
	FRPGSkillTargetResult& OutValidatedResult,
	FText& OutError) const
{
	OutValidatedResult.Reset();
	AActor* SourceActor = GetSkillSourceActor();
	if (!SubmittedResult.bIsValid || !IsValid(SourceActor) ||
		SubmittedResult.SourceLocation.ContainsNaN() ||
		SubmittedResult.TargetLocation.ContainsNaN())
	{
		OutError = FText::FromString(
			TEXT("Legacy target contains invalid spatial data."));
		return false;
	}

	const FVector SourceLocation = SourceActor->GetActorLocation();
	constexpr float SourceTolerance = 300.0f;
	constexpr float LegacyRangeWithTolerance = 1500.0f;
	if (FVector::DistSquared(
			SourceLocation,
			SubmittedResult.SourceLocation) >
			FMath::Square(SourceTolerance) ||
		FVector::DistSquared(
			SourceLocation,
			SubmittedResult.TargetLocation) >
			FMath::Square(LegacyRangeWithTolerance))
	{
		OutError = FText::FromString(
			TEXT("Legacy target exceeds server movement or range tolerance."));
		return false;
	}

	const FVector AimDirection =
		(SubmittedResult.TargetLocation - SourceLocation).GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		OutError = FText::FromString(
			TEXT("Legacy target has no usable aim direction."));
		return false;
	}

	OutValidatedResult.bIsValid = true;
	OutValidatedResult.TargetActor =
		IsValid(SubmittedResult.TargetActor)
			? SubmittedResult.TargetActor
			: nullptr;
	OutValidatedResult.SourceLocation = SourceLocation;
	OutValidatedResult.TargetLocation = SubmittedResult.TargetLocation;
	OutValidatedResult.AimDirection = AimDirection;
	OutValidatedResult.HitQueryTransform =
		FTransform(AimDirection.Rotation(), SourceLocation);
	OutError = FText::GetEmpty();
	return true;
}

bool URPGGameplayAbility_SkillContainer::ResolveLegacyTarget()
{
	ActiveTargetResult.Reset();
	AActor* SourceActor = GetSkillSourceActor();
	if (!IsValid(SourceActor))
	{
		return false;
	}

	FVector AimDirection =
		SourceActor->GetActorForwardVector().GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		return false;
	}

	ActiveTargetResult.bIsValid = true;
	ActiveTargetResult.SourceLocation = SourceActor->GetActorLocation();
	ActiveTargetResult.TargetLocation =
		ActiveTargetResult.SourceLocation + AimDirection * 1000.0f;
	ActiveTargetResult.AimDirection = AimDirection;
	ActiveTargetResult.HitQueryTransform =
		FTransform(AimDirection.Rotation(), ActiveTargetResult.SourceLocation);
	ActiveTargetResult.bOrientSourceToAim = false;
	return true;
}

void URPGGameplayAbility_SkillContainer::ApplyResolvedAimRotation()
{
	if (!ActiveTargetResult.bIsValid ||
		!ActiveTargetResult.bOrientSourceToAim)
	{
		return;
	}

	AActor* SourceActor = GetSkillSourceActor();
	const FVector PlanarDirection =
		ActiveTargetResult.AimDirection.GetSafeNormal2D();
	if (IsValid(SourceActor) && !PlanarDirection.IsNearlyZero())
	{
		SourceActor->SetActorRotation(
			FRotator(0.0f, PlanarDirection.Rotation().Yaw, 0.0f));
	}
}

bool URPGGameplayAbility_SkillContainer::GetActiveSkillHitQueryProfile(FRPGHitQueryProfile& OutProfile) const
{
	OutProfile = {};
	return IsActive() && !bIsAbilityEnding && ActiveSkillSpec.SkillTag.IsValid()
		&& ActiveSkillSpec.TryGetHitQueryProfile(OutProfile);
}

bool URPGGameplayAbility_SkillContainer::ExecuteActiveSkillHitQuery(
	TArray<FRPGHitQueryResult>& OutResults) const
{
	OutResults.Reset();
	UWorld* World = GetWorld();
	AActor* SourceActor = GetSkillSourceActor();
	FString ProfileError;
	if (!IsActive() || bIsAbilityEnding || !World || !IsValid(SourceActor) || !ActiveTargetResult.bIsValid ||
		!ActiveSkillSpec.SecurityProfile.IsValid(&ProfileError) ||
		(ActiveSkillSpec.SecurityProfile.bRequireAuthorityHitQuery &&
		 !SourceActor->HasAuthority()))
	{
		return false;
	}

	const URPGHitQuerySubsystem* HitQuerySubsystem =
		World->GetSubsystem<URPGHitQuerySubsystem>();
	if (!HitQuerySubsystem)
	{
		return false;
	}

	FRPGHitQueryContext Context;
	Context.SourceActor = SourceActor;
	Context.QueryTransform = ActiveTargetResult.HitQueryTransform;
	if (ActiveTargetingPolicy && !ActiveTargetingPolicy->ResolveHitQueryTransform(ActiveTargetResult, Context.QueryTransform)) return false;
	if (!GetActiveSkillHitQueryProfile(Context.Profile)) return false;
	const int32 SecurityMaximum = FMath::Max(
		1,
		ActiveSkillSpec.SecurityProfile.MaximumTargetsPerQuery);
	if (Context.Profile.Filter.MaxResults <= 0 ||
		Context.Profile.Filter.MaxResults > SecurityMaximum)
	{
		Context.Profile.Filter.MaxResults = SecurityMaximum;
	}
	return HitQuerySubsystem->ExecuteHitQuery(Context, OutResults);
}

void URPGGameplayAbility_SkillContainer::CaptureSkillBossEncounters(APawn* Source)
{
	SkillBossAttackEncounters.Reset();
	SkillBossAttackSource.Reset();
	SkillBossAttackController.Reset();
	if (!IsValid(Source) || !Source->HasAuthority() || !IsValid(Source->GetController()) || !Source->GetWorld()) return;
	SkillBossAttackSource = Source;
	SkillBossAttackController = Source->GetController();
	for (TActorIterator<APawn> It(Source->GetWorld()); It; ++It)
	{
		const auto* Boss = It->FindComponentByClass<URPGBossAIComponent>();
		auto* Threat = It->FindComponentByClass<URPGBossThreatComponent>();
		if (Boss && Threat && Boss->GetBossState().EncounterState == ERPGBossEncounterState::InCombat)
		{
			SkillBossAttackEncounters.Add(Threat, Boss->GetBossState().EncounterId);
		}
	}
}

bool URPGGameplayAbility_SkillContainer::ApplySkillDamageToServerHit(AActor* Source,
	const FHitResult& ServerHit, TSubclassOf<UGameplayEffect> EffectClass, float Damage,
	FGameplayTag DamageTag, const FRPGSkillSecurityProfile& Profile, FText& OutError)
{
	AActor* Target = ServerHit.GetActor();
	auto* Threat = IsValid(Target) ? Target->FindComponentByClass<URPGBossThreatComponent>() : nullptr;
	const auto* Boss = IsValid(Target) ? Target->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	if (!Threat && !Boss)
	{
		// Preserve the existing authorized route for ordinary NPCs.
		FActiveGameplayEffectHandle AppliedHandle;
		return URPGSecurityBlueprintLibrary::ApplyAuthorizedServerDamage(Source, ServerHit,
			EffectClass, Damage, DamageTag, Profile, AppliedHandle, OutError);
	}
	const APawn* SourcePawn = Cast<APawn>(Source);
	const int32* Encounter = Threat ? SkillBossAttackEncounters.Find(Threat) : nullptr;
	if (!Threat || !IsValid(SourcePawn) || !SourcePawn->HasAuthority() ||
		SourcePawn != SkillBossAttackSource.Get() || !SkillBossAttackController.IsValid() ||
		SourcePawn->GetController() != SkillBossAttackController.Get() ||
		!Encounter || *Encounter <= 0)
	{
		OutError = FText::FromString(TEXT("Boss damage requires the encounter and source captured at skill activation."));
		return false; // Never fall back to generic damage for a boss.
	}
	UAbilitySystemComponent* SourceASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Source);
	if (!SourceASC || !CurrentActorInfo || SourceASC != CurrentActorInfo->AbilitySystemComponent.Get())
	{
		OutError = FText::FromString(TEXT("Boss skill source Ability System is unavailable or changed."));
		return false;
	}
	if (!DamageTag.IsValid()) DamageTag = RPGGameplayTags::Shared_SetByCaller_BaseDamage;
	FGameplayEffectContextHandle Context = MakeEffectContext(CurrentSpecHandle, CurrentActorInfo);
	Context.AddHitResult(ServerHit);
	// Match the ordinary NPC route's level; keep authored damage/scalars unchanged.
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(EffectClass, 1.0f, Context);
	if (!Spec.IsValid())
	{
		OutError = FText::FromString(TEXT("Unable to build the boss skill damage effect."));
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(DamageTag, Damage);
	float HealthDamage = 0.f;
	bool bThreatAdded = false;
	if (!Threat->ApplyDamageFromServerHit(Cast<APawn>(Source), ServerHit, Spec, *Encounter,
		Profile, HealthDamage, bThreatAdded))
	{
		OutError = FText::FromString(TEXT("Boss skill damage was rejected by the encounter or server damage guard."));
		return false;
	}
	return true;
}

bool URPGGameplayAbility_SkillContainer::ExecuteAuthorizedSkillDamage(
	const TSubclassOf<UGameplayEffect> AuthorizedDamageEffectClass,
	const float BaseDamage,
	const FGameplayTag SetByCallerDamageTag,
	TArray<FRPGHitQueryResult>& OutAppliedHits,
	int32& OutAppliedTargetCount,
	FText& OutError)
{
	return ExecuteAuthorizedSkillDamageInternal(AuthorizedDamageEffectClass, BaseDamage, SetByCallerDamageTag,
		OutAppliedHits, OutAppliedTargetCount, OutError, nullptr);
}

bool URPGGameplayAbility_SkillContainer::ExecuteAuthorizedSkillDamageInternal(
	const TSubclassOf<UGameplayEffect> AuthorizedDamageEffectClass, const float BaseDamage,
	const FGameplayTag SetByCallerDamageTag, TArray<FRPGHitQueryResult>& OutAppliedHits,
	int32& OutAppliedTargetCount, FText& OutError, TArray<FRPGApprovedHitContext>* OutReactionContexts,
	const bool bAuthoredFinisher)
{
	if (OutReactionContexts) OutReactionContexts->Reset();
	OutAppliedHits.Reset();
	OutAppliedTargetCount = 0;
	OutError = FText::GetEmpty();
	if (!IsActive() || bIsAbilityEnding || bApplyingAuthorizedSkillDamage)
	{
		OutError = FText::FromString(TEXT("Skill damage requires an active, non-reentrant activation."));
		return false;
	}
	AActor* SourceActor = GetSkillSourceActor();
	FString ProfileError;
	if (!IsValid(SourceActor) || !SourceActor->HasAuthority())
	{
		OutError = FText::FromString(
			TEXT("Execute Authorized Skill Damage must run on the server."));
		return false;
	}
	if (!ActiveSkillSpec.SecurityProfile.IsValid(&ProfileError))
	{
		OutError = FText::FromString(ProfileError);
		return false;
	}
	if (!AuthorizedDamageEffectClass ||
		!FMath::IsFinite(BaseDamage) || BaseDamage <= 0.0f)
	{
		OutError = FText::FromString(
			TEXT("An authorized skill damage effect and positive base damage are required."));
		return false;
	}

	const int32 RemainingHitBudget = FMath::Max(
		0,
		ActiveSkillSpec.SecurityProfile.MaximumHitsPerActivation -
			ServerAppliedHitCount);
	if (RemainingHitBudget <= 0)
	{
		OutError = FText::FromString(
			TEXT("The skill has exhausted its server hit budget for this activation."));
		return false;
	}

	TArray<FRPGHitQueryResult> ServerHits;
	if (!ExecuteActiveSkillHitQuery(ServerHits))
	{
		OutError = FText::FromString(
			TEXT("The authoritative skill HitQuery found no eligible target."));
		return false;
	}
	const FGameplayTag DamageScalarTag =
		FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage"), false);
	const float DamageScalar = DamageScalarTag.IsValid()
		? ActiveSkillSpec.GetStatScalar(DamageScalarTag)
		: 1.0f;
	const float ResolveMultiplier = CommittedResolveDamageMultiplier;
	const float FinalDamage = BaseDamage * DamageScalar * ResolveMultiplier;
	if (!FMath::IsFinite(FinalDamage) || FinalDamage <= 0.0f ||
		FinalDamage > ActiveSkillSpec.SecurityProfile.MaximumDamagePerHit)
	{
		OutError = FText::FromString(
			TEXT("Tripod or mode scaling exceeds the skill damage security limit."));
		return false;
	}

	// GameplayEffect delegates may reenter damage, end the ability or activate
	// it again. Reserve before calling them and never continue into a new snapshot.
	TGuardValue<bool> ApplyingDamageGuard(bApplyingAuthorizedSkillDamage, true);
	const uint64 ActivationSerial = SkillDamageActivationSerial;
	const FRPGSkillSecurityProfile FrozenSecurityProfile = ActiveSkillSpec.SecurityProfile;
	FRPGCombatContributionAmounts Contributions;
	if (!ActiveSkillSpec.TryGetCombatContributions(Contributions))
	{
		OutError = FText::FromString(TEXT("Invalid frozen skill contribution amounts."));
		return false;
	}
	const bool bDeliverContributions = (ActiveSkillSpec.ContributionHitSection.IsNone() || bAuthoredFinisher)
		&& (Contributions.Stagger > 0.f || Contributions.PartBreak > 0.f);
	FText LastError;
	for (const FRPGHitQueryResult& ServerHit : ServerHits)
	{
		if (!IsActive() || bIsAbilityEnding || SkillDamageActivationSerial != ActivationSerial ||
			ServerAppliedHitCount >= FrozenSecurityProfile.MaximumHitsPerActivation)
		{
			break;
		}
		if (!IsValid(ServerHit.TargetActor) || ServerHit.TargetActor->IsActorBeingDestroyed()) continue;
		const UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(ServerHit.TargetActor);
		if (TargetASC && TargetASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Invincible))
		{
			continue;
		}
		++ServerAppliedHitCount;
		const FRPGApprovedHitContext ReactionContext(SourceActor, ServerHit.TargetActor);
		const TWeakObjectPtr<URPGCombatContributionComponent> ContributionReceiver =
			ServerHit.TargetActor->FindComponentByClass<URPGCombatContributionComponent>();
		const uint32 ReceiverGeneration = ContributionReceiver.IsValid() ? ContributionReceiver->GetContributionState().Generation : 0;
		if (ApplySkillDamageToServerHit(
			SourceActor,
			ServerHit.HitResult,
			AuthorizedDamageEffectClass,
			FinalDamage,
			SetByCallerDamageTag,
			FrozenSecurityProfile,
			LastError))
		{
			OutAppliedHits.Add(ServerHit);
			if (OutReactionContexts) OutReactionContexts->Add(ReactionContext);
			++OutAppliedTargetCount;
			if (bDeliverContributions && ContributionReceiver.IsValid() && IsActive() && !bIsAbilityEnding && SkillDamageActivationSerial == ActivationSerial)
				ContributionReceiver->AcceptApprovedSkillHit(*this, ActivationSerial, ReceiverGeneration, SourceActor, ReactionContext, Contributions);
			if (!bResolveHitAwarded && IsActive() && SkillDamageActivationSerial == ActivationSerial)
			{
				bResolveHitAwarded = true; // Reserve before gauge/UI callbacks; never award per target/tick.
				URPGGreatSwordResolveAbility::AwardConfirmedSkillHit(*this);
			}
		}
		else if (IsActive() && SkillDamageActivationSerial == ActivationSerial)
		{
			--ServerAppliedHitCount;
		}
	}

	if (OutAppliedTargetCount <= 0)
	{
		OutError = LastError.IsEmpty()
			? FText::FromString(TEXT("Every authoritative hit was rejected."))
			: LastError;
		return false;
	}
#if !UE_BUILD_SHIPPING
	URPGSkillNetworkE2EProbeComponent::ObserveFinalCapApplication(*this, OutAppliedHits, OutAppliedTargetCount, ServerAppliedHitCount);
#endif
	return true;
}

void URPGGameplayAbility_SkillContainer::HandleApplyDamage(const FGameplayEventData& Payload)
{
	if (!IsActive() || bIsAbilityEnding) return;
	if (!ActiveSkillSpec.ExecutionPolicyClass)
	{
		Super::HandleApplyDamage(Payload);
		return;
	}
	ApplyPolicySkillDamageEvent();
}

void URPGGameplayAbility_SkillContainer::HandleApplyAOEDamage(const FGameplayEventData& Payload)
{
	if (!IsActive() || bIsAbilityEnding) return;
	if (!ActiveSkillSpec.ExecutionPolicyClass)
	{
		Super::HandleApplyAOEDamage(Payload);
		return;
	}
	ApplyPolicySkillDamageEvent();
}

void URPGGameplayAbility_SkillContainer::ApplyAuthoredSkillFinisher(
	USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, const int32 MontageInstanceId)
{
	AActor* Source = GetSkillSourceActor();
	const auto* ActorInfo = GetCurrentActorInfo();
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	UAnimInstance* Anim = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	const FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(MontageInstanceId) : nullptr;
	if (!IsActive() || bIsAbilityEnding || bFinisherHitAttempted || !IsValid(Source) || !Source->HasAuthority()
		|| !ActorInfo || !IsValid(Mesh) || ActorInfo->SkeletalMeshComponent.Get() != Mesh || Mesh->GetOwner() != Source
		|| !Anim || Mesh->GetAnimInstance() != Anim || !ASC || ASC->GetAnimatingAbility() != this
		|| !ActiveMontageTask || !ActiveSkillSpec.Montage || Animation != ActiveSkillSpec.Montage
		|| Anim != SkillMontageAnimInstance.Get() || MontageInstanceId == INDEX_NONE || MontageInstanceId != SkillMontageInstanceId
		|| !Instance || Instance->Montage != ActiveSkillSpec.Montage || !Instance->IsPlaying()
		|| ActiveSkillSpec.ContributionHitSection.IsNone()
		|| Instance->Montage->GetSectionName(Instance->Montage->GetSectionIndexFromPosition(Instance->GetPosition()))
			!= ActiveSkillSpec.ContributionHitSection
		|| !ActiveExecutionPolicy || !ActiveExecutionPolicy->IsContributionFinisherReady()) return;
	// Reserve before hit queries, damage and delegate callbacks. A miss is not retried.
	bFinisherHitAttempted = true;
	ApplyPolicySkillDamageEvent(true);
}

void URPGGameplayAbility_SkillContainer::ApplyPolicySkillDamageEvent(const bool bAuthoredFinisher)
{
	AActor* Source = GetSkillSourceActor();
	if (!IsValid(Source) || !Source->HasAuthority()) return;
	TArray<FRPGHitQueryResult> AppliedHits;
	int32 AppliedCount = 0;
	FText Error;
	const uint64 ActivationSerial = SkillDamageActivationSerial;
	const ERPGAttackHitReactionPolicy ReactionPolicy = ActiveSkillSpec.HitReactionPolicy;
	const float StunDuration = ActiveSkillSpec.StunDuration;
	const FRPGSlowSettings Slow = ActiveSkillSpec.Slow;
	const FRPGBurnSettings Burn = ActiveSkillSpec.Burn;
	const FRPGDefenseReductionSettings DefenseReduction = ActiveSkillSpec.DefenseReduction;
	const FRPGDamageTakenIncreaseSettings DamageTakenIncrease = ActiveSkillSpec.DamageTakenIncrease;
	const FRPGStaggerSettings Stagger = ActiveSkillSpec.Stagger;
	const FRPGKnockbackSettings Knockback = ActiveSkillSpec.Knockback;
	TArray<FRPGApprovedHitContext> ReactionContexts;
	if (!ExecuteAuthorizedSkillDamageInternal(DamageEffectClass, ActiveSkillSpec.BaseDamage,
		FGameplayTag(), AppliedHits, AppliedCount, Error, &ReactionContexts, bAuthoredFinisher)) return;
	if (!IsActive() || bIsAbilityEnding || SkillDamageActivationSerial != ActivationSerial) return;

	// Presentation and identity gain are based only on approved server hits.
	FGameplayEffectContextHandle CueContext;
	K2_ExecuteGameplayCue(WeaponHitSoundCueTag, CueContext);
	for (int32 Index = 0; Index < AppliedHits.Num(); ++Index)
	{
		const FRPGHitQueryResult& Hit = AppliedHits[Index];
		if (!IsActive() || bIsAbilityEnding || SkillDamageActivationSerial != ActivationSerial) break;
		if (!IsValid(Hit.TargetActor)) continue;
		if (!TryRouteApprovedHitReaction(Hit.HitResult, ReactionPolicy, StunDuration, Knockback, Slow, ReactionContexts[Index], Burn, Stagger, DefenseReduction, DamageTakenIncrease)
			&& EventTag == RPGGameplayTags::Shared_Event_Hit && HitReactTag.IsValid())
		{
			FGameplayEventData Reaction;
			Reaction.Instigator = CurrentActorInfo ? CurrentActorInfo->OwnerActor.Get() : Source;
			Reaction.Target = Hit.TargetActor;
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Hit.TargetActor, HitReactTag, Reaction);
		}
		if (HitEffect && Source->GetNetMode() != NM_DedicatedServer)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), HitEffect, Hit.TargetActor->GetActorLocation());
		}
	}
	if (IsActive() && !bIsAbilityEnding && SkillDamageActivationSerial == ActivationSerial) GainIdentity();
}

void URPGGameplayAbility_SkillContainer::HandlePolicyMontageCompleted()
{
	ActiveMontageTask = nullptr;
	if (ActiveExecutionPolicy)
	{
		ActiveExecutionPolicy->OnMontageCompleted();
	}
}

void URPGGameplayAbility_SkillContainer::HandlePolicyMontageInterrupted()
{
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGSkillNetE2E")))
	{
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MONTAGE_INTERRUPTED Avatar=%s Ability=%s Active=%d Ending=%d"),
			*GetNameSafe(GetAvatarActorFromActorInfo()), *GetName(), IsActive(), bIsAbilityEnding);
	}
#endif
	ActiveMontageTask = nullptr;
	if (ActiveExecutionPolicy)
	{
		ActiveExecutionPolicy->OnMontageInterrupted();
	}
}

bool URPGGameplayAbility_SkillContainer::StartPolicyEventTask()
{
	if (!ActiveExecutionPolicy)
	{
		return true;
	}

	const FGameplayTag ExecutionEventTag =
		ActiveExecutionPolicy->GetExecutionEventTag();
	if (!ExecutionEventTag.IsValid())
	{
		return true;
	}

	ActivePolicyEventTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			ExecutionEventTag,
			nullptr,
			false,
			true);
	if (!ActivePolicyEventTask)
	{
		return false;
	}

	ActivePolicyEventTask->EventReceived.AddDynamic(
		this,
		&ThisClass::HandlePolicyExecutionEvent);
	ActivePolicyEventTask->ReadyForActivation();
	return true;
}

void URPGGameplayAbility_SkillContainer::StartExecutionInputTasks()
{
	ArmInputPressedTask();
	ArmInputReleasedTask();
}

void URPGGameplayAbility_SkillContainer::ArmInputPressedTask()
{
	if (!IsActive() || ActiveInputPressedTask)
	{
		return;
	}

	ActiveInputPressedTask =
		UAbilityTask_WaitInputPress::WaitInputPress(this, false);
	if (!ActiveInputPressedTask)
	{
		return;
	}

	ActiveInputPressedTask->OnPress.AddDynamic(
		this,
		&ThisClass::HandleReplicatedInputPressed);
	ActiveInputPressedTask->ReadyForActivation();
}

void URPGGameplayAbility_SkillContainer::ArmInputReleasedTask()
{
	if (!IsActive() || ActiveInputReleasedTask)
	{
		return;
	}

	ActiveInputReleasedTask =
		UAbilityTask_WaitInputRelease::WaitInputRelease(this, false);
	if (!ActiveInputReleasedTask)
	{
		return;
	}

	ActiveInputReleasedTask->OnRelease.AddDynamic(
		this,
		&ThisClass::HandleReplicatedInputReleased);
	ActiveInputReleasedTask->ReadyForActivation();
}

void URPGGameplayAbility_SkillContainer::HandlePolicyExecutionEvent(
	FGameplayEventData Payload)
{
	if (!ActiveExecutionPolicy)
	{
		return;
	}

	if (ShouldWaitForRemoteTargetData())
	{
		bRemotePolicyEventWindowOpen = true;
		RemotePolicyEventWindowTime = GetWorld()
			? GetWorld()->GetTimeSeconds()
			: 0.0;
		TryDispatchRemotePolicyExecutionEvent();
		return;
	}

	if (!RefreshSkillTargetWithApplicationTag(Payload.EventTag))
	{
		FinishSkillExecution(true);
		return;
	}
	ActiveExecutionPolicy->OnExecutionEvent(Payload);
}

void URPGGameplayAbility_SkillContainer::TryDispatchRemotePolicyExecutionEvent()
{
	if (!ShouldWaitForRemoteTargetData() || !ActiveExecutionPolicy ||
		!bRemotePolicyEventWindowOpen || !bRemotePolicyEventDataReady)
	{
		return;
	}

	const double Tolerance = FMath::Clamp(
		static_cast<double>(RemotePolicyEventSyncTolerance),
		0.05,
		1.0);
	const double EventSkew = FMath::Abs(
		RemotePolicyEventWindowTime - RemotePolicyEventDataTime);
	if (EventSkew > Tolerance)
	{
		if (RemotePolicyEventDataTime < RemotePolicyEventWindowTime)
		{
			bRemotePolicyEventDataReady = false;
		}
		else
		{
			bRemotePolicyEventWindowOpen = false;
		}
		return;
	}

	const FGameplayTag ExecutionEventTag =
		ActiveExecutionPolicy->GetExecutionEventTag();
	ResetRemotePolicyEventSync();
	if (!ExecutionEventTag.IsValid())
	{
		return;
	}

	FGameplayEventData Payload;
	Payload.EventTag = ExecutionEventTag;
	Payload.Instigator = GetSkillSourceActor();
	ActiveExecutionPolicy->OnExecutionEvent(Payload);
}

void URPGGameplayAbility_SkillContainer::ResetRemotePolicyEventSync()
{
	bRemotePolicyEventWindowOpen = false;
	bRemotePolicyEventDataReady = false;
	RemotePolicyEventWindowTime = 0.0;
	RemotePolicyEventDataTime = 0.0;
}

void URPGGameplayAbility_SkillContainer::HandleReplicatedInputPressed(
	const float TimeWaited)
{
	ActiveInputPressedTask = nullptr;
	if (!IsLocallyControlled())
	{
		DispatchExecutionInputPressed();
	}
	if (IsActive())
	{
		ArmInputPressedTask();
	}
}

void URPGGameplayAbility_SkillContainer::HandleReplicatedInputReleased(
	const float TimeHeld)
{
	ActiveInputReleasedTask = nullptr;
	if (!IsLocallyControlled())
	{
		DispatchExecutionInputReleased();
	}
	if (IsActive())
	{
		ArmInputReleasedTask();
	}
}
