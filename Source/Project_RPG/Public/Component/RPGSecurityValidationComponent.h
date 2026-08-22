#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Security/RPGSecurityTypes.h"
#include "RPGSecurityValidationComponent.generated.h"

class UGameplayAbility;
class URPGSecurityPolicy;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGSecurityViolationReported,
	const FRPGSecurityViolation&,
	Violation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGSecurityRiskThresholdExceeded,
	float,
	RiskScore);

/**
 * Server-only gameplay security monitor for one player avatar.
 *
 * UE CharacterMovement remains the authority and performs corrections. This
 * component adds project-level anomaly scoring, ability flood rejection, and
 * reusable combat validation. It never accepts security state from a client.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGSecurityValidationComponent final
	: public UActorComponent
{
	GENERATED_BODY()

public:
	URPGSecurityValidationComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Non-mutating admission check; accepted activations are recorded separately. */
	bool CanAcceptAbilityActivation(
		const UClass* AbilityClass,
		FString& OutReason) const;
	void RecordAbilityActivation(const UClass* AbilityClass);

	bool ValidateCombatHit(
		AActor* TargetActor,
		const FHitResult& HitResult,
		float MaximumDistance,
		float HitLocationTolerance,
		FString& OutReason);
	bool ValidateDamage(float Damage, FString& OutReason);

	void ReportInvalidTargetData(const FString& Detail);
	void ReportViolation(
		ERPGSecurityViolationType Type,
		ERPGSecurityViolationSeverity Severity,
		float Score,
		const FString& Detail);

	/** Call before a legitimate server teleport, dash, or forced displacement. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Security")
	void AuthorizeMovementDiscontinuity(
		float DurationSeconds,
		float ExtraDistance,
		FName Reason);

	/** Ends only the matching movement window, so overlapping tasks do not cancel each other. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Security")
	void CancelMovementAuthorization(
		FName Reason,
		bool bResetBaseline = false);

	/** GameMode may swap PvE/PvP policies before play starts. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Security")
	void SetSecurityPolicy(URPGSecurityPolicy* NewPolicy);

	UFUNCTION(BlueprintPure, Category = "RPG|Security")
	URPGSecurityPolicy* GetSecurityPolicy() const { return SecurityPolicy; }

	UFUNCTION(BlueprintPure, Category = "RPG|Security")
	FRPGSecurityPolicyConfig GetEffectivePolicyConfig() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Security")
	void ResetMovementBaseline();

	UFUNCTION(BlueprintPure, Category = "RPG|Security")
	float GetRiskScore() const { return RiskScore; }

	UFUNCTION(BlueprintPure, Category = "RPG|Security")
	int32 GetTotalViolationCount() const { return TotalViolationCount; }

	UFUNCTION(BlueprintPure, Category = "RPG|Security|Movement")
	bool IsMovementAuthorizationActive() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Security|Movement")
	float GetRemainingAuthorizedMovementDistance() const
	{
		return AuthorizedExtraDistance;
	}

	UFUNCTION(BlueprintPure, Category = "RPG|Security|Movement")
	FName GetAuthorizedMovementReason() const { return AuthorizedMovementReason; }

	UPROPERTY(BlueprintAssignable, Category = "RPG|Security")
	FRPGSecurityViolationReported OnViolationReported;

	/** Bind server-side enforcement here; automatic kicks are intentionally opt-in. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Security")
	FRPGSecurityRiskThresholdExceeded OnRiskThresholdExceeded;

private:
	double GetServerTimeSeconds() const;
	void SampleMovement();
	void DecayRiskScore(float DeltaTime);
	void PruneAbilityActivationHistory(double Now);
	bool IsAuthorityOwner() const;
	const FRPGSecurityPolicyConfig& GetPolicyConfigRef() const;

	/** Assign DA_SecurityPolicy on the inherited player Blueprint component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Security|Policy",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URPGSecurityPolicy> SecurityPolicy;

	/** Used when no DataAsset is assigned, keeping native/test pawns functional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Security|Policy",
		meta = (AllowPrivateAccess = "true", ShowOnlyInnerProperties))
	FRPGSecurityPolicyConfig FallbackPolicy;

	bool bHasMovementBaseline = false;
	bool bRiskThresholdBroadcast = false;
	int32 ConsecutiveSpeedViolationSamples = 0;
	int32 TotalViolationCount = 0;
	float RiskScore = 0.0f;
	double SpawnGraceEndsAt = 0.0;
	double AuthorizedMovementEndsAt = 0.0;
	float AuthorizedExtraDistance = 0.0f;
	FName AuthorizedMovementReason;
	FRPGMovementSecuritySample PreviousMovementSample;
	TArray<double> AbilityActivationHistory;
	TMap<FName, double> LastAbilityActivationByClass;
	TMap<ERPGSecurityViolationType, double> LastLogTimeByType;
};
