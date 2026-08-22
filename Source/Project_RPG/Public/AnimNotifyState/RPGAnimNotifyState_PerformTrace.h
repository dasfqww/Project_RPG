#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "Type/RPGEnumTypes.h"
#include "RPGAnimNotifyState_PerformTrace.generated.h"

class ARPGWeaponBase;

/** D1 trace tuning retained as a redirectable serialized struct. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGWeaponTraceParams
{
	GENERATED_BODY()

	/** Maximum linear or angular edge travel represented by one sweep. */
	UPROPERTY(EditAnywhere, Category = "Trace", meta = (ClampMin = "1.0"))
	float TargetDistance = 20.0f;

	/** Used to measure blade travel; the collision box center is the safe fallback. */
	UPROPERTY(EditAnywhere, Category = "Trace")
	FName TraceSocketName = TEXT("TraceSocket");
};

/** D1 debug settings retained as a redirectable serialized struct. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGWeaponTraceDebugParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Trace")
	bool bDrawDebugShape = false;

	UPROPERTY(EditAnywhere, Category = "Trace")
	FColor TraceColor = FColor::Red;

	UPROPERTY(EditAnywhere, Category = "Trace")
	FColor HitColor = FColor::Green;
};

/**
 * D1-compatible weapon sweep notify state.
 *
 * Runtime data is keyed by skeletal mesh because notify objects are shared by
 * every actor playing an animation. Keeping the previous transform on the
 * notify object itself would mix traces from different network pawns.
 */
UCLASS(meta = (DisplayName = "Perform Weapon Trace"))
class PROJECT_RPG_API URPGAnimNotifyState_PerformTrace : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	URPGAnimNotifyState_PerformTrace(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComponent,
		UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComponent,
		UAnimSequenceBase* Animation, float FrameDeltaTime,
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComponent,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

public:
	/** Property names intentionally match D1 for montage serialization. */
	UPROPERTY(EditAnywhere, Category = "Trace")
	EWeaponHandType WeaponHandType = EWeaponHandType::LeftHand;

	/** Authority is the safe default: damage decisions never trust client hits. */
	UPROPERTY(EditAnywhere, Category = "Trace")
	TEnumAsByte<ENetRole> ExecuteNetRole = ROLE_Authority;

	UPROPERTY(EditAnywhere, Category = "Trace")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, Category = "Trace")
	FRPGWeaponTraceParams TraceParams;

	UPROPERTY(EditAnywhere, Category = "Trace")
	FRPGWeaponTraceDebugParams TraceDebugParams;

private:
	struct FRuntimeState
	{
		TWeakObjectPtr<ARPGWeaponBase> WeaponActor;
		TSet<TWeakObjectPtr<AActor>> HitActors;
		FTransform PreviousCollisionTransform = FTransform::Identity;
		FVector PreviousSampleLocation = FVector::ZeroVector;
	};

	bool ShouldExecute(const USkeletalMeshComponent* MeshComponent) const;
	ARPGWeaponBase* ResolveWeapon(const USkeletalMeshComponent* MeshComponent) const;
	FVector ResolveSampleLocation(const ARPGWeaponBase* WeaponActor) const;
	void PerformTrace(USkeletalMeshComponent* MeshComponent, FRuntimeState& State);
	void RemoveStaleStates();

	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FRuntimeState> RuntimeStates;
};
