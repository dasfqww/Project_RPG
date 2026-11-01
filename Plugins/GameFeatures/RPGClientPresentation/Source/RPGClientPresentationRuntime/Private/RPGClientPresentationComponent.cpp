#include "RPGClientPresentationComponent.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/RPGUICameraManagerComponent.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGClientPresentation, Log, All);

namespace
{
	struct FViewportVote
	{
		int32 Count = 0;
		bool bPrevious = false;
	};
	TMap<TWeakObjectPtr<UGameViewportClient>, FViewportVote> ViewportVotes;
}

URPGClientPresentationComponent::URPGClientPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URPGClientPresentationComponent::OnRegister()
{
	TStrongObjectPtr<URPGClientPresentationComponent> KeepAlive(this);
	Super::OnRegister();
	if (HasBegunPlay() && !bEndingPresentation)
	{
		StartViewportPresentation();
		StartCameraPresentation();
	}
}

void URPGClientPresentationComponent::OnUnregister()
{
	TStrongObjectPtr<URPGClientPresentationComponent> KeepAlive(
		!IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ? this : nullptr);
	TGuardValue<bool> UnregisteringGuard(bUnregisteringPresentation, true);
	StopCameraPresentation();
	StopViewportPresentation();
	Super::OnUnregister();
}

void URPGClientPresentationComponent::BeginPlay()
{
	TStrongObjectPtr<URPGClientPresentationComponent> KeepAlive(this);
	// Blueprint BeginPlay may unregister/destroy this component or end its world.
	bEndingPresentation = false;
	Super::BeginPlay();
	StartViewportPresentation();
	StartCameraPresentation();
}

void URPGClientPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	TStrongObjectPtr<URPGClientPresentationComponent> KeepAlive(
		!IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ? this : nullptr);
	bEndingPresentation = true;
	StopCameraPresentation();
	StopViewportPresentation();
	Super::EndPlay(Reason);
}

bool URPGClientPresentationComponent::CanApplyPresentation(const UWorld* World) const
{
	if (!IsInGameThread() || bEndingPresentation || bUnregisteringPresentation
		|| !IsRegistered() || !HasBegunPlay() || IsBeingDestroyed() || IsUnreachable()
		|| HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return false; }
	const AActor* Owner = GetOwner();
	return IsValid(Owner) && !Owner->IsActorBeingDestroyed() && IsValid(World) && !World->IsUnreachable()
		&& !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && World == GetWorld()
		&& World->IsGameWorld() && !World->bIsTearingDown && World->GetNetMode() != NM_DedicatedServer;
}

void URPGClientPresentationComponent::StartViewportPresentation()
{
	UWorld* World = GetWorld();
	if (!bDisableSplitscreen || !CanApplyPresentation(World))
	{
		StopViewportPresentation();
		return;
	}
	UGameInstance* Instance = World->GetGameInstance();
	UGameViewportClient* Viewport = IsValid(Instance) ? Instance->GetGameViewportClient() : nullptr;
	if (!IsValid(Viewport) || Viewport->IsUnreachable() || Viewport->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
	{
		StopViewportPresentation();
		return;
	}
	if (AppliedViewport.Get() == Viewport) { return; }
	TStrongObjectPtr<URPGClientPresentationComponent> KeepAlive(this);
	TStrongObjectPtr<UWorld> KeepWorld(World);
	TStrongObjectPtr<UGameViewportClient> KeepViewport(Viewport);
	StopViewportPresentation();
	// Restoring an old viewport calls virtual LayoutPlayers, which may change this component's lifetime/owner.
	if (!CanApplyPresentation(World) || !AppliedViewport.IsExplicitlyNull()) { return; }
	Instance = World->GetGameInstance();
	if (!IsValid(Instance) || Instance->GetGameViewportClient() != Viewport || !IsValid(Viewport)
		|| Viewport->IsUnreachable() || Viewport->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return; }
	for (auto It = ViewportVotes.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) { It.RemoveCurrent(); }
	}
	{
		FViewportVote& Vote = ViewportVotes.FindOrAdd(Viewport);
		if (Vote.Count++ == 0) { Vote.bPrevious = Viewport->IsSplitscreenForceDisabled(); }
		AppliedViewport = Viewport;
	}
	const uint64 RequestRevision = ++ViewportRequestRevision;
	// Publish ownership first and never retain a map reference across LayoutPlayers callbacks.
	if (!Viewport->IsSplitscreenForceDisabled()) { Viewport->SetForceDisableSplitscreen(true); }
	if (ViewportRequestRevision != RequestRevision || AppliedViewport.Get() != Viewport) { return; }
	if (!CanApplyPresentation(World)) { StopViewportPresentation(); return; }
	const FViewportVote* Current = ViewportVotes.Find(Viewport);
	UE_LOG(LogRPGClientPresentation, Display,
		TEXT("RPG_PRESENTATION APPLIED World=%s Votes=%d"), *World->GetName(), Current ? Current->Count : 0);
}

void URPGClientPresentationComponent::StopViewportPresentation()
{
	if (AppliedViewport.IsExplicitlyNull()) { return; }
	const TWeakObjectPtr<UGameViewportClient> ReleasedViewport = AppliedViewport;
	AppliedViewport.Reset();
	++ViewportRequestRevision;
	FViewportVote* Vote = ViewportVotes.Find(ReleasedViewport);
	if (!Vote) { return; }
	const bool bPrevious = Vote->bPrevious;
	const bool bLastVote = --Vote->Count <= 0;
	if (bLastVote) { ViewportVotes.Remove(ReleasedViewport); }
	const FString WorldName = GetNameSafe(GetWorld());
	UGameViewportClient* Viewport = ReleasedViewport.Get();
	// Remove the registration before a layout callback can acquire a new request for this viewport.
	if (bLastVote && !bPrevious && IsValid(Viewport) && !Viewport->IsUnreachable()
		&& !Viewport->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && Viewport->IsSplitscreenForceDisabled())
	{
		TStrongObjectPtr<UGameViewportClient> KeepViewport(Viewport);
		Viewport->SetForceDisableSplitscreen(false);
	}
	const FViewportVote* Current = ViewportVotes.Find(ReleasedViewport);
	const UGameViewportClient* CurrentViewport = ReleasedViewport.Get();
	UE_LOG(LogRPGClientPresentation, Display,
		TEXT("RPG_PRESENTATION RELEASED World=%s RemainingVotes=%d SplitscreenDisabled=%d"),
		*WorldName, Current ? Current->Count : 0,
		IsValid(CurrentViewport) && CurrentViewport->IsSplitscreenForceDisabled() ? 1 : 0);
}

void URPGClientPresentationComponent::StartCameraPresentation()
{
	UWorld* World = GetWorld();
	if (CameraTargetActorTag.IsNone() || !CanApplyPresentation(World)) { return; }
	if (CameraRefreshWorld.Get() == World && World->GetTimerManager().TimerExists(CameraRefreshTimer)) { return; }
	StopCameraPresentation();
	CameraRefreshWorld = World;
	World->GetTimerManager().SetTimer(CameraRefreshTimer,
		FTimerDelegate::CreateUObject(this, &ThisClass::RefreshCameraRequests), 0.25f, true);
	RefreshCameraRequests();
}

void URPGClientPresentationComponent::StopCameraPresentation()
{
	if (UWorld* World = CameraRefreshWorld.Get();
		IsValid(World) && !World->IsUnreachable() && !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
	{
		World->GetTimerManager().ClearTimer(CameraRefreshTimer);
	}
	CameraRefreshTimer.Invalidate();
	CameraRefreshWorld.Reset();
	bCameraTargetAmbiguous = false;
	auto Removed = MoveTemp(CameraRequests);
	CameraRequests.Reset();
	for (const auto& Pair : Removed)
	{
		if (URPGUICameraManagerComponent* Component = Pair.Value.Get()) { Component->RemovePresentationRequest(this); }
	}
}

void URPGClientPresentationComponent::RefreshCameraRequests()
{
	if (bRefreshingCameraRequests) { return; }
	UWorld* World = CameraRefreshWorld.Get();
	if (!CanApplyPresentation(World) || CameraTargetActorTag.IsNone())
	{
		StopCameraPresentation();
		return;
	}
	TStrongObjectPtr<URPGClientPresentationComponent> KeepAlive(this);
	TGuardValue<bool> RefreshingGuard(bRefreshingCameraRequests, true);
	ACameraActor* Target = nullptr;
	bool bAmbiguous = false;
	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		if (!IsValid(*It) || It->IsActorBeingDestroyed() || !It->ActorHasTag(CameraTargetActorTag)) { continue; }
		if (Target) { bAmbiguous = true; Target = nullptr; break; }
		Target = *It;
	}
	if (bAmbiguous && !bCameraTargetAmbiguous)
	{
		UE_LOG(LogRPGClientPresentation, Warning,
			TEXT("RPG_PRESENTATION CAMERA_SKIPPED World=%s Tag=%s Reason=MultipleCameraActors"),
			*World->GetName(), *CameraTargetActorTag.ToString());
	}
	bCameraTargetAmbiguous = bAmbiguous;
	TMap<TWeakObjectPtr<ULocalPlayer>, TWeakObjectPtr<URPGUICameraManagerComponent>> Desired;
	if (Target)
	{
		if (UGameInstance* Instance = World->GetGameInstance())
		{
			for (ULocalPlayer* Player : Instance->GetLocalPlayers())
			{
				if (!IsValid(Player)) { continue; }
				APlayerController* Controller = Player->GetPlayerController(nullptr);
				if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || !Controller->IsLocalController()
					|| Controller->GetWorld() != World || Controller->GetLocalPlayer() != Player) { continue; }
				URPGUICameraManagerComponent* Component = URPGUICameraManagerComponent::GetComponent(Controller);
				if (IsValid(Component) && Component->IsRegistered() && !Component->IsBeingDestroyed()
					&& Component->GetWorld() == World) { Desired.Add(Player, Component); }
			}
		}
	}
	for (auto It = CameraRequests.CreateIterator(); It; ++It)
	{
		const TWeakObjectPtr<URPGUICameraManagerComponent>* Wanted = Desired.Find(It.Key());
		if (!Wanted || *Wanted != It.Value())
		{
			if (URPGUICameraManagerComponent* Component = It.Value().Get()) { Component->RemovePresentationRequest(this); }
			It.RemoveCurrent();
		}
	}
	for (const auto& Pair : Desired)
	{
		URPGUICameraManagerComponent* Component = Pair.Value.Get();
		if (Component && Component->AddPresentationRequest(this, Target, CameraPriority))
		{
			CameraRequests.Add(Pair.Key, Component);
		}
		else
		{
			if (Component) { Component->RemovePresentationRequest(this); }
			CameraRequests.Remove(Pair.Key);
		}
	}
}
