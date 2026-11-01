#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Experience/RPGAsyncAction_WaitForExperienceReady.h"
#include "RPGExperienceReadyComponent.generated.h"

UENUM(BlueprintType)
enum class ERPGExperienceWaitState : uint8
{
	Idle,
	Waiting,
	Ready,
	Failed,
	Cancelled
};

/** Optional actor-owned consumer of the current world's one-shot Experience readiness. */
UCLASS(Blueprintable, ClassGroup = (RPG),
	meta = (BlueprintSpawnableComponent, DisplayName = "RPG Experience Ready"))
class PROJECT_RPG_API URPGExperienceReadyComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGExperienceReadyComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnUnregister() override;

	/** Disable when the owner needs to bind result delegates before calling StartWaiting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Experience")
	bool bAutoWaitOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Experience",
		meta = (ClampMin = "0.001", Units = "s"))
	float WaitTimeoutSeconds = 65.f;

	/**
	 * Starts one wait after BeginPlay begins. Results can arrive before this call returns.
	 * Returns false while waiting, inside result callbacks, or outside a registered game component.
	 * A new wait does not change selection or retry the Experience manager's loading.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Experience",
		meta = (UnsafeDuringActorConstruction = "true"))
	bool StartWaiting();

	/** Silently cancels a pending wait. Completed results are left intact. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Experience")
	void CancelWaiting();

	/** Last wait result; this component does not follow a persistent actor across travel automatically. */
	UFUNCTION(BlueprintPure, Category = "RPG|Experience")
	ERPGExperienceWaitState GetWaitState() const { return WaitState; }

	/** Cached result of the last successful wait; cleared by a new wait, EndPlay or unregistering. */
	UFUNCTION(BlueprintPure, Category = "RPG|Experience")
	const URPGExperienceDefinition* GetReadyExperience() const { return ReadyExperience.Get(); }

	UFUNCTION(BlueprintPure, Category = "RPG|Experience")
	const FString& GetLastReason() const { return LastReason; }

	/** Feature actions are ready; HUD, Pawn and widget existence remain separate conditions. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Experience")
	FRPGExperienceWaitResult OnReady;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Experience")
	FRPGExperienceWaitResult OnFailed;

	/** World/manager cancellation while still waiting. Explicit cancellation and owner cleanup are silent. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Experience")
	FRPGExperienceWaitResult OnCancelled;

private:
	UFUNCTION()
	void HandleReady(const URPGExperienceDefinition* Experience, const FString& Reason);
	UFUNCTION()
	void HandleFailed(const URPGExperienceDefinition* Experience, const FString& Reason);
	UFUNCTION()
	void HandleCancelled(const URPGExperienceDefinition* Experience, const FString& Reason);

	void Complete(ERPGExperienceWaitState Result, const URPGExperienceDefinition* Experience,
		const FString& Reason);
	void ClearPendingWait(bool bCancel);
	void StopObserving(const FString& Reason);

	UPROPERTY(Transient)
	TObjectPtr<URPGAsyncAction_WaitForExperienceReady> PendingWait;
	UPROPERTY(Transient)
	TObjectPtr<const URPGExperienceDefinition> ReadyExperience;
	UPROPERTY(Transient)
	ERPGExperienceWaitState WaitState = ERPGExperienceWaitState::Idle;

	FString LastReason;
	bool bBeginningPlay = false;
	bool bEndingPlay = false;
	bool bDispatchingResult = false;
};

