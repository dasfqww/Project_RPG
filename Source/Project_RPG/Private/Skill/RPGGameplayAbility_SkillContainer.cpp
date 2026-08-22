#include "Skill/RPGGameplayAbility_SkillContainer.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "GameplayPrediction.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/HitQuery/RPGHitQuerySubsystem.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Controller/RPGPlayerController.h"
#include "FunctionLibrary/RPGSecurityBlueprintLibrary.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Skill/RPGSkillAction.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillTargetingPolicy.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGGameplayAbility_SkillContainer)

URPGGameplayAbility_SkillContainer::URPGGameplayAbility_SkillContainer()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	bUseLegacyManualManaCost = false;
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

void URPGGameplayAbility_SkillContainer::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	ServerAppliedHitCount = 0;
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive())
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
	if (URPGPlayerSkillComponent* SkillComponent = AvatarActor
		? AvatarActor->FindComponentByClass<URPGPlayerSkillComponent>()
		: nullptr)
	{
		SaveData = SkillComponent->GetSkillSaveData(SkillDefinition->SkillTag);
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
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		}
		return;
	}

	if (!RefreshSkillTarget() || !StartSkillAfterTargetReady())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
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
		StartExecutionInputTasks();
		if (!CommitAbility(
			CurrentSpecHandle,
			CurrentActorInfo,
			CurrentActivationInfo))
		{
			return false;
		}
		return ActiveExecutionPolicy->StartExecution();
	}

	if (!CommitAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo))
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

void URPGGameplayAbility_SkillContainer::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	RemoveReplicatedTargetDataDelegate();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(
			InitialTargetDataTimeoutHandle);
	}
	bWaitingForInitialTargetData = false;
	bPreparedTargetForInputReplication = false;
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
	if (IsActive() && !ShouldWaitForRemoteTargetData())
	{
		bPreparedTargetForInputReplication = RefreshSkillTarget();
		return bPreparedTargetForInputReplication;
	}
	return true;
}

bool URPGGameplayAbility_SkillContainer::PreReplicateAbilityInputReleased()
{
	if (IsActive() && !ShouldWaitForRemoteTargetData())
	{
		bPreparedTargetForInputReplication = RefreshSkillTarget();
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

	ActiveMontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("SkillExecutionMontage"),
			ActiveSkillSpec.Montage,
			FMath::Max(AttackSpeed, 0.01f),
			StartSection);
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
	ActiveMontageTask->ReadyForActivation();
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

bool URPGGameplayAbility_SkillContainer::ExecuteActiveSkillHitQuery(
	TArray<FRPGHitQueryResult>& OutResults) const
{
	OutResults.Reset();
	UWorld* World = GetWorld();
	AActor* SourceActor = GetSkillSourceActor();
	FString ProfileError;
	if (!World || !IsValid(SourceActor) || !ActiveTargetResult.bIsValid ||
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
	Context.Profile = ActiveSkillSpec.TargetingProfile;
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

bool URPGGameplayAbility_SkillContainer::ExecuteAuthorizedSkillDamage(
	const TSubclassOf<UGameplayEffect> AuthorizedDamageEffectClass,
	const float BaseDamage,
	const FGameplayTag SetByCallerDamageTag,
	TArray<FRPGHitQueryResult>& OutAppliedHits,
	int32& OutAppliedTargetCount,
	FText& OutError)
{
	OutAppliedHits.Reset();
	OutAppliedTargetCount = 0;
	OutError = FText::GetEmpty();
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
	if (ServerHits.Num() > RemainingHitBudget)
	{
		ServerHits.SetNum(RemainingHitBudget, EAllowShrinking::No);
	}

	const FGameplayTag DamageScalarTag =
		FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage"), false);
	const float DamageScalar = DamageScalarTag.IsValid()
		? ActiveSkillSpec.GetStatScalar(DamageScalarTag)
		: 1.0f;
	const float FinalDamage = BaseDamage * DamageScalar;
	if (!FMath::IsFinite(FinalDamage) || FinalDamage <= 0.0f ||
		FinalDamage > ActiveSkillSpec.SecurityProfile.MaximumDamagePerHit)
	{
		OutError = FText::FromString(
			TEXT("Tripod or mode scaling exceeds the skill damage security limit."));
		return false;
	}

	FText LastError;
	for (const FRPGHitQueryResult& ServerHit : ServerHits)
	{
		FActiveGameplayEffectHandle AppliedHandle;
		if (URPGSecurityBlueprintLibrary::ApplyAuthorizedServerDamage(
			SourceActor,
			ServerHit.HitResult,
			AuthorizedDamageEffectClass,
			FinalDamage,
			SetByCallerDamageTag,
			ActiveSkillSpec.SecurityProfile,
			AppliedHandle,
			LastError))
		{
			OutAppliedHits.Add(ServerHit);
			++OutAppliedTargetCount;
			++ServerAppliedHitCount;
		}
	}

	if (OutAppliedTargetCount <= 0)
	{
		OutError = LastError.IsEmpty()
			? FText::FromString(TEXT("Every authoritative hit was rejected."))
			: LastError;
		return false;
	}
	return true;
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
