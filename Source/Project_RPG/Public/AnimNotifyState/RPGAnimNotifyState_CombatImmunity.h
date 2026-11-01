#pragma once
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Combat/RPGCombatImmunityTypes.h"
#include "RPGAnimNotifyState_CombatImmunity.generated.h"

/** Author on the GAS montage itself. Server montage position, rather than callback pairing, owns the window. */
UCLASS(NotBlueprintable, meta=(DisplayName="RPG Combat Immunity Window"))
class PROJECT_RPG_API URPGAnimNotifyState_CombatImmunity final : public UAnimNotifyState
{
	GENERATED_BODY()
public:
	URPGAnimNotifyState_CombatImmunity();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Immunity")
	FRPGCombatImmunitySettings Immunities;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("RPG Combat Immunity Window"); }
	virtual void NotifyBegin(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, float Duration, const FAnimNotifyEventReference& Reference) override;
	virtual void NotifyTick(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, float DeltaTime, const FAnimNotifyEventReference& Reference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& Reference) override;
#if WITH_EDITOR
	virtual bool CanBePlaced(UAnimSequenceBase* Animation) const override;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
