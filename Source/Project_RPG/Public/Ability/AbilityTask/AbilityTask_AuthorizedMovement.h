#pragma once

#include "Abilities/Tasks/AbilityTask.h"
#include "CoreMinimal.h"
#include "Security/RPGSecurityTypes.h"
#include "AbilityTask_AuthorizedMovement.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRPGAuthorizedMovementTaskDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGAuthorizedMovementRejectedDelegate,
	FText,
	Reason);

/**
 * Brackets a Blueprint-authored dash, leap, teleport, or forced movement.
 *
 * Run the montage/root-motion/movement logic from OnAuthorized. The task grants
 * only a finite server budget and automatically revokes it on completion or
 * ability cancellation.
 */
UCLASS()
class PROJECT_RPG_API UAbilityTask_AuthorizedMovement : public UAbilityTask
{
	GENERATED_BODY()

public:
	/** Uses the active RPGSkillDefinition profile; this is the preferred skill-BP node. */
	UFUNCTION(BlueprintCallable, Category = "RPG|AbilityTasks|Security",
		meta = (DisplayName = "Authorized Skill Movement Window",
			HidePin = "OwningAbility",
			DefaultToSelf = "OwningAbility",
			BlueprintInternalUseOnly = "true"))
	static UAbilityTask_AuthorizedMovement* AuthorizedSkillMovementWindow(
		UGameplayAbility* OwningAbility);

	UFUNCTION(BlueprintCallable, Category = "RPG|AbilityTasks|Security",
		meta = (DisplayName = "Authorized Movement Window",
			HidePin = "OwningAbility",
			DefaultToSelf = "OwningAbility",
			BlueprintInternalUseOnly = "true"))
	static UAbilityTask_AuthorizedMovement* AuthorizedMovementWindow(
		UGameplayAbility* OwningAbility,
		const FRPGSkillMovementSecurityProfile& MovementProfile);

	virtual void Activate() override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

	UPROPERTY(BlueprintAssignable)
	FRPGAuthorizedMovementTaskDelegate OnAuthorized;

	UPROPERTY(BlueprintAssignable)
	FRPGAuthorizedMovementTaskDelegate OnFinished;

	UPROPERTY(BlueprintAssignable)
	FRPGAuthorizedMovementTaskDelegate OnCancelled;

	UPROPERTY(BlueprintAssignable)
	FRPGAuthorizedMovementRejectedDelegate OnRejected;

private:
	void FinishWindow();
	void RevokeAuthorization();
	void Reject(const FText& Reason);

	FRPGSkillMovementSecurityProfile CachedProfile;
	FTimerHandle FinishTimerHandle;
	FName EffectiveReason = NAME_None;
	bool bAuthorizationApplied = false;
	bool bStarted = false;
	bool bResolved = false;
};
