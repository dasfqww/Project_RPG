#pragma once

#include "CoreMinimal.h"
#include "Engine/CancellableAsyncAction.h"
#include "TimerManager.h"
#include "RPGAsyncAction_WaitForExperienceReady.generated.h"

class AGameStateBase;
class URPGExperienceDefinition;
class URPGExperienceManagerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRPGExperienceWaitResult,
	const URPGExperienceDefinition*, Experience, const FString&, Reason);

/** Waits for this world's Experience without changing selection or cancelling its loading. */
UCLASS(BlueprintType, meta = (ExposedAsyncProxy = AsyncAction))
class PROJECT_RPG_API URPGAsyncAction_WaitForExperienceReady : public UCancellableAsyncAction
{
	GENERATED_BODY()
public:
	/**
	 * Creates a one-shot waiter. C++ callers bind delegates, then call Activate.
	 * TimeoutSeconds covers GameState arrival, selection replication and Experience loading.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Experience",
		meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true",
			DisplayName = "Wait For RPG Experience Ready", AdvancedDisplay = "TimeoutSeconds"))
	static URPGAsyncAction_WaitForExperienceReady* WaitForRPGExperienceReady(
		UObject* WorldContextObject, float TimeoutSeconds = 65.f);

	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;
	virtual bool ShouldBroadcastDelegates() const override;

	/** Experience assets and feature actions are ready. Does not guarantee that a HUD or Pawn exists. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Experience")
	FRPGExperienceWaitResult Ready;

	/** Loading failure, missing manager, or this waiter's timeout. Experience is null. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Experience")
	FRPGExperienceWaitResult Failed;

	/** The bound world or manager ended. Explicit Cancel is silent, as on other cancellable actions. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Experience")
	FRPGExperienceWaitResult Cancelled;

private:
	enum class EResult : uint8 { Ready, Failed, Cancelled };

	void HandleGameStateSet(AGameStateBase* GameState);
	void EvaluateManager();
	void HandleManagerEnded();
	void HandleTimeout();
	void Finish(EResult Result, const URPGExperienceDefinition* Experience, const FString& Reason);
	void UnbindManager();
	void Cleanup();

	UFUNCTION()
	void HandleLoaded(const URPGExperienceDefinition* Experience);
	UFUNCTION()
	void HandleFailed(const FString& Reason);

	TWeakObjectPtr<UWorld> WaitingWorld;
	TWeakObjectPtr<URPGExperienceManagerComponent> BoundManager;
	FDelegateHandle GameStateSetHandle;
	FDelegateHandle WorldCleanupHandle;
	FDelegateHandle ManagerEndedHandle;
	FTimerHandle TimeoutHandle;
	float TimeoutSeconds = 65.f;
	bool bActivated = false;
	bool bCompleted = false;
};
