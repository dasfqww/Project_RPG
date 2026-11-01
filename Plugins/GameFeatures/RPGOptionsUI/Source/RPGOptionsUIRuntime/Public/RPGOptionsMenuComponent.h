#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "InputCoreTypes.h"
#include "RPGOptionsMenuComponent.generated.h"

class ARPGHUD;
class URPGOptionMenu;
class UInputAction;
class UEnhancedInputComponent;
class APlayerController;
class UWorld;

/** Owns the existing HUD options widget for the lifetime of a world-scoped feature. */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPGOPTIONSUIRUNTIME_API URPGOptionsMenuComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGOptionsMenuComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	void AddFeatureReference(bool bCreatedByFeature, UInputAction* ToggleAction = nullptr);
	void RemoveFeatureReference(UInputAction* ToggleAction = nullptr);

	UFUNCTION(BlueprintPure, Category = "UI|Options")
	URPGOptionMenu* GetOptionMenuWidget() const { return OptionsWidget; }

	/** True only while this component provides at least one shortcut binding. */
	UFUNCTION(BlueprintPure, Category = "UI|Options")
	bool HasShortcutBindings() const;

	/** Current effective button keys for this component's shortcuts, belonging to its local player only. */
	UFUNCTION(BlueprintPure, Category = "UI|Options")
	TArray<FKey> GetShortcutKeys() const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	bool CanManageOptions(const UWorld* World) const;
	void RefreshOptions();
	void AcquireOptions();
	void ReleaseOptions(bool bRestoreDefault);
	bool HasActiveOptionsSession() const;
	void QueueOptionsRefresh();
	void CancelPendingRefresh();
	void UpdateInputBindings();
	void ReleaseInputBindings();
	void ToggleOptionsFromInput();

	UPROPERTY(Transient)
	TMap<TObjectPtr<UInputAction>, int32> ToggleActions;
	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> ShortcutInput;
	TWeakObjectPtr<APlayerController> ShortcutController;
	TArray<uint32> ShortcutBindingHandles;

	UPROPERTY(Transient)
	TObjectPtr<URPGOptionMenu> OptionsWidget;
	TWeakObjectPtr<ARPGHUD> OptionsHUD;
	TWeakObjectPtr<UWorld> OptionsWorld;
	int32 FeatureReferences = 0;
	bool bDestroyWhenUnused = false;
	bool bEndingOptions = false;
	bool bUnregisteringOptions = false;
	bool bAcquiringOptions = false;
	bool bReleasingOptions = false;
	bool bUpdatingShortcuts = false;
	uint64 OptionsGeneration = 0;
	FTimerHandle PendingRefreshHandle;
	TWeakObjectPtr<UWorld> PendingRefreshWorld;
};
