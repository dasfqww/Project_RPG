#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "GameFeatureAction_RPGClientWidgets.h"
#include "RPGClientUIWidgetComponent.generated.h"

class UUserWidget;
class UWorld;

USTRUCT()
struct FRPGClientWidgetRequest
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Widget;
	UPROPERTY(Transient)
	TSubclassOf<UUserWidget> WidgetClass;

	int32 References = 0;
	int32 ZOrder = 0;
	bool bInputTransparent = true;
	bool bConstructing = false;
	uint64 Generation = 0;
};

/** Owns only widgets requested by RPGClientUI actions, leaving the HUD's own widgets alone. */
UCLASS(ClassGroup = (RPG))
class RPGCLIENTUIRUNTIME_API URPGClientUIWidgetComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGClientUIWidgetComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Records ownership without constructing widgets; the caller can record its cleanup first. */
	TArray<FName> AddWidgetRequests(const TArray<FRPGClientWidgetEntry>& Entries, bool bCreatedByFeature);
	void RemoveWidgetRequests(const TArray<FName>& WidgetIds);
	/** Retries while local-player readiness is pending; temporary unregister preserves the requests. */
	void RefreshWidgets();

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	bool CanManageWidgets(const UWorld* World) const;
	void QueueRefresh();
	void CancelPendingRefresh();
	void ReleaseWidgetInstances(bool bClearRequests);

	FTimerHandle PendingRefreshTimer;
	TWeakObjectPtr<UWorld> PendingRefreshWorld;
	uint64 WidgetLifecycleGeneration = 0;
	bool bUnregisteringWidgets = false;
	bool bRefreshingWidgets = false;

	UPROPERTY(Transient)
	TMap<FName, FRPGClientWidgetRequest> Requests;
	uint64 NextGeneration = 0;
	bool bDestroyWhenUnused = false;
	bool bEndingPlay = false;
};
