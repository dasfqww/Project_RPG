#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/RPGHitReactionTypes.h"
#include "RPGCombatReactionComponent.generated.h"

class UAnimInstance;
class URPGHitReactionProfile;

/** Shared Player/NPC receiver. A presentation profile declares cosmetic support.
 * Duration effects, control locks and forced movement remain outside this increment. */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGCombatReactionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGCombatReactionComponent();

	/** Native callers invoke only after their existing server damage approval succeeds.
	 * Unsupported means no profile; it never grants permission to interrupt gameplay.
	 * ApprovedHitId identifies this accepted hit, including its target and hit window;
	 * reuse it when redelivering the same outcome, not for another multi-hit pulse. */
	ERPGHitReactionRoute PresentApprovedDamageHit(AActor* Source, const FHitResult& ServerHit,
		const FGuid& ApprovedHitId);

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "RPG|Reaction|Presentation")
	TObjectPtr<URPGHitReactionProfile> PresentationProfile;

private:
	/** This is a transient signal: no late-join replay, no client-to-server entry point. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPresentHit(uint32 Sequence, ERPGHitReactionDirection Direction,
		UAnimMontage* Montage, FName Section, double ExpiresAtServerTime, uint32 RecoveryRevision);

	bool IsLivingReceiver() const;
	bool ReadServerTime(double& OutTime) const;
	uint32 GetRecoveryRevision() const;
	void StopOwnedPresentation();
	static bool IsSafeCosmeticMontage(const UAnimMontage* Montage, FName Section);

	uint32 ServerSequence = 0;
	uint32 LastReceivedSequence = 0;
	bool bHasReceivedSequence = false;
	TArray<FGuid> RecentApprovedHits;
	TWeakObjectPtr<UAnimInstance> PresentationAnimInstance;
	TWeakObjectPtr<UAnimMontage> PresentationMontage;
	int32 PresentationInstanceId = INDEX_NONE;
	uint32 PresentationRecoveryRevision = 0;

	friend struct FRPGHitReactionTestAccess;
	friend class URPGBossMapE2EProbeComponent;
	friend class URPGCombatControlComponent;
};
