// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGUICameraManagerComponent.generated.h"

class ARPGPlayerCameraManager;

class AActor;
class ACameraActor;
class AHUD;
class APlayerController;
class FDebugDisplayInfo;
class UCanvas;
class UObject;

UCLASS(Transient, Within = RPGPlayerCameraManager)
class PROJECT_RPG_API URPGUICameraManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	URPGUICameraManagerComponent();

	static URPGUICameraManagerComponent* GetComponent(APlayerController* PC);

	virtual void InitializeComponent() override;

	
	void SetViewTarget(AActor* InViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams());

	/** Own one local camera request per requester. Both references are weak and must belong to this world.
	 * Higher priority wins; equal priorities use the most recently changed request.
	 * This overrides only the calculated POV, leaving engine ViewTarget and controller input untouched.
	 */
	bool AddPresentationRequest(const UObject* RequestOwner, ACameraActor* InCamera, int32 Priority = 0);
	void RemovePresentationRequest(const UObject* RequestOwner);
	ACameraActor* GetPresentationViewTarget() const;

	bool NeedsToUpdateViewTarget() const;
	/** Compatibility entry point; commits no presentation POV when the request expires. */
	void UpdateViewTarget(struct FTViewTarget& OutVT, float DeltaTime);
	/** True only when the same live presentation request produced and committed a POV. */
	bool TryUpdateViewTarget(struct FTViewTarget& OutVT, float DeltaTime);

	void OnShowDebugInfo(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	struct FPresentationRequest
	{
		TWeakObjectPtr<ACameraActor> Camera;
		int32 Priority = 0;
		uint64 Order = 0;
	};

	APlayerController* GetLocalPresentationController() const;
	TMap<TWeakObjectPtr<const UObject>, FPresentationRequest> PresentationRequests;
	uint64 PresentationRevision = 0;
	uint64 NextPresentationOrder = 0;
	bool bCalculatingPresentationView = false;

	UPROPERTY(Transient)
	TObjectPtr<AActor> ViewTarget;

	UPROPERTY(Transient)
	bool bUpdatingViewTarget = false;

	FDelegateHandle ShowDebugInfoHandle;

public:	
	FORCEINLINE bool IsSettingViewTarget() const { return bUpdatingViewTarget; }
	FORCEINLINE AActor* GetViewTarget() const { return ViewTarget; }
		
};
