#include "Tests/RPGSkillNetworkE2EProbeComponent.h"
#include "Tests/RPGSkillTripodNetworkProbe.h"
#include "Tests/RPGSkillDamageNetworkProbe.h"
#include "Tests/RPGSkillFinalCapNetworkProbe.h"
#include "Tests/RPGNPCDeathNetworkProbe.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Animation/AnimInstance.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RPGAbilityTags.h"
#include "RPGInputTags.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillCatalogSubsystem.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "UI/MVVM/RPGSkillViewModel.h"
#include "UI/Skill/RPGTripodBoard.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillNetworkE2EProbeComponent)

DEFINE_LOG_CATEGORY_STATIC(LogRPGSkillNetworkE2E, Log, All);

#define RPG_SKILL_PROBE_LOG(Verbosity, Format, ...) UE_LOG(LogRPGSkillNetworkE2E, Verbosity, TEXT("%s%s"), \
	*FRPGSkillDamageNetworkProbe::LogContext(GetOwner()), *FString::Printf(Format, ##__VA_ARGS__))

namespace
{
	constexpr double ProbeTimeoutSeconds = 30.0;
	constexpr double ChargeHoldSeconds = 0.75;
	constexpr double ActivationRetrySeconds = 0.25;
	constexpr double InputEventTimeoutSeconds = 0.5;
	constexpr int32 ProgressionTestLevel = 4;

	bool IsProgressionProbeEnabled()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("RPGSkillProgressionE2E"));
	}
}

URPGSkillNetworkE2EProbeComponent::URPGSkillNetworkE2EProbeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

void URPGSkillNetworkE2EProbeComponent::ObserveTargetData(URPGAbilitySystemComponent& ASC,
	const FGameplayAbilitySpecHandle Handle, const FPredictionKey Original,
	const FGameplayAbilityTargetDataHandle& Data, const FGameplayTag Tag,
	const FPredictionKey Message, const bool bSending, const bool bAdmitted)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGSkillTargetDataAbuseE2E"))) return;
	const auto* Pawn = Cast<APawn>(ASC.GetAvatarActor());
	const auto* Controller = Pawn ? Pawn->GetController() : nullptr;
	auto* Probe = Controller ? Controller->FindComponentByClass<URPGSkillNetworkE2EProbeComponent>() : nullptr;
	if (!Probe || Probe->bFinished || !Probe->DamageProbe) return;
	if (bSending) Probe->DamageProbe->ObserveSentTargetData(ASC, Handle, Original, Data, Tag, Message);
	else Probe->DamageProbe->ObserveReceivedTargetData(ASC, Handle, Original, Data, Tag, Message, bAdmitted);
#endif
}

void URPGSkillNetworkE2EProbeComponent::ObserveFinalCapExecution(const UAbilitySystemComponent& Source,
	const UAbilitySystemComponent& Target, const double Calculated, const double Cap, const bool bBound, const bool bApproved)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGSkillFinalCapE2E"))) return;
	const auto* Pawn = Cast<APawn>(Source.GetAvatarActor());
	const auto* Controller = Pawn ? Pawn->GetController() : nullptr;
	auto* Probe = Controller ? Controller->FindComponentByClass<URPGSkillNetworkE2EProbeComponent>() : nullptr;
	if (Probe && !Probe->bFinished && Probe->FinalCapProbe)
		Probe->FinalCapProbe->ObserveExecution(Source, Target, Calculated, Cap, bBound, bApproved);
#endif
}

void URPGSkillNetworkE2EProbeComponent::ObserveFinalCapApplication(URPGGameplayAbility_SkillContainer& Skill,
	const TArray<FRPGHitQueryResult>& Hits, const int32 AppliedCount, const int32 BudgetUsed)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGSkillFinalCapE2E"))) return;
	const auto* Pawn = Cast<APawn>(Skill.GetAvatarActorFromActorInfo());
	const auto* Controller = Pawn ? Pawn->GetController() : nullptr;
	auto* Probe = Controller ? Controller->FindComponentByClass<URPGSkillNetworkE2EProbeComponent>() : nullptr;
	if (Probe && !Probe->bFinished && Probe->FinalCapProbe)
		Probe->FinalCapProbe->ObserveApplication(Skill, Hits, AppliedCount, BudgetUsed);
#endif
}

void URPGSkillNetworkE2EProbeComponent::BeginPlay()
{
	Super::BeginPlay();

#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGSkillNetE2E")) &&
		!FParse::Param(FCommandLine::Get(), TEXT("RPGNPCDeathE2E")))
	{
		return;
	}

	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller)
	{
		return;
	}

	DeadlineSeconds = FPlatformTime::Seconds() + ProbeTimeoutSeconds;
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGNPCDeathE2E")))
	{
		NPCDeathProbe = MakeShared<FRPGNPCDeathNetworkProbe>();
		DeadlineSeconds = FPlatformTime::Seconds() + 120.;
		SetComponentTickEnabled(true);
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGSkillDamageE2E")))
	{
		DamageProbe = MakeShared<FRPGSkillDamageNetworkProbe>();
		DeadlineSeconds += ProbeTimeoutSeconds;
	}
	if (DamageProbe && FParse::Param(FCommandLine::Get(), TEXT("RPGSkillFinalCapE2E")))
	{
		FinalCapProbe = MakeShared<FRPGSkillFinalCapNetworkProbe>();
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGSkillTripodE2E")))
	{
		TripodProbe = MakeShared<FRPGSkillTripodNetworkProbe>();
		DeadlineSeconds += ProbeTimeoutSeconds;
	}
	SetComponentTickEnabled(true);
	RPG_SKILL_PROBE_LOG(Display,
		TEXT("RPG_SKILL_NETTEST WAIT Role=%s Controller=%s"),
		Controller->GetNetMode() == NM_Client ? TEXT("Client") : TEXT("Server"),
		*GetNameSafe(Controller));
#endif
}

void URPGSkillNetworkE2EProbeComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	FinishProbe();
	DamageProbe.Reset();
	TripodProbe.Reset();
	FinalCapProbe.Reset();
	NPCDeathProbe.Reset();
	Super::EndPlay(EndPlayReason);
}

void URPGSkillNetworkE2EProbeComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if !UE_BUILD_SHIPPING
	if (bFinished)
	{
		return;
	}
	if (NPCDeathProbe)
	{
		auto* Controller = Cast<APlayerController>(GetOwner());
		if (!Controller) return;
		NPCDeathProbe->Tick(*Controller);
		if (!NPCDeathProbe->Failure().IsEmpty()) { FailProbe(*NPCDeathProbe->Failure()); return; }
		if (NPCDeathProbe->Passed()) { FinishProbe(); return; }
		if (FPlatformTime::Seconds() >= DeadlineSeconds) FailProbe(TEXT("NPCDeathTimeout"));
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	if (NowSeconds >= DeadlineSeconds)
	{
		FailProbe(TEXT("Timeout"));
		return;
	}

	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	URPGAbilitySystemComponent* AbilitySystem = Cast<URPGAbilitySystemComponent>(
		Pawn ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn) : nullptr);
	if (!Controller || !AbilitySystem)
	{
		return;
	}
	if (TripodProbe)
	{
		if (!Controller->HasAuthority() && !Controller->IsLocalController()) return;
		const auto Result = TripodProbe->Tick(*Controller);
		if (Result == FRPGSkillTripodNetworkProbe::EResult::Failed)
		{
			RPG_SKILL_PROBE_LOG(Error, TEXT("RPG_SKILL_TRIPOD_NETTEST FAILED Role=%s Reason=%s"),
				Controller->HasAuthority() ? TEXT("Server") : TEXT("Client"), *TripodProbe->GetFailure());
			FailProbe(*TripodProbe->GetFailure());
		}
		else if (Result == FRPGSkillTripodNetworkProbe::EResult::Passed)
		{
			FinishProbe();
		}
		return;
	}
	if (bObservedCancelledEnd)
	{
		FailProbe(TEXT("AbilityEndedCancelled"));
		return;
	}
	if (DamageProbe)
	{
		DamageProbe->Tick(*Controller, *AbilitySystem, SkillHandle);
		if (!DamageProbe->Failure().IsEmpty()) { FailProbe(*DamageProbe->Failure()); return; }
	}
	if (FinalCapProbe)
	{
		FinalCapProbe->Tick(*Controller, *AbilitySystem, SkillHandle);
		if (!FinalCapProbe->Failure().IsEmpty()) { FailProbe(*FinalCapProbe->Failure()); return; }
	}

	if (Controller->GetNetMode() == NM_Client)
	{
		// Client ownership may not be established yet when the component receives
		// BeginPlay. Defer the local-controller guard until input is actually sent.
		if (!Controller->IsLocalController())
		{
			return;
		}
		TickClient(*AbilitySystem, NowSeconds);
	}
	else
	{
		TickServer(*AbilitySystem);
	}
#endif
}

FGameplayAbilitySpec* URPGSkillNetworkE2EProbeComponent::FindChargeSkillSpec(
	URPGAbilitySystemComponent& AbilitySystem) const
{
	for (FGameplayAbilitySpec& Spec : AbilitySystem.GetActivatableAbilities())
	{
		const UGameplayAbility* Ability = Spec.Ability;
		if (Ability &&
			Cast<URPGGameplayAbility_SkillContainer>(Ability) &&
			Ability->GetClass()->GetName().Contains(TEXT("ChargeSlash")))
		{
			return &Spec;
		}
	}
	return nullptr;
}

bool URPGSkillNetworkE2EProbeComponent::PrepareClientInput()
{
	if (InputSubsystem.IsValid() && SkillInputAction.IsValid())
	{
		return true;
	}

	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->GetLocalPlayer())
	{
		return false;
	}
	const UDataAsset_InputConfig* Config = LoadObject<UDataAsset_InputConfig>(
		nullptr,
		TEXT("/Game/Blueprints/Character/Player/Input/"
			"DA_InputConfig.DA_InputConfig"));
	const UInputAction* Action = Config
		? Config->FindAbilityInputActionByTag(
			RPGGameplayTags::InputTag_MustBeHeld_Charge_IdentitySkill)
		: nullptr;
	if (!Config || !Config->DefaultMappingContext || !Action ||
		Action->ValueType != EInputActionValueType::Boolean)
	{
		FailProbe(TEXT("ChargeInputConfigMissing"));
		return false;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
			Controller->GetLocalPlayer());
	const UEnhancedInputComponent* InputComponent =
		Cast<UEnhancedInputComponent>(Controller->InputComponent);
	if (!Subsystem || !InputComponent ||
		!Subsystem->HasMappingContext(Config->DefaultMappingContext))
	{
		return false;
	}

	bool bStartedBound = false;
	bool bCompletedBound = false;
	for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding :
		InputComponent->GetActionEventBindings())
	{
		if (Binding && Binding->GetAction() == Action)
		{
			bStartedBound |= Binding->GetTriggerEvent() == ETriggerEvent::Started;
			bCompletedBound |= Binding->GetTriggerEvent() == ETriggerEvent::Completed;
		}
	}
	if (!bStartedBound || !bCompletedBound)
	{
		FailProbe(TEXT("ChargeInputActionNotBound"));
		return false;
	}

	InputSubsystem = Subsystem;
	SkillInputAction = Action;
	return true;
}

bool URPGSkillNetworkE2EProbeComponent::RequestClientProgression(const int32 TargetLevel)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	URPGPlayerSkillComponent* Component = Pawn
		? Pawn->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	UGameInstance* GameInstance = Controller ? Controller->GetGameInstance() : nullptr;
	URPGSkillCatalogSubsystem* Catalog = GameInstance
		? GameInstance->GetSubsystem<URPGSkillCatalogSubsystem>() : nullptr;
	if (!Component || !Catalog)
	{
		FailProbe(TEXT("ProgressionComponentOrCatalogMissing"));
		return false;
	}
	if (!Catalog->IsCatalogReady())
	{
		Catalog->RequestSkillDefinitions();
		return false;
	}
	URPGSkillDefinition* Definition = Catalog->FindSkillDefinition(
		RPGGameplayTags::Player_Ability_Skill_Charge);
	if (!Definition)
	{
		FailProbe(TEXT("ChargeProgressionDefinitionMissing"));
		return false;
	}
	TStrongObjectPtr<URPGSkillViewModel> ViewModel(NewObject<URPGSkillViewModel>(this));
	ViewModel->InitializeSkillData(Component, {Definition});
	if (TargetLevel == ProgressionTestLevel)
	{
		if (Component->GetSkillSaveData(Definition->SkillTag).SkillLevel != 1 ||
			Component->GetRemainingSP() != Component->GetTotalSP())
		{
			FailProbe(TEXT("ProgressionFixtureNotAtDefault"));
			return false;
		}
		for (int32 Level = 1; Level < TargetLevel; ++Level)
		{
			ViewModel->RequestSkillLevelUp(Definition->SkillTag);
		}
	}
	else
	{
		ViewModel->RequestSkillLevelMin(Definition->SkillTag);
	}
	if (ViewModel->SkillSlots.Num() != 1 ||
		ViewModel->SkillSlots[0]->SkillLevel != TargetLevel ||
		ViewModel->RemainingSP != Component->GetRemainingSP())
	{
		FailProbe(TEXT("ProgressionViewModelDidNotRefresh"));
		return false;
	}
	RPG_SKILL_PROBE_LOG(Display,
		TEXT("RPG_SKILL_NETTEST CLIENT_LEVEL_REQUESTED Level=%d RemainingSP=%d"),
		TargetLevel, ViewModel->RemainingSP);
	return true;
}

bool URPGSkillNetworkE2EProbeComponent::HasReplicatedProgression(const int32 ExpectedLevel) const
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	const URPGPlayerSkillComponent* Component = Pawn
		? Pawn->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	const FArrayProperty* Property = FindFProperty<FArrayProperty>(
		URPGPlayerSkillComponent::StaticClass(), TEXT("ReplicatedSkillData"));
	if (!Component || !Property)
	{
		return false;
	}
	// GetSkillSaveData alone includes optimistic edits. Inspect the replicated
	// backing array so the next step requires an actual server response.
	const auto& Entries = *Property->ContainerPtrToValuePtr<TArray<FRPGReplicatedSkillSaveEntry>>(Component);
	const FRPGReplicatedSkillSaveEntry* Entry = Entries.FindByPredicate(
		[](const FRPGReplicatedSkillSaveEntry& Candidate)
		{
			return Candidate.SkillTag == RPGGameplayTags::Player_Ability_Skill_Charge;
		});
	int32 ExpectedUsedSP = 0;
	for (int32 Level = 2; Level <= ExpectedLevel; ++Level)
	{
		ExpectedUsedSP += Component->GetRequiredSPForLevel(Level);
	}
	return Entry && Entry->SaveData.SkillLevel == ExpectedLevel &&
		Component->GetSkillSaveData(Entry->SkillTag).SkillLevel == ExpectedLevel &&
		Component->GetRemainingSP() == Component->GetTotalSP() - ExpectedUsedSP;
}

void URPGSkillNetworkE2EProbeComponent::ObserveAbilityEnd(
	URPGAbilitySystemComponent& AbilitySystem)
{
	if (ObservedAbilitySystem.Get() == &AbilitySystem)
	{
		return;
	}
	if (URPGAbilitySystemComponent* Previous = ObservedAbilitySystem.Get())
	{
		Previous->OnAbilityEnded.Remove(AbilityEndedHandle);
	}
	ObservedAbilitySystem = &AbilitySystem;
	AbilityEndedHandle = AbilitySystem.OnAbilityEnded.AddUObject(
		this, &ThisClass::HandleAbilityEnded);
}

void URPGSkillNetworkE2EProbeComponent::HandleAbilityEnded(
	const FAbilityEndedData& EndData)
{
	if (EndData.AbilitySpecHandle != SkillHandle)
	{
		return;
	}
	bObservedAbilityEnd = true;
	bObservedCancelledEnd = EndData.bWasCancelled;
}

void URPGSkillNetworkE2EProbeComponent::ObserveReleaseSection(
	URPGAbilitySystemComponent& AbilitySystem,
	const FGameplayAbilitySpec& Spec)
{
	if (bObservedReleaseSection || !Spec.IsActive())
	{
		return;
	}
	const URPGGameplayAbility_SkillContainer* Skill =
		Cast<URPGGameplayAbility_SkillContainer>(Spec.GetPrimaryInstance());
	const FRPGSkillRuntimeSpec* RuntimeSpec = Skill
		? &Skill->GetActiveSkillSpec() : nullptr;
	const FRPGSkillChargeExecutionConfig* ChargeConfig = RuntimeSpec
		? RuntimeSpec->ExecutionConfig.GetPtr<FRPGSkillChargeExecutionConfig>()
		: nullptr;
	const ACharacter* Avatar = Cast<ACharacter>(AbilitySystem.GetAvatarActor());
	UAnimInstance* AnimInstance = Avatar && Avatar->GetMesh()
		? Avatar->GetMesh()->GetAnimInstance() : nullptr;
	if (ChargeConfig && RuntimeSpec && RuntimeSpec->Montage && AnimInstance &&
		AnimInstance->Montage_GetCurrentSection(RuntimeSpec->Montage) ==
			ChargeConfig->ReleaseSection)
	{
		bObservedReleaseSection = true;
		RPG_SKILL_PROBE_LOG(Display,
			TEXT("RPG_SKILL_NETTEST RELEASE_SECTION Role=%s Controller=%s"),
			GetOwner() && GetOwner()->GetNetMode() == NM_Client
				? TEXT("Client") : TEXT("Server"),
			*GetNameSafe(GetOwner()));
#if !UE_BUILD_SHIPPING
		if (DamageProbe && GetOwner() && GetOwner()->GetNetMode() == NM_Client &&
			FParse::Param(FCommandLine::Get(), TEXT("RPGSkillClientEndRaceE2E")))
		{
			// Explicit local-presentation fixture only: the server's authored montage/GE
			// and natural EndAbility send the real GAS RPC. Never fabricate an end/RPC.
			const float Before = AnimInstance->Montage_GetPlayRate(RuntimeSpec->Montage);
			AnimInstance->Montage_SetPlayRate(RuntimeSpec->Montage, 0.5f);
			RPG_SKILL_PROBE_LOG(Display,
				TEXT("RPG_SKILL_END_RACE CLIENT_MONTAGE_DELAY Handle=%s Original=%d Before=%.6f After=%.6f Fixture=1"),
				*Spec.Handle.ToString(), Skill->GetCurrentActivationInfo().GetActivationPredictionKey().Current,
				Before, AnimInstance->Montage_GetPlayRate(RuntimeSpec->Montage));
		}
#endif
	}
}

void URPGSkillNetworkE2EProbeComponent::TickClient(
	URPGAbilitySystemComponent& AbilitySystem,
	const double NowSeconds)
{
	FGameplayAbilitySpec* Spec = SkillHandle.IsValid()
		? AbilitySystem.FindAbilitySpecFromHandle(SkillHandle)
		: FindChargeSkillSpec(AbilitySystem);
	if (!Spec)
	{
		if (!bLoggedMissingClientSpec)
		{
			bLoggedMissingClientSpec = true;
			const APawn* AvatarPawn = Cast<APawn>(AbilitySystem.GetAvatarActor());
			RPG_SKILL_PROBE_LOG(Display,
				TEXT("RPG_SKILL_NETTEST CLIENT_WAITING_FOR_SPEC Specs=%d "
					"Avatar=%s PawnLocal=%d"),
				AbilitySystem.GetActivatableAbilities().Num(),
				*GetNameSafe(AbilitySystem.GetAvatarActor()),
				AvatarPawn && AvatarPawn->IsLocallyControlled() ? 1 : 0);
		}
		return;
	}
	SkillHandle = Spec->Handle;
	ObserveAbilityEnd(AbilitySystem);
	if (!Spec->GetDynamicSpecSourceTags().HasTagExact(
		RPGGameplayTags::InputTag_MustBeHeld_Charge_IdentitySkill))
	{
		FailProbe(TEXT("ChargeInputTagMissing"));
		return;
	}
	if (!PrepareClientInput())
	{
		return;
	}

	switch (ClientPhase)
	{
	case EClientPhase::WaitingForAbility:
	{
		if (DamageProbe && !DamageProbe->ReadyForInput()) return;
		if (FinalCapProbe && !FinalCapProbe->ReadyForInput()) return;
		if (NowSeconds < NextActivationAttemptSeconds ||
			!AbilitySystem.HasMatchingGameplayTag(
				RPGGameplayTags::Player_Status_Rage_Active) ||
			AbilitySystem.GetUserAbilityActivationInhibited())
		{
			return;
		}
		if (IsProgressionProbeEnabled())
		{
			if (RequestClientProgression(ProgressionTestLevel))
			{
				ClientPhase = EClientPhase::WaitingForProgression;
			}
			break;
		}
		InputSubsystem->StartContinuousInputInjectionForAction(
			SkillInputAction.Get(), FInputActionValue(true), {}, {});
		NextActivationAttemptSeconds = NowSeconds + InputEventTimeoutSeconds;
		ClientPhase = EClientPhase::WaitingForActivation;
		RPG_SKILL_PROBE_LOG(Display,
			TEXT("RPG_SKILL_NETTEST CLIENT_INPUT_STARTED Action=%s"),
			*GetNameSafe(SkillInputAction.Get()));
		break;
	}

	case EClientPhase::WaitingForProgression:
		if (NowSeconds < NextActivationAttemptSeconds)
		{
			break;
		}
		if (HasReplicatedProgression(ProgressionTestLevel))
		{
			RPG_SKILL_PROBE_LOG(Display,
				TEXT("RPG_SKILL_NETTEST CLIENT_LEVEL_CONFIRMED Level=4"));
			InputSubsystem->StartContinuousInputInjectionForAction(
				SkillInputAction.Get(), FInputActionValue(true), {}, {});
			NextActivationAttemptSeconds = NowSeconds + InputEventTimeoutSeconds;
			ClientPhase = EClientPhase::WaitingForActivation;
		}
		break;

	case EClientPhase::WaitingForActivation:
		if (!Spec->IsActive())
		{
			if (NowSeconds >= NextActivationAttemptSeconds)
			{
				InputSubsystem->StopContinuousInputInjectionForAction(
					SkillInputAction.Get());
				NextActivationAttemptSeconds = NowSeconds + ActivationRetrySeconds;
				ClientPhase = IsProgressionProbeEnabled()
					? EClientPhase::WaitingForProgression : EClientPhase::WaitingForAbility;
				RPG_SKILL_PROBE_LOG(Warning,
					TEXT("RPG_SKILL_NETTEST CLIENT_ACTIVATION_RETRY InputPressed=%d"),
					Spec->InputPressed ? 1 : 0);
			}
			break;
		}
		if (!Spec->InputPressed)
		{
			FailProbe(TEXT("InputBindingDidNotPressSpec"));
			return;
		}
		if (IsProgressionProbeEnabled())
		{
			const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance());
			if (!Skill || Skill->GetActiveSkillSpec().SkillLevel != ProgressionTestLevel)
			{
				FailProbe(TEXT("ClientProgressionSnapshotMismatch"));
				return;
			}
			RPG_SKILL_PROBE_LOG(Display,
				TEXT("RPG_SKILL_NETTEST CLIENT_LEVEL_SNAPSHOT Level=4"));
		}
		ReleaseInputAtSeconds = NowSeconds + ChargeHoldSeconds;
		ClientPhase = EClientPhase::HoldingInput;
		RPG_SKILL_PROBE_LOG(Display,
			TEXT("RPG_SKILL_NETTEST CLIENT_ACTIVE Ability=%s"),
			*GetNameSafe(Spec->Ability));
		break;

	case EClientPhase::HoldingInput:
		ObserveReleaseSection(AbilitySystem, *Spec);
		if (!Spec->IsActive())
		{
			FailProbe(TEXT("ClientEndedBeforeRelease"));
			return;
		}
#if !UE_BUILD_SHIPPING
		if (DamageProbe && DamageProbe->NeedsInputPriming())
		{
			const auto* Controller = Cast<APlayerController>(GetOwner());
			const auto* Input = Controller ? Cast<UEnhancedInputComponent>(Controller->InputComponent) : nullptr;
			const FEnhancedInputActionEventBinding* Started = nullptr;
			if (Input) for (const auto& Binding : Input->GetActionEventBindings())
				if (Binding && Binding->GetAction() == SkillInputAction.Get() &&
					Binding->GetTriggerEvent() == ETriggerEvent::Started && Binding->IsBoundToObject(Controller))
				{
					if (Started) { FailProbe(TEXT("MultiAmbiguousStartedBinding")); return; }
					Started = Binding.Get();
				}
			if (!Started) { FailProbe(TEXT("MultiMissingStartedBinding")); return; }
			// One controlled re-press through the existing input binding, not a fabricated RPC/key.
			// A first press activates the ability; it need not also dispatch an active-spec Press event.
			RPG_SKILL_PROBE_LOG(Display, TEXT("RPG_SKILL_NETTEST CLIENT_INPUT_PRIMED Action=IA_IdentitySkill FixtureBinding=1"));
			Started->Execute(FInputActionInstance(SkillInputAction.Get()));
			if (DamageProbe->NeedsInputPriming()) { FailProbe(TEXT("MultiStartedBindingDidNotSendTargetData")); return; }
		}
#endif
		if (DamageProbe && DamageProbe->HoldForAbuseVerification(AbilitySystem)) break;
		if (NowSeconds >= ReleaseInputAtSeconds)
		{
			InputSubsystem->StopContinuousInputInjectionForAction(
				SkillInputAction.Get());
			NextActivationAttemptSeconds = NowSeconds + InputEventTimeoutSeconds;
			ClientPhase = EClientPhase::WaitingForReleaseEvent;
			RPG_SKILL_PROBE_LOG(Display,
				TEXT("RPG_SKILL_NETTEST CLIENT_INPUT_STOPPED"));
		}
		break;

	case EClientPhase::WaitingForReleaseEvent:
		ObserveReleaseSection(AbilitySystem, *Spec);
		if (!Spec->InputPressed)
		{
			ClientPhase = EClientPhase::WaitingForEnd;
			RPG_SKILL_PROBE_LOG(Display,
				TEXT("RPG_SKILL_NETTEST CLIENT_RELEASED"));
		}
		else if (NowSeconds >= NextActivationAttemptSeconds)
		{
			FailProbe(TEXT("InputBindingDidNotReleaseSpec"));
		}
		break;

	case EClientPhase::WaitingForEnd:
		ObserveReleaseSection(AbilitySystem, *Spec);
		if (!Spec->IsActive())
		{
			if (DamageProbe && !DamageProbe->Passed()) return;
			if (FinalCapProbe && !FinalCapProbe->Passed()) return;
			if (!bObservedAbilityEnd || !bObservedReleaseSection)
			{
				FailProbe(TEXT("ChargeDidNotFinishReleaseSection"));
				return;
			}
			if (IsProgressionProbeEnabled())
			{
				if (RequestClientProgression(1))
				{
					ClientPhase = EClientPhase::WaitingForReset;
				}
				break;
			}
			RPG_SKILL_PROBE_LOG(Display,
				TEXT("RPG_SKILL_NETTEST CLIENT_COMPLETE"));
			ClientPhase = EClientPhase::Finished;
			FinishProbe();
		}
		break;

	case EClientPhase::WaitingForReset:
		if (HasReplicatedProgression(1))
		{
			RPG_SKILL_PROBE_LOG(Display,
				TEXT("RPG_SKILL_NETTEST CLIENT_LEVEL_RESET Level=1 FullRefund=1"));
			RPG_SKILL_PROBE_LOG(Display, TEXT("RPG_SKILL_NETTEST CLIENT_COMPLETE"));
			ClientPhase = EClientPhase::Finished;
			FinishProbe();
		}
		break;

	case EClientPhase::Finished:
		break;
	}
}

void URPGSkillNetworkE2EProbeComponent::TickServer(
	URPGAbilitySystemComponent& AbilitySystem)
{
	if (!bServerProvisionedRage)
	{
		bServerProvisionedRage = true;
		// ChargeSlash is an IdentitySkill and requires the active Rage state.
		// Establish that prerequisite on the authority for this isolated probe.
		if (!AbilitySystem.HasMatchingGameplayTag(
			RPGGameplayTags::Player_Status_Rage_Active))
		{
			AbilitySystem.AddLooseGameplayTag(
				RPGGameplayTags::Player_Status_Rage_Active, 1,
				EGameplayTagReplicationState::CountToOwner);
		}
	}

	FGameplayAbilitySpec* Spec = SkillHandle.IsValid()
		? AbilitySystem.FindAbilitySpecFromHandle(SkillHandle)
		: FindChargeSkillSpec(AbilitySystem);
	if (!Spec)
	{
		if (!bServerProvisionedAbility)
		{
			UClass* ChargeAbilityClass = LoadClass<UGameplayAbility>(nullptr,
				TEXT("/Game/Blueprints/Character/Player/Abilities/Skill/"
					"GA_Player_ChargeSlash.GA_Player_ChargeSlash_C"));
			if (!ChargeAbilityClass)
			{
				FailProbe(TEXT("ChargeAbilityClassMissing"));
				return;
			}
			FGameplayAbilitySpec GrantedSpec(ChargeAbilityClass, 1);
			GrantedSpec.SourceObject = AbilitySystem.GetAvatarActor();
			GrantedSpec.GetDynamicSpecSourceTags().AddTag(
				RPGGameplayTags::InputTag_MustBeHeld_Charge_IdentitySkill);
			SkillHandle = AbilitySystem.GiveAbility(GrantedSpec);
			bServerProvisionedAbility = SkillHandle.IsValid();
			if (!bServerProvisionedAbility)
			{
				FailProbe(TEXT("ChargeAbilityGrantFailed"));
				return;
			}
			ObserveAbilityEnd(AbilitySystem);
			RPG_SKILL_PROBE_LOG(Display,
				TEXT("RPG_SKILL_NETTEST SERVER_GRANTED Controller=%s"),
				*GetNameSafe(GetOwner()));
		}
		return;
	}
	SkillHandle = Spec->Handle;
	ObserveAbilityEnd(AbilitySystem);

	if (Spec->IsActive())
	{
		ObserveReleaseSection(AbilitySystem, *Spec);
		if (!bServerObservedActive)
		{
			if (IsProgressionProbeEnabled())
			{
				const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance());
				const auto* Component = AbilitySystem.GetAvatarActor()
					? AbilitySystem.GetAvatarActor()->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
				if (!Skill || !Component ||
					Skill->GetActiveSkillSpec().SkillLevel != ProgressionTestLevel ||
					Component->GetSkillSaveData(RPGGameplayTags::Player_Ability_Skill_Charge).SkillLevel != ProgressionTestLevel ||
					Component->GetRemainingSP() != Component->GetTotalSP() - 3)
				{
					FailProbe(TEXT("ServerProgressionSnapshotMismatch"));
					return;
				}
				RPG_SKILL_PROBE_LOG(Display,
					TEXT("RPG_SKILL_NETTEST SERVER_LEVEL_SNAPSHOT Level=4 Controller=%s"), *GetNameSafe(GetOwner()));
			}
			bServerObservedActive = true;
			RPG_SKILL_PROBE_LOG(Display,
				TEXT("RPG_SKILL_NETTEST SERVER_ACTIVE Controller=%s Ability=%s"),
				*GetNameSafe(GetOwner()),
				*GetNameSafe(Spec->Ability));
		}
		return;
	}

	if (bServerObservedActive)
	{
		if (DamageProbe && !DamageProbe->Passed()) return;
		if (FinalCapProbe && !FinalCapProbe->Passed()) return;
		if (!bObservedAbilityEnd || !bObservedReleaseSection)
		{
			FailProbe(TEXT("ServerChargeDidNotFinishReleaseSection"));
			return;
		}
		if (IsProgressionProbeEnabled())
		{
			const auto* Component = AbilitySystem.GetAvatarActor()
				? AbilitySystem.GetAvatarActor()->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
			if (!Component ||
				Component->GetSkillSaveData(RPGGameplayTags::Player_Ability_Skill_Charge).SkillLevel != 1 ||
				Component->GetRemainingSP() != Component->GetTotalSP())
			{
				return;
			}
			RPG_SKILL_PROBE_LOG(Display,
				TEXT("RPG_SKILL_NETTEST SERVER_LEVEL_RESET Level=1 FullRefund=1 Controller=%s"), *GetNameSafe(GetOwner()));
		}
		RPG_SKILL_PROBE_LOG(Display,
			TEXT("RPG_SKILL_NETTEST SERVER_COMPLETE Controller=%s"),
			*GetNameSafe(GetOwner()));
		FinishProbe();
	}
}

void URPGSkillNetworkE2EProbeComponent::FailProbe(const TCHAR* Reason)
{
	RPG_SKILL_PROBE_LOG(Error,
		TEXT("RPG_SKILL_NETTEST FAILED Role=%s Controller=%s Reason=%s"),
		GetOwner() && GetOwner()->GetNetMode() == NM_Client
			? TEXT("Client") : TEXT("Server"),
		*GetNameSafe(GetOwner()),
		Reason);
	FinishProbe();
}

void URPGSkillNetworkE2EProbeComponent::FinishProbe()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	SetComponentTickEnabled(false);
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = InputSubsystem.Get())
	{
		if (const UInputAction* Action = SkillInputAction.Get())
		{
			Subsystem->StopContinuousInputInjectionForAction(Action);
		}
	}
	if (URPGAbilitySystemComponent* AbilitySystem = ObservedAbilitySystem.Get())
	{
		AbilitySystem->OnAbilityEnded.Remove(AbilityEndedHandle);
	}
	AbilityEndedHandle.Reset();
}
