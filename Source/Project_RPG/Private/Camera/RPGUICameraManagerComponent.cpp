// Fill out your copyright notice in the Description page of Project Settings.

#include "Camera/RPGUICameraManagerComponent.h"

#include "Camera/RPGPlayerCameraManager.h"
#include "Camera/CameraActor.h"
#include "Engine/LocalPlayer.h"
#include "UObject/StrongObjectPtr.h"
#include "DisplayDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGUICameraManagerComponent)

URPGUICameraManagerComponent::URPGUICameraManagerComponent()
{
	bWantsInitializeComponent = true;
}

URPGUICameraManagerComponent* URPGUICameraManagerComponent::GetComponent(APlayerController* PC)
{
	if (!IsValid(PC) || PC->IsActorBeingDestroyed()) { return nullptr; }

	const ARPGPlayerCameraManager* CameraManager = Cast<ARPGPlayerCameraManager>(PC->PlayerCameraManager);
	if (!IsValid(CameraManager) || CameraManager->IsActorBeingDestroyed()) { return nullptr; }

	URPGUICameraManagerComponent* Component = CameraManager->GetUICameraComponent();
	return IsValid(Component) ? Component : nullptr;
}

void URPGUICameraManagerComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void URPGUICameraManagerComponent::OnRegister()
{
	Super::OnRegister();

	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->bIsTearingDown
		|| World->GetNetMode() == NM_DedicatedServer || ShowDebugInfoHandle.IsValid())
	{
		return;
	}

	ShowDebugInfoHandle = AHUD::OnShowDebugInfo.AddUObject(this, &ThisClass::OnShowDebugInfo);
}

void URPGUICameraManagerComponent::OnUnregister()
{
	if (ShowDebugInfoHandle.IsValid())
	{
		AHUD::OnShowDebugInfo.Remove(ShowDebugInfoHandle);
		ShowDebugInfoHandle.Reset();
	}
	ViewTarget = nullptr;
	PresentationRequests.Reset();
	++PresentationRevision;

	Super::OnUnregister();
}

void URPGUICameraManagerComponent::SetViewTarget(AActor* InViewTarget, FViewTargetTransitionParams TransitionParams)
{
	ARPGPlayerCameraManager* CameraManager = Cast<ARPGPlayerCameraManager>(GetOwner());
	const UWorld* World = GetWorld();
	if (bUpdatingViewTarget || !IsValid(CameraManager) || CameraManager->IsActorBeingDestroyed()
		|| !World || World->bIsTearingDown)
	{
		return;
	}
	if (InViewTarget && (!IsValid(InViewTarget) || InViewTarget->IsActorBeingDestroyed()
		|| InViewTarget->GetWorld() != World))
	{
		return;
	}

	// A null target keeps the engine's existing fallback to the owning controller.
	TGuardValue<bool> SettingViewTargetGuard(bUpdatingViewTarget, true);
	ViewTarget = InViewTarget;
	CameraManager->SetViewTarget(InViewTarget, TransitionParams);
}

APlayerController* URPGUICameraManagerComponent::GetLocalPresentationController() const
{
	if (!IsInGameThread() || !IsRegistered() || IsBeingDestroyed() || IsUnreachable()
		|| HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return nullptr; }
	const UWorld* World = GetWorld();
	const ARPGPlayerCameraManager* CameraManager = Cast<ARPGPlayerCameraManager>(GetOwner());
	if (!IsValid(CameraManager) || CameraManager->IsActorBeingDestroyed()
		|| !IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown
		|| World->GetNetMode() == NM_DedicatedServer || CameraManager->GetWorld() != World) { return nullptr; }
	APlayerController* Controller = CameraManager->GetOwningPlayerController();
	return IsValid(Controller) && !Controller->IsActorBeingDestroyed() && Controller->IsLocalController()
		&& Controller->GetWorld() == World && Controller->PlayerCameraManager == CameraManager
		&& Controller->GetLocalPlayer() && Controller->GetLocalPlayer()->GetPlayerController(nullptr) == Controller
		? Controller : nullptr;
}

bool URPGUICameraManagerComponent::AddPresentationRequest(
	const UObject* RequestOwner, ACameraActor* InCamera, int32 Priority)
{
	if (!GetLocalPresentationController() || !IsValid(RequestOwner) || RequestOwner->IsUnreachable()
		|| RequestOwner->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || RequestOwner->GetWorld() != GetWorld()
		|| !IsValid(InCamera) || InCamera->IsActorBeingDestroyed() || InCamera->GetWorld() != GetWorld()) { return false; }
	for (auto It = PresentationRequests.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().Camera.IsValid())
		{
			It.RemoveCurrent();
			++PresentationRevision;
		}
	}
	const TWeakObjectPtr<const UObject> OwnerKey(RequestOwner);
	if (const FPresentationRequest* Existing = PresentationRequests.Find(OwnerKey);
		Existing && Existing->Camera.Get() == InCamera && Existing->Priority == Priority) { return true; }
	FPresentationRequest& Request = PresentationRequests.FindOrAdd(OwnerKey);
	Request.Camera = InCamera;
	Request.Priority = Priority;
	Request.Order = ++NextPresentationOrder;
	++PresentationRevision;
	return true;
}

void URPGUICameraManagerComponent::RemovePresentationRequest(const UObject* RequestOwner)
{
	if (IsInGameThread() && RequestOwner && PresentationRequests.Remove(RequestOwner) > 0) { ++PresentationRevision; }
}

ACameraActor* URPGUICameraManagerComponent::GetPresentationViewTarget() const
{
	if (PresentationRequests.IsEmpty() || !GetLocalPresentationController()) { return nullptr; }
	const UWorld* World = GetWorld();
	const FPresentationRequest* Best = nullptr;
	for (const auto& Pair : PresentationRequests)
	{
		const UObject* RequestOwner = Pair.Key.Get();
		ACameraActor* Camera = Pair.Value.Camera.Get();
		if (!IsValid(RequestOwner) || RequestOwner->IsUnreachable()
			|| RequestOwner->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || RequestOwner->GetWorld() != World
			|| !IsValid(Camera) || Camera->IsActorBeingDestroyed() || Camera->GetWorld() != World) { continue; }
		if (!Best || Pair.Value.Priority > Best->Priority
			|| (Pair.Value.Priority == Best->Priority && Pair.Value.Order > Best->Order))
		{
			Best = &Pair.Value;
		}
	}
	return Best ? Best->Camera.Get() : nullptr;
}

bool URPGUICameraManagerComponent::NeedsToUpdateViewTarget() const
{
	return !bCalculatingPresentationView && GetPresentationViewTarget() != nullptr;
}

void URPGUICameraManagerComponent::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	TryUpdateViewTarget(OutVT, DeltaTime);
}

bool URPGUICameraManagerComponent::TryUpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	if (!GetLocalPresentationController() || bCalculatingPresentationView) { return false; }
	TStrongObjectPtr<URPGUICameraManagerComponent> KeepAlive(this);
	TStrongObjectPtr<APlayerController> Controller(GetLocalPresentationController());
	TStrongObjectPtr<ACameraActor> Camera(GetPresentationViewTarget());
	TStrongObjectPtr<UWorld> CameraWorld(GetWorld());
	if (!Controller.IsValid() || !Camera.IsValid() || !CameraWorld.IsValid()) { return false; }
	const uint64 Revision = PresentationRevision;
	TGuardValue<bool> CalculatingGuard(bCalculatingPresentationView, true);
	FMinimalViewInfo PresentationView(OutVT.POV);
	Camera->CalcCamera(DeltaTime, PresentationView);
	// Camera calculation can call game code that releases a feature, replaces the manager, or changes the winner.
	if (Revision != PresentationRevision || GetWorld() != CameraWorld.Get()
		|| GetLocalPresentationController() != Controller.Get() || GetPresentationViewTarget() != Camera.Get()) { return false; }
	OutVT.POV = PresentationView;
	return true;
}

void URPGUICameraManagerComponent::OnShowDebugInfo(AHUD* HUD, UCanvas* Canvas,
	const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos)
{
	if (!IsRegistered() || !IsValid(HUD) || !IsValid(Canvas)
		|| !DisplayInfo.IsDisplayOn(FName(TEXT("UICamera"))))
	{
		return;
	}

	const ARPGPlayerCameraManager* CameraManager = Cast<ARPGPlayerCameraManager>(GetOwner());
	const APlayerController* Controller = HUD->GetOwningPlayerController();
	const UWorld* World = GetWorld();
	if (!IsValid(CameraManager) || !IsValid(Controller)
		|| Controller->PlayerCameraManager != CameraManager
		|| !World || World->bIsTearingDown || HUD->GetWorld() != World)
	{
		return;
	}

	// AHUD's delegate is global; only draw this local player's camera.
	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;
	DisplayDebugManager.SetDrawColor(FColor::White);
	DisplayDebugManager.DrawString(FString::Printf(
		TEXT("RPGUICamera: %s RequestedTarget=%s PresentationTarget=%s Requests=%d Updating=%d"),
		*GetNameSafe(this), *GetNameSafe(ViewTarget), *GetNameSafe(GetPresentationViewTarget()),
		PresentationRequests.Num(), bUpdatingViewTarget ? 1 : 0));
}
