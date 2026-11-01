#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "RPGClientPresentationComponent.generated.h"

class UGameViewportClient;
class ULocalPlayer;
class URPGUICameraManagerComponent;

/** World-scoped client viewport settings and optional local camera requests. */
UCLASS(Blueprintable, ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPGCLIENTPRESENTATIONRUNTIME_API URPGClientPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGClientPresentationComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Presentation")
	bool bDisableSplitscreen = true;

	/** Opt-in: exactly one CameraActor in this world must have this Actor Tag. None preserves the existing camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Presentation|Camera")
	FName CameraTargetActorTag = NAME_None;

	/** Higher priority wins; equal priorities keep the last changed request. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Presentation|Camera")
	int32 CameraPriority = 0;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	bool CanApplyPresentation(const UWorld* World) const;
	void StartViewportPresentation();
	void StopViewportPresentation();
	void StartCameraPresentation();
	void StopCameraPresentation();
	void RefreshCameraRequests();

	TMap<TWeakObjectPtr<ULocalPlayer>, TWeakObjectPtr<URPGUICameraManagerComponent>> CameraRequests;
	TWeakObjectPtr<UWorld> CameraRefreshWorld;
	FTimerHandle CameraRefreshTimer;
	bool bRefreshingCameraRequests = false;
	bool bEndingPresentation = false;
	bool bUnregisteringPresentation = false;
	uint64 ViewportRequestRevision = 0;
	bool bCameraTargetAmbiguous = false;
	TWeakObjectPtr<UGameViewportClient> AppliedViewport;
};
