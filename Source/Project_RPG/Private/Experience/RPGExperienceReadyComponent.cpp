#include "Experience/RPGExperienceReadyComponent.h"

#include "DataAsset/Definition/RPGExperienceDefinition.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

URPGExperienceReadyComponent::URPGExperienceReadyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URPGExperienceReadyComponent::BeginPlay()
{
	TStrongObjectPtr<URPGExperienceReadyComponent> KeepAlive(this);
	bEndingPlay = false;
	WaitState = ERPGExperienceWaitState::Idle;
	ReadyExperience = nullptr;
	LastReason.Reset();
	// Blueprint component BeginPlay is called before the engine marks HasBegunPlay.
	TGuardValue<bool> BeginningPlayGuard(bBeginningPlay, true);
	Super::BeginPlay();
	if (bAutoWaitOnBeginPlay && WaitState == ERPGExperienceWaitState::Idle)
	{
		StartWaiting();
	}
}

bool URPGExperienceReadyComponent::StartWaiting()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (bEndingPlay || bDispatchingResult || WaitState == ERPGExperienceWaitState::Waiting
		|| IsGarbageCollecting() || IsUnreachable() || IsBeingDestroyed()
		|| !IsRegistered() || (!HasBegunPlay() && !bBeginningPlay)
		|| !IsValid(Owner) || Owner->IsActorBeingDestroyed()
		|| !World || !World->IsGameWorld() || World->bIsTearingDown || !World->GetGameInstance())
	{
		return false;
	}

	TStrongObjectPtr<URPGExperienceReadyComponent> KeepAlive(this);
	ReadyExperience = nullptr;
	LastReason.Reset();
	WaitState = ERPGExperienceWaitState::Waiting;
	URPGAsyncAction_WaitForExperienceReady* Action =
		URPGAsyncAction_WaitForExperienceReady::WaitForRPGExperienceReady(this, WaitTimeoutSeconds);
	if (!Action)
	{
		Complete(ERPGExperienceWaitState::Failed, nullptr, TEXT("Could not create an Experience waiter."));
		return false;
	}

	// Store and bind before Activate: an already loaded/failed manager completes synchronously.
	PendingWait = Action;
	Action->Ready.AddUniqueDynamic(this, &ThisClass::HandleReady);
	Action->Failed.AddUniqueDynamic(this, &ThisClass::HandleFailed);
	Action->Cancelled.AddUniqueDynamic(this, &ThisClass::HandleCancelled);
	TStrongObjectPtr<URPGAsyncAction_WaitForExperienceReady> KeepWaiter(Action);
	Action->Activate();
	// Complete may have cleared PendingWait; never assign the completed proxy back here.
	return true;
}

void URPGExperienceReadyComponent::CancelWaiting()
{
	if (WaitState != ERPGExperienceWaitState::Waiting) { return; }
	StopObserving(TEXT("Experience waiting was explicitly cancelled."));
}

void URPGExperienceReadyComponent::HandleReady(
	const URPGExperienceDefinition* Experience, const FString& Reason)
{
	Complete(ERPGExperienceWaitState::Ready, Experience, Reason);
}

void URPGExperienceReadyComponent::HandleFailed(
	const URPGExperienceDefinition* /*Experience*/, const FString& Reason)
{
	Complete(ERPGExperienceWaitState::Failed, nullptr, Reason);
}

void URPGExperienceReadyComponent::HandleCancelled(
	const URPGExperienceDefinition* /*Experience*/, const FString& Reason)
{
	Complete(ERPGExperienceWaitState::Cancelled, nullptr, Reason);
}

void URPGExperienceReadyComponent::Complete(
	ERPGExperienceWaitState Result, const URPGExperienceDefinition* Experience, const FString& Reason)
{
	if (WaitState != ERPGExperienceWaitState::Waiting || bEndingPlay) { return; }
	AActor* Owner = GetOwner();
	if (IsGarbageCollecting() || IsUnreachable() || IsBeingDestroyed()
		|| !IsRegistered() || !IsValid(Owner) || Owner->IsActorBeingDestroyed())
	{
		StopObserving(TEXT("The Experience consumer is ending."));
		return;
	}

	TStrongObjectPtr<URPGExperienceReadyComponent> KeepAlive(this);
	TStrongObjectPtr<URPGExperienceDefinition> KeepExperience(
		const_cast<URPGExperienceDefinition*>(Experience));
	const FString ReasonCopy(Reason);
	WaitState = Result;
	ReadyExperience = Result == ERPGExperienceWaitState::Ready ? KeepExperience.Get() : nullptr;
	LastReason = ReasonCopy;
	ClearPendingWait(false);

	// A result listener may destroy the component or collect garbage. Payloads remain valid.
	TGuardValue<bool> DispatchGuard(bDispatchingResult, true);
	switch (Result)
	{
	case ERPGExperienceWaitState::Ready: OnReady.Broadcast(KeepExperience.Get(), ReasonCopy); break;
	case ERPGExperienceWaitState::Failed: OnFailed.Broadcast(nullptr, ReasonCopy); break;
	case ERPGExperienceWaitState::Cancelled: OnCancelled.Broadcast(nullptr, ReasonCopy); break;
	default: break;
	}
}

void URPGExperienceReadyComponent::ClearPendingWait(bool bCancel)
{
	URPGAsyncAction_WaitForExperienceReady* Action = PendingWait.Get();
	PendingWait = nullptr;
	// Owner cleanup can run during GC; never create a new strong root in that path.
	if (!IsValid(Action)) { return; }
	Action->Ready.RemoveDynamic(this, &ThisClass::HandleReady);
	Action->Failed.RemoveDynamic(this, &ThisClass::HandleFailed);
	Action->Cancelled.RemoveDynamic(this, &ThisClass::HandleCancelled);
	if (bCancel) { Action->Cancel(); }
}

void URPGExperienceReadyComponent::StopObserving(const FString& Reason)
{
	if (WaitState == ERPGExperienceWaitState::Waiting || WaitState == ERPGExperienceWaitState::Ready)
	{
		WaitState = ERPGExperienceWaitState::Cancelled;
		LastReason = Reason;
	}
	ReadyExperience = nullptr;
	ClearPendingWait(true);
}

void URPGExperienceReadyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true;
	StopObserving(TEXT("The Experience consumer ended play."));
	Super::EndPlay(Reason);
}

void URPGExperienceReadyComponent::OnUnregister()
{
	StopObserving(TEXT("The Experience consumer was unregistered."));
	Super::OnUnregister();
}

