#include "Component/Progression/RPGTrainingObjectiveComponent.h"

#include "Ability/RPGComboSkillAbility.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameMode/RPGGameModeBase.h"
#include "Manager/HttpWebManager.h"
#include "Net/UnrealNetwork.h"
#include "Player/RPGPlayerState.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillExecutionTypes.h"

URPGTrainingObjectiveComponent::URPGTrainingObjectiveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.25f;
	SetIsReplicatedByDefault(true);
}

void URPGTrainingObjectiveComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		SetComponentTickEnabled(true);
		RefreshObservation(); // The periodic retry also handles late identity/ASC restoration.
	}
}

void URPGTrainingObjectiveComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEnding = true;
	ClearObservation();
	Super::EndPlay(Reason);
}

void URPGTrainingObjectiveComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	RefreshObservation();
}

void URPGTrainingObjectiveComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGTrainingObjectiveComponent, Progress, COND_OwnerOnly, REPNOTIFY_Always);
}

URPGAbilitySystemComponent* URPGTrainingObjectiveComponent::ResolveEligibleASC() const
{
	auto* Player = Cast<ARPGPlayer>(GetOwner());
	const auto* World = GetWorld();
	const auto* Mode = World ? World->GetAuthGameMode<ARPGGameModeBase>() : nullptr;
	const auto* State = Player ? Player->GetPlayerState<ARPGPlayerState>() : nullptr;
	const auto* Controller = Player ? Player->GetController() : nullptr;
	const auto* Growth = Player ? Player->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	const auto* Class = Growth ? Growth->GetPlayerClassDefinition() : nullptr;
	const auto* Web = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
	auto* ASC = Player ? Cast<URPGAbilitySystemComponent>(Player->GetAbilitySystemComponent()) : nullptr;
	if (bEnding || !IsValid(Player) || Player->IsActorBeingDestroyed() || !Player->HasAuthority()
		|| !IsValid(World) || World->bIsTearingDown || !IsValid(Mode) || !Mode->IsBackendDungeonDifficultyReady()
		|| Mode->GetConfirmedDungeonContentId() != TEXT("Progression.Training")
		|| Mode->GetCurrentGameDifficulty() != ERPGGameDifficulty::Normal
		|| !IsValid(State) || !State->HasAuthenticatedCharacter() || State->GetBackendConnectionGeneration() <= 0
		|| !IsValid(Controller) || Controller->GetPawn() != Player || Controller->PlayerState != State
		|| !Web || !Web->IsConfiguredForDungeonServer()
		|| State->GetBackendDungeonSessionId() != Web->GetConfiguredDungeonSessionId()
		|| !Growth || Growth->GetSkillLoadState() != ERPGPlayerSkillLoadState::Ready || Growth->IsConfigurationMutable()
		|| !Class || Class->ClassId != FName(TEXT("GreatSwordWarrior"))
		|| Player->FindComponentByClass<URPGTrainingObjectiveComponent>() != this
		|| !ASC || ASC->GetAvatarActor() != Player
		|| Mode->IsDungeonPartyMemberRevoked(State->GetBackendDungeonSessionId(),
			State->GetBackendCharacterId(), State->GetAuthenticatedSteamId())) return nullptr;
	return FRPGApprovedHitContext(Player, Player).IsCurrent(Player, Player) ? ASC : nullptr;
}

bool URPGTrainingObjectiveComponent::IsObservationCurrent() const
{
	auto* ASC = ResolveEligibleASC();
	const auto* Player = Cast<ARPGPlayer>(GetOwner());
	const auto* State = Player ? Player->GetPlayerState<ARPGPlayerState>() : nullptr;
	const auto* Growth = Player ? Player->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	return ASC && Progress.bActive && ASC == ObservedASC.Get() && State && Growth
		&& State == ObservedPlayerState.Get() && Player->GetController() == ObservedController.Get()
		&& State->GetBackendDungeonSessionId() == ObservedSession && State->GetBackendCharacterId() == ObservedCharacter
		&& State->GetAuthenticatedSteamId() == ObservedSteamId && State->GetBackendConnectionGeneration() == ObservedConnectionGeneration
		&& Growth->GetSkillRestoreGeneration() == ObservedRestoreGeneration
		&& PawnLifetime.IsCurrent(GetOwner(), GetOwner());
}

void URPGTrainingObjectiveComponent::ClearObservation()
{
	if (auto* ASC = ObservedASC.Get())
	{
		ASC->AbilityActivatedCallbacks.Remove(ActivatedHandle);
		ASC->AbilityCommittedCallbacks.Remove(CommittedHandle);
		ASC->OnAbilityEnded.Remove(EndedHandle);
	}
	ActivatedHandle.Reset(); CommittedHandle.Reset(); EndedHandle.Reset();
	ObservedASC.Reset(); ObservedPlayerState.Reset(); ObservedController.Reset();
	ObservedSession.Reset(); ObservedCharacter.Reset(); ObservedSteamId.Reset();
	ObservedConnectionGeneration = 0; ObservedRestoreGeneration = 0;
	Casts.Reset();
	Progress = {};
}

void URPGTrainingObjectiveComponent::RefreshObservation()
{
	if (bEnding || bRefreshing || !GetOwner() || !GetOwner()->HasAuthority()) return;
	TGuardValue<bool> Guard(bRefreshing, true);
	if (IsObservationCurrent())
	{
		for (auto It = Casts.CreateIterator(); It; ++It)
			if (!It.Key().IsValid() || !It.Key()->IsActive()) It.RemoveCurrent();
		return;
	}
	const bool bWasActive = Progress.bActive;
	ClearObservation();
	if (auto* ASC = ResolveEligibleASC())
	{
		auto* Player = CastChecked<ARPGPlayer>(GetOwner());
		auto* State = Player->GetPlayerState<ARPGPlayerState>();
		ObservedASC = ASC; ObservedPlayerState = State; ObservedController = Player->GetController();
		ObservedSession = State->GetBackendDungeonSessionId(); ObservedCharacter = State->GetBackendCharacterId();
		ObservedSteamId = State->GetAuthenticatedSteamId(); ObservedConnectionGeneration = State->GetBackendConnectionGeneration();
		ObservedRestoreGeneration = Player->FindComponentByClass<URPGPlayerSkillComponent>()->GetSkillRestoreGeneration();
		PawnLifetime = FRPGApprovedHitContext(Player, Player);
		Progress.bActive = true;
		ActivatedHandle = ASC->AbilityActivatedCallbacks.AddUObject(this, &ThisClass::ObserveActivated);
		CommittedHandle = ASC->AbilityCommittedCallbacks.AddUObject(this, &ThisClass::ObserveCommitted);
		EndedHandle = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::ObserveEnded);
	}
	if (bWasActive || Progress.bActive) Publish();
}

URPGTrainingObjectiveComponent::EGoal URPGTrainingObjectiveComponent::Classify(const UGameplayAbility& Ability) const
{
	if (const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(&Ability))
	{
		const auto* Definition = Skill->GetSkillDefinition();
		if (Definition && Definition->SkillTag == RPGGameplayTags::Player_Ability_Skill_AssultBlade) return EGoal::Assault;
		if (Definition && Definition->SkillTag == RPGGameplayTags::Player_Ability_Skill_Charge) return EGoal::Charge;
		return EGoal::None;
	}
	if (Ability.IsA<URPGComboSkillAbility>() && Ability.GetAssetTags().HasTagExact(RPGGameplayTags::Player_Ability_Attack_Light_GreatSword))
		return EGoal::BasicAttack;
	return Ability.IsA<URPGPlayerGameplayAbility>() && Ability.GetAssetTags().HasTagExact(RPGGameplayTags::Player_Ability_Roll)
		? EGoal::Evade : EGoal::None;
}

bool URPGTrainingObjectiveComponent::IsCurrentGrant(const UGameplayAbility& Ability, FGameplayAbilitySpecHandle Handle) const
{
	const auto* ASC = ObservedASC.Get();
	const auto* Info = Ability.GetCurrentActorInfo();
	const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
	if (!Info || Info->AvatarActor.Get() != GetOwner() || Info->AbilitySystemComponent.Get() != ASC
		|| !Spec || !Spec->Ability || Spec->Ability->GetClass() != Ability.GetClass()) return false;
	if (Ability.IsA<URPGGameplayAbility_SkillContainer>())
	{
		const auto* ClassOwner = GetOwner()->FindComponentByClass<URPGPlayerClassComponent>();
		return ClassOwner && ClassOwner->CanUseAbilitySpec(Handle, Ability.GetClass());
	}
	return true; // Weapon/roll grants remain owned by their existing equipment/startup paths.
}

void URPGTrainingObjectiveComponent::ObserveActivated(UGameplayAbility* Ability)
{
	if (bRefreshing || !IsObservationCurrent()) return;
	const auto* RPGAbility = Cast<URPGPlayerGameplayAbility>(Ability);
	if (!RPGAbility || !Ability->IsInstantiated() || !Ability->IsActive()
		|| !IsCurrentGrant(*Ability, Ability->GetCurrentAbilitySpecHandle())) return;
	const EGoal Goal = Classify(*Ability);
	if (Goal == EGoal::None) return;
	FObservedCast Record;
	Record.Goal = Goal; Record.Handle = Ability->GetCurrentAbilitySpecHandle();
	Casts.Add(Ability, Record);
}

void URPGTrainingObjectiveComponent::ObserveCommitted(UGameplayAbility* Ability)
{
	if (bRefreshing || !IsObservationCurrent() || !IsValid(Ability)) return;
	auto* Record = Casts.Find(Ability);
	if (!Record || Record->bCommitted || Record->Handle != Ability->GetCurrentAbilitySpecHandle()
		|| !IsCurrentGrant(*Ability, Record->Handle)) return;
	const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Ability);
	if (!Skill) return;
	const auto& Spec = Skill->GetActiveSkillSpec();
	if ((Record->Goal == EGoal::Assault && Spec.SkillTag != RPGGameplayTags::Player_Ability_Skill_AssultBlade)
		|| (Record->Goal == EGoal::Charge && Spec.SkillTag != RPGGameplayTags::Player_Ability_Skill_Charge)) return;
	if (Record->Goal == EGoal::Charge)
	{
		const auto* Config = Spec.ExecutionConfig.GetPtr<FRPGSkillChargeExecutionConfig>();
		const auto* Policy = Cast<URPGSkillExecutionPolicy_Charge>(Skill->GetActiveExecutionPolicy());
		if (!Spec.ExecutionPolicyClass || !Spec.ExecutionPolicyClass->IsChildOf(URPGSkillExecutionPolicy_Charge::StaticClass())
			|| !Config || Config->MaxChargeLevel < 1 || !Policy || !Policy->IsA(Spec.ExecutionPolicyClass)) return;
		Record->ChargePolicy = Policy;
	}
	Record->bCommitted = true;
	Record->SkillActivation = Skill->GetSkillActivationSerial();
}

void URPGTrainingObjectiveComponent::ObserveEnded(const FAbilityEndedData& Ended)
{
	if (bRefreshing || !IsObservationCurrent()) return;
	auto* Ability = Cast<URPGPlayerGameplayAbility>(Ended.AbilityThatEnded.Get());
	// An earlier end listener may already have activated this same instance again.
	if (!Ability || Ability->IsActive() || Ability->GetCombatExecutionId().IsValid()) return;
	FObservedCast Record;
	if (!Casts.RemoveAndCopyValue(Ability, Record)) return; // Reserve before publishing/reentrant callbacks.
	if (Ended.bWasCancelled || Record.Handle != Ended.AbilitySpecHandle || !IsCurrentGrant(*Ability, Record.Handle)) return;
	if (const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Ability))
		if (!Record.bCommitted || Skill->GetSkillActivationSerial() != Record.SkillActivation) return;
	if (Record.Goal == EGoal::Charge && (!Record.ChargePolicy.IsValid()
		|| !Record.ChargePolicy->WasReleasedAtMaximumCharge())) return;
	bool* Completed = nullptr;
	switch (Record.Goal)
	{
	case EGoal::BasicAttack: Completed = &Progress.bBasicAttackCompleted; break;
	case EGoal::Evade: Completed = &Progress.bEvadeCompleted; break;
	case EGoal::Assault: Completed = &Progress.bAssaultCompleted; break;
	case EGoal::Charge: Completed = &Progress.bChargeCompleted; break;
	default: return;
	}
	if (!*Completed)
	{
		*Completed = true;
		Publish();
	}
}

void URPGTrainingObjectiveComponent::Publish()
{
	GetOwner()->ForceNetUpdate();
	OnTrainingProgressChanged.Broadcast(Progress);
}

void URPGTrainingObjectiveComponent::OnRep_Progress()
{
	OnTrainingProgressChanged.Broadcast(Progress);
}
