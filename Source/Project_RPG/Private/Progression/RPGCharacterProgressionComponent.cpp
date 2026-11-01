#include "Progression/RPGCharacterProgressionComponent.h"

#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Manager/HttpWebManager.h"
#include "Net/UnrealNetwork.h"
#include "Player/RPGPlayerState.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Security/RPGDungeonGameplayAuthority.h"

URPGCharacterProgressionComponent::URPGCharacterProgressionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URPGCharacterProgressionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ThisClass, View, COND_OwnerOnly);
}

void URPGCharacterProgressionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	ClassOwner = GetOwner()->FindComponentByClass<URPGPlayerClassComponent>();
	if (ClassOwner.IsValid())
		ApplicationChangedHandle = ClassOwner->OnApplicationChanged.AddUObject(this, &ThisClass::TryInitialize);
	if (auto* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveRestartedDelegate.AddUniqueDynamic(this, &ThisClass::HandlePawnRestarted);
	NotifyOwnerContextChanged();
}

void URPGCharacterProgressionComponent::BindPlayerState()
{
	const auto* Pawn = Cast<APawn>(GetOwner());
	ARPGPlayerState* State = Pawn ? Pawn->GetPlayerState<ARPGPlayerState>() : nullptr;
	if (ObservedState.Get() == State) return;
	if (ObservedState.IsValid())
	{
		ObservedState->OnCharacterIdentityChanged.RemoveDynamic(this, &ThisClass::HandleIdentityChanged);
		ObservedState->OnDungeonSessionIdentityChanged.RemoveDynamic(this, &ThisClass::HandleIdentityChanged);
	}
	ObservedState = State;
	if (State)
	{
		State->OnCharacterIdentityChanged.AddUniqueDynamic(this, &ThisClass::HandleIdentityChanged);
		State->OnDungeonSessionIdentityChanged.AddUniqueDynamic(this, &ThisClass::HandleIdentityChanged);
	}
}

void URPGCharacterProgressionComponent::CaptureContext()
{
	const auto* Pawn = CastChecked<APawn>(GetOwner());
	ExpectedState = ObservedState;
	ExpectedController = Pawn->GetController();
	ExpectedWorld = GetWorld();
	ExpectedCharacterId = ExpectedState->GetBackendCharacterId();
	ExpectedSteamId = ExpectedState->GetAuthenticatedSteamId();
	ExpectedSessionId = ExpectedState->GetBackendDungeonSessionId();
	ExpectedConnectionGeneration = ExpectedState->GetBackendConnectionGeneration();
}

bool URPGCharacterProgressionComponent::HasCurrentContext() const
{
	const auto* Pawn = Cast<APawn>(GetOwner());
	return !bStopped && IsRegistered() && IsValid(Pawn) && Pawn->HasAuthority() && !Pawn->IsActorBeingDestroyed()
		&& !IsDungeonGameplayRevoked(Pawn) && ExpectedWorld.Get() == GetWorld() && GetWorld() && !GetWorld()->bIsTearingDown
		&& ExpectedController.IsValid() && Pawn->GetController() == ExpectedController.Get()
		&& ExpectedController->GetPawn() == Pawn && ExpectedState.IsValid() && Pawn->GetPlayerState() == ExpectedState.Get()
		&& ExpectedState->HasAuthenticatedCharacter() && !ExpectedState->IsAuthenticatedIdentityRevoked()
		&& ExpectedState->GetBackendCharacterId() == ExpectedCharacterId && ExpectedState->GetAuthenticatedSteamId() == ExpectedSteamId
		&& ExpectedState->GetBackendDungeonSessionId() == ExpectedSessionId
		&& ExpectedState->GetBackendConnectionGeneration() == ExpectedConnectionGeneration && ClassOwner.IsValid();
}

bool URPGCharacterProgressionComponent::CanRebindReadyContext() const
{
	const auto* Pawn = Cast<APawn>(GetOwner());
	const auto* State = ObservedState.Get();
	return View.State == ERPGProgressionRestoreState::Ready && IsValid(Pawn) && Pawn->HasAuthority()
		&& !IsDungeonGameplayRevoked(Pawn) && State && State->HasAuthenticatedCharacter() && !State->IsAuthenticatedIdentityRevoked()
		&& State->GetBackendCharacterId() == ExpectedCharacterId && State->GetAuthenticatedSteamId() == ExpectedSteamId
		&& State->GetBackendDungeonSessionId() == ExpectedSessionId && ClassOwner.IsValid()
		&& ClassOwner->GetApplyState() == ERPGPlayerClassApplyState::Applied;
}

void URPGCharacterProgressionComponent::NotifyOwnerContextChanged()
{
	if (bStopped || !HasBegunPlay() || !GetOwner() || !GetOwner()->HasAuthority()) return;
	BindPlayerState();
	// A verified reconnect of the same character/session keeps existing grants, resources and cooldowns.
	if (bFinished && CanRebindReadyContext()) { CaptureContext(); return; }
	if (bStarted && !HasCurrentContext())
	{
		Fail(FText::FromString(TEXT("Progression no longer belongs to the current Pawn/character context.")));
		return;
	}
	TryInitialize();
}

void URPGCharacterProgressionComponent::HandleIdentityChanged(const FString& Identity)
{
	NotifyOwnerContextChanged();
}

void URPGCharacterProgressionComponent::HandlePawnRestarted(APawn* Pawn)
{
	if (Pawn == GetOwner()) NotifyOwnerContextChanged();
}

void URPGCharacterProgressionComponent::TryInitialize()
{
	if (bStopped || bFinished || bApplying || bStarted || !ClassOwner.IsValid() || !ClassOwner->IsAbilitySystemReady()) return;
	BindPlayerState();
	const auto* State = ObservedState.Get();
	if (!State || !State->HasAuthenticatedCharacter()) return;
	const auto* Pawn = Cast<APawn>(GetOwner());
	// PossessedBy/startup runs before APlayerController::SetPawn. Wait for the
	// engine's restart notification rather than treating that normal gap as a failure.
	if (!Pawn || !Pawn->GetController() || Pawn->GetController()->GetPawn() != Pawn) return;
	auto* Growth = GetOwner()->FindComponentByClass<URPGPlayerSkillComponent>();
	auto* Slots = GetOwner()->FindComponentByClass<UQuickSlotComponent>();
	if (!Growth || !Slots || Growth->GetSkillLoadState() != ERPGPlayerSkillLoadState::Uninitialized || Slots->HasInitializedSkillSlots())
	{
		const uint32 ExistingGeneration = Growth ? Growth->GetSkillRestoreGeneration() : 0;
		const bool bUnmanagedReady = Growth && Growth->GetSkillLoadState() == ERPGPlayerSkillLoadState::Ready
			&& Growth->IsConfigurationMutable();
		Fail(FText::FromString(TEXT("Backend progression cannot replace a live or previous skill restore.")));
		// Authentication must never promote a development/local recipe to a saved character.
		// Do not touch another persistent supplier or a restore that changed during the failure callback.
		if (bUnmanagedReady && IsValid(Growth) && Growth->GetSkillRestoreGeneration() == ExistingGeneration)
		{
			Growth->InvalidateClassSkillRestore(ExistingGeneration);
			if (ClassOwner.IsValid()) ClassOwner->ReleaseClassAbilities();
		}
		return;
	}
	bStarted = true;
	CaptureContext();
	if (!HasCurrentContext()) { Fail(FText::FromString(TEXT("Authenticated progression context is not ready."))); return; }
	View.State = ERPGProgressionRestoreState::Loading;
	OnRep_View();
	if (bStopped || bFinished) return;
	if (!HasCurrentContext()) { Fail(FText::FromString(TEXT("Progression context changed before the backend request."))); return; }
	const auto* Instance = GetWorld()->GetGameInstance();
	auto* Manager = Instance ? Instance->GetSubsystem<UHttpWebManager>() : nullptr;
	const uint64 Serial = ++RequestSerial;
	TWeakObjectPtr<URPGCharacterProgressionComponent> WeakThis(this);
	if (!Manager || Manager->GetConfiguredDungeonSessionId() != ExpectedSessionId
		|| !Manager->LoadConfiguredCharacterProgression(ExpectedCharacterId,
			[WeakThis, Serial](FRPGCharacterProgressionResult Result)
			{
				if (WeakThis.IsValid()) WeakThis->HandleRecord(Serial, MoveTemp(Result));
			})) Fail(FText::FromString(TEXT("Assigned-server progression transport is unavailable.")));
}

void URPGCharacterProgressionComponent::HandleRecord(const uint64 Serial, FRPGCharacterProgressionResult Result)
{
	if (bStopped || bFinished || bRecordReceived || Serial != RequestSerial) return;
	if (!HasCurrentContext()) { Fail(FText::FromString(TEXT("Stale progression response was discarded."))); return; }
	if (!Result.bSuccessful) { Fail(FText::FromString(Result.Error)); return; }
	bRecordReceived = true;
	Snapshot = MoveTemp(Result.Snapshot);
	View.State = ERPGProgressionRestoreState::Applying;
	OnRep_View();
	if (bStopped || bFinished) return;
	if (!HasCurrentContext()) { Fail(FText::FromString(TEXT("Progression context changed before class loading."))); return; }
	auto* Growth = GetOwner()->FindComponentByClass<URPGPlayerSkillComponent>();
	SkillRestoreRequestId = Growth ? Growth->BeginClassSkillRestore(Snapshot.ClassId) : 0;
	if (!SkillRestoreRequestId) { Fail(FText::FromString(TEXT("Backend skill restore could not begin."))); return; }
	if (bStopped || bFinished) return;
	if (!HasCurrentContext()) { Fail(FText::FromString(TEXT("Progression context changed while restore began."))); return; }
	const FSoftObjectPath ClassPath = UAssetManager::Get().GetPrimaryAssetPath(Snapshot.ClassId);
	if (!ClassPath.IsValid()) { Fail(FText::FromString(TEXT("Saved class has no registered primary asset; content migration is required."))); return; }
	LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(ClassPath,
		FStreamableDelegate::CreateWeakLambda(this, [this, Serial]() { HandleClassLoaded(Serial); }));
	if (bFinished) LoadHandle.Reset();
	else if (!LoadHandle.IsValid()) Fail(FText::FromString(TEXT("Saved class asset could not be requested.")));
}

void URPGCharacterProgressionComponent::HandleClassLoaded(const uint64 Serial)
{
	if (bStopped || bFinished || Serial != RequestSerial) return;
	if (!HasCurrentContext()) { Fail(FText::FromString(TEXT("Stale class load was discarded."))); return; }
	LoadedDefinition = Cast<URPGPlayerClassDefinition>(UAssetManager::Get().GetPrimaryAssetObject(Snapshot.ClassId));
	LoadHandle.Reset();
	if (!LoadedDefinition) { Fail(FText::FromString(TEXT("Saved class asset is missing or has the wrong type."))); return; }
	ApplyRecord();
}

void URPGCharacterProgressionComponent::ApplyRecord()
{
	TGuardValue<bool> Guard(bApplying, true);
	if (!HasCurrentContext() || !ClassOwner->IsAbilitySystemReady())
	{
		Fail(FText::FromString(TEXT("Progression lost its startup/identity context before application.")));
		return;
	}
	auto* Growth = GetOwner()->FindComponentByClass<URPGPlayerSkillComponent>();
	auto* Slots = GetOwner()->FindComponentByClass<UQuickSlotComponent>();
	FRPGPlayerSkillRestoreData Record;
	FText Error;
	if (!Growth || !Slots || !Snapshot.BuildSkillRestoreData(*LoadedDefinition, *Growth, Record, Error)
		|| !Growth->CompleteClassSkillRestore(SkillRestoreRequestId, LoadedDefinition, Record, Error))
	{
		Fail(Error.IsEmpty() ? FText::FromString(TEXT("Saved progression record could not be applied.")) : Error);
		return;
	}
	if (bStopped || bFinished) return; // Grant callbacks can tear down the original Pawn.
	if (!HasCurrentContext()) { Fail(FText::FromString(TEXT("Progression context changed during class grants."))); return; }
	ClassOwner->RefreshAppliedClass();
	if (!HasCurrentContext() || ClassOwner->GetApplyState() != ERPGPlayerClassApplyState::Applied
		|| !Slots->InitializeSkillSlotsFromRecord(SkillRestoreRequestId, Snapshot.SkillSlots, Error))
	{
		Fail(Error.IsEmpty() ? FText::FromString(TEXT("Saved class grants or combat slots failed to initialize.")) : Error);
		return;
	}
	if (bStopped || bFinished) return;
	if (!HasCurrentContext()) { Fail(FText::FromString(TEXT("Progression context changed during slot initialization."))); return; }
	bFinished = true;
	View.State = ERPGProgressionRestoreState::Ready;
	View.ProfileId = Snapshot.ProfileId;
	View.BackendRevision = Snapshot.Revision;
	View.Experience = Snapshot.Experience;
	View.Level = Snapshot.Level;
	View.Milestones = Snapshot.Milestones;
	View.Error = FText::GetEmpty();
	OnRep_View();
}

void URPGCharacterProgressionComponent::Fail(const FText& Error)
{
	if (bStopped || View.State == ERPGProgressionRestoreState::Failed) return;
	bFinished = true;
	++RequestSerial;
	if (LoadHandle.IsValid()) LoadHandle->CancelHandle();
	LoadHandle.Reset();
	View = {};
	View.State = ERPGProgressionRestoreState::Failed;
	View.Error = Error;
	if (SkillRestoreRequestId && GetOwner() && GetOwner()->HasAuthority())
	{
		if (auto* Growth = GetOwner()->FindComponentByClass<URPGPlayerSkillComponent>();
			Growth && Growth->GetSkillRestoreGeneration() == SkillRestoreRequestId)
		{
			Growth->InvalidateClassSkillRestore(SkillRestoreRequestId);
			if (ClassOwner.IsValid()) ClassOwner->ReleaseClassAbilities();
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("Character progression restore failed on %s: %s"), *GetNameSafe(GetOwner()), *Error.ToString());
	OnRep_View();
}

void URPGCharacterProgressionComponent::OnRep_View()
{
	if (GetOwner() && GetOwner()->HasAuthority()) GetOwner()->ForceNetUpdate();
	OnProgressionChanged.Broadcast();
}

void URPGCharacterProgressionComponent::StopPending()
{
	bStopped = true;
	++RequestSerial;
	if (LoadHandle.IsValid()) LoadHandle->CancelHandle();
	LoadHandle.Reset();
	if (ClassOwner.IsValid()) ClassOwner->OnApplicationChanged.Remove(ApplicationChangedHandle);
	if (auto* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &ThisClass::HandlePawnRestarted);
	if (ObservedState.IsValid())
	{
		ObservedState->OnCharacterIdentityChanged.RemoveDynamic(this, &ThisClass::HandleIdentityChanged);
		ObservedState->OnDungeonSessionIdentityChanged.RemoveDynamic(this, &ThisClass::HandleIdentityChanged);
	}
	ObservedState.Reset();
	ClassOwner.Reset();
}

void URPGCharacterProgressionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopPending();
	Super::EndPlay(Reason);
}

void URPGCharacterProgressionComponent::OnUnregister()
{
	StopPending();
	Super::OnUnregister();
}
