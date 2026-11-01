#include "Experience/RPGAsyncAction_WaitForExperienceReady.h"

#include "DataAsset/Definition/RPGExperienceDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Manager/Component/RPGExperienceManagerComponent.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Stack.h"
#include "UObject/UObjectGlobals.h"

URPGAsyncAction_WaitForExperienceReady* URPGAsyncAction_WaitForExperienceReady::WaitForRPGExperienceReady(
	UObject* WorldContextObject, float InTimeoutSeconds)
{
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || !World->IsGameWorld() || !World->GetGameInstance())
	{
		FFrame::KismetExecutionMessage(
			TEXT("Wait For RPG Experience Ready requires a game world with a GameInstance."),
			ELogVerbosity::Error);
		return nullptr;
	}
	URPGAsyncAction_WaitForExperienceReady* Action = NewObject<URPGAsyncAction_WaitForExperienceReady>();
	Action->WaitingWorld = World;
	Action->TimeoutSeconds = InTimeoutSeconds;
	Action->RegisterWithGameInstance(World);
	return Action;
}

void URPGAsyncAction_WaitForExperienceReady::Activate()
{
	if (bActivated || bCompleted) { return; }
	bActivated = true;
	Super::Activate();
	if (!FMath::IsFinite(TimeoutSeconds) || TimeoutSeconds <= 0.f)
	{
		Finish(EResult::Failed, nullptr, TEXT("TimeoutSeconds must be finite and greater than zero."));
		return;
	}
	UWorld* World = WaitingWorld.Get();
	if (!World || World->bIsTearingDown || !IsRegistered())
	{
		Finish(EResult::Cancelled, nullptr, TEXT("The waiting world is ending."));
		return;
	}

	// Install world/timeout cleanup before checking a manager that may already be ready.
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddWeakLambda(this,
		[this](UWorld* EndingWorld, bool, bool)
		{
			if (EndingWorld == WaitingWorld.Get())
			{
				Finish(EResult::Cancelled, nullptr, TEXT("The waiting world is ending."));
			}
		});
	GameStateSetHandle = World->GameStateSetEvent.AddUObject(
		this, &ThisClass::HandleGameStateSet);
	World->GetTimerManager().SetTimer(
		TimeoutHandle, this, &ThisClass::HandleTimeout, TimeoutSeconds, false);
	HandleGameStateSet(World->GetGameState());
}

void URPGAsyncAction_WaitForExperienceReady::HandleGameStateSet(AGameStateBase* GameState)
{
	if (bCompleted) { return; }
	UWorld* World = WaitingWorld.Get();
	if (!World || World->bIsTearingDown)
	{
		Finish(EResult::Cancelled, nullptr, TEXT("The waiting world is ending."));
		return;
	}
	if (!GameState)
	{
		if (BoundManager.IsValid())
		{
			Finish(EResult::Cancelled, nullptr, TEXT("The waiting GameState was removed."));
		}
		return;
	}
	if (GameState->GetWorld() != World) { return; }
	URPGExperienceManagerComponent* Manager =
		GameState->FindComponentByClass<URPGExperienceManagerComponent>();
	if (!IsValid(Manager))
	{
		Finish(EResult::Failed, nullptr, TEXT("GameState has no RPGExperienceManagerComponent."));
		return;
	}
	if (BoundManager.Get() != Manager)
	{
		UnbindManager();
		BoundManager = Manager;
		Manager->OnExperienceLoaded.AddUniqueDynamic(this, &ThisClass::HandleLoaded);
		Manager->OnExperienceLoadFailed.AddUniqueDynamic(this, &ThisClass::HandleFailed);
		ManagerEndedHandle = Manager->OnExperienceEnded.AddUObject(
			this, &ThisClass::HandleManagerEnded);
	}
	// Bind first, then check current state so a completed load is never missed.
	EvaluateManager();
}

void URPGAsyncAction_WaitForExperienceReady::EvaluateManager()
{
	if (bCompleted) { return; }
	UWorld* World = WaitingWorld.Get();
	if (!World || World->bIsTearingDown)
	{
		Finish(EResult::Cancelled, nullptr, TEXT("The waiting world is ending."));
		return;
	}
	URPGExperienceManagerComponent* Manager = BoundManager.Get();
	if (!Manager)
	{
		Finish(EResult::Cancelled, nullptr, TEXT("The waiting Experience manager was removed."));
		return;
	}
	switch (Manager->GetLoadState())
	{
	case ERPGExperienceLoadState::Loaded:
		if (const URPGExperienceDefinition* Experience = Manager->GetCurrentExperience())
		{
			Finish(EResult::Ready, Experience, FString());
		}
		else
		{
			Finish(EResult::Failed, nullptr, TEXT("The loaded Experience has no definition."));
		}
		break;
	case ERPGExperienceLoadState::Failed:
		Finish(EResult::Failed, nullptr, Manager->GetLastError());
		break;
	case ERPGExperienceLoadState::Deactivating:
		Finish(EResult::Cancelled, nullptr, TEXT("The waiting Experience manager is ending."));
		break;
	default:
		break;
	}
}

void URPGAsyncAction_WaitForExperienceReady::HandleLoaded(const URPGExperienceDefinition* /*Experience*/)
{
	// An earlier multicast listener may have ended the manager; read its live state again.
	EvaluateManager();
}

void URPGAsyncAction_WaitForExperienceReady::HandleFailed(const FString& /*Reason*/)
{
	EvaluateManager();
}

void URPGAsyncAction_WaitForExperienceReady::HandleManagerEnded()
{
	Finish(EResult::Cancelled, nullptr, TEXT("The waiting Experience manager is ending."));
}

void URPGAsyncAction_WaitForExperienceReady::HandleTimeout()
{
	if (bCompleted) { return; }
	UWorld* World = WaitingWorld.Get();
	if (!World || World->bIsTearingDown)
	{
		Finish(EResult::Cancelled, nullptr, TEXT("The waiting world is ending."));
		return;
	}
	if (BoundManager.IsStale())
	{
		Finish(EResult::Cancelled, nullptr, TEXT("The waiting Experience manager was removed."));
		return;
	}
	if (BoundManager.IsValid())
	{
		EvaluateManager();
		if (bCompleted) { return; }
	}
	Finish(EResult::Failed, nullptr, TEXT("Timed out waiting for this world's RPG Experience to become ready."));
}

void URPGAsyncAction_WaitForExperienceReady::UnbindManager()
{
	if (URPGExperienceManagerComponent* Manager = BoundManager.Get())
	{
		Manager->OnExperienceLoaded.RemoveDynamic(this, &ThisClass::HandleLoaded);
		Manager->OnExperienceLoadFailed.RemoveDynamic(this, &ThisClass::HandleFailed);
		Manager->OnExperienceEnded.Remove(ManagerEndedHandle);
	}
	BoundManager.Reset();
	ManagerEndedHandle.Reset();
}

void URPGAsyncAction_WaitForExperienceReady::Cleanup()
{
	UnbindManager();
	if (UWorld* World = WaitingWorld.Get())
	{
		World->GameStateSetEvent.Remove(GameStateSetHandle);
	}
	GameStateSetHandle.Reset();
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	WorldCleanupHandle.Reset();
	// The GameInstance timer manager survives travel; clear our handle before unregistering.
	if (FTimerManager* TimerManager = GetTimerManager())
	{
		TimerManager->ClearTimer(TimeoutHandle);
	}
	TimeoutHandle.Invalidate();
}

bool URPGAsyncAction_WaitForExperienceReady::ShouldBroadcastDelegates() const
{
	return !bCompleted && !IsGarbageCollecting() && !IsUnreachable()
		&& !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
		&& Super::ShouldBroadcastDelegates();
}

void URPGAsyncAction_WaitForExperienceReady::Finish(
	EResult Result, const URPGExperienceDefinition* Experience, const FString& Reason)
{
	if (bCompleted) { return; }
	if (!ShouldBroadcastDelegates())
	{
		SetReadyToDestroy();
		return;
	}
	// Retain payload and proxy through Blueprint callbacks, including explicit GC or Cancel.
	TStrongObjectPtr<URPGAsyncAction_WaitForExperienceReady> KeepAlive(this);
	TStrongObjectPtr<URPGExperienceDefinition> KeepExperience(
		const_cast<URPGExperienceDefinition*>(Experience));
	const FString ReasonCopy(Reason);
	bCompleted = true;
	Cleanup();
	switch (Result)
	{
	case EResult::Ready: Ready.Broadcast(KeepExperience.Get(), ReasonCopy); break;
	case EResult::Failed: Failed.Broadcast(nullptr, ReasonCopy); break;
	case EResult::Cancelled: Cancelled.Broadcast(nullptr, ReasonCopy); break;
	}
	Super::SetReadyToDestroy();
}

void URPGAsyncAction_WaitForExperienceReady::SetReadyToDestroy()
{
	bCompleted = true;
	Cleanup();
	Super::SetReadyToDestroy();
}
