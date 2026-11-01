#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "RPGCharacterMovementComponent.generated.h"

class UAnimInstance;

/** Shared Player/NPC voluntary movement gate. Never restores a cached movement mode. */
UCLASS()
class PROJECT_RPG_API URPGCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	URPGCharacterMovementComponent();
	/** A cast owns its constraint; zero locks voluntary translation, positive values cap its speed. */
	FGuid BeginSkillMovementConstraint(UObject* Source, float SpeedScalar);
	void EndSkillMovementConstraint(FGuid Handle);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Scoped base walking-speed limit; modifiers are evaluated afterwards, never cached/restored. */
	FGuid BeginWalkSpeedLimit(float BaseSpeed);
	void EndWalkSpeedLimit(FGuid Handle);
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
	virtual bool CanAttemptJump() const override;
	virtual FVector ConstrainInputAcceleration(const FVector& InputAcceleration) const override;
	virtual void PerformMovement(float DeltaTime) override;
	virtual void RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) override;
	virtual void RequestPathMove(const FVector& MoveInput) override;
	virtual void PhysicsRotation(float DeltaTime) override;
	/** Server-measured displacement actually applied by based movement; not an allowance or client input. */
	FVector GetServerBasedMovementTotal() const { return ServerBasedMovementTotal; }
protected:
	virtual void UpdateBasedMovement(float DeltaSeconds) override;
	virtual void ApplyRootMotionToVelocity(float DeltaTime) override;
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.f, const FVector& MoveDelta = FVector::ZeroVector) override;
private:
	struct FSkillMovementConstraint
	{
		TWeakObjectPtr<UObject> Source;
		float SpeedScalar = 1.f;
	};
	float GetSkillMovementScalar() const;
	bool IsVoluntaryMovementBlocked() const;
	void UpdateSkillMovementRootMotion();
	void RestoreSkillRootMotionMode();
	UFUNCTION()
	void OnRep_SkillMovementActive();
	UPROPERTY(ReplicatedUsing = OnRep_SkillMovementActive)
	bool bReplicatedSkillMovementActive = false;
	TMap<FGuid, FSkillMovementConstraint> SkillMovementConstraints;
	TWeakObjectPtr<UAnimInstance> SkillRootMotionInstance;
	uint8 SavedSkillRootMotionMode = 0;
	TMap<FGuid, float> WalkSpeedLimits;
	FVector ServerBasedMovementTotal = FVector::ZeroVector;
};
